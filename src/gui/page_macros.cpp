// "Macros" page: list of the 50 macro slots and an event editor with recording.
#include <algorithm>
#include <set>

#include "app.hpp"
#include "layout.hpp"
#include "macro.hpp"

using namespace k86;

namespace k86gui {

namespace {

enum { COL_INDEX, COL_EVENT, COL_TARGET, COL_DELAY, N_COLS };

struct MacrosPage {
    App* app = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* repeat = nullptr;
    GtkWidget* size = nullptr;
    GtkWidget* state = nullptr;
    GtkWidget* save = nullptr;
    GtkWidget* record = nullptr;
    GtkWidget* insertMode = nullptr;
    GtkListStore* store = nullptr;
    GtkWidget* tree = nullptr;
    KeyboardView* picker = nullptr;
    int slot = 0;
    Macro work;
    bool dirty = false;
    // Recording.
    gulong pressId = 0, releaseId = 0;
    gint64 lastEvent = 0;
    std::set<uint8_t> down;
};

std::string eventName(const MacroEvent& e) {
    switch (e.type) {
        case MacroEvent::Type::KeyDown: return "Key down";
        case MacroEvent::Type::KeyUp: return "Key up";
        case MacroEvent::Type::MouseDown: return "Mouse down";
        case MacroEvent::Type::MouseUp: return "Mouse up";
        case MacroEvent::Type::MouseMove: return "Mouse move";
    }
    return {};
}

std::string eventTarget(const MacroEvent& e) {
    if (e.type == MacroEvent::Type::MouseMove) return std::to_string(e.dx) + ", " + std::to_string(e.dy);
    if (e.type == MacroEvent::Type::MouseDown || e.type == MacroEvent::Type::MouseUp) {
        static const char* b[] = {"Left", "Right", "Middle", "Back", "Forward"};
        return e.code >= 240 && e.code <= 244 ? b[e.code - 240] : std::to_string(e.code);
    }
    return keyLabel(e.code);
}

std::string summary(const std::optional<Macro>& m) {
    if (!m) return "not read";
    if (m->events.empty()) return "empty";
    std::string s;
    int keys = 0;
    for (const auto& e : m->events)
        if (e.type == MacroEvent::Type::KeyDown || e.type == MacroEvent::Type::MouseDown) {
            if (keys < 6) s += (keys ? " " : "") + eventTarget(e);
            ++keys;
        }
    if (keys > 6) s += " …";
    return s.empty() ? std::to_string(m->events.size()) + " events" : s;
}

void updateState(MacrosPage* p) {
    std::string size;
    try {
        auto bytes = encodeMacro(p->work);
        size_t used = 2;
        for (size_t i = 2; i + 3 < bytes.size(); ++i)
            if (!bytes[i] && !bytes[i + 1] && !bytes[i + 2] && !bytes[i + 3]) {
                used = i;
                break;
            }
        size = std::to_string(used) + " of 252 bytes";
    } catch (const std::exception&) {
        size = "too long for the keyboard (256 bytes)";
    }
    gtk_label_set_text(GTK_LABEL(p->size), size.c_str());
    gtk_label_set_text(GTK_LABEL(p->state), p->dirty ? "Unsaved changes" : "");
    gtk_widget_set_sensitive(p->save, p->dirty);
}

void fillStore(MacrosPage* p) {
    gtk_list_store_clear(p->store);
    for (size_t i = 0; i < p->work.events.size(); ++i) {
        const auto& e = p->work.events[i];
        GtkTreeIter it;
        gtk_list_store_append(p->store, &it);
        gtk_list_store_set(p->store, &it, COL_INDEX, int(i + 1), COL_EVENT, eventName(e).c_str(), COL_TARGET,
                           eventTarget(e).c_str(), COL_DELAY, int(e.delayMs), -1);
    }
    updateState(p);
}

void changed(MacrosPage* p) {
    p->dirty = true;
    fillStore(p);
}

int selectedRow(MacrosPage* p) {
    GtkTreeSelection* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(p->tree));
    GtkTreeModel* model;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &model, &it)) return -1;
    int idx = 0;
    gtk_tree_model_get(model, &it, COL_INDEX, &idx, -1);
    return idx - 1;
}

void selectRow(MacrosPage* p, int row) {
    if (row < 0 || row >= int(p->work.events.size())) return;
    GtkTreePath* path = gtk_tree_path_new_from_indices(row, -1);
    gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->tree)), path);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(p->tree), path, nullptr, FALSE, 0, 0);
    gtk_tree_path_free(path);
}

