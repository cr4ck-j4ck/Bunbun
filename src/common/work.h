// Bunbun — Work mode: your working days and hours, lunch, breaks, and a log of
// how the day went (worked, away, asleep, lunch, breaks, overtime).
//
// Bunbun.UseCase picks what Bunbun is for: AI agents (as before), office work
// (this, with the agent pieces hidden) or both. Work mode runs on a schedule
// (Work.Days / Start / End, lunch, breaks) or by hand (Start, Pause, Take a break,
// Done for today). Every few seconds the island calls Tracker::Tick with the time
// and how long the keyboard and mouse have been idle; the tracker says what state
// the day is in, extends the day's timeline and raises events for the pills
// (lunch, break due, goal reached, day over).
//
// Pure: no clock, file or UI here. Shared with Linux and the unit tests.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace bb::work {

// ── What Bunbun is for ───────────────────────────────────────────────────────
// Study runs on the same engine as Work: a timetable of subjects instead of office
// hours, Pomodoro sessions that count, a daily goal and a streak.
enum class UseCase { Agents, Work, Both, Study };

inline UseCase ParseUseCase(std::wstring v) {
    for (auto& c : v) c = static_cast<wchar_t>(towlower(c));
    if (v == L"work") return UseCase::Work;
    if (v == L"both") return UseCase::Both;
    if (v == L"study") return UseCase::Study;
    return UseCase::Agents;
}
inline bool AgentsOn(UseCase u) { return u == UseCase::Agents || u == UseCase::Both; }
inline bool WorkOn(UseCase u) { return u != UseCase::Agents; }
inline bool StudyOn(UseCase u) { return u == UseCase::Study; }

// One class or study block of the timetable.
struct Block {
    int weekday = 0;             // 0 = Monday … 6 = Sunday
    int start = 0, end = 0;      // minutes after midnight, end after start
    std::wstring subject;
};

// ── The schedule ─────────────────────────────────────────────────────────────
struct Schedule {
    unsigned days = 0x1F;        // bit 0 = Monday … bit 6 = Sunday
    int start = 9 * 60;          // minutes after midnight
    int end = 18 * 60;           // an end before the start runs past midnight (night shifts)
    int lunchStart = 13 * 60;
    int lunchMinutes = 45;       // 0: no lunch
    int breakEvery = 50;         // minutes of work before a break reminder; 0: none
    int breakMinutes = 5;        // a break's length (and how long away counts as one)
    int goalMinutes = 0;         // the day's goal; 0: the scheduled hours less lunch
    bool autoStart = true;       // start and stop with the schedule (else only by hand)
    std::vector<Block> blocks;   // Study: the timetable; when set, it replaces days / start / end and lunch
};

// "09:00", "9:30", "18", "6:15 pm" → minutes after midnight; `fallback` when unreadable.
inline int ParseClock(const std::wstring& text, int fallback) {
    int h = -1, m = 0;
    size_t i = 0;
    auto digits = [&](int* out) {
        int v = 0, n = 0;
        while (i < text.size() && iswdigit(text[i]) && n < 2) { v = v * 10 + (text[i] - L'0'); ++i; ++n; }
        if (n) *out = v;
        return n > 0;
    };
    while (i < text.size() && iswspace(text[i])) ++i;
    if (!digits(&h)) return fallback;
    if (i < text.size() && (text[i] == L':' || text[i] == L'.')) { ++i; if (!digits(&m)) return fallback; }
    std::wstring rest = text.substr(i);
    for (auto& c : rest) c = static_cast<wchar_t>(towlower(c));
    if (rest.find(L"pm") != std::wstring::npos && h < 12) h += 12;
    if (rest.find(L"am") != std::wstring::npos && h == 12) h = 0;
    if (h > 24 || m > 59 || (h == 24 && m > 0)) return fallback;
    return (h * 60 + m) % 1440;
}

