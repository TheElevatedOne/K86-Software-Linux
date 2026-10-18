// k86-gui --self-test: drives the real widgets against the simulated keyboard and checks what
// ends up in its memory. Covers the write paths of every page without touching hardware.
#include <glib/gstdio.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <memory>

#include "app.hpp"
#include "layout.hpp"
#include "selftest.hpp"

using namespace k86;

namespace k86gui {

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

void pump(int ms) {
    gint64 until = g_get_monotonic_time() + gint64(ms) * 1000;
    while (g_get_monotonic_time() < until) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(2000);
    }
}

void settle(App* app) {
    pump(450);  // debounced writes
    for (int i = 0; i < 500 && app->worker.pending() > 0; ++i) pump(10);
    pump(20);
}

// Depth-first search through a widget tree.
void walk(GtkWidget* w, const std::function<bool(GtkWidget*)>& f) {
    if (!f(w)) return;
    if (GTK_IS_CONTAINER(w)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList* l = children; l; l = l->next) walk(GTK_WIDGET(l->data), f);
        g_list_free(children);
    }
    if (GTK_IS_MENU_BUTTON(w)) {
        if (GtkMenu* m = gtk_menu_button_get_popup(GTK_MENU_BUTTON(w))) walk(GTK_WIDGET(m), f);
    }
}

GtkWidget* findButton(GtkWidget* root, const std::string& text, int index = 0) {
    GtkWidget* found = nullptr;
    int n = 0;
    walk(root, [&](GtkWidget* w) {
        if (found) return false;
        const char* l = nullptr;
        if (GTK_IS_BUTTON(w)) l = gtk_button_get_label(GTK_BUTTON(w));
        if (GTK_IS_MENU_ITEM(w)) l = gtk_menu_item_get_label(GTK_MENU_ITEM(w));
        if (l && text == l && n++ == index) found = w;
        return true;
    });
    return found;
}

GtkWidget* findTooltip(GtkWidget* root, const std::string& text, int index = 0) {
    GtkWidget* found = nullptr;
    int n = 0;
    walk(root, [&](GtkWidget* w) {
        if (found) return false;
        gchar* t = gtk_widget_get_tooltip_text(w);
        if (t && text == t && GTK_IS_BUTTON(w) && n++ == index) found = w;
        g_free(t);
        return true;
    });
    return found;
}

// The value widget next to a label in a two-column grid.
GtkWidget* gridValue(GtkWidget* root, const std::string& text) {
    GtkWidget* found = nullptr;
    walk(root, [&](GtkWidget* w) {
        if (found) return false;
        if (GTK_IS_GRID(w))
            for (int r = 0; r < 32 && !found; ++r) {
                GtkWidget* l = gtk_grid_get_child_at(GTK_GRID(w), 0, r);
                if (l && GTK_IS_LABEL(l) && text == gtk_label_get_text(GTK_LABEL(l)))
                    found = gtk_grid_get_child_at(GTK_GRID(w), 1, r);
            }
        return true;
    });
    return found;
}

// A key press or release as the window would get it from the display server.
void sendKey(App* app, unsigned keycode, bool press) {
    GdkEvent* e = gdk_event_new(press ? GDK_KEY_PRESS : GDK_KEY_RELEASE);
    GdkWindow* win = gtk_widget_get_window(app->window);
    e->key.window = win ? GDK_WINDOW(g_object_ref(win)) : nullptr;
    e->key.hardware_keycode = guint16(keycode);
    e->key.time = GDK_CURRENT_TIME;
    gboolean handled = FALSE;
    g_signal_emit_by_name(app->window, press ? "key-press-event" : "key-release-event", e, &handled);
    gdk_event_free(e);
}

template <typename T>
T* findOf(GtkWidget* root, int index, bool (*is)(GtkWidget*)) {
    int n = 0;
    GtkWidget* found = nullptr;
    walk(root, [&](GtkWidget* w) {
        if (found) return false;
        if (is(w) && n++ == index) found = w;
        return true;
    });
    return reinterpret_cast<T*>(found);
}

