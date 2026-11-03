// Bunbun — the island on Linux (GTK 3 + Cairo/Pango).
//
// One transparent window at the top of the screen holds the pill; its input
// region is the pill itself, so clicks around it reach the windows below.
//   - Wayland with layer-shell (KDE Plasma, Sway, Hyprland, …): a layer
//     surface anchored to the top edge (gtk-layer-shell).
//   - X11 (and GNOME on Wayland, which has no layer-shell, through XWayland):
//     an always-on-top dock window placed at the top of the work area.
// It shows the clock, what's playing (MPRIS), the mascot, and coding agents
// (the same agent bridge as on Windows, over a Unix socket), with Allow / Deny
// and question answers. With the Studio layout (the default) the island opens
// into the Studio frame on hover; that lives in studio_linux.cpp. See
// docs/linux.md for what isn't ported yet.
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib-unix.h>
#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <pango/pangocairo.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <cstring>
#include <ctime>
#include <mutex>
#include <cwchar>
#include <string>
#include <vector>

#include "../../agents/agent_view.h"
#include "../../agents/hook_installer.h"
#include "compat/d2d1.h"
#include "../../mascot/mascot.h"
#include "island_api.h"
#include "island_linux.h"
#include "../../island/studio_pages.h"
#include "mascot_png.h"
#include "mpris.h"
#include "platform.h"
#include "settings.h"
#include "lyrics_linux.h"
#include "sources.h"

