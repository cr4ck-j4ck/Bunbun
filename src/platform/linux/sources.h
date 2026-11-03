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

// ── System (/proc, /sys) ────────────────────────────────────────────────────
struct Load {
    int cpu = 0, mem = 0, disk = 0;  // percent
    double rxKBs = 0, txKBs = 0;     // network, KB/s
    double memUsedGB = 0, memTotalGB = 0;
    long uptime = 0;                 // seconds
};
struct Battery {
    bool has = false, charging = false;
    int percent = 0;
};
const Load& SystemLoad();     // sampled at most every 1.5 s
const Battery& BatteryState();

// ── Weather (Open-Meteo, like Windows) ──────────────────────────────────────
struct Hour {
    int hour = 0;
    double temp = 0;
    int code = 0;  // WMO weather code
};
struct Weather {
    bool has = false, loading = true;
    std::string city, error;
    double temp = 0, high = 0, low = 0;
    int code = 0;
    bool fahrenheit = false;
    std::vector<Hour> hours;  // the next few hours
};
const Weather& CurrentWeather();  // refreshed every 15 minutes
const char* WeatherWords(int code);

// ── Quick toggles (NetworkManager, BlueZ, GNOME's notification banners) ─────
struct Radios {
    bool wifiKnown = false, wifi = false;
    std::string network;  // the connection in use
    bool btKnown = false, bt = false;
    bool dndKnown = false, dnd = false;
};
const Radios& RadioState();  // refreshed every 3 s
void SetWifi(bool on);
void SetBluetooth(bool on);
void SetDnd(bool on);

// ── Clipboard history (the last 8 texts you copied) ─────────────────────────
const std::vector<std::string>& ClipHistory();
void CopyClip(int index);
void ClearClips();

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
};
Focus& FocusState();
void FocusStart(int minutes);
void FocusPause();  // pauses or resumes
void FocusAdd(int minutes);
void FocusStop();
bool FocusTakeFinished();  // true once when a running timer reaches zero

}  // namespace bb::lsrc
