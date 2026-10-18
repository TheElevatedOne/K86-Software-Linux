// Shared state of the k86-gui window. The pages read from `Cache` (filled by the preload on
// connect and updated optimistically on every change) and send changes through `worker`.
#pragma once

#include <gtk/gtk.h>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "keyboard.hpp"
#include "keyboard_view.hpp"
#include "sysinfo.hpp"
#include "worker.hpp"

namespace k86gui {

struct Cache {
    const k86::DeviceModel* model = nullptr;
    k86::Versions versions;
    std::optional<uint32_t> firmwareId;
    std::optional<k86::LightSetting> light, side;
    int profile = 0;        // profile shown and edited in the window (0-based)
    int activeProfile = 0;  // profile the keyboard uses
    std::optional<uint8_t> debounce;
    std::optional<k86::SleepTimes> sleep;
    std::map<std::string, int> options;
    std::vector<k86::KeyMatrix> keymaps;                // [layer]
    std::vector<std::array<k86::KeyMatrix, 2>> fn;      // [layer][os]
    std::vector<std::array<k86::Rgb, k86::kSlots>> pictures;  // [pattern]
    std::vector<std::optional<k86::Macro>> macros;      // [slot]
    bool loaded = false;
};

struct Options {
    std::string dryRun;      // "", "v1" or "v2"
    bool readOnly = false;   // refuse every write (safe mode)
    std::string screenshot;  // render every page to PREFIX-name.png and exit
    std::string page;        // page to show first
    bool selfTest = false;   // drive the widgets against the simulator and check the result
};

struct Page {
    std::string name, title;
    GtkWidget* widget = nullptr;
    std::function<void()> refresh;  // re-read the cache into the widgets
};

struct App {
    Options opt;
    Worker worker;
    Cache cache;
    k86::SystemInfoCollector sysinfo;

    GtkWidget* window = nullptr;
    GtkWidget* stack = nullptr;          // main pages
    GtkWidget* outer = nullptr;          // "connect your keyboard" / pages
    GtkWidget* deviceCombo = nullptr;
    GtkWidget* statusLabel = nullptr;
    GtkWidget* spinner = nullptr;
    GtkWidget* progress = nullptr;
    GtkWidget* profileBox = nullptr;
    std::vector<GtkWidget*> profileButtons;
    GtkWidget* emptyLabel = nullptr;
    std::vector<std::string> devicePaths;
    std::vector<Page> pages;
    guint hotplugTimer = 0;
    bool connecting = false;
    int updating = 0;  // > 0 while widgets are filled from the cache (suppresses writes)
    std::optional<bool> autoConfirm;  // self-test: answer confirmations without a dialog
    int errorCount = 0;               // errors shown so far (checked by the self-test)

    // Writes are refused up front in read-only mode; returns false then.
    bool canWrite();
    void refreshPages();
    void error(const std::string& text);
    void info(const std::string& text);
    bool confirm(const std::string& text);
    std::function<void()> reload;  // read everything from the keyboard again
    void status(const std::string& text);
};

// RAII guard for App::updating.
struct Updating {
    App* app;
    explicit Updating(App* a) : app(a) { ++app->updating; }
    ~Updating() { --app->updating; }
};

// ------------------------------------------------------------------ widget helpers (widgets.cpp)
GtkWidget* vbox(int spacing = 8, int border = 0);
GtkWidget* hbox(int spacing = 6);
GtkWidget* label(const std::string& text, bool dim = false);
GtkWidget* heading(const std::string& text);
GtkWidget* button(const std::string& text, std::function<void()> cb);
GtkWidget* section(const std::string& title, GtkWidget* child);
GtkWidget* scrolledV(GtkWidget* child);
GtkWidget* flowBox();
void onClicked(GtkWidget* w, std::function<void()> cb);
void onChanged(GtkWidget* w, const char* signal, std::function<void()> cb);
void clearContainer(GtkWidget* c);
GdkRGBA toRgba(k86::Rgb c);
k86::Rgb fromRgba(const GdkRGBA& c);
std::string hexColor(k86::Rgb c);
// Small square colour swatch button.
GtkWidget* swatchButton(k86::Rgb c, const std::string& tooltip, std::function<void()> cb);
void addCss();

// Linux evdev key code (GDK hardware keycode - 8) to HID usage, 0 if unknown.
uint8_t evdevToHid(unsigned evdev);

// ------------------------------------------------------------------------------------- pages
Page buildKeysPage(App* app, bool fnLayer);
Page buildLightingPage(App* app);
Page buildPerKeyPage(App* app);
Page buildMacrosPage(App* app);
Page buildScreenPage(App* app);
Page buildSettingsPage(App* app);

// Presentation helpers shared by pages.
std::string lightModeLabel(const k86::LightModeInfo& m);
std::vector<std::string> lightOptionNames(const k86::LightModeInfo& m);
k86::KeyAction matrixAction(const k86::KeyMatrix& m, int slot);
// Name of the physical key at a slot ("Caps Lock", "Knob press"), from the layout or the factory map.
std::string slotLabel(const k86::DeviceModel& m, int slot);
// Selects a key (by layout id) on a Keys / Fn page widget; used for screenshots.
void selectKeyOnPage(GtkWidget* page, const std::string& id);

}  // namespace k86gui
