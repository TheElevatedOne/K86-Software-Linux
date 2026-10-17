// Linux hidraw transport for the vendor configuration interface.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace k86 {

constexpr size_t kReportSize = 64;
using Report = std::array<uint8_t, kReportSize>;

struct HidCollection {
    uint16_t usagePage = 0;
    uint16_t usage = 0;
};

struct HidrawInfo {
    std::string path;          // /dev/hidrawN
    uint16_t vid = 0;
    uint16_t pid = 0;
    std::string name;          // HID_NAME from sysfs
    int interfaceNumber = -1;  // bInterfaceNumber, -1 if unknown
    std::vector<HidCollection> collections;  // top-level collections

    bool hasCollection(uint16_t usagePage, uint16_t usage) const;
};

// Enumerates all hidraw nodes via sysfs. Never throws; unreadable nodes are skipped.
std::vector<HidrawInfo> enumerateHidraw();

// Parses a HID report descriptor and returns its top-level application collections.
std::vector<HidCollection> parseTopLevelCollections(const std::vector<uint8_t>& desc);

// Abstract byte transport. The original software talks to the keyboard with
// 64-byte HID feature reports (report ID 0) in both directions.
class Transport {
public:
    virtual ~Transport() = default;
    virtual void sendFeature(const Report& report) = 0;
    virtual Report getFeature() = 0;
};

class HidrawTransport : public Transport {
public:
    explicit HidrawTransport(const std::string& path);
    ~HidrawTransport() override;
    HidrawTransport(const HidrawTransport&) = delete;
    HidrawTransport& operator=(const HidrawTransport&) = delete;

    void sendFeature(const Report& report) override;
    Report getFeature() override;

private:
    int fd_ = -1;
    std::string path_;
};

}  // namespace k86
