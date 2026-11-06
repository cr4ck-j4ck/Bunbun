// ── Edge light ──────────────────────────────────────────────────────────────
// Bunbun: a light that runs along the island's outline (Bunbun.EdgeLight): twin
// comets that start at the top corners and meet at the bottom, a single comet, or
// the whole outline breathing. The idea comes from Vayli's notch; the drawing is
// our own (edgelight_draw.inc). Bunbun.EdgeLightOn picks what plays it, any mix
// of: hover, opening, closing, a track change, notifications, agents, system
// pop-ups, or always. Each play lasts a few seconds and fades; the render loop
// only keeps frames coming while it is lit.
//
// Render thread only: Note() is called from Renderer::Render, Lit() from the
// render loop, which share the thread.
namespace EdgeLight {

// The trigger bits and their parser live in src/common/edgelight.h.
using namespace bb::edgelight;

// Which trigger an activity appearing on the island counts as (0 for none).
inline unsigned KindTrigger(IslandKind k) {
    switch (k) {
        case IslandKind::Notification: case IslandKind::Reminder: case IslandKind::Meeting:
        case IslandKind::Download: case IslandKind::Clipboard:
            return kOnNotification;
        case IslandKind::Agent:
            return kOnAgent;
        case IslandKind::Volume: case IslandKind::Brightness: case IslandKind::BatteryLow: case IslandKind::CapsLock:
        case IslandKind::Device: case IslandKind::Bluetooth: case IslandKind::DoNotDisturb: case IslandKind::Privacy:
        case IslandKind::Progress: case IslandKind::Timer:
            return kOnSystem;
        default:
            return 0;
    }
}

constexpr double kPlaySeconds = 4.0;  // how long one play lasts
constexpr double kFadeIn = 0.25, kFadeOut = 0.9;

double g_start = -1.0;   // when the current play began
double g_until = -1.0;   // when it ends
bool g_prevHover = false, g_prevOpen = false;
int g_prevKind = -1;
double g_prevTitleAt = -1.0;
bool g_prevAgentAsks = false;

inline void Play(double now) {
    if (now >= g_until) g_start = now;  // a play already running just lasts longer
    g_until = now + kPlaySeconds;
}

// Once a frame: what the island is doing. Plays the light for whichever of the
// chosen triggers (Settings::edgeLightOn) just happened.
void Note(const SharedState& state, const Settings& s, bool hover, bool open, IslandKind kind, double now) {
    const unsigned on = s.edgeLightOn;
    bool fire = false;
    if ((on & kOnHover) && hover && !g_prevHover) fire = true;
    if ((on & kOnOpen) && open && !g_prevOpen) fire = true;
    if ((on & kOnClose) && !open && g_prevOpen) fire = true;
    const int k = static_cast<int>(kind);
    if (g_prevKind != -1 && k != g_prevKind && (on & KindTrigger(kind))) fire = true;
    // A song change (not the first song seen after start).
    const double titleAt = state.media.titleChangedAt;
    if ((on & kOnTrack) && g_prevTitleAt >= 0.0 && titleAt > g_prevTitleAt && state.media.available) fire = true;
    // An agent waiting on you plays it even when its card was already showing.
    const bool asks = state.agent.phase == bb::AgentPhase::Approval;
    if ((on & kOnAgent) && asks && !g_prevAgentAsks) fire = true;

    g_prevHover = hover;
    g_prevOpen = open;
    g_prevKind = k;
    g_prevTitleAt = titleAt;
    g_prevAgentAsks = asks;
    if (fire) Play(now);
}

// How bright the light is now, 0 to 1.
float Level(const Settings& s, double now) {
    if (s.edgeLight == 0) return 0.0f;
    if (s.edgeLightOn & kOnAlways) return 1.0f;
    if (g_start < 0.0 || now >= g_until) return 0.0f;
    const double in = std::clamp((now - g_start) / kFadeIn, 0.0, 1.0);
    const double out = std::clamp((g_until - now) / kFadeOut, 0.0, 1.0);
    return static_cast<float>(std::min(in, out));
}

bool Lit(const Settings& s, double now) { return Level(s, now) > 0.0f; }

// Seconds into the animation: from the start of the play, so the comets always
// set off from the top corners; Always just runs on the clock.
double Clock(const Settings& s, double now) { return (s.edgeLightOn & kOnAlways) || g_start < 0.0 ? now : now - g_start; }

}  // namespace EdgeLight
