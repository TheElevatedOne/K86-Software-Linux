#include <algorithm>
#include <cstdio>

#include "app.hpp"
#include "layout.hpp"

using namespace k86;

namespace k86gui {

namespace {

struct Callback {
    std::function<void()> f;
};

void attach(GtkWidget* w, const char* signal, std::function<void()> cb) {
    auto* c = new Callback{std::move(cb)};
    g_signal_connect_data(
        w, signal, G_CALLBACK(+[](GtkWidget*, gpointer d) { static_cast<Callback*>(d)->f(); }), c,
        [](gpointer d, GClosure*) { delete static_cast<Callback*>(d); }, GConnectFlags(0));
}

}  // namespace

GtkWidget* vbox(int spacing, int border) {
    GtkWidget* b = gtk_box_new(GTK_ORIENTATION_VERTICAL, spacing);
    if (border) gtk_container_set_border_width(GTK_CONTAINER(b), guint(border));
    return b;
}

GtkWidget* hbox(int spacing) { return gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing); }

GtkWidget* label(const std::string& text, bool dim) {
    GtkWidget* l = gtk_label_new(text.c_str());
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
    if (dim) gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
    return l;
}

GtkWidget* heading(const std::string& text) {
    GtkWidget* l = label(text);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "k86-heading");
    return l;
}

void onClicked(GtkWidget* w, std::function<void()> cb) { attach(w, "clicked", std::move(cb)); }
void onChanged(GtkWidget* w, const char* signal, std::function<void()> cb) { attach(w, signal, std::move(cb)); }

GtkWidget* button(const std::string& text, std::function<void()> cb) {
    GtkWidget* b = gtk_button_new_with_label(text.c_str());
    onClicked(b, std::move(cb));
    return b;
}

GtkWidget* section(const std::string& title, GtkWidget* child) {
    GtkWidget* b = vbox(6);
    gtk_box_pack_start(GTK_BOX(b), heading(title), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(b), child, FALSE, FALSE, 0);
    return b;
}

GtkWidget* scrolledV(GtkWidget* child) {
    GtkWidget* s = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(s), child);
    gtk_widget_set_vexpand(s, TRUE);
    return s;
}

GtkWidget* flowBox() {
    GtkWidget* f = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(f), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(f), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(f), 3);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(f), 8);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(f), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(f), 4);
    gtk_widget_set_valign(f, GTK_ALIGN_START);
    return f;
}

void clearContainer(GtkWidget* c) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(c));
    for (GList* l = children; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);
}

GdkRGBA toRgba(Rgb c) { return GdkRGBA{(c >> 16 & 0xFF) / 255.0, (c >> 8 & 0xFF) / 255.0, (c & 0xFF) / 255.0, 1.0}; }

Rgb fromRgba(const GdkRGBA& c) {
    auto ch = [](double v) { return Rgb(std::clamp(int(v * 255.0 + 0.5), 0, 255)); };
    return ch(c.red) << 16 | ch(c.green) << 8 | ch(c.blue);
}

std::string hexColor(Rgb c) {
    char b[8];
    snprintf(b, sizeof b, "#%06x", c & 0xFFFFFF);
    return b;
}

GtkWidget* swatchButton(Rgb c, const std::string& tooltip, std::function<void()> cb) {
    GtkWidget* b = gtk_button_new();
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 22, 22);
    g_object_set_data(G_OBJECT(area), "rgb", GUINT_TO_POINTER(c));
    g_signal_connect(area, "draw", G_CALLBACK(+[](GtkWidget* w, cairo_t* cr, gpointer) -> gboolean {
                         Rgb c = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w), "rgb"));
                         GdkRGBA rgba = toRgba(c);
                         gdk_cairo_set_source_rgba(cr, &rgba);
                         cairo_paint(cr);
                         return TRUE;
                     }),
                     nullptr);
    gtk_container_add(GTK_CONTAINER(b), area);
    gtk_widget_set_tooltip_text(b, tooltip.c_str());
    gtk_style_context_add_class(gtk_widget_get_style_context(b), "k86-swatch");
    onClicked(b, std::move(cb));
    return b;
}

