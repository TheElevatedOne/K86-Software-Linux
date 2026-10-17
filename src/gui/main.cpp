// k86-gui - GTK 3 configuration window for the Attack Shark K86.
#include <gtk/gtk.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "discovery.hpp"
#include "keyboard.hpp"
#include "screen.hpp"
#include "sysinfo.hpp"

using namespace k86;

namespace {

// Fake device for --dry-run: answers with the echoed command byte and an acknowledge (1).
class EchoTransport : public Transport {
public:
    void sendFeature(const Report& r) override { last_ = r[0]; }
    Report getFeature() override {
        Report r{};
        r[0] = last_;
        r[1] = 1;
        return r;
    }

private:
    uint8_t last_ = 0;
};

struct App;

struct LightControls {
    App* app = nullptr;
    bool side = false;
    GtkWidget* frame = nullptr;
    GtkWidget* mode = nullptr;
    GtkWidget* brightness = nullptr;
    GtkWidget* speed = nullptr;
    GtkWidget* option = nullptr;
    GtkWidget* color = nullptr;
    GtkWidget* rainbow = nullptr;
};

enum { KEY_SLOT, KEY_NAME, KEY_ACTION, KEY_COLS };
enum { PK_SLOT, PK_NAME, PK_HEX, PK_COLS };

struct App {
    std::string dryRun;
    std::unique_ptr<Transport> transport;
    std::unique_ptr<Keyboard> kb;
    SystemInfoCollector sysinfo;
    guint sysTimer = 0;
    std::vector<std::string> devicePaths;

    GtkWidget* window = nullptr;
    GtkWidget* deviceCombo = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* notebook = nullptr;

    LightControls light, side;

    GtkWidget* keyLayer = nullptr;
    GtkWidget* keyFn = nullptr;
    GtkWidget* keyOs = nullptr;
    GtkListStore* keyStore = nullptr;
    KeyMatrix loadedMatrix{};

    GtkWidget* perKeyLayer = nullptr;
    GtkListStore* perKeyStore = nullptr;
    std::array<Rgb, kSlots> perKeyColors{};

    GtkWidget* macroSlot = nullptr;
    GtkWidget* macroRepeat = nullptr;
    GtkWidget* macroText = nullptr;

    GtkWidget* imageChooser = nullptr;
    GtkWidget* imagePreview = nullptr;
    GtkWidget* screenSlot = nullptr;
    GtkWidget* gifDelay = nullptr;
    GtkWidget* progress = nullptr;
    GtkWidget* sysSync = nullptr;

    GtkWidget* profile = nullptr;
    GtkWidget* debounce = nullptr;
    GtkWidget* sleepBt = nullptr;
    GtkWidget* sleep24 = nullptr;
    GtkWidget* deepBt = nullptr;
    GtkWidget* deep24 = nullptr;
    GtkWidget* optionsGrid = nullptr;
    std::map<std::string, std::pair<GtkWidget*, int>> optionEdits;  // name -> (spin, loaded value)
};

// ----------------------------------------------------------------------------- helpers
void pumpEvents() {
    while (gtk_events_pending()) gtk_main_iteration();
}

void message(App* app, GtkMessageType type, const std::string& text) {
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, type, GTK_BUTTONS_CLOSE, "%s",
                                          text.c_str());
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

bool ask(App* app, const std::string& text) {
    GtkWidget* d = gtk_message_dialog_new(GTK_WINDOW(app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_YES_NO, "%s", text.c_str());
    bool yes = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_YES;
    gtk_widget_destroy(d);
    return yes;
}

// Runs a keyboard operation with the UI locked; errors become a dialog.
template <typename F>
void guarded(App* app, F&& f) {
    if (!app->kb) {
        message(app, GTK_MESSAGE_INFO, "Connect a keyboard first.");
        return;
    }
    gtk_widget_set_sensitive(app->notebook, FALSE);
    pumpEvents();
    std::string error;
    try {
        f();
    } catch (const std::exception& e) {
        error = e.what();
    }
    gtk_widget_set_sensitive(app->notebook, TRUE);
    if (!error.empty()) message(app, GTK_MESSAGE_ERROR, error);
}

void setStatus(App* app, const std::string& s) { gtk_label_set_text(GTK_LABEL(app->status), s.c_str()); }

std::string hexColor(Rgb c) {
    char b[8];
    snprintf(b, sizeof b, "#%06x", c & 0xFFFFFF);
    return b;
}

GdkRGBA toRgba(Rgb c) { return GdkRGBA{(c >> 16 & 0xFF) / 255.0, (c >> 8 & 0xFF) / 255.0, (c & 0xFF) / 255.0, 1.0}; }

Rgb fromRgba(const GdkRGBA& c) {
    auto ch = [](double v) { return Rgb(std::clamp(int(v * 255.0 + 0.5), 0, 255)); };
    return ch(c.red) << 16 | ch(c.green) << 8 | ch(c.blue);
}

bool pickColor(App* app, const char* title, Rgb& c) {
    GtkWidget* d = gtk_color_chooser_dialog_new(title, GTK_WINDOW(app->window));
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(d), FALSE);
    GdkRGBA rgba = toRgba(c);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(d), &rgba);
    bool ok = gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK;
    if (ok) {
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(d), &rgba);
        c = fromRgba(rgba);
    }
    gtk_widget_destroy(d);
    return ok;
}

