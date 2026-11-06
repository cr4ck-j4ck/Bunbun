// Bunbun — Linux counterparts of the Windows island's placement, hiding, motion,
// music-pill, alert, date, idle-eyes and lyric settings (see features_linux.h).
#include "features_linux.h"

#include <gio/gio.h>
#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_X11
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
// X11's None would shadow AgentPhase::None; the X11 code below uses 0 instead.
#undef None
#endif

#include <dirent.h>
#include <signal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>
#include <ctime>
#include <map>

#include "../../common/applist.h"
#include "look_linux.h"
#include "mpris.h"
#include "platform.h"
#include "settings.h"
#include "sources.h"

namespace bb::lfeat {
namespace {

Feat g_feat;
bool g_lyricsVisualizer = true;
std::function<void(const std::string&, const std::string&, double, double)> g_toast;
std::function<void()> g_changed;

double Mono() { return g_get_monotonic_time() / 1e6; }
void Toast(const std::string& title, const std::string& text, double seconds = 3.0, double level = -1) {
    if (g_toast) g_toast(title, text, seconds, level);
}
std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string Trim(std::string s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
    return s;
}

// The Windows island's spring per style (stiffness and damping multipliers) and
// its speed steps, as in settings-ui/src/pages/Animations.tsx.
void SpringFor(const std::wstring& style, const std::wstring& speed, double* k, double* d) {
    double km = 1.0, dm = 1.0;
    if (style == L"smooth") dm = 1.35;
    else if (style == L"bouncy") { km = 1.1; dm = 0.7; }
    else if (style == L"snappy") { km = 1.5; dm = 1.25; }
    double sp = 1.0;
    if (speed == L"very-slow") sp = 0.5;
    else if (speed == L"slow") sp = 0.75;
    else if (speed == L"fast") sp = 1.35;
    else if (speed == L"very-fast") sp = 1.65;
    else if (speed == L"ultra-fast") sp = 2.0;
    // Base: about the old 0.2 s ease, critically damped at the default style.
    const double k0 = 260.0 * km * sp * sp;
    *k = k0;
    *d = 2.0 * std::sqrt(k0) * dm;
}

}  // namespace

bool g_separateDisplays = false;  // Appearance.DisplayLink = separate, with the island on all displays

void LoadSettings() {
    namespace s = bb::settings;
    Feat f;
    const std::wstring pos = s::Get(L"Appearance.Position");
    g_separateDisplays = s::Get(L"Appearance.Displays") == L"all" && s::Get(L"Appearance.DisplayLink") == L"separate";
    f.bottom = pos.rfind(L"bottom", 0) == 0;
    f.align = pos.find(L"left") != std::wstring::npos ? 0 : pos.find(L"right") != std::wstring::npos ? 2 : 1;
    f.offsetX = std::clamp(s::GetInt(L"Appearance.OffsetX"), -2000, 2000);
    f.separateExpandedY = s::GetBool(L"Appearance.SeparateExpandedOffsetY");
    f.offsetYExpanded = std::clamp(s::GetInt(L"Appearance.OffsetYExpanded"), -200, 400);
    f.merged = s::GetBool(L"Appearance.BorderMergedMode");
    f.monitor = s::Get(L"Appearance.TargetMonitor");
    f.onTop = s::GetBool(L"Behavior.AlwaysOnTop");

    f.autoHideIdle = std::max(0, s::GetInt(L"Behavior.AutoHideIdleSeconds"));
    {
        // Behavior.FullscreenMode (tuck | show | hide) replaced AutoHideFullscreen and
        // FullscreenTuck; until it's set, an old "don't hide" is Show and an old "tuck" Tuck.
        std::wstring mode = s::Get(L"Behavior.FullscreenMode");
        if (!s::IsSet(L"Behavior.FullscreenMode")) {
            if (s::IsSet(L"Behavior.FullscreenTuck") && s::Get(L"Behavior.FullscreenTuck") == L"true") mode = L"tuck";
            else if (s::IsSet(L"Behavior.AutoHideFullscreen") && s::Get(L"Behavior.AutoHideFullscreen") == L"false") mode = L"show";
        }
        f.hideFullscreen = mode == L"hide";
        f.tuckFullscreen = mode != L"hide" && mode != L"show";  // tuck, the default
    }
    f.unhideOnHover = s::GetBool(L"Behavior.UnhideOnHover");
    f.hideHotkey = s::GetBool(L"Shortcuts.HideShowHotkeyEnabled");
    f.hideMods = s::Get(L"Shortcuts.HideShowModifiers");
    f.hideKey = s::Get(L"Shortcuts.HideShowKey");
    f.tuckHotkey = s::GetBool(L"Bunbun.TuckHotkey");
    f.tuckRevealMs = std::clamp(s::GetInt(L"Bunbun.TuckRevealMs"), 0, 5000);
    f.fsShowApps = s::Get(L"Behavior.FullscreenShowApps");
    f.fsTuckApps = s::Get(L"Behavior.FullscreenTuckApps");
    f.fsHideApps = s::Get(L"Behavior.FullscreenHideApps");
    f.dndFollowSystem = s::GetBool(L"Bunbun.DndFollowSystem");
    f.afkMinutes = std::clamp(s::GetInt(L"Bunbun.AfkMinutes"), 0, 240);
    f.sleepMinutes = std::clamp(s::GetInt(L"Bunbun.SleepMinutes"), 0, 480);

    SpringFor(s::Get(L"Animations.AnimationStyle"), s::Get(L"Animations.AnimationSpeed"), &f.stiffness, &f.damping);
    const std::wstring fps = s::Get(L"Animations.TargetFPS");
    f.fps = fps == L"auto" ? 60 : std::clamp(static_cast<int>(wcstol(fps.c_str(), nullptr, 10)), 30, 500);
    f.ambientFps = std::clamp(s::GetInt(L"Bunbun.AmbientFps"), 15, 120);
    const std::wstring saver = s::Get(L"Animations.PowerSaver");
    f.powerSaver = saver == L"always" || (saver == L"auto" && lsrc::BatteryState().has && !lsrc::BatteryState().charging);
    f.coverSpin = s::GetBool(L"Animations.PillCoverSpin");
    f.mediaTransitions = s::GetBool(L"Animations.ExpandedMediaTransitions");
    const std::wstring spec = s::Get(L"Appearance.SpectrumStyle");
    f.spectrum = spec == L"led" ? 1 : spec == L"plasma" ? 2 : spec == L"orb" ? 3 : 0;
    const std::wstring prog = s::Get(L"Appearance.ProgressStyle");
    f.progress = prog == L"wavy" ? 1 : prog == L"squiggle" ? 2 : prog == L"bar" ? 3 : 0;

    f.collapsedTitle = s::GetBool(L"Modules.CollapsedTitle");
    f.pillClock = s::GetBool(L"Modules.MediaPillClock");
    f.autoExpand = s::GetBool(L"Modules.MediaAutoExpand");
    f.expandBlocklist = s::Get(L"Modules.MediaExpandBlocklist");
    f.musicSwipe = s::GetBool(L"Bunbun.MusicSwipe");
    f.musicApp = s::Get(L"Bunbun.MusicApp");

    f.battery = s::GetBool(L"Modules.Battery");
    f.batteryMoods = s::GetBool(L"Bunbun.BatteryMoods");
    f.chargingEvery = std::max(0, s::GetInt(L"Bunbun.ChargingReminderMinutes"));
    f.capsLock = s::GetBool(L"Modules.CapsLock");
    f.brightness = s::GetBool(L"Modules.Brightness");
    f.volume = s::GetBool(L"Modules.Volume");
    f.bluetooth = s::GetBool(L"Modules.BluetoothIndicator");
    f.bluetoothBattery = s::GetBool(L"Modules.BluetoothShowBattery");
    f.bluetoothLow = s::GetBool(L"Bunbun.BluetoothLowWarning");
    f.dnd = s::GetBool(L"Modules.DoNotDisturbIndicator");
    f.clipboard = s::GetBool(L"Modules.Clipboard");
    f.countdownBar = s::GetBool(L"Modules.StatusCountdownProgress");
    f.timer = s::GetBool(L"Modules.TimerModule");
    f.reminderSound = s::GetBool(L"Modules.ReminderSound");
    f.quietReminders = s::GetBool(L"Modules.QuietReminders");
    f.focusGuard = s::GetBool(L"Bunbun.FocusGuard");
    f.focusGuardApps = s::Get(L"Bunbun.FocusGuardApps");

    f.dateFormat = s::Get(L"Modules.DateFormat");
    const std::wstring first = s::Get(L"Modules.FirstDayOfWeek");
    if (first == L"sunday") f.mondayFirst = false;
    else if (first == L"monday") f.mondayFirst = true;
    else {
        // auto: the locale's first weekday (glibc: 1 = Sunday, 2 = Monday).
        const char* lang = getenv("LC_TIME");
        if (!lang || !*lang) lang = getenv("LANG");
        const std::string l = lang ? lang : "";
        f.mondayFirst = !(l.rfind("en_US", 0) == 0 || l.rfind("en_CA", 0) == 0 || l.rfind("ja", 0) == 0 ||
                          l.rfind("ko", 0) == 0 || l.rfind("zh_TW", 0) == 0 || l.rfind("he", 0) == 0 ||
                          l.rfind("pt_BR", 0) == 0 || l.rfind("hi", 0) == 0);
    }

    f.eyes = s::Get(L"Bunbun.IdleFace") == L"eyes";
    const std::wstring eye = s::Get(L"Bunbun.EyeColor");
    const wchar_t* names[] = {L"mint", L"white", L"blue", L"pink", L"purple", L"yellow"};
    for (int i = 0; i < 6; ++i)
        if (eye == names[i]) f.eyeColor = i;
    f.eyeStatus = s::GetBool(L"Bunbun.EyeStatusColors");
    const std::wstring strip = s::Get(L"Bunbun.IdleStripItems");
    f.strip = strip == L"time" ? 1 : strip == L"weather" ? 2 : strip == L"nothing" ? 3 : strip == L"name" ? 4
            : strip == L"name-weather" ? 5 : 0;
    f.name.clear();
    if (f.strip >= 4) {
        const std::wstring name = s::Get(L"Bunbun.IdleName");
        f.name = bb::plat::Utf8(name.empty() ? std::wstring(L"Bunbun") : name);
    }
    f.mascotDnd = s::GetBool(L"Bunbun.MascotDnd");
    g_lyricsVisualizer = s::GetBool(L"Modules.LyricsVisualizer");

    f.karaoke = s::GetBool(L"Modules.KaraokeLyrics");
    f.karaokeGlow = s::GetBool(L"Modules.KaraokeLetterGlow");
    f.neighbours = s::GetBool(L"Modules.LyricsNeighborLinesVisible");
    f.btOffsetMs = std::clamp(s::GetInt(L"Modules.LyricsOffsetBluetoothMs"), -3000, 3000);
    g_feat = f;
}

const Feat& Get() { return g_feat; }

// ── Placement ───────────────────────────────────────────────────────────────

bool SeparateDisplays() { return g_separateDisplays; }
void SetPlacement(int align, bool bottom) { g_feat.align = align; g_feat.bottom = bottom; }

GdkMonitor* TargetMonitor(GdkDisplay* display) {
    const std::wstring& m = g_separateDisplays ? std::wstring(L"follow") : g_feat.monitor;
    if (m == L"follow") {  // the monitor the pointer is on
        if (GdkSeat* seat = gdk_display_get_default_seat(display)) {
            int x = 0, y = 0;
            gdk_device_get_position(gdk_seat_get_pointer(seat), nullptr, &x, &y);
            if (GdkMonitor* mon = gdk_display_get_monitor_at_point(display, x, y)) return mon;
        }
    } else if (m != L"primary") {
        const int n = static_cast<int>(wcstol(m.c_str(), nullptr, 10));
        if (n >= 1 && n <= gdk_display_get_n_monitors(display)) return gdk_display_get_monitor(display, n - 1);
    }
    GdkMonitor* mon = gdk_display_get_primary_monitor(display);
    return mon ? mon : gdk_display_get_monitor(display, 0);
}

// ── Hiding ──────────────────────────────────────────────────────────────────

namespace {
double g_lastInteraction = 0;
bool g_tucked = false, g_hotkeyHidden = false;
bool g_fullscreen = false;

#ifdef GDK_WINDOWING_X11
// The active window's _NET_WM_STATE says whether it's full screen.
bool X11FullscreenNow() {
    GdkDisplay* gd = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gd)) return false;
    Display* d = GDK_DISPLAY_XDISPLAY(gd);
    const Window root = DefaultRootWindow(d);
    const Atom activeAtom = XInternAtom(d, "_NET_ACTIVE_WINDOW", True), stateAtom = XInternAtom(d, "_NET_WM_STATE", True),
               fullAtom = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", True);
    if (activeAtom == 0 || stateAtom == 0 || fullAtom == 0) return false;
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    Window active = 0;
    gdk_x11_display_error_trap_push(gd);
    if (XGetWindowProperty(d, root, activeAtom, 0, 1, False, XA_WINDOW, &type, &format, &n, &after, &data) == Success && data) {
        if (n) active = *reinterpret_cast<Window*>(data);
        XFree(data);
        data = nullptr;
    }
    bool full = false;
    if (active && XGetWindowProperty(d, active, stateAtom, 0, 64, False, XA_ATOM, &type, &format, &n, &after, &data) == Success && data) {
        const Atom* atoms = reinterpret_cast<Atom*>(data);
        for (unsigned long i = 0; i < n; ++i) full = full || atoms[i] == fullAtom;
        XFree(data);
    }
    gdk_x11_display_error_trap_pop_ignored(gd);
    return full;
}

