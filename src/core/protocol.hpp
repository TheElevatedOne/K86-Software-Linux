// Packet framing used by the original driver (see docs/PROTOCOL.md).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "hidraw.hpp"

namespace k86 {

// Mirrors the driver's checksum enum (Bit7 = 0, Bit8 = 1, None = 2).
enum class Checksum { Bit7, Bit8, None };

// Builds the 64-byte feature report exactly like the original `sendFeature`:
//  - the message is zero padded to at least 8 bytes,
//  - Bit7: byte[7] = 255 - (sum(byte[0..6]) & 0xFF)
//  - Bit8: byte[8] = 255 - (sum(byte[0..7]) & 0xFF)
//  - the result is zero padded / truncated to 64 bytes.
Report frame(const std::vector<uint8_t>& msg, Checksum cs);

// Command/response session with the inter-command delays used by the original software.
class Session {
public:
    using SleepFn = std::function<void(int ms)>;

    explicit Session(Transport& t, SleepFn sleeper = {});

    // Send one command. `preDelayMs` matches the driver's default 10 ms pre-send delay.
    void send(const std::vector<uint8_t>& msg, Checksum cs, int preDelayMs = 10);
    // Read the pending response (feature report). `preDelayMs` defaults to 10 ms.
    Report read(int preDelayMs = 10);
    // send() followed by read() (the driver's `commonMsg`).
    Report query(const std::vector<uint8_t>& msg, Checksum cs, int sendDelayMs = 10, int readDelayMs = 10);
    void sleep(int ms);

private:
    Transport& t_;
    SleepFn sleep_;
};

}  // namespace k86