inline std::wstring FormatClock(int minutes) {
    minutes = ((minutes % 1440) + 1440) % 1440;
    wchar_t buf[8];
    swprintf(buf, 8, L"%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

// "mon,tue,wed" (also "weekdays", "weekend", "everyday") → the days mask.
inline unsigned ParseDays(std::wstring text) {
    for (auto& c : text) c = static_cast<wchar_t>(towlower(c));
    if (text.find(L"every") != std::wstring::npos || text.find(L"daily") != std::wstring::npos) return 0x7F;
    unsigned mask = 0;
    if (text.find(L"weekday") != std::wstring::npos) mask |= 0x1F;
    if (text.find(L"weekend") != std::wstring::npos) mask |= 0x60;
    static const wchar_t* kDays[] = {L"mon", L"tue", L"wed", L"thu", L"fri", L"sat", L"sun"};
    for (int d = 0; d < 7; ++d)
        if (text.find(kDays[d]) != std::wstring::npos) mask |= 1u << d;
    return mask;
}

// "mon 10:00-11:00 Maths; tue,thu 14:00-16:00 Physics" (also newlines, "weekdays") → blocks.
inline std::vector<Block> ParseTimetable(const std::wstring& text) {
    std::vector<Block> out;
    size_t at = 0;
    while (at <= text.size()) {
        size_t end = text.find_first_of(L";\n", at);
        if (end == std::wstring::npos) end = text.size();
        std::wstring entry = text.substr(at, end - at);
        at = end + 1;
        // days, then the times, then the subject.
        size_t i = 0;
        while (i < entry.size() && iswspace(entry[i])) ++i;
        size_t d0 = i;
        while (i < entry.size() && !iswspace(entry[i]) && !iswdigit(entry[i])) ++i;
        const unsigned days = ParseDays(entry.substr(d0, i - d0));
        while (i < entry.size() && iswspace(entry[i])) ++i;
        const size_t dash = entry.find(L'-', i);
        if (!days || dash == std::wstring::npos) continue;
        const int from = ParseClock(entry.substr(i, dash - i), -1);
        size_t j = dash + 1;
        while (j < entry.size() && iswspace(entry[j])) ++j;
        size_t k = j;
        while (k < entry.size() && (iswdigit(entry[k]) || entry[k] == L':' || entry[k] == L'.')) ++k;
        const int to = ParseClock(entry.substr(j, k - j), -1);
        std::wstring subject = entry.substr(k);
        while (!subject.empty() && iswspace(subject.front())) subject.erase(subject.begin());
        while (!subject.empty() && iswspace(subject.back())) subject.pop_back();
        if (from < 0 || to <= from) continue;
        if (subject.empty()) subject = L"Study";
        for (int d = 0; d < 7; ++d)
            if (days & (1u << d)) out.push_back({d, from, to, subject});
        if (at > text.size()) break;
    }
    return out;
}

// Minutes from start to end (a night shift wraps); 0 when they're the same.
inline int ShiftMinutes(const Schedule& s) { return ((s.end - s.start) % 1440 + 1440) % 1440; }

inline int GoalMinutes(const Schedule& s) {
    if (s.goalMinutes > 0) return s.goalMinutes;
    if (!s.blocks.empty()) return 0;  // Study without a goal: no ring
    return std::max(0, ShiftMinutes(s) - std::max(0, s.lunchMinutes));
}

// ── The day's timeline ──────────────────────────────────────────────────────
enum class Kind : int { Work, Away, Asleep, Lunch, Break, Overtime, Paused, Count };
inline const char* KindKey(Kind k) {
    static const char* kKeys[] = {"work", "away", "asleep", "lunch", "break", "overtime", "paused"};
    return kKeys[static_cast<int>(k)];
}
inline Kind KindFromKey(const std::string& key) {
    for (int k = 0; k < static_cast<int>(Kind::Count); ++k)
        if (key == KindKey(static_cast<Kind>(k))) return static_cast<Kind>(k);
    return Kind::Work;
}

// Seconds after the day's midnight; past 86400 for a night shift's hours after midnight.
// Each kind's colour (0xRRGGBB), the same on the island, the Work page and the report.
inline uint32_t KindColour(Kind k) {
    switch (k) {
        case Kind::Work: return 0x0a84ff;
        case Kind::Overtime: return 0x5e5ce6;
        case Kind::Away: return 0xff9f0a;
        case Kind::Asleep: return 0x8e8e93;
        case Kind::Lunch: return 0x30d158;
        case Kind::Break: return 0x64d2ff;
        default: return 0x636366;
    }
}
inline const wchar_t* KindLabel(Kind k) {
    switch (k) {
        case Kind::Work: return L"Worked";
        case Kind::Overtime: return L"Overtime";
        case Kind::Away: return L"Away";
        case Kind::Asleep: return L"Asleep";
        case Kind::Lunch: return L"Lunch";
        case Kind::Break: return L"Breaks";
        default: return L"Paused";
    }
}

struct Segment {
    int start = 0, end = 0;
    Kind kind = Kind::Work;
};

struct Day {
    int date = 0;                  // yyyymmdd of the work day (a night shift's start date)
    std::vector<Segment> segments;
    int breaks = 0;                // breaks taken (Take a break, or away for a break's length)
    std::map<std::wstring, int> subjects;  // Study: seconds studied per subject
};

// What a day (or several) added up to, in seconds.
struct Summary {
    int seconds[static_cast<int>(Kind::Count)] = {};
    int first = -1, last = -1;     // first and last second of the timeline (-1: nothing yet)
    int breaks = 0;
    int days = 0;
    std::map<std::wstring, int> subjects;
    int Worked() const { return seconds[static_cast<int>(Kind::Work)] + seconds[static_cast<int>(Kind::Overtime)]; }
    int Of(Kind k) const { return seconds[static_cast<int>(k)]; }
};

inline Summary Summarize(const Day& d) {
    Summary s;
    for (const Segment& g : d.segments) {
        if (g.end <= g.start) continue;
        s.seconds[static_cast<int>(g.kind)] += g.end - g.start;
        if (s.first < 0 || g.start < s.first) s.first = g.start;
        s.last = std::max(s.last, g.end);
    }
    s.breaks = d.breaks;
    s.days = d.segments.empty() ? 0 : 1;
    s.subjects = d.subjects;
    return s;
}

inline Summary Add(Summary a, const Summary& b) {
    for (int k = 0; k < static_cast<int>(Kind::Count); ++k) a.seconds[k] += b.seconds[k];
    a.breaks += b.breaks;
    a.days += b.days;
    for (const auto& [name, sec] : b.subjects) a.subjects[name] += sec;
    return a;
}

// ── State ────────────────────────────────────────────────────────────────────
enum class Phase { Off, Working, Away, Asleep, Lunch, Break, Paused, Overtime, Done };

inline const wchar_t* PhaseLabel(Phase p) {
    switch (p) {
        case Phase::Working: return L"Working";  // Study's own words: StudyLabel
        case Phase::Away: return L"Away";
        case Phase::Asleep: return L"Asleep";
        case Phase::Lunch: return L"Lunch";
        case Phase::Break: return L"On a break";
        case Phase::Paused: return L"Paused";
        case Phase::Overtime: return L"Overtime";
        case Phase::Done: return L"Done for today";
        default: return L"Off work";
    }
}

// Study's words for the same states.
inline const wchar_t* StudyLabel(Phase p) {
    switch (p) {
        case Phase::Working: return L"Studying";
        case Phase::Overtime: return L"Extra study";
        case Phase::Done: return L"Done for today";
        case Phase::Off: return L"Free time";
        default: return PhaseLabel(p);
    }
}

// What you did by hand today. Reset when the work day changes.
struct Manual {
    int date = 0;                // the work day it belongs to
    bool started = false;        // Start work (outside the schedule, or with the schedule off)
    bool paused = false;
    bool done = false;           // Done for today: no more tracking until tomorrow
    long long breakUntil = 0;    // Take a break: epoch seconds it ends
};

// One tick's view of the clock and the person.
struct Input {
    long long epoch = 0;         // seconds (for breaks)
    int date = 0;                // today, yyyymmdd
    int prevDate = 0;            // yesterday, yyyymmdd
    int weekday = 0;             // today: 0 = Monday … 6 = Sunday
    int second = 0;              // seconds after midnight
    double idle = 0;             // seconds since the last keyboard or mouse input
    int afkMinutes = 0;          // Bunbun.AfkMinutes (0: off)
    int sleepMinutes = 0;        // Bunbun.SleepMinutes (0: off)
    bool extraActive = false;    // Study: a Pomodoro focus session is running (counts like Start)
};

enum Event : unsigned {
    kLunchStart = 1u << 0,
    kLunchEnd = 1u << 1,
    kBreakDue = 1u << 2,
    kGoalReached = 1u << 3,
    kDayEnded = 1u << 4,         // the scheduled hours ended while you were working
    kDayStarted = 1u << 5,       // the scheduled hours began (or Start work)
    kNewDay = 1u << 6,           // the work day changed: save the previous one
    kBlockStarted = 1u << 7,     // Study: a block of the timetable began (Tracker::subject)
};

class Tracker {
   public:
    Schedule schedule;
    Manual manual;
    Day day;                     // the work day being recorded
    Phase phase = Phase::Off;
    int workedSinceBreak = 0;    // seconds of work since the last break, lunch or long absence
    int awayRun = 0;             // seconds of the current absence
    bool goalCheered = false;
    bool inHours = false;
    int block = -2;              // Study: the timetable block now (-1 none, -2 not looked yet)
    std::wstring subject;        // Study: its subject

    // Study: the timetable block `in` falls in, or -1.
    int BlockAt(const Input& in) const {
        const int minute = in.second / 60;
        for (size_t i = 0; i < schedule.blocks.size(); ++i) {
            const Block& b = schedule.blocks[i];
            if (b.weekday == in.weekday && minute >= b.start && minute < b.end) return static_cast<int>(i);
        }
        return -1;
    }

    // Where `in` falls in the schedule: within hours, the work day it belongs to, and
    // the second within that day (past 86400 after midnight on a night shift).
    void Locate(const Input& in, bool* within, int* workDate, int* second) const {
        const int minute = in.second / 60;
        const bool today = (schedule.days >> in.weekday) & 1u;
        const bool yesterday = (schedule.days >> ((in.weekday + 6) % 7)) & 1u;
        *within = false;
        *workDate = in.date;
        *second = in.second;
        if (!schedule.blocks.empty()) {  // Study: within a block of the timetable
            *within = BlockAt(in) >= 0;
            return;
        }
        if (ShiftMinutes(schedule) == 0) return;
        if (schedule.start < schedule.end) {
            *within = today && minute >= schedule.start && minute < schedule.end;
        } else if (today && minute >= schedule.start) {
            *within = true;
        } else if (yesterday && minute < schedule.end) {
            *within = true;
            *workDate = in.prevDate;
            *second = in.second + 86400;
        }
    }

    bool InLunch(const Input& in) const {
        if (schedule.lunchMinutes <= 0 || !schedule.blocks.empty()) return false;
        const int minute = in.second / 60;
        const int from = schedule.lunchStart, to = (schedule.lunchStart + schedule.lunchMinutes) % 1440;
        return from < to ? (minute >= from && minute < to) : (minute >= from || minute < to);
    }

    void Start(int workDate) { Reset(workDate); manual.started = true; manual.done = false; manual.paused = false; }
    void Pause() { manual.paused = true; }
    void Resume() { manual.paused = false; manual.breakUntil = 0; }
    void TakeBreak(long long epoch) {
        manual.breakUntil = epoch + std::max(1, schedule.breakMinutes) * 60;
        manual.paused = false;
        ++day.breaks;
        workedSinceBreak = 0;
    }
    void Done(int workDate) { Reset(workDate); manual.done = true; manual.started = false; manual.paused = false; }

    // Advances `dt` seconds to `in`. Returns the events that happened.
    unsigned Tick(const Input& in, int dt) {
        unsigned events = 0;
        bool within = false;
        int workDate = in.date, second = in.second;
        Locate(in, &within, &workDate, &second);
        Reset(workDate);
        if (day.date != workDate) {
            if (day.date != 0 && !day.segments.empty()) events |= kNewDay;
            day = Day{};
            day.date = workDate;
            goalCheered = false;
            workedSinceBreak = 0;
        }
        const bool wasInHours = inHours;
        inHours = within;
        const Phase before = phase;
        // Study: which block of the timetable, and its subject.
        if (!schedule.blocks.empty()) {
            const int now = BlockAt(in);
            if (now != block && now >= 0 && block != -2) events |= kBlockStarted;
            block = now;
            subject = now >= 0 ? schedule.blocks[static_cast<size_t>(now)].subject : in.extraActive ? std::wstring(L"Pomodoro") : std::wstring();
        }
        const bool active = !manual.done && ((schedule.autoStart && within) || manual.started || in.extraActive);
        if (!active) {
            phase = manual.done ? Phase::Done : Phase::Off;
            if (wasInHours && !within && Counted(before) && schedule.blocks.empty()) events |= kDayEnded;  // not after each class
            awayRun = 0;
            return events;
        }
        if (within && !wasInHours && schedule.autoStart && schedule.blocks.empty()) events |= kDayStarted;

        const double afk = in.afkMinutes > 0 ? in.afkMinutes * 60.0 : 1e18;
        const double asleep = in.sleepMinutes > 0 ? in.sleepMinutes * 60.0 : 1e18;
        if (manual.paused) phase = Phase::Paused;
        else if (manual.breakUntil > in.epoch) phase = Phase::Break;
        else if (within && InLunch(in)) phase = Phase::Lunch;
        else if (in.idle >= asleep) phase = Phase::Asleep;
        else if (in.idle >= afk) phase = Phase::Away;
        else phase = (within || !schedule.autoStart) ? Phase::Working : Phase::Overtime;  // overtime only past your hours
        if (manual.breakUntil && manual.breakUntil <= in.epoch) manual.breakUntil = 0;

        if (phase == Phase::Lunch && before != Phase::Lunch) events |= kLunchStart;
        if (before == Phase::Lunch && phase != Phase::Lunch) events |= kLunchEnd;

        const Kind kind = KindOf(phase);
        if (!Counted(before)) dt = 0;  // the first tick of a stretch adds no time before it
        // Becoming away: the idle minutes before it weren't work either.
        if ((phase == Phase::Away || phase == Phase::Asleep) && (before == Phase::Working || before == Phase::Overtime))
            Reclassify(second - static_cast<int>(in.idle), second, Kind::Away);
        Extend(second, dt, kind);

        if (phase == Phase::Working || phase == Phase::Overtime) {
            if (!schedule.blocks.empty()) day.subjects[subject.empty() ? std::wstring(L"Other") : subject] += dt;
            workedSinceBreak += dt;
            if (awayRun >= schedule.breakMinutes * 60 && schedule.breakMinutes > 0) { ++day.breaks; workedSinceBreak = dt; }
            awayRun = 0;
            if (schedule.breakEvery > 0 && workedSinceBreak >= schedule.breakEvery * 60) {
                events |= kBreakDue;
                workedSinceBreak = 0;
            }
        } else if (phase == Phase::Away || phase == Phase::Asleep) {
            awayRun += dt;
        } else {
            workedSinceBreak = 0;  // lunch, a break, paused
            awayRun = 0;
        }
        const int goal = GoalMinutes(schedule) * 60;
        if (!goalCheered && goal > 0 && Summarize(day).Worked() >= goal) {
            goalCheered = true;
            events |= kGoalReached;
        }
        return events;
    }

    static bool Counted(Phase p) { return p != Phase::Off && p != Phase::Done; }
    static Kind KindOf(Phase p) {
        switch (p) {
            case Phase::Away: return Kind::Away;
            case Phase::Asleep: return Kind::Asleep;
            case Phase::Lunch: return Kind::Lunch;
            case Phase::Break: return Kind::Break;
            case Phase::Paused: return Kind::Paused;
            case Phase::Overtime: return Kind::Overtime;
            default: return Kind::Work;
        }
    }

   private:
    void Reset(int workDate) {
        if (manual.date == workDate) return;
        manual = Manual{};
        manual.date = workDate;
    }

    // Adds [second - dt, second) of `kind`, joining the last segment when it touches.
    void Extend(int second, int dt, Kind kind) {
        const int from = std::max(0, second - dt);
        if (!day.segments.empty()) {
            Segment& last = day.segments.back();
            if (last.kind == kind && from - last.end <= 30 && second >= last.end) {
                last.end = second;
                return;
            }
            if (from < last.end) last.end = std::max(last.start, from);  // never overlap
        }
        day.segments.push_back({from, second, kind});
    }

    // Turns work in [from, to) into `kind` (the idle stretch before an absence was noticed).
    void Reclassify(int from, int to, Kind kind) {
        from = std::max(0, from);
        if (from >= to) return;
        std::vector<Segment> out;
        for (const Segment& g : day.segments) {
            const bool work = g.kind == Kind::Work || g.kind == Kind::Overtime;
            if (!work || g.end <= from || g.start >= to) { out.push_back(g); continue; }
            if (g.start < from) out.push_back({g.start, from, g.kind});
            out.push_back({std::max(g.start, from), std::min(g.end, to), kind});
            if (g.end > to) out.push_back({to, g.end, g.kind});
        }
        // Join neighbours of the same kind.
        std::vector<Segment> joined;
        for (const Segment& g : out) {
            if (!joined.empty() && joined.back().kind == g.kind && g.start - joined.back().end <= 30)
                joined.back().end = std::max(joined.back().end, g.end);
            else
                joined.push_back(g);
        }
        day.segments = std::move(joined);
    }
};

// yyyymmdd of the day before `date`.
inline int PrevDate(int date) {
    int y = date / 10000, m = date / 100 % 100, d = date % 100;
    if (--d < 1) {
        if (--m < 1) { m = 12; --y; }
        static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        d = kDays[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0);
    }
    return y * 10000 + m * 100 + d;
}

// UTF-8 for subject names in the file.
inline std::string ToUtf8(const std::wstring& w) {
    std::string out;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t c = static_cast<uint32_t>(w[i]);
        if (sizeof(wchar_t) == 2 && c >= 0xD800 && c < 0xDC00 && i + 1 < w.size()) c = 0x10000 + ((c - 0xD800) << 10) + (static_cast<uint32_t>(w[++i]) - 0xDC00);
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    }
    return out;
}
inline std::wstring FromUtf8(const std::string& s) {
    std::wstring out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = c;
        int n = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        if (n) cp = c & (0x3F >> n);
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        } else {
            out += static_cast<wchar_t>(cp);
        }
    }
    return out;
}

