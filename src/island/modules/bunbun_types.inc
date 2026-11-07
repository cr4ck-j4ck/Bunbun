// Bunbun: data types for the Bunbun modules (connectivity lists, calendar link,
// downloads). Included in the core's anonymous namespace before SharedState,
// which holds one of each.

// One Wi-Fi network from the last scan, shown in the long-press list.
struct WifiNetwork {
    std::wstring ssid;
    std::wstring profile;  // saved profile name; empty when Windows has none (needs a password)
    int signal = 0;        // 0-100
    bool secured = false;
    bool connected = false;
};

// One paired Bluetooth device, shown in the long-press list. Only audio devices
// can be connected or disconnected from Bunbun (through their audio driver);
// the others open Bluetooth settings.
struct BluetoothListDevice {
    std::wstring name;
    bool connected = false;
    bool audio = false;
};

// The next meeting from the user's calendar link(s), with a join link when the
// event carries one (Meet, Zoom, Teams, Webex).
// One event from the calendar link, for the Calendar page's month and week views.
struct CalendarItem {
    double startsAt = 0.0, endsAt = 0.0;  // Unix seconds (EpochNow)
    std::wstring title;
};

struct MeetingSnapshot {
    bool valid = false;
    std::wstring title;
    std::wstring joinUrl;
    double startsAt = 0.0;  // Unix seconds (EpochNow)
    double endsAt = 0.0;
    bool fetchFailed = false;
};

// "Discord is using your microphone": shown briefly when an app starts using the
// microphone (kind 0), camera (1) or screen (2).
struct PrivacyAlertSnapshot {
    bool active = false;
    std::wstring text;
    int kind = 0;
    double expiresAt = 0.0;
};

// One to-do; with a due time it's also a reminder (src/common/todos.h, shared
// with the settings app).
using TodoItem = bb::todos::Item;

// A reminder that's due, shown as a pill with Done and Snooze.
struct ReminderSnapshot {
    bool active = false;
    int id = 0;
    std::wstring text;
    double expiresAt = 0.0;  // NowSeconds clock
    bool quiet = false;      // small, silent pill (full screen, presenting, meeting, focus)
};

// Today's water, for the Water page (water_module.inc).
struct WaterSnapshot {
    int goalMl = 2000, glassMl = 250, bottleMl = 0;
    int takenMl = 0, glasses = 0, bottleLeftMl = 0;
    bool reminders = false;
    int nextMinute = -1;                    // minute of the day of the next reminder; -1 = none today
    std::vector<std::pair<int, int>> week;  // (yyyymmdd, ml), oldest first, today last
};

// Bunbun: Work mode's day, for the Home card, the Work page and the mascot (work_module.inc).
struct WorkSnapshot {
    bool on = false;                        // Bunbun.UseCase is work or both
    int phase = 0;                          // bb::work::Phase
    int seconds[7] = {};                    // today, by bb::work::Kind
    int goal = 0;                           // the day's goal, seconds
    int breaks = 0;
    int first = -1, last = -1;              // the timeline's span, seconds after midnight
    int start = 540, end = 1080, lunchStart = 780, lunchMinutes = 45;  // the schedule, minutes
    bool autoStart = true, inHours = false;
    int workDate = 0;
    long long breakUntil = 0;               // epoch seconds a break ends
    std::vector<bb::work::Segment> segments;
    // Study (Bunbun.UseCase = study): the subject now, when its block ends, today's
    // subjects (most studied first) and the streak of days that met the goal.
    bool study = false;
    std::wstring subject;
    int blockEnd = -1;                      // minutes after midnight
    int nextBlock = -1;                     // the next block today: its start, minutes (-1: none)
    std::wstring nextSubject;
    std::vector<std::pair<std::wstring, int>> subjects;
    int streak = 0;
    int Worked() const { return seconds[0] + seconds[5]; }
};

// The reminder pill shows water reminders under these ids (water_module.inc).
constexpr int kWaterDrinkId = -7001;
constexpr int kWaterRefillId = -7002;
inline bool IsWaterReminderId(int id) { return id == kWaterDrinkId || id == kWaterRefillId; }

// A browser download in progress (or just finished) in the Downloads folder.
struct DownloadSnapshot {
    bool active = false;
    bool finished = false;
    std::wstring name;
    std::wstring path;          // the finished file, once it exists
    ULONG64 bytes = 0;
    double bytesPerSecond = 0.0;
    int others = 0;             // further downloads running at the same time
    double expiresAt = 0.0;     // finished card only
};