int spinValue(GtkWidget* w) { return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w)); }
void setSpin(GtkWidget* w, int v) { gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), v); }
bool checked(GtkWidget* w) { return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)); }

GtkWidget* spin(int lo, int hi) {
    GtkWidget* s = gtk_spin_button_new_with_range(lo, hi, 1);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(s), 0);
    return s;
}

GtkWidget* hscale(int lo, int hi) {
    GtkWidget* s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, lo, hi, 1);
    gtk_scale_set_digits(GTK_SCALE(s), 0);
    gtk_widget_set_hexpand(s, TRUE);
    return s;
}

GtkWidget* labeled(const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

GtkWidget* vbox() {
    GtkWidget* b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(b), 10);
    return b;
}

GtkWidget* hbox() { return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); }

GtkWidget* button(GtkWidget* box, const char* label, GCallback cb, gpointer data, bool end = false) {
    GtkWidget* b = gtk_button_new_with_label(label);
    g_signal_connect(b, "clicked", cb, data);
    end ? gtk_box_pack_end(GTK_BOX(box), b, FALSE, FALSE, 0) : gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    return b;
}

GtkWidget* scrolled(GtkWidget* child) {
    GtkWidget* s = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(s), child);
    gtk_widget_set_vexpand(s, TRUE);
    return s;
}

// ----------------------------------------------------------------------------- lighting
const std::vector<LightModeInfo>& modesOf(LightControls* c) {
    return c->side ? c->app->kb->model().sideLightModes : c->app->kb->model().lightModes;
}

void updateLightControls(LightControls* c) {
    if (!c->app->kb) return;
    const auto& modes = modesOf(c);
    int i = gtk_combo_box_get_active(GTK_COMBO_BOX(c->mode));
    if (i < 0 || i >= int(modes.size())) return;
    const auto& m = modes[size_t(i)];
    gtk_widget_set_sensitive(c->speed, m.speed);
    gtk_widget_set_sensitive(c->option, m.options > 0);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(c->option), 0, std::max(0, m.options - 1));
    gtk_widget_set_sensitive(c->color, m.color);
    gtk_widget_set_sensitive(c->rainbow, m.color);
}

void readLight(LightControls* c) {
    App* app = c->app;
    guarded(app, [&] {
        auto l = c->side ? app->kb->getSideLight() : app->kb->getLight();
        if (!l) return;
        const auto& modes = modesOf(c);
        for (size_t i = 0; i < modes.size(); ++i)
            if (modes[i].code == l->mode) gtk_combo_box_set_active(GTK_COMBO_BOX(c->mode), int(i));
        gtk_range_set_value(GTK_RANGE(c->brightness), l->brightness);
        gtk_range_set_value(GTK_RANGE(c->speed), l->speed);
        setSpin(c->option, l->option);
        GdkRGBA rgba = toRgba(l->color);
        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(c->color), &rgba);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(c->rainbow), l->rainbow);
    });
}

void writeLight(LightControls* c) {
    App* app = c->app;
    guarded(app, [&] {
        int i = gtk_combo_box_get_active(GTK_COMBO_BOX(c->mode));
        if (i < 0) return;
        LightSetting l;
        l.mode = modesOf(c)[size_t(i)].code;
        l.brightness = uint8_t(gtk_range_get_value(GTK_RANGE(c->brightness)));
        l.speed = uint8_t(gtk_range_get_value(GTK_RANGE(c->speed)));
        l.option = uint8_t(spinValue(c->option));
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(c->color), &rgba);
        l.color = fromRgba(rgba);
        l.rainbow = checked(c->rainbow);
        c->side ? app->kb->setSideLight(l) : app->kb->setLight(l);
    });
}

GtkWidget* buildLightGroup(App* app, LightControls& c, const char* title, bool side) {
    c.app = app;
    c.side = side;
    c.frame = gtk_frame_new(title);
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    c.mode = gtk_combo_box_text_new();
    c.brightness = hscale(0, 4);
    c.speed = hscale(0, 4);
    c.option = spin(0, 0);
    c.color = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(c.color), FALSE);
    GdkRGBA red = toRgba(0xFF0000);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(c.color), &red);
    c.rainbow = gtk_check_button_new_with_label("Rainbow (multi-colour)");
    const char* labels[] = {"Effect", "Brightness", "Speed", "Option / direction", "Colour", ""};
    GtkWidget* widgets[] = {c.mode, c.brightness, c.speed, c.option, c.color, c.rainbow};
    for (int r = 0; r < 6; ++r) {
        gtk_grid_attach(GTK_GRID(grid), labeled(labels[r]), 0, r, 1, 1);
        gtk_widget_set_hexpand(widgets[r], TRUE);
        gtk_grid_attach(GTK_GRID(grid), widgets[r], 1, r, 1, 1);
    }
    GtkWidget* row = hbox();
    button(row, "Apply", G_CALLBACK(+[](GtkButton*, gpointer d) { writeLight(static_cast<LightControls*>(d)); }), &c, true);
    button(row, "Read from keyboard",
           G_CALLBACK(+[](GtkButton*, gpointer d) { readLight(static_cast<LightControls*>(d)); }), &c, true);
    gtk_grid_attach(GTK_GRID(grid), row, 0, 6, 2, 1);
    g_signal_connect(c.mode, "changed",
                     G_CALLBACK(+[](GtkComboBox*, gpointer d) { updateLightControls(static_cast<LightControls*>(d)); }),
                     &c);
    gtk_container_add(GTK_CONTAINER(c.frame), grid);
    return c.frame;
}

