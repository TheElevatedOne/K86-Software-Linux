// "Keys" and "Fn layer" pages: the keyboard drawing on top, the selected key's assignment below.
// Picking an action writes it to the keyboard right away (like the original software).
#include <algorithm>

#include "app.hpp"
#include "layout.hpp"
#include "macro.hpp"

using namespace k86;

namespace k86gui {

namespace {

struct KeysPage {
    App* app = nullptr;
    bool fn = false;
    int os = 0;  // Fn layer OS variant (YC3123 only)
    KeyboardView* view = nullptr;
    KeyboardView* picker = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* detail = nullptr;
    GtkWidget* osBox = nullptr;
    GtkWidget* osCombo = nullptr;
    GtkWidget* mods[4] = {};
    GtkWidget* macroCombo = nullptr;
    GtkWidget* macroMode[3] = {};
    GtkWidget* sideTab = nullptr;
    GtkWidget* notebook = nullptr;
    GtkWidget* layerTitle = nullptr;

    bool ready() const {
        const Cache& c = app->cache;
        if (!c.loaded || !c.model) return false;
        size_t p = size_t(c.profile);
        return fn ? p < c.fn.size() : p < c.keymaps.size();
    }
    KeyMatrix& matrix() {
        Cache& c = app->cache;
        return fn ? c.fn[size_t(c.profile)][size_t(os)] : c.keymaps[size_t(c.profile)];
    }
    KeyAction factory(int slot) const {
        const uint8_t* d = (fn ? app->cache.model->defaultFnMatrix : app->cache.model->defaultMatrix) + slot * 4;
        return {d[0], d[1], d[2], d[3]};
    }
};

void updateDetail(KeysPage* p) {
    const auto& sel = p->view->selected;
    if (!p->ready() || sel.empty()) {
        gtk_label_set_text(GTK_LABEL(p->title), "Select a key");
        gtk_label_set_text(GTK_LABEL(p->detail),
                           p->fn ? "Click a key on the keyboard to change what Fn + that key does. Ctrl-click selects "
                                   "several keys."
                                 : "Click a key on the keyboard to change what it does. Ctrl-click selects several "
                                   "keys.");
        return;
    }
    const DeviceModel& m = *p->app->cache.model;
    if (sel.size() > 1) {
        gtk_label_set_text(GTK_LABEL(p->title), (std::to_string(sel.size()) + " keys selected").c_str());
        gtk_label_set_text(GTK_LABEL(p->detail), "The action you pick is assigned to all of them.");
        return;
    }
    int slot = *sel.begin();
    KeyAction a = matrixAction(p->matrix(), slot), f = p->factory(slot);
    std::string key = (p->fn ? "Fn + " : "") + slotLabel(m, slot);
    gtk_label_set_text(GTK_LABEL(p->title), (key + "  →  " + actionLabel(a)).c_str());
    std::string d = a == f ? std::string("Factory setting.") : "Factory setting: " + actionLabel(f) + ".";
    gtk_label_set_text(GTK_LABEL(p->detail), d.c_str());
}

void redraw(KeysPage* p) {
    p->view->redraw();
    updateDetail(p);
}

// Writes `a` to every selected key. Many keys at once go out as one full-matrix write.
void assign(KeysPage* p, const KeyAction& a) {
    App* app = p->app;
    if (!p->ready()) return;
    if (p->view->selected.empty()) {
        app->status("Select a key on the keyboard first.");
        return;
    }
    if (!app->canWrite()) return;
    std::vector<int> slots;
    for (int s : p->view->selected)
        if (matrixAction(p->matrix(), s) != a) slots.push_back(s);
    if (slots.empty()) return;

    KeyMatrix before = p->matrix();
    for (int s : slots) std::copy(a.begin(), a.end(), p->matrix().begin() + s * 4);
    KeyMatrix after = p->matrix();
    redraw(p);

    const bool fn = p->fn;
    const uint8_t layer = uint8_t(app->cache.profile), os = uint8_t(p->os);
    auto restore = [p, before, layer, os, fn](const std::string& e) {
        if (fn) p->app->cache.fn[layer][os] = before;
        else p->app->cache.keymaps[layer] = before;
        redraw(p);
        p->app->error("Could not change the key: " + e);
    };
    std::string what = slots.size() == 1 ? "Writing key" : "Writing keys";
    p->app->worker.post(
        what,
        [fn, layer, os, slots, a, after](Keyboard& kb, const Worker::ProgressFn&) {
            if (slots.size() >= 6) {
                fn ? kb.setFnMatrix(layer, os, after) : kb.setKeyMatrix(layer, after);
                return;
            }
            for (int s : slots) fn ? kb.setFnKey(layer, os, uint8_t(s), a) : kb.setKey(layer, uint8_t(s), a);
        },
        [app, slots, a] {
            app->status(slots.size() == 1 ? "Saved: " + actionLabel(a)
                                          : "Saved " + std::to_string(slots.size()) + " keys: " + actionLabel(a));
        },
        restore);
}

void resetLayer(KeysPage* p) {
    App* app = p->app;
    if (!p->ready() || !app->canWrite()) return;
    std::string what = p->fn ? "the Fn layer" : "all keys";
    if (!app->confirm("Restore the factory setting of " + what + " in profile " +
                      std::to_string(app->cache.profile + 1) + "?"))
        return;
    KeyMatrix def;
    std::copy_n(p->fn ? app->cache.model->defaultFnMatrix : app->cache.model->defaultMatrix, def.size(), def.begin());
    KeyMatrix before = p->matrix();
    p->matrix() = def;
    redraw(p);
    const bool fn = p->fn;
    const uint8_t layer = uint8_t(app->cache.profile), os = uint8_t(p->os);
    app->worker.post(
        "Restoring factory layout",
        [fn, layer, os, def](Keyboard& kb, const Worker::ProgressFn&) {
            fn ? kb.setFnMatrix(layer, os, def) : kb.setKeyMatrix(layer, def);
        },
        [app] { app->status("Factory layout restored."); },
        [p, before](const std::string& e) {
            p->matrix() = before;
            redraw(p);
            p->app->error("Could not restore the layout: " + e);
        });
}

KeyAction keyWithModifiers(KeysPage* p, uint8_t usage) {
    static const uint8_t modUsage[4] = {0xE0, 0xE1, 0xE2, 0xE3};
    std::vector<uint8_t> keys;
    for (int i = 0; i < 4; ++i)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->mods[i])) && modUsage[i] != usage)
            keys.push_back(modUsage[i]);
    keys.push_back(usage);
    if (keys.size() > 3) keys.erase(keys.begin(), keys.end() - 3);
    if (keys.size() == 1) return {0, 0, keys[0], 0};
    if (keys.size() == 2) return {0, keys[0], keys[1], 0};
    return {0, keys[0], keys[1], keys[2]};
}

