#include "worker.hpp"

#include <gtk/gtk.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace k86gui {

void runOnMain(std::function<void()> f) {
    auto* p = new std::function<void()>(std::move(f));
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        [](gpointer d) -> gboolean {
            (*static_cast<std::function<void()>*>(d))();
            return G_SOURCE_REMOVE;
        },
        p, [](gpointer d) { delete static_cast<std::function<void()>*>(d); });
}

namespace {

bool looksLikeDeviceLoss(const std::string& e) {
    return e.find(std::strerror(ENODEV)) != std::string::npos || e.find(std::strerror(EIO)) != std::string::npos ||
           e.find(std::strerror(EPIPE)) != std::string::npos;
}

}  // namespace

Worker::Worker() : thread_([this] { loop(); }) {}

Worker::~Worker() {
    {
        std::lock_guard<std::mutex> g(mu_);
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    thread_.join();
}

void Worker::loop() {
    for (;;) {
        Task t;
        {
            std::unique_lock<std::mutex> g(mu_);
            cv_.wait(g, [this] { return stop_ || !queue_.empty(); });
            if (stop_) break;
            t = std::move(queue_.front());
            queue_.pop_front();
        }
        t.run();
    }
    dev_ = {};
}

void Worker::enqueue(Task t) {
    ++pending_;
    if (onBusy) onBusy(pending_, t.what);
    {
        std::lock_guard<std::mutex> g(mu_);
        queue_.push_back(std::move(t));
    }
    cv_.notify_one();
}

void Worker::finished(const std::string& what) {
    pending_ = std::max(0, pending_ - 1);
    if (onBusy) onBusy(pending_, pending_ ? what : std::string());
}

void Worker::open(std::function<OpenedDevice()> opener,
                  std::function<void(const k86::DeviceModel*, const std::string&)> done) {
    connected_ = false;
    enqueue({"Connecting", [this, opener, done] {
                 dev_ = {};
                 const k86::DeviceModel* model = nullptr;
                 std::string text;
                 try {
                     dev_ = opener();
                     model = &dev_.keyboard->model();
                     text = dev_.description;
                 } catch (const std::exception& e) {
                     dev_ = {};
                     text = e.what();
                 }
                 runOnMain([this, model, text, done] {
                     connected_ = model != nullptr;
                     finished("Connecting");
                     done(model, text);
                 });
             }});
}

void Worker::close() {
    connected_ = false;
    enqueue({"Disconnecting", [this] {
                 dev_ = {};
                 runOnMain([this] { finished("Disconnecting"); });
             }});
}

void Worker::dropQueued(const std::string& what) {
    int dropped = 0;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto it = std::remove_if(queue_.begin(), queue_.end(), [&](const Task& t) { return t.what == what; });
        dropped = int(queue_.end() - it);
        queue_.erase(it, queue_.end());
    }
    pending_ = std::max(0, pending_ - dropped);
}

void Worker::post(const std::string& what, Job job, std::function<void()> ok,
                  std::function<void(const std::string&)> fail) {
    enqueue({what, [this, what, job, ok, fail] {
                 std::string error;
                 if (!dev_.keyboard) {
                     error = "no keyboard connected";
                 } else {
                     ProgressFn progress = [this](double f, const std::string& text) {
                         runOnMain([this, f, text] {
                             if (onProgress) onProgress(f, text);
                         });
                     };
                     try {
                         job(*dev_.keyboard, progress);
                     } catch (const std::exception& e) {
                         error = e.what();
                     }
                 }
                 bool lost = !error.empty() && looksLikeDeviceLoss(error);
                 if (lost) dev_ = {};
                 runOnMain([this, what, ok, fail, error, lost] {
                     finished(what);
                     if (lost) {
                         connected_ = false;
                         if (onLost) onLost();
                         return;
                     }
                     if (error.empty()) {
                         if (ok) ok();
                     } else if (fail) {
                         fail(error);
                     } else if (onError) {
                         onError(what, error);
                     }
                 });
             }});
}

}  // namespace k86gui