GtkWidget* buildLightTab(App* app) {
    GtkWidget* b = vbox();
    gtk_box_pack_start(GTK_BOX(b), buildLightGroup(app, app->light, "Key backlight", false), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), buildLightGroup(app, app->side, "Side light", true), FALSE, FALSE, 0);
    return b;
}

// ----------------------------------------------------------------------------- key mapping
void readKeys(App* app) {
    guarded(app, [&] {
        uint8_t layer = uint8_t(spinValue(app->keyLayer));
        uint8_t os = uint8_t(std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(app->keyOs))));
        app->loadedMatrix = checked(app->keyFn) ? app->kb->getFnMatrix(layer, os) : app->kb->getKeyMatrix(layer);
        gtk_list_store_clear(app->keyStore);
        const KeyMatrix& m = app->loadedMatrix;
        for (int i = 0; i < kSlots; ++i) {
            std::string name = slotName(app->kb->defaultMatrix(), i);
            KeyAction a{m[i * 4], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]};
            if (name.empty() && a == KeyAction{0, 0, 0, 0}) continue;
            GtkTreeIter it;
            gtk_list_store_append(app->keyStore, &it);
            gtk_list_store_set(app->keyStore, &it, KEY_SLOT, i, KEY_NAME, name.empty() ? "-" : name.c_str(), KEY_ACTION,
                               actionToString(a).c_str(), -1);
        }
    });
}

void writeKeys(App* app) {
    guarded(app, [&] {
        uint8_t layer = uint8_t(spinValue(app->keyLayer));
        uint8_t os = uint8_t(std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(app->keyOs))));
        bool fn = checked(app->keyFn);
        int changed = 0;
        GtkTreeIter it;
        GtkTreeModel* model = GTK_TREE_MODEL(app->keyStore);
        for (gboolean ok = gtk_tree_model_get_iter_first(model, &it); ok; ok = gtk_tree_model_iter_next(model, &it)) {
            int slot = 0;
            gchar* text = nullptr;
            gtk_tree_model_get(model, &it, KEY_SLOT, &slot, KEY_ACTION, &text, -1);
            std::string s = text ? text : "";
            g_free(text);
            std::string err;
            auto a = parseAction(s, &err);
            if (!a) throw std::runtime_error("slot " + std::to_string(slot) + ": " + err);
            auto& m = app->loadedMatrix;
            KeyAction old{m[slot * 4], m[slot * 4 + 1], m[slot * 4 + 2], m[slot * 4 + 3]};
            if (*a == old) continue;
            fn ? app->kb->setFnKey(layer, os, uint8_t(slot), *a) : app->kb->setKey(layer, uint8_t(slot), *a);
            std::copy(a->begin(), a->end(), m.begin() + slot * 4);
            ++changed;
        }
        setStatus(app, std::to_string(changed) + " key(s) written");
    });
}

void restoreKeys(App* app) {
    if (!app->kb || !ask(app, "Write the factory layout to this layer?")) return;
    guarded(app, [&] {
        uint8_t layer = uint8_t(spinValue(app->keyLayer));
        uint8_t os = uint8_t(std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(app->keyOs))));
        checked(app->keyFn) ? app->kb->setFnMatrix(layer, os, app->kb->defaultFnMatrix())
                            : app->kb->setKeyMatrix(layer, app->kb->defaultMatrix());
    });
    readKeys(app);
}