// The active window's title and class ("Firefox", "discord"), lower case.
std::string X11ActiveWindowText() {
    GdkDisplay* gd = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gd)) return {};
    Display* d = GDK_DISPLAY_XDISPLAY(gd);
    const Atom activeAtom = XInternAtom(d, "_NET_ACTIVE_WINDOW", True), nameAtom = XInternAtom(d, "_NET_WM_NAME", True),
               utf8 = XInternAtom(d, "UTF8_STRING", True);
    if (activeAtom == 0) return {};
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    Window active = 0;
    std::string text;
    gdk_x11_display_error_trap_push(gd);
    if (XGetWindowProperty(d, DefaultRootWindow(d), activeAtom, 0, 1, False, XA_WINDOW, &type, &format, &n, &after, &data) == Success &&
        data) {
        if (n) active = *reinterpret_cast<Window*>(data);
        XFree(data);
        data = nullptr;
    }
    if (active && nameAtom && utf8 &&
        XGetWindowProperty(d, active, nameAtom, 0, 256, False, utf8, &type, &format, &n, &after, &data) == Success && data) {
        text.assign(reinterpret_cast<char*>(data), n);
        XFree(data);
        data = nullptr;
    }
    XClassHint hint{};
    if (active && XGetClassHint(d, active, &hint)) {
        if (hint.res_name) { text += "\n" + std::string(hint.res_name); XFree(hint.res_name); }
        if (hint.res_class) { text += "\n" + std::string(hint.res_class); XFree(hint.res_class); }
    }
    gdk_x11_display_error_trap_pop_ignored(gd);
    return Lower(text);
}

// Global hotkeys (X11 only: Wayland has no way for an app to grab keys).
struct Grab { KeyCode code = 0; unsigned mods = 0; void (*action)() = nullptr; };
std::vector<Grab> g_grabs;
// A grab per Caps Lock / Num Lock state, or the key wouldn't fire with either on.
constexpr unsigned kLockVariants[] = {0u, static_cast<unsigned>(LockMask), static_cast<unsigned>(Mod2Mask),
                                      static_cast<unsigned>(LockMask | Mod2Mask)};

unsigned ModsFor(const std::wstring& m) {
    if (m == L"ctrl_shift") return ControlMask | ShiftMask;
    if (m == L"alt_shift") return Mod1Mask | ShiftMask;
    if (m == L"win_alt") return Mod4Mask | Mod1Mask;
    if (m == L"ctrl_alt_shift") return ControlMask | Mod1Mask | ShiftMask;
    return ControlMask | Mod1Mask;
}

void Ungrab(Display* d, Window root) {
    for (const Grab& g : g_grabs)
        for (unsigned extra : kLockVariants) XUngrabKey(d, g.code, g.mods | extra, root);
    g_grabs.clear();
}