namespace bb::lisland {

using bb::AgentPhase;
using bb::AgentView;
namespace mascot = bb::mascot;

// ── Layout (logical pixels; GTK scales for HiDPI) ───────────────────────────

// The window; the pill sits inside it. Room for the Studio frame (520 x 256) and
// the plan card (480 x 320), plus the nudge and the shadow.
constexpr double kCanvasW = 540, kCanvasH = 340;
constexpr double kTopGap = 6;                       // between the screen edge and the pill
constexpr double kCollapsedH = 36;
constexpr double kOpenDelay = 0.15;                 // the pointer rests this long before Studio opens

// ── State (all on the GTK main thread, except the agent mailbox) ────────────

GtkWidget* g_window = nullptr;
bool g_layerShell = false;
double g_scale = 1.0;       // Appearance.SizeScale
double g_offsetY = 0;       // Appearance.OffsetY
double g_opacity = 0.96;    // Themes.PillOpacity
double g_fill[3] = {0.035, 0.035, 0.045};  // the surface colour; Themes.SolidColor when Fill is Opaque
bool g_mascotOn = true;     // Bunbun.Mascot and MascotOnIsland
bool g_agentsOn = true;     // Bunbun.Agents
bool g_agentPill = true;    // Bunbun.AgentPill: the pill while an agent works (cards always show)
bool g_mediaOn = true;      // Modules.Media: what's playing
bool g_streamingMood = true;  // Bunbun.StreamingMood: a video shows the popcorn mascot and "Streaming"
bool g_expandOnHover = true;  // Behavior.ExpandOnHover; off: a click opens and closes the island
bool g_pinned = false;      // opened by a click (ExpandOnHover off)
std::string g_toastTitle, g_toastText;  // a reminder or the focus timer, until g_toastUntil
double g_toastUntil = 0;
bool g_clock24 = true;
bool g_showSeconds = false;

double g_w = 120, g_h = kCollapsedH;  // the pill's current size (animated)
double g_lastFrame = 0;
bool g_hover = false;
double g_hoverSince = 0;
double g_mouseX = -1, g_mouseY = -1;
std::vector<Hit> g_hits;
int g_hoverHit = -1;
guint g_timer = 0;
double g_lastDrawAt = 0;  // the watchdog in main() remaps the window if frames stop

bb::mpris::Track g_track;
GdkPixbuf* g_art = nullptr;
std::string g_artUrl;
GCancellable* g_artCancel = nullptr;

std::mutex g_agentMutex;
AgentView g_agentInbox;
bool g_agentInboxNew = false;
bool g_agentInboxNudge = false;
AgentView g_agent;
double g_nudgeAt = -10;

mascot::Mood g_mood = mascot::Mood::Idle;
double g_pokeUntil = 0;  // clicking the mascot makes it hop
double g_moodSince = 0;
ID2D1RenderTarget* g_rt = nullptr;  // the mascot draws through the Direct2D stand-ins

double Now() { return g_get_monotonic_time() / 1e6; }

void ScheduleFrame(bool fast);

// ── Settings ────────────────────────────────────────────────────────────────

void LoadSettings() {
    namespace s = bb::settings;
    s::Invalidate();
    g_scale = std::clamp(s::GetDouble(L"Appearance.SizeScale"), 0.6, 2.0);
    if (g_scale <= 0) g_scale = 1.0;
    g_offsetY = s::GetInt(L"Appearance.OffsetY");
    g_opacity = std::clamp(s::GetInt(L"Themes.PillOpacity"), 30, 100) / 100.0;
    g_fill[0] = 0.035; g_fill[1] = 0.035; g_fill[2] = 0.045;
    if (s::Get(L"Themes.IslandFill") == L"opaque") {
        // One steady colour, nothing showing through.
        g_opacity = 1.0;
        std::wstring hex = s::Get(L"Themes.SolidColor");
        if (!hex.empty() && hex[0] == L'#') hex.erase(0, 1);
        if (hex.size() == 3) hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
        const unsigned long v = hex.size() >= 6 ? std::wcstoul(hex.substr(0, 6).c_str(), nullptr, 16) : 0;
        g_fill[0] = ((v >> 16) & 0xFF) / 255.0;
        g_fill[1] = ((v >> 8) & 0xFF) / 255.0;
        g_fill[2] = (v & 0xFF) / 255.0;
    }
    g_mascotOn = s::GetBool(L"Bunbun.Mascot") && s::GetBool(L"Bunbun.MascotOnIsland");
    g_agentsOn = s::GetBool(L"Bunbun.Agents");
    g_agentPill = s::GetBool(L"Bunbun.AgentPill");
    g_mediaOn = s::GetBool(L"Modules.Media");
    g_streamingMood = s::GetBool(L"Bunbun.StreamingMood");
    g_expandOnHover = s::GetBool(L"Behavior.ExpandOnHover");
    if (g_expandOnHover) g_pinned = false;
    g_showSeconds = s::GetBool(L"Modules.ShowSeconds");
    const std::wstring fmt = s::Get(L"Modules.ClockFormat");
    if (fmt == L"12h") g_clock24 = false;
    else if (fmt == L"24h") g_clock24 = true;
    else {
        // "system": the locale's own time format says whether it uses AM/PM.
        char buf[16] = {};
        tm t{};
        t.tm_hour = 15;
        strftime(buf, sizeof buf, "%X", &t);
        g_clock24 = std::string(buf).find("15") != std::string::npos;
    }
    mascot::SetStyle(mascot::StyleFromSettings(s::Get(L"Bunbun.Character"), s::Get(L"Bunbun.MascotColor"),
                                               s::GetInt(L"Bunbun.MascotMotion"), s::GetInt(L"Bunbun.MascotHop"),
                                               s::GetInt(L"Bunbun.MascotBlinkEvery") * 10,
                                               s::GetBool(L"Bunbun.MascotEyesFollow"), s::GetBool(L"Bunbun.MascotCelebrate")));
    StudioLoadSettings();
    bb::llyrics::LoadSettings();
    bb::llyrics::OnTrack(g_track);  // lyrics just switched on: look this song up
}

// ── Drawing helpers ─────────────────────────────────────────────────────────

void RoundRect(cairo_t* cr, double x, double y, double w, double h, double r) {
    r = std::min({r, w / 2, h / 2});
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

double Text(cairo_t* cr, const std::string& text, double x, double y, double maxW, double size, bool bold,
            double r, double g, double b, double a, bool mono, int lines) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_from_string(mono ? "Monospace" : "Sans");
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text.c_str(), -1);
    if (maxW > 0) {
        pango_layout_set_width(layout, static_cast<int>(maxW * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        if (lines > 1) {
            pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            pango_layout_set_height(layout, -lines);
        }
    }
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
    return w;
}

double TextWidth(cairo_t* cr, const std::string& text, double size, bool bold) {
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text.c_str(), -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    pango_font_description_free(font);
    g_object_unref(layout);
    return w;
}

// Is the player showing a video rather than music? Same rule as Windows'
// IsVideoMedia: music apps are music; browsers report everything alike, so there a
// track with an album is music (YouTube Music, Spotify on the web) and one without
// is a video; known video players are video.
bool IsVideoTrack(const bb::mpris::Track& t) {
    std::string id = t.player;
    for (char& c : id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* m : {"spotify", "rhythmbox", "elisa", "lollypop", "amarok", "clementine", "strawberry", "audacious",
                          "deadbeef", "cmus", "quod libet", "youtube music", "youtube-music", "tidal", "deezer", "amberol",
                          "g4music", "tauon", "musikcube", "cider", "apple music"})
        if (id.find(m) != std::string::npos) return false;
    for (const char* b : {"firefox", "chrom", "brave", "edge", "vivaldi", "opera", "librewolf", "zen", "epiphany", "web"})
        if (id.find(b) != std::string::npos) return t.album.empty();
    for (const char* v : {"vlc", "mpv", "celluloid", "totem", "videos", "smplayer", "kodi", "stremio", "haruna", "dragon", "parole"})
        if (id.find(v) != std::string::npos) return true;
    return false;
}

// A video plays: the small island shows the mascot munching popcorn and "Streaming".
bool StreamingNow() { return g_streamingMood && g_mediaOn && g_track.active && IsVideoTrack(g_track); }

void DrawMascot(cairo_t* cr, double cx, double cy, double size) {
    if (!g_mascotOn) return;
    cairo_save(cr);
    bbcompat::SetCairo(g_rt, cr);
    // Eyes follow the pointer while it's over the island, else glance ahead.
    D2D1_POINT_2F look{0, 0};
    if (g_hover && g_mouseX >= 0) {
        look.x = static_cast<float>(std::clamp((g_mouseX - cx) / 80.0, -1.0, 1.0));
        look.y = static_cast<float>(std::clamp((g_mouseY - cy) / 40.0, -1.0, 1.0));
    }
    mascot::Draw(g_rt, D2D1::Point2F(static_cast<float>(cx), static_cast<float>(cy)), static_cast<float>(size), g_mood,
                 Now(), g_moodSince, look);
    cairo_restore(cr);
}

void Button(cairo_t* cr, const Rect& r, const std::string& label, bool primary, Action action, int index = -1) {
    const bool hovered = static_cast<int>(g_hits.size()) == g_hoverHit;
    RoundRect(cr, r.x, r.y, r.w, r.h, 9);
    if (primary) cairo_set_source_rgba(cr, 0.47, 0.78, 1.0, hovered ? 1.0 : 0.85);
    else cairo_set_source_rgba(cr, 1, 1, 1, hovered ? 0.22 : 0.13);
    cairo_fill(cr);
    const double tw = TextWidth(cr, label, 12, true);
    const double ink = primary ? 0.07 : 1.0;
    Text(cr, label, r.x + std::max(6.0, (r.w - tw) / 2), r.y + (r.h - 15) / 2, r.w - 12, 12, true, ink, ink, ink + (primary ? 0.01 : 0), 0.95);
    g_hits.push_back({r, action, index});
}

// Media control glyphs, drawn rather than taken from a font.
void Glyph(cairo_t* cr, Action a, double cx, double cy, double s, bool playing) {
    cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
    if (a == Action::PlayPause && playing) {
        cairo_rectangle(cr, cx - s * 0.45, cy - s * 0.5, s * 0.3, s);
        cairo_rectangle(cr, cx + s * 0.15, cy - s * 0.5, s * 0.3, s);
        cairo_fill(cr);
        return;
    }
    const double dir = a == Action::Previous ? -1 : 1;
    const auto tri = [&](double x0) {
        cairo_move_to(cr, x0 - dir * s * 0.35, cy - s * 0.5);
        cairo_line_to(cr, x0 + dir * s * 0.45, cy);
        cairo_line_to(cr, x0 - dir * s * 0.35, cy + s * 0.5);
        cairo_close_path(cr);
    };
    if (a == Action::PlayPause) tri(cx);
    else { tri(cx - dir * s * 0.3); tri(cx + dir * s * 0.45); }
    cairo_fill(cr);
}

void IconButton(cairo_t* cr, double cx, double cy, double size, Action a, bool playing) {
    const Rect r{cx - size / 2, cy - size / 2, size, size};
    if (static_cast<int>(g_hits.size()) == g_hoverHit) {
        cairo_arc(cr, cx, cy, size / 2, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.14);
        cairo_fill(cr);
    }
    Glyph(cr, a, cx, cy, size * 0.36, playing);
    g_hits.push_back({r, a});
}

void Art(cairo_t* cr, double x, double y, double size, double alpha) {
    if (!g_art || alpha <= 0) return;
    cairo_save(cr);
    RoundRect(cr, x, y, size, size, size * 0.22);
    cairo_clip(cr);
    const double k = size / std::max(gdk_pixbuf_get_width(g_art), gdk_pixbuf_get_height(g_art));
    cairo_translate(cr, x, y);
    cairo_scale(cr, k, k);
    gdk_cairo_set_source_pixbuf(cr, g_art, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

// Bars that bob while music plays. Not driven by the audio yet (see docs/linux.md).
void Bars(cairo_t* cr, double x, double cy, double h) {
    const double t = Now();
    cairo_set_source_rgba(cr, 0.47, 0.78, 1.0, 0.9);
    for (int i = 0; i < 4; ++i) {
        const double v = g_track.playing ? 0.35 + 0.65 * std::fabs(std::sin(t * (3.1 + i * 0.9) + i * 1.7)) : 0.25;
        RoundRect(cr, x + i * 5, cy - h * v / 2, 3, h * v, 1.5);
        cairo_fill(cr);
    }
}

std::string ClockText() {
    const time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    char buf[32];
    if (g_clock24) strftime(buf, sizeof buf, g_showSeconds ? "%H:%M:%S" : "%H:%M", &lt);
    else strftime(buf, sizeof buf, g_showSeconds ? "%l:%M:%S %p" : "%l:%M %p", &lt);
    std::string s = buf;
    while (!s.empty() && s[0] == ' ') s.erase(0, 1);
    return s;
}

std::string AgentLine(const AgentView& a) {
    std::string who = bb::plat::Utf8(a.agent.empty() ? L"Agent" : a.agent);
    if (!a.project.empty()) who += " \xC2\xB7 " + bb::plat::Utf8(a.project);
    return who;
}

// ── What to show ────────────────────────────────────────────────────────────

enum class Kind { Clock, Media, Agent, AgentCard, Studio, RichCard, Toast };

Kind CurrentKind() {
    Kind k = Kind::Clock;
    const bool toast = Now() < g_toastUntil && !Composing() && !g_hover;
    if (g_agentsOn && g_agent.phase != AgentPhase::None) {
        if (g_agent.phase == AgentPhase::Approval || g_agent.phase == AgentPhase::Question)
            return RichCardShown() ? Kind::RichCard : Kind::AgentCard;
        if (g_agentPill) k = Kind::Agent;
    }
    if (toast) return Kind::Toast;
    if (k == Kind::Clock && g_track.active && g_mediaOn) k = Kind::Media;
    // Studio: the frame opens while the pointer rests on the island, and stays
    // open while you type a reply into it.
    const bool open = g_pinned || (g_expandOnHover && g_hover && Now() - g_hoverSince >= kOpenDelay);
    if (StudioLayout() && (Composing() || open)) return Kind::Studio;
    return k;
}

mascot::Mood MoodFor(Kind k) {
    const bool agentShown = g_agentsOn && g_agent.phase != AgentPhase::None;
    if (k == Kind::Agent || k == Kind::AgentCard || k == Kind::RichCard || (k == Kind::Studio && agentShown)) {
        switch (g_agent.phase) {
            case AgentPhase::Thinking: return mascot::Mood::Thinking;
            case AgentPhase::Working: return mascot::Mood::Working;
            case AgentPhase::Approval:
            case AgentPhase::Question: return mascot::Mood::Alert;
            case AgentPhase::Finished: return mascot::Mood::Happy;
            case AgentPhase::Error: return mascot::Mood::Sad;
            default: break;
        }
    }
    if ((k == Kind::Media || k == Kind::Studio) && g_track.playing)
        return StreamingNow() ? mascot::Mood::Streaming : mascot::Mood::Music;
    if (k == Kind::Toast || Now() < g_pokeUntil) return mascot::Mood::Happy;
    return mascot::Mood::Idle;
}

// How many lines the card's detail takes (at most 3, or 2 above a diff).
int CardLines(cairo_t* cr, const AgentView& a) {
    if (a.detail.empty()) return 0;
    const int most = a.diffOld.empty() && a.diffNew.empty() ? 3 : 2;
    const double width = TextWidth(cr, bb::plat::Utf8(a.detail), 12, false);
    return std::clamp(static_cast<int>(std::ceil(width / (460 - 32 - 30))), 1, most);  // a little slack for mono / wrapping
}

// The pill's target size for what it shows.
void TargetSize(cairo_t* cr, Kind k, double* w, double* h) {
    switch (k) {
        case Kind::Clock:
            *w = 30 + TextWidth(cr, ClockText(), 14, true) + (g_mascotOn ? 34 : 0);
            *h = kCollapsedH;
            return;
        case Kind::Media:
            if (g_pinned || (g_expandOnHover && g_hover)) { *w = 380; *h = 96; return; }
            if (StreamingNow()) {  // the mascot, "Streaming" and the bars
                *w = 44 + TextWidth(cr, "Streaming", 12.5, true) + 44;
                *h = kCollapsedH;
                return;
            }
            // Lyrics on the small island: as wide as the song's longest line needs.
            if (const double lw = LyricPillWidth(cr, 38 + 40); lw > 0) { *w = lw; *h = kCollapsedH; return; }
            *w = 36 + std::min(200.0, TextWidth(cr, g_track.title, 12.5, true)) + 44;
            *h = kCollapsedH;
            return;
        case Kind::Agent:
            *w = g_agent.phase == AgentPhase::Finished || g_agent.phase == AgentPhase::Error ? 320 : 400;
            *h = 44;
            return;
        case Kind::AgentCard: {
            *w = 460;
            double body = 18.0 * CardLines(cr, g_agent);
            if (!g_agent.diffOld.empty() || !g_agent.diffNew.empty()) body += 40;
            *h = 54 + body + 52;
            return;
        }
        case Kind::Studio:
            *w = bb::studio::kIslandW;
            *h = bb::studio::kIslandH;
            return;
        case Kind::RichCard:
            RichCardSize(cr, w, h);
            return;
        case Kind::Toast:
            *w = std::clamp(56 + std::max(TextWidth(cr, g_toastTitle, 11.5, true), TextWidth(cr, g_toastText, 13.5, true)) + 22, 200.0, 440.0);
            *h = 50;
            return;
    }
}

// ── Painting ────────────────────────────────────────────────────────────────

void PaintClock(cairo_t* cr, const Rect& p) {
    double x = p.x + 14;
    if (g_mascotOn) {
        DrawMascot(cr, x + 13, p.y + p.h / 2, 30);
        x += 34;
    }
    Text(cr, ClockText(), x, p.y + (p.h - 18) / 2, p.w - (x - p.x) - 10, 14, true);
}

void PaintMedia(cairo_t* cr, const Rect& p) {
    const bool expanded = p.h > 60;
    if (!expanded && StreamingNow()) {
        // A video: no cover or title, just the mascot with its popcorn, "Streaming"
        // and the bars (still in the accent colour).
        DrawMascot(cr, p.x + 22, p.y + p.h / 2, 34);
        Text(cr, "Streaming", p.x + 44, p.y + (p.h - 16) / 2, p.w - 44 - 40, 12.5, true);
        Bars(cr, p.x + p.w - 30, p.y + p.h / 2, 16);
        return;
    }
    if (!expanded) {
        // The owner's rule: art and the music-vibing mascot take turns, 3.5 s each.
        const double phase = std::fmod(Now(), 7.0);
        double artAlpha = 0;
        if (g_art) artAlpha = g_mascotOn ? (phase < 3.5 ? std::clamp(std::min(phase, 3.5 - phase) / 0.35, 0.0, 1.0) : 0.0) : 1.0;
        if (artAlpha < 1.0) DrawMascot(cr, p.x + 21, p.y + p.h / 2, 28);  // under the art while it fades
        Art(cr, p.x + 9, p.y + 5, p.h - 10, artAlpha);
        if (bb::llyrics::PillOn()) PaintLyricPill(cr, {p.x + 38, p.y, p.w - 38 - 40, p.h});
        else Text(cr, g_track.title.empty() ? g_track.player : g_track.title, p.x + 38, p.y + (p.h - 16) / 2, p.w - 38 - 40, 12.5,
                  true);
        Bars(cr, p.x + p.w - 30, p.y + p.h / 2, 16);
        return;
    }
    const double art = p.h - 24;
    if (g_art) Art(cr, p.x + 12, p.y + 12, art, 1.0);
    else {
        RoundRect(cr, p.x + 12, p.y + 12, art, art, art * 0.22);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
        cairo_fill(cr);
        DrawMascot(cr, p.x + 12 + art / 2, p.y + 12 + art / 2, art * 0.9);
    }
    const double tx = p.x + 24 + art, tw = p.w - (tx - p.x) - 14;
    Text(cr, g_track.title, tx, p.y + 13, tw, 14, true);
    Text(cr, g_track.artist.empty() ? g_track.player : g_track.artist, tx, p.y + 33, tw, 12, false, 1, 1, 1, 0.65);
    const double cy = p.y + p.h - 24;
    IconButton(cr, tx + 16, cy, 30, Action::Previous, g_track.playing);
    IconButton(cr, tx + 56, cy, 34, Action::PlayPause, g_track.playing);
    IconButton(cr, tx + 96, cy, 30, Action::Next, g_track.playing);
    Bars(cr, p.x + p.w - 34, cy, 18);
}

void PaintAgent(cairo_t* cr, const Rect& p) {
    DrawMascot(cr, p.x + 24, p.y + p.h / 2, 32);
    const double x = p.x + 46, w = p.w - 46 - 14;
    const AgentView& a = g_agent;
    std::string status;
    switch (a.phase) {
        case AgentPhase::Thinking: status = "Thinking\xE2\x80\xA6"; break;
        case AgentPhase::Finished: status = "Done"; break;
        case AgentPhase::Error: status = "Hit an error"; break;
        default: status = a.step.empty() ? "Working\xE2\x80\xA6" : bb::plat::Utf8(a.step);
    }
    Text(cr, AgentLine(a), x, p.y + 5, w, 11.5, true, 1, 1, 1, 0.7);
    Text(cr, status, x, p.y + 22, w - 70, 12.5, true);
    if (a.added >= 0 || a.removed >= 0) {
        const std::string add = "+" + std::to_string(std::max(0, a.added)), del = "\xE2\x88\x92" + std::to_string(std::max(0, a.removed));
        const double dw = TextWidth(cr, del, 11.5, true);
        Text(cr, del, p.x + p.w - 14 - dw, p.y + 24, 0, 11.5, true, 1.0, 0.45, 0.45);
        Text(cr, add, p.x + p.w - 20 - dw - TextWidth(cr, add, 11.5, true), p.y + 24, 0, 11.5, true, 0.45, 0.9, 0.55);
    }
    if (a.sessions > 1) Text(cr, std::to_string(a.sessions) + " agents", p.x + p.w - 74, p.y + 5, 60, 10.5, false, 1, 1, 1, 0.5);
}

void PaintCard(cairo_t* cr, const Rect& p) {
    const AgentView& a = g_agent;
    DrawMascot(cr, p.x + 28, p.y + 28, 36);
    const double x = p.x + 54, w = p.w - 54 - 16;
    Text(cr, AgentLine(a), x, p.y + 10, w, 11.5, true, 1, 1, 1, 0.65);
    const std::string head = a.phase == AgentPhase::Question ? "Has a question" : bb::plat::Utf8(a.tool.empty() ? L"Needs your OK" : a.tool);
    Text(cr, head, x, p.y + 27, w, 14, true);
    double y = p.y + 54;
    const int lines = CardLines(cr, a);
    if (!a.detail.empty()) {
        Text(cr, bb::plat::Utf8(a.detail), p.x + 16, y, p.w - 32, 12, false, 1, 1, 1, 0.85, a.phase == AgentPhase::Approval, lines);
        y += 18.0 * lines;
    }
    if (!a.diffOld.empty() || !a.diffNew.empty()) {
        Text(cr, "\xE2\x88\x92 " + bb::plat::Utf8(a.diffOld), p.x + 16, y + 2, p.w - 32, 11, false, 1.0, 0.5, 0.5, 0.95, true);
        Text(cr, "+ " + bb::plat::Utf8(a.diffNew), p.x + 16, y + 20, p.w - 32, 11, false, 0.5, 0.92, 0.6, 0.95, true);
        y += 40;
    }
    const double bh = 32, gap = 8, by = p.y + p.h - bh - 12, left = p.x + 14, right = p.x + p.w - 14;
    if (a.phase == AgentPhase::Question && !a.options.empty()) {
        const int n = static_cast<int>(std::min<size_t>(a.options.size(), 4));
        const double bw = (right - left - gap * (n - 1)) / n;
        for (int i = 0; i < n; ++i)
            Button(cr, {left + i * (bw + gap), by, bw, bh}, bb::plat::Utf8(a.options[i]), i == 0, Action::Answer, i);
    } else if (a.phase == AgentPhase::Question) {
        // Several questions, multi-select or free text (AgentView::questions): there's
        // no card for those on Linux yet, and Allow would send no answers.
        Button(cr, {left, by, right - left, bh}, "Answer in terminal", true, Action::Terminal);
    } else {
        const double bw = (right - left - gap * 2) / 3;
        Button(cr, {left, by, bw, bh}, "Allow", true, Action::Allow);
        Button(cr, {left + bw + gap, by, bw, bh}, "Deny", false, Action::Deny);
        Button(cr, {left + 2 * (bw + gap), by, bw, bh}, "In terminal", false, Action::Terminal);
    }
    if (a.queued > 0) Text(cr, "+" + std::to_string(a.queued) + " waiting", p.x + p.w - 90, p.y + 10, 76, 10.5, false, 1, 1, 1, 0.5);
}

void PaintToast(cairo_t* cr, const Rect& p) {
    DrawMascot(cr, p.x + 26, p.y + p.h / 2, 32);
    const double x = p.x + 50, w = p.w - 50 - 16;
    Text(cr, g_toastTitle, x, p.y + 7, w, 11.5, true, 1, 1, 1, 0.65);
    Text(cr, g_toastText, x, p.y + 24, w, 13.5, true);
}

// Where the pill is in the window this frame.
Rect PillRect() {
    return {(kCanvasW - g_w) / 2, kTopGap, g_w, g_h};
}

// The pill as a region of pixel rows, its corners rounded with `radius`.
cairo_region_t* PillRegion(const Rect& p, double radius) {
    cairo_region_t* region = cairo_region_create();
    const int x0 = static_cast<int>(std::floor(p.x)), y0 = static_cast<int>(std::floor(p.y));
    const int w = static_cast<int>(std::ceil(p.w)), h = static_cast<int>(std::ceil(p.h));
    for (int y = 0; y < h; ++y) {
        const double dy = std::max({0.0, radius - (y + 0.5), (y + 0.5) - (h - radius)});
        const int inset = dy > 0 ? static_cast<int>(std::ceil(radius - std::sqrt(std::max(0.0, radius * radius - dy * dy)))) : 0;
        cairo_rectangle_int_t row{x0 + inset, y0 + y, w - 2 * inset, 1};
        if (row.width > 0) cairo_region_union_rectangle(region, &row);
    }
    return region;
}

void UpdateInputRegion(const Rect& p, double radius) {
    if (!gtk_widget_get_realized(g_window)) return;
    cairo_rectangle_int_t r{static_cast<int>(std::floor(p.x)) - 2, static_cast<int>(std::floor(p.y)) - 2,
                            static_cast<int>(std::ceil(p.w)) + 4, static_cast<int>(std::ceil(p.h)) + 4};
    cairo_region_t* region = cairo_region_create_rectangle(&r);
    gtk_widget_input_shape_combine_region(g_window, region);
    cairo_region_destroy(region);
    // X11 without a compositor can't show transparency: the whole window would
    // be a black box. Cut the window itself to the pill's shape instead.
    if (!g_layerShell) {
        if (gdk_screen_is_composited(gtk_widget_get_screen(g_window))) {
            gtk_widget_shape_combine_region(g_window, nullptr);
        } else {
            cairo_region_t* shape = PillRegion(p, radius);
            gtk_widget_shape_combine_region(g_window, shape);
            cairo_region_destroy(shape);
        }
    }
}

gboolean OnDraw(GtkWidget*, cairo_t* cr, gpointer) {
    const double now = Now();
    g_lastDrawAt = now;
    const double dt = std::clamp(now - g_lastFrame, 0.0, 0.1);
    g_lastFrame = now;

    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_scale(cr, g_scale, g_scale);

    const Kind kind = CurrentKind();
    const mascot::Mood mood = MoodFor(kind);
    if (mood != g_mood) {
        g_mood = mood;
        g_moodSince = now;
    }

    double tw = 0, th = 0;
    TargetSize(cr, kind, &tw, &th);
    const double k = 1.0 - std::exp(-dt * 16.0);  // ease toward the target, ~0.2 s
    g_w += (tw - g_w) * k;
    g_h += (th - g_h) * k;
    if (std::fabs(tw - g_w) < 0.3) g_w = tw;
    if (std::fabs(th - g_h) < 0.3) g_h = th;
    const bool resizing = g_w != tw || g_h != th;

    Rect p = PillRect();
    // The nudge: a small bounce when a new agent event arrives.
    const double since = now - g_nudgeAt;
    if (since < 0.45) {
        const double bump = std::sin(since / 0.45 * M_PI) * 4;
        p.x -= bump;
        p.w += bump * 2;
    }

    // The surface, with a soft shadow. The open island gets rounder corners.
    const double radius = std::min(p.h / 2, p.h > 150 ? 32.0 : 22.0);
    for (int i = 3; i >= 1; --i) {
        RoundRect(cr, p.x - i, p.y - i + 2, p.w + 2 * i, p.h + 2 * i, radius + i);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.07);
        cairo_fill(cr);
    }
    RoundRect(cr, p.x, p.y, p.w, p.h, radius);
    cairo_set_source_rgba(cr, g_fill[0], g_fill[1], g_fill[2], g_opacity);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    g_hits.clear();
    cairo_save(cr);
    RoundRect(cr, p.x, p.y, p.w, p.h, radius);
    cairo_clip(cr);
    // Content appears once the pill is close to its size, so it never spills.
    if (!resizing || (std::fabs(tw - g_w) < 24 && std::fabs(th - g_h) < 16)) {
        switch (kind) {
            case Kind::Clock: PaintClock(cr, p); break;
            case Kind::Media: PaintMedia(cr, p); break;
            case Kind::Agent: PaintAgent(cr, p); break;
            case Kind::AgentCard: PaintCard(cr, p); break;
            case Kind::Studio:
                PaintStudio(cr, p, std::clamp((g_h - 120.0) / (bb::studio::kIslandH - 120.0), 0.0, 1.0));
                break;
            case Kind::RichCard: PaintRichCard(cr, p); break;
            case Kind::Toast: PaintToast(cr, p); break;
        }
    }
    cairo_restore(cr);

    Rect scaled{p.x * g_scale, p.y * g_scale, p.w * g_scale, p.h * g_scale};
    UpdateInputRegion(scaled, radius * g_scale);

    // Fast frames while something moves; the mascot's idle motion is fine at ~8 fps.
    const bool fast = resizing || since < 0.45 || kind == Kind::Media || kind == Kind::AgentCard || kind == Kind::Studio ||
                      kind == Kind::RichCard ||
                      mascot::NeedsFastFrames(g_mood, now, g_moodSince) || g_hover;
    ScheduleFrame(fast);
    return TRUE;
}

gboolean OnTimer(gpointer) {
    g_timer = 0;
    if (g_window) gtk_widget_queue_draw(g_window);
    return G_SOURCE_REMOVE;
}

void ScheduleFrame(bool fast) {
    if (g_timer) return;
    g_timer = g_timeout_add(fast ? 16 : 125, OnTimer, nullptr);
}

void Redraw() {
    if (g_timer) {
        g_source_remove(g_timer);
        g_timer = 0;
    }
    if (g_window) gtk_widget_queue_draw(g_window);
}

// ── Album art ───────────────────────────────────────────────────────────────

void SetArt(GdkPixbuf* pb) {
    if (g_art) g_object_unref(g_art);
    g_art = nullptr;
    if (pb) {
        const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb), side = std::min(w, h);
        // A centred square, at most 192 px: covers are often 1000+ px.
        GdkPixbuf* square = gdk_pixbuf_new_subpixbuf(pb, (w - side) / 2, (h - side) / 2, side, side);
        g_art = gdk_pixbuf_scale_simple(square, std::min(side, 192), std::min(side, 192), GDK_INTERP_BILINEAR);
        g_object_unref(square);
    }
    Redraw();
}

void OnArtLoaded(GObject* src, GAsyncResult* res, gpointer) {
    char* data = nullptr;
    gsize len = 0;
    GError* err = nullptr;
    if (!g_file_load_contents_finish(G_FILE(src), res, &data, &len, nullptr, &err)) {
        if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            Wh_Log(L"art: %ls", bb::plat::Wide(err ? err->message : "load failed").c_str());
        g_clear_error(&err);
        return;
    }
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    GdkPixbuf* pb = nullptr;
    if (gdk_pixbuf_loader_write(loader, reinterpret_cast<guchar*>(data), len, &err) && gdk_pixbuf_loader_close(loader, &err))
        pb = gdk_pixbuf_loader_get_pixbuf(loader);
    else
        gdk_pixbuf_loader_close(loader, nullptr);
    if (err) Wh_Log(L"art: %ls", bb::plat::Wide(err->message).c_str());
    g_clear_error(&err);
    SetArt(pb);
    g_object_unref(loader);
    g_free(data);
}

void LoadArt(const std::string& url) {
    if (url == g_artUrl) return;
    g_artUrl = url;
    if (g_artCancel) {
        g_cancellable_cancel(g_artCancel);
        g_object_unref(g_artCancel);
        g_artCancel = nullptr;
    }
    SetArt(nullptr);
    if (url.empty()) return;
    // file:// always works; http(s):// needs GVfs, which GNOME and KDE desktops have.
    GFile* file = g_file_new_for_uri(url.c_str());
    g_artCancel = g_cancellable_new();
    g_file_load_contents_async(file, g_artCancel, OnArtLoaded, nullptr);
    g_object_unref(file);
}

void OnMediaChanged() {
    g_track = bb::mpris::Current();
    LoadArt(g_track.active ? g_track.artUrl : std::string());
    bb::llyrics::OnTrack(g_track);
    Redraw();
}

// ── Agents ──────────────────────────────────────────────────────────────────

gboolean ApplyAgentView(gpointer) {
    std::lock_guard lock(g_agentMutex);
    if (!g_agentInboxNew) return G_SOURCE_REMOVE;
    g_agentInboxNew = false;
    if (g_agentInbox.generation < g_agent.generation && g_agentInbox.generation != 0) return G_SOURCE_REMOVE;
    g_agent = g_agentInbox;
    if (g_agentInboxNudge) g_nudgeAt = Now();
    g_agentInboxNudge = false;
    Redraw();
    return G_SOURCE_REMOVE;
}

// ── Input ───────────────────────────────────────────────────────────────────

int HitAt(double x, double y) {
    x /= g_scale;
    y /= g_scale;
    for (size_t i = 0; i < g_hits.size(); ++i)
        if (g_hits[i].r.Contains(x, y)) return static_cast<int>(i);
    return -1;
}

void OpenSettings();
void ShowMenu(GdkEvent* event);

void Do(const Hit& h) {
    switch (h.action) {
        case Action::PlayPause: bb::mpris::PlayPause(); break;
        case Action::Next: bb::mpris::Next(); break;
        case Action::Previous: bb::mpris::Previous(); break;
        case Action::Allow: bb::agents::Decide(g_agent.requestId, bb::AgentDecision::Allow); break;
        case Action::Deny: bb::agents::Decide(g_agent.requestId, bb::AgentDecision::Deny); break;
        case Action::Terminal: bb::agents::Decide(g_agent.requestId, bb::AgentDecision::Terminal); break;
        case Action::Answer: bb::agents::Decide(g_agent.requestId, bb::AgentDecision::Allow, h.index); break;
        case Action::Settings: OpenSettings(); break;
        case Action::None: break;
        default: StudioDo(h); break;
    }
}

gboolean OnButtonPress(GtkWidget*, GdkEventButton* e, gpointer) {
    if (e->type != GDK_BUTTON_PRESS) return TRUE;
    if (e->button == 3) {
        ShowMenu(reinterpret_cast<GdkEvent*>(e));
        return TRUE;
    }
    if (e->button != 1) return TRUE;
    const int i = HitAt(e->x, e->y);
    const Kind kind = CurrentKind();
    if (kind == Kind::Toast && i < 0) g_toastUntil = 0;  // a click puts it away
    else if (i >= 0) Do(g_hits[i]);
    // Expand on hover off: a click on the island opens it, another closes it.
    else if (!g_expandOnHover && kind != Kind::AgentCard && kind != Kind::RichCard) g_pinned = !g_pinned;
    else if (kind == Kind::Clock) g_pokeUntil = Now() + 1.2;  // a poke
    Redraw();
    return TRUE;
}

gboolean OnMotion(GtkWidget*, GdkEventMotion* e, gpointer) {
    g_mouseX = e->x / g_scale;
    g_mouseY = e->y / g_scale;
    const int hit = HitAt(e->x, e->y);
    if (hit != g_hoverHit) {
        g_hoverHit = hit;
        Redraw();
    }
    return FALSE;
}

gboolean OnCrossing(GtkWidget*, GdkEventCrossing* e, gpointer) {
    // Moving onto a child area isn't leaving the island.
    if (e->detail == GDK_NOTIFY_INFERIOR) return FALSE;
    const bool entering = e->type == GDK_ENTER_NOTIFY;
    if (entering && !g_hover) {
        g_hoverSince = Now();
        // Over a working agent, the frame opens on the Agent page.
        if (g_agentsOn && g_agent.phase != AgentPhase::None) StudioShowAgentPage();
        // Studio opens after kOpenDelay; make sure a frame comes then.
        g_timeout_add(static_cast<guint>(kOpenDelay * 1000) + 10, [](gpointer) -> gboolean { Redraw(); return G_SOURCE_REMOVE; }, nullptr);
    }
    g_hover = entering;
    if (!g_hover) {
        g_hoverHit = -1;
        g_mouseX = g_mouseY = -1;
    }
    Redraw();
    return FALSE;
}

gboolean OnScroll(GtkWidget*, GdkEventScroll* e, gpointer) {
    double delta = 0;
    switch (e->direction) {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT: delta = -1; break;
        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT: delta = 1; break;
        case GDK_SCROLL_SMOOTH: delta = std::fabs(e->delta_x) > std::fabs(e->delta_y) ? e->delta_x : e->delta_y; break;
    }
    const Kind k = CurrentKind();
    if (k == Kind::Studio || k == Kind::RichCard) StudioScroll(delta);
    Redraw();
    return TRUE;
}

gboolean OnKey(GtkWidget*, GdkEventKey* e, gpointer) {
    const bool used = ComposeKey(e);
    if (used) Redraw();
    return used;
}

// ── Menu (the tray menu's items; there's no tray on Linux yet) ──────────────

void OpenSettings() {
    // bunbun-settings needs WebKitGTK. If it can't start (exit 127: a library
    // is missing), open settings.ini in the default editor instead.
    const std::string exe = bb::plat::ExeDir() + "/bunbun-settings";
    bb::settings::EnsureFile();
    if (!bb::plat::FileExists(exe)) {
        bb::plat::OpenWithDefault(bb::settings::Path());
        return;
    }
    gchar* argv[] = {const_cast<gchar*>(exe.c_str()), nullptr};
    GPid pid = 0;
    if (!g_spawn_async(nullptr, argv, nullptr, G_SPAWN_DO_NOT_REAP_CHILD, nullptr, nullptr, &pid, nullptr)) {
        bb::plat::OpenWithDefault(bb::settings::Path());
        return;
    }
    g_child_watch_add(pid, [](GPid child, gint status, gpointer) {
        g_spawn_close_pid(child);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) bb::plat::OpenWithDefault(bb::settings::Path());
    }, nullptr);
}

void ShowMenu(GdkEvent* event) {
    GtkWidget* menu = gtk_menu_new();
    const auto item = [&](const char* label, GCallback cb) {
        GtkWidget* it = gtk_menu_item_new_with_label(label);
        g_signal_connect(it, "activate", cb, nullptr);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
    };
    item("Settings\xE2\x80\xA6", G_CALLBACK(+[](GtkMenuItem*, gpointer) { OpenSettings(); }));
    if (bb::hooks::ClaudeHooksInstalled())
        item("Disconnect Claude Code", G_CALLBACK(+[](GtkMenuItem*, gpointer) {
            std::wstring report;
            bb::hooks::UninstallClaudeHooks(&report);
            Wh_Log(L"%ls", report.c_str());
        }));
    else
        item("Connect Claude Code", G_CALLBACK(+[](GtkMenuItem*, gpointer) {
            std::wstring report;
            bb::hooks::InstallClaudeHooks(&report);
            Wh_Log(L"%ls", report.c_str());
        }));
    // Lyrics on the small island, as on Windows' right-click menu.
    GtkWidget* lyrics = gtk_check_menu_item_new_with_label("Lyrics on the island");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(lyrics), bb::llyrics::PillOn());
    g_signal_connect(lyrics, "toggled", G_CALLBACK(+[](GtkCheckMenuItem* it, gpointer) {
        const std::wstring on = L"true";
        bb::settings::Set(L"Bunbun.PillLyrics", gtk_check_menu_item_get_active(it) ? &on : nullptr);
    }), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), lyrics);
    item("Open data folder", G_CALLBACK(+[](GtkMenuItem*, gpointer) {
        bb::plat::MakeDirs(bb::plat::ConfigDir());
        bb::plat::OpenWithDefault(bb::plat::ConfigDir());
    }));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    item("Quit Bunbun", G_CALLBACK(+[](GtkMenuItem*, gpointer) { gtk_main_quit(); }));
    gtk_widget_show_all(menu);
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell* m, gpointer) {
        g_idle_add([](gpointer w) -> gboolean { gtk_widget_destroy(GTK_WIDGET(w)); return G_SOURCE_REMOVE; }, m);
    }), nullptr);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
}

