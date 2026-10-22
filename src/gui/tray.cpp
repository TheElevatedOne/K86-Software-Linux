#include "tray.hpp"

#ifdef K86_HAVE_AYATANA
#include <libayatana-appindicator/app-indicator.h>
// libayatana-appindicator 0.6 marks its GTK 3 API deprecated in favour of the GTK-free
// ayatana-appindicator-glib; the GTK 3 API is what Ubuntu 22.04 (0.5.x) and the distros ship.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <string>

namespace k86gui {

namespace {

constexpr const char* kIconName = "k86-gui";

#ifdef K86_HAVE_AYATANA
// Directory with hicolor/<size>/apps/k86-gui.png for builds that are run without installing.
std::string sourceIconDir() {
#ifdef K86_SOURCE_ICON_DIR
    std::string d = K86_SOURCE_ICON_DIR;
    if (g_file_test((d + "/hicolor/32x32/apps/k86-gui.png").c_str(), G_FILE_TEST_EXISTS)) return d;
#endif
    return {};
}
#endif

}  // namespace

Tray::~Tray() { stop(); }

bool Tray::available() {
#ifdef K86_HAVE_AYATANA
    return true;
#else
    return false;
#endif
}

bool Tray::start() {
#ifdef K86_HAVE_AYATANA
    if (indicator_) return true;
    menu_ = gtk_menu_new();
    g_object_ref_sink(menu_);
    statusItem_ = gtk_menu_item_new_with_label(status_.c_str());
    gtk_widget_set_sensitive(statusItem_, FALSE);
    toggleItem_ = gtk_menu_item_new_with_label("Show window");
    GtkWidget* quit = gtk_menu_item_new_with_label("Quit");
    for (GtkWidget* w : {statusItem_, gtk_separator_menu_item_new(), toggleItem_, gtk_separator_menu_item_new(), quit})
        gtk_menu_shell_append(GTK_MENU_SHELL(menu_), w);
    g_signal_connect(toggleItem_, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
                         auto* t = static_cast<Tray*>(d);
                         if (t->onActivate) t->onActivate();
                     }),
                     this);
    g_signal_connect(quit, "activate", G_CALLBACK(+[](GtkMenuItem*, gpointer d) {
                         auto* t = static_cast<Tray*>(d);
                         if (t->onQuit) t->onQuit();
                     }),
                     this);
    gtk_widget_show_all(menu_);

    AppIndicator* ind = app_indicator_new("k86-gui", kIconName, APP_INDICATOR_CATEGORY_HARDWARE);
    indicator_ = ind;
    // Not installed: point the tray host at the icons in the source tree.
    if (!gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), kIconName)) {
        std::string dir = sourceIconDir();
        if (!dir.empty()) app_indicator_set_icon_theme_path(ind, dir.c_str());
        else app_indicator_set_icon_full(ind, "input-keyboard", "Attack Shark K86");
    }
    app_indicator_set_title(ind, "Attack Shark K86");
    app_indicator_set_menu(ind, GTK_MENU(menu_));
    app_indicator_set_secondary_activate_target(ind, toggleItem_);  // middle click
    connId_ = g_signal_connect(ind, "connection-changed", G_CALLBACK(+[](AppIndicator*, gboolean connected, gpointer d) {
                                   static_cast<Tray*>(d)->setRegistered(connected);
                               }),
                               this);
    app_indicator_set_status(ind, APP_INDICATOR_STATUS_ACTIVE);
    menuChanged();
    return true;
#else
    return false;
#endif
}

void Tray::stop() {
#ifdef K86_HAVE_AYATANA
    if (!indicator_) return;
    auto* ind = static_cast<AppIndicator*>(indicator_);
    if (connId_) g_signal_handler_disconnect(ind, connId_);
    connId_ = 0;
    app_indicator_set_status(ind, APP_INDICATOR_STATUS_PASSIVE);
    g_object_unref(ind);
    indicator_ = nullptr;
    gtk_widget_destroy(menu_);
    g_object_unref(menu_);
    menu_ = statusItem_ = toggleItem_ = nullptr;
    setRegistered(false);
#endif
}

void Tray::setRegistered(bool r) {
    if (registered_ == r) return;
    registered_ = r;
    if (onRegisteredChanged) onRegisteredChanged(r);
}

void Tray::setStatus(const std::string& text) {
    status_ = text;
    if (statusItem_) gtk_menu_item_set_label(GTK_MENU_ITEM(statusItem_), text.c_str());
}

void Tray::menuChanged() {
    if (toggleItem_)
        gtk_menu_item_set_label(GTK_MENU_ITEM(toggleItem_),
                                windowVisible && windowVisible() ? "Hide window" : "Show window");
}

}  // namespace k86gui