GtkWidget* buildKeysTab(App* app) {
    GtkWidget* b = vbox();
    GtkWidget* row = hbox();
    app->keyLayer = spin(0, 2);
    app->keyFn = gtk_check_button_new_with_label("Fn layer");
    app->keyOs = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyOs), "Windows");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->keyOs), "macOS");
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->keyOs), 0);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Profile:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->keyLayer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->keyFn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Fn OS:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->keyOs, FALSE, FALSE, 0);
    button(row, "Restore defaults", G_CALLBACK(+[](GtkButton*, gpointer d) { restoreKeys(static_cast<App*>(d)); }), app, true);
    button(row, "Write changes", G_CALLBACK(+[](GtkButton*, gpointer d) { writeKeys(static_cast<App*>(d)); }), app, true);
    button(row, "Read", G_CALLBACK(+[](GtkButton*, gpointer d) { readKeys(static_cast<App*>(d)); }), app, true);
    gtk_box_pack_start(GTK_BOX(b), row, FALSE, FALSE, 0);

    app->keyStore = gtk_list_store_new(KEY_COLS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->keyStore));
    GtkCellRenderer* text = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Slot", text, "text", KEY_SLOT, nullptr);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Key", text, "text", KEY_NAME, nullptr);
    GtkCellRenderer* edit = gtk_cell_renderer_text_new();
    g_object_set(edit, "editable", TRUE, nullptr);
    g_signal_connect(edit, "edited",
                     G_CALLBACK(+[](GtkCellRendererText*, gchar* path, gchar* value, gpointer d) {
                         App* app = static_cast<App*>(d);
                         GtkTreeIter it;
                         if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(app->keyStore), &it, path))
                             gtk_list_store_set(app->keyStore, &it, KEY_ACTION, value, -1);
                     }),
                     app);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Action (click to edit)", edit, "text",
                                                KEY_ACTION, nullptr);
    gtk_box_pack_start(GTK_BOX(b), scrolled(view), TRUE, TRUE, 0);
    GtkWidget* help = labeled(
        "Actions: A, F5, LCtrl+C, consumer:VolumeUp, system:Sleep, mouse:Left, macro:N[:repeat|toggle|hold], Fn, "
        "profile:Next, Disabled, raw:aa,bb,cc,dd");
    gtk_label_set_line_wrap(GTK_LABEL(help), TRUE);
    gtk_box_pack_start(GTK_BOX(b), help, FALSE, FALSE, 0);
    return b;
}

// ----------------------------------------------------------------------------- per-key colours
void redrawPerKey(App* app) {
    gtk_list_store_clear(app->perKeyStore);
    if (!app->kb) return;
    for (int i = 0; i < kSlots; ++i) {
        std::string name = slotName(app->kb->defaultMatrix(), i);
        if (name.empty()) continue;
        GtkTreeIter it;
        gtk_list_store_append(app->perKeyStore, &it);
        gtk_list_store_set(app->perKeyStore, &it, PK_SLOT, i, PK_NAME, name.c_str(), PK_HEX,
                           hexColor(app->perKeyColors[size_t(i)]).c_str(), -1);
    }
}

GtkWidget* buildPerKeyTab(App* app) {
    GtkWidget* b = vbox();
    GtkWidget* row = hbox();
    app->perKeyLayer = spin(0, 4);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Pattern:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->perKeyLayer, FALSE, FALSE, 0);
    button(row, "Write", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               guarded(app, [&] { app->kb->setUserPicture(uint8_t(spinValue(app->perKeyLayer)), app->perKeyColors); });
           }),
           app, true);
    button(row, "Fill all...", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               Rgb c = 0xFFFFFF;
               if (!app->kb || !pickColor(app, "Fill colour", c)) return;
               for (int i = 0; i < kSlots; ++i)
                   app->perKeyColors[size_t(i)] = slotName(app->kb->defaultMatrix(), i).empty() ? 0 : c;
               redrawPerKey(app);
           }),
           app, true);
    button(row, "Read", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               guarded(app, [&] { app->perKeyColors = app->kb->getUserPicture(uint8_t(spinValue(app->perKeyLayer))); });
               redrawPerKey(app);
           }),
           app, true);
    gtk_box_pack_start(GTK_BOX(b), row, FALSE, FALSE, 0);

    app->perKeyStore = gtk_list_store_new(PK_COLS, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->perKeyStore));
    GtkCellRenderer* text = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Key", text, "text", PK_NAME, nullptr);
    GtkCellRenderer* swatch = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Colour (double-click to change)", swatch,
                                                "text", PK_HEX, "cell-background", PK_HEX, nullptr);
    g_signal_connect(view, "row-activated",
                     G_CALLBACK(+[](GtkTreeView*, GtkTreePath* path, GtkTreeViewColumn*, gpointer d) {
                         App* app = static_cast<App*>(d);
                         GtkTreeIter it;
                         if (!gtk_tree_model_get_iter(GTK_TREE_MODEL(app->perKeyStore), &it, path)) return;
                         int slot = 0;
                         gtk_tree_model_get(GTK_TREE_MODEL(app->perKeyStore), &it, PK_SLOT, &slot, -1);
                         Rgb c = app->perKeyColors[size_t(slot)];
                         if (!pickColor(app, "Key colour", c)) return;
                         app->perKeyColors[size_t(slot)] = c;
                         gtk_list_store_set(app->perKeyStore, &it, PK_HEX, hexColor(c).c_str(), -1);
                     }),
                     app);
    gtk_box_pack_start(GTK_BOX(b), scrolled(view), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(b), labeled("Shown by the \"custom\" lighting effect; its option selects the pattern."),
                       FALSE, FALSE, 0);
    return b;
}

// ----------------------------------------------------------------------------- macros
std::string textViewText(GtkWidget* tv) {
    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(buf, &a, &b);
    gchar* t = gtk_text_buffer_get_text(buf, &a, &b, FALSE);
    std::string s = t ? t : "";
    g_free(t);
    return s;
}