// ── Window placement ────────────────────────────────────────────────────────

void PlaceWindow() {
    const int w = static_cast<int>(std::ceil(kCanvasW * g_scale)), h = static_cast<int>(std::ceil(kCanvasH * g_scale));
    gtk_widget_set_size_request(g_window, w, h);
    gtk_window_resize(GTK_WINDOW(g_window), w, h);
    if (g_layerShell) {
        gtk_layer_set_margin(GTK_WINDOW(g_window), GTK_LAYER_SHELL_EDGE_TOP, static_cast<int>(g_offsetY));
        return;
    }
    GdkDisplay* display = gtk_widget_get_display(g_window);
    GdkMonitor* mon = gdk_display_get_primary_monitor(display);
    if (!mon) mon = gdk_display_get_monitor(display, 0);
    GdkRectangle area{0, 0, 1920, 1080};
    if (mon) gdk_monitor_get_workarea(mon, &area);
    gtk_window_move(GTK_WINDOW(g_window), area.x + (area.width - w) / 2, area.y + static_cast<int>(g_offsetY));
}

// X11: a window mapped while the window manager or compositor is still
// starting (an autostart race at login) can end up never getting exposed;
// mapping it again fixes that. Also used when compositing turns on or off.
void Remap() {
    if (g_layerShell || !g_window) return;
    gtk_widget_hide(g_window);
    gtk_widget_show(g_window);
    PlaceWindow();
    Redraw();
}

