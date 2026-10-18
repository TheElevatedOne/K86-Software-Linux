// "Screen" (LCD images, clock, system info) and "Settings" pages.
#include <algorithm>
#include <stdexcept>

#include "app.hpp"
#include "screen.hpp"

using namespace k86;

namespace k86gui {

// ----------------------------------------------------------------------------------------- screen
namespace {

struct ScreenPage {
    App* app = nullptr;
    GtkWidget* chooser = nullptr;
    GtkWidget* preview = nullptr;
    GtkWidget* slot = nullptr;
    GtkWidget* delay = nullptr;
    GtkWidget* language = nullptr;
    GtkWidget* sysSync = nullptr;
    GtkWidget* sysRow = nullptr;
    guint sysTimer = 0;
};

void upload(ScreenPage* p) {
    App* app = p->app;
    gchar* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(p->chooser));
    if (!file) {
        app->info("Choose an image or GIF first.");
        return;
    }
    std::string path = file;
    g_free(file);
    if (!app->canWrite()) return;
    LoadedImage img;
    try {
        img = loadImage(path);
    } catch (const std::exception& e) {
        app->error(e.what());
        return;
    }
    if (img.frames.size() > 255) {
        app->error("The GIF has more than 255 frames.");
        return;
    }
    const DeviceModel& m = *app->cache.model;
    uint8_t slot = uint8_t(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(p->slot)) - 1);
    int delay = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(p->delay));
    if (!delay) delay = img.delaysMs.empty() || img.delaysMs[0] <= 0 ? 50 : std::min(img.delaysMs[0], 255);
    app->worker.post(
        "Uploading image",
        [img, slot, delay, W = m.screenWidth, H = m.screenHeight](Keyboard& kb, const Worker::ProgressFn& progress) {
            if (img.frames.size() == 1) {
                auto px = fitToScreen(img.frames[0], W, H);
                ScreenImage si = toScreenImage(px, W, contentRect(px, W, H));
                if (!kb.screenBegin(slot, 1, 0, si)) throw std::runtime_error("the keyboard rejected the image");
                kb.screenSendFrame(slot, 1, 0, si, [&](double f) { progress(f, "Uploading image"); });
                return;
            }
            std::vector<std::vector<uint32_t>> px;
            std::vector<Rect> rects;
            for (const auto& f : img.frames) {
                px.push_back(fitToScreen(f, W, H));
                rects.push_back(contentRect(px.back(), W, H));
            }
            Rect u = unionRect(rects);
            uint8_t n = uint8_t(img.frames.size());
            std::vector<ScreenImage> frames;
            for (auto& x : px) frames.push_back(toScreenImage(x, W, u));
            if (!kb.screenBegin(0, n, uint8_t(delay), frames[0])) throw std::runtime_error("the keyboard rejected the GIF");
            for (size_t i = 0; i < frames.size(); ++i)
                kb.screenSendFrame(uint8_t(i), n, uint8_t(delay), frames[i], [&](double f) {
                    progress((double(i) + f) / double(frames.size()), "Uploading GIF");
                });
        },
        [app] { app->status("Image uploaded."); });
}

gboolean sendSysInfo(gpointer d) {
    auto* p = static_cast<ScreenPage*>(d);
    if (!p->app->worker.connected() || p->app->worker.pending() > 0) return G_SOURCE_CONTINUE;
    SystemInfo info = p->app->sysinfo.sample();
    p->app->worker.post(
        "System info", [info](Keyboard& kb, const Worker::ProgressFn&) { kb.sendSystemInfo(info); }, {},
        [p](const std::string& e) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->sysSync), FALSE);
            p->app->error(e);
        });
    return G_SOURCE_CONTINUE;
}

}  // namespace

