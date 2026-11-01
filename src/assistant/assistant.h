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
    Info,         // `info` says which
};

enum class Info { None, Time, Date, Battery, Weather, NowPlaying, System };

struct Intent {
    Action action = Action::None;
    Info info = Info::None;
    std::wstring target;   // what to open
    std::wstring label;    // human name of the target ("Notepad", "Wi-Fi settings")
    int number = 0;
    std::wstring reply;    // answer text for Reply / None
};

// `searchEngine`: "google", "bing" or "duckduckgo".
Intent Parse(const std::wstring& text, const std::wstring& searchEngine);

// Evaluates + - * / % ^ and parentheses. False when `expr` is not arithmetic.
bool Evaluate(const std::wstring& expr, double* out);

// Best Start-menu shortcut whose name matches `name`, or "" (does disk I/O).
std::wstring FindStartMenuApp(const std::wstring& name);

// One-line command reference for the help card.
std::wstring HelpText();

}  // namespace bb::assistant
