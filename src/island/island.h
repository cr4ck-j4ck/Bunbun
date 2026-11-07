// Bunbun — entry points of the island core (island_core.cpp).
#pragma once

#include <windows.h>

#include "../agents/agent_view.h"

BOOL IslandStart();
void IslandStop();
void IslandReloadSettings();

// Replaces the agent pill's contents and wakes the render loop. Thread-safe.
// nudge = play the little bump that marks a new event.
void IslandSetAgentView(const bb::AgentView& view, bool nudge);

// Shows "Bunbun <version> is ready" on the island; a click opens `url`. Thread-safe.
void IslandAnnounceUpdate(const wchar_t* version, const wchar_t* url);

// The island clock (seconds); AgentView::phaseSince uses it.
double IslandNow();

// Opens the Ask Bun box (the Quick Lookup panel), as its hotkey (Ctrl+Alt+B) does.
void IslandOpenAsk();
void IslandSubmitAsk(const std::wstring& query);

// False when the Ask Bun hotkey could not be registered (another app owns it).
bool IslandAskHotkeyActive();

// The island as it is on screen right now (PNG bytes), its screen rect, and the
// rect / work area of its display. False when nothing is shown. Any thread.
#include <string>
bool IslandSnapshotPng(std::string* png, RECT* screen, RECT* monitor, RECT* work);
// Moves a snapshot's rects to display `index` (1-based) when that display shows a mirror.
bool IslandDisplayPlace(int index, RECT* screen, RECT* monitor, RECT* work);
// Bunbun: steps the open island through its pages and saves a snapshot of each to
// %LOCALAPPDATA%\Bunbun\previews\<page>.png (in the background). Returns how many
// pages it will capture, 0 when a capture is already running.
int IslandCapturePages();
// Bunbun: test hooks for the pipe (tools that drive the island without the mouse).
// Opens the island pinned on page `tab` (its index in the tab bar; -1 closes it);
// returns how many pages there are.
int IslandTestOpen(int tab);
// The click spots on the island now, as JSON: [[action, arg, left, top, right, bottom], ...]
// in client pixels (action is the StudioAction number).
std::string IslandTestHits();
// A left click at client point (x, y), sent to the island window as a real click
// would be (the pointer itself doesn't move).
bool IslandTestClick(int x, int y);
// A made-up event for `seconds`, so tests can see settings that act on live events
// without touching the PC: kind is media | volume | brightness | caps | battery |
// notification | progress | privacy | mic | camera | download | bluetooth | cheer | clear.
// a / b are text (title / artist, app / body...), n a number (percent, on/off).
// While it lasts, the media, lyrics and privacy pollers leave that part of the state alone.
bool IslandTestState(const std::string& kind, const std::wstring& a, const std::wstring& b, int n, int seconds);
