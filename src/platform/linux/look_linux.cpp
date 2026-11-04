// Bunbun — the Linux island's look, sounds and privacy dots (see look_linux.h).
#include "look_linux.h"

#include <dirent.h>
#include <glib.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <thread>

#include "platform.h"
#include "settings.h"

namespace bb::llook {
namespace {

Look g_look;         // written and read on the main thread
std::mutex g_lookMutex;  // guards writes, and the privacy thread's copies

// The same palettes as Windows (kThemePalettes in island_core.cpp): background,
// primary text, secondary text, border.
struct Palette { const wchar_t* id; uint32_t bg, fg, sec, border; };
constexpr Palette kPalettes[] = {
    {L"obsidian", 0x08080A, 0xFFFFFF, 0x9B9BA5, 0x1E1E22}, {L"graphite", 0x1C1C1E, 0xFFFFFF, 0xA8A8AE, 0x323236},
    {L"slate", 0x111721, 0xE9EEF6, 0x92A2B8, 0x232C3A},    {L"nord", 0x2E3440, 0xECEFF4, 0xA7B0C0, 0x3B4252},
    {L"evergreen", 0x0C1512, 0xE4F1EA, 0x8CAE9D, 0x1A2A23}, {L"espresso", 0x1A1512, 0xF6EEE7, 0xB7A395, 0x2E2420},
    {L"plum", 0x16111C, 0xF1EAF6, 0xAC9BB9, 0x281F33},     {L"porcelain", 0xF5F6F8, 0x14161A, 0x5B6270, 0xD8DBE1},
};
bool g_bottom = false, g_merged = false;

void Rgb(uint32_t v, double* out) {
    out[0] = ((v >> 16) & 0xFF) / 255.0;
    out[1] = ((v >> 8) & 0xFF) / 255.0;
    out[2] = (v & 0xFF) / 255.0;
}

// "#RGB", "#RRGGBB" or "#RRGGBBAA" (alpha ignored); false if it isn't a colour.
bool Hex(std::wstring s, double* out) {
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    if (s.size() == 3 || s.size() == 4) s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() < 6) return false;
    s = s.substr(0, 6);
    if (!std::all_of(s.begin(), s.end(), [](wchar_t c) { return std::iswxdigit(c); })) return false;
    Rgb(static_cast<uint32_t>(std::wcstoul(s.c_str(), nullptr, 16)), out);
    return true;
}

}  // namespace

