// Drawing of the K86 that reacts to the mouse: hover, click / Ctrl-click selection, drag
// painting. What each key shows is decided by the page through `describe`.
#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <set>
#include <string>

#include "layout.hpp"

namespace k86gui {

struct KeyLook {
    enum class Style { Normal, Changed, Disabled, Function, Dim };
    std::string text;       // big label in the middle of the cap
    std::string corner;     // small label in the top-left corner (usually the printed legend)
    Style style = Style::Normal;
    bool hasFill = false;   // per-key colour mode: fill the cap with `fill`
    k86::Rgb fill = 0;
};

class KeyboardView {
public:
    KeyboardView();
    GtkWidget* widget() const { return area_; }

    void setModel(const k86::DeviceModel* m);
    const k86::DeviceModel* model() const { return model_; }
    void redraw() const { gtk_widget_queue_draw(area_); }

    std::function<KeyLook(const k86::KeyGeometry&, int slot)> describe;
    std::function<std::string(const k86::KeyGeometry&, int slot)> tooltip;
    // Click on a key; `extend` is true for Ctrl / Shift clicks.
    std::function<void(int slot, bool extend)> onClick;
    // Called for every key the pointer enters while the button is held (drag painting).
    std::function<void(int slot)> onDrag;

    // Selection drawn with the accent outline (managed by the page).
    std::set<int> selected;
    bool selectable = true;

    int slotAt(double x, double y) const;
    const k86::KeyGeometry* keyForSlot(int slot) const;

private:
    struct Transform {
        double scale, ox, oy;
    };
    Transform transform() const;
    void draw(cairo_t* cr);

    GtkWidget* area_;
    const k86::DeviceModel* model_ = nullptr;
    int hover_ = -1;
    bool dragging_ = false;
    int lastDrag_ = -1;
};

}  // namespace k86gui
