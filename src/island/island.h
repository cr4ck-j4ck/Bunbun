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

// The island clock (seconds); AgentView::phaseSince uses it.
double IslandNow();

// Opens the Ask Bun box (the Quick Lookup panel), as its hotkey (Ctrl+Alt+B) does.
void IslandOpenAsk();

// False when the Ask Bun hotkey could not be registered (another app owns it).
bool IslandAskHotkeyActive();

// The island as it is on screen right now (PNG bytes), its screen rect, and the
// rect / work area of its display. False when nothing is shown. Any thread.
#include <string>
bool IslandSnapshotPng(std::string* png, RECT* screen, RECT* monitor, RECT* work);
