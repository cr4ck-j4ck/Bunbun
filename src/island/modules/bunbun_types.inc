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
