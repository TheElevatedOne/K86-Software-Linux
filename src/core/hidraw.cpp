#include "hidraw.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace k86 {

bool HidrawInfo::hasCollection(uint16_t usagePage, uint16_t usage) const {
    return std::any_of(collections.begin(), collections.end(), [&](const HidCollection& c) {
        return c.usagePage == usagePage && c.usage == usage;
    });
}

std::vector<HidCollection> parseTopLevelCollections(const std::vector<uint8_t>& d) {
    std::vector<HidCollection> out;
    uint32_t usagePage = 0;
    uint32_t usage = 0;
    int depth = 0;
    size_t i = 0;
    while (i < d.size()) {
        uint8_t prefix = d[i];
        if (prefix == 0xFE) {  // long item
            if (i + 1 >= d.size()) break;
            i += 3 + d[i + 1];
            continue;
        }
        size_t size = prefix & 0x03;
        if (size == 3) size = 4;
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = (prefix >> 4) & 0x0F;
        if (i + 1 + size > d.size()) break;
        uint32_t value = 0;
        for (size_t b = 0; b < size; ++b) value |= uint32_t(d[i + 1 + b]) << (8 * b);
        i += 1 + size;

        if (type == 1 && tag == 0) {  // Global: Usage Page
            usagePage = value;
        } else if (type == 2 && tag == 0) {  // Local: Usage
            usage = value;
            if (size == 4) {  // extended usage carries its own page
                usagePage = value >> 16;
                usage = value & 0xFFFF;
            }
        } else if (type == 0 && tag == 0x0A) {  // Main: Collection
            if (depth == 0) out.push_back({uint16_t(usagePage), uint16_t(usage)});
            ++depth;
            usage = 0;
        } else if (type == 0 && tag == 0x0C) {  // Main: End Collection
            if (depth > 0) --depth;
        } else if (type == 0) {  // other main items clear local state
            usage = 0;
        }
    }
    return out;
}

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

}  // namespace

std::vector<HidrawInfo> enumerateHidraw() {
    std::vector<HidrawInfo> result;
    DIR* dir = opendir("/sys/class/hidraw");
    if (!dir) return result;
    while (dirent* ent = readdir(dir)) {
        std::string node = ent->d_name;
        if (node.rfind("hidraw", 0) != 0) continue;
        std::string sys = "/sys/class/hidraw/" + node + "/device";
        HidrawInfo info;
        info.path = "/dev/" + node;

        std::istringstream uevent(readFile(sys + "/uevent"));
        std::string line;
        while (std::getline(uevent, line)) {
            if (line.rfind("HID_ID=", 0) == 0) {
                unsigned bus = 0, vid = 0, pid = 0;
                if (sscanf(line.c_str() + 7, "%x:%x:%x", &bus, &vid, &pid) == 3) {
                    info.vid = uint16_t(vid);
                    info.pid = uint16_t(pid);
                }
            } else if (line.rfind("HID_NAME=", 0) == 0) {
                info.name = line.substr(9);
            }
        }
        // The USB interface directory is the parent of the HID device directory.
        std::string ifnum = trim(readFile(sys + "/../bInterfaceNumber"));
        if (!ifnum.empty()) info.interfaceNumber = int(strtol(ifnum.c_str(), nullptr, 16));

        std::string desc = readFile(sys + "/report_descriptor");
        info.collections = parseTopLevelCollections(std::vector<uint8_t>(desc.begin(), desc.end()));
        result.push_back(std::move(info));
    }
    closedir(dir);
    std::sort(result.begin(), result.end(),
              [](const HidrawInfo& a, const HidrawInfo& b) { return a.path < b.path; });
    return result;
}

HidrawTransport::HidrawTransport(const std::string& path) : path_(path) {
    fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        std::string msg = "cannot open " + path + ": " + std::strerror(errno);
        if (errno == EACCES) msg += " (install the udev rule from udev/ or run as root)";
        throw std::runtime_error(msg);
    }
}

HidrawTransport::~HidrawTransport() {
    if (fd_ >= 0) ::close(fd_);
}

void HidrawTransport::sendFeature(const Report& report) {
    // hidraw expects the report ID as the first byte; the device uses unnumbered reports (ID 0).
    uint8_t buf[kReportSize + 1];
    buf[0] = 0;
    std::memcpy(buf + 1, report.data(), kReportSize);
    if (ioctl(fd_, HIDIOCSFEATURE(sizeof buf), buf) < 0)
        throw std::runtime_error("HIDIOCSFEATURE on " + path_ + " failed: " + std::strerror(errno));
}

Report HidrawTransport::getFeature() {
    uint8_t buf[kReportSize + 1] = {};
    int n = ioctl(fd_, HIDIOCGFEATURE(sizeof buf), buf);
    if (n < 0)
        throw std::runtime_error("HIDIOCGFEATURE on " + path_ + " failed: " + std::strerror(errno));
    // For unnumbered reports the kernel keeps byte 0 as the (zero) report ID.
    Report r{};
    std::memcpy(r.data(), buf + 1, kReportSize);
    return r;
}

}  // namespace k86