GtkWidget* buildMacroTab(App* app) {
    GtkWidget* b = vbox();
    GtkWidget* row = hbox();
    app->macroSlot = spin(0, kMaxMacros - 1);
    app->macroRepeat = spin(0, 65535);
    setSpin(app->macroRepeat, 1);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Macro slot:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->macroSlot, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Repeat:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->macroRepeat, FALSE, FALSE, 0);
    button(row, "Write", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               guarded(app, [&] {
                   Macro m = parseMacro(textViewText(app->macroText), uint16_t(spinValue(app->macroRepeat)));
                   app->kb->setMacro(uint8_t(spinValue(app->macroSlot)), m);
               });
           }),
           app, true);
    button(row, "Read", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               guarded(app, [&] {
                   Macro m = app->kb->getMacro(uint8_t(spinValue(app->macroSlot)));
                   setSpin(app->macroRepeat, m.repeat);
                   std::string s = macroToString(m);
                   gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->macroText)), s.c_str(), -1);
               });
           }),
           app, true);
    gtk_box_pack_start(GTK_BOX(b), row, FALSE, FALSE, 0);
    app->macroText = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->macroText), GTK_WRAP_WORD);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->macroText), TRUE);
    gtk_box_pack_start(GTK_BOX(b), scrolled(app->macroText), TRUE, TRUE, 0);
    GtkWidget* help = labeled(
        "Events: +KEY (down), -KEY (up), KEY (tap), +mouse:Left / -mouse:Left, move:DX,DY. Append /MS for the delay "
        "after an event (default 10 ms), e.g. \"+LCtrl/5 C/20 -LCtrl\". Assign the macro to a key with the action "
        "macro:N on the Key mapping tab.");
    gtk_label_set_line_wrap(GTK_LABEL(help), TRUE);
    gtk_box_pack_start(GTK_BOX(b), help, FALSE, FALSE, 0);
    return b;
}

// ----------------------------------------------------------------------------- screen
void uploadImage(App* app) {
    gchar* file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(app->imageChooser));
    if (!file) {
        message(app, GTK_MESSAGE_INFO, "Choose an image first.");
        return;
    }
    std::string path = file;
    g_free(file);
    guarded(app, [&] {
        LoadedImage img = loadImage(path);
        const auto& m = app->kb->model();
        const int W = m.screenWidth, H = m.screenHeight;
        auto prog = [app](double p) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), std::clamp(p, 0.0, 1.0));
            pumpEvents();
        };
        if (img.frames.size() == 1) {
            uint8_t slot = uint8_t(spinValue(app->screenSlot) - 1);
            auto px = fitToScreen(img.frames[0], W, H);
            ScreenImage si = toScreenImage(px, W, contentRect(px, W, H));
            if (!app->kb->screenBegin(slot, 1, 0, si)) throw std::runtime_error("keyboard rejected the image upload");
            app->kb->screenSendFrame(slot, 1, 0, si, prog);
            return;
        }
        if (img.frames.size() > 255) throw std::runtime_error("GIF has more than 255 frames");
        int delay = spinValue(app->gifDelay);
        if (!delay) delay = img.delaysMs[0] > 0 ? std::min(img.delaysMs[0], 255) : 50;
        std::vector<std::vector<uint32_t>> px;
        std::vector<Rect> rects;
        for (const auto& f : img.frames) {
            px.push_back(fitToScreen(f, W, H));
            rects.push_back(contentRect(px.back(), W, H));
        }
        Rect u = unionRect(rects);
        uint8_t n = uint8_t(img.frames.size());
        std::vector<ScreenImage> frames;
        for (auto& p : px) frames.push_back(toScreenImage(p, W, u));
        if (!app->kb->screenBegin(0, n, uint8_t(delay), frames[0])) throw std::runtime_error("keyboard rejected the upload");
        for (size_t i = 0; i < frames.size(); ++i)
            app->kb->screenSendFrame(uint8_t(i), n, uint8_t(delay), frames[i],
                                     [&](double p) { prog((double(i) + p) / double(frames.size())); });
    });
}

gboolean sendSysInfo(gpointer d) {
    App* app = static_cast<App*>(d);
    if (!app->kb) return G_SOURCE_CONTINUE;
    try {
        app->kb->sendSystemInfo(app->sysinfo.sample());
    } catch (const std::exception& e) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->sysSync), FALSE);
        message(app, GTK_MESSAGE_ERROR, e.what());
    }
    return G_SOURCE_CONTINUE;
}