void GrabKey(Display* d, Window root, wchar_t key, unsigned mods, void (*action)()) {
    char name[2] = {static_cast<char>(std::toupper(static_cast<int>(key))), 0};
    const KeySym sym = XStringToKeysym(name);
    const KeyCode code = sym ? XKeysymToKeycode(d, sym) : 0;
    if (!code) return;
    for (unsigned extra : kLockVariants) XGrabKey(d, code, mods | extra, root, True, GrabModeAsync, GrabModeAsync);
    g_grabs.push_back({code, mods, action});
}

GdkFilterReturn OnXEvent(GdkXEvent* xev, GdkEvent*, gpointer) {
    const XEvent* e = static_cast<XEvent*>(xev);
    if (e->type != KeyPress) return GDK_FILTER_CONTINUE;
    const unsigned mods = e->xkey.state & (ControlMask | Mod1Mask | ShiftMask | Mod4Mask);
    for (const Grab& g : g_grabs)
        if (g.code == e->xkey.keycode && g.mods == mods && g.action) {
            g.action();
            return GDK_FILTER_REMOVE;
        }
    return GDK_FILTER_CONTINUE;
}

// skipGrabs: GNOME runs the shortcuts instead (UpdateShortcuts); grabbing too would
// toggle twice whenever an X11 window has the focus.
void UpdateHotkeys(bool skipGrabs) {
    GdkDisplay* gd = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gd)) return;
    Display* d = GDK_DISPLAY_XDISPLAY(gd);
    const Window root = DefaultRootWindow(d);
    gdk_x11_display_error_trap_push(gd);
    Ungrab(d, root);
    if (skipGrabs) {
        gdk_x11_display_error_trap_pop_ignored(gd);
        return;
    }
    if (g_feat.hideHotkey) {
        const std::wstring k = g_feat.hideKey.empty() ? L"D" : g_feat.hideKey;
        GrabKey(d, root, k[0], ModsFor(g_feat.hideMods), [] { ToggleHidden(); });
    }
    if (g_feat.tuckHotkey) GrabKey(d, root, L'H', ControlMask | Mod1Mask, [] { SetTucked(!Tucked()); });
    gdk_x11_display_error_trap_pop_ignored(gd);
    static bool filtered = false;
    if (!filtered) {
        filtered = true;
        gdk_window_add_filter(nullptr, OnXEvent, nullptr);
        XSelectInput(d, root, KeyPressMask);
    }
}
#endif

// GNOME on Wayland: Bunbun runs through XWayland there, and an X11 key grab never
// sees keys pressed in Wayland apps. GNOME's own custom shortcuts can run a command
// though, so the hotkeys become two of those, running bunbun --toggle and --tuck.
bool GnomeWayland() {
    const char* session = getenv("XDG_SESSION_TYPE");
    const char* desktop = getenv("XDG_CURRENT_DESKTOP");
    return session && strcmp(session, "wayland") == 0 && desktop && (strstr(desktop, "GNOME") || strstr(desktop, "gnome"));
}

constexpr const char* kKeysSchema = "org.gnome.settings-daemon.plugins.media-keys";
constexpr const char* kKeySchema = "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding";
constexpr const char* kKeyBase = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/";

// "<Control><Alt>d" for Shortcuts.HideShowModifiers / HideShowKey.
std::string GnomeAccel(const std::wstring& mods, wchar_t key) {
    std::string out;
    if (mods == L"ctrl_shift") out = "<Control><Shift>";
    else if (mods == L"alt_shift") out = "<Alt><Shift>";
    else if (mods == L"win_alt") out = "<Super><Alt>";
    else if (mods == L"ctrl_alt_shift") out = "<Control><Alt><Shift>";
    else out = "<Control><Alt>";
    return out + static_cast<char>(std::tolower(static_cast<int>(key)));
}

// Adds or updates our custom shortcut `id`, or removes it when accel is empty.
// Other custom shortcuts are left alone.
void SetGnomeShortcut(const char* id, const char* name, const std::string& accel, const std::string& command) {
    GSettingsSchemaSource* src = g_settings_schema_source_get_default();
    GSettingsSchema* listSchema = src ? g_settings_schema_source_lookup(src, kKeysSchema, TRUE) : nullptr;
    GSettingsSchema* keySchema = src ? g_settings_schema_source_lookup(src, kKeySchema, TRUE) : nullptr;
    if (!listSchema || !keySchema) {  // not GNOME's settings daemon
        if (listSchema) g_settings_schema_unref(listSchema);
        if (keySchema) g_settings_schema_unref(keySchema);
        return;
    }
    const std::string path = std::string(kKeyBase) + id + "/";
    GSettings* list = g_settings_new_full(listSchema, nullptr, nullptr);
    GSettings* key = g_settings_new_full(keySchema, nullptr, path.c_str());
    gchar** paths = g_settings_get_strv(list, "custom-keybindings");
    std::vector<const char*> keep;
    bool listed = false;
    for (gchar** p = paths; *p; ++p) {
        if (path == *p) listed = true;
        else keep.push_back(*p);
    }
    if (!accel.empty()) {
        g_settings_set_string(key, "name", name);
        g_settings_set_string(key, "command", command.c_str());
        g_settings_set_string(key, "binding", accel.c_str());
        keep.push_back(path.c_str());
    } else {
        g_settings_reset(key, "name");
        g_settings_reset(key, "command");
        g_settings_reset(key, "binding");
    }
    if (listed != !accel.empty()) {
        keep.push_back(nullptr);
        g_settings_set_strv(list, "custom-keybindings", keep.data());
    }
    g_strfreev(paths);
    g_settings_sync();
    g_object_unref(key);
    g_object_unref(list);
    g_settings_schema_unref(keySchema);
    g_settings_schema_unref(listSchema);
}

// The hotkeys (Shortcuts.HideShowHotkeyEnabled, Bunbun.TuckHotkey): GNOME shortcuts
// on GNOME Wayland, else X11 key grabs. Other Wayland desktops can bind
// bunbun --toggle / --tuck in their keyboard settings (docs/linux.md).
void UpdateShortcuts() {
    const bool gnome = GnomeWayland();
    if (gnome) {
        gchar* quoted = g_shell_quote(bb::plat::IslandCommand().c_str());
        const std::string exe = quoted;
        g_free(quoted);
        const std::wstring k = g_feat.hideKey.empty() ? L"D" : g_feat.hideKey;
        const std::string toggle = g_feat.hideHotkey ? GnomeAccel(g_feat.hideMods, k[0]) : std::string();
        const std::string tuck = g_feat.tuckHotkey ? "<Control><Alt>h" : std::string();
        // Written only when something changed: every settings save lands here.
        static std::string s_applied;
        const std::string want = exe + "|" + toggle + "|" + tuck;
        if (want != s_applied) {
            s_applied = want;
            SetGnomeShortcut("bunbun-toggle", "Bunbun: hide or show the island", toggle, exe + " --toggle");
            SetGnomeShortcut("bunbun-tuck", "Bunbun: tuck the island away", tuck, exe + " --tuck");
        }
    }
#ifdef GDK_WINDOWING_X11
    UpdateHotkeys(gnome);
#endif
}
}  // namespace

bool FullscreenAppActive() { return g_fullscreen; }
bool g_fullscreenTuck = false;
bool FullscreenTuckNow() { return g_fullscreen && g_fullscreenTuck; }

// ── Idle time (AFK) ─────────────────────────────────────────────────────────
// Asked every 5 s on a worker thread: GNOME's Mutter IdleMonitor (milliseconds),
// else the freedesktop ScreenSaver's GetSessionIdleTime (KDE and others, seconds).
std::atomic<double> g_idleSeconds{-1.0};
double IdleSeconds() { return g_idleSeconds.load(); }