// ── The file (work.txt) ──────────────────────────────────────────────────────
// One line per segment: "yyyymmdd<TAB>start<TAB>end<TAB>kind", a "breaks" line per
// day, and the manual state. Keeps the last 180 days.
struct Log {
    std::map<int, Day> days;
    Manual manual;
};

inline Log Parse(const std::string& text) {
    Log log;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("manual=", 0) == 0) {
            long long v[5] = {};
            int n = 0;
            std::istringstream f(line.substr(7));
            std::string part;
            while (n < 5 && std::getline(f, part, ',')) v[n++] = std::atoll(part.c_str());
            log.manual.date = static_cast<int>(v[0]);
            log.manual.started = v[1] != 0;
            log.manual.paused = v[2] != 0;
            log.manual.done = v[3] != 0;
            log.manual.breakUntil = v[4];
            continue;
        }
        std::istringstream f(line);
        std::string a, b, c, d;
        if (!std::getline(f, a, '\t') || !std::getline(f, b, '\t') || !std::getline(f, c, '\t')) continue;
        const int date = std::atoi(a.c_str());
        if (date < 19700101) continue;
        Day& day = log.days[date];
        day.date = date;
        if (b == "breaks") { day.breaks = std::atoi(c.c_str()); continue; }
        std::getline(f, d, '\t');
        if (b == "subject") { if (!d.empty()) day.subjects[FromUtf8(d)] = std::atoi(c.c_str()); continue; }
        Segment g{std::atoi(b.c_str()), std::atoi(c.c_str()), KindFromKey(d)};
        if (g.end > g.start) day.segments.push_back(g);
    }
    return log;
}

