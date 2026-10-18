// Runs all keyboard I/O on one background thread so the window stays responsive (a single write
// can take ~2 s while the firmware commits it). Results and errors are delivered on the GTK main
// loop, in the order the jobs were posted.
#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "keyboard.hpp"

namespace k86gui {

// Posts `f` to the GTK main loop (FIFO with other posts).
void runOnMain(std::function<void()> f);

struct OpenedDevice {
    std::unique_ptr<k86::Transport> transport;  // the device (or simulator)
    std::unique_ptr<k86::Transport> wrapper;    // optional guard in front of it (read-only mode)
    std::unique_ptr<k86::Keyboard> keyboard;
    std::string description;
};

class Worker {
public:
    // Progress reporting from inside a job (thread safe; forwarded to the main loop).
    using ProgressFn = std::function<void(double fraction, const std::string& text)>;
    using Job = std::function<void(k86::Keyboard&, const ProgressFn&)>;

    Worker();
    ~Worker();

    // Opens a device on the worker thread, replacing the current one. `done` runs on the main
    // loop with the model (nullptr on failure) and a description or error message.
    void open(std::function<OpenedDevice()> opener,
              std::function<void(const k86::DeviceModel*, const std::string&)> done);
    void close();

    // Queues a job. `ok` / `fail` run on the main loop afterwards.
    void post(const std::string& what, Job job, std::function<void()> ok = {},
              std::function<void(const std::string&)> fail = {});
    // Drops queued (not yet started) jobs, e.g. when a newer write supersedes them.
    void dropQueued(const std::string& what);

    bool connected() const { return connected_; }
    int pending() const { return pending_; }

    // Main loop notifications.
    std::function<void(int pending, const std::string& what)> onBusy;
    std::function<void(double fraction, const std::string& text)> onProgress;
    std::function<void(const std::string& what, const std::string& error)> onError;  // when no fail handler
    std::function<void()> onLost;  // the device disappeared

private:
    struct Task {
        std::string what;
        std::function<void()> run;
    };
    void loop();
    void enqueue(Task t);
    void finished(const std::string& what);

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Task> queue_;
    bool stop_ = false;

    // Worker thread only.
    OpenedDevice dev_;

    // Main thread only.
    bool connected_ = false;
    int pending_ = 0;
};

}  // namespace k86gui
