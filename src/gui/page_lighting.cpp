// "Lighting" (effects of the backlight and side light) and "Per-key colours" pages.
#include <algorithm>
#include <cmath>

#include "app.hpp"
#include "layout.hpp"

using namespace k86;

namespace k86gui {

namespace {

// The colour order of the keyboard's own Fn+Enter cycle.
const std::pair<Rgb, const char*> kPresets[] = {
    {0xFF0000, "Red"},  {0xFF8000, "Orange"}, {0xFFFF00, "Yellow"}, {0x00FF00, "Green"},
    {0x00FFFF, "Cyan"}, {0x0000FF, "Blue"},   {0x8000FF, "Purple"}, {0xFFFFFF, "White"},
};

Rgb hue(double h) {
    h = std::fmod(h, 1.0) * 6.0;
    double x = 1 - std::fabs(std::fmod(h, 2.0) - 1);
    double r = 0, g = 0, b = 0;
    switch (int(h)) {
        case 0: r = 1, g = x; break;
        case 1: r = x, g = 1; break;
        case 2: g = 1, b = x; break;
        case 3: g = x, b = 1; break;
        case 4: r = x, b = 1; break;
        default: r = 1, b = x; break;
    }
    return Rgb(std::lround(r * 255)) << 16 | Rgb(std::lround(g * 255)) << 8 | Rgb(std::lround(b * 255));
}

Rgb scale(Rgb c, double f) {
    auto ch = [&](int s) { return Rgb(std::clamp(int(std::lround(((c >> s) & 0xFF) * f)), 0, 255)) << s; };
    return ch(16) | ch(8) | ch(0);
}

struct LightGroup {
    App* app = nullptr;
    bool side = false;
    GtkWidget* frame = nullptr;
    GtkWidget* effects = nullptr;  // flow box of radio buttons
    std::vector<GtkWidget*> effectButtons;
    GtkWidget* brightness = nullptr;
    GtkWidget* speed = nullptr;
    GtkWidget* speedRow = nullptr;
    GtkWidget* option = nullptr;
    GtkWidget* optionRow = nullptr;
    GtkWidget* colorRow = nullptr;
    GtkWidget* colorButton = nullptr;
    GtkWidget* rainbow = nullptr;
    guint timer = 0;
    std::function<void()> changed;  // redraw the preview

    const std::vector<LightModeInfo>& modes() const {
        static const std::vector<LightModeInfo> none;
        const DeviceModel* m = app->cache.model;
        if (!m) return none;
        return side ? m->sideLightModes : m->lightModes;
    }
    std::optional<LightSetting>& value() { return side ? app->cache.side : app->cache.light; }
};

const LightModeInfo* currentMode(LightGroup* g) {
    if (!g->value()) return nullptr;
    for (const auto& m : g->modes())
        if (m.code == g->value()->mode) return &m;
    return nullptr;
}

void updateSensitivity(LightGroup* g) {
    const LightModeInfo* m = currentMode(g);
    gtk_widget_set_sensitive(g->speedRow, m && m->speed);
    gtk_widget_set_visible(g->optionRow, m && m->options > 0);
    gtk_widget_set_sensitive(g->colorRow, m && m->color);
    gtk_widget_set_sensitive(g->brightness, m && m->code != 0);
}

void fillGroup(LightGroup* g) {
    Updating u(g->app);
    const auto& modes = g->modes();
    const auto& v = g->value();
    for (size_t i = 0; i < g->effectButtons.size(); ++i)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->effectButtons[i]),
                                     v && i < modes.size() && modes[i].code == v->mode);
    if (!v) return;
    gtk_range_set_value(GTK_RANGE(g->brightness), v->brightness);
    gtk_range_set_value(GTK_RANGE(g->speed), v->speed);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(g->option));
    if (const LightModeInfo* m = currentMode(g)) {
        for (const auto& n : lightOptionNames(*m)) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g->option), n.c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(g->option), std::min<int>(v->option, std::max(0, m->options - 1)));
    }
    GdkRGBA rgba = toRgba(v->color);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(g->colorButton), &rgba);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->rainbow), v->rainbow);
    updateSensitivity(g);
}