void StartIdleWatch() {
    std::thread([] {
        GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
        if (!bus) return;
        for (;;) {
            double secs = -1.0;
            if (GVariant* r = g_dbus_connection_call_sync(bus, "org.gnome.Mutter.IdleMonitor", "/org/gnome/Mutter/IdleMonitor/Core",
                                                          "org.gnome.Mutter.IdleMonitor", "GetIdletime", nullptr,
                                                          G_VARIANT_TYPE("(t)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr)) {
                guint64 ms = 0;
                g_variant_get(r, "(t)", &ms);
                secs = ms / 1000.0;
                g_variant_unref(r);
            } else if (GVariant* r2 = g_dbus_connection_call_sync(bus, "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                                                                  "org.freedesktop.ScreenSaver", "GetSessionIdleTime", nullptr,
                                                                  G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr)) {
                guint32 s = 0;
                g_variant_get(r2, "(u)", &s);
                secs = s;
                g_variant_unref(r2);
            }
            g_idleSeconds.store(secs);
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();
}
void NoteInteraction() { g_lastInteraction = Mono(); }
bool AutoHidden() { return g_feat.autoHideIdle > 0 && Mono() - g_lastInteraction > g_feat.autoHideIdle; }
bool Tucked() { return g_tucked; }
void SetTucked(bool tucked) {
    g_tucked = tucked;
    if (g_changed) g_changed();
}
bool HiddenByHotkey() { return g_hotkeyHidden; }
void ToggleHidden() {
    g_hotkeyHidden = !g_hotkeyHidden;
    if (g_changed) g_changed();
}

// ── Motion ──────────────────────────────────────────────────────────────────

void Spring(double* x, double* v, double target, double dt) {
    // Semi-implicit Euler in small steps, so a long frame can't blow it up.
    const double k = g_feat.stiffness, d = g_feat.damping;
    for (double left = dt; left > 0; left -= 1.0 / 240) {
        const double h = std::min(left, 1.0 / 240);
        *v += (-k * (*x - target) - d * *v) * h;
        *x += *v * h;
    }
    if (std::fabs(*x - target) < 0.3 && std::fabs(*v) < 2) {
        *x = target;
        *v = 0;
    }
}

int FrameMs(bool interacting) {
    int fps = interacting ? g_feat.fps : std::min(g_feat.fps, g_feat.ambientFps);
    if (g_feat.powerSaver) fps = std::min(fps, interacting ? 30 : 15);
    return std::max(2, 1000 / std::max(1, fps));
}

// ── The music pill ──────────────────────────────────────────────────────────

bool ExpandBlocked(const std::string& player, const std::string& title) {
    const std::string hay = Lower(player + "\n" + title);
    const std::string list = Lower(bb::plat::Utf8(g_feat.expandBlocklist));
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(',', start);
        const std::string word = Trim(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!word.empty() && hay.find(word) != std::string::npos) return true;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return false;
}

void OpenMusicApp() {
    const std::wstring& app = g_feat.musicApp;
    const bb::mpris::Track t = bb::mpris::Current();
    if (app == L"auto" || (app == L"spotify" && Lower(t.player).find("spotify") != std::string::npos)) {
        if (t.active) bb::mpris::Raise();
        else if (app == L"spotify") bb::plat::Spawn({"spotify"});
        return;
    }
    if (app == L"spotify") {
        if (!bb::plat::Spawn({"spotify"})) bb::plat::OpenWithDefault("https://open.spotify.com");
    } else if (app == L"youtube-music") {
        bb::plat::OpenWithDefault("https://music.youtube.com");
    } else if (app == L"apple-music") {
        bb::plat::OpenWithDefault("https://music.apple.com");
    }
}

// ── Audio levels ────────────────────────────────────────────────────────────
// parec reads the default output's monitor as 16-bit mono at 11 025 Hz. Every 256
// samples (about 23 ms), a Goertzel filter per band measures log-spaced
// frequencies from 60 Hz to 5 kHz; levels rise fast and fall slowly, like the
// Windows island's bars.

namespace {
GPid g_parec = 0;
GIOChannel* g_audioCh = nullptr;
guint g_audioWatch = 0;
std::vector<float> g_bands, g_noBands;
std::vector<int16_t> g_pcm;
std::string g_pcmBytes;
double g_audioWantedAt = -10;

void ProcessBlock(const int16_t* s, int n) {
    constexpr double kRate = 11025.0;
    if (g_bands.size() != static_cast<size_t>(kBands)) g_bands.assign(kBands, 0.0f);
    for (int b = 0; b < kBands; ++b) {
        const double f = 60.0 * std::pow(5000.0 / 60.0, b / double(kBands - 1));
        const double k = 2.0 * std::cos(2.0 * M_PI * f / kRate);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));  // Hann window
            const double s0 = s[i] / 32768.0 * w + k * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - k * s1 * s2;
        // Decibels mapped to 0..1, with a tilt so the treble shows as much as the bass.
        const double db = 10.0 * std::log10(power / (n * n) + 1e-12) + 6.0 * b / (kBands - 1);
        const float level = static_cast<float>(std::clamp((db + 62.0) / 50.0, 0.0, 1.0));
        float& cur = g_bands[b];
        cur = level > cur ? cur + (level - cur) * 0.7f : cur * 0.86f + level * 0.14f;
    }
}

gboolean OnAudio(GIOChannel* ch, GIOCondition cond, gpointer) {
    if (cond & (G_IO_HUP | G_IO_ERR)) {
        g_audioWatch = 0;
        return G_SOURCE_REMOVE;
    }
    char buf[4096];
    gsize got = 0;
    const GIOStatus st = g_io_channel_read_chars(ch, buf, sizeof buf, &got, nullptr);
    if (st == G_IO_STATUS_EOF) { g_audioWatch = 0; return G_SOURCE_REMOVE; }
    g_pcmBytes.append(buf, got);
    constexpr size_t kBlock = 256 * 2;
    while (g_pcmBytes.size() >= kBlock) {
        ProcessBlock(reinterpret_cast<const int16_t*>(g_pcmBytes.data()), 256);
        g_pcmBytes.erase(0, kBlock);
    }
    return G_SOURCE_CONTINUE;
}

void StopAudio() {
    if (g_audioWatch) g_source_remove(g_audioWatch);
    g_audioWatch = 0;
    if (g_audioCh) {
        g_io_channel_shutdown(g_audioCh, FALSE, nullptr);
        g_io_channel_unref(g_audioCh);
        g_audioCh = nullptr;
    }
    if (g_parec) {
        kill(g_parec, SIGTERM);
        g_spawn_close_pid(g_parec);
        g_parec = 0;
    }
    g_bands.clear();
    g_pcmBytes.clear();
}

void StartAudio() {
    static int have = -1;
    if (have < 0) {
        gchar* p = g_find_program_in_path("parec");
        have = p != nullptr;
        g_free(p);
    }
    if (!have || g_parec) return;
    gchar* argv[] = {const_cast<gchar*>("parec"), const_cast<gchar*>("--device=@DEFAULT_MONITOR@"),
                     const_cast<gchar*>("--format=s16le"), const_cast<gchar*>("--rate=11025"), const_cast<gchar*>("--channels=1"),
                     const_cast<gchar*>("--latency-msec=30"), const_cast<gchar*>("--raw"), nullptr};
    gint out = -1;
    if (!g_spawn_async_with_pipes(nullptr, argv, nullptr, static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                                                                                     G_SPAWN_STDERR_TO_DEV_NULL),
                                  nullptr, nullptr, &g_parec, nullptr, &out, nullptr, nullptr)) {
        g_parec = 0;
        return;
    }
    g_child_watch_add(g_parec, [](GPid pid, gint, gpointer) {
        if (pid == g_parec) {  // it died on its own (no audio server): give up until wanted again
            g_parec = 0;
            StopAudio();
        }
        g_spawn_close_pid(pid);
    }, nullptr);
    g_audioCh = g_io_channel_unix_new(out);
    g_io_channel_set_encoding(g_audioCh, nullptr, nullptr);
    g_io_channel_set_buffered(g_audioCh, FALSE);
    g_io_channel_set_flags(g_audioCh, G_IO_FLAG_NONBLOCK, nullptr);
    g_io_channel_set_close_on_unref(g_audioCh, TRUE);
    g_audioWatch = g_io_add_watch(g_audioCh, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), OnAudio, nullptr);
}
}  // namespace

void WantAudio(bool wanted) {
    if (wanted) {
        g_audioWantedAt = Mono();
        StartAudio();
    }
}

const std::vector<float>& AudioBands() { return g_parec ? g_bands : g_noBands; }
bool LyricsVisualizer() { return g_lyricsVisualizer; }

// ── Spectrum and progress ───────────────────────────────────────────────────

void Spectrum(cairo_t* cr, double x, double cy, double w, double h, const double* c, double alpha, bool playing, double now) {
    // The real levels when they're being captured, else a gentle animation.
    const std::vector<float>& bands = AudioBands();
    const auto level = [&](int i, int n) {
        if (!playing) return 0.2;
        if (!bands.empty()) return 0.12 + 0.88 * bands[std::min<size_t>(bands.size() - 1, static_cast<size_t>(i) * bands.size() / n)];
        return 0.3 + 0.7 * std::fabs(std::sin(now * (2.6 + i * 0.7) + i * 1.3 + n));
    };
    cairo_save(cr);
    switch (g_feat.spectrum) {
        case 1: {  // LED: columns of little lit squares
            const int cols = std::max(3, static_cast<int>(w / 5)), rows = 4;
            const double cw = w / cols, ch = h / rows;
            for (int i = 0; i < cols; ++i) {
                const int lit = static_cast<int>(std::ceil(level(i, cols) * rows));
                for (int r = 0; r < rows; ++r) {
                    cairo_set_source_rgba(cr, c[0], c[1], c[2], alpha * (r < lit ? 0.95 : 0.15));
                    cairo_rectangle(cr, x + i * cw + 0.5, cy + h / 2 - (r + 1) * ch + 0.5, cw - 1, ch - 1);
                    cairo_fill(cr);
                }
            }
            break;
        }
        case 2: {  // Plasma: a soft filled wave
            cairo_move_to(cr, x, cy + h / 2);
            for (double px = 0; px <= w; px += 1) {
                const double v = playing ? std::sin(px / w * 6.3 + now * 3.1) * 0.5 + std::sin(px / w * 11 - now * 2.3) * 0.3 : 0.05;
                cairo_line_to(cr, x + px, cy - v * h / 2);
            }
            cairo_line_to(cr, x + w, cy + h / 2);
            cairo_close_path(cr);
            cairo_pattern_t* g = cairo_pattern_create_linear(0, cy - h / 2, 0, cy + h / 2);
            cairo_pattern_add_color_stop_rgba(g, 0, c[0], c[1], c[2], alpha * 0.9);
            cairo_pattern_add_color_stop_rgba(g, 1, c[0], c[1], c[2], alpha * 0.1);
            cairo_set_source(cr, g);
            cairo_fill(cr);
            cairo_pattern_destroy(g);
            break;
        }
        case 3: {  // Orb: a glowing ball that breathes with the beat
            const double v = level(0, 1), r = std::min(w, h) / 2 * (0.55 + 0.45 * v);
            cairo_pattern_t* g = cairo_pattern_create_radial(x + w / 2, cy, 0, x + w / 2, cy, r);
            cairo_pattern_add_color_stop_rgba(g, 0, c[0], c[1], c[2], alpha);
            cairo_pattern_add_color_stop_rgba(g, 1, c[0], c[1], c[2], 0);
            cairo_set_source(cr, g);
            cairo_arc(cr, x + w / 2, cy, r, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_pattern_destroy(g);
            break;
        }
        default: {  // Bars
            const int n = std::max(3, static_cast<int>(w / 5));
            const double bw = w / n;
            cairo_set_source_rgba(cr, c[0], c[1], c[2], alpha);
            for (int i = 0; i < n; ++i) {
                const double bh = h * level(i, n), bx = x + i * bw;
                const double rr = std::min(1.5, (bw - 2) / 2);
                cairo_new_sub_path(cr);
                cairo_arc(cr, bx + bw - 2 - rr, cy - bh / 2 + rr, rr, -M_PI / 2, 0);
                cairo_arc(cr, bx + bw - 2 - rr, cy + bh / 2 - rr, rr, 0, M_PI / 2);
                cairo_arc(cr, bx + rr, cy + bh / 2 - rr, rr, M_PI / 2, M_PI);
                cairo_arc(cr, bx + rr, cy - bh / 2 + rr, rr, M_PI, 3 * M_PI / 2);
                cairo_close_path(cr);
            }
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);
}

void Progress(cairo_t* cr, double x, double cy, double w, double f, const double* c, bool playing, double now) {
    f = std::clamp(f, 0.0, 1.0);
    cairo_save(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const double thick = g_feat.progress == 3 ? 6.0 : 3.0;
    cairo_set_line_width(cr, thick);
    // The track.
    cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
    cairo_move_to(cr, x, cy);
    cairo_line_to(cr, x + w, cy);
    cairo_stroke(cr);
    // The played part: straight, or a wave / squiggle that flattens when paused.
    cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.95);
    const double amp = (g_feat.progress == 1 || g_feat.progress == 2) && playing ? (g_feat.progress == 2 ? 2.2 : 3.0) : 0.0;
    const double freq = g_feat.progress == 2 ? 0.55 : 0.22;
    cairo_move_to(cr, x, cy);
    for (double px = 1; px <= w * f; px += 1) cairo_line_to(cr, x + px, cy + std::sin(px * freq - now * 6) * amp);
    cairo_stroke(cr);
    cairo_arc(cr, x + w * f, cy, thick + 1.5, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_restore(cr);
}

// ── Battery ─────────────────────────────────────────────────────────────────

namespace {
double g_cheerAt = -10;
bool g_wasCharging = false, g_batteryKnown = false;
int g_lowShown = 101;  // the lowest threshold already announced this discharge
double g_chargeToastAt = 0;

// Minutes to full, from the battery's energy (or charge) and its power (or current).
int MinutesToFull() {
    DIR* d = opendir("/sys/class/power_supply");
    if (!d) return -1;
    int minutes = -1;
    while (dirent* e = readdir(d)) {
        if (std::string(e->d_name).rfind("BAT", 0) != 0) continue;
        const std::string dir = std::string("/sys/class/power_supply/") + e->d_name + "/";
        const auto num = [&](const char* name) {
            std::string v;
            return bb::plat::ReadFile(dir + name, &v) ? atof(v.c_str()) : -1.0;
        };
        double now = num("energy_now"), full = num("energy_full"), rate = num("power_now");
        if (now < 0) { now = num("charge_now"); full = num("charge_full"); rate = num("current_now"); }
        if (now >= 0 && full > now && rate > 0) minutes = static_cast<int>((full - now) / rate * 60 + 0.5);
        break;
    }
    closedir(d);
    return minutes;
}

void CheckBattery() {
    const lsrc::Battery& b = lsrc::BatteryState();
    if (!b.has) return;
    bb::mascot::SetCharging(g_feat.batteryMoods && b.charging && b.percent < 100);
    if (!g_batteryKnown) {
        g_batteryKnown = true;
        g_wasCharging = b.charging;
        g_chargeToastAt = Mono();
        return;
    }
    if (b.charging != g_wasCharging) {
        g_wasCharging = b.charging;
        g_lowShown = 101;
        g_chargeToastAt = Mono();
        if (g_feat.battery) Toast(b.charging ? "Charging" : "On battery", std::to_string(b.percent) + "%", 2.5, b.percent / 100.0);
    }
    if (b.charging && b.percent >= 100 && Mono() - g_cheerAt > 600) g_cheerAt = Mono();
    // Low battery: once at 20 % and once at 10 % per discharge.
    if (g_feat.battery && !b.charging) {
        for (int at : {10, 20})
            if (b.percent <= at && g_lowShown > at) {
                g_lowShown = at;
                Toast("Battery low", std::to_string(b.percent) + "% left", 5, b.percent / 100.0);
                break;
            }
    }
    // While charging: the level (and time to full) every ChargingReminderMinutes.
    if (b.charging && g_feat.chargingEvery > 0 && b.percent < 100 && Mono() - g_chargeToastAt > g_feat.chargingEvery * 60.0) {
        g_chargeToastAt = Mono();
        std::string text = std::to_string(b.percent) + "%";
        const int m = MinutesToFull();
        if (m > 0) text += m >= 60 ? " \xC2\xB7 " + std::to_string(m / 60) + " h " + std::to_string(m % 60) + " min to full"
                                   : " \xC2\xB7 " + std::to_string(m) + " min to full";
        Toast("Charging", text, 3, b.percent / 100.0);
    }
}
}  // namespace

bool BatteryCheer() { return g_feat.batteryMoods && Mono() - g_cheerAt < 2.5; }
bool BatteryDrowsy() {
    const lsrc::Battery& b = lsrc::BatteryState();
    return g_feat.batteryMoods && b.has && !b.charging && b.percent < 15;
}

// ── Caps Lock, brightness, volume, Bluetooth, Do not disturb, copies ────────

namespace {
bool g_caps = false, g_num = false, g_keysKnown = false;
double g_startedAt = 0;

void OnKeymap(GdkKeymap* km, gpointer) {
    const bool caps = gdk_keymap_get_caps_lock_state(km), num = gdk_keymap_get_num_lock_state(km);
    // The keymap reports its state once more just after start-up: not a change.
    if (g_keysKnown && g_feat.capsLock && Mono() - g_startedAt > 3.0) {
        if (caps != g_caps) Toast("Caps Lock", caps ? "On" : "Off", 1.5);
        else if (num != g_num) Toast("Num Lock", num ? "On" : "Off", 1.5);
    }
    g_keysKnown = true;
    g_caps = caps;
    g_num = num;
}

// Brightness: the first backlight in /sys (sysfs files don't notify, so polled).
std::string g_backlight;
int g_lastBrightness = -1;

void FindBacklight() {
    if (DIR* d = opendir("/sys/class/backlight")) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.') { g_backlight = std::string("/sys/class/backlight/") + e->d_name; break; }
        closedir(d);
    }
}

void CheckBrightness() {
    if (g_backlight.empty()) return;
    std::string now, max;
    if (!bb::plat::ReadFile(g_backlight + "/brightness", &now) || !bb::plat::ReadFile(g_backlight + "/max_brightness", &max)) return;
    const int maxV = std::max(1, atoi(max.c_str()));
    const int pct = static_cast<int>(100.0 * atoi(now.c_str()) / maxV + 0.5);
    if (g_lastBrightness >= 0 && pct != g_lastBrightness && g_feat.brightness)
        Toast("Brightness", std::to_string(pct) + "%", 1.5, pct / 100.0);
    g_lastBrightness = pct;
}

// Volume: `pactl subscribe` reports sink changes (PulseAudio, and PipeWire through
// pipewire-pulse); each one reads the default sink's volume and mute.
int g_lastVolume = -1;
bool g_lastMute = false;
double g_volumeStartedAt = 0;

bool ReadVolume(int* pct, bool* mute) {
    gchar* out = nullptr;
    if (!g_spawn_command_line_sync("pactl get-sink-volume @DEFAULT_SINK@", &out, nullptr, nullptr, nullptr) || !out) return false;
    const std::string v = out;
    g_free(out);
    const size_t p = v.find('%');
    if (p == std::string::npos) return false;
    size_t s = p;
    while (s > 0 && isdigit(static_cast<unsigned char>(v[s - 1]))) --s;
    *pct = atoi(v.c_str() + s);
    out = nullptr;
    if (g_spawn_command_line_sync("pactl get-sink-mute @DEFAULT_SINK@", &out, nullptr, nullptr, nullptr) && out) {
        *mute = std::string(out).find("yes") != std::string::npos;
        g_free(out);
    }
    return true;
}

gboolean OnPactlLine(GIOChannel* ch, GIOCondition cond, gpointer) {
    if (cond & (G_IO_HUP | G_IO_ERR)) return G_SOURCE_REMOVE;
    gchar* line = nullptr;
    if (g_io_channel_read_line(ch, &line, nullptr, nullptr, nullptr) != G_IO_STATUS_NORMAL || !line) return G_SOURCE_CONTINUE;
    const bool sink = strstr(line, "'change' on sink") != nullptr;
    g_free(line);
    if (!sink || !g_feat.volume || Mono() - g_volumeStartedAt < 1.0) return G_SOURCE_CONTINUE;
    int pct = 0;
    bool mute = false;
    if (ReadVolume(&pct, &mute) && (pct != g_lastVolume || mute != g_lastMute)) {
        if (g_lastVolume >= 0) Toast("Volume", mute ? "Muted" : std::to_string(pct) + "%", 1.5, mute ? 0.0 : std::min(pct, 100) / 100.0);
        g_lastVolume = pct;
        g_lastMute = mute;
    }
    return G_SOURCE_CONTINUE;
}

void StartVolumeWatch() {
    gchar* pactl = g_find_program_in_path("pactl");
    if (!pactl) return;  // no PulseAudio tools: no volume alerts
    gchar* argv[] = {pactl, const_cast<gchar*>("subscribe"), nullptr};
    gint out = -1;
    if (g_spawn_async_with_pipes(nullptr, argv, nullptr, G_SPAWN_DEFAULT, nullptr, nullptr, nullptr, nullptr, &out, nullptr, nullptr)) {
        GIOChannel* ch = g_io_channel_unix_new(out);
        g_io_add_watch(ch, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), OnPactlLine, nullptr);
        g_volumeStartedAt = Mono();
        bool mute = false;
        ReadVolume(&g_lastVolume, &mute);
        g_lastMute = mute;
    }
    g_free(pactl);
}

// Bluetooth: BlueZ announces Connected and the battery level as property changes.
GDBusConnection* g_system = nullptr;
std::map<std::string, int> g_lowWarnedDay;  // device path -> day of year it was warned

std::string DeviceName(const char* path) {
    std::string name = "Bluetooth device";
    if (GVariant* r = g_dbus_connection_call_sync(g_system, "org.bluez", path, "org.freedesktop.DBus.Properties", "Get",
                                                  g_variant_new("(ss)", "org.bluez.Device1", "Alias"), G_VARIANT_TYPE("(v)"),
                                                  G_DBUS_CALL_FLAGS_NONE, 500, nullptr, nullptr)) {
        GVariant* v = nullptr;
        g_variant_get(r, "(v)", &v);
        if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) name = g_variant_get_string(v, nullptr);
        if (v) g_variant_unref(v);
        g_variant_unref(r);
    }
    return name;
}

int DeviceBattery(const char* path) {
    int pct = -1;
    if (GVariant* r = g_dbus_connection_call_sync(g_system, "org.bluez", path, "org.freedesktop.DBus.Properties", "Get",
                                                  g_variant_new("(ss)", "org.bluez.Battery1", "Percentage"), G_VARIANT_TYPE("(v)"),
                                                  G_DBUS_CALL_FLAGS_NONE, 500, nullptr, nullptr)) {
        GVariant* v = nullptr;
        g_variant_get(r, "(v)", &v);
        if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_BYTE)) pct = g_variant_get_byte(v);
        if (v) g_variant_unref(v);
        g_variant_unref(r);
    }
    return pct;
}