GtkWidget* actionButton(KeysPage* p, const ActionChoice& c) {
    GtkWidget* b = button(c.label, [p, a = c.action] { assign(p, a); });
    gtk_style_context_add_class(gtk_widget_get_style_context(b), "k86-action");
    if (!c.tooltip.empty()) gtk_widget_set_tooltip_text(b, c.tooltip.c_str());
    return b;
}

GtkWidget* categoryTab(KeysPage* p, const std::string& category, const std::string& note = {}) {
    GtkWidget* box = vbox(8, 8);
    if (!note.empty()) gtk_box_pack_start(GTK_BOX(box), label(note, true), FALSE, FALSE, 0);
    GtkWidget* flow = flowBox();
    for (const auto& c : actionCatalog())
        if (c.category == category) gtk_container_add(GTK_CONTAINER(flow), actionButton(p, c));
    gtk_box_pack_start(GTK_BOX(box), flow, FALSE, FALSE, 0);
    return box;
}

GtkWidget* keyboardTab(KeysPage* p) {
    GtkWidget* box = vbox(8, 8);
    GtkWidget* row = hbox(6);
    gtk_box_pack_start(GTK_BOX(row), label("Press together with:"), FALSE, FALSE, 0);
    const char* names[4] = {"Ctrl", "Shift", "Alt", "Win"};
    for (int i = 0; i < 4; ++i) {
        p->mods[i] = gtk_toggle_button_new_with_label(names[i]);
        gtk_box_pack_start(GTK_BOX(row), p->mods[i], FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(row), label("then click the key to send below.", true), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    p->picker = new KeyboardView();
    gtk_widget_set_size_request(p->picker->widget(), 480, 170);
    p->picker->describe = [](const KeyGeometry& k, int) {
        return KeyLook{k.knob ? std::string() : k.legend, {}, KeyLook::Style::Normal, false, 0};
    };
    p->picker->tooltip = [](const KeyGeometry& k, int) { return actionLabel(k.factory); };
    p->picker->onClick = [p](int slot, bool) {
        const KeyGeometry* k = p->picker->keyForSlot(slot);
        if (!k) return;
        KeyAction a = k->factory;
        if (a[0] == 0) a = keyWithModifiers(p, a[2]);
        assign(p, a);
    };
    p->picker->selectable = false;
    gtk_box_pack_start(GTK_BOX(box), p->picker->widget(), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), label("Keys that are not on the K86:", true), FALSE, FALSE, 0);
    GtkWidget* flow = flowBox();
    std::vector<uint8_t> extra;
    for (uint8_t u = 0x68; u <= 0x73; ++u) extra.push_back(u);  // F13-F24
    for (uint8_t u : {0x53, 0x54, 0x55, 0x56, 0x57, 0x58}) extra.push_back(u);
    for (uint8_t u = 0x59; u <= 0x63; ++u) extra.push_back(u);  // numpad digits and dot
    for (uint8_t u : {0x67, 0x64, 0x32, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x90, 0x91, 0x74, 0x75, 0x77, 0x78, 0x79, 0x7A,
                      0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x66})
        extra.push_back(u);
    for (uint8_t u : extra) {
        GtkWidget* b = button(keyLabel(u), [p, u] { assign(p, keyWithModifiers(p, u)); });
        gtk_container_add(GTK_CONTAINER(flow), b);
    }
    gtk_box_pack_start(GTK_BOX(box), flow, FALSE, FALSE, 0);
    return box;
}

void refreshMacroCombo(KeysPage* p) {
    GtkComboBoxText* c = GTK_COMBO_BOX_TEXT(p->macroCombo);
    int active = std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(c)));
    gtk_combo_box_text_remove_all(c);
    const auto& macros = p->app->cache.macros;
    for (int i = 0; i < kMaxMacros; ++i) {
        std::string t = "Macro " + std::to_string(i + 1);
        if (size_t(i) < macros.size() && macros[size_t(i)]) {
            const Macro& m = *macros[size_t(i)];
            t += m.events.empty() ? "  (empty)" : "  (" + std::to_string(m.events.size()) + " events)";
        }
        gtk_combo_box_text_append_text(c, t.c_str());
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
}