Page buildScreenPage(App* app) {
    auto* p = new ScreenPage();
    p->app = app;
    GtkWidget* b = vbox(10, 14);
    g_object_set_data_full(G_OBJECT(b), "page", p, [](gpointer d) { delete static_cast<ScreenPage*>(d); });

    gtk_box_pack_start(GTK_BOX(b), heading("Image on the screen"), FALSE, FALSE, 0);
    GtkWidget* row = hbox(8);
    p->chooser = gtk_file_chooser_button_new("Image", GTK_FILE_CHOOSER_ACTION_OPEN);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images (PNG, JPEG, BMP, GIF, TGA)");
    for (const char* pat : {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.gif", "*.tga", "*.PNG", "*.JPG", "*.GIF"})
        gtk_file_filter_add_pattern(filter, pat);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(p->chooser), filter);
    gtk_box_pack_start(GTK_BOX(row), label("Image or GIF:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), p->chooser, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(b), row, FALSE, FALSE, 0);

    GtkWidget* frame = gtk_frame_new(nullptr);
    p->preview = gtk_image_new();
    gtk_widget_set_size_request(p->preview, 240, 135);
    gtk_container_add(GTK_CONTAINER(frame), p->preview);
    gtk_widget_set_halign(frame, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(b), frame, FALSE, FALSE, 0);
    onChanged(p->chooser, "file-set", [p] {
        gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(p->chooser));
        if (!f) return;
        GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(f, 240, 135, TRUE, nullptr);
        g_free(f);
        gtk_image_set_from_pixbuf(GTK_IMAGE(p->preview), pb);
        if (pb) g_object_unref(pb);
    });

    GtkWidget* row2 = hbox(8);
    p->slot = gtk_spin_button_new_with_range(1, 5, 1);
    p->delay = gtk_spin_button_new_with_range(0, 255, 1);
    gtk_box_pack_start(GTK_BOX(row2), label("Still image slot:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row2), p->slot, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row2), label("GIF frame delay (ms, 0 = from the file):"), FALSE, FALSE, 12);
    gtk_box_pack_start(GTK_BOX(row2), p->delay, FALSE, FALSE, 0);
    GtkWidget* up = button("Upload", [p] { upload(p); });
    gtk_style_context_add_class(gtk_widget_get_style_context(up), "suggested-action");
    gtk_box_pack_end(GTK_BOX(row2), up, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), row2, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b),
                       label("Images are scaled to 240 × 135. A GIF replaces the animation; it can have up to 255 "
                             "frames.",
                             true),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(b), heading("Clock and status"), FALSE, FALSE, 8);
    GtkWidget* row3 = hbox(8);
    gtk_box_pack_start(GTK_BOX(row3), button("Set the clock to this computer's time", [app] {
                           if (!app->canWrite()) return;
                           app->worker.post("Setting the clock",
                                            [](Keyboard& kb, const Worker::ProgressFn&) { kb.syncClock(); },
                                            [app] { app->status("Clock set."); });
                       }),
                       FALSE, FALSE, 0);
    p->language = button("Switch screen language", [app] {
        if (!app->canWrite()) return;
        app->worker.post("Switching language", [](Keyboard& kb, const Worker::ProgressFn&) {
            kb.switchScreenLanguage(true);
        });
    });
    gtk_box_pack_start(GTK_BOX(row3), p->language, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), row3, FALSE, FALSE, 0);
    p->sysRow = hbox(8);
    p->sysSync = gtk_check_button_new_with_label("Send CPU, memory, disk and network stats to the screen every second");
    gtk_box_pack_start(GTK_BOX(p->sysRow), p->sysSync, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), p->sysRow, FALSE, FALSE, 0);
    onChanged(p->sysSync, "toggled", [p] {
        bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->sysSync));
        if (on && !p->app->canWrite()) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->sysSync), FALSE);
            return;
        }
        if (on && !p->sysTimer) {
            p->app->sysinfo.sample();
            p->sysTimer = g_timeout_add(1000, sendSysInfo, p);
        } else if (!on && p->sysTimer) {
            g_source_remove(p->sysTimer);
            p->sysTimer = 0;
        }
    });

    gtk_box_pack_start(GTK_BOX(b), heading("Stored images"), FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(b), button("Erase all images…", [app] {
                           if (!app->canWrite() || !app->confirm("Erase all images stored on the keyboard screen? "
                                                                 "This takes about a minute."))
                               return;
                           app->worker.post(
                               "Erasing the screen flash",
                               [](Keyboard& kb, const Worker::ProgressFn&) {
                                   if (!kb.eraseScreenFlash())
                                       throw std::runtime_error("the keyboard did not acknowledge the erase");
                               },
                               [app] { app->info("Erase started; it takes about 55 seconds."); });
                       }),
                       FALSE, FALSE, 0);

    Page result{"screen", "Screen", scrolledV(b), {}};
    result.refresh = [p] {
        const DeviceModel* m = p->app->cache.model;
        if (!m) return;
        gtk_spin_button_set_range(GTK_SPIN_BUTTON(p->slot), 1, m->screenSlots);
        gtk_widget_set_visible(p->language, m->canSwitchLanguage);
        gtk_widget_set_visible(p->sysRow, m->canSyncSystemInfo);
    };
    return result;
}