// Days in a row, up to `today`, that met `goalSeconds` (today counts once it has;
// until then the streak runs to yesterday). `current` is today's live day.
inline int Streak(const Log& log, const Day& current, int today, int goalSeconds) {
    if (goalSeconds <= 0) return 0;
    auto met = [&](int date) {
        const Day* d = nullptr;
        if (current.date == date) d = &current;
        else if (auto it = log.days.find(date); it != log.days.end()) d = &it->second;
        return d && Summarize(*d).Worked() >= goalSeconds;
    };
    int n = 0, date = today;
    if (met(date)) ++n;
    date = PrevDate(date);
    for (int i = 0; i < 400 && met(date); ++i, date = PrevDate(date)) ++n;
    return n;
}

inline std::string Serialize(const Log& log, int keepDays = 180) {
    std::string out;
    out += "manual=" + std::to_string(log.manual.date) + "," + (log.manual.started ? "1" : "0") + "," +
           (log.manual.paused ? "1" : "0") + "," + (log.manual.done ? "1" : "0") + "," +
           std::to_string(log.manual.breakUntil) + "\n";
    int kept = 0;
    for (auto it = log.days.rbegin(); it != log.days.rend() && kept < keepDays; ++it, ++kept) {
        const Day& d = it->second;
        if (d.breaks) out += std::to_string(d.date) + "\tbreaks\t" + std::to_string(d.breaks) + "\n";
        for (const auto& [name, sec] : d.subjects)
            out += std::to_string(d.date) + "\tsubject\t" + std::to_string(sec) + "\t" + ToUtf8(name) + "\n";
        for (const Segment& g : d.segments)
            out += std::to_string(d.date) + "\t" + std::to_string(g.start) + "\t" + std::to_string(g.end) + "\t" +
                   KindKey(g.kind) + "\n";
    }
    return out;
}

// "3h 12m", "45m", "0m".
inline std::wstring Duration(int seconds) {
    const int m = std::max(0, seconds) / 60;
    if (m < 60) return std::to_wstring(m) + L"m";
    return std::to_wstring(m / 60) + L"h " + std::to_wstring(m % 60) + L"m";
}

}  // namespace bb::work