GtkWidget* buildScreenTab(App* app) {
    GtkWidget* b = vbox();
    GtkWidget* row = hbox();
    app->imageChooser = gtk_file_chooser_button_new("Image", GTK_FILE_CHOOSER_ACTION_OPEN);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images (PNG, JPEG, BMP, GIF, TGA)");
    for (const char* p : {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.gif", "*.tga", "*.PNG", "*.JPG", "*.GIF"})
        gtk_file_filter_add_pattern(filter, p);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(app->imageChooser), filter);
    gtk_box_pack_start(GTK_BOX(row), gtk_label_new("Image / GIF:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->imageChooser, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(b), row, FALSE, FALSE, 0);

    app->imagePreview = gtk_image_new();
    gtk_widget_set_size_request(app->imagePreview, 240, 135);
    gtk_box_pack_start(GTK_BOX(b), app->imagePreview, FALSE, FALSE, 0);
    g_signal_connect(app->imageChooser, "file-set", G_CALLBACK(+[](GtkFileChooserButton* fc, gpointer d) {
                         App* app = static_cast<App*>(d);
                         gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
                         if (!f) return;
                         GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(f, 240, 135, TRUE, nullptr);
                         g_free(f);
                         gtk_image_set_from_pixbuf(GTK_IMAGE(app->imagePreview), pb);
                         if (pb) g_object_unref(pb);
                     }),
                     app);

    GtkWidget* row2 = hbox();
    app->screenSlot = spin(1, 5);
    app->gifDelay = spin(0, 255);
    gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("Static image slot:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row2), app->screenSlot, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row2), gtk_label_new("GIF frame delay (ms, 0 = from file):"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row2), app->gifDelay, FALSE, FALSE, 0);
    button(row2, "Upload", G_CALLBACK(+[](GtkButton*, gpointer d) { uploadImage(static_cast<App*>(d)); }), app, true);
    gtk_box_pack_start(GTK_BOX(b), row2, FALSE, FALSE, 0);

    app->progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(b), app->progress, FALSE, FALSE, 0);

    GtkWidget* row3 = hbox();
    button(row3, "Sync clock", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               guarded(app, [&] { app->kb->syncClock(); });
           }),
           app);
    button(row3, "Language switch", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               guarded(app, [&] { app->kb->switchScreenLanguage(true); });
           }),
           app);
    button(row3, "Erase images", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               if (!app->kb || !ask(app, "Erase all images stored on the keyboard screen?")) return;
               guarded(app, [&] {
                   if (!app->kb->eraseScreenFlash()) throw std::runtime_error("keyboard did not acknowledge the erase");
                   message(app, GTK_MESSAGE_INFO, "Erase started; it takes about 55 seconds.");
               });
           }),
           app);
    app->sysSync = gtk_check_button_new_with_label("Send system info every second");
    g_signal_connect(app->sysSync, "toggled", G_CALLBACK(+[](GtkToggleButton* t, gpointer d) {
                         App* app = static_cast<App*>(d);
                         if (gtk_toggle_button_get_active(t)) {
                             app->sysinfo.sample();
                             if (!app->sysTimer) app->sysTimer = g_timeout_add(1000, sendSysInfo, app);
                         } else if (app->sysTimer) {
                             g_source_remove(app->sysTimer);
                             app->sysTimer = 0;
                         }
                     }),
                     app);
    gtk_box_pack_start(GTK_BOX(row3), app->sysSync, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), row3, FALSE, FALSE, 0);
    return b;
}

// ----------------------------------------------------------------------------- settings
void readSettings(App* app) {
    guarded(app, [&] {
        if (auto p = app->kb->getProfile()) setSpin(app->profile, *p);
        if (auto d = app->kb->getDebounce()) setSpin(app->debounce, *d);
        if (auto s = app->kb->getSleep()) {
            setSpin(app->sleepBt, s->bluetooth);
            setSpin(app->sleep24, s->wireless24);
            setSpin(app->deepBt, s->deepBluetooth);
            setSpin(app->deep24, s->deepWireless24);
        }
        auto opts = app->kb->getOptions();
        GList* children = gtk_container_get_children(GTK_CONTAINER(app->optionsGrid));
        for (GList* c = children; c; c = c->next) gtk_widget_destroy(GTK_WIDGET(c->data));
        g_list_free(children);
        app->optionEdits.clear();
        int r = 0;
        for (const auto& [k, v] : opts) {
            GtkWidget* s = spin(0, 255);
            setSpin(s, v);
            gtk_grid_attach(GTK_GRID(app->optionsGrid), labeled(k.c_str()), 0, r, 1, 1);
            gtk_grid_attach(GTK_GRID(app->optionsGrid), s, 1, r, 1, 1);
            app->optionEdits[k] = {s, v};
            ++r;
        }
        gtk_widget_show_all(app->optionsGrid);
    });
}

void writeSettings(App* app) {
    guarded(app, [&] {
        Keyboard& kb = *app->kb;
        const auto& m = kb.model();
        if (auto p = kb.getProfile(); !p || *p != spinValue(app->profile)) kb.setProfile(uint8_t(spinValue(app->profile)));
        if (auto d = kb.getDebounce(); !d || *d != spinValue(app->debounce)) kb.setDebounce(uint8_t(spinValue(app->debounce)));
        SleepTimes t{uint16_t(spinValue(app->sleepBt)), uint16_t(spinValue(app->sleep24)), uint16_t(spinValue(app->deepBt)),
                     uint16_t(spinValue(app->deep24))};
        auto in = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
        if (!in(t.bluetooth, m.sleepMin, m.sleepMax) || !in(t.wireless24, m.sleepMin, m.sleepMax) ||
            !in(t.deepBluetooth, m.deepSleepMin, m.deepSleepMax) || !in(t.deepWireless24, m.deepSleepMin, m.deepSleepMax))
            throw std::runtime_error("sleep must be " + std::to_string(m.sleepMin) + "-" + std::to_string(m.sleepMax) +
                                     " s, deep sleep " + std::to_string(m.deepSleepMin) + "-" +
                                     std::to_string(m.deepSleepMax) + " s");
        auto cur = kb.getSleep();
        if (!cur || cur->bluetooth != t.bluetooth || cur->wireless24 != t.wireless24 ||
            cur->deepBluetooth != t.deepBluetooth || cur->deepWireless24 != t.deepWireless24)
            kb.setSleep(t);
        std::map<std::string, int> changes;
        for (const auto& [k, e] : app->optionEdits)
            if (spinValue(e.first) != e.second) changes[k] = spinValue(e.first);
        if (!changes.empty()) kb.setOptions(changes);
    });
    readSettings(app);
}