std::vector<KeyboardView*> views(GtkWidget* root) {
    std::vector<KeyboardView*> v;
    walk(root, [&](GtkWidget* w) {
        if (auto* kv = static_cast<KeyboardView*>(g_object_get_data(G_OBJECT(w), "k86-view"))) v.push_back(kv);
        return true;
    });
    return v;
}

GtkWidget* page(App* app, const std::string& name) {
    for (auto& p : app->pages)
        if (p.name == name) {
            gtk_stack_set_visible_child_name(GTK_STACK(app->stack), name.c_str());
            pump(30);
            return p.widget;
        }
    return nullptr;
}

int slotOf(App* app, const std::string& id) {
    for (const auto& k : k86Layout())
        if (k.id == id)
            if (auto s = layoutSlot(*app->cache.model, k)) return *s;
    return -1;
}

void click(GtkWidget* w, const std::string& what) {
    if (!w) {
        check(false, "found " + what);
        return;
    }
    if (GTK_IS_MENU_ITEM(w)) gtk_menu_item_activate(GTK_MENU_ITEM(w));
    else gtk_button_clicked(GTK_BUTTON(w));
}

// Reads the simulator's state on the worker thread (the only thread that touches it).
SimulatedTransport::State snapshot(App* app, SimulatedTransport* sim) {
    auto out = std::make_shared<SimulatedTransport::State>();
    app->worker.post("Self-test snapshot", [sim, out](Keyboard&, const Worker::ProgressFn&) { *out = sim->state; });
    settle(app);
    return *out;
}

// Runs a read on the worker thread through the keyboard API (the simulator answers it).
template <typename T>
T readBack(App* app, std::function<T(Keyboard&)> f) {
    auto out = std::make_shared<T>();
    app->worker.post("Self-test read", [f, out](Keyboard& kb, const Worker::ProgressFn&) { *out = f(kb); });
    settle(app);
    return *out;
}

size_t writeCount(App* app, SimulatedTransport* sim) {
    auto n = std::make_shared<size_t>(0);
    app->worker.post("Self-test count", [sim, n](Keyboard&, const Worker::ProgressFn&) { *n = sim->writes().size(); });
    settle(app);
    return *n;
}

// Number of reports with command `cmd` written since write number `mark`.
size_t commandsSince(App* app, SimulatedTransport* sim, size_t mark, uint8_t cmd) {
    auto n = std::make_shared<size_t>(0);
    app->worker.post("Self-test count", [sim, n, mark, cmd](Keyboard&, const Worker::ProgressFn&) {
        const auto& w = sim->writes();
        for (size_t i = mark; i < w.size(); ++i) *n += w[i][0] == cmd;
    });
    settle(app);
    return *n;
}

KeyAction at(const KeyMatrix& m, int slot) { return matrixAction(m, slot); }

}  // namespace

