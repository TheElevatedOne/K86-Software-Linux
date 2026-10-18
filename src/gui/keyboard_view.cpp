#include "keyboard_view.hpp"

#include <algorithm>
#include <cmath>

using namespace k86;

namespace k86gui {

namespace {

constexpr double kPad = 14;  // case border around the keys, in layout units

struct Color {
    double r, g, b;
};
Color hex(uint32_t c) { return {(c >> 16 & 0xFF) / 255.0, (c >> 8 & 0xFF) / 255.0, (c & 0xFF) / 255.0}; }
void set(cairo_t* cr, Color c, double a = 1) { cairo_set_source_rgba(cr, c.r, c.g, c.b, a); }

const Color kCase = hex(0x1B1D22), kCaseEdge = hex(0x2C2F36);
const Color kCap = hex(0x353840), kCapEdge = hex(0x4D525C), kCapHover = hex(0x41454F);
const Color kText = hex(0xECEDEF), kTextDim = hex(0x8A8F99), kCorner = hex(0xA3A8B2);
const Color kAccent = hex(0xFF8A1F);  // selection (Attack Shark orange)
const Color kChanged = hex(0x4EA1FF), kFunction = hex(0x37B39A);

void roundedRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

double luminance(Rgb c) {
    Color k = hex(c);
    return 0.2126 * k.r + 0.7152 * k.g + 0.0722 * k.b;
}

// Draws `text` centred in the box, shrinking the font until it fits.
void centeredText(cairo_t* cr, const std::string& text, double cx, double cy, double maxW, double size,
                  bool bold) {
    if (text.empty()) return;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text.c_str(), -1);
    for (int i = 0; i < 12; ++i) {
        PangoFontDescription* fd = pango_font_description_from_string("Sans");
        pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
        if (bold) pango_font_description_set_weight(fd, PANGO_WEIGHT_SEMIBOLD);
        pango_layout_set_font_description(layout, fd);
        pango_font_description_free(fd);
        int w, h;
        pango_layout_get_pixel_size(layout, &w, &h);
        if (w <= maxW || size < 6) {
            cairo_move_to(cr, cx - w / 2.0, cy - h / 2.0);
            pango_cairo_show_layout(cr, layout);
            break;
        }
        size *= 0.88;
    }
    g_object_unref(layout);
}

void cornerText(cairo_t* cr, const std::string& text, double x, double y, double size) {
    if (text.empty()) return;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, text.c_str(), -1);
    PangoFontDescription* fd = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd, size * PANGO_SCALE);
    pango_layout_set_font_description(layout, fd);
    pango_font_description_free(fd);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
}

}  // namespace