GtkWidget* buildSettingsTab(App* app) {
    GtkWidget* b = vbox();
    GtkWidget* frame = gtk_frame_new("General");
    GtkWidget* grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    app->profile = spin(0, 2);
    app->debounce = spin(0, 255);
    app->sleepBt = spin(0, 65535);
    app->sleep24 = spin(0, 65535);
    app->deepBt = spin(0, 65535);
    app->deep24 = spin(0, 65535);
    const char* labels[] = {"Active profile", "Debounce (ms)", "Sleep after, Bluetooth (s)", "Sleep after, 2.4 GHz (s)",
                            "Deep sleep after, Bluetooth (s)", "Deep sleep after, 2.4 GHz (s)"};
    GtkWidget* widgets[] = {app->profile, app->debounce, app->sleepBt, app->sleep24, app->deepBt, app->deep24};
    for (int r = 0; r < 6; ++r) {
        gtk_grid_attach(GTK_GRID(grid), labeled(labels[r]), 0, r, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), widgets[r], 1, r, 1, 1);
    }
    gtk_container_add(GTK_CONTAINER(frame), grid);
    gtk_box_pack_start(GTK_BOX(b), frame, FALSE, FALSE, 0);

    GtkWidget* oframe = gtk_frame_new("Option flags");
    app->optionsGrid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(app->optionsGrid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(app->optionsGrid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(app->optionsGrid), 12);
    gtk_container_add(GTK_CONTAINER(oframe), app->optionsGrid);
    gtk_box_pack_start(GTK_BOX(b), oframe, FALSE, FALSE, 0);

    GtkWidget* row = hbox();
    button(row, "Read", G_CALLBACK(+[](GtkButton*, gpointer d) { readSettings(static_cast<App*>(d)); }), app);
    button(row, "Write", G_CALLBACK(+[](GtkButton*, gpointer d) { writeSettings(static_cast<App*>(d)); }), app);
    button(row, "Factory reset...", G_CALLBACK(+[](GtkButton*, gpointer d) {
               App* app = static_cast<App*>(d);
               if (!app->kb || !ask(app, "Reset the keyboard to factory settings?")) return;
               guarded(app, [&] { app->kb->factoryReset(); });
               readSettings(app);
           }),
           app, true);
    gtk_box_pack_start(GTK_BOX(b), row, FALSE, FALSE, 0);
    return b;
}

// ----------------------------------------------------------------------------- device handling
void populateForModel(App* app) {
    const auto& m = app->kb->model();
    for (LightControls* c : {&app->light, &app->side}) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(c->mode));
        for (const auto& lm : c->side ? m.sideLightModes : m.lightModes)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c->mode), lm.name.c_str());
        gtk_range_set_range(GTK_RANGE(c->speed), 0, c->side ? std::max(1, m.maxSideSpeed) : m.maxSpeed);
        gtk_combo_box_set_active(GTK_COMBO_BOX(c->mode), 0);
    }
    gtk_widget_set_visible(app->side.frame, !m.sideLightModes.empty());
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(app->keyLayer), 0, m.layers - 1);
    gtk_widget_set_sensitive(app->keyOs, m.chip == Chip::YC3123);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(app->profile), 0, m.layers - 1);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(app->perKeyLayer), 0, m.chip == Chip::YC3123 ? 4 : 0);
    gtk_spin_button_set_range(GTK_SPIN_BUTTON(app->screenSlot), 1, m.screenSlots);
}

void refreshDevices(App* app) {
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->deviceCombo));
    app->devicePaths.clear();
    if (!app->dryRun.empty()) {
        std::string label = "Dry run (" + app->dryRun + ", no hardware)";
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->deviceCombo), label.c_str());
        app->devicePaths.push_back({});
    } else {
        for (const auto& c : findCandidates()) {
            std::string label = c.info.path + "  " + c.model->displayName + "  " + c.info.name;
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->deviceCombo), label.c_str());
            app->devicePaths.push_back(c.info.path);
        }
        if (app->devicePaths.empty())
            setStatus(app, "No K86 found. Connect it with the USB cable and check permissions (udev rule).");
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(app->deviceCombo), 0);
}