void LoadSettings() {
    namespace s = bb::settings;
    Look l;
    const std::wstring shape = s::Get(L"Appearance.ShapeStyle");
    l.shape = shape == L"notch" ? Shape::Notch : shape == L"w11" ? Shape::W11 : Shape::Pill;

    // Theme: a preset's colours, or the hex fields for Custom.
    const std::wstring preset = s::Get(L"Themes.ThemePreset");
    const Palette* pal = &kPalettes[0];
    for (const Palette& p : kPalettes)
        if (preset == p.id) pal = &p;
    Rgb(pal->bg, l.fill);
    Rgb(pal->fg, l.ink);
    Rgb(pal->sec, l.sec);
    Rgb(pal->border, l.border);
    if (preset == L"custom") {
        Hex(s::Get(L"Themes.PillBgColor"), l.fill);
        Hex(s::Get(L"Themes.TextPrimaryColor"), l.ink);
        Hex(s::Get(L"Themes.TextSecondaryColor"), l.sec);
    }
    // A border colour you picked wins over the preset's, as on Windows.
    if (preset == L"custom" || s::IsSet(L"Themes.ContourBorderHex")) Hex(s::Get(L"Themes.ContourBorderHex"), l.border);
    l.borderOn = s::Get(L"Themes.ContourBorderMode") != L"borderless";
    l.opacity = std::clamp(s::GetInt(L"Themes.PillOpacity"), 35, 100) / 100.0;
    // Graphite reads best a little see-through, as on Windows.
    if (pal->bg == 0x1C1C1E && preset != L"custom" && !s::IsSet(L"Themes.PillOpacity")) l.opacity = 0.88;
    if (s::Get(L"Themes.IslandFill") == L"opaque") {
        // One steady colour, nothing showing through.
        l.fill[0] = l.fill[1] = l.fill[2] = 0;
        Hex(s::Get(L"Themes.SolidColor"), l.fill);
        l.opacity = 1.0;
        l.depth = false;  // flat, as on Windows
        l.bloom = 0;
    }
    l.shadow = s::GetBool(L"Themes.DropShadow");
    l.depth = s::GetBool(L"Themes.MaterialDepth");
    l.bloom = std::clamp(s::GetInt(L"Themes.AccentBloom"), 0, 200) / 100.0;
    l.tint = std::clamp(s::GetInt(L"Themes.TintIntensity"), 0, 100) / 100.0;
    const std::string font = bb::plat::Utf8(s::Get(L"Themes.FontFamily"));
    l.font = font.empty() ? "Sans" : font;
    l.textScale = std::clamp(s::GetInt(L"Themes.TextScale"), 70, 160) / 100.0;

    l.sounds = s::GetBool(L"Bunbun.Sounds");
    l.soundOpenClose = s::GetBool(L"Bunbun.SoundOpenClose");
    l.soundVolume = std::clamp(s::GetInt(L"Bunbun.SoundVolume"), 0, 100);

    l.dots = s::GetBool(L"Indicators.PrivacyDots");
    l.dotMic = s::GetBool(L"Indicators.PrivacyDotsMic");
    l.dotCam = s::GetBool(L"Indicators.PrivacyDotsCam");
    l.dotScreen = s::GetBool(L"Indicators.PrivacyDotsScreen");
    Hex(s::Get(L"Indicators.PrivacyDotsScreenHex"), l.screenColor);
    Hex(s::Get(L"Indicators.PrivacyDotsMicHex"), l.micColor);
    Hex(s::Get(L"Indicators.PrivacyDotsCamHex"), l.camColor);
    std::lock_guard lock(g_lookMutex);
    g_look = l;
}

const Look& Get() { return g_look; }

// ── Shape ───────────────────────────────────────────────────────────────────

double Radius(double h) {
    if (g_look.shape == Shape::W11) return h > 60 ? 12.0 : 8.0;
    return std::min(h / 2, h > 150 ? 32.0 : 22.0);
}

double TopGap(double normalGap) { return g_look.shape == Shape::Notch || g_merged ? 0.0 : normalGap; }
void SetEdge(bool bottom, bool merged) { g_bottom = bottom; g_merged = merged; }