// Inserts after the selected row (or at the end).
void insertEvents(MacrosPage* p, const std::vector<MacroEvent>& evs) {
    int at = selectedRow(p);
    size_t pos = at < 0 ? p->work.events.size() : size_t(at) + 1;
    p->work.events.insert(p->work.events.begin() + long(pos), evs.begin(), evs.end());
    changed(p);
    selectRow(p, int(pos + evs.size()) - 1);
}

void loadSlot(MacrosPage* p, int slot) {
    p->slot = slot;
    const auto& macros = p->app->cache.macros;
    p->work = size_t(slot) < macros.size() && macros[size_t(slot)] ? *macros[size_t(slot)] : Macro{};
    if (p->work.repeat == 0) p->work.repeat = 1;
    p->dirty = false;
    gtk_label_set_text(GTK_LABEL(p->title), ("Macro " + std::to_string(slot + 1)).c_str());
    {
        Updating u(p->app);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->repeat), p->work.repeat);
    }
    fillStore(p);
}

void refreshList(MacrosPage* p) {
    for (int i = 0; i < kMaxMacros; ++i) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(p->list), i);
        if (!row) break;
        GtkWidget* l = GTK_WIDGET(g_object_get_data(G_OBJECT(row), "summary"));
        const auto& macros = p->app->cache.macros;
        std::string s = size_t(i) < macros.size() ? summary(macros[size_t(i)]) : "not read";
        gtk_label_set_text(GTK_LABEL(l), s.c_str());
    }
}

void stopRecording(MacrosPage* p) {
    if (p->pressId) g_signal_handler_disconnect(p->app->window, p->pressId);
    if (p->releaseId) g_signal_handler_disconnect(p->app->window, p->releaseId);
    p->pressId = p->releaseId = 0;
    p->down.clear();
    if (!p->work.events.empty()) p->work.events.back().delayMs = 10;
    fillStore(p);
}

gboolean recordKey(GtkWidget*, GdkEventKey* e, gpointer d) {
    auto* p = static_cast<MacrosPage*>(d);
    unsigned evdev = e->hardware_keycode >= 8 ? e->hardware_keycode - 8 : 0;
    uint8_t hid = evdevToHid(evdev);
    if (!hid) return TRUE;
    bool press = e->type == GDK_KEY_PRESS;
    if (press && p->down.count(hid)) return TRUE;  // auto-repeat
    if (!press && !p->down.count(hid)) return TRUE;
    press ? (void)p->down.insert(hid) : (void)p->down.erase(hid);
    gint64 now = g_get_monotonic_time();
    if (!p->work.events.empty() && p->lastEvent)
        p->work.events.back().delayMs = uint16_t(std::clamp<gint64>((now - p->lastEvent) / 1000, 1, 65535));
    p->lastEvent = now;
    MacroEvent ev;
    ev.type = press ? MacroEvent::Type::KeyDown : MacroEvent::Type::KeyUp;
    ev.code = hid;
    ev.delayMs = 10;
    p->work.events.push_back(ev);
    changed(p);
    selectRow(p, int(p->work.events.size()) - 1);
    return TRUE;  // keep the keys away from the rest of the window while recording
}

void startRecording(MacrosPage* p) {
    p->lastEvent = 0;
    p->down.clear();
    p->pressId = g_signal_connect(p->app->window, "key-press-event", G_CALLBACK(recordKey), p);
    p->releaseId = g_signal_connect(p->app->window, "key-release-event", G_CALLBACK(recordKey), p);
    p->app->status("Recording: press keys on your keyboard, then click Stop.");
}

}  // namespace