void connectDevice(App* app) {
    if (app->sysTimer) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->sysSync), FALSE);
    app->kb.reset();
    app->transport.reset();
    gtk_widget_set_sensitive(app->notebook, FALSE);
    try {
        if (!app->dryRun.empty()) {
            app->transport = std::make_unique<EchoTransport>();
            const DeviceModel& m = app->dryRun == "v1" ? knownModels()[0] : knownModels()[1];
            app->kb = makeKeyboard(*app->transport, m, [](int) {});
            setStatus(app, m.displayName + " - dry run");
        } else {
            int i = gtk_combo_box_get_active(GTK_COMBO_BOX(app->deviceCombo));
            if (i < 0 || size_t(i) >= app->devicePaths.size()) throw std::runtime_error("no keyboard selected");
            OpenKeyboard ok = openKeyboard(app->devicePaths[size_t(i)], false);
            Versions v = ok.keyboard->readVersions();
            char ver[64] = "";
            if (v.usb) snprintf(ver, sizeof ver, ", USB fw 0x%04x", *v.usb);
            setStatus(app, ok.keyboard->model().displayName + " (" + ok.keyboard->model().firmwareName + ") at " +
                               ok.info.path + ", firmware id " +
                               (ok.firmwareId ? std::to_string(*ok.firmwareId) : std::string("?")) + ver);
            app->transport = std::move(ok.transport);
            app->kb = std::move(ok.keyboard);
        }
    } catch (const std::exception& e) {
        setStatus(app, std::string("Connection failed: ") + e.what());
        return;
    }
    populateForModel(app);
    gtk_widget_set_sensitive(app->notebook, TRUE);
    readLight(&app->light);
    if (!app->kb->model().sideLightModes.empty()) readLight(&app->side);
    readSettings(app);
}

struct Options {
    std::string dryRun, screenshot;
};

gboolean takeScreenshots(gpointer d) {
    auto* pair = static_cast<std::pair<App*, std::string>*>(d);
    App* app = pair->first;
    int pages = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    for (int i = 0; i < pages; ++i) {
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), i);
        for (int k = 0; k < 20; ++k) pumpEvents();
        GdkWindow* w = gtk_widget_get_window(app->window);
        GdkPixbuf* pb = gdk_pixbuf_get_from_window(w, 0, 0, gdk_window_get_width(w), gdk_window_get_height(w));
        if (pb) {
            std::string file = pair->second + "-" + std::to_string(i) + ".png";
            gdk_pixbuf_save(pb, file.c_str(), "png", nullptr, nullptr);
            g_object_unref(pb);
        }
    }
    gtk_main_quit();
    return G_SOURCE_REMOVE;
}

}  // namespace

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dry-run") opt.dryRun = "v2";
        else if (a.rfind("--dry-run=", 0) == 0) opt.dryRun = a.substr(10);
        else if (a == "--screenshot" && i + 1 < argc) opt.screenshot = argv[++i];
        else {
            printf("Usage: k86-gui [--dry-run[=v1|v2]] [--screenshot PREFIX]\n"
                   "  --dry-run      use a simulated K86 (v1 = YC3121, v2 = YC3123) instead of hardware\n"
                   "  --screenshot   save every tab as PREFIX-N.png and exit\n");
            return a == "--help" || a == "-h" ? 0 : 1;
        }
    }
    if (!opt.dryRun.empty() && opt.dryRun != "v1" && opt.dryRun != "v2") {
        fprintf(stderr, "--dry-run takes v1 or v2\n");
        return 1;
    }

    App app;
    app.dryRun = opt.dryRun;
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Attack Shark K86");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 900, 680);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    GtkWidget* root = vbox();
    GtkWidget* top = hbox();
    app.deviceCombo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new("Keyboard:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), app.deviceCombo, TRUE, TRUE, 0);
    button(top, "Refresh", G_CALLBACK(+[](GtkButton*, gpointer d) { refreshDevices(static_cast<App*>(d)); }), &app);
    button(top, "Connect", G_CALLBACK(+[](GtkButton*, gpointer d) { connectDevice(static_cast<App*>(d)); }), &app);
    gtk_box_pack_start(GTK_BOX(root), top, FALSE, FALSE, 0);
    app.status = labeled("Not connected");
    gtk_label_set_selectable(GTK_LABEL(app.status), TRUE);
    gtk_box_pack_start(GTK_BOX(root), app.status, FALSE, FALSE, 0);

    app.notebook = gtk_notebook_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), buildLightTab(&app), gtk_label_new("Lighting"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), buildKeysTab(&app), gtk_label_new("Key mapping"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), buildPerKeyTab(&app), gtk_label_new("Per-key colours"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), buildMacroTab(&app), gtk_label_new("Macros"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), buildScreenTab(&app), gtk_label_new("Screen"));
    gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), buildSettingsTab(&app), gtk_label_new("Settings"));
    gtk_widget_set_sensitive(app.notebook, FALSE);
    gtk_box_pack_start(GTK_BOX(root), app.notebook, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(app.window), root);
    gtk_widget_show_all(app.window);

    refreshDevices(&app);
    if (app.devicePaths.size() == 1) connectDevice(&app);

    std::pair<App*, std::string> shot{&app, opt.screenshot};
    if (!opt.screenshot.empty()) g_timeout_add(300, takeScreenshots, &shot);
    gtk_main();
    if (app.sysTimer) g_source_remove(app.sysTimer);
    return 0;
}