// Debounced write of the group's current setting (sliders send many changes).
void scheduleWrite(LightGroup* g) {
    if (g->app->updating || !g->value()) return;
    if (g->changed) g->changed();
    updateSensitivity(g);
    if (g->timer) g_source_remove(g->timer);
    g->timer = g_timeout_add(350, +[](gpointer d) -> gboolean {
        auto* g = static_cast<LightGroup*>(d);
        g->timer = 0;
        if (!g->app->canWrite()) return G_SOURCE_REMOVE;
        LightSetting l = *g->value();
        bool side = g->side;
        std::string what = side ? "Writing side light" : "Writing backlight";
        g->app->worker.dropQueued(what);
        g->app->worker.post(
            what,
            [l, side](Keyboard& kb, const Worker::ProgressFn&) {
                kb.leaveUserAnimation();
                side ? kb.setSideLight(l) : kb.setLight(l);
            },
            [g, side] { g->app->status(side ? "Side light saved." : "Backlight saved."); });
        return G_SOURCE_REMOVE;
    }, g);
}

GtkWidget* sliderRow(const char* text, GtkWidget* scale) {
    GtkWidget* row = hbox(10);
    GtkWidget* l = label(text);
    gtk_widget_set_size_request(l, 110, -1);
    gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), scale, TRUE, TRUE, 0);
    return row;
}

GtkWidget* buildGroup(App* app, LightGroup* g, bool side) {
    g->app = app;
    g->side = side;
    GtkWidget* box = vbox(8);
    gtk_box_pack_start(GTK_BOX(box), heading(side ? "Side light" : "Backlight"), FALSE, FALSE, 0);
    g->effects = flowBox();
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(g->effects), TRUE);
    gtk_box_pack_start(GTK_BOX(box), g->effects, FALSE, FALSE, 0);

    g->brightness = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 4, 1);
    g->speed = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 4, 1);
    for (GtkWidget* s : {g->brightness, g->speed}) {
        gtk_scale_set_digits(GTK_SCALE(s), 0);
        gtk_scale_set_value_pos(GTK_SCALE(s), GTK_POS_RIGHT);
        gtk_range_set_round_digits(GTK_RANGE(s), 0);
    }
    gtk_box_pack_start(GTK_BOX(box), sliderRow("Brightness", g->brightness), FALSE, FALSE, 0);
    g->speedRow = sliderRow("Speed", g->speed);
    gtk_box_pack_start(GTK_BOX(box), g->speedRow, FALSE, FALSE, 0);
    g->option = gtk_combo_box_text_new();
    g->optionRow = sliderRow("Direction", g->option);
    gtk_widget_set_halign(g->option, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), g->optionRow, FALSE, FALSE, 0);

    g->colorRow = hbox(4);
    GtkWidget* cl = label("Colour");
    gtk_widget_set_size_request(cl, 110, -1);
    gtk_box_pack_start(GTK_BOX(g->colorRow), cl, FALSE, FALSE, 0);
    for (const auto& [c, name] : kPresets) {
        gtk_box_pack_start(GTK_BOX(g->colorRow), swatchButton(c, name, [g, c = c] {
                               if (!g->value()) return;
                               g->value()->color = c;
                               g->value()->rainbow = false;
                               fillGroup(g);
                               scheduleWrite(g);
                           }),
                           FALSE, FALSE, 0);
    }
    g->colorButton = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(g->colorButton), FALSE);
    gtk_widget_set_tooltip_text(g->colorButton, "Any colour");
    gtk_box_pack_start(GTK_BOX(g->colorRow), g->colorButton, FALSE, FALSE, 6);
    g->rainbow = gtk_check_button_new_with_label("Rainbow");
    gtk_box_pack_start(GTK_BOX(g->colorRow), g->rainbow, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(box), g->colorRow, FALSE, FALSE, 0);

    onChanged(g->brightness, "value-changed", [g] {
        if (g->app->updating || !g->value()) return;
        g->value()->brightness = uint8_t(gtk_range_get_value(GTK_RANGE(g->brightness)));
        scheduleWrite(g);
    });
    onChanged(g->speed, "value-changed", [g] {
        if (g->app->updating || !g->value()) return;
        g->value()->speed = uint8_t(gtk_range_get_value(GTK_RANGE(g->speed)));
        scheduleWrite(g);
    });
    onChanged(g->option, "changed", [g] {
        if (g->app->updating || !g->value()) return;
        g->value()->option = uint8_t(std::max(0, gtk_combo_box_get_active(GTK_COMBO_BOX(g->option))));
        scheduleWrite(g);
    });
    onChanged(g->colorButton, "color-set", [g] {
        if (!g->value()) return;
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(g->colorButton), &rgba);
        g->value()->color = fromRgba(rgba);
        g->value()->rainbow = false;
        fillGroup(g);
        scheduleWrite(g);
    });
    onChanged(g->rainbow, "toggled", [g] {
        if (g->app->updating || !g->value()) return;
        g->value()->rainbow = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g->rainbow));
        scheduleWrite(g);
    });
    g->frame = box;
    return box;
}