// --------------------------------------------------------------------------------------- settings
namespace {

struct OptionInfo {
    const char* key;
    const char* label;
    bool editable;
    int max;  // 1 = on/off switch
    int step;
};

// Option flags of both firmwares (see docs/PROTOCOL.md); unknown keys are shown as numbers.
const OptionInfo kOptions[] = {
    {"winLock", "Lock the Win key", true, 1, 1},
    {"macMode", "Mac mode", false, 1, 1},
    {"wasdSwap", "Swap WASD and the arrow keys", true, 1, 1},
    {"ledOff", "Backlight off", true, 1, 1},
    {"sideLedOff", "Side light off", true, 1, 1},
    {"keyboardMode", "Keyboard mode flag", true, 1, 1},
    {"keyboardLock", "Lock the keyboard", true, 1, 1},
    {"fnMatrix", "Alternative Fn layer", true, 1, 1},
    {"powerSave", "Power saving", true, 1, 1},
    {"system", "Detected system (0 Windows, 1 macOS, 2 iOS, 3 Android)", false, 3, 1},
    {"fnLayer", "Fn layer", true, 255, 1},
    {"antiMistouch", "Anti-mistouch", true, 1, 1},
    {"rtStab", "Rapid trigger stability", true, 125, 25},
    {"lowPower", "Low power mode", false, 1, 1},
};

struct SettingsPage {
    App* app = nullptr;
    GtkWidget* debounce = nullptr;
    GtkWidget* sleep[4] = {};
    GtkWidget* optionsGrid = nullptr;
    std::map<std::string, GtkWidget*> optionWidgets;
    GtkWidget* versions = nullptr;
};

int optionValue(GtkWidget* w) {
    if (GTK_IS_SWITCH(w)) return gtk_switch_get_active(GTK_SWITCH(w)) ? 1 : 0;
    if (GTK_IS_SPIN_BUTTON(w)) return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
    return 0;
}

void fillSettings(SettingsPage* p) {
    Updating u(p->app);
    const Cache& c = p->app->cache;
    if (c.debounce) gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->debounce), *c.debounce);
    if (c.sleep) {
        const uint16_t v[4] = {c.sleep->bluetooth, c.sleep->wireless24, c.sleep->deepBluetooth, c.sleep->deepWireless24};
        for (int i = 0; i < 4; ++i) gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->sleep[i]), v[i]);
    }
    clearContainer(p->optionsGrid);
    p->optionWidgets.clear();
    // Known options in a fixed order, then any others.
    std::vector<std::pair<std::string, int>> ordered;
    for (const auto& o : kOptions)
        if (auto it = c.options.find(o.key); it != c.options.end()) ordered.emplace_back(*it);
    for (const auto& kv : c.options)
        if (std::none_of(std::begin(kOptions), std::end(kOptions), [&](const OptionInfo& o) { return kv.first == o.key; }))
            ordered.emplace_back(kv);
    int row = 0;
    for (const auto& [key, value] : ordered) {
        const OptionInfo* info = nullptr;
        for (const auto& o : kOptions)
            if (key == o.key) info = &o;
        GtkWidget* w;
        if (info && info->max == 1) {
            w = gtk_switch_new();
            gtk_switch_set_active(GTK_SWITCH(w), value != 0);
        } else {
            w = gtk_spin_button_new_with_range(0, info ? info->max : 255, info ? info->step : 1);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), value);
        }
        gtk_widget_set_halign(w, GTK_ALIGN_START);
        gtk_widget_set_sensitive(w, info ? info->editable : false);
        gtk_grid_attach(GTK_GRID(p->optionsGrid), label(info ? info->label : key), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(p->optionsGrid), w, 1, row, 1, 1);
        p->optionWidgets[key] = w;
        ++row;
    }
    gtk_widget_show_all(p->optionsGrid);

    std::string v;
    auto ver = [](std::optional<uint16_t> x) {
        char b[16];
        snprintf(b, sizeof b, "%x.%02x", *x >> 8, *x & 0xFF);
        return std::string(b);
    };
    if (c.model) v += c.model->displayName + " (" + c.model->firmwareName + ")";
    if (c.firmwareId) v += ", firmware id " + std::to_string(*c.firmwareId);
    auto has = [](const std::optional<uint16_t>& x) { return x && *x; };
    if (has(c.versions.usb)) v += "\nKeyboard firmware " + ver(c.versions.usb);
    if (has(c.versions.rf)) v += ", wireless " + ver(c.versions.rf);
    if (has(c.versions.oled)) v += ", screen " + ver(c.versions.oled);
    if (has(c.versions.mled)) v += ", LED " + ver(c.versions.mled);
    gtk_label_set_text(GTK_LABEL(p->versions), v.c_str());
}

