// k86-gui - GTK 3 configuration window for the Attack Shark K86.
//
// The window works like the original software: a drawing of the keyboard on the key pages,
// click a key to change it, and everything is read from the keyboard when it is connected.
#include <glib/gstdio.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <atomic>
#include <memory>

#include "app.hpp"
#include "discovery.hpp"
#include "selftest.hpp"
#include "simulator.hpp"

using namespace k86;
using namespace k86gui;

namespace k86gui {

bool App::canWrite() {
    if (!worker.connected()) {
        info("Connect the keyboard first.");
        return false;
    }
    if (opt.readOnly) {
        error("Read-only mode: nothing is written to the keyboard. Start k86-gui without --read-only to make "
              "changes.");
        return false;
    }
    return true;
}

void App::refreshPages() {
    for (auto& p : pages)
        if (p.refresh) p.refresh();
}

namespace {

void dialog(App* app, GtkMessageType type, const std::string& text) {
    if (!app->opt.screenshot.empty() || !app->window || !gtk_widget_get_visible(app->window)) {
        std::cerr << "k86-gui: " << text << "\n";
        return;
    }
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, type, GTK_BUTTONS_CLOSE, "%s",
                                          text.c_str());
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

}  // namespace

void App::error(const std::string& text) {
    ++errorCount;
    dialog(this, GTK_MESSAGE_ERROR, text);
}
void App::info(const std::string& text) { dialog(this, GTK_MESSAGE_INFO, text); }

bool App::confirm(const std::string& text) {
    if (autoConfirm) return *autoConfirm;
    if (!opt.screenshot.empty()) return false;
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_YES_NO, "%s", text.c_str());
    bool yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
    gtk_widget_destroy(d);
    return yes;
}

void App::status(const std::string& text) {
    if (statusLabel) gtk_label_set_text(GTK_LABEL(statusLabel), text.c_str());
}

}  // namespace k86gui

namespace {

// ------------------------------------------------------------------------------------- preload
void setProgress(App* app, double f, const std::string& text) {
    gtk_widget_set_visible(app->progress, f < 1.0);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), std::clamp(f, 0.0, 1.0));
    if (!text.empty()) app->status(text);
}

void updateProfileButtons(App* app) {
    Updating u(app);
    const int layers = app->cache.model ? app->cache.model->layers : 3;
    for (size_t i = 0; i < app->profileButtons.size(); ++i) {
        GtkWidget* b = app->profileButtons[i];
        gtk_widget_set_visible(b, int(i) < layers);
        std::string t = "Profile " + std::to_string(i + 1) + (int(i) == app->cache.activeProfile ? " ★" : "");
        gtk_button_set_label(GTK_BUTTON(b), t.c_str());
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(b), int(i) == app->cache.profile);
    }
}

std::function<void()> screenshotHook;
std::atomic<SimulatedTransport*> simulator{nullptr};
int exitCode = 0;