GtkWidget* macroTab(KeysPage* p) {
    GtkWidget* box = vbox(8, 8);
    GtkWidget* row = hbox(8);
    p->macroCombo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(row), label("Macro:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), p->macroCombo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    const char* modes[3] = {"Play it (as many times as its repeat count)", "Toggle: press to start, press again to stop",
                            "Repeat while the key is held"};
    for (int i = 0; i < 3; ++i) {
        p->macroMode[i] = gtk_radio_button_new_with_label_from_widget(
            i ? GTK_RADIO_BUTTON(p->macroMode[0]) : nullptr, modes[i]);
        gtk_box_pack_start(GTK_BOX(box), p->macroMode[i], FALSE, FALSE, 0);
    }
    GtkWidget* row2 = hbox(8);
    gtk_box_pack_start(GTK_BOX(row2), button("Assign macro", [p] {
                           int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(p->macroCombo));
                           if (idx < 0) return;
                           uint8_t type = 0;
                           for (int i = 0; i < 3; ++i)
                               if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->macroMode[i]))) type = uint8_t(i);
                           assign(p, KeyAction{9, type, uint8_t(idx), 0});
                       }),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row2), button("Edit macros…", [p] {
                           gtk_stack_set_visible_child_name(GTK_STACK(p->app->stack), "macros");
                       }),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row2, FALSE, FALSE, 0);
    return box;
}

}  // namespace

