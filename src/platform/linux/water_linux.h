// Bunbun — the water reminder on Linux (src/common/water.h has the rules, shared
// with Windows). Today and the last week live in $XDG_STATE_HOME/Bunbun/water.txt.
// GTK main thread only.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace bb::lwater {

struct Snapshot {
    int goalMl = 2000, glassMl = 250, bottleMl = 0;
    int takenMl = 0, glasses = 0, bottleLeftMl = 0;
    bool reminders = false;
    int nextMinute = -1;                    // minute of the day of the next reminder; -1 = none today
    std::vector<std::pair<int, int>> week;  // (yyyymmdd, ml), oldest first, today last
};

void LoadSettings();  // Bunbun.Water*
bool PageOn();        // Bunbun.WaterPage
Snapshot Now();

enum class Due { None, Drink, Refill };
// Called every 30 s: whether to remind now (and remembers that it did).
Due Check();

void Drink();
// When the last glass was logged, and when the goal's cheer starts (island Now() seconds).
double DrankAt();
double GoalAt();
void Undo();
void Refill();
void Snooze();

}  // namespace bb::lwater
