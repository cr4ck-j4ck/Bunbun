// Bunbun — the rest of the Windows settings that make sense on Linux: where the
// island sits, hiding, the motion, the music pill's options, system alerts
// (battery, Caps Lock, brightness, volume, Bluetooth, Do not disturb, copies),
// the date, the idle eyes face and the lyric options. Settings are read here and
// island_linux.cpp / studio_linux.cpp ask for them; the alerts come back as toasts
// through ShowToast.
#pragma once

#include <cairo.h>

#include <functional>
#include <string>
#include <vector>

#include "../../agents/agent_view.h"
#include "../../mascot/mascot.h"

namespace bb::lfeat {

struct Feat {
    // Where (Appearance.*)
    int align = 1;            // 0 left, 1 centre, 2 right (Position)
    bool bottom = false;      // bottom-* positions
    int offsetX = 0;
    bool separateExpandedY = false;
    int offsetYExpanded = 0;
    bool merged = false;      // BorderMergedMode: flush with the edge, no gap
    std::wstring monitor = L"primary";  // TargetMonitor: primary, 1..5, follow
    bool onTop = true;        // Behavior.AlwaysOnTop

    // Hiding (Behavior.*, Shortcuts.*, Bunbun.TuckHotkey)
    int autoHideIdle = 0;     // seconds, 0 = never
    bool hideFullscreen = true, tuckFullscreen = false, unhideOnHover = true;
    bool hideHotkey = true;   // Ctrl+Alt+D by default (X11)
    std::wstring hideMods = L"ctrl_alt", hideKey = L"D";
    bool tuckHotkey = true;   // Ctrl+Alt+H (X11) and the menu item
    int tuckRevealMs = 450;   // Bunbun.TuckRevealMs: pointing at the line this long brings it back
    std::wstring fsShowApps, fsTuckApps, fsHideApps;  // Behavior.Fullscreen*Apps

    // Motion (Animations.*, Bunbun.AmbientFps)
    double stiffness = 260, damping = 32;  // the resize spring (AnimationStyle, AnimationSpeed)
    int fps = 60;             // TargetFPS
    int ambientFps = 30;      // moving content while you're not on the island
    bool powerSaver = false;  // PowerSaver: always, or auto on battery
    bool coverSpin = false, mediaTransitions = false;
    int spectrum = 0;         // Appearance.SpectrumStyle: bars, led, plasma, orb
    int progress = 0;         // Appearance.ProgressStyle: slim, wavy, squiggle, bar

    // The music pill (Modules.*, Bunbun.*)
    bool collapsedTitle = true, pillClock = false, autoExpand = false;
    std::wstring expandBlocklist;
    bool musicSwipe = true;
    std::wstring musicApp = L"auto";

    // Alerts
    bool battery = true, batteryMoods = true;
    int chargingEvery = 15;   // minutes, 0 = off
    bool capsLock = true, brightness = true, volume = true;
    bool bluetooth = true, bluetoothBattery = true, bluetoothLow = true;
    bool dnd = true, clipboard = true, countdownBar = false;
    bool timer = true;        // the Focus timer items in the right-click menu
    bool reminderSound = true, quietReminders = true;
    bool focusGuard = false;  // Bunbun.FocusGuard (X11): a note when a distracting app comes up
    std::wstring focusGuardApps;

    // The date (Modules.*)
    std::wstring dateFormat;  // yyyy MM M dd d MMM MMMM ddd dddd
    bool mondayFirst = true;  // FirstDayOfWeek (auto: the locale)

    // The idle face (Bunbun.*)
    bool eyes = false;        // IdleFace = eyes
    int eyeColor = 0;         // mint, white, blue, pink, purple, yellow
    bool eyeStatus = false;
    int strip = 0;            // IdleStripItems: 0 time-weather, 1 time, 2 weather, 3 nothing, 4 name, 5 name-weather
    std::string name;         // Bunbun.IdleName, for 4 and 5 (UTF-8)
    bool mascotDnd = false;
    bool dndFollowSystem = false;  // Bunbun.DndFollowSystem: the desktop's DND puts the mascot in DND
    int afkMinutes = 3;            // Bunbun.AfkMinutes: AFK after this long without input (0 = off)
    int sleepMinutes = 10;         // Bunbun.SleepMinutes: asleep after this long without input (0 = never)