// Rebuilds the effect buttons for the connected model.
void populateGroup(LightGroup* g) {
    clearContainer(g->effects);
    g->effectButtons.clear();
    GtkWidget* first = nullptr;
    for (const auto& m : g->modes()) {
        GtkWidget* b = first ? gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(first),
                                                                           lightModeLabel(m).c_str())
                             : gtk_radio_button_new_with_label(nullptr, lightModeLabel(m).c_str());
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(b), FALSE);  // look like buttons
        if (!first) first = b;
        uint8_t code = m.code;
        onChanged(b, "toggled", [g, b, code] {
            if (g->app->updating || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b))) return;
            if (!g->value()) g->value() = LightSetting{};
            g->value()->mode = code;
            g->value()->option = 0;
            fillGroup(g);
            scheduleWrite(g);
        });
        gtk_container_add(GTK_CONTAINER(g->effects), b);
        g->effectButtons.push_back(b);
    }
    const DeviceModel* m = g->app->cache.model;
    if (m) gtk_range_set_range(GTK_RANGE(g->speed), 0, g->side ? m->maxSideSpeed : m->maxSpeed);
    gtk_widget_show_all(g->effects);
}

// Approximation of an effect for the preview drawing.
KeyLook previewLook(App* app, const KeyGeometry& k, int slot) {
    const auto& l = app->cache.light;
    KeyLook look{k.knob ? "" : k.legend, {}, KeyLook::Style::Normal, true, 0x101114};
    if (!l || l->mode == 0) return look;
    double bright = 0.25 + 0.75 * l->brightness / 4.0;
    if (l->mode == 13) {  // custom: the stored picture
        size_t pat = app->cache.model && app->cache.model->chip == Chip::YC3123 ? l->option : 0;
        if (pat < app->cache.pictures.size()) look.fill = scale(app->cache.pictures[pat][size_t(slot)], bright);
        return look;
    }
    Rgb base = l->rainbow || l->mode == 3 ? hue((k.x + k.w / 2) / kLayoutWidth) : l->color;
    if (l->mode == 21) base = 0x808080;
    look.fill = scale(base, bright);
    return look;
}

struct LightingPage {
    App* app = nullptr;
    KeyboardView* preview = nullptr;
    LightGroup light, side;
    GtkWidget* sideBox = nullptr;
    GtkWidget* note = nullptr;
};

}  // namespace

Page buildLightingPage(App* app) {
    auto* p = new LightingPage();
    p->app = app;
    GtkWidget* content = vbox(12, 14);
    g_object_set_data_full(G_OBJECT(content), "page", p, [](gpointer d) { delete static_cast<LightingPage*>(d); });

    p->preview = new KeyboardView();
    p->preview->selectable = false;
    p->preview->describe = [app](const KeyGeometry& k, int slot) { return previewLook(app, k, slot); };
    gtk_box_pack_start(GTK_BOX(content), p->preview->widget(), FALSE, FALSE, 0);
    p->note = label("The drawing is a still approximation of the effect.", true);
    gtk_box_pack_start(GTK_BOX(content), p->note, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(content), buildGroup(app, &p->light, false), FALSE, FALSE, 0);
    p->sideBox = buildGroup(app, &p->side, true);
    gtk_box_pack_start(GTK_BOX(content), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(content), p->sideBox, FALSE, FALSE, 0);
    p->light.changed = [p] { p->preview->redraw(); };
    p->side.changed = [] {};

    Page result{"lighting", "Lighting", scrolledV(content), {}};
    result.refresh = [p] {
        const DeviceModel* m = p->app->cache.model;
        if (p->preview->model() != m) {
            p->preview->setModel(m);
            populateGroup(&p->light);
            populateGroup(&p->side);
        }
        gtk_widget_set_visible(p->sideBox, m && !m->sideLightModes.empty());
        fillGroup(&p->light);
        fillGroup(&p->side);
        p->preview->redraw();
    };
    return result;
}