Page buildMacrosPage(App* app) {
    auto* p = new MacrosPage();
    p->app = app;
    GtkWidget* page = hbox(0);
    g_object_set_data_full(G_OBJECT(page), "page", p, [](gpointer d) { delete static_cast<MacrosPage*>(d); });

    // Slot list.
    p->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(p->list), GTK_SELECTION_BROWSE);
    for (int i = 0; i < kMaxMacros; ++i) {
        GtkWidget* row = vbox(1, 6);
        GtkWidget* name = label("Macro " + std::to_string(i + 1));
        gtk_style_context_add_class(gtk_widget_get_style_context(name), "k86-current");
        GtkWidget* sum = label("not read", true);
        gtk_label_set_ellipsize(GTK_LABEL(sum), PANGO_ELLIPSIZE_END);
        gtk_label_set_line_wrap(GTK_LABEL(sum), FALSE);
        gtk_box_pack_start(GTK_BOX(row), name, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), sum, FALSE, FALSE, 0);
        gtk_list_box_insert(GTK_LIST_BOX(p->list), row, -1);
        g_object_set_data(G_OBJECT(gtk_widget_get_parent(row)), "summary", sum);
    }
    g_signal_connect(p->list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer d) {
                         auto* p = static_cast<MacrosPage*>(d);
                         if (!row || p->app->updating) return;
                         int i = gtk_list_box_row_get_index(row);
                         if (i == p->slot) return;
                         if (p->dirty && !p->app->confirm("Discard the unsaved changes of Macro " +
                                                          std::to_string(p->slot + 1) + "?")) {
                             Updating u(p->app);
                             gtk_list_box_select_row(GTK_LIST_BOX(p->list),
                                                     gtk_list_box_get_row_at_index(GTK_LIST_BOX(p->list), p->slot));
                             return;
                         }
                         if (p->pressId) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->record), FALSE);
                         loadSlot(p, i);
                     }),
                     p);
    GtkWidget* listScroll = scrolledV(p->list);
    gtk_widget_set_size_request(listScroll, 220, -1);
    gtk_box_pack_start(GTK_BOX(page), listScroll, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 0);

    // Editor.
    GtkWidget* ed = vbox(8, 14);
    GtkWidget* head = hbox(10);
    p->title = heading("Macro 1");
    gtk_box_pack_start(GTK_BOX(head), p->title, FALSE, FALSE, 0);
    p->repeat = gtk_spin_button_new_with_range(1, 65535, 1);
    gtk_box_pack_end(GTK_BOX(head), p->repeat, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(head), label("Repeat count:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ed), head, FALSE, FALSE, 0);
    onChanged(p->repeat, "value-changed", [p] {
        if (p->app->updating) return;
        p->work.repeat = uint16_t(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(p->repeat)));
        p->dirty = true;
        updateState(p);
    });

    GtkWidget* bar = hbox(4);
    p->record = gtk_toggle_button_new_with_label("● Record");
    gtk_widget_set_tooltip_text(p->record, "Record key presses and releases with their timing");
    onChanged(p->record, "toggled", [p] {
        bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->record));
        gtk_button_set_label(GTK_BUTTON(p->record), on ? "■ Stop" : "● Record");
        on ? startRecording(p) : stopRecording(p);
    });
    gtk_box_pack_start(GTK_BOX(bar), p->record, FALSE, FALSE, 0);
    GtkWidget* mouse = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(mouse), "Add mouse click");
    GtkWidget* menu = gtk_menu_new();
    const char* buttons[] = {"Left", "Right", "Middle", "Back", "Forward"};
    for (int i = 0; i < 5; ++i) {
        GtkWidget* item = gtk_menu_item_new_with_label(buttons[i]);
        uint8_t code = uint8_t(240 + i);
        onChanged(item, "activate", [p, code] {
            MacroEvent d{MacroEvent::Type::MouseDown, code, 0, 0, 10}, u{MacroEvent::Type::MouseUp, code, 0, 0, 10};
            insertEvents(p, {d, u});
        });
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(mouse), menu);
    gtk_box_pack_start(GTK_BOX(bar), mouse, FALSE, FALSE, 0);
    auto rowOp = [p](int delta) {
        int r = selectedRow(p);
        int to = r + delta;
        if (r < 0 || to < 0 || to >= int(p->work.events.size())) return;
        std::swap(p->work.events[size_t(r)], p->work.events[size_t(to)]);
        changed(p);
        selectRow(p, to);
    };
    gtk_box_pack_start(GTK_BOX(bar), button("↑", [rowOp] { rowOp(-1); }), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), button("↓", [rowOp] { rowOp(1); }), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), button("Delete", [p] {
                           int r = selectedRow(p);
                           if (r < 0) return;
                           p->work.events.erase(p->work.events.begin() + r);
                           changed(p);
                           selectRow(p, std::min(r, int(p->work.events.size()) - 1));
                       }),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), button("Clear", [p] {
                           p->work.events.clear();
                           changed(p);
                       }),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ed), bar, FALSE, FALSE, 0);

    p->store = gtk_list_store_new(N_COLS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    p->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(p->store));
    g_object_unref(p->store);
    GtkCellRenderer* text = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p->tree), -1, "#", text, "text", COL_INDEX, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p->tree), -1, "Event", text, "text", COL_EVENT, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p->tree), -1, "Key / button", text, "text", COL_TARGET,
                                                nullptr);
    GtkCellRenderer* delay = gtk_cell_renderer_spin_new();
    g_object_set(delay, "editable", TRUE, "adjustment", gtk_adjustment_new(10, 0, 65535, 1, 10, 0), "digits", 0,
                 nullptr);
    g_signal_connect(delay, "edited", G_CALLBACK(+[](GtkCellRendererText*, gchar* path, gchar* value, gpointer d) {
                         auto* p = static_cast<MacrosPage*>(d);
                         int row = atoi(path);
                         if (row < 0 || row >= int(p->work.events.size())) return;
                         p->work.events[size_t(row)].delayMs = uint16_t(std::clamp(atoi(value), 0, 65535));
                         changed(p);
                         selectRow(p, row);
                     }),
                     p);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(p->tree), -1, "Then wait (ms, click to edit)", delay,
                                                "text", COL_DELAY, nullptr);
    GtkWidget* treeScroll = scrolledV(p->tree);
    gtk_widget_set_size_request(treeScroll, -1, 180);
    gtk_box_pack_start(GTK_BOX(ed), treeScroll, TRUE, TRUE, 0);

    GtkWidget* pickRow = hbox(8);
    gtk_box_pack_start(GTK_BOX(pickRow), label("Click a key below to add it as:"), FALSE, FALSE, 0);
    p->insertMode = gtk_combo_box_text_new();
    for (const char* m : {"Tap (down + up)", "Key down", "Key up"})
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->insertMode), m);
    gtk_combo_box_set_active(GTK_COMBO_BOX(p->insertMode), 0);
    gtk_box_pack_start(GTK_BOX(pickRow), p->insertMode, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ed), pickRow, FALSE, FALSE, 0);

    p->picker = new KeyboardView();
    p->picker->selectable = false;
    gtk_widget_set_size_request(p->picker->widget(), 480, 170);
    p->picker->describe = [](const KeyGeometry& k, int) {
        bool usable = k.factory[0] == 0;
        return KeyLook{k.knob ? "" : k.legend, {}, usable ? KeyLook::Style::Normal : KeyLook::Style::Dim, false, 0};
    };
    p->picker->onClick = [p](int slot, bool) {
        const KeyGeometry* k = p->picker->keyForSlot(slot);
        if (!k || k->factory[0] != 0) return;  // only keyboard keys can be part of a macro
        uint8_t u = k->factory[2];
        int mode = gtk_combo_box_get_active(GTK_COMBO_BOX(p->insertMode));
        MacroEvent d{MacroEvent::Type::KeyDown, u, 0, 0, 10}, up{MacroEvent::Type::KeyUp, u, 0, 0, 10};
        if (mode == 1) insertEvents(p, {d});
        else if (mode == 2) insertEvents(p, {up});
        else insertEvents(p, {d, up});
    };
    gtk_box_pack_start(GTK_BOX(ed), p->picker->widget(), FALSE, FALSE, 0);

    GtkWidget* actions = hbox(8);
    p->size = label("", true);
    p->state = label("");
    gtk_box_pack_start(GTK_BOX(actions), p->size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), p->state, FALSE, FALSE, 8);
    p->save = button("Save to keyboard", [p] {
        App* app = p->app;
        if (!app->canWrite()) return;
        Macro m = p->work;
        try {
            encodeMacro(m);
        } catch (const std::exception& e) {
            app->error(e.what());
            return;
        }
        uint8_t slot = uint8_t(p->slot);
        app->worker.post(
            "Writing macro", [m, slot](Keyboard& kb, const Worker::ProgressFn&) { kb.setMacro(slot, m); },
            [p, m, slot] {
                if (slot < p->app->cache.macros.size()) p->app->cache.macros[slot] = m;
                if (p->slot == slot) {
                    p->dirty = false;
                    updateState(p);
                }
                refreshList(p);
                p->app->status("Macro " + std::to_string(slot + 1) + " saved.");
                p->app->refreshPages();
            });
    });
    gtk_style_context_add_class(gtk_widget_get_style_context(p->save), "suggested-action");
    gtk_box_pack_end(GTK_BOX(actions), p->save, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(actions), button("Revert", [p] { loadSlot(p, p->slot); }), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ed), actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ed), label("Assign a macro to a key on the Keys or Fn layer page (Macro tab).", true),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(page), scrolledV(ed), TRUE, TRUE, 0);

    Page result{"macros", "Macros", page, {}};
    result.refresh = [p] {
        const DeviceModel* m = p->app->cache.model;
        if (p->picker->model() != m) p->picker->setModel(m);
        refreshList(p);
        if (!p->dirty) loadSlot(p, p->slot);
        Updating u(p->app);
        gtk_list_box_select_row(GTK_LIST_BOX(p->list), gtk_list_box_get_row_at_index(GTK_LIST_BOX(p->list), p->slot));
    };
    return result;
}

}  // namespace k86gui
