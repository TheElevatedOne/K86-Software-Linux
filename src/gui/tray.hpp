// System tray icon through libayatana-appindicator (StatusNotifierItem: KDE, waybar, GNOME with
// the AppIndicator extension, ...). Without a tray host `registered()` stays false and the window
// should not hide itself. Builds without the library get a tray that never starts.
#pragma once

#include <gtk/gtk.h>

#include <functional>
#include <string>

namespace k86gui {

class Tray {
public:
    Tray() = default;
    ~Tray();
    Tray(const Tray&) = delete;
    Tray& operator=(const Tray&) = delete;

    // True if k86-gui was built with libayatana-appindicator.
    static bool available();

    // Callbacks (main loop).
    std::function<void()> onActivate;               // "Show window" / "Hide window", middle click
    std::function<void()> onQuit;                   // "Quit"
    std::function<bool()> windowVisible;            // for the Show / Hide menu label
    std::function<void(bool)> onRegisteredChanged;  // a tray host took (or dropped) the icon

    // Creates the indicator; it registers with the tray host now or when one appears.
    bool start();
    void stop();
    bool running() const { return indicator_ != nullptr; }
    bool registered() const { return registered_; }

    // Status line at the top of the menu, e.g. "K86 (YC3121) connected".
    void setStatus(const std::string& text);
    // Re-reads windowVisible() for the menu.
    void menuChanged();

private:
    void* indicator_ = nullptr;  // AppIndicator*
    GtkWidget* menu_ = nullptr;
    GtkWidget* statusItem_ = nullptr;
    GtkWidget* toggleItem_ = nullptr;
    gulong connId_ = 0;
    std::string status_ = "Attack Shark K86";
    bool registered_ = false;

    void setRegistered(bool r);
};

}  // namespace k86gui