// Reads everything from the keyboard. Each part fills the cache and the pages as it arrives.
void preload(App* app) {
    const DeviceModel* m = app->cache.model;
    if (!m) return;
    const int layers = m->layers;
    const int oses = m->chip == Chip::YC3123 ? 2 : 1;
    const int patterns = m->chip == Chip::YC3123 ? 5 : 1;
    const double total = 4 + layers + layers * oses + patterns + kMaxMacros;
    auto done = std::make_shared<double>(0);
    auto step = [app, done, total](const std::string& text) {
        *done += 1;
        setProgress(app, *done / total, text);
    };

    struct General {
        Versions versions;
        std::optional<uint32_t> firmwareId;
        std::optional<LightSetting> light, side;
        std::optional<uint8_t> profile, debounce;
        std::optional<SleepTimes> sleep;
        std::map<std::string, int> options;
    };
    auto g = std::make_shared<General>();
    const bool hasSide = !m->sideLightModes.empty();
    app->worker.post(
        "Reading settings",
        [g, hasSide](Keyboard& kb, const Worker::ProgressFn&) {
            g->firmwareId = kb.readDeviceId();
            g->versions = kb.readVersions();
            g->profile = kb.getProfile();
            g->debounce = kb.getDebounce();
            g->sleep = kb.getSleep();
            g->options = kb.getOptions();
            g->light = kb.getLight();
            if (hasSide) g->side = kb.getSideLight();
        },
        [app, g, step, layers] {
            Cache& c = app->cache;
            c.versions = g->versions;
            c.firmwareId = g->firmwareId;
            c.light = g->light;
            c.side = g->side;
            c.debounce = g->debounce;
            c.sleep = g->sleep;
            c.options = g->options;
            if (g->profile && *g->profile < layers) c.activeProfile = c.profile = *g->profile;
            for (int i = 0; i < 4; ++i) step("Reading settings");
            updateProfileButtons(app);
            app->refreshPages();
        });

    for (int l = 0; l < layers; ++l) {
        auto km = std::make_shared<KeyMatrix>();
        app->worker.post(
            "Reading key maps", [km, l](Keyboard& kb, const Worker::ProgressFn&) { *km = kb.getKeyMatrix(uint8_t(l)); },
            [app, km, l, step] {
                if (size_t(l) < app->cache.keymaps.size()) app->cache.keymaps[size_t(l)] = *km;
                step("Reading key maps");
                if (l == app->cache.profile) app->refreshPages();
            });
    }
    for (int l = 0; l < layers; ++l)
        for (int os = 0; os < oses; ++os) {
            auto km = std::make_shared<KeyMatrix>();
            app->worker.post(
                "Reading Fn layers",
                [km, l, os](Keyboard& kb, const Worker::ProgressFn&) { *km = kb.getFnMatrix(uint8_t(l), uint8_t(os)); },
                [app, km, l, os, oses, step] {
                    if (size_t(l) < app->cache.fn.size()) {
                        app->cache.fn[size_t(l)][size_t(os)] = *km;
                        if (oses == 1) app->cache.fn[size_t(l)][1] = *km;
                    }
                    step("Reading Fn layers");
                    if (l == app->cache.profile) app->refreshPages();
                });
        }
    for (int pat = 0; pat < patterns; ++pat) {
        auto pic = std::make_shared<std::array<Rgb, kSlots>>();
        app->worker.post(
            "Reading per-key colours",
            [pic, pat](Keyboard& kb, const Worker::ProgressFn&) { *pic = kb.getUserPicture(uint8_t(pat)); },
            [app, pic, pat, step] {
                if (size_t(pat) < app->cache.pictures.size()) app->cache.pictures[size_t(pat)] = *pic;
                step("Reading per-key colours");
                if (pat == 0) app->refreshPages();
            });
    }
    for (int i = 0; i < kMaxMacros; ++i) {
        auto mac = std::make_shared<Macro>();
        app->worker.post(
            "Reading macros", [mac, i](Keyboard& kb, const Worker::ProgressFn&) { *mac = kb.getMacro(uint8_t(i)); },
            [app, mac, i, step] {
                app->cache.macros[size_t(i)] = *mac;
                step(i + 1 == kMaxMacros ? "Ready." : "Reading macros");
                if (i + 1 == kMaxMacros || i % 10 == 9) app->refreshPages();
                if (i + 1 == kMaxMacros) {
                    setProgress(app, 1.0, "Read everything from the keyboard.");
                    if (screenshotHook) screenshotHook();
                }
            });
    }
}

// --------------------------------------------------------------------------------- connection
void showConnected(App* app, bool connected, const std::string& message = {}) {
    gtk_stack_set_visible_child_name(GTK_STACK(app->outer), connected ? "pages" : "empty");
    gtk_widget_set_sensitive(app->profileBox, connected);
    if (!connected && !message.empty()) gtk_label_set_text(GTK_LABEL(app->emptyLabel), message.c_str());
}

