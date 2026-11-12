// Bunbun — calendar links on Linux (see calendar_linux.h).
#include "calendar_linux.h"

#include <glib.h>

#include <cmath>
#include <ctime>
#include <cwctype>

#include "platform.h"
#include "settings.h"
#include "sources.h"

namespace bb::lcal {
namespace {

std::function<void()> g_changed;
std::wstring g_urls;
int g_alertMinutes = 10;
std::vector<bb::ics::Event> g_events;
bb::ics::Meeting g_next;
bool g_failed = false;
double g_dismissedStart = 0;
int g_pending = 0;  // fetches still running
std::vector<bb::ics::Event> g_fresh;
bool g_anyOk = false;

// Wall time in a calendar's time zone (an IANA name such as "Europe/London";
// Outlook's Windows names fall back to local time) to Unix seconds, through GLib.
double ToEpoch(const bb::ics::WallTime& w, const std::wstring& tzid) {
    GTimeZone* tz = nullptr;
    if (!tzid.empty()) tz = g_time_zone_new_identifier(bb::plat::Utf8(tzid).c_str());
    if (!tz) tz = g_time_zone_new_local();
    GDateTime* dt = g_date_time_new(tz, w.y, w.mo, w.d, w.h, w.mi, w.s);
    const double epoch = dt ? static_cast<double>(g_date_time_to_unix(dt)) : 0.0;
    if (dt) g_date_time_unref(dt);
    g_time_zone_unref(tz);
    return epoch;
}

double EpochNow() { return static_cast<double>(time(nullptr)); }

// The local yyyymmdd of a Unix time.
int YmdOf(double epoch) {
    const time_t t = static_cast<time_t>(epoch);
    tm lt{};
    localtime_r(&t, &lt);
    return (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
}

std::vector<int> g_leave;  // all-day leave days from the links (work mode skips them)

// The all-day events of the weeks around now: the settings app's days-off calendar reads
// calendar-days.txt (as on Windows), and work mode takes the leave days.
void PublishDays() {
    const double now = EpochNow();
    const std::vector<bb::ics::Meeting> items = bb::ics::MeetingsBetween(g_events, now - 42 * 86400.0, now + 70 * 86400.0, ToEpoch, true);
    g_leave = bb::ics::LeaveDates(items, YmdOf);
    static std::string last = "\x01";
    const std::string text = bb::plat::Utf8(bb::ics::AllDayText(items, YmdOf));
    if (text == last) return;
    last = text;
    bb::plat::WriteFileAtomic(bb::plat::StateDir() + "/calendar-days.txt", text);
}

void Recompute() {
    const bb::ics::Meeting next = bb::ics::NextMeeting(g_events, EpochNow(), ToEpoch);
    const bool changed = next.valid != g_next.valid || next.startsAt != g_next.startsAt || next.title != g_next.title;
    g_next = next;
    if (changed && g_changed) g_changed();
}

std::vector<std::string> Links() {
    std::vector<std::string> out;
    std::wstring u;
    for (size_t i = 0; i <= g_urls.size(); ++i) {  // several links, separated by spaces, commas or new lines
        if (i == g_urls.size() || iswspace(g_urls[i]) || g_urls[i] == L',') {
            if (!u.empty()) {
                std::string s = bb::plat::Utf8(u);
                if (s.rfind("webcal://", 0) == 0) s = "https://" + s.substr(9);
                out.push_back(s);
            }
            u.clear();
        } else {
            u += g_urls[i];
        }
    }
    return out;
}

void Refresh() {
    if (g_pending > 0) return;
    const std::vector<std::string> links = Links();
    if (links.empty()) {
        g_events.clear();
        g_failed = false;
        PublishDays();
        Recompute();
        return;
    }
    g_fresh.clear();
    g_anyOk = false;
    g_failed = false;
    g_pending = static_cast<int>(links.size());
    for (const std::string& url : links) {
        bb::lsrc::FetchUrl(url, [](const std::string& body) {
            if (body.find("BEGIN:VCALENDAR") != std::string::npos) {
                std::vector<bb::ics::Event> parsed = bb::ics::Parse(bb::plat::Wide(body), ToEpoch);
                g_fresh.insert(g_fresh.end(), std::make_move_iterator(parsed.begin()), std::make_move_iterator(parsed.end()));
                g_anyOk = true;
            } else {
                g_failed = true;  // the link itself is private: not logged
            }
            if (--g_pending == 0) {
                if (g_anyOk) g_events = std::move(g_fresh);
                PublishDays();
                Recompute();
                if (g_changed) g_changed();
            }
        });
    }
}

}  // namespace

void Start(std::function<void()> changed) {
    g_changed = std::move(changed);
    LoadSettings();
    // Fetch every five minutes (a minute after a failure); re-pick the next meeting
    // every 20 s, so it moves on when one ends.
    g_timeout_add_seconds(20, [](gpointer) -> gboolean {
        static int ticks = 0;
        ++ticks;
        if (ticks % 15 == 0 || (g_failed && ticks % 3 == 0)) Refresh();
        Recompute();
        return G_SOURCE_CONTINUE;
    }, nullptr);
}

void LoadSettings() {
    namespace s = bb::settings;
    std::wstring urls = s::Get(L"Modules.CalendarIcsUrl");
    // A country's public holidays and festivals ride along as one more link (Modules.CalendarHolidays, as on Windows).
    const std::wstring region = s::Get(L"Modules.CalendarHolidays");
    if (!region.empty() && region != L"none" && region.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz_") == std::wstring::npos)
        urls += L" https://calendar.google.com/calendar/ical/en." + region + L"%23holiday%40group.v.calendar.google.com/public/basic.ics";
    g_alertMinutes = std::max(0, s::GetInt(L"Modules.MeetingAlertMinutes"));
    if (urls != g_urls) {
        g_urls = urls;
        Refresh();
    }
}

const bb::ics::Meeting& Next() { return g_next; }

std::vector<bb::ics::Meeting> Today() {
    const time_t now = time(nullptr);
    tm lt{};
    localtime_r(&now, &lt);
    lt.tm_hour = lt.tm_min = lt.tm_sec = 0;
    lt.tm_isdst = -1;
    const double start = static_cast<double>(mktime(&lt));
    return bb::ics::MeetingsBetween(g_events, start, start + 86400.0, ToEpoch);
}

int AlertMinutes() { return g_alertMinutes; }
bool FetchFailed() { return g_failed; }
const std::vector<int>& LeaveDays() { return g_leave; }

bool PillWanted(double now) {
    const bb::ics::Meeting& m = g_next;
    if (!m.valid || g_alertMinutes <= 0 || m.startsAt == g_dismissedStart) return false;
    return now >= m.startsAt - g_alertMinutes * 60.0 && now < m.startsAt + 5 * 60.0;
}

void Dismiss() { g_dismissedStart = g_next.startsAt; }

void Join() {
    if (!g_next.joinUrl.empty()) bb::plat::OpenWithDefault(bb::plat::Utf8(g_next.joinUrl));
    Dismiss();
}

}  // namespace bb::lcal