int DayOfYear() {
    const time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    return lt.tm_yday + lt.tm_year * 400;
}

void OnBluez(GDBusConnection*, const gchar*, const gchar* path, const gchar*, const gchar*, GVariant* params, gpointer) {
    const gchar* iface = nullptr;
    GVariant* changed = nullptr;
    g_variant_get(params, "(&s@a{sv}@as)", &iface, &changed, nullptr);
    if (!changed) return;
    if (strcmp(iface, "org.bluez.Device1") == 0 && g_feat.bluetooth) {
        gboolean connected = FALSE;
        if (g_variant_lookup(changed, "Connected", "b", &connected)) {
            std::string text = DeviceName(path);
            if (connected && g_feat.bluetoothBattery) {
                const int pct = DeviceBattery(path);
                if (pct >= 0) text += " \xC2\xB7 " + std::to_string(pct) + "%";
            }
            Toast(connected ? "Connected" : "Disconnected", text, 3);
        }
    } else if (strcmp(iface, "org.bluez.Battery1") == 0 && g_feat.bluetoothLow) {
        guchar pct = 0;
        if (g_variant_lookup(changed, "Percentage", "y", &pct) && pct <= 20 && g_lowWarnedDay[path] != DayOfYear()) {
            g_lowWarnedDay[path] = DayOfYear();
            Toast("Low battery", DeviceName(path) + " \xC2\xB7 " + std::to_string(pct) + "%", 5, pct / 100.0);
        }
    }
    g_variant_unref(changed);
}