void OnSettingsFileChanged(GFileMonitor*, GFile*, GFile*, GFileMonitorEvent ev, gpointer) {
    if (ev != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT && ev != G_FILE_MONITOR_EVENT_CREATED &&
        ev != G_FILE_MONITOR_EVENT_RENAMED && ev != G_FILE_MONITOR_EVENT_MOVED_IN)
        return;
    LoadSettings();
    PlaceWindow();
    Redraw();
}

// ── Single instance ─────────────────────────────────────────────────────────

std::string LockPath() { return bb::plat::RuntimeDir() + "/island.lock"; }

// Holds an flock for the life of the process; the file names our pid for --quit.
bool TakeInstanceLock() {
    const int fd = open(LockPath().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return false;
    }
    if (ftruncate(fd, 0) == 0) {
        const std::string pid = std::to_string(getpid()) + "\n";
        if (write(fd, pid.data(), pid.size()) < 0) {}
    }
    return true;  // fd stays open on purpose
}

// Asks a running island to quit (SIGTERM) and waits up to 3 s for it.
bool QuitRunning() {
    std::string text;
    if (!bb::plat::ReadFile(LockPath(), &text)) return false;
    const pid_t pid = static_cast<pid_t>(atoi(text.c_str()));
    if (pid <= 1 || pid == getpid() || kill(pid, SIGTERM) != 0) return false;
    for (int i = 0; i < 60; ++i) {
        if (kill(pid, 0) != 0) return true;
        usleep(50 * 1000);
    }
    return false;
}