Page buildKeysPage(App* app, bool fnLayer) {
    auto* p = new KeysPage();
    p->app = app;
    p->fn = fnLayer;

    GtkWidget* page = vbox(10, 14);
    g_object_set_data_full(G_OBJECT(page), "page", p, [](gpointer d) { delete static_cast<KeysPage*>(d); });

    GtkWidget* top = hbox(10);
    p->layerTitle = heading(fnLayer ? "Fn layer" : "Keys");
    gtk_box_pack_start(GTK_BOX(top), p->layerTitle, FALSE, FALSE, 0);
    if (fnLayer) {
        p->osBox = hbox(6);
        p->osCombo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->osCombo), "Windows");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->osCombo), "macOS");
        gtk_combo_box_set_active(GTK_COMBO_BOX(p->osCombo), 0);
        gtk_box_pack_start(GTK_BOX(p->osBox), label("Fn layer for:"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(p->osBox), p->osCombo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(top), p->osBox, FALSE, FALSE, 12);
        onChanged(p->osCombo, "changed", [p] {
            p->os = std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(p->osCombo)));
            redraw(p);
        });
    }
    GtkWidget* reset = button("Restore factory layout…", [p] { resetLayer(p); });
    gtk_box_pack_end(GTK_BOX(top), reset, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), top, FALSE, FALSE, 0);

    p->view = new KeyboardView();
    p->view->describe = [p](const KeyGeometry& k, int slot) -> KeyLook {
        KeyLook look;
        if (!p->ready()) return {k.knob ? "" : k.legend, {}, KeyLook::Style::Dim, false, 0};
        KeyAction a = matrixAction(p->matrix(), slot), f = p->factory(slot);
        std::string legend = k.knob ? std::string() : k.legend;
        if (p->fn) {
            if (a == KeyAction{0, 0, 0, 0}) return {legend, {}, KeyLook::Style::Dim, false, 0};
            return {actionShortLabel(a), legend, a == f ? KeyLook::Style::Function : KeyLook::Style::Changed, false, 0};
        }
        if (a == f) return {legend, {}, KeyLook::Style::Normal, false, 0};
        if (a == KeyAction{0, 0, 0, 0}) return {legend, {}, KeyLook::Style::Disabled, false, 0};
        return {actionShortLabel(a), legend, KeyLook::Style::Changed, false, 0};
    };
    p->view->tooltip = [p](const KeyGeometry&, int slot) -> std::string {
        if (!p->ready()) return {};
        KeyAction a = matrixAction(p->matrix(), slot), f = p->factory(slot);
        std::string t = (p->fn ? "Fn + " : "") + slotLabel(*p->app->cache.model, slot) + "\n" + actionLabel(a);
        if (a != f) t += "\nFactory: " + actionLabel(f);
        return t;
    };
    p->view->onClick = [p](int slot, bool extend) {
        auto& sel = p->view->selected;
        if (!extend) sel = {slot};
        else if (sel.count(slot)) sel.erase(slot);
        else sel.insert(slot);
        updateDetail(p);
    };
    gtk_box_pack_start(GTK_BOX(page), p->view->widget(), FALSE, FALSE, 0);

    // Selected key header.
    GtkWidget* info = hbox(10);
    GtkWidget* texts = vbox(2);
    p->title = label("");
    gtk_style_context_add_class(gtk_widget_get_style_context(p->title), "k86-current");
    p->detail = label("", true);
    gtk_box_pack_start(GTK_BOX(texts), p->title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(texts), p->detail, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(info), texts, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(info), button(fnLayer ? "No Fn function" : "Disable key",
                                           [p] { assign(p, KeyAction{0, 0, 0, 0}); }),
                     FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(info), button("Restore default", [p] {
                         if (!p->ready() || p->view->selected.empty()) return;
                         // Each key goes back to its own factory action.
                         auto sel = p->view->selected;
                         for (int s : sel) {
                             p->view->selected = {s};
                             assign(p, p->factory(s));
                         }
                         p->view->selected = sel;
                         updateDetail(p);
                     }),
                     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), info, FALSE, FALSE, 0);

    // Action categories.
    p->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(p->notebook), TRUE);
    auto add = [p](GtkWidget* w, const char* title) {
        gtk_notebook_append_page(GTK_NOTEBOOK(p->notebook), scrolledV(w), gtk_label_new(title));
        return w;
    };
    add(keyboardTab(p), "Keyboard");
    add(categoryTab(p, "Media"), "Media");
    add(categoryTab(p, "Mouse"), "Mouse");
    add(macroTab(p), "Macro");
    add(categoryTab(p, "Lighting", "K86 lighting functions (as on the factory Fn layer)."), "Lighting");
    p->sideTab = add(categoryTab(p, "Side light", "Side light functions (as on the factory Fn layer)."), "Side light");
    add(categoryTab(p, "Connection", "Wireless channel switching (as on the factory Fn layer)."), "Connection");
    add(categoryTab(p, "System"), "System");
    add(categoryTab(p, "Profile"), "Profile");
    gtk_widget_set_vexpand(p->notebook, TRUE);
    gtk_box_pack_start(GTK_BOX(page), p->notebook, TRUE, TRUE, 0);

    Page result;
    result.name = fnLayer ? "fn" : "keys";
    result.title = fnLayer ? "Fn layer" : "Keys";
    result.widget = page;
    result.refresh = [p] {
        const DeviceModel* m = p->app->cache.model;
        if (p->view->model() != m) {
            p->view->setModel(m);
            p->picker->setModel(m);
        }
        if (p->osBox) gtk_widget_set_visible(p->osBox, m && m->chip == Chip::YC3123);
        if (m && m->chip != Chip::YC3123) p->os = 0;
        gtk_widget_set_visible(gtk_widget_get_parent(gtk_widget_get_parent(p->sideTab)),
                               m && !m->sideLightModes.empty());
        refreshMacroCombo(p);
        std::string t = (p->fn ? "Fn layer" : "Keys") + std::string(" — profile ") +
                        std::to_string(p->app->cache.profile + 1);
        gtk_label_set_text(GTK_LABEL(p->layerTitle), t.c_str());
        redraw(p);
        p->picker->redraw();
    };
    // Test hook for screenshots: select a key by layout id.
    g_object_set_data(G_OBJECT(page), "select", p);
    return result;
}

// Selects the key with the given layout id on a keys page (used by --screenshot).
void selectKeyOnPage(GtkWidget* page, const std::string& id) {
    auto* p = static_cast<KeysPage*>(g_object_get_data(G_OBJECT(page), "select"));
    if (!p || !p->app->cache.model) return;
    for (const auto& k : k86Layout())
        if (k.id == id)
            if (auto s = layoutSlot(*p->app->cache.model, k)) p->view->selected = {*s};
    redraw(p);
}

}  // namespace k86gui
