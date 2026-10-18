// k86-gui --self-test: drives the real widgets against the simulated keyboard and checks what
// ends up in its memory. Covers the write paths of every page without touching hardware.
#include <iostream>

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

GtkWidget* findButton(GtkWidget* root, const std::string& text) {
    GtkWidget* found = nullptr;
    walk(root, [&](GtkWidget* w) {
        if (found) return false;
        const char* l = nullptr;
        if (GTK_IS_BUTTON(w)) l = gtk_button_get_label(GTK_BUTTON(w));
        if (GTK_IS_MENU_ITEM(w)) l = gtk_menu_item_get_label(GTK_MENU_ITEM(w));
        if (l && text == l) found = w;
        return true;
    });
    return found;
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

    // Restore factory layout of profile 1 (confirmations are answered "no" in tests, so call
    // the write directly through the cache check: nothing must have been written).
    size_t before = 0;
    app->worker.post("count", [sim, &before](Keyboard&, const Worker::ProgressFn&) { before = sim->writes().size(); });
    settle(app);
    click(findButton(keys, "Restore factory layout…"), "Restore factory layout button");
    settle(app);
    size_t after = 0;
    app->worker.post("count", [sim, &after](Keyboard&, const Worker::ProgressFn&) { after = sim->writes().size(); });
    settle(app);
    check(before == after, "declined confirmation writes nothing");

    std::cout << (failures ? "self-test FAILED (" + std::to_string(failures) + ")" : std::string("self-test passed"))
              << "\n";
    return failures ? 1 : 0;
}

}  // namespace k86gui
