// Screen Time on Linux (see screentime_linux.h).
#include "screentime_linux.h"

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
#include <unistd.h>

#include <algorithm>
#include <ctime>
#include <map>
#include <string>

#include "features_linux.h"
#include "platform.h"
#include "settings.h"

// X11 last: its headers define macros (None, Status...) that clash with ours.
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace bb::lscreen {
namespace {

namespace st = bb::screentime;

bool g_on = true;
int g_limit = 0;
st::Log g_log;
bool g_loaded = false;
double g_lastTick = 0, g_carry = 0, g_savedAt = 0;

struct Resolved {
    std::wstring name;
    GdkPixbuf* icon = nullptr;
};
std::map<std::wstring, Resolved> g_apps;  // by key (the program's path)

double Mono() { return g_get_monotonic_time() / 1e6; }

int Today() {
    const time_t t = time(nullptr);
    const struct ::tm* lt = localtime(&t);
    return (lt->tm_year + 1900) * 10000 + (lt->tm_mon + 1) * 100 + lt->tm_mday;
}

std::string FilePath() { return bb::plat::StateDir() + "/screentime.tsv"; }

void Load() {
    if (g_loaded) return;
    g_loaded = true;
    std::string text;
    if (bb::plat::ReadFile(FilePath(), &text)) g_log = st::Parse(bb::plat::Wide(text));
    st::Prune(&g_log, Today());
}

// The app in front: its program (from the window's _NET_WM_PID) and WM_CLASS.
bool FrontApp(std::string* exe, std::string* wmClass) {
    GdkDisplay* gd = gdk_display_get_default();
    if (!gd || !GDK_IS_X11_DISPLAY(gd)) return false;
    Display* d = GDK_DISPLAY_XDISPLAY(gd);
    const Atom activeAtom = XInternAtom(d, "_NET_ACTIVE_WINDOW", True), pidAtom = XInternAtom(d, "_NET_WM_PID", True);
    if (!activeAtom || !pidAtom) return false;
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    Window active = 0;
    long pid = 0;
    gdk_x11_display_error_trap_push(gd);
    if (XGetWindowProperty(d, DefaultRootWindow(d), activeAtom, 0, 1, False, XA_WINDOW, &type, &format, &n, &after, &data) == Success && data) {
        if (n) active = *reinterpret_cast<Window*>(data);
        XFree(data);
        data = nullptr;
    }
    if (active && XGetWindowProperty(d, active, pidAtom, 0, 1, False, XA_CARDINAL, &type, &format, &n, &after, &data) == Success && data) {
        if (n) pid = static_cast<long>(*reinterpret_cast<unsigned long*>(data));
        XFree(data);
    }
    XClassHint hint{};
    if (active && XGetClassHint(d, active, &hint)) {
        if (hint.res_class) { *wmClass = hint.res_class; XFree(hint.res_class); }
        if (hint.res_name) XFree(hint.res_name);
    }
    gdk_x11_display_error_trap_pop_ignored(gd);
    if (pid <= 0 || pid == getpid()) return false;  // nothing in front, or the island itself
    char path[4096];
    const std::string link = "/proc/" + std::to_string(pid) + "/exe";
    const ssize_t len = readlink(link.c_str(), path, sizeof path - 1);
    if (len <= 0) return false;
    *exe = std::string(path, static_cast<size_t>(len));
    return true;
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string Base(const std::string& p) { return p.substr(p.find_last_of('/') + 1); }

// The installed app for this program: its .desktop entry's name and icon, matched
// by WM_CLASS (StartupWMClass or the entry's id) or by the program it runs.
void Resolve(const std::wstring& key, const std::string& exe, const std::string& wmClass) {
    Resolved r;
    GDesktopAppInfo* best = nullptr;
    const std::string cls = Lower(wmClass), bin = Lower(Base(exe));
    GList* all = g_app_info_get_all();
    for (GList* l = all; l && !best; l = l->next) {
        GDesktopAppInfo* info = G_DESKTOP_APP_INFO(l->data);
        const char* wm = g_desktop_app_info_get_startup_wm_class(info);
        const char* id = g_app_info_get_id(G_APP_INFO(info));
        std::string idBase = id ? Lower(id) : "";
        if (idBase.size() > 8 && idBase.compare(idBase.size() - 8, 8, ".desktop") == 0) idBase.resize(idBase.size() - 8);
        if ((wm && !cls.empty() && Lower(wm) == cls) || (!cls.empty() && idBase == cls) ||
            (!cls.empty() && idBase.size() > cls.size() && idBase.compare(idBase.size() - cls.size(), cls.size(), cls) == 0 &&
             idBase[idBase.size() - cls.size() - 1] == '.'))
            best = info;
    }
    for (GList* l = all; l && !best; l = l->next) {
        GDesktopAppInfo* info = G_DESKTOP_APP_INFO(l->data);
        const char* run = g_app_info_get_executable(G_APP_INFO(info));
        if (run && Lower(Base(run)) == bin) best = info;
    }
    if (best) {
        r.name = bb::plat::Wide(g_app_info_get_display_name(G_APP_INFO(best)));
        if (GIcon* icon = g_app_info_get_icon(G_APP_INFO(best))) {
            if (GtkIconInfo* ii = gtk_icon_theme_lookup_by_gicon(gtk_icon_theme_get_default(), icon, 64, GTK_ICON_LOOKUP_FORCE_SIZE)) {
                r.icon = gtk_icon_info_load_icon(ii, nullptr);
                g_object_unref(ii);
            }
        }
    }
    g_list_free_full(all, g_object_unref);
    if (r.name.empty()) {  // no installed entry: the window's class, else the program's name
        std::string n = wmClass.empty() ? Base(exe) : wmClass;
        if (!n.empty()) n[0] = static_cast<char>(toupper(static_cast<unsigned char>(n[0])));
        r.name = bb::plat::Wide(n);
    }
    g_apps[key] = r;
}

}  // namespace

void LoadSettings() {
    namespace s = bb::settings;
    g_on = s::GetBool(L"Bunbun.ScreenTime");
    g_limit = std::clamp(s::GetInt(L"Bunbun.ScreenTimeLimit"), 0, 24);
}

bool On() { return g_on; }
int LimitHours() { return g_limit; }
bool Supported() {
    GdkDisplay* gd = gdk_display_get_default();
    return gd && GDK_IS_X11_DISPLAY(gd);
}

void Tick() {
    const double now = Mono();
    double dt = g_lastTick > 0 ? now - g_lastTick : 0;
    g_lastTick = now;
    if (!g_on || !Supported() || dt <= 0 || dt > 10) return;  // a gap (sleep, a stall) isn't screen time
    // Away (no input for two minutes) doesn't count, unless the app is full screen (a film).
    const double idle = bb::lfeat::IdleSeconds();
    if (idle > 120 && !bb::lfeat::FullscreenAppActive()) return;
    std::string exe, cls;
    if (!FrontApp(&exe, &cls)) return;
    Load();
    const std::wstring key = bb::plat::Wide(Lower(exe));
    if (!g_apps.count(key)) Resolve(key, exe, cls);
    g_carry += dt;
    const long long whole = static_cast<long long>(g_carry);
    if (whole <= 0) return;
    g_carry -= whole;
    st::Add(&g_log, Today(), key, g_apps[key].name, bb::plat::Wide(exe), whole);
    if (now - g_savedAt > 60) Save();
}

View Get() {
    Load();
    View v;
    v.summary = st::Summarize(g_log, Today());
    for (const st::AppTime& a : v.summary.apps) {
        // Apps from earlier sessions: look them up from the program path saved with them.
        if (!g_apps.count(a.key) && !a.path.empty()) Resolve(a.key, bb::plat::Utf8(a.path), "");
        auto it = g_apps.find(a.key);
        v.icons.push_back(it != g_apps.end() ? it->second.icon : nullptr);
    }
    return v;
}

void Save() {
    if (!g_loaded) return;
    g_savedAt = Mono();
    st::Prune(&g_log, Today());
    bb::plat::MakeDirs(bb::plat::StateDir());
    bb::plat::WriteFileAtomic(FilePath(), bb::plat::Utf8(st::Serialize(g_log)));
}

}  // namespace bb::lscreen
