// Bunbun — where the Linux island's pages get their data: the Linux counterparts
// of the Windows island's modules (system monitor, weather, quick toggles,
// clipboard history, quick note, to-dos and reminders, the focus timer).
// Everything runs on the GTK main loop; slow things (web requests, D-Bus) are
// asynchronous and call back through the function given to Start().
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../../common/todos.h"

namespace bb::lsrc {

// Starts the clipboard watch and the timers. `changed` asks for a redraw.
void Start(std::function<void()> changed);
// Rereads the settings that matter here (weather city and units, clipboard history).
void LoadSettings();

// GET an http(s) URL (GIO, else curl); `done` gets the body, or "" on failure. Main loop.
void FetchUrl(const std::string& url, std::function<void(const std::string&)> done);

// ── System (/proc, /sys) ────────────────────────────────────────────────────
struct Load {
    int cpu = 0, mem = 0, disk = 0;  // percent
    int gpu = -1;                    // percent busy, -1 when the driver doesn't say (AMD and Intel do)
    double rxKBs = 0, txKBs = 0;     // network, KB/s
    double memUsedGB = 0, memTotalGB = 0;
    long uptime = 0;                 // seconds
};
struct Battery {
    bool has = false, charging = false;
    int percent = 0;
    bool full = false, onPower = false;  // charged to the top; the charger is in
    int health = -1;                     // full charge vs design capacity, percent (-1 unknown)
    long secondsLeft = -1;               // on battery, at the current draw (-1 unknown)
};
const Load& SystemLoad();     // sampled at most every 1.5 s
const Battery& BatteryState();

// ── Weather (Open-Meteo, like Windows) ──────────────────────────────────────
struct Hour {
    int hour = 0;
    double temp = 0;
    int code = 0;  // WMO weather code
};
struct Day {
    std::string name;  // "Today", "Sat"
    double lo = 0, hi = 0;
    int code = 0;
};
struct Weather {
    bool has = false, loading = true;
    std::string city, error;
    double temp = 0, high = 0, low = 0;
    int code = 0;
    bool fahrenheit = false;
    std::vector<Hour> hours;  // the next few hours
    // The Weather page's details; each is unknown (< 0, empty) when the service didn't say.
    double feels = -999, wind = -1, uv = -1;
    int humidity = -1;
    std::string sunrise, sunset;  // "6:39"
    std::vector<Day> days;        // today and the next four
    double updatedAt = 0;         // the island's clock when it last arrived
};
const Weather& CurrentWeather();  // refreshed every 15 minutes
const char* WeatherWords(int code);
std::string UvWords(double uv);  // "2 Low", "7 High"

// ── Quick toggles (NetworkManager, BlueZ, GNOME's notification banners) ─────
struct Radios {
    bool wifiKnown = false, wifi = false;
    std::string network;  // the connection in use
    bool wired = false;   // that connection is Ethernet
    int signal = -1;      // its Wi-Fi signal, percent (-1 unknown)
    bool btKnown = false, bt = false;
    bool dndKnown = false, dnd = false;
};
const Radios& RadioState();  // refreshed every 3 s
void SetWifi(bool on);
void SetBluetooth(bool on);
void SetDnd(bool on);

// ── Clipboard history (the last 40 texts you copied, as on Windows) ─────────
const std::vector<std::string>& ClipHistory();
const std::vector<double>& ClipTimes();  // when each was copied (the island's clock)
void RemoveClip(int index);
void CopyClip(int index);
void ClearClips();
// Puts `text` on the clipboard from Bunbun itself: into the history, without a copy preview.
void CopyText(const std::string& text);
// Called with each new text you copy (even with the history off), for the copy preview.
void OnCopy(std::function<void(const std::string&)> listener);

// ── Quick note: note.txt next to settings.ini, the same file as on Windows ──
const std::vector<std::wstring>& NoteLines();
bool NoteIsCheckbox(const std::wstring& line, bool* checked);
void NoteToggle(int line);
void NoteEdit();  // opens note.txt in the text editor

// ── To-dos: todos.tsv, shared with the settings app ─────────────────────────
const std::vector<bb::todos::Item>& Todos();
void TodoToggle(int id);
// To-dos whose time has come since the last call (marked as notified).
std::vector<std::wstring> TakeDueReminders();

// ── Focus timer ─────────────────────────────────────────────────────────────
struct Focus {
    bool active = false, running = false;
    double endsAt = 0, remaining = 0, total = 0;  // the island's clock (seconds)
    int pickH = 0, pickM = 25;                    // the picker on the idle page
    int kind = 0;                                 // who started it: bb::ltimers::kKind* (0 the old focus timer)
};
Focus& FocusState();
void FocusStart(int minutes);
void FocusStartSeconds(int seconds, int kind);  // the Timers page (timers_linux.cpp)
void FocusPause();  // pauses or resumes
void FocusAdd(int minutes);
void FocusStop();
bool FocusTakeFinished();  // true once when a running timer reaches zero
int FocusTakeFinishedKind();  // the same, as the finished timer's kind; -1 when none finished

}  // namespace bb::lsrc
