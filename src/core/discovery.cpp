#include "discovery.hpp"

#include <stdexcept>

namespace k86 {

std::vector<Candidate> findCandidates() {
    std::vector<Candidate> out;
    for (auto& info : enumerateHidraw()) {
        const DeviceModel* m = findModel(info.vid, info.pid);
        if (!m || !info.hasCollection(kConfigUsagePage, kConfigUsage)) continue;
        out.push_back({info, m});
    }
    return out;
}

OpenKeyboard openKeyboard(const std::string& path, bool force) {
    HidrawInfo info;
    const DeviceModel* model = nullptr;
    auto cands = findCandidates();
    if (path.empty()) {
        if (cands.empty())
            throw std::runtime_error(
                "no K86 configuration interface found (VID 3151, PID 4015/5002, usage page 0xFFFF/2). "
                "Only the USB cable connection is supported.");
        info = cands[0].info;
        model = cands[0].model;
    } else {
        for (auto& c : cands)
            if (c.info.path == path) info = c.info, model = c.model;
        if (!model) {
            for (auto& i : enumerateHidraw())
                if (i.path == path) info = i;
            if (info.path.empty()) throw std::runtime_error(path + " is not a hidraw device");
            model = findModel(info.vid, info.pid);
            if (!model) {
                if (!force) throw std::runtime_error(path + " is not a known K86 interface (use --force to override)");
                model = &knownModels()[0];
            }
        }
    }

    OpenKeyboard ok;
    ok.info = info;
    ok.transport = std::make_unique<HidrawTransport>(info.path);
    ok.keyboard = makeKeyboard(*ok.transport, *model);
    ok.firmwareId = ok.keyboard->readDeviceId();
    if (ok.firmwareId && *ok.firmwareId != model->firmwareId) {
        if (const DeviceModel* exact = findModel(info.vid, info.pid, ok.firmwareId)) {
            ok.keyboard = makeKeyboard(*ok.transport, *exact);
        } else if (!force) {
            throw std::runtime_error("keyboard at " + info.path + " reports firmware id " +
                                     std::to_string(*ok.firmwareId) + ", expected " +
                                     std::to_string(model->firmwareId) +
                                     " for a K86. Another keyboard uses the same USB ids; use --force at your own risk.");
        }
    } else if (!ok.firmwareId && !force) {
        throw std::runtime_error("keyboard at " + info.path + " did not answer the device id query (0x8F)");
    }
    return ok;
}

}  // namespace k86
