// Bunbun — the tray icon on Linux (see tray_linux.h).
#include "tray_linux.h"

#include <dlfcn.h>
#include <unistd.h>

#include <string>

#include "compat/d2d1.h"
#include "mascot_png.h"
#include "platform.h"

namespace bb::ltray {
namespace {

// The few AppIndicator calls we need (libayatana-appindicator3 and the older
// libappindicator3 share them).
using NewFn = void* (*)(const char* id, const char* icon, int category);
using SetStatusFn = void (*)(void* ind, int status);
using SetMenuFn = void (*)(void* ind, GtkMenu* menu);
using SetIconFn = void (*)(void* ind, const char* icon, const char* desc);
using SetThemePathFn = void (*)(void* ind, const char* path);
using SetTitleFn = void (*)(void* ind, const char* title);
constexpr int kCategoryApplicationStatus = 0, kStatusActive = 1;

struct AppIndicatorApi {
    NewFn create = nullptr;
    SetStatusFn setStatus = nullptr;
    SetMenuFn setMenu = nullptr;
    SetIconFn setIcon = nullptr;
    SetThemePathFn setThemePath = nullptr;
    SetTitleFn setTitle = nullptr;
};

std::function<GtkWidget*()> g_menuFor;
AppIndicatorApi g_api;
void* g_indicator = nullptr;
GtkStatusIcon* g_status = nullptr;  // the X11 fallback
GtkWidget* g_menu = nullptr;
std::string g_iconDir, g_iconPath;
int g_iconGen = 0;

bool LoadAppIndicator() {
    void* lib = nullptr;
    for (const char* name : {"libayatana-appindicator3.so.1", "libappindicator3.so.1"})
        if ((lib = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break;
    if (!lib) return false;
    g_api.create = reinterpret_cast<NewFn>(dlsym(lib, "app_indicator_new"));
    g_api.setStatus = reinterpret_cast<SetStatusFn>(dlsym(lib, "app_indicator_set_status"));
    g_api.setMenu = reinterpret_cast<SetMenuFn>(dlsym(lib, "app_indicator_set_menu"));
    g_api.setIcon = reinterpret_cast<SetIconFn>(dlsym(lib, "app_indicator_set_icon_full"));
    g_api.setThemePath = reinterpret_cast<SetThemePathFn>(dlsym(lib, "app_indicator_set_icon_theme_path"));
    g_api.setTitle = reinterpret_cast<SetTitleFn>(dlsym(lib, "app_indicator_set_title"));
    return g_api.create && g_api.setStatus && g_api.setMenu && g_api.setIcon && g_api.setThemePath;
}

// The mascot (your character and colour), drawn as a PNG in the runtime folder.
// A new file name each time, so panels that cache icons by name pick it up.
std::string IconName() { return "bunbun-tray-" + std::to_string(g_iconGen); }

bool DrawIcon() {
    const std::string png = bb::plat::RenderMascotPng(bb::mascot::GetStyle(), bb::mascot::Mood::Idle, 64, 0.6, -10, {0, 0});
    if (png.empty()) return false;
    g_iconDir = bb::plat::RuntimeDir() + "/tray";
    bb::plat::MakeDirs(g_iconDir);
    if (!g_iconPath.empty()) unlink(g_iconPath.c_str());
    ++g_iconGen;
    g_iconPath = g_iconDir + "/" + IconName() + ".png";
    return bb::plat::WriteFileAtomic(g_iconPath, png);
}

void RebuildMenu() {
    if (!g_menuFor) return;
    GtkWidget* menu = g_menuFor();
    gtk_widget_show_all(menu);
    if (g_indicator) g_api.setMenu(g_indicator, GTK_MENU(menu));  // the indicator keeps a reference
    if (g_menu) gtk_widget_destroy(g_menu);
    g_menu = menu;
}

}  // namespace

void Start(std::function<GtkWidget*()> menu) {
    g_menuFor = std::move(menu);
    DrawIcon();
    if (LoadAppIndicator()) {
        g_indicator = g_api.create("bunbun", IconName().c_str(), kCategoryApplicationStatus);
        if (g_indicator) {
            g_api.setThemePath(g_indicator, g_iconDir.c_str());
            g_api.setIcon(g_indicator, IconName().c_str(), "Bunbun");
            if (g_api.setTitle) g_api.setTitle(g_indicator, "Bunbun");
            g_api.setStatus(g_indicator, kStatusActive);
            RebuildMenu();
            return;
        }
    }
    // No AppIndicator: the classic tray icon, where the panel has a system tray (X11).
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    g_status = gtk_status_icon_new_from_file(g_iconPath.c_str());
    gtk_status_icon_set_tooltip_text(g_status, "Bunbun");
    gtk_status_icon_set_title(g_status, "Bunbun");
    const auto popup = +[](GtkStatusIcon*, gpointer) {
        RebuildMenu();
        if (g_menu) gtk_menu_popup_at_pointer(GTK_MENU(g_menu), nullptr);
    };
    g_signal_connect(g_status, "activate", G_CALLBACK(popup), nullptr);
    g_signal_connect(g_status, "popup-menu", G_CALLBACK(+[](GtkStatusIcon* s, guint, guint, gpointer d) {
        reinterpret_cast<void (*)(GtkStatusIcon*, gpointer)>(d)(s, nullptr);
    }), reinterpret_cast<gpointer>(popup));
    G_GNUC_END_IGNORE_DEPRECATIONS
}

void Refresh() {
    if (!g_indicator && !g_status) return;
    if (DrawIcon()) {
        if (g_indicator) {
            g_api.setThemePath(g_indicator, g_iconDir.c_str());
            g_api.setIcon(g_indicator, IconName().c_str(), "Bunbun");
        }
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        if (g_status) gtk_status_icon_set_from_file(g_status, g_iconPath.c_str());
        G_GNUC_END_IGNORE_DEPRECATIONS
    }
    if (g_indicator) RebuildMenu();
}

bool Shown() {
    if (g_indicator) return true;
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    return g_status && gtk_status_icon_is_embedded(g_status);
    G_GNUC_END_IGNORE_DEPRECATIONS
}

}  // namespace bb::ltray