void connectTo(App* app, const std::string& path) {
    if (app->connecting) return;
    app->connecting = true;
    const Options opt = app->opt;
    app->status("Connecting…");
    app->worker.open(
        [opt, path]() -> OpenedDevice {
            OpenedDevice d;
            if (!opt.dryRun.empty()) {
                const DeviceModel& m = opt.dryRun == "v1" ? knownModels()[0] : knownModels()[1];
                auto sim = std::make_unique<SimulatedTransport>(m);
                sim->onWrite = [](const Report& r) {
                    std::printf("dry-run write:");
                    for (size_t i = 0; i < 16; ++i) std::printf(" %02x", r[i]);
                    std::printf(" ...\n");
                    std::fflush(stdout);
                };
                simulator = sim.get();
                d.transport = std::move(sim);
                Transport* t = d.transport.get();
                if (opt.readOnly) {
                    d.wrapper = std::make_unique<ReadOnlyTransport>(*t);
                    t = d.wrapper.get();
                }
                d.keyboard = makeKeyboard(*t, m, [](int) {});
                d.description = m.displayName + " — simulated (dry run)";
                return d;
            }
            OpenKeyboard ok = openKeyboard(path, false);
            d.transport = std::move(ok.transport);
            if (opt.readOnly) {
                // Re-create the keyboard on top of the read-only guard.
                d.wrapper = std::make_unique<ReadOnlyTransport>(*d.transport);
                d.keyboard = makeKeyboard(*d.wrapper, ok.keyboard->model());
                d.keyboard->setWaitForCommit(true);
            } else {
                d.keyboard = std::move(ok.keyboard);
            }
            d.description = d.keyboard->model().displayName + " at " + ok.info.path;
            if (ok.firmwareId) d.description += ", firmware id " + std::to_string(*ok.firmwareId);
            return d;
        },
        [app](const DeviceModel* model, const std::string& text) {
            app->connecting = false;
            if (!model) {
                showConnected(app, false, "Could not open the keyboard:\n" + text);
                app->status("Connection failed.");
                return;
            }
            Cache c;
            c.model = model;
            c.loaded = true;
            c.keymaps.assign(size_t(model->layers), KeyMatrix{});
            for (auto& km : c.keymaps) std::copy_n(model->defaultMatrix, km.size(), km.begin());
            c.fn.assign(size_t(model->layers), {});
            for (auto& layer : c.fn)
                for (auto& km : layer) std::copy_n(model->defaultFnMatrix, km.size(), km.begin());
            c.pictures.assign(model->chip == Chip::YC3123 ? 5 : 1, {});
            c.macros.assign(kMaxMacros, std::nullopt);
            app->cache = std::move(c);
            gtk_label_set_text(GTK_LABEL(app->deviceCombo), text.c_str());
            if (app->tray) app->tray->setStatus(model->displayName + " connected");
            showConnected(app, true);
            updateProfileButtons(app);
            app->refreshPages();
            preload(app);
        });
}

void lost(App* app) {
    app->cache = Cache{};
    showConnected(app, false, "The keyboard was disconnected.\nConnect it with the USB cable; it is picked up "
                              "automatically.");
    gtk_label_set_text(GTK_LABEL(app->deviceCombo), "No keyboard");
    if (app->tray) app->tray->setStatus("No keyboard connected");
    setProgress(app, 1.0, "Keyboard disconnected.");
}

// ------------------------------------------------------------------------------------ tray
bool windowVisible(App* app) { return gtk_widget_get_visible(app->window); }

void setWindowVisible(App* app, bool visible) {
    if (visible) {
        gtk_widget_show(app->window);
        gtk_window_present(GTK_WINDOW(app->window));
    } else {
        gtk_widget_hide(app->window);
    }
    if (app->tray) app->tray->menuChanged();
}

bool trayWanted(App* app) {
    return app->opt.screenshot.empty() && (app->settings.tray || app->opt.minimize);
}