void addCss() {
    static const char* css = R"(
        .k86-heading { font-weight: bold; font-size: 110%; margin-top: 4px; }
        .k86-swatch { padding: 2px; min-width: 0; min-height: 0; }
        .k86-pill { border-radius: 9px; padding: 1px 8px; background-color: alpha(#ff8a1f, 0.18); color: #ff8a1f; }
        .k86-pill-warn { border-radius: 9px; padding: 1px 8px; background-color: alpha(#e5484d, 0.18); color: #e5484d; }
        .k86-current { font-weight: bold; }
        .k86-action { padding: 4px 10px; }
        .k86-empty { font-size: 120%; }
    )";
    GtkCssProvider* p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(p),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

uint8_t evdevToHid(unsigned code) {
    // linux/input-event-codes.h -> USB HID keyboard page.
    static const struct {
        unsigned evdev;
        uint8_t hid;
    } map[] = {
        {1, 0x29},   {2, 0x1E},   {3, 0x1F},   {4, 0x20},   {5, 0x21},   {6, 0x22},   {7, 0x23},   {8, 0x24},
        {9, 0x25},   {10, 0x26},  {11, 0x27},  {12, 0x2D},  {13, 0x2E},  {14, 0x2A},  {15, 0x2B},  {16, 0x14},
        {17, 0x1A},  {18, 0x08},  {19, 0x15},  {20, 0x17},  {21, 0x1C},  {22, 0x18},  {23, 0x0C},  {24, 0x12},
        {25, 0x13},  {26, 0x2F},  {27, 0x30},  {28, 0x28},  {29, 0xE0},  {30, 0x04},  {31, 0x16},  {32, 0x07},
        {33, 0x09},  {34, 0x0A},  {35, 0x0B},  {36, 0x0D},  {37, 0x0E},  {38, 0x0F},  {39, 0x33},  {40, 0x34},
        {41, 0x35},  {42, 0xE1},  {43, 0x31},  {44, 0x1D},  {45, 0x1B},  {46, 0x06},  {47, 0x19},  {48, 0x05},
        {49, 0x11},  {50, 0x10},  {51, 0x36},  {52, 0x37},  {53, 0x38},  {54, 0xE5},  {55, 0x55},  {56, 0xE2},
        {57, 0x2C},  {58, 0x39},  {59, 0x3A},  {60, 0x3B},  {61, 0x3C},  {62, 0x3D},  {63, 0x3E},  {64, 0x3F},
        {65, 0x40},  {66, 0x41},  {67, 0x42},  {68, 0x43},  {69, 0x53},  {70, 0x47},  {71, 0x5F},  {72, 0x60},
        {73, 0x61},  {74, 0x56},  {75, 0x5C},  {76, 0x5D},  {77, 0x5E},  {78, 0x57},  {79, 0x59},  {80, 0x5A},
        {81, 0x5B},  {82, 0x62},  {83, 0x63},  {86, 0x64},  {87, 0x44},  {88, 0x45},  {96, 0x58},  {97, 0xE4},
        {98, 0x54},  {99, 0x46},  {100, 0xE6}, {102, 0x4A}, {103, 0x52}, {104, 0x4B}, {105, 0x50}, {106, 0x4F},
        {107, 0x4D}, {108, 0x51}, {109, 0x4E}, {110, 0x49}, {111, 0x4C}, {117, 0x67}, {119, 0x48}, {125, 0xE3},
        {126, 0xE7}, {127, 0x65}, {183, 0x68}, {184, 0x69}, {185, 0x6A}, {186, 0x6B}, {187, 0x6C}, {188, 0x6D},
        {189, 0x6E}, {190, 0x6F}, {191, 0x70}, {192, 0x71}, {193, 0x72}, {194, 0x73},
    };
    for (const auto& m : map)
        if (m.evdev == code) return m.hid;
    return 0;
}

// ------------------------------------------------------------------------- presentation helpers
std::string lightModeLabel(const LightModeInfo& m) {
    static const std::map<std::string, std::string> names = {
        {"off", "Off"},           {"static", "Static"},         {"breath", "Breathing"},
        {"neon", "Neon"},         {"wave", "Wave"},             {"ripple", "Ripple"},
        {"raindrop", "Raindrop"}, {"snake", "Snake"},           {"reactive", "Reactive"},
        {"converge", "Converge"}, {"sinewave", "Sine wave"},    {"kaleidoscope", "Kaleidoscope"},
        {"linewave", "Line wave"}, {"custom", "Custom (per key)"}, {"laser", "Laser"},
        {"circlewave", "Circle wave"}, {"dazzle", "Dazzle"},    {"raindown", "Rain down"},
        {"meteor", "Meteor"},     {"reactiveoff", "Reactive (fade)"}, {"music", "Music"},
        {"screencolor", "Screen colour"}, {"music3", "Music 3"}, {"train", "Train"}, {"fireworks", "Fireworks"},
    };
    auto it = names.find(m.name);
    return it == names.end() ? m.name : it->second;
}

std::vector<std::string> lightOptionNames(const LightModeInfo& m) {
    // Option names of the original's light layouts (translated).
    if (m.name == "wave") return {"Right", "Left", "Down", "Up"};
    if (m.name == "snake") return {"Zigzag", "Loop"};
    if (m.name == "kaleidoscope") return {"Outward", "Inward"};
    if (m.name == "linewave") return {"Right", "Left"};
    if (m.name == "circlewave") return {"Counter-clockwise", "Clockwise"};
    if (m.name == "music") return {"Upright", "Separate", "Intersect"};
    std::vector<std::string> v;
    for (int i = 0; i < m.options; ++i) v.push_back(m.name == "custom" ? "Pattern " + std::to_string(i + 1)
                                                                       : "Option " + std::to_string(i + 1));
    return v;
}

KeyAction matrixAction(const KeyMatrix& m, int slot) {
    return {m[size_t(slot) * 4], m[size_t(slot) * 4 + 1], m[size_t(slot) * 4 + 2], m[size_t(slot) * 4 + 3]};
}

std::string slotLabel(const DeviceModel& m, int slot) {
    for (const auto& k : k86Layout()) {
        if (layoutSlot(m, k) != slot) continue;
        if (k.id == "AudioVolumeDown") return "Knob turn left";
        if (k.id == "AudioVolumeUp") return "Knob turn right";
        if (k.id == "AudioVolumeMute") return "Knob press";
        if (k.id == "Fn") return "Fn";
        return actionLabel(k.factory);
    }
    const uint8_t* d = m.defaultMatrix + slot * 4;
    KeyAction a{d[0], d[1], d[2], d[3]};
    if (a == KeyAction{0, 0, 0, 0}) return "Slot " + std::to_string(slot);
    return actionLabel(a);
}

}  // namespace k86gui