// ------------------------------------------------------------------------------ per-key colours
namespace {

struct PerKeyPage {
    App* app = nullptr;
    KeyboardView* view = nullptr;
    std::array<Rgb, kSlots> work{};
    int pattern = 0;
    bool dirty = false;
    Rgb brush = 0xFF0000;
    GtkWidget* brushButton = nullptr;
    GtkWidget* patternBox = nullptr;
    GtkWidget* patternCombo = nullptr;
    GtkWidget* pick = nullptr;
    GtkWidget* state = nullptr;
    GtkWidget* apply = nullptr;
};

void setDirty(PerKeyPage* p, bool d) {
    p->dirty = d;
    gtk_label_set_text(GTK_LABEL(p->state), d ? "Unsaved changes" : "Same as on the keyboard");
    gtk_widget_set_sensitive(p->apply, d);
    p->view->redraw();
}

void loadWork(PerKeyPage* p) {
    const auto& pics = p->app->cache.pictures;
    if (size_t(p->pattern) < pics.size()) p->work = pics[size_t(p->pattern)];
    setDirty(p, false);
}

void setBrush(PerKeyPage* p, Rgb c) {
    p->brush = c;
    GdkRGBA rgba = toRgba(c);
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(p->brushButton), &rgba);
}

}  // namespace