// Browsers on X11 capture the screen themselves, without PipeWire, but show the
// same window as on Windows while they share: Chromium's "<site> is sharing your
// screen" bar (Google Meet, and the like), Firefox's "Sharing Indicator".
void CheckSharingWindows() {
#ifdef GDK_WINDOWING_X11
    GdkDisplay* gd = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gd)) return;
    Display* d = GDK_DISPLAY_XDISPLAY(gd);
    const Atom listAtom = XInternAtom(d, "_NET_CLIENT_LIST", True), nameAtom = XInternAtom(d, "_NET_WM_NAME", True),
               utf8 = XInternAtom(d, "UTF8_STRING", True);
    if (!listAtom || !nameAtom || !utf8) return;
    Atom type;
    int format;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    bool sharing = false;
    gdk_x11_display_error_trap_push(gd);
    if (XGetWindowProperty(d, DefaultRootWindow(d), listAtom, 0, 4096, False, XA_WINDOW, &type, &format, &n, &after, &data) == Success && data) {
        const Window* wins = reinterpret_cast<Window*>(data);
        for (unsigned long i = 0; i < n && !sharing; ++i) {
            unsigned char* name = nullptr;
            unsigned long nn = 0;
            if (XGetWindowProperty(d, wins[i], nameAtom, 0, 256, False, utf8, &type, &format, &nn, &after, &name) == Success && name) {
                const std::string title(reinterpret_cast<char*>(name), nn);
                for (const char* m : {" is sharing your screen", " is sharing a window", " is sharing this tab", "Sharing Indicator"})
                    if (title.find(m) != std::string::npos) sharing = true;
                XFree(name);
            }
        }
        XFree(data);
    }
    gdk_x11_display_error_trap_pop_ignored(gd);
    bb::llook::SetSharingWindow(sharing);