void writeSettings(SettingsPage* p) {
    App* app = p->app;
    if (!app->cache.model || !app->canWrite()) return;
    const DeviceModel& m = *app->cache.model;
    auto spin = [](GtkWidget* w) { return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w)); };
    SleepTimes t{uint16_t(spin(p->sleep[0])), uint16_t(spin(p->sleep[1])), uint16_t(spin(p->sleep[2])),
                 uint16_t(spin(p->sleep[3]))};
    auto in = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
    if (!in(t.bluetooth, m.sleepMin, m.sleepMax) || !in(t.wireless24, m.sleepMin, m.sleepMax) ||
        !in(t.deepBluetooth, m.deepSleepMin, m.deepSleepMax) || !in(t.deepWireless24, m.deepSleepMin, m.deepSleepMax)) {
        app->error("Sleep must be " + std::to_string(m.sleepMin) + "–" + std::to_string(m.sleepMax) +
                   " s and deep sleep " + std::to_string(m.deepSleepMin) + "–" + std::to_string(m.deepSleepMax) +
                   " s (now " + std::to_string(t.bluetooth) + ", " + std::to_string(t.wireless24) + ", " +
                   std::to_string(t.deepBluetooth) + ", " + std::to_string(t.deepWireless24) + ").");
        return;
    }
    uint8_t debounce = uint8_t(spin(p->debounce));
    std::map<std::string, int> changes;
    for (const auto& [k, w] : p->optionWidgets) {
        auto it = app->cache.options.find(k);
        if (gtk_widget_get_sensitive(w) && it != app->cache.options.end() && optionValue(w) != it->second)
            changes[k] = optionValue(w);
    }
    const bool sleepChanged = !app->cache.sleep || app->cache.sleep->bluetooth != t.bluetooth ||
                              app->cache.sleep->wireless24 != t.wireless24 ||
                              app->cache.sleep->deepBluetooth != t.deepBluetooth ||
                              app->cache.sleep->deepWireless24 != t.deepWireless24;
    const bool debounceChanged = app->cache.debounce != debounce;
    if (!sleepChanged && !debounceChanged && changes.empty()) {
        app->status("Nothing changed.");
        return;
    }
    app->worker.post(
        "Writing settings",
        [=](Keyboard& kb, const Worker::ProgressFn&) {
            if (debounceChanged) kb.setDebounce(debounce);
            if (sleepChanged) kb.setSleep(t);
            if (!changes.empty()) kb.setOptions(changes);
        },
        [app, t, debounce, changes] {
            app->cache.sleep = t;
            app->cache.debounce = debounce;
            for (const auto& [k, v] : changes) app->cache.options[k] = v;
            app->status("Settings saved.");
            app->refreshPages();
        });
}

}  // namespace

Page buildSettingsPage(App* app) {
    auto* p = new SettingsPage();
    p->app = app;
    GtkWidget* b = vbox(10, 14);
    g_object_set_data_full(G_OBJECT(b), "page", p, [](gpointer d) { delete static_cast<SettingsPage*>(d); });

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    p->debounce = gtk_spin_button_new_with_range(0, 255, 1);
    const char* sleepLabels[4] = {"Sleep after, Bluetooth (s)", "Sleep after, 2.4 GHz (s)",
                                  "Deep sleep after, Bluetooth (s)", "Deep sleep after, 2.4 GHz (s)"};
    gtk_grid_attach(GTK_GRID(grid), label("Debounce (ms)"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), p->debounce, 1, 0, 1, 1);
    for (int i = 0; i < 4; ++i) {
        p->sleep[i] = gtk_spin_button_new_with_range(0, 65535, 10);
        gtk_grid_attach(GTK_GRID(grid), label(sleepLabels[i]), 0, i + 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), p->sleep[i], 1, i + 1, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(b), section("General", grid), FALSE, FALSE, 0);

    p->optionsGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(p->optionsGrid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(p->optionsGrid), 16);
    gtk_box_pack_start(GTK_BOX(b), section("Options", p->optionsGrid), FALSE, FALSE, 0);

    GtkWidget* row = hbox(8);
    GtkWidget* save = button("Save settings", [p] { writeSettings(p); });
    gtk_style_context_add_class(gtk_widget_get_style_context(save), "suggested-action");
    gtk_box_pack_start(GTK_BOX(row), save, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), button("Revert", [p] { fillSettings(p); }), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), row, FALSE, FALSE, 0);

    p->versions = label("", true);
    gtk_label_set_selectable(GTK_LABEL(p->versions), TRUE);
    gtk_box_pack_start(GTK_BOX(b), section("Device", p->versions), FALSE, FALSE, 8);

    GtkWidget* reset = button("Factory reset…", [app] {
        if (!app->canWrite() ||
            !app->confirm("Reset the keyboard to factory settings? Key maps, macros and lighting are erased."))
            return;
        app->worker.post("Factory reset", [](Keyboard& kb, const Worker::ProgressFn&) { kb.factoryReset(); },
                         [app] {
                             app->status("Factory reset done. Reading the keyboard again…");
                             if (app->reload) app->reload();
                         });
    });
    gtk_style_context_add_class(gtk_widget_get_style_context(reset), "destructive-action");
    gtk_widget_set_halign(reset, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(b), reset, FALSE, FALSE, 0);

    Page result{"settings", "Settings", scrolledV(b), {}};
    result.refresh = [p] { fillSettings(p); };
    return result;
}

}  // namespace k86gui