// Closing the window only hides it while a tray host shows our icon (otherwise the app would
// keep running with no way back to it).
bool hideOnClose(App* app) {
    return app->tray && app->tray->registered() && (app->settings.keepInTray || app->opt.minimize);
}

void applyTray(App* app) {
    if (trayWanted(app) && !app->tray) {
        app->tray = std::make_unique<Tray>();
        Tray* t = app->tray.get();
        t->windowVisible = [app] { return windowVisible(app); };
        t->onActivate = [app] { setWindowVisible(app, !windowVisible(app)); };
        t->onQuit = [] { gtk_main_quit(); };
        t->onRegisteredChanged = [app](bool registered) {
            // The tray host went away while the window was hidden: bring the window back.
            if (!registered && !windowVisible(app)) setWindowVisible(app, true);
            if (app->trayChanged) app->trayChanged();
        };
        t->setStatus(app->cache.model ? app->cache.model->displayName + " connected" : "No keyboard connected");
        if (!t->start()) app->tray.reset();
    } else if (!trayWanted(app) && app->tray) {
        app->tray.reset();
        if (!windowVisible(app)) setWindowVisible(app, true);
    }
    if (app->trayChanged) app->trayChanged();
}

// Looks for a keyboard while none is connected.
gboolean hotplug(gpointer d) {
    auto* app = static_cast<App*>(d);
    if (app->worker.connected() || app->connecting) return G_SOURCE_CONTINUE;
    if (!app->opt.dryRun.empty()) {
        connectTo(app, {});
        return G_SOURCE_CONTINUE;
    }
    auto cands = findCandidates();
    if (cands.empty()) return G_SOURCE_CONTINUE;
    connectTo(app, cands[0].info.path);
    return G_SOURCE_CONTINUE;
}

// ------------------------------------------------------------------------------------- window
GtkWidget* buildTopBar(App* app) {
    GtkWidget* bar = hbox(10);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 8);
    GtkWidget* name = gtk_label_new("Attack Shark K86");
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "k86-heading");
    gtk_box_pack_start(GTK_BOX(bar), name, FALSE, FALSE, 0);
    app->deviceCombo = label("No keyboard", true);
    gtk_label_set_ellipsize(GTK_LABEL(app->deviceCombo), PANGO_ELLIPSIZE_END);
    gtk_label_set_line_wrap(GTK_LABEL(app->deviceCombo), FALSE);
    gtk_box_pack_start(GTK_BOX(bar), app->deviceCombo, FALSE, FALSE, 0);
    if (app->opt.readOnly) {
        GtkWidget* pill = gtk_label_new("read-only");
        gtk_widget_set_tooltip_text(pill, "Started with --read-only: nothing is written to the keyboard");
        gtk_style_context_add_class(gtk_widget_get_style_context(pill), "k86-pill-warn");
        gtk_box_pack_start(GTK_BOX(bar), pill, FALSE, FALSE, 0);
    }
    if (!app->opt.dryRun.empty()) {
        GtkWidget* pill = gtk_label_new("dry run");
        gtk_widget_set_tooltip_text(pill, "A simulated keyboard; writes are printed to the terminal");
        gtk_style_context_add_class(gtk_widget_get_style_context(pill), "k86-pill");
        gtk_box_pack_start(GTK_BOX(bar), pill, FALSE, FALSE, 0);
    }

    GtkWidget* reload = button("Read again", [app] {
        if (app->reload) app->reload();
    });
    gtk_widget_set_tooltip_text(reload, "Read every setting from the keyboard again");
    gtk_box_pack_end(GTK_BOX(bar), reload, FALSE, FALSE, 0);

    app->profileBox = hbox(0);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->profileBox), "linked");
    GtkWidget* first = nullptr;
    for (int i = 0; i < 3; ++i) {
        GtkWidget* b = first ? gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(first), "")
                             : gtk_radio_button_new_with_label(nullptr, "");
        if (!first) first = b;
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), FALSE);
        gtk_widget_set_tooltip_text(b, "Show and edit this profile (★ = the profile the keyboard uses)");
        onChanged(b, "toggled", [app, b, i] {
            if (app->updating || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b))) return;
            app->cache.profile = i;
            app->refreshPages();
        });
        app->profileButtons.push_back(b);
        gtk_box_pack_start(GTK_BOX(app->profileBox), b, FALSE, FALSE, 0);
    }
    GtkWidget* activate = button("Use on keyboard", [app] {
        if (!app->canWrite()) return;
        uint8_t p = uint8_t(app->cache.profile);
        app->worker.post(
            "Switching profile", [p](Keyboard& kb, const Worker::ProgressFn&) { kb.setProfile(p); },
            [app, p] {
                app->cache.activeProfile = p;
                updateProfileButtons(app);
                app->status("The keyboard now uses profile " + std::to_string(p + 1) + ".");
            });
    });
    gtk_widget_set_tooltip_text(activate, "Make the shown profile the one the keyboard uses");
    gtk_box_pack_start(GTK_BOX(app->profileBox), activate, FALSE, FALSE, 8);
    gtk_box_pack_end(GTK_BOX(bar), app->profileBox, FALSE, FALSE, 0);
    return bar;
}