// Under the AppImage, our folder is a new temporary mount every run; agents'
// hooks must point at a copy of bunbun-hook that stays put.
void InstallStableHook() {
    if (!getenv("APPIMAGE")) return;
    const std::string dst = bb::plat::HomeDir() + "/.local/share/Bunbun/bin/bunbun-hook";
    std::string data;
    if (!bb::plat::ReadFile(bb::plat::ExeDir() + "/bunbun-hook", &data)) return;
    std::string old;
    if (bb::plat::ReadFile(dst, &old) && old == data) return;
    if (bb::plat::WriteFileAtomic(dst, data)) chmod(dst.c_str(), 0755);
}

void ShowToast(const std::string& title, const std::string& text, double seconds) {
    g_toastTitle = title;
    g_toastText = text;
    g_toastUntil = Now() + seconds;
    g_nudgeAt = Now();
    Redraw();
}

}  // namespace bb::lisland

using namespace bb::lisland;

// ── Entry points used by shared code ────────────────────────────────────────

double IslandNow() { return Now(); }

void IslandSetAgentView(const AgentView& view, bool nudge) {
    {
        std::lock_guard lock(g_agentMutex);
        g_agentInbox = view;
        g_agentInboxNew = true;
        g_agentInboxNudge = g_agentInboxNudge || nudge;
    }
    g_idle_add(ApplyAgentView, nullptr);
}

