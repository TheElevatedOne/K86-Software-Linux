// Window settings (tray icon, start minimized) in a small key file.
#include <glib.h>

#include "app.hpp"

namespace k86gui {

namespace {
constexpr const char* kGroup = "tray";
}

std::string defaultAppSettingsPath() {
    gchar* p = g_build_filename(g_get_user_config_dir(), "k86-gui", "settings.ini", nullptr);
    std::string s = p;
    g_free(p);
    return s;
}

AppSettings loadAppSettings(const std::string& path) {
    AppSettings s;
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE, nullptr)) {
        auto get = [kf](const char* key, bool& out) {
            GError* e = nullptr;
            gboolean v = g_key_file_get_boolean(kf, kGroup, key, &e);
            if (e) g_error_free(e);
            else out = v;
        };
        get("enabled", s.tray);
        get("keep-running", s.keepInTray);
        get("start-minimized", s.startMinimized);
    }
    g_key_file_free(kf);
    return s;
}

bool saveAppSettings(const std::string& path, const AppSettings& s) {
    gchar* dir = g_path_get_dirname(path.c_str());
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_KEEP_COMMENTS, nullptr);  // keep unknown keys
    g_key_file_set_boolean(kf, kGroup, "enabled", s.tray);
    g_key_file_set_boolean(kf, kGroup, "keep-running", s.keepInTray);
    g_key_file_set_boolean(kf, kGroup, "start-minimized", s.startMinimized);
    GError* e = nullptr;
    bool ok = g_key_file_save_to_file(kf, path.c_str(), &e);
    if (e) {
        g_warning("k86-gui: cannot save %s: %s", path.c_str(), e->message);
        g_error_free(e);
    }
    g_key_file_free(kf);
    return ok;
}

}  // namespace k86gui
