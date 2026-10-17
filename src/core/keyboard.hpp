// High level K86 keyboard API. Two firmware families exist under the "K86" name in the
// original driver; they share most features but use different command numbers:
//
//   K86 (YC3121)  VID 0x3151 PID 0x4015  firmware id 1168  ("yc3121_sg8967_3m_soc")
//   K86 (YC3123)  VID 0x3151 PID 0x5002  firmware id 2730  ("yc3123_sg_sg8967_3m_1k")
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "keycodes.hpp"
#include "macro.hpp"
#include "protocol.hpp"

namespace k86 {

constexpr int kSlots = 128;
using KeyMatrix = std::array<uint8_t, kSlots * 4>;
using Rgb = uint32_t;  // 0xRRGGBB

enum class Chip { YC3121, YC3123 };

struct LightModeInfo {
    std::string name;  // CLI name, e.g. "wave"
    std::string original;  // name in the original software, e.g. "LightWave"
    uint8_t code;
    int options;  // number of selectable options (directions / patterns), 0 if none
    bool speed;   // has a speed setting
    bool color;   // supports a fixed colour / rainbow
};

struct DeviceModel {
    std::string displayName;
    std::string firmwareName;
    uint16_t vid, pid;
    uint32_t firmwareId;
    Chip chip;
    int layers;  // key-map profiles
    const uint8_t* defaultMatrix;
    const uint8_t* defaultFnMatrix;
    std::vector<LightModeInfo> lightModes;
    std::vector<LightModeInfo> sideLightModes;  // empty if no side light
    int maxBrightness, maxSpeed, maxSideSpeed;
    int screenWidth, screenHeight, screenSlots;
    bool canSyncSystemInfo, canSwitchLanguage;
    int sleepMin, sleepMax, deepSleepMin, deepSleepMax;  // seconds
    int vendorSleepMs;  // pause the original software inserts after write commands
};

const std::vector<DeviceModel>& knownModels();
const DeviceModel* findModel(uint16_t vid, uint16_t pid, std::optional<uint32_t> firmwareId = std::nullopt);

struct LightSetting {
    uint8_t mode = 1;  // LightModeInfo::code
    uint8_t option = 0;
    uint8_t brightness = 4;
    uint8_t speed = 2;
    Rgb color = 0xFF0000;
    bool rainbow = false;  // "dazzle" (multi-colour) mode
};

struct SleepTimes {
    uint16_t bluetooth = 120, wireless24 = 120;          // seconds until sleep
    uint16_t deepBluetooth = 1680, deepWireless24 = 1680;  // seconds until deep sleep
};

struct Versions {
    std::optional<uint16_t> usb, rf, mled, oled, flash;
};

struct SystemInfo {
    double diskAvailableBytes = 0, diskTotalBytes = 0, memUsedBytes = 0, memTotalBytes = 0;
    int cpuUsagePercent = 0, cpuTemperatureC = 0;
    double netTotalUpBytes = 0, netTotalDownBytes = 0;
};

struct ScreenImage {
    std::vector<uint8_t> data;  // RGB565 big-endian, column major, cropped to the rectangle
    uint16_t left = 0, top = 0, right = 0, bottom = 0;
};

using Progress = std::function<void(double)>;

class Keyboard {
public:
    Keyboard(Transport& t, const DeviceModel& model, Session::SleepFn sleeper = {});
    virtual ~Keyboard() = default;

    const DeviceModel& model() const { return model_; }
    Session& session() { return s_; }

    // Firmware device id (command 0x8F); identifies the exact model behind a VID/PID.
    std::optional<uint32_t> readDeviceId();
    virtual Versions readVersions() = 0;

    virtual void setProfile(uint8_t p) = 0;
    virtual std::optional<uint8_t> getProfile() = 0;
    virtual void setDebounce(uint8_t ms) = 0;
    virtual std::optional<uint8_t> getDebounce() = 0;
    virtual void setSleep(const SleepTimes& t) = 0;
    virtual std::optional<SleepTimes> getSleep() = 0;