#endif
}

// Bunbun.FocusGuard: while a focus timer runs, a gentle note when the window in
// front matches one of Bunbun.FocusGuardApps (at most once a minute per word).
std::map<std::string, double> g_guardShown;
void CheckFocusGuard() {
#ifdef GDK_WINDOWING_X11
    if (!g_feat.focusGuard || !lsrc::FocusState().running) return;
    const std::string front = X11ActiveWindowText();
    if (front.empty()) return;
    const std::string list = Lower(bb::plat::Utf8(g_feat.focusGuardApps));
    size_t start = 0;
    while (start <= list.size()) {
        const size_t comma = list.find(',', start);
        const std::string word = Trim(list.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!word.empty() && front.find(word) != std::string::npos) {
            if (Mono() - g_guardShown[word] > 60) {
                g_guardShown[word] = Mono();
                Toast("Focus", "Still focusing \xE2\x80\x94 " + word + " can wait", 4);
            }
            return;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
#endif
}

// Do not disturb (GNOME's notification banners), from the Quick toggles' state.
int g_dndKnown = -1;
void CheckDnd() {
    const lsrc::Radios& r = lsrc::RadioState();
    if (!r.dndKnown) return;
    const int now = r.dnd ? 1 : 0;
    if (g_dndKnown >= 0 && now != g_dndKnown && g_feat.dnd) Toast("Do not disturb", r.dnd ? "On" : "Off", 2);
    g_dndKnown = now;
}

// The default sound output: a Bluetooth device (bluez_output.* / bluez_sink.*)?
bool g_btAudio = false;
void CheckAudioOutput() {
    if (g_feat.btOffsetMs == 0) return;
    gchar* out = nullptr;
    if (g_spawn_command_line_sync("pactl get-default-sink", &out, nullptr, nullptr, nullptr) && out) {
        g_btAudio = strstr(out, "bluez") != nullptr;
        g_free(out);
    }
}
}  // namespace

bool BluetoothAudio() { return g_btAudio; }

bool VolumeNow(int* pct, bool* mute) {
    static double at = -10;
    static int v = -1;
    static bool m = false;
    static bool ok = false;
    if (Mono() - at > 2.0) {
        at = Mono();
        ok = ReadVolume(&v, &m);
    }
    *pct = v;
    *mute = m;
    return ok && v >= 0;
}

void SetVolume(int pct) {
    const std::string cmd = "pactl set-sink-volume @DEFAULT_SINK@ " + std::to_string(std::clamp(pct, 0, 150)) + "%";
    g_spawn_command_line_async(cmd.c_str(), nullptr);
}

void ToggleMute() { g_spawn_command_line_async("pactl set-sink-mute @DEFAULT_SINK@ toggle", nullptr); }

// `pactl list sinks` has each sink's Name and Description; the default's is shown.
std::string AudioOutputName() {
    static double at = -100;
    static std::string name;
    if (Mono() - at < 10) return name;
    at = Mono();
    gchar* def = nullptr;
    gchar* list = nullptr;
    if (g_spawn_command_line_sync("pactl get-default-sink", &def, nullptr, nullptr, nullptr) && def &&
        g_spawn_command_line_sync("pactl list sinks", &list, nullptr, nullptr, nullptr) && list) {
        std::string sink = def;
        while (!sink.empty() && isspace(static_cast<unsigned char>(sink.back()))) sink.pop_back();
        const std::string all = list;
        const size_t at2 = all.find("Name: " + sink);
        if (at2 != std::string::npos) {
            const size_t d = all.find("Description: ", at2);
            if (d != std::string::npos) name = all.substr(d + 13, all.find('\n', d) - d - 13);
        }
    }
    g_free(def);
    g_free(list);
    return name;
}

void Start(std::function<void(const std::string&, const std::string&, double, double)> toast, std::function<void()> changed) {
    StartIdleWatch();  // AFK
    g_toast = std::move(toast);
    g_changed = std::move(changed);
    g_lastInteraction = Mono();
    g_startedAt = Mono();
    if (GdkKeymap* km = gdk_keymap_get_for_display(gdk_display_get_default())) {
        OnKeymap(km, nullptr);
        g_signal_connect(km, "state-changed", G_CALLBACK(OnKeymap), nullptr);
    }
    FindBacklight();
    StartVolumeWatch();
    g_system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    if (g_system)
        g_dbus_connection_signal_subscribe(g_system, "org.bluez", "org.freedesktop.DBus.Properties", "PropertiesChanged", nullptr,
                                           nullptr, G_DBUS_SIGNAL_FLAGS_NONE, OnBluez, nullptr, nullptr);
    lsrc::OnCopy([](const std::string& text) {
        if (!g_feat.clipboard) return;
        std::string line = text.substr(0, text.find('\n'));
        if (line.size() > 120) line = line.substr(0, 120);
        Toast("Copied", line, 2);
    });
    g_timeout_add(300, [](gpointer) -> gboolean {
        CheckBrightness();
        return G_SOURCE_CONTINUE;
    }, nullptr);
    g_timeout_add_seconds(1, [](gpointer) -> gboolean {
#ifdef GDK_WINDOWING_X11
        // In full screen plus the per-app lists (matched on the window's class and title).
        int action = 0;  // 0 nothing, 1 tuck, 2 hide
        const bool watched = g_feat.hideFullscreen || g_feat.tuckFullscreen || !g_feat.fsTuckApps.empty() ||
                             !g_feat.fsHideApps.empty();
        if (watched && X11FullscreenNow()) {
            const std::wstring app = bb::plat::Wide(X11ActiveWindowText());
            if (bb::applist::Matches(g_feat.fsShowApps, app)) action = 0;
            else if (bb::applist::Matches(g_feat.fsHideApps, app)) action = 2;
            else if (bb::applist::Matches(g_feat.fsTuckApps, app)) action = 1;
            else action = g_feat.tuckFullscreen ? 1 : g_feat.hideFullscreen ? 2 : 0;
        }
        const bool full = action != 0;
        if (full != g_fullscreen || (action == 1) != g_fullscreenTuck) {
            g_fullscreen = full;
            g_fullscreenTuck = action == 1;
            if (g_changed) g_changed();
        }
#endif
        CheckDnd();
        return G_SOURCE_CONTINUE;
    }, nullptr);
    g_timeout_add_seconds(2, [](gpointer) -> gboolean {
        CheckFocusGuard();
        CheckSharingWindows();
        return G_SOURCE_CONTINUE;
    }, nullptr);
    // The capture stops a few seconds after nothing wants it any more.
    g_timeout_add_seconds(1, [](gpointer) -> gboolean {
        if (g_parec && Mono() - g_audioWantedAt > 3) StopAudio();
        return G_SOURCE_CONTINUE;
    }, nullptr);
    g_timeout_add_seconds(5, [](gpointer) -> gboolean {
        CheckBattery();
        CheckAudioOutput();
        return G_SOURCE_CONTINUE;
    }, nullptr);
    CheckBattery();
    CheckAudioOutput();
    UpdateShortcuts();
}

void ApplySettingsChange() {
    UpdateShortcuts();
}

// ── The date ────────────────────────────────────────────────────────────────

std::string FormatDate(time_t when) {
    tm lt{};
    localtime_r(&when, &lt);
    char buf[64];
    if (g_feat.dateFormat.empty()) {
        strftime(buf, sizeof buf, "%a %e %b", &lt);
        std::string s = buf;
        for (size_t p; (p = s.find("  ")) != std::string::npos;) s.erase(p, 1);
        return s;
    }
    // The Windows island's tokens: yyyy, MMMM, MMM, MM, M, dddd, ddd, dd, d.
    const std::string f = bb::plat::Utf8(g_feat.dateFormat);
    std::string out;
    for (size_t i = 0; i < f.size();) {
        const auto run = [&](char c) { size_t n = 0; while (i + n < f.size() && f[i + n] == c) ++n; return n; };
        if (f[i] == 'y') {
            const size_t n = run('y');
            strftime(buf, sizeof buf, n >= 3 ? "%Y" : "%y", &lt);
            out += buf;
            i += n;
        } else if (f[i] == 'M') {
            const size_t n = run('M');
            strftime(buf, sizeof buf, n >= 4 ? "%B" : n == 3 ? "%b" : n == 2 ? "%m" : "%-m", &lt);
            out += buf;
            i += n;
        } else if (f[i] == 'd') {
            const size_t n = run('d');
            strftime(buf, sizeof buf, n >= 4 ? "%A" : n == 3 ? "%a" : n == 2 ? "%d" : "%-d", &lt);
            out += buf;
            i += n;
        } else {
            out += f[i++];
        }
    }
    return out;
}

// ── Idle eyes ───────────────────────────────────────────────────────────────
// The Windows island's eyes (studio_draw.inc, DrawIdleEyes): two glowing eyes that
// follow the pointer and blink, coloured by what the agents are doing.

void DrawEyes(cairo_t* cr, double cx, double cy, double height, bb::mascot::Mood mood, bb::AgentPhase phase, double now,
              double moodSince, double pointerX, double pointerY) {
    static constexpr uint32_t kColors[6] = {0xe3ffd4, 0xffffff, 0x8fd3ff, 0xffb3d1, 0xd4b3ff, 0xfff2a8};
    const auto rgb = [](uint32_t v, double* c) { c[0] = ((v >> 16) & 0xff) / 255.0; c[1] = ((v >> 8) & 0xff) / 255.0; c[2] = (v & 0xff) / 255.0; };
    const double u = std::clamp(height / 32.0, 0.4, 3.0), k = 0.62 * u;
    double col[3];
    rgb(kColors[std::clamp(g_feat.eyeColor, 0, 5)], col);
    double alpha = 1.0;
    enum class Shape { Normal, Focus, Curious, Surprised, Happy, Closed, Dizzy, Sad } shape = Shape::Normal;
    switch (g_feat.eyeStatus ? phase : bb::AgentPhase::None) {
        case bb::AgentPhase::Thinking:
        case bb::AgentPhase::Working:
            rgb(0xff9a1f, col);
            alpha = 0.3 + 0.7 * (0.5 + 0.5 * std::cos(now / 0.2));
            shape = Shape::Focus;
            break;
        case bb::AgentPhase::Question: rgb(0xff9a1f, col); shape = Shape::Curious; break;
        case bb::AgentPhase::Approval: rgb(0xff9a1f, col); shape = Shape::Surprised; break;
        case bb::AgentPhase::Finished: rgb(0x22e05a, col); shape = Shape::Happy; break;
        case bb::AgentPhase::Error: rgb(0xff3b3b, col); break;
        default: break;
    }
    switch (mood) {
        case bb::mascot::Mood::Confused: shape = Shape::Dizzy; break;
        case bb::mascot::Mood::Sleepy: shape = Shape::Closed; break;
        case bb::mascot::Mood::Happy: shape = Shape::Happy; break;
        case bb::mascot::Mood::Sad: shape = Shape::Sad; break;
        default: break;
    }
    double dx = 0, dy = 0;
    if (bb::mascot::GetStyle().eyesFollow && pointerX >= 0) {
        dx = std::clamp((pointerX - cx) / (320.0 * u), -1.0, 1.0) * 2.2 * u;
        dy = std::clamp((pointerY - cy) / (200.0 * u), -1.0, 1.0) * 0.8 * u;
    }
    // Blink: 140 ms, the first at 2.5 s, then every 2.5-6 s; one in 40 is a wink.
    bool blink = false, wink = false;
    if (shape == Shape::Normal || shape == Shape::Focus) {
        static double next = -1, until = -1;
        static bool winkNext = false;
        if (next < 0) next = now + 2.5;
        if (now >= next) {
            until = now + 0.14;
            const double r = std::fmod(std::sin(now * 12.9898) * 43758.5453, 1.0);
            next = now + 2.5 + std::fabs(r) * 3.5;
            winkNext = std::fabs(std::fmod(r * 97.0, 1.0)) < 0.025;
        }
        if (now < until) { blink = !winkNext; wink = winkNext; }
    }
    const double sep = 8.5 * u;
    const double bob = mood == bb::mascot::Mood::Music ? std::sin(now * 2 * M_PI / 0.5) * u : 0.0;
    const double line = 3.0 * k;
    const auto ellipse = [&](double x, double y, double rx, double ry) {
        cairo_save(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, rx, ry);
        cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_fill(cr);
    };
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (int side = -1; side <= 1; side += 2) {
        const double ex = cx + side * sep + dx, ey = cy + dy + bob;
        cairo_set_source_rgba(cr, col[0], col[1], col[2], alpha * 0.16);  // a faint rim of light
        ellipse(ex, ey, 5 * k + 1.2 * u, 8 * k + 1.2 * u);
        cairo_set_source_rgba(cr, col[0], col[1], col[2], alpha);
        cairo_set_line_width(cr, line);
        Shape s = shape;
        if (blink || (wink && side > 0)) s = Shape::Closed;
        switch (s) {
            case Shape::Normal: ellipse(ex, ey, 5 * k, 8 * k); break;
            case Shape::Focus: ellipse(ex, ey + k, 5.5 * k, 4 * k); break;
            case Shape::Curious: ellipse(ex, ey, 6 * k, 9.5 * k); break;
            case Shape::Surprised: ellipse(ex, ey, 7 * k, 7 * k); break;
            case Shape::Happy:
                cairo_new_path(cr);
                cairo_arc(cr, ex, ey + 3 * k, 5 * k, 3.6128, 5.8119);
                cairo_stroke(cr);
                break;
            case Shape::Closed:
                cairo_new_path(cr);
                if (wink && side > 0 && !blink) {
                    cairo_move_to(cr, ex - 5 * k, ey + 2 * k);
                    cairo_line_to(cr, ex + 5 * k, ey + 2 * k);
                } else {
                    cairo_arc(cr, ex, ey - 2 * k, 5 * k, 0.4712, 2.6704);
                }
                cairo_stroke(cr);
                break;
            case Shape::Sad:
                cairo_save(cr);
                cairo_translate(cr, ex, ey + 2 * k);
                cairo_rotate(cr, side * -0.45);
                ellipse(0, 0, 4.5 * k, 6.5 * k);
                cairo_restore(cr);
                break;
            case Shape::Dizzy: {
                const double turn = (now - moodSince) / 0.14 * side;
                cairo_new_path(cr);
                cairo_move_to(cr, ex, ey);
                for (double a = 0.3; a < 12.566; a += 0.3) {
                    const double r = a * 0.55 * k;
                    cairo_line_to(cr, ex + std::cos(a + turn) * r, ey + std::sin(a + turn) * r);
                }
                cairo_set_line_width(cr, 2.2 * k);
                cairo_stroke(cr);
                break;
            }
        }
    }
}

}  // namespace bb::lfeat