void SurfacePath(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min({r, w / 2, h / 2});
    cairo_new_sub_path(cr);
    if (g_look.shape == Shape::Notch || g_merged) {
        // Square against the screen edge, rounded on the other side. The notch also
        // gets concave "ears" where it meets the edge, as on Windows
        // (CreateNotchGeometry): it flows out of the bezel instead of starting with a
        // hard corner. Each ear is a quadratic curve from the edge, past the body's
        // corner (the control point), onto its side; written as a cubic for cairo.
        const double ear = g_look.shape == Shape::Notch ? std::max(0.0, std::min({10.0, h / 3, w / 8})) : 0.0;
        const auto quad = [&](double x0, double y0, double cx, double cy, double x2, double y2) {
            cairo_curve_to(cr, x0 + 2.0 / 3 * (cx - x0), y0 + 2.0 / 3 * (cy - y0), x2 + 2.0 / 3 * (cx - x2), y2 + 2.0 / 3 * (cy - y2),
                           x2, y2);
        };
        if (g_bottom) {
            const double b = y + h;
            cairo_move_to(cr, x + w + ear, b);
            cairo_line_to(cr, x - ear, b);
            if (ear > 0) quad(x - ear, b, x, b, x, b - ear);
            cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
            cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
            if (ear > 0) quad(x + w, b - ear, x + w, b, x + w + ear, b);
        } else {
            cairo_move_to(cr, x - ear, y);
            cairo_line_to(cr, x + w + ear, y);
            if (ear > 0) quad(x + w + ear, y, x + w, y, x + w, y + ear);
            cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
            cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
            if (ear > 0) quad(x, y + ear, x, y, x - ear, y);
        }
        cairo_close_path(cr);
        return;
    }
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

// ── Text ────────────────────────────────────────────────────────────────────

void Ink(double* r, double* g, double* b, double* a) {
    if (*r != 1 || *g != 1 || *b != 1) return;
    // Labels drawn at 40-80 % white are secondary text; fills and glyphs keep the ink.
    const bool secondary = *a >= 0.4 && *a < 0.8;
    const double* c = secondary ? g_look.sec : g_look.ink;
    *r = c[0];
    *g = c[1];
    *b = c[2];
    if (secondary) *a = std::min(1.0, *a / 0.65);
}

const char* Font() { return g_look.font.c_str(); }
double TextSize(double size) { return size * g_look.textScale; }

// ── Sounds ──────────────────────────────────────────────────────────────────

void PlayChime(bb::chime::Chime c) {
    using bb::chime::Chime;
    const Look& l = g_look;
    if (!l.sounds || l.soundVolume == 0) return;
    if ((c == Chime::Open || c == Chime::Close) && !l.soundOpenClose) return;
    // Open/close never stack up; other chimes win over them.
    static gint64 lastAt = 0;
    const gint64 now = g_get_monotonic_time();
    if ((c == Chime::Open || c == Chime::Close) && now - lastAt < 450000) return;
    lastAt = now;

    // The chime as a file in the runtime folder, made once per volume.
    const std::string dir = bb::plat::RuntimeDir();
    const std::string path = dir + "/chime-" + std::to_string(static_cast<int>(c)) + "-" + std::to_string(l.soundVolume) + ".wav";
    if (!bb::plat::FileExists(path)) {
        bb::plat::MakeDirs(dir);
        const auto wav = bb::chime::MakeWav(bb::chime::NotesFor(c), l.soundVolume / 100.0f);
        if (!bb::plat::WriteFileAtomic(path, std::string(wav.begin(), wav.end()))) return;
    }
    // PulseAudio's player first: it also works on PipeWire (pipewire-pulse), while
    // pw-play needs PipeWire itself running (Ubuntu 22.04 and WSL use PulseAudio).
    // Then pw-play, then plain ALSA.
    static std::string player;
    static bool looked = false;
    if (!looked) {
        looked = true;
        for (const char* p : {"paplay", "pw-play", "aplay"}) {
            if (gchar* found = g_find_program_in_path(p)) {
                player = p;
                g_free(found);
                break;
            }
        }
        if (player.empty()) fprintf(stderr, "Bunbun: no pw-play, paplay or aplay; sounds are off.\n");
    }
    if (player.empty()) return;
    if (player == "aplay") bb::plat::Spawn({player, "-q", path});
    else bb::plat::Spawn({player, path});
}

// ── Privacy dots ────────────────────────────────────────────────────────────
// Camera: a process of ours has a /dev/video* device open. Microphone: an ALSA
// capture stream is running (/proc/asound/card*/pcm*c/sub*/status), which is
// also true under PipeWire and PulseAudio while something records.

namespace {
std::atomic<bool> g_mic{false}, g_cam{false}, g_screen{false};
std::atomic<bool> g_sharingWindow{false};
std::mutex g_screenTextMutex;
std::string g_screenText;  // guarded by g_screenTextMutex

// The screen, shared or recorded. Three signs, any one lights the dot:
//  - a recorder running: ffmpeg / GStreamer grabbing X11 or KMS, wf-recorder,
//    wl-screenrec, gpu-screen-recorder, SimpleScreenRecorder, Kazam, Peek, vokoscreen;
//  - OBS with an output on (its log's last Start/Stop marker, as on Windows);
//  - PipeWire streaming a screencast: a running Video/Source node that isn't a
//    camera, which is how the desktop portal shares a screen (browsers, Zoom, Meet,
//    OBS on Wayland) and how GNOME's own recorder works.
std::string g_recorderName;  // the privacy thread's: who RecorderRunning found
bool RecorderRunning(bool* obs) {
    DIR* proc = opendir("/proc");
    if (!proc) return false;
    bool found = false;
    while (dirent* e = readdir(proc)) {
        if (found) break;
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        std::string cmd;
        if (!bb::plat::ReadFile(std::string("/proc/") + e->d_name + "/cmdline", &cmd) || cmd.empty()) continue;
        for (char& c : cmd) if (c == 0) c = ' ';
        const std::string exe = cmd.substr(0, cmd.find(' '));
        const std::string name = exe.substr(exe.rfind('/') + 1);
        if (name == "obs") *obs = true;
        for (const char* r : {"wf-recorder", "wl-screenrec", "gpu-screen-recorder", "simplescreenrecorder", "kazam", "peek",
                              "vokoscreenNG", "vokoscreen"})
            if (name == r) { found = true; g_recorderName = name; }
        if ((name == "ffmpeg" && (cmd.find("x11grab") != std::string::npos || cmd.find("kmsgrab") != std::string::npos)) ||
            (name.rfind("gst-launch", 0) == 0 && cmd.find("ximagesrc") != std::string::npos)) {
            found = true;
            g_recorderName = name;
        }
    }
    closedir(proc);
    return found;
}

// 0 = no OBS output, 1 = recording (or the replay buffer), 2 = streaming.
int ObsOutput() {
    const std::string dir = bb::plat::HomeDir() + "/.config/obs-studio/logs";
    DIR* d = opendir(dir.c_str());
    if (!d) return 0;
    std::string newest;
    while (dirent* e = readdir(d))
        if (strstr(e->d_name, ".txt") && std::string(e->d_name) > newest) newest = e->d_name;  // named by date and time
    closedir(d);
    std::string text;
    if (newest.empty() || !bb::plat::ReadFile(dir + "/" + newest, &text)) return 0;
    if (text.size() > 512 * 1024) text = text.substr(text.size() - 512 * 1024);
    const auto on = [&](const char* o) {
        const size_t start = text.rfind(std::string("==== ") + o + " Start"), stop = text.rfind(std::string("==== ") + o + " Stop");
        return start != std::string::npos && (stop == std::string::npos || stop < start);
    };
    if (on("Streaming")) return 2;
    return on("Recording") || on("Replay Buffer") ? 1 : 0;
}

bool PipeWireScreencast() {
    static int haveDump = -1;
    if (haveDump < 0) {
        gchar* p = g_find_program_in_path("pw-dump");
        haveDump = p != nullptr;
        g_free(p);
    }
    if (!haveDump) return false;
    gchar* out = nullptr;
    if (!g_spawn_command_line_sync("pw-dump", &out, nullptr, nullptr, nullptr) || !out) return false;
    const std::string dump = out;
    g_free(out);
    // pw-dump prints one JSON object per graph object; look at each node's text.
    bool found = false;
    for (size_t at = dump.find("\"type\": \"PipeWire:Interface:Node\""); at != std::string::npos && !found;
         at = dump.find("\"type\": \"PipeWire:Interface:Node\"", at + 1)) {
        const size_t end = dump.find("\"type\": \"PipeWire:Interface:", at + 10);
        const std::string node = dump.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (node.find("\"media.class\": \"Video/Source\"") == std::string::npos) continue;
        if (node.find("\"state\": \"running\"") == std::string::npos) continue;
        if (node.find("api.v4l2") != std::string::npos || node.find("api.libcamera") != std::string::npos) continue;  // a camera
        found = true;
    }
    return found;
}

bool ScreenCaptured() {
    std::string text;
    bool obs = false;
    const bool recorder = RecorderRunning(&obs);
    const int obsOut = obs ? ObsOutput() : 0;
    if (obsOut == 2) text = "OBS Studio is streaming your screen";
    else if (obsOut == 1) text = "OBS Studio is recording your screen";
    else if (recorder) text = g_recorderName + " is recording your screen";
    else if (g_sharingWindow.load()) text = "Your browser is sharing your screen";
    else if (PipeWireScreencast()) text = "An app is sharing your screen";
    std::lock_guard lock(g_screenTextMutex);
    g_screenText = text;
    return !text.empty();
}

bool CameraOpen() {
    DIR* proc = opendir("/proc");
    if (!proc) return false;
    bool found = false;
    while (dirent* e = readdir(proc)) {
        if (found) break;
        if (e->d_name[0] < '1' || e->d_name[0] > '9') continue;
        const std::string fdDir = std::string("/proc/") + e->d_name + "/fd";
        DIR* fds = opendir(fdDir.c_str());  // fails for other users' processes
        if (!fds) continue;
        while (dirent* f = readdir(fds)) {
            if (f->d_name[0] == '.') continue;
            char target[64];
            const ssize_t n = readlink((fdDir + "/" + f->d_name).c_str(), target, sizeof target - 1);
            if (n > 0) {
                target[n] = 0;
                if (strncmp(target, "/dev/video", 10) == 0) {
                    found = true;
                    break;
                }
            }
        }
        closedir(fds);
    }
    closedir(proc);
    return found;
}

bool MicRecording() {
    DIR* asound = opendir("/proc/asound");
    if (!asound) return false;
    bool found = false;
    while (dirent* card = readdir(asound)) {
        if (found || strncmp(card->d_name, "card", 4) != 0) continue;
        const std::string cardDir = std::string("/proc/asound/") + card->d_name;
        DIR* pcms = opendir(cardDir.c_str());
        if (!pcms) continue;
        while (dirent* pcm = readdir(pcms)) {
            const std::string name = pcm->d_name;
            if (name.rfind("pcm", 0) != 0 || name.back() != 'c') continue;  // capture devices only
            for (int sub = 0; sub < 8 && !found; ++sub) {
                std::string status;
                if (bb::plat::ReadFile(cardDir + "/" + name + "/sub" + std::to_string(sub) + "/status", &status) &&
                    status.find("RUNNING") != std::string::npos)
                    found = true;
            }
            if (found) break;
        }
        closedir(pcms);
    }
    closedir(asound);
    return found;
}
}  // namespace

void StartPrivacy(std::function<void()> changed) {
    auto* cb = new std::function<void()>(std::move(changed));
    std::thread([cb] {
        for (;;) {
            bool wantMic, wantCam, wantScreen;
            {
                std::lock_guard lock(g_lookMutex);
                wantMic = g_look.dots && g_look.dotMic;
                wantCam = g_look.dots && g_look.dotCam;
                wantScreen = g_look.dots && g_look.dotScreen;
            }
            const bool mic = wantMic && MicRecording();
            const bool cam = wantCam && CameraOpen();
            const bool screen = wantScreen && ScreenCaptured();
            const bool micChanged = mic != g_mic.exchange(mic);  // all exchanged every time
            const bool camChanged = cam != g_cam.exchange(cam);
            const bool screenChanged = screen != g_screen.exchange(screen);
            if (micChanged || camChanged || screenChanged)
                g_idle_add([](gpointer p) -> gboolean { (*static_cast<std::function<void()>*>(p))(); return G_SOURCE_REMOVE; }, cb);
            sleep(2);
        }
    }).detach();
}

bool MicInUse() { return g_mic.load(); }
bool CamInUse() { return g_cam.load(); }
bool ScreenInUse() { return g_screen.load(); }
void SetSharingWindow(bool shown) { g_sharingWindow.store(shown); }
std::string ScreenText() {
    std::lock_guard lock(g_screenTextMutex);
    return g_screenText;
}

}  // namespace bb::llook