KeyboardView::KeyboardView() {
    area_ = gtk_drawing_area_new();
    gtk_widget_set_size_request(area_, 620, 250);
    gtk_widget_set_hexpand(area_, TRUE);
    gtk_widget_add_events(area_, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                                     GDK_LEAVE_NOTIFY_MASK);
    gtk_widget_set_has_tooltip(area_, TRUE);
    g_object_set_data_full(G_OBJECT(area_), "k86-view", this,
                           [](gpointer p) { delete static_cast<KeyboardView*>(p); });

    g_signal_connect(area_, "draw", G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer d) -> gboolean {
                         static_cast<KeyboardView*>(d)->draw(cr);
                         return TRUE;
                     }),
                     this);
    g_signal_connect(area_, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton* e, gpointer d) -> gboolean {
                         auto* v = static_cast<KeyboardView*>(d);
                         if (e->button != 1 || e->type != GDK_BUTTON_PRESS) return FALSE;
                         int slot = v->slotAt(e->x, e->y);
                         v->dragging_ = true;
                         v->lastDrag_ = slot;
                         if (slot < 0) return TRUE;
                         bool extend = e->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK);
                         if (v->onClick) v->onClick(slot, extend);
                         if (v->onDrag) v->onDrag(slot);
                         v->redraw();
                         return TRUE;
                     }),
                     this);
    g_signal_connect(area_, "button-release-event", G_CALLBACK(+[](GtkWidget*, GdkEventButton*, gpointer d) -> gboolean {
                         auto* v = static_cast<KeyboardView*>(d);
                         v->dragging_ = false;
                         v->lastDrag_ = -1;
                         return TRUE;
                     }),
                     this);
    g_signal_connect(area_, "motion-notify-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventMotion* e, gpointer d) -> gboolean {
                         auto* v = static_cast<KeyboardView*>(d);
                         int slot = v->slotAt(e->x, e->y);
                         if (slot != v->hover_) {
                             v->hover_ = slot;
                             v->redraw();
                         }
                         if (v->dragging_ && slot >= 0 && slot != v->lastDrag_ && v->onDrag) {
                             v->lastDrag_ = slot;
                             v->onDrag(slot);
                             v->redraw();
                         }
                         return TRUE;
                     }),
                     this);
    g_signal_connect(area_, "leave-notify-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer d) -> gboolean {
                         auto* v = static_cast<KeyboardView*>(d);
                         v->hover_ = -1;
                         v->redraw();
                         return FALSE;
                     }),
                     this);
    g_signal_connect(area_, "query-tooltip",
                     G_CALLBACK(+[](GtkWidget*, gint x, gint y, gboolean, GtkTooltip* tip, gpointer d) -> gboolean {
                         auto* v = static_cast<KeyboardView*>(d);
                         int slot = v->slotAt(x, y);
                         const KeyGeometry* k = v->keyForSlot(slot);
                         if (!k || !v->tooltip) return FALSE;
                         std::string t = v->tooltip(*k, slot);
                         if (t.empty()) return FALSE;
                         gtk_tooltip_set_text(tip, t.c_str());
                         return TRUE;
                     }),
                     this);
}

void KeyboardView::setModel(const DeviceModel* m) {
    model_ = m;
    selected.clear();
    hover_ = -1;
    redraw();
}

KeyboardView::Transform KeyboardView::transform() const {
    double w = gtk_widget_get_allocated_width(area_), h = gtk_widget_get_allocated_height(area_);
    double tw = kLayoutWidth + 2 * kPad, th = kLayoutHeight + 2 * kPad;
    double s = std::min(w / tw, h / th);
    return {s, (w - tw * s) / 2 + kPad * s, (h - th * s) / 2 + kPad * s};
}

const KeyGeometry* KeyboardView::keyForSlot(int slot) const {
    if (!model_ || slot < 0) return nullptr;
    for (const auto& k : k86Layout())
        if (layoutSlot(*model_, k) == slot) return &k;
    return nullptr;
}

int KeyboardView::slotAt(double x, double y) const {
    if (!model_) return -1;
    Transform t = transform();
    double lx = (x - t.ox) / t.scale, ly = (y - t.oy) / t.scale;
    for (const auto& k : k86Layout()) {
        if (lx < k.x || lx >= k.x + k.w || ly < k.y || ly >= k.y + k.h) continue;
        if (k.knob && std::hypot(lx - kKnobCx, ly - kKnobCy) > kKnobR) continue;
        if (auto s = layoutSlot(*model_, k)) return *s;
    }
    return -1;
}