GtkWidget* buildStatusBar(App* app) {
    GtkWidget* bar = hbox(8);
    gtk_container_set_border_width(GTK_CONTAINER(bar), 6);
    app->spinner = gtk_spinner_new();
    gtk_box_pack_start(GTK_BOX(bar), app->spinner, FALSE, FALSE, 0);
    app->statusLabel = label("");
    gtk_label_set_line_wrap(GTK_LABEL(app->statusLabel), FALSE);
    gtk_label_set_ellipsize(GTK_LABEL(app->statusLabel), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(bar), app->statusLabel, TRUE, TRUE, 0);
    app->progress = gtk_progress_bar_new();
    gtk_widget_set_size_request(app->progress, 220, -1);
    gtk_widget_set_valign(app->progress, GTK_ALIGN_CENTER);
    gtk_box_pack_end(GTK_BOX(bar), app->progress, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all(app->progress, TRUE);
    return bar;
}

GtkWidget* buildEmpty(App* app) {
    GtkWidget* b = vbox(14, 40);
    gtk_widget_set_valign(b, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(b, GTK_ALIGN_CENTER);
    GtkWidget* icon = gtk_image_new_from_icon_name("input-keyboard-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 96);
    gtk_box_pack_start(GTK_BOX(b), icon, FALSE, FALSE, 0);
    app->emptyLabel = gtk_label_new(
        "Looking for the K86…\nConnect it with the USB cable; it is picked up automatically.");
    gtk_label_set_justify(GTK_LABEL(app->emptyLabel), GTK_JUSTIFY_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->emptyLabel), "k86-empty");
    gtk_box_pack_start(GTK_BOX(b), app->emptyLabel, FALSE, FALSE, 0);
    GtkWidget* hint = gtk_label_new(
        "If it is connected but not found, install the udev rule (see the README) or check the cable.");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
    gtk_box_pack_start(GTK_BOX(b), hint, FALSE, FALSE, 0);
    return b;
}

// --------------------------------------------------------------------------------- screenshots
void pump(int ms) {
    gint64 until = g_get_monotonic_time() + gint64(ms) * 1000;
    while (g_get_monotonic_time() < until) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(5000);
    }
}

void savePng(GtkWidget* window, const std::string& file) {
    GdkPixbuf* pb = gtk_offscreen_window_get_pixbuf(GTK_OFFSCREEN_WINDOW(window));
    if (!pb) {
        std::cerr << "k86-gui: could not render " << file << "\n";
        return;
    }
    gdk_pixbuf_save(pb, file.c_str(), "png", nullptr, nullptr);
    g_object_unref(pb);
}

void takeScreenshots(App* app) {
    for (auto& p : app->pages) {
        gtk_stack_set_visible_child_name(GTK_STACK(app->stack), p.name.c_str());
        if (p.name == "keys") selectKeyOnPage(p.widget, "CapsLock");
        if (p.name == "fn") selectKeyOnPage(p.widget, "Digit1");
        pump(300);
        std::string file = app->opt.screenshot + "-" + p.name + ".png";
        savePng(app->window, file);
        std::cout << "wrote " << file << "\n";
    }
    gtk_main_quit();
}

}  // namespace

