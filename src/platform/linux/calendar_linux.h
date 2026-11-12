// Bunbun — your calendar on Linux: the private iCal link(s) from Settings
// (Modules.CalendarIcsUrl), fetched every five minutes, parsed with the same code
// as Windows (common/ics.h). Gives the Calendar page today's meetings and the
// island the next one, shown Modules.MeetingAlertMinutes before it starts.
#pragma once

#include <functional>
#include <vector>

#include "../../common/ics.h"

namespace bb::lcal {

void Start(std::function<void()> changed);
void LoadSettings();

const bb::ics::Meeting& Next();                // the next meeting within two days
std::vector<bb::ics::Meeting> Today();         // today's meetings, in start order
int AlertMinutes();                            // 0 = no meeting pill
bool FetchFailed();
// The days of all-day leave events (leave, vacation, PTO, OOO, day off) in the links, around now.
const std::vector<int>& LeaveDays();

// The meeting pill: from AlertMinutes before a meeting until 5 min after it started,
// unless you put it away.
bool PillWanted(double epochNow);
void Dismiss();
void Join();  // opens the meeting's link (and puts the pill away)

}  // namespace bb::lcal