void KeyboardView::draw(cairo_t* cr) {
    Transform t = transform();
    const double s = t.scale;
    // Case.
    roundedRect(cr, t.ox - kPad * s, t.oy - kPad * s, (kLayoutWidth + 2 * kPad) * s, (kLayoutHeight + 2 * kPad) * s,
                12 * s);
    set(cr, kCase);
    cairo_fill_preserve(cr);
    set(cr, kCaseEdge);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
    if (!model_) return;

    // Knob body (the three zones are drawn as slices of it).
    cairo_arc(cr, t.ox + kKnobCx * s, t.oy + kKnobCy * s, kKnobR * s, 0, 2 * M_PI);
    set(cr, hex(0x2A2D33));
    cairo_fill(cr);

    for (const auto& k : k86Layout()) {
        auto slotOpt = layoutSlot(*model_, k);
        if (!slotOpt) continue;
        const int slot = *slotOpt;
        KeyLook look = describe ? describe(k, slot) : KeyLook{k.legend, {}, KeyLook::Style::Normal, false, 0};
        const bool sel = selected.count(slot) > 0, hov = slot == hover_;
        double x = t.ox + k.x * s, y = t.oy + k.y * s, w = k.w * s, h = k.h * s;

        cairo_save(cr);
        if (k.knob) {
            cairo_arc(cr, t.ox + kKnobCx * s, t.oy + kKnobCy * s, (kKnobR - 1.5) * s, 0, 2 * M_PI);
            cairo_clip(cr);
            cairo_rectangle(cr, x, y, w, h);
        } else {
            roundedRect(cr, x + 1, y + 1, w - 2, h - 2, 5 * s);
        }
        Color fill = hov ? kCapHover : kCap;
        if (look.style == KeyLook::Style::Disabled || look.style == KeyLook::Style::Dim) fill = hex(0x26282D);
        if (look.hasFill) fill = hex(look.fill);
        set(cr, fill);
        cairo_fill_preserve(cr);
        if (look.hasFill && hov) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
            cairo_fill_preserve(cr);
        }
        Color edge = kCapEdge;
        double lw = 1.2;
        if (look.style == KeyLook::Style::Changed) edge = kChanged, lw = 1.8;
        if (look.style == KeyLook::Style::Function) edge = kFunction, lw = 1.8;
        if (sel) edge = kAccent, lw = 3.0;
        set(cr, edge);
        cairo_set_line_width(cr, lw);
        cairo_stroke(cr);
        cairo_restore(cr);

        // Labels.
        Color tc = kText;
        if (look.hasFill) tc = luminance(look.fill) > 0.55 ? hex(0x15171A) : hex(0xFFFFFF);
        else if (look.style == KeyLook::Style::Disabled || look.style == KeyLook::Style::Dim) tc = kTextDim;
        else if (look.style == KeyLook::Style::Changed) tc = hex(0xBFDCFF);
        else if (look.style == KeyLook::Style::Function) tc = hex(0xB6F0E2);
        if (k.knob) {
            set(cr, tc);
            const char* glyph = k.id == "AudioVolumeDown" ? "−" : k.id == "AudioVolumeUp" ? "+" : "●";
            centeredText(cr, look.text.empty() ? glyph : look.text, x + w / 2, t.oy + kKnobCy * s, w - 2, 10 * s,
                         false);
            continue;
        }
        if (!look.corner.empty()) {
            Color cc = look.hasFill ? tc : kCorner;
            set(cr, cc, look.hasFill ? 0.75 : 1);
            cornerText(cr, look.corner, x + 5 * s, y + 3 * s, 8.5 * s);
        }
        set(cr, tc);
        double cy = y + h / 2 + (look.corner.empty() ? 0 : 4 * s);
        centeredText(cr, look.text, x + w / 2, cy, w - 7 * s, 12 * s, look.style == KeyLook::Style::Changed);
        if (look.style == KeyLook::Style::Disabled && !look.hasFill) {
            set(cr, kTextDim, 0.7);
            cairo_set_line_width(cr, 1.2);
            cairo_move_to(cr, x + w * 0.25, cy);
            cairo_line_to(cr, x + w * 0.75, cy);
            cairo_stroke(cr);
        }
    }
    // Knob outline and zone separators.
    cairo_arc(cr, t.ox + kKnobCx * s, t.oy + kKnobCy * s, kKnobR * s, 0, 2 * M_PI);
    set(cr, kCapEdge);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
}

}  // namespace k86gui