    // Lyrics (Modules.*)
    bool karaoke = true, karaokeGlow = true, neighbours = false;
    int btOffsetMs = 0;       // LyricsOffsetBluetoothMs, used while Bluetooth audio plays
};

void LoadSettings();
const Feat& Get();
// After the settings file changed: re-grab the hotkeys.
void ApplySettingsChange();

// Starts the alert watchers. `toast(title, text, seconds, level)` shows one;
// level 0..1 draws a bar (volume, brightness), < 0 none.
void Start(std::function<void(const std::string&, const std::string&, double, double)> toast,
           std::function<void()> changed);

// ── Placement ───────────────────────────────────────────────────────────────
// The monitor the island belongs on (TargetMonitor), or null for the default.
struct _GdkMonitor* TargetMonitor(struct _GdkDisplay* display);
// Appearance.DisplayLink = separate (with the island on all displays): it follows the pointer.
bool SeparateDisplays();
// The island's side and edge on the display it's on now (Separate mode: each display's own).
void SetPlacement(int align, bool bottom);

// ── Hiding ──────────────────────────────────────────────────────────────────
bool FullscreenAppActive();   // X11: the active window is full screen and the island tucks or hides
bool FullscreenTuckNow();     // ... and it tucks (else hides): In full screen and the per-app lists
int FullscreenMonitor();      // the display that full-screen window is on (GDK index), -1 unknown
// Seconds since the last keyboard or mouse input, from GNOME (Mutter) or KDE / the
// freedesktop ScreenSaver over D-Bus; -1 while unknown.
double IdleSeconds();
void NoteInteraction();       // the pointer, an agent or new media: resets auto-hide
bool AutoHidden();            // hidden by Behavior.AutoHideIdleSeconds
bool Tucked();
void SetTucked(bool tucked);
bool HiddenByHotkey();
void ToggleHidden();

// ── Motion ──────────────────────────────────────────────────────────────────
// One spring step of `x` toward `target` (velocity in *v), the style and speed applied.
void Spring(double* x, double* v, double target, double dt);
int FrameMs(bool interacting);  // the fast frame interval: TargetFPS, AmbientFps, battery saver

// ── The music pill ──────────────────────────────────────────────────────────
bool ExpandBlocked(const std::string& player, const std::string& title);
// Raises the music app (MusicApp = auto: the one playing, over MPRIS).
void OpenMusicApp();

// The music bars in the chosen SpectrumStyle, in a w x h box centred on cy.
// They follow the clock, not the audio (Linux has no capture for them yet).
void Spectrum(cairo_t* cr, double x, double cy, double w, double h, const double* rgb, double alpha, bool playing, double now);
// The song's progress line in the chosen ProgressStyle (0..1).
void Progress(cairo_t* cr, double x, double cy, double w, double fraction, const double* rgb, bool playing, double now);

// The music's real levels, from what's playing (PulseAudio / PipeWire monitor
// through parec), kBands bands 0..1. Capturing only runs while `wanted` is set
// (something plays and its bars are on screen); empty when there's no capture.
constexpr int kBands = 12;
void WantAudio(bool wanted);
const std::vector<float>& AudioBands();
bool LyricsVisualizer();  // Modules.LyricsVisualizer

// ── Battery ─────────────────────────────────────────────────────────────────
// The mascot's battery moods: Happy for a moment at full, Sleepy below 15 %.
bool BatteryCheer();
bool BatteryDrowsy();

// ── The date ────────────────────────────────────────────────────────────────
std::string FormatDate(time_t when);  // DateFormat, or the locale's "Tue 3 Nov"

// ── Idle eyes ───────────────────────────────────────────────────────────────
void DrawEyes(cairo_t* cr, double cx, double cy, double height, bb::mascot::Mood mood, bb::AgentPhase phase,
              double now, double moodSince, double pointerX, double pointerY);

// ── Lyrics ──────────────────────────────────────────────────────────────────
bool BluetoothAudio();
// The default sound output (pactl): volume 0-100+ and mute, read at most every 2 s;
// false without PulseAudio / PipeWire tools.
bool VolumeNow(int* pct, bool* mute);
void SetVolume(int pct);
void ToggleMute();
std::string AudioOutputName();  // its description ("Built-in Audio", a headset's name), "" unknown  // the default sound output is a Bluetooth device

}  // namespace bb::lfeat
