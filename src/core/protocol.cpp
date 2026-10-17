#include "protocol.hpp"

#include <chrono>
#include <thread>

namespace k86 {

Report frame(const std::vector<uint8_t>& msg, Checksum cs) {
    std::vector<uint8_t> a = msg;
    if (a.size() < 8) a.resize(8, 0);
    if (cs == Checksum::Bit7) {
        unsigned sum = 0;
        for (int i = 0; i < 7; ++i) sum += a[i];
        a[7] = uint8_t(255 - (sum & 0xFF));
    } else if (cs == Checksum::Bit8) {
        if (a.size() < 9) a.resize(9, 0);
        unsigned sum = 0;
        for (int i = 0; i < 8; ++i) sum += a[i];
        a[8] = uint8_t(255 - (sum & 0xFF));
    }
    Report r{};
    for (size_t i = 0; i < a.size() && i < kReportSize; ++i) r[i] = a[i];
    return r;
}

Session::Session(Transport& t, SleepFn sleeper) : t_(t), sleep_(std::move(sleeper)) {
    if (!sleep_)
        sleep_ = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
}

void Session::sleep(int ms) {
    if (ms > 0) sleep_(ms);
}

void Session::send(const std::vector<uint8_t>& msg, Checksum cs, int preDelayMs) {
    sleep(preDelayMs);
    t_.sendFeature(frame(msg, cs));
}

Report Session::read(int preDelayMs) {
    sleep(preDelayMs);
    return t_.getFeature();
}

Report Session::query(const std::vector<uint8_t>& msg, Checksum cs, int sendDelayMs, int readDelayMs) {
    send(msg, cs, sendDelayMs);
    return read(readDelayMs);
}

}  // namespace k86