int main(int argc, char** argv) {
    if (!gtk_init_check(&argc, &argv)) {
        bool selfTest = false;
        for (int i = 1; i < argc; ++i) selfTest |= std::string(argv[i]) == "--self-test";
        std::fprintf(stderr, "k86-gui: cannot open a display%s\n", selfTest ? "; skipping the self-test" : "");
        return selfTest ? 77 : 1;
    }
    auto app = std::make_unique<App>();
    Options& opt = app->opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dry-run") opt.dryRun = "v1";
        else if (a.rfind("--dry-run=", 0) == 0) opt.dryRun = a.substr(10);
        else if (a == "--read-only") opt.readOnly = true;
        else if (a == "--screenshot" && i + 1 < argc) opt.screenshot = argv[++i];
        else if (a == "--page" && i + 1 < argc) opt.page = argv[++i];
        else if (a == "--self-test") opt.selfTest = true;
        else if (a == "--minimize") opt.minimize = true;
        else {
            std::printf(
                "Usage: k86-gui [--minimize] [--dry-run[=v1|v2]] [--read-only] [--page NAME] [--screenshot PREFIX]\n"
                "  --minimize     start hidden with an icon in the system tray (closing the window\n"
                "                 keeps it running there)\n"
                "  --dry-run      use a simulated K86 (v1 = YC3121, v2 = YC3123); writes are printed\n"
                "  --read-only    never write to the keyboard (everything is still read)\n"
                "  --page NAME    start on a page: keys, fn, lighting, perkey, macros, screen, settings\n"
                "  --screenshot   read the keyboard, save every page as PREFIX-NAME.png and exit\n"
                "  --self-test    click through every page against a simulated keyboard and check the writes\n");
            return a == "--help" || a == "-h" ? 0 : 1;
        }
    }
    if (opt.selfTest) {
        if (opt.dryRun.empty()) opt.dryRun = "v1";
        opt.readOnly = false;
        if (opt.screenshot.empty()) opt.screenshot = "-";  // off screen, no window
        opt.minimize = false;
    }
    if (!opt.dryRun.empty() && opt.dryRun != "v1" && opt.dryRun != "v2") {
        std::fprintf(stderr, "--dry-run takes v1 or v2\n");
        return 1;
    }

    addCss();
    gtk_window_set_default_icon_name("k86-gui");
    App* a = app.get();
    if (opt.selfTest) {
        // Never touch the user's settings from the self-test.
        gchar* dir = g_dir_make_tmp("k86-gui-selftest-XXXXXX", nullptr);
        a->settingsPath = std::string(dir ? dir : g_get_tmp_dir()) + "/settings.ini";
        g_free(dir);
    } else {
        a->settingsPath = defaultAppSettingsPath();
    }
    a->settings = loadAppSettings(a->settingsPath);
    a->applyTray = [a] { applyTray(a); };
    // Screenshots are rendered off screen, so no window appears at all.
    a->window = opt.screenshot.empty() ? gtk_window_new(GTK_WINDOW_TOPLEVEL) : gtk_offscreen_window_new();
    gtk_window_set_title(GTK_WINDOW(a->window), "Attack Shark K86");
    gtk_window_set_default_size(GTK_WINDOW(a->window), 1240, 900);
    g_signal_connect(a->window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(a->window, "delete-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer d) -> gboolean {
                         auto* a = static_cast<App*>(d);
                         if (!hideOnClose(a)) return FALSE;  // close = quit
                         setWindowVisible(a, false);
                         return TRUE;
                     }),
                     a);

    GtkWidget* root = vbox(0);
    if (!opt.screenshot.empty()) gtk_widget_set_size_request(root, 1240, 900);
    gtk_box_pack_start(GTK_BOX(root), buildTopBar(a), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    a->outer = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(a->outer), buildEmpty(a), "empty");
    GtkWidget* body = hbox(0);
    a->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(a->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    a->pages = {buildKeysPage(a, false), buildKeysPage(a, true), buildLightingPage(a), buildPerKeyPage(a),
                buildMacrosPage(a),      buildScreenPage(a),     buildSettingsPage(a)};
    for (auto& p : a->pages) gtk_stack_add_titled(GTK_STACK(a->stack), p.widget, p.name.c_str(), p.title.c_str());
    GtkWidget* sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar), GTK_STACK(a->stack));
    gtk_widget_set_size_request(sidebar, 170, -1);
    gtk_box_pack_start(GTK_BOX(body), sidebar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), a->stack, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(a->outer), body, "pages");
    gtk_box_pack_start(GTK_BOX(root), a->outer, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), buildStatusBar(a), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(a->window), root);

    a->worker.onBusy = [a](int pending, const std::string& what) {
        pending ? gtk_spinner_start(GTK_SPINNER(a->spinner)) : gtk_spinner_stop(GTK_SPINNER(a->spinner));
        if (pending && !what.empty() && what.rfind("Reading", 0) != 0) a->status(what + "…");
    };
    a->worker.onError = [a](const std::string& what, const std::string& e) { a->error(what + " failed: " + e); };
    a->worker.onLost = [a] { lost(a); };
    a->reload = [a] {
        if (!a->worker.connected()) return;
        preload(a);
    };

    const bool startHidden =
        opt.screenshot.empty() && (opt.minimize || (a->settings.tray && a->settings.startMinimized));
    applyTray(a);
    if (startHidden && a->tray) {
        gtk_widget_show_all(root);
        // Show the window after all if no tray host takes the icon.
        g_timeout_add_seconds(3, +[](gpointer d) -> gboolean {
            auto* a = static_cast<App*>(d);
            if (!(a->tray && a->tray->registered()) && !windowVisible(a)) {
                std::cerr << "k86-gui: no system tray found; showing the window\n";
                setWindowVisible(a, true);
            }
            return G_SOURCE_REMOVE;
        }, a);
    } else {
        gtk_widget_show_all(a->window);
    }
    showConnected(a, false);
    if (!opt.page.empty()) gtk_stack_set_visible_child_name(GTK_STACK(a->stack), opt.page.c_str());
    if (opt.selfTest) {
        screenshotHook = [a] {
            screenshotHook = nullptr;
            runOnMain([a] {
                exitCode = runSelfTest(a, simulator.load());
                gtk_main_quit();
            });
        };
    } else if (!opt.screenshot.empty()) {
        screenshotHook = [a] {
            screenshotHook = nullptr;
            runOnMain([a] { takeScreenshots(a); });
        };
        // Give up if no keyboard shows up.
        g_timeout_add_seconds(60, +[](gpointer d) -> gboolean {
            auto* a = static_cast<App*>(d);
            std::cerr << "k86-gui: no keyboard was read within 60 s\n";
            takeScreenshots(a);
            return G_SOURCE_REMOVE;
        }, a);
    }
    hotplug(a);
    a->hotplugTimer = g_timeout_add_seconds(2, hotplug, a);
    gtk_main();
    g_source_remove(a->hotplugTimer);
    a->tray.reset();
    if (opt.selfTest) {  // remove the temporary settings file and its directory
        gchar* dir = g_path_get_dirname(a->settingsPath.c_str());
        g_unlink(a->settingsPath.c_str());
        g_rmdir(dir);
        g_free(dir);
    }
    return exitCode;
}