    virtual void setLight(const LightSetting& l);
    virtual std::optional<LightSetting> getLight();
    virtual void setSideLight(const LightSetting& l);
    virtual std::optional<LightSetting> getSideLight();

    // Chip specific option flags as name -> value (see docs/PROTOCOL.md).
    virtual std::map<std::string, int> getOptions() = 0;
    // Read-modify-write of the given option names. Throws on unknown names.
    virtual void setOptions(const std::map<std::string, int>& changes) = 0;

    virtual KeyMatrix getKeyMatrix(uint8_t layer) = 0;
    virtual void setKeyMatrix(uint8_t layer, const KeyMatrix& m) = 0;
    virtual void setKey(uint8_t layer, uint8_t slot, const KeyAction& a) = 0;
    // Fn layer; `os` is 0 = Windows, 1 = macOS (only the YC3123 firmware has per-OS Fn layers).
    virtual KeyMatrix getFnMatrix(uint8_t layer, uint8_t os) = 0;
    virtual void setFnMatrix(uint8_t layer, uint8_t os, const KeyMatrix& m) = 0;
    virtual void setFnKey(uint8_t layer, uint8_t os, uint8_t slot, const KeyAction& a) = 0;

    Macro getMacro(uint8_t index);
    void setMacro(uint8_t index, const Macro& m);

    // Per-key colours for the "custom" light mode, indexed by key slot.
    virtual std::array<Rgb, kSlots> getUserPicture(uint8_t layer) = 0;
    virtual void setUserPicture(uint8_t layer, const std::array<Rgb, kSlots>& colors) = 0;
    // Per-key animation upload.
    virtual void startUserAnimation() = 0;
    virtual void sendUserAnimationFrame(uint8_t frame, uint8_t frameCount, uint16_t delay,
                                        const std::array<Rgb, kSlots>& colors) = 0;

    // LCD: announce an upload, then stream each frame (identical on both chips).
    bool screenBegin(uint8_t frame, uint8_t frameCount, uint8_t delay, const ScreenImage& first);
    void screenSendFrame(uint8_t frame, uint8_t frameCount, uint8_t delay, const ScreenImage& img,
                         const Progress& progress = {});
    void syncClock();
    void sendSystemInfo(const SystemInfo& info);
    void switchScreenLanguage(bool value = true);
    bool eraseScreenFlash();

    virtual void factoryReset() = 0;

    const KeyMatrix& defaultMatrix() const { return defaults_; }
    const KeyMatrix& defaultFnMatrix() const { return fnDefaults_; }

protected:
    void vendorSleep(int ms = -1) { s_.sleep(ms < 0 ? model_.vendorSleepMs : ms); }
    // Send `data` in 56-byte chunks, each prefixed by an 8-byte header built by `header(page, len)`.
    void sendPaged(const std::vector<uint8_t>& data, int pages,
                   const std::function<std::vector<uint8_t>(int page, size_t len)>& header,
                   int preDelayMs = 10);
    std::vector<uint8_t> readPaged(const std::function<std::vector<uint8_t>(int page)>& cmd, int pages);

    virtual uint8_t cmdSetMacro() const = 0;
    virtual uint8_t maxSpeed() const = 0;
    // The original driver's "dazzle"/"normal" colour-type nibbles (identical on both chips).
    static constexpr uint8_t kDazzle = 8, kNormal = 7;

    Session s_;
    const DeviceModel& model_;
    KeyMatrix defaults_{}, fnDefaults_{};
};

// Opens the right implementation for the model.
std::unique_ptr<Keyboard> makeKeyboard(Transport& t, const DeviceModel& model, Session::SleepFn sleeper = {});

// Slot helpers based on a model's default matrix.
std::string slotName(const KeyMatrix& defaults, int slot);  // e.g. "A", "Mute", "Slot104"
std::optional<int> findSlot(const KeyMatrix& defaults, const std::string& name);

const LightModeInfo* findLightMode(const std::vector<LightModeInfo>& modes, const std::string& name);
const LightModeInfo* findLightMode(const std::vector<LightModeInfo>& modes, uint8_t code);

}  // namespace k86
