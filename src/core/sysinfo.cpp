#include "sysinfo.hpp"

#include <dirent.h>
#include <sys/statvfs.h>

#include <fstream>
#include <sstream>
#include <string>

namespace k86 {

namespace {

std::string readLine(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> out;
    if (DIR* d = opendir(path.c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') out.push_back(e->d_name);
        closedir(d);
    }
    return out;
}

// Prefers the CPU package sensor (coretemp / k10temp / zenpower), then a thermal zone.
int cpuTemperature() {
    for (const auto& h : listDir("/sys/class/hwmon")) {
        std::string base = "/sys/class/hwmon/" + h;
        std::string name = readLine(base + "/name");
        if (name == "coretemp" || name == "k10temp" || name == "zenpower" || name == "cpu_thermal") {
            std::string v = readLine(base + "/temp1_input");
            if (!v.empty()) return std::stoi(v) / 1000;
        }
    }
    int fallback = -1;
    for (const auto& z : listDir("/sys/class/thermal")) {
        if (z.rfind("thermal_zone", 0) != 0) continue;
        std::string base = "/sys/class/thermal/" + z;
        std::string v = readLine(base + "/temp");
        if (v.empty()) continue;
        int t = std::stoi(v) / 1000;
        std::string type = readLine(base + "/type");
        if (type == "x86_pkg_temp" || type.find("cpu") != std::string::npos) return t;
        if (fallback < 0) fallback = t;
    }
    return fallback < 0 ? 0 : fallback;
}

}  // namespace

SystemInfo SystemInfoCollector::sample() {
    SystemInfo s;
    struct statvfs vfs{};
    if (statvfs("/", &vfs) == 0) {
        s.diskAvailableBytes = double(vfs.f_bavail) * vfs.f_frsize;
        s.diskTotalBytes = double(vfs.f_blocks) * vfs.f_frsize;
    }

    std::ifstream mem("/proc/meminfo");
    std::string key;
    double kb;
    std::string unit;
    double total = 0, avail = 0;
    while (mem >> key >> kb) {
        std::getline(mem, unit);
        if (key == "MemTotal:") total = kb * 1024;
        if (key == "MemAvailable:") avail = kb * 1024;
    }
    s.memTotalBytes = total;
    s.memUsedBytes = total - avail;

    std::ifstream stat("/proc/stat");
    std::string cpu;
    unsigned long long v[10] = {};
    stat >> cpu;
    for (auto& x : v) stat >> x;
    unsigned long long idle = v[3] + v[4], tot = 0;
    for (auto x : v) tot += x;
    if (prevTotal_ && tot > prevTotal_) {
        double dt = double(tot - prevTotal_), di = double(idle - prevIdle_);
        s.cpuUsagePercent = int((1.0 - di / dt) * 100.0 + 0.5);
    }
    prevIdle_ = idle, prevTotal_ = tot;

    s.cpuTemperatureC = cpuTemperature();

    std::ifstream net("/proc/net/dev");
    std::string line;
    std::getline(net, line);
    std::getline(net, line);
    while (std::getline(net, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string ifname = line.substr(0, colon);
        ifname.erase(0, ifname.find_first_not_of(' '));
        if (ifname == "lo") continue;
        std::istringstream in(line.substr(colon + 1));
        unsigned long long f[16] = {};
        for (auto& x : f) in >> x;
        s.netTotalDownBytes += double(f[0]);
        s.netTotalUpBytes += double(f[8]);
    }
    return s;
}

}  // namespace k86