int IslandCapturePages() {
    struct Job {
        std::mutex m;
        std::condition_variable cv;
        bool done = false;
        int count = 0;
    };
    auto job = std::make_shared<Job>();
    g_idle_add([](gpointer p) -> gboolean {
        auto* j = static_cast<std::shared_ptr<Job>*>(p);
        const int count = StudioCapturePages();
        {
            std::lock_guard lock((*j)->m);
            (*j)->count = count;
            (*j)->done = true;
        }
        (*j)->cv.notify_all();
        delete j;
        return G_SOURCE_REMOVE;
    }, new std::shared_ptr<Job>(job));
    std::unique_lock lock(job->m);
    return job->cv.wait_for(lock, std::chrono::seconds(5), [&] { return job->done; }) ? job->count : -1;
}

int main(int argc, char** argv) {
    bool replace = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--quit") return QuitRunning() ? 0 : 1;
        if (a == "--replace") replace = true;
        // The desktop file's Settings action. Exec keys can't hold shell code, so
        // the island finds bunbun-settings next to itself.
        if (a == "--settings") {
            const std::string exe = bb::plat::ExeDir() + "/bunbun-settings";
            bb::settings::EnsureFile();
            execl(exe.c_str(), exe.c_str(), static_cast<char*>(nullptr));
            return bb::plat::OpenWithDefault(bb::settings::Path()) ? 0 : 1;
        }
        if (a == "--version") {
            printf("Bunbun %s\n", BUNBUN_VERSION_STR);
            return 0;
        }
        if (a == "--help" || a == "-h") {
            printf("Usage: bunbun [--replace | --quit | --settings | --version | --render-icon FILE.png [SIZE]]\n");
            return 0;
        }
        // Packaging draws the app icon with the real mascot code (Bun, idle).
        if (a == "--render-icon" && i + 1 < argc) {
            const int px = i + 2 < argc ? std::clamp(atoi(argv[i + 2]), 16, 1024) : 256;
            const std::string png = bb::plat::RenderMascotPng(mascot::Style{}, mascot::Mood::Idle, px, 0.6, -10, {0, 0});
            return !png.empty() && bb::plat::WriteFileAtomic(argv[i + 1], png) ? 0 : 1;
        }
    }
    if (replace) QuitRunning();
    if (!TakeInstanceLock()) {
        fprintf(stderr, "Bunbun is already running (bunbun --replace restarts it).\n");
        return 0;
    }

    gtk_init(&argc, &argv);
    GdkDisplay* display = gdk_display_get_default();
