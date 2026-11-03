// Bunbun — what shared code (the agent bridge) needs from the Linux island.
// The Windows counterparts are island/island.h and island/wh_shim.h.
#pragma once

#include "../../agents/agent_view.h"

// The island clock (seconds); AgentView::phaseSince uses it.
double IslandNow();

// Replaces the agent pill's contents. Any thread.
void IslandSetAgentView(const bb::AgentView& view, bool nudge);

// Saves pictures of the island's pages for the settings app (Island pages) and
// returns how many; -1 if the island didn't answer. Any thread; waits for the
// GTK main loop.
int IslandCapturePages();

// Settings by "Section.Key" ("Bunbun.ShellApprovals"): true/false read as 1/0.
int Wh_GetIntSetting(const wchar_t* name, ...);

// Appends to $XDG_STATE_HOME/Bunbun/bunbun.log when BUNBUN_LOG=1. A wide
// printf format: use %ls for wide strings.
void BbLog(const wchar_t* format, ...);
#define Wh_Log(...) BbLog(__VA_ARGS__)
