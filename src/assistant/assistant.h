// Bunbun — Bun's local Windows assistant.
//
// Turns what the user types into the Ask Bun box into an intent: open an app or
// settings page, control media / volume, start a timer, search the web, answer a
// quick question about the PC, or do arithmetic. Everything runs locally; no
// text leaves the machine except a web search the user explicitly asks for.
//
// Parsing is pure (no side effects) so it can be unit tested; the island core
// executes the intent on its window thread.
#pragma once

#include <string>
#include <vector>

namespace bb::assistant {

// Action::WaterAdd's `number` for "a bottle": the bottle size set for Water, else 750 ml.
constexpr int kWaterBottle = -99;

enum class Action {
    None,         // nothing to do; `reply` says why / what to try
    Reply,        // `reply` is the whole answer (math, help)
    Open,         // ShellExecute `target` (exe, URI, URL or .lnk path)
    WebSearch,    // open `target` (a search URL)
    MediaPrevious,
    MediaToggle,
    MediaNext,
    VolumeSet,    // `number` = 0..100
    VolumeUp,
    VolumeDown,
    Mute,
    Unmute,
    Lock,
    Screenshot,
    TimerStart,   // `number` = minutes
    TimerStop,
    MascotDndOn,  // the mascot settles in with a snack and stops bouncing
    MascotDndOff,
    TodoAdd,      // `label` = the to-do; with a due time it's a reminder (see Due)
    TodoList,     // show the open to-dos
    TodoDone,     // `label` = which one (matched loosely)
    TodoClearDone,
    FocusStart,   // `number` = minutes (25 by default)
    FocusStop,
    WifiOn,
    WifiOff,
    BluetoothOn,
    BluetoothOff,
    BrightnessSet,  // `number` = 0..100
    BrightnessUp,
    BrightnessDown,
    Undo,           // put back what the last command changed
    Lookup,         // Bunbun: online encyclopedia / dictionary lookup (Wikipedia / Wiktionary)
    WaterAdd,       // Bunbun: log water; `number` = ml, or -n for n glasses (0 = one glass, kWaterBottle = a bottle)
    WaterUndo,      // take the last glass back
    WorkStart,      // Bunbun: Work / Study mode's day (Settings, Mode)
    WorkPause,
    WorkResume,
    WorkBreak,
    WorkDone,
    Info,         // `info` says which
};

enum class Info { None, Time, Date, Battery, Weather, NowPlaying, System, Water, Work };

struct Intent {
    Action action = Action::None;
    Info info = Info::None;
    std::wstring target;   // what to open; for Info::Weather, the place asked about ("" = here)
    std::wstring label;    // human name of the target ("Notepad", "Wi-Fi settings")
    int number = 0;        // for Info::Weather, 1 = tomorrow
    std::wstring reply;    // answer text for Reply / None
    // TodoAdd due time, relative or by the clock; the core turns it into a date.
    // Both unset = no reminder.
    int dueInMinutes = -1;  // "in 20 minutes"
    int dueHour = -1;       // "at 3pm" -> 15, "at 9:30" -> 9 (24 h)
    int dueMinute = 0;
    int dueDays = 0;        // "tomorrow" -> 1
    // Repeating reminder: "daily", "weekdays", "weekly:1,3" (0 = Sunday), or
    // "weekly" for "every week" (the core picks today's weekday). See todos.h.
    std::wstring repeat;
    bool unknown = false;   // nothing matched: a question for the local AI, when it's on
};

// `searchEngine`: "google", "bing" or "duckduckgo".
Intent Parse(const std::wstring& text, const std::wstring& searchEngine);

// Evaluates + - * / % ^ and parentheses. False when `expr` is not arithmetic.
bool Evaluate(const std::wstring& expr, double* out);

// Best Start-menu shortcut whose name matches `name`, or "". Reads a cached index
// of the Start menu (built on first use, refreshed every 10 minutes).
std::wstring FindStartMenuApp(const std::wstring& name);

// Builds the Start-menu index now (does disk I/O), so the first "open …" is instant.
void WarmStartMenuIndex();

// How well an app name matches what was typed, 0 = not at all, 100 = exact.
// Exact > prefix > a word's prefix > initials ("vsc" = Visual Studio Code) >
// contains > letters in order. Pure; both are compared case-insensitively.
int AppMatchScore(const std::wstring& appName, const std::wstring& typed);

// One-line command reference for the help card.
std::wstring HelpText();

}  // namespace bb::assistant
