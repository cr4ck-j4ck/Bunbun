// Bunbun — Work mode on Linux (src/common/work.h has the rules, shared with
// Windows): Bunbun.UseCase, your hours, lunch and breaks, and the day's log in
// $XDG_STATE_HOME/Bunbun/work.txt. GTK main thread only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../../common/work.h"

namespace bb::lwork {

void LoadSettings();  // Bunbun.UseCase and Work.*
bool On();            // Work mode is part of this setup (office work or both)
bool AgentsOn();      // agents are part of it (AI agents or both)

struct Snapshot {
    bool on = false;
    bb::work::Phase phase = bb::work::Phase::Off;
    bb::work::Summary today;
    int goal = 0;                      // seconds
    bb::work::Schedule schedule;
    bool inHours = false;
    long long breakUntil = 0;          // epoch seconds
    std::vector<bb::work::Segment> segments;
    // Study (Bunbun.UseCase = study)
    bool study = false;
    std::wstring subject;
    int blockEnd = -1, nextBlock = -1;      // minutes after midnight (-1: none)
    std::wstring nextSubject;
    std::vector<std::pair<std::wstring, int>> subjects;  // today, most studied first
    int streak = 0;
};
Snapshot Now();

// Every few seconds: advances the day. Returns the notes to show (title, text).
std::vector<std::pair<std::string, std::string>> Tick();

void Start();
void Pause();
void Resume();
void TakeBreak();
void Done();

}  // namespace bb::lwork