Page buildPerKeyPage(App* app) {
    auto* p = new PerKeyPage();
    p->app = app;
    GtkWidget* content = vbox(10, 14);
    g_object_set_data_full(G_OBJECT(content), "page", p, [](gpointer d) { delete static_cast<PerKeyPage*>(d); });

    GtkWidget* top = hbox(10);
    gtk_box_pack_start(GTK_BOX(top), heading("Per-key colours"), FALSE, FALSE, 0);
    p->patternBox = hbox(6);
    p->patternCombo = gtk_combo_box_text_new();
    for (int i = 1; i <= 5; ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->patternCombo), ("Pattern " + std::to_string(i)).c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(p->patternCombo), 0);
    gtk_box_pack_start(GTK_BOX(p->patternBox), p->patternCombo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), p->patternBox, FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(content), top, FALSE, FALSE, 0);
    onChanged(p->patternCombo, "changed", [p] {
        int i = gtk_combo_box_get_active(GTK_COMBO_BOX(p->patternCombo));
        if (i < 0 || i == p->pattern) return;
        if (p->dirty && !p->app->confirm("Discard the unsaved colours of this pattern?")) {
            Updating u(p->app);
            gtk_combo_box_set_active(GTK_COMBO_BOX(p->patternCombo), p->pattern);
            return;
        }
        p->pattern = i;
        loadWork(p);
    });

    p->view = new KeyboardView();
    p->view->selectable = false;
    p->view->describe = [p](const KeyGeometry& k, int slot) {
        return KeyLook{k.knob ? "" : k.legend, {}, KeyLook::Style::Normal, true, p->work[size_t(slot)]};
    };
    p->view->tooltip = [p](const KeyGeometry&, int slot) {
        return p->app->cache.model ? slotLabel(*p->app->cache.model, slot) + "  " + hexColor(p->work[size_t(slot)])
                                   : std::string();
    };
    p->view->onDrag = [p](int slot) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->pick))) {
            setBrush(p, p->work[size_t(slot)]);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->pick), FALSE);
            return;
        }
        if (p->work[size_t(slot)] == p->brush) return;
        p->work[size_t(slot)] = p->brush;
        setDirty(p, true);
    };
    gtk_box_pack_start(GTK_BOX(content), p->view->widget(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), label("Click or drag over keys to paint them with the brush colour.", true),
                       FALSE, FALSE, 0);

    GtkWidget* tools = hbox(4);
    gtk_box_pack_start(GTK_BOX(tools), label("Brush:"), FALSE, FALSE, 4);
    p->brushButton = gtk_color_button_new();
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(p->brushButton), FALSE);
    setBrush(p, 0xFF0000);
    onChanged(p->brushButton, "color-set", [p] {
        GdkRGBA rgba;
        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(p->brushButton), &rgba);
        p->brush = fromRgba(rgba);
    });
    gtk_box_pack_start(GTK_BOX(tools), p->brushButton, FALSE, FALSE, 0);
    for (const auto& [c, name] : kPresets)
        gtk_box_pack_start(GTK_BOX(tools), swatchButton(c, name, [p, c = c] { setBrush(p, c); }), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tools), swatchButton(0x000000, "Off (black)", [p] { setBrush(p, 0); }), FALSE, FALSE, 0);
    p->pick = gtk_toggle_button_new_with_label("Pick from key");
    gtk_widget_set_tooltip_text(p->pick, "Click a key to take its colour as the brush");
    gtk_box_pack_start(GTK_BOX(tools), p->pick, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(tools), button("Fill all", [p] {
                           if (!p->app->cache.model) return;
                           for (const auto& k : k86Layout())
                               if (auto s = layoutSlot(*p->app->cache.model, k)) p->work[size_t(*s)] = p->brush;
                           setDirty(p, true);
                       }),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(tools), button("Rainbow", [p] {
                           if (!p->app->cache.model) return;
                           for (const auto& k : k86Layout())
                               if (auto s = layoutSlot(*p->app->cache.model, k))
                                   p->work[size_t(*s)] = hue((k.x + k.w / 2) / kLayoutWidth);
                           setDirty(p, true);
                       }),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), tools, FALSE, FALSE, 0);

    GtkWidget* actions = hbox(8);
    p->state = label("");
    gtk_box_pack_start(GTK_BOX(actions), p->state, FALSE, FALSE, 0);
    p->apply = button("Apply to keyboard", [p] {
        App* app = p->app;
        if (!app->canWrite()) return;
        auto colors = p->work;
        uint8_t pattern = uint8_t(p->pattern);
        app->worker.post(
            "Writing per-key colours",
            [colors, pattern](Keyboard& kb, const Worker::ProgressFn&) {
                kb.leaveUserAnimation();
                kb.setUserPicture(pattern, colors);
            },
            [p, colors, pattern] {
                if (pattern < p->app->cache.pictures.size()) p->app->cache.pictures[pattern] = colors;
                setDirty(p, p->work != colors);
                p->app->status("Per-key colours saved.");
                p->app->refreshPages();
            });
    });
    gtk_style_context_add_class(gtk_widget_get_style_context(p->apply), "suggested-action");
    gtk_box_pack_end(GTK_BOX(actions), p->apply, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(actions), button("Revert", [p] { loadWork(p); }), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(actions), button("Use the Custom effect", [p] {
                         App* app = p->app;
                         if (!app->canWrite()) return;
                         LightSetting l = app->cache.light.value_or(LightSetting{});
                         l.mode = 13;
                         l.option = uint8_t(p->pattern);
                         app->cache.light = l;
                         app->worker.post(
                             "Writing backlight",
                             [l](Keyboard& kb, const Worker::ProgressFn&) {
                                 kb.leaveUserAnimation();
                                 kb.setLight(l);
                             },
                             [app] {
                                 app->status("Backlight set to the Custom effect.");
                                 app->refreshPages();
                             });
                     }),
                     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), actions, FALSE, FALSE, 0);
    setDirty(p, false);

    Page result{"perkey", "Per-key colours", scrolledV(content), {}};
    result.refresh = [p] {
        const DeviceModel* m = p->app->cache.model;
        if (p->view->model() != m) {
            p->view->setModel(m);
            p->pattern = 0;
            Updating u(p->app);
            gtk_combo_box_set_active(GTK_COMBO_BOX(p->patternCombo), 0);
            p->dirty = false;
        }
        gtk_widget_set_visible(p->patternBox, m && m->chip == Chip::YC3123);
        if (!p->dirty) loadWork(p);
        p->view->redraw();
    };
    return result;
}

}  // namespace k86gui