#ifdef GDK_WINDOWING_WAYLAND
    // Wayland without layer-shell (GNOME): a normal Wayland window can't place
    // itself or stay on top, so run through XWayland instead.
    if (GDK_IS_WAYLAND_DISPLAY(display) && !gtk_layer_is_supported()) {
        if (!getenv("BUNBUN_NO_XWAYLAND_FALLBACK") && getenv("DISPLAY")) {
            setenv("GDK_BACKEND", "x11", 1);
            setenv("BUNBUN_NO_XWAYLAND_FALLBACK", "1", 1);
            unlink(LockPath().c_str());
            execv("/proc/self/exe", argv);
        }
        fprintf(stderr, "Bunbun: this Wayland compositor has no layer-shell and there's no XWayland; see docs/linux.md.\n");
    }
    g_layerShell = GDK_IS_WAYLAND_DISPLAY(display) && gtk_layer_is_supported();
#endif

    bb::settings::EnsureFile();
    LoadSettings();
    InstallStableHook();
    g_rt = bbcompat::CreateCairoTarget();

    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_window), "Bunbun");
    gtk_widget_set_app_paintable(g_window, TRUE);
    if (GdkVisual* rgba = gdk_screen_get_rgba_visual(gtk_widget_get_screen(g_window)))
        gtk_widget_set_visual(g_window, rgba);
    gtk_window_set_decorated(GTK_WINDOW(g_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(g_window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(g_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(g_window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(g_window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(g_window), FALSE);
    if (g_layerShell) {
        gtk_layer_init_for_window(GTK_WINDOW(g_window));
        gtk_layer_set_namespace(GTK_WINDOW(g_window), "bunbun");
        gtk_layer_set_layer(GTK_WINDOW(g_window), GTK_LAYER_SHELL_LAYER_TOP);
        gtk_layer_set_anchor(GTK_WINDOW(g_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_exclusive_zone(GTK_WINDOW(g_window), 0);  // sit below top panels, reserve no space
        gtk_layer_set_keyboard_mode(GTK_WINDOW(g_window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    } else {
        // A dock stays above normal windows but under fullscreen ones, and
        // window managers place it where we ask. BUNBUN_X11_OVERRIDE=1 uses an
        // unmanaged (override-redirect) window instead, for window managers
        // that mishandle docks.
        gtk_window_set_type_hint(GTK_WINDOW(g_window), GDK_WINDOW_TYPE_HINT_DOCK);
        gtk_window_set_keep_above(GTK_WINDOW(g_window), TRUE);
        gtk_window_stick(GTK_WINDOW(g_window));
        if (getenv("BUNBUN_X11_OVERRIDE")) {
            gtk_widget_realize(g_window);
            gdk_window_set_override_redirect(gtk_widget_get_window(g_window), TRUE);
        }
    }
    gtk_widget_add_events(g_window, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_ENTER_NOTIFY_MASK |
                                        GDK_LEAVE_NOTIFY_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK |
                                        GDK_KEY_PRESS_MASK | GDK_FOCUS_CHANGE_MASK);
    g_signal_connect(g_window, "draw", G_CALLBACK(OnDraw), nullptr);
    g_signal_connect(g_window, "button-press-event", G_CALLBACK(OnButtonPress), nullptr);
    g_signal_connect(g_window, "motion-notify-event", G_CALLBACK(OnMotion), nullptr);
    g_signal_connect(g_window, "enter-notify-event", G_CALLBACK(OnCrossing), nullptr);
    g_signal_connect(g_window, "leave-notify-event", G_CALLBACK(OnCrossing), nullptr);
    g_signal_connect(g_window, "scroll-event", G_CALLBACK(OnScroll), nullptr);
    g_signal_connect(g_window, "key-press-event", G_CALLBACK(OnKey), nullptr);
    g_signal_connect(g_window, "focus-out-event", G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer) -> gboolean {
        ComposeFocusOut();
        return FALSE;
    }), nullptr);
    g_signal_connect(g_window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    PlaceWindow();
    gtk_widget_show_all(g_window);
    PlaceWindow();

    GFile* settingsFile = g_file_new_for_path(bb::settings::Path().c_str());
    GFileMonitor* monitor = g_file_monitor_file(settingsFile, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr);
    if (monitor) g_signal_connect(monitor, "changed", G_CALLBACK(OnSettingsFileChanged), nullptr);

    for (int sig : {SIGTERM, SIGINT, SIGHUP})
        g_unix_signal_add(sig, [](gpointer) -> gboolean { gtk_main_quit(); return G_SOURCE_REMOVE; }, nullptr);

    g_lastDrawAt = Now();
    bb::llyrics::Start([] { Redraw(); });
    bb::mpris::Start(OnMediaChanged);
    bb::lsrc::Start([] { Redraw(); });
    if (!bb::agents::Start()) fprintf(stderr, "Bunbun: the agent socket is in use; agents won't show.\n");
    // The clock needs a redraw when the minute (or second) turns over. Every
    // frame path also passes through here, so no draw for a few seconds means
    // the window isn't being exposed (see Remap).
    g_timeout_add_seconds(1, [](gpointer) -> gboolean {
        if (Now() - g_lastDrawAt > 3) {
            g_lastDrawAt = Now();
            Remap();
        }
        // Reminders that came due, and the end of a focus timer.
        for (const std::wstring& text : bb::lsrc::TakeDueReminders()) ShowToast("Reminder", bb::plat::Utf8(text), 10);
        if (bb::lsrc::FocusTakeFinished()) ShowToast("Focus", "Time's up \xE2\x80\x94 take a break", 10);
        gtk_widget_queue_draw(g_window);
        return G_SOURCE_CONTINUE;
    }, nullptr);
    g_signal_connect(gtk_widget_get_screen(g_window), "composited-changed", G_CALLBACK(+[](GdkScreen*, gpointer) { Remap(); }),
                     nullptr);

    gtk_main();

    bb::agents::Stop();
    bb::mpris::Stop();
    if (monitor) g_object_unref(monitor);
    g_object_unref(settingsFile);
    unlink(LockPath().c_str());
    return 0;
}