int runSelfTest(App* app, SimulatedTransport* sim) {
    const DeviceModel& m = *app->cache.model;
    std::cout << "self-test on the simulated " << m.displayName << "\n";

    // Keys page: select Caps Lock, pick Left Ctrl on the picker keyboard.
    GtkWidget* keys = page(app, "keys");
    auto kv = views(keys);
    check(kv.size() == 2, "keys page has the keyboard and the picker");
    if (kv.size() < 2) return 1;
    int caps = slotOf(app, "CapsLock"), lctrl = slotOf(app, "ControlLeft"), keyC = slotOf(app, "KeyC");
    kv[0]->onClick(caps, false);
    kv[1]->onClick(lctrl, false);
    settle(app);
    check(at(snapshot(app, sim).keymaps[0], caps) == (KeyAction{0, 0, 0xE0, 0}), "Caps Lock -> Left Ctrl written");

    // Ctrl + C as a combination on Caps Lock.
    GtkWidget* ctrlToggle = findButton(keys, "Ctrl");
    if (ctrlToggle) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctrlToggle), TRUE);
    kv[1]->onClick(keyC, false);
    if (ctrlToggle) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ctrlToggle), FALSE);
    settle(app);
    check(at(snapshot(app, sim).keymaps[0], caps) == (KeyAction{0, 0xE0, 0x06, 0}), "Caps Lock -> Ctrl + C written");

    // Several keys at once (Ctrl-click) get a media key.
    int f1 = slotOf(app, "F1"), f2 = slotOf(app, "F2");
    kv[0]->onClick(f1, false);
    kv[0]->onClick(f2, true);
    click(findButton(keys, "Volume +"), "Volume + button");
    settle(app);
    auto st = snapshot(app, sim);
    check(at(st.keymaps[0], f1) == (KeyAction{3, 0, 0xE9, 0}) && at(st.keymaps[0], f2) == (KeyAction{3, 0, 0xE9, 0}),
          "F1 and F2 -> Volume + written");

    // Macro assignment with the toggle mode.
    kv[0]->onClick(f2, false);
    GtkWidget* toggleMode = findButton(keys, "Toggle: press to start, press again to stop");
    if (toggleMode) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggleMode), TRUE);
    click(findButton(keys, "Assign macro"), "Assign macro button");
    settle(app);
    check(at(snapshot(app, sim).keymaps[0], f2) == (KeyAction{9, 1, 0, 0}), "F2 -> Macro 1 (toggle) written");

    // Restore default and disable.
    kv[0]->onClick(caps, false);
    click(findButton(keys, "Restore default"), "Restore default button");
    kv[0]->onClick(f1, false);
    click(findButton(keys, "Disable key"), "Disable key button");
    settle(app);
    st = snapshot(app, sim);
    check(at(st.keymaps[0], caps) == (KeyAction{0, 0, 0x39, 0}), "Caps Lock restored");
    check(at(st.keymaps[0], f1) == (KeyAction{0, 0, 0, 0}), "F1 disabled");
    check(at(app->cache.keymaps[0], caps) == at(st.keymaps[0], caps), "window cache matches the keyboard");

    // Another profile: view profile 2, change a key there, make it the active profile.
    click(app->profileButtons[1], "Profile 2 button");
    pump(50);
    int esc = slotOf(app, "Escape");
    kv[0]->onClick(esc, false);
    kv[1]->onClick(slotOf(app, "Backquote"), false);
    click(findButton(app->window, "Use on keyboard"), "Use on keyboard button");
    settle(app);
    st = snapshot(app, sim);
    check(at(st.keymaps[1], esc) == (KeyAction{0, 0, 0x35, 0}) && at(st.keymaps[0], esc) == (KeyAction{0, 0, 0x29, 0}),
          "Esc -> ` written to profile 2 only");
    check(st.profile == 1, "profile 2 made active");
    click(app->profileButtons[0], "Profile 1 button");
    pump(50);

    // Fn layer: Fn + 1 -> Bluetooth 2.
    GtkWidget* fn = page(app, "fn");
    auto fv = views(fn);
    int d1 = slotOf(app, "Digit1");
    fv[0]->onClick(d1, false);
    click(findButton(fn, "Bluetooth 2"), "Bluetooth 2 button");
    settle(app);
    check(at(snapshot(app, sim).fn[0][0], d1) == (KeyAction{14, 0, 1, 0}), "Fn + 1 -> Bluetooth 2 written");

    // Lighting: Static, green.
    GtkWidget* light = page(app, "lighting");
    click(findButton(light, "Static"), "Static effect");
    settle(app);
    GtkWidget* green = nullptr;
    walk(light, [&](GtkWidget* w) {
        if (green) return false;
        gchar* t = gtk_widget_get_tooltip_text(w);
        if (t && std::string(t) == "Green" && GTK_IS_BUTTON(w)) green = w;
        g_free(t);
        return true;
    });
    click(green, "green swatch");
    settle(app);
    st = snapshot(app, sim);
    check(st.light[0] == 1 && st.light[4] == 0x00 && st.light[5] == 0xFF && st.light[6] == 0x00,
          "backlight -> static green written");

    // Per-key colours: paint W blue, apply.
    GtkWidget* pk = page(app, "perkey");
    auto pv = views(pk);
    GtkWidget* blue = nullptr;
    walk(pk, [&](GtkWidget* w) {
        if (blue) return false;
        gchar* t = gtk_widget_get_tooltip_text(w);
        if (t && std::string(t) == "Blue" && GTK_IS_BUTTON(w)) blue = w;
        g_free(t);
        return true;
    });
    click(blue, "blue swatch");
    int w = slotOf(app, "KeyW");
    pv[0]->onDrag(w);
    click(findButton(pk, "Apply to keyboard"), "Apply to keyboard");
    settle(app);
    st = snapshot(app, sim);
    check(st.pictures[0][size_t(w) * 3] == 0 && st.pictures[0][size_t(w) * 3 + 2] == 0xFF, "W painted blue");

    // Macros: macro 2 = tap A, tap B, repeat 3.
    GtkWidget* mac = page(app, "macros");
    GtkListBox* list = findOf<GtkListBox>(mac, 0, [](GtkWidget* w) -> bool { return GTK_IS_LIST_BOX(w); });
    gtk_list_box_select_row(list, gtk_list_box_get_row_at_index(list, 1));
    pump(30);
    auto mv = views(mac);
    mv[0]->onClick(slotOf(app, "KeyA"), false);
    mv[0]->onClick(slotOf(app, "KeyB"), false);
    GtkSpinButton* rep = findOf<GtkSpinButton>(mac, 0, [](GtkWidget* w) -> bool { return GTK_IS_SPIN_BUTTON(w); });
    gtk_spin_button_set_value(rep, 3);
    click(findButton(mac, "Save to keyboard"), "Save to keyboard");
    settle(app);
    st = snapshot(app, sim);
    Macro got = decodeMacro(std::vector<uint8_t>(st.macros[1].begin(), st.macros[1].end()));
    check(macroToString(got) == "+A/10 -A/10 +B/10 -B/10" && got.repeat == 3, "macro 2 written: " + macroToString(got));

    // Settings: debounce 8.
    GtkWidget* set = page(app, "settings");
    // The spin button next to the "Debounce (ms)" label (grid children are not in insertion order).
    GtkWidget* deb = nullptr;
    walk(set, [&](GtkWidget* w) {
        if (deb) return false;
        if (GTK_IS_GRID(w)) {
            GtkWidget* l = gtk_grid_get_child_at(GTK_GRID(w), 0, 0);
            if (l && GTK_IS_LABEL(l) && std::string(gtk_label_get_text(GTK_LABEL(l))) == "Debounce (ms)")
                deb = gtk_grid_get_child_at(GTK_GRID(w), 1, 0);
        }
        return true;
    });
    if (deb) gtk_spin_button_set_value(GTK_SPIN_BUTTON(deb), 8);
    click(findButton(set, "Save settings"), "Save settings");
    settle(app);
    check(snapshot(app, sim).debounce == 8, "debounce 8 written");

    // Lighting details: brightness, speed and direction of the backlight (Wave), then the side light.
    click(findButton(light, "Wave"), "Wave effect");
    settle(app);
    GtkRange* bright = findOf<GtkRange>(light, 0, [](GtkWidget* w) -> bool { return GTK_IS_SCALE(w); });
    GtkRange* speed = findOf<GtkRange>(light, 1, [](GtkWidget* w) -> bool { return GTK_IS_SCALE(w); });
    GtkComboBox* dir = findOf<GtkComboBox>(light, 0, [](GtkWidget* w) -> bool { return GTK_IS_COMBO_BOX(w); });
    if (bright) gtk_range_set_value(bright, 2);
    if (speed) gtk_range_set_value(speed, 1);
    if (dir) gtk_combo_box_set_active(dir, 1);
    settle(app);
    auto lit = readBack<std::optional<LightSetting>>(app, [](Keyboard& kb) { return kb.getLight(); });
    check(lit && lit->mode == 4 && lit->brightness == 2 && lit->speed == 1 && lit->option == 1,
          "backlight -> wave, brightness 2, speed 1, direction 2 written");
    if (!m.sideLightModes.empty()) {
        click(findButton(light, "Static", 1), "side light Static effect");
        settle(app);
        click(findTooltip(light, "Purple", 1), "side light purple swatch");
        settle(app);
        auto side = readBack<std::optional<LightSetting>>(app, [](Keyboard& kb) { return kb.getSideLight(); });
        check(side && side->mode == 1 && !side->rainbow && app->cache.side && side->color == app->cache.side->color &&
                  side->color != 0,
              "side light -> static purple written");
        lit = readBack<std::optional<LightSetting>>(app, [](Keyboard& kb) { return kb.getLight(); });
        check(lit && lit->mode == 4, "side light change leaves the backlight alone");
    }

    // Per-key colours: fill everything red; "Use the Custom effect" switches the backlight.
    GtkWidget* red = findTooltip(pk, "Red");
    click(red, "red swatch");
    click(findButton(pk, "Fill all"), "Fill all button");
    click(findButton(pk, "Apply to keyboard"), "Apply to keyboard");
    click(findButton(pk, "Use the Custom effect"), "Use the Custom effect button");
    settle(app);
    st = snapshot(app, sim);
    check(st.pictures[0][size_t(caps) * 3] == 0xFF && st.pictures[0][size_t(caps) * 3 + 1] == 0 &&
              st.pictures[0][size_t(w) * 3] == 0xFF,
          "Fill all red written");
    lit = readBack<std::optional<LightSetting>>(app, [](Keyboard& kb) { return kb.getLight(); });
    check(lit && lit->mode == 13, "backlight -> Custom effect");
    if (m.chip == Chip::YC3123) {
        // Pattern 3: paint Esc green and apply; pattern 1 must stay red.
        GtkComboBox* pat = findOf<GtkComboBox>(pk, 0, [](GtkWidget* w) -> bool { return GTK_IS_COMBO_BOX(w); });
        if (pat) gtk_combo_box_set_active(pat, 2);
        pump(30);
        click(findTooltip(pk, "Green"), "green swatch");
        pv[0]->onDrag(esc);
        click(findButton(pk, "Apply to keyboard"), "Apply to keyboard (pattern 3)");
        settle(app);
        st = snapshot(app, sim);
        check(st.pictures[2][size_t(esc) * 3 + 1] == 0xFF && st.pictures[2][size_t(esc) * 3] == 0 &&
                  st.pictures[0][size_t(esc) * 3] == 0xFF,
              "pattern 3 written, pattern 1 untouched");
        if (pat) gtk_combo_box_set_active(pat, 0);
        pump(30);
    }

    // Macro editor: record from the keyboard (A, B with auto-repeat), add a mouse click, edit.
    page(app, "macros");
    gtk_list_box_select_row(list, gtk_list_box_get_row_at_index(list, 2));
    pump(30);
    GtkWidget* rec = findButton(mac, "● Record");
    check(rec != nullptr, "found the Record button");
    if (rec) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rec), TRUE);
        const unsigned kA = 30 + 8, kB = 48 + 8;  // evdev KEY_A / KEY_B as X11 key codes
        sendKey(app, kA, true);
        pump(40);
        sendKey(app, kA, true);  // auto-repeat: ignored
        pump(40);
        sendKey(app, kA, false);
        pump(20);
        sendKey(app, kB, true);
        sendKey(app, kB, false);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(rec), FALSE);
        click(findButton(mac, "Right"), "Add mouse click > Right");
        click(findButton(mac, "Save to keyboard"), "Save to keyboard (recorded)");
        settle(app);
        st = snapshot(app, sim);
        got = decodeMacro(std::vector<uint8_t>(st.macros[2].begin(), st.macros[2].end()));
        bool shape = got.events.size() == 6 && got.events[0].code == 0x04 && got.events[1].code == 0x04 &&
                     got.events[2].code == 0x05 && got.events[3].code == 0x05 &&
                     got.events[4].type == MacroEvent::Type::MouseDown && got.events[5].type == MacroEvent::Type::MouseUp;
        check(shape, "recorded macro 3 written: " + macroToString(got));
        check(shape && got.events[0].delayMs >= 70 && got.events[1].delayMs >= 15 && got.events[3].delayMs == 10,
              "recorded delays follow the real timing");
    }
    // Clear macro 2 and save: the slot is emptied on the keyboard.
    gtk_list_box_select_row(list, gtk_list_box_get_row_at_index(list, 1));
    pump(30);
    click(findButton(mac, "Clear"), "Clear button");
    click(findButton(mac, "Save to keyboard"), "Save to keyboard (cleared)");
    settle(app);
    st = snapshot(app, sim);
    check(decodeMacro(std::vector<uint8_t>(st.macros[1].begin(), st.macros[1].end())).events.empty(),
          "cleared macro 2 written");

    // Screen: clock, still image upload.
    GtkWidget* scr = page(app, "screen");
    size_t mark = writeCount(app, sim);
    click(findButton(scr, "Set the clock to this computer's time"), "Set the clock button");
    settle(app);
    st = snapshot(app, sim);
    check(commandsSince(app, sim, mark, 0x28) == 1, "clock set");
    gchar* tmpPath = nullptr;
    int fd = g_file_open_tmp("k86-selftest-XXXXXX.png", &tmpPath, nullptr);
    if (fd >= 0) close(fd);
    GdkPixbuf* pb = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 36);
    gdk_pixbuf_fill(pb, 0x3366ccff);
    bool saved = tmpPath && gdk_pixbuf_save(pb, tmpPath, "png", nullptr, nullptr);
    g_object_unref(pb);
    GtkWidget* chooser = findOf<GtkWidget>(scr, 0, [](GtkWidget* w) -> bool { return GTK_IS_FILE_CHOOSER(w); });
    if (saved && chooser) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), tmpPath);
        for (int i = 0; i < 100; ++i) {
            gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
            bool ok = f != nullptr;
            g_free(f);
            if (ok) break;
            pump(10);
        }
        mark = writeCount(app, sim);
        click(findButton(scr, "Upload"), "Upload button");
        settle(app);
        check(commandsSince(app, sim, mark, 0xA5) == 1 && commandsSince(app, sim, mark, 0x25) > 0,
              "image upload announced and streamed");
    } else {
        check(false, "test image prepared");
    }
    if (tmpPath) g_unlink(tmpPath), g_free(tmpPath);

    // Settings: an option switch and the sleep times.
    page(app, "settings");
    GtkWidget* sw = gridValue(set, "Swap WASD and the arrow keys");
    if (sw) gtk_switch_set_active(GTK_SWITCH(sw), TRUE);
    GtkWidget* sleepBt = gridValue(set, "Sleep after, Bluetooth (s)");
    if (sleepBt) gtk_spin_button_set_value(GTK_SPIN_BUTTON(sleepBt), 300);
    click(findButton(set, "Save settings"), "Save settings (options, sleep)");
    settle(app);
    auto opts = readBack<std::map<std::string, int>>(app, [](Keyboard& kb) { return kb.getOptions(); });
    auto slp = readBack<std::optional<SleepTimes>>(app, [](Keyboard& kb) { return kb.getSleep(); });
    check(sw && opts.count("wasdSwap") && opts["wasdSwap"] == 1, "WASD swap option written");
    check(slp && slp->bluetooth == 300, "Bluetooth sleep 300 s written");
    check(app->errorCount == 0, "no errors so far");
    // An out-of-range sleep time is refused before anything is sent.
    int errs = app->errorCount;
    mark = writeCount(app, sim);
    GtkWidget* deepBt = gridValue(set, "Deep sleep after, Bluetooth (s)");
    if (deepBt) gtk_spin_button_set_value(GTK_SPIN_BUTTON(deepBt), 1);  // below the minimum on both chips
    click(findButton(set, "Save settings"), "Save settings (invalid)");
    settle(app);
    check(app->errorCount == errs + 1 && writeCount(app, sim) == mark, "invalid sleep time refused");
    click(findButton(set, "Revert"), "Revert settings");
    pump(30);
    check(deepBt && gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(deepBt)) == slp->deepBluetooth,
          "Revert restores the form");

    // Read-only mode refuses writes in the window itself.
    app->opt.readOnly = true;
    errs = app->errorCount;
    mark = writeCount(app, sim);
    page(app, "keys");
    kv[0]->onClick(caps, false);
    kv[1]->onClick(lctrl, false);
    click(findButton(set, "Save settings"), "Save settings (read-only)");
    settle(app);
    check(writeCount(app, sim) == mark && app->errorCount > errs, "read-only mode writes nothing");
    check(at(app->cache.keymaps[0], caps) == (KeyAction{0, 0, 0x39, 0}), "read-only mode leaves the cache alone");
    app->opt.readOnly = false;
    errs = app->errorCount;

    // Confirmations: "no" writes nothing, "yes" restores the factory layout of the profile.
    page(app, "keys");
    kv[0]->onClick(caps, false);
    kv[1]->onClick(lctrl, false);
    settle(app);
    app->autoConfirm = false;
    mark = writeCount(app, sim);
    click(findButton(keys, "Restore factory layout…"), "Restore factory layout button");
    settle(app);
    check(writeCount(app, sim) == mark, "declined confirmation writes nothing");
    app->autoConfirm = true;
    click(findButton(keys, "Restore factory layout…"), "Restore factory layout button");
    settle(app);
    st = snapshot(app, sim);
    check(std::equal(st.keymaps[0].begin(), st.keymaps[0].end(), m.defaultMatrix), "factory layout restored");
    check(at(st.keymaps[1], esc) == (KeyAction{0, 0, 0x35, 0}), "other profiles keep their changes");

    // Factory reset, then everything is read again.
    click(findButton(set, "Factory reset…"), "Factory reset button");
    settle(app);
    settle(app);
    st = snapshot(app, sim);
    check(st.debounce == 5 && std::equal(st.keymaps[1].begin(), st.keymaps[1].end(), m.defaultMatrix),
          "factory reset sent");
    check(app->cache.debounce == 5 && at(app->cache.keymaps[1], esc) == (KeyAction{0, 0, 0x29, 0}) &&
              app->cache.light && app->cache.light->mode == st.light[0],
          "window re-read the keyboard after the reset");
    app->autoConfirm.reset();

    // Pulling the cable: a write fails with ENODEV, the queued reads are dropped without an error
    // each, the window shows the "connect your keyboard" page and picks the keyboard up again (the
    // dry run reconnects a fresh simulator on the next hotplug poll). `sim` is gone after this.
    app->worker.post("Self-test unplug", [sim](Keyboard&, const Worker::ProgressFn&) { sim->unplugged = true; });
    page(app, "keys");
    kv[0]->onClick(caps, false);
    kv[1]->onClick(lctrl, false);
    app->reload();
    settle(app);
    check(!app->worker.connected() && app->worker.pending() == 0, "unplugged keyboard noticed");
    check(std::string(gtk_stack_get_visible_child_name(GTK_STACK(app->outer))) == "empty",
          "window shows the connect page");
    for (int i = 0; i < 800 && !(app->worker.connected() && app->worker.pending() == 0 && app->cache.loaded); ++i)
        pump(10);
    settle(app);
    check(app->worker.connected() && app->cache.loaded &&
              std::string(gtk_stack_get_visible_child_name(GTK_STACK(app->outer))) == "pages",
          "keyboard picked up again and read");
    check(app->cache.keymaps.size() == size_t(m.layers) && at(app->cache.keymaps[0], caps) == (KeyAction{0, 0, 0x39, 0}),
          "fresh key maps after reconnecting");

    check(app->errorCount == errs, "no unexpected errors (" + std::to_string(app->errorCount) + " shown)");

    std::cout << (failures ? "self-test FAILED (" + std::to_string(failures) + ")" : std::string("self-test passed"))
              << "\n";
    return failures ? 1 : 0;
}

}  // namespace k86gui
