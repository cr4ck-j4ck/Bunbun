// Bunbun — the Linux island's data sources (see sources.h).
#include "sources.h"

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>

#include "../../common/json.h"
#include "island_api.h"
#include "platform.h"
#include "settings.h"

namespace bb::lsrc {
namespace {

std::function<void()> g_changed;
void Changed() { if (g_changed) g_changed(); }
double Mono() { return g_get_monotonic_time() / 1e6; }

// ── System ──────────────────────────────────────────────────────────────────

Load g_load;
Battery g_battery;
double g_sampledAt = -10;
unsigned long long g_cpuIdle = 0, g_cpuTotal = 0, g_rx = 0, g_tx = 0;
double g_netAt = 0;

void Sample() {
    const double now = Mono();
    if (now - g_sampledAt < 1.5) return;
    g_sampledAt = now;
    g_battery = Battery{};
    if (DIR* d = opendir("/sys/class/power_supply")) {
        while (dirent* e = readdir(d)) {
            if (std::string(e->d_name).rfind("BAT", 0) != 0) continue;
            const std::string dir = std::string("/sys/class/power_supply/") + e->d_name;
            std::string cap, status;
            if (!bb::plat::ReadFile(dir + "/capacity", &cap)) continue;
            bb::plat::ReadFile(dir + "/status", &status);
            g_battery.has = true;
            g_battery.percent = std::clamp(atoi(cap.c_str()), 0, 100);
            g_battery.charging = status.rfind("Charging", 0) == 0 || status.rfind("Full", 0) == 0;
            break;
        }
        closedir(d);
    }
    std::string stat;
    if (bb::plat::ReadFile("/proc/stat", &stat) && stat.rfind("cpu ", 0) == 0) {
        unsigned long long v[8] = {};
        sscanf(stat.c_str() + 4, "%llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
        const unsigned long long idle = v[3] + v[4], total = v[0] + v[1] + v[2] + v[3] + v[4] + v[5] + v[6] + v[7];
        if (g_cpuTotal && total > g_cpuTotal)
            g_load.cpu = static_cast<int>(100.0 * (1.0 - double(idle - g_cpuIdle) / double(total - g_cpuTotal)) + 0.5);
        g_cpuIdle = idle;
        g_cpuTotal = total;
    }
    std::string mem;
    if (bb::plat::ReadFile("/proc/meminfo", &mem)) {
        long long total = 0, avail = 0;
        if (const size_t t = mem.find("MemTotal:"); t != std::string::npos) total = atoll(mem.c_str() + t + 9);
        if (const size_t a = mem.find("MemAvailable:"); a != std::string::npos) avail = atoll(mem.c_str() + a + 13);
        if (total > 0) {
            g_load.mem = static_cast<int>(100.0 * (1.0 - double(avail) / double(total)) + 0.5);
            g_load.memTotalGB = total / 1048576.0;
            g_load.memUsedGB = (total - avail) / 1048576.0;
        }
    }
    struct statvfs fs{};
    if (statvfs("/", &fs) == 0 && fs.f_blocks > 0)
        g_load.disk = static_cast<int>(100.0 * (1.0 - double(fs.f_bavail) / double(fs.f_blocks)) + 0.5);
    // Every interface but loopback, from /proc/net/dev.
    std::string dev;
    if (bb::plat::ReadFile("/proc/net/dev", &dev)) {
        unsigned long long rx = 0, tx = 0;
        size_t pos = dev.find('\n', dev.find('\n') + 1);
        while (pos != std::string::npos && pos + 1 < dev.size()) {
            const size_t end = dev.find('\n', pos + 1);
            const std::string line = dev.substr(pos + 1, end == std::string::npos ? std::string::npos : end - pos - 1);
            pos = end;
            const size_t colon = line.find(':');
            if (colon == std::string::npos || line.find("lo:") != std::string::npos) continue;
            unsigned long long v[9] = {};
            if (sscanf(line.c_str() + colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4],
                       &v[5], &v[6], &v[7], &v[8]) == 9) {
                rx += v[0];
                tx += v[8];
            }
        }
        if (g_netAt > 0 && rx >= g_rx && tx >= g_tx) {
            const double dt = std::max(0.1, now - g_netAt);
            g_load.rxKBs = (rx - g_rx) / 1024.0 / dt;
            g_load.txKBs = (tx - g_tx) / 1024.0 / dt;
        }
        g_rx = rx;
        g_tx = tx;
        g_netAt = now;
    }
    struct sysinfo info{};
    if (sysinfo(&info) == 0) g_load.uptime = info.uptime;
}

// ── Web requests ────────────────────────────────────────────────────────────
// GIO fetches https:// where GVfs is installed (every GNOME and KDE desktop);
// elsewhere curl does it. Calls back on the main loop with the body, or "".

using Fetched = std::function<void(const std::string&)>;

void FetchWithCurl(const std::string& url, Fetched done) {
    GError* err = nullptr;
    GSubprocess* proc = g_subprocess_new(static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE),
                                         &err, "curl", "-fsSL", "--max-time", "15", url.c_str(), nullptr);
    if (!proc) {
        g_clear_error(&err);
        done({});
        return;
    }
    g_subprocess_communicate_utf8_async(proc, nullptr, nullptr, [](GObject* src, GAsyncResult* res, gpointer p) {
        auto* cb = static_cast<Fetched*>(p);
        char* out = nullptr;
        GError* e = nullptr;
        const bool ok = g_subprocess_communicate_utf8_finish(G_SUBPROCESS(src), res, &out, nullptr, &e) &&
                        g_subprocess_get_successful(G_SUBPROCESS(src));
        g_clear_error(&e);
        (*cb)(ok && out ? std::string(out) : std::string());
        g_free(out);
        g_object_unref(src);
        delete cb;
    }, new Fetched(std::move(done)));
}

void Fetch(const std::string& url, Fetched done) {
    GFile* file = g_file_new_for_uri(url.c_str());
    struct Job { std::string url; Fetched done; };
    g_file_load_contents_async(file, nullptr, [](GObject* src, GAsyncResult* res, gpointer p) {
        auto* job = static_cast<Job*>(p);
        char* data = nullptr;
        gsize len = 0;
        GError* err = nullptr;
        if (g_file_load_contents_finish(G_FILE(src), res, &data, &len, nullptr, &err)) {
            job->done(std::string(data, len));
            g_free(data);
        } else {
            g_clear_error(&err);
            FetchWithCurl(job->url, std::move(job->done));  // no GVfs http(s) here
        }
        delete job;
    }, new Job{url, std::move(done)});
    g_object_unref(file);
}

std::string UrlEncode(const std::string& s) {
    char* e = g_uri_escape_string(s.c_str(), nullptr, FALSE);
    std::string out = e ? e : "";
    g_free(e);
    return out;
}

// ── Weather ─────────────────────────────────────────────────────────────────

Weather g_weather;
std::string g_weatherCity;  // Modules.WeatherCity ("" = where you are, from your IP)
bool g_weatherOn = true;
double g_weatherAt = -1e9;
int g_weatherGen = 0;  // drops answers to a request a newer one replaced

void Forecast(double lat, double lon, const std::string& place, int gen) {
    char url[512];
    snprintf(url, sizeof url,
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code"
             "&hourly=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&forecast_days=2&timezone=auto%s",
             lat, lon, g_weather.fahrenheit ? "&temperature_unit=fahrenheit" : "");
    Fetch(url, [place, gen](const std::string& body) {
        if (gen != g_weatherGen) return;
        g_weather.loading = false;
        json::Value v;
        const json::Value* cur = nullptr;
        if (body.empty() || !json::Parse(body, &v) || !(cur = v.find("current"))) {
            g_weather.error = "Couldn't reach the weather service";
            Changed();
            return;
        }
        g_weather.has = true;
        g_weather.error.clear();
        g_weather.city = place;
        g_weather.temp = cur->getNumber("temperature_2m");
        g_weather.code = static_cast<int>(cur->getNumber("weather_code"));
        if (const json::Value* d = v.find("daily")) {
            const json::Value* hi = d->find("temperature_2m_max");
            const json::Value* lo = d->find("temperature_2m_min");
            if (hi && hi->isArray() && !hi->arr.empty()) g_weather.high = hi->arr[0].num;
            if (lo && lo->isArray() && !lo->arr.empty()) g_weather.low = lo->arr[0].num;
        }
        g_weather.hours.clear();
        const json::Value* h = v.find("hourly");
        const json::Value* times = h ? h->find("time") : nullptr;
        const json::Value* temps = h ? h->find("temperature_2m") : nullptr;
        const json::Value* codes = h ? h->find("weather_code") : nullptr;
        if (times && temps && codes && times->isArray()) {
            // Times are local ("2026-11-03T15:00"): start at the coming hour.
            const time_t now = time(nullptr);
            tm lt{};
            localtime_r(&now, &lt);
            char key[32];
            strftime(key, sizeof key, "%Y-%m-%dT%H:00", &lt);
            size_t i = 0;
            while (i < times->arr.size() && times->arr[i].str <= key) ++i;
            for (; i < times->arr.size() && g_weather.hours.size() < 5 && i < temps->arr.size() && i < codes->arr.size(); ++i)
                g_weather.hours.push_back({atoi(times->arr[i].str.c_str() + 11), temps->arr[i].num, static_cast<int>(codes->arr[i].num)});
        }
        Changed();
    });
}

void RefreshWeather() {
    if (!g_weatherOn) return;
    g_weatherAt = Mono();
    const int gen = ++g_weatherGen;
    if (!g_weatherCity.empty()) {
        Fetch("https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=" + UrlEncode(g_weatherCity),
              [gen](const std::string& body) {
                  if (gen != g_weatherGen) return;
                  json::Value v;
                  const json::Value* r = nullptr;
                  if (body.empty() || !json::Parse(body, &v) || !(r = v.find("results")) || !r->isArray() || r->arr.empty()) {
                      g_weather.loading = false;
                      g_weather.error = body.empty() ? "Couldn't reach the weather service" : "Couldn't find " + g_weatherCity;
                      Changed();
                      return;
                  }
                  const json::Value& place = r->arr[0];
                  Forecast(place.getNumber("latitude"), place.getNumber("longitude"), place.getString("name"), gen);
              });
        return;
    }
    // No city set: roughly where you are, from your IP address (as Windows does
    // without location access).
    Fetch("https://api.bigdatacloud.net/data/reverse-geocode-client?localityLanguage=en", [gen](const std::string& body) {
        if (gen != g_weatherGen) return;
        json::Value v;
        if (body.empty() || !json::Parse(body, &v) || !v.find("latitude")) {
            g_weather.loading = false;
            g_weather.error = "Set your city in Settings to see the weather";
            Changed();
            return;
        }
        std::string city = v.getString("city");
        if (city.empty()) city = v.getString("locality");
        Forecast(v.getNumber("latitude"), v.getNumber("longitude"), city, gen);
    });
}

// ── Quick toggles ───────────────────────────────────────────────────────────

Radios g_radios;
GDBusConnection* g_system = nullptr;

GVariant* GetProp(const char* bus, const char* path, const char* iface, const char* name) {
    if (!g_system) return nullptr;
    GVariant* r = g_dbus_connection_call_sync(g_system, bus, path, "org.freedesktop.DBus.Properties", "Get",
                                              g_variant_new("(ss)", iface, name), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE,
                                              300, nullptr, nullptr);
    if (!r) return nullptr;
    GVariant* v = nullptr;
    g_variant_get(r, "(v)", &v);
    g_variant_unref(r);
    return v;
}

bool SetProp(const char* bus, const char* path, const char* iface, const char* name, GVariant* value) {
    if (!g_system) return false;
    GError* err = nullptr;
    GVariant* r = g_dbus_connection_call_sync(g_system, bus, path, "org.freedesktop.DBus.Properties", "Set",
                                              g_variant_new("(ssv)", iface, name, value), nullptr,
                                              G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION, 5000, nullptr, &err);
    if (err) {
        Wh_Log(L"toggle: %ls", bb::plat::Wide(err->message).c_str());
        g_clear_error(&err);
    }
    if (r) g_variant_unref(r);
    return r != nullptr;
}

// GNOME's "Do Not Disturb" is its notification banners switch.
GSettings* NotificationSettings() {
    static GSettings* s = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
        GSettingsSchemaSource* src = g_settings_schema_source_get_default();
        if (GSettingsSchema* schema = src ? g_settings_schema_source_lookup(src, "org.gnome.desktop.notifications", TRUE) : nullptr) {
            if (g_settings_schema_has_key(schema, "show-banners")) s = g_settings_new("org.gnome.desktop.notifications");
            g_settings_schema_unref(schema);
        }
    }
    return s;
}

void RefreshRadios() {
    Radios r;
    if (GVariant* v = GetProp("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.NetworkManager",
                              "WirelessEnabled")) {
        r.wifiKnown = true;
        r.wifi = g_variant_get_boolean(v);
        g_variant_unref(v);
        if (GVariant* p = GetProp("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager",
                                  "org.freedesktop.NetworkManager", "PrimaryConnection")) {
            const char* path = g_variant_get_string(p, nullptr);
            if (path && strcmp(path, "/") != 0)
                if (GVariant* id = GetProp("org.freedesktop.NetworkManager", path, "org.freedesktop.NetworkManager.Connection.Active", "Id")) {
                    r.network = g_variant_get_string(id, nullptr);
                    g_variant_unref(id);
                }
            g_variant_unref(p);
        }
    }
    if (GVariant* v = GetProp("org.bluez", "/org/bluez/hci0", "org.bluez.Adapter1", "Powered")) {
        r.btKnown = true;
        r.bt = g_variant_get_boolean(v);
        g_variant_unref(v);
    }
    if (GSettings* s = NotificationSettings()) {
        r.dndKnown = true;
        r.dnd = !g_settings_get_boolean(s, "show-banners");
    }
    const bool changed = r.wifi != g_radios.wifi || r.bt != g_radios.bt || r.dnd != g_radios.dnd || r.network != g_radios.network ||
                         r.wifiKnown != g_radios.wifiKnown || r.btKnown != g_radios.btKnown;
    g_radios = r;
    if (changed) Changed();
}

// ── Clipboard ───────────────────────────────────────────────────────────────

std::vector<std::string> g_clips;
bool g_clipOn = true;
bool g_clipSelfSet = false;  // the next change is our own copy

void OnClipboardText(GtkClipboard*, const gchar* text, gpointer) {
    if (!text || !g_clipOn) return;
    std::string t = text;
    while (!t.empty() && isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    if (t.empty()) return;
    if (t.size() > 4000) t.resize(4000);
    g_clips.erase(std::remove(g_clips.begin(), g_clips.end(), t), g_clips.end());
    g_clips.insert(g_clips.begin(), t);
    if (g_clips.size() > 8) g_clips.resize(8);
    Changed();
}

// ── Note ────────────────────────────────────────────────────────────────────

std::string NotePath() { return bb::plat::ConfigDir() + "/note.txt"; }
std::vector<std::wstring> g_note;
double g_noteCheckedAt = -10;
timespec g_noteWritten{};

void ReadNote() {
    const double now = Mono();
    if (now - g_noteCheckedAt < 1.0) return;
    g_noteCheckedAt = now;
    struct stat st{};
    if (stat(NotePath().c_str(), &st) != 0) {
        g_note.clear();
        return;
    }
    if (st.st_mtim.tv_sec == g_noteWritten.tv_sec && st.st_mtim.tv_nsec == g_noteWritten.tv_nsec) return;
    g_noteWritten = st.st_mtim;
    std::string data;
    bb::plat::ReadFile(NotePath(), &data);
    if (data.size() > 64 * 1024) data.resize(64 * 1024);
    if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);
    const std::wstring text = bb::plat::Wide(data);
    g_note.clear();
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        g_note.push_back(std::move(line));
        start = end + 1;
    }
    while (!g_note.empty() && g_note.back().empty()) g_note.pop_back();
}

// ── To-dos ──────────────────────────────────────────────────────────────────

std::wstring TodosPath() { return bb::plat::Wide(bb::plat::ConfigDir() + "/todos.tsv"); }
std::vector<bb::todos::Item> g_todos;
double g_todosAt = -10;

void ReadTodos(bool force = false) {
    const double now = Mono();
    if (!force && now - g_todosAt < 2.0) return;
    g_todosAt = now;
    g_todos = bb::todos::Load(TodosPath());
}

// ── Focus ───────────────────────────────────────────────────────────────────

Focus g_focus;
bool g_focusFinished = false;

}  // namespace

void Start(std::function<void()> changed) {
    g_changed = std::move(changed);
    g_system = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
    LoadSettings();
    g_signal_connect(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), "owner-change", G_CALLBACK(+[](GtkClipboard* cb, GdkEvent*, gpointer) {
        if (g_clipSelfSet) { g_clipSelfSet = false; return; }
        if (g_clipOn) gtk_clipboard_request_text(cb, OnClipboardText, nullptr);
    }), nullptr);
    RefreshRadios();
    g_timeout_add_seconds(3, [](gpointer) -> gboolean { RefreshRadios(); return G_SOURCE_CONTINUE; }, nullptr);
    // The weather every 15 minutes; a failed try again after one.
    g_timeout_add_seconds(60, [](gpointer) -> gboolean {
        const double age = Mono() - g_weatherAt;
        if (age > 15 * 60 || (!g_weather.has && age > 60)) RefreshWeather();
        return G_SOURCE_CONTINUE;
    }, nullptr);
    // The focus timer runs out even while its page isn't on screen.
    g_timeout_add(250, [](gpointer) -> gboolean {
        if (g_focus.active && g_focus.running && Mono() >= g_focus.endsAt) {
            g_focus = Focus{false, false, 0, 0, 0, g_focus.pickH, g_focus.pickM};
            g_focusFinished = true;
            Changed();
        }
        return G_SOURCE_CONTINUE;
    }, nullptr);
}

void LoadSettings() {
    namespace s = bb::settings;
    const std::string city = bb::plat::Utf8(s::Get(L"Modules.WeatherCity"));
    const bool fahrenheit = s::GetBool(L"Modules.WeatherFahrenheit");
    const bool on = s::GetBool(L"Modules.Weather");
    const bool refetch = city != g_weatherCity || fahrenheit != g_weather.fahrenheit || (on && !g_weatherOn);
    g_weatherCity = city;
    g_weather.fahrenheit = fahrenheit;
    g_weatherOn = on;
    if (refetch && on) {
        g_weather = Weather{};
        g_weather.fahrenheit = fahrenheit;
        RefreshWeather();
    }
    g_clipOn = s::GetBool(L"Bunbun.ClipboardHistory");
    if (!g_clipOn) g_clips.clear();
}

const Load& SystemLoad() { Sample(); return g_load; }
const Battery& BatteryState() { Sample(); return g_battery; }

const Weather& CurrentWeather() { return g_weather; }

const char* WeatherWords(int code) {
    switch (code) {
        case 0: return "Clear";
        case 1: return "Mostly clear";
        case 2: return "Partly cloudy";
        case 3: return "Cloudy";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drizzle";
        case 56: case 57: return "Freezing drizzle";
        case 61: case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: case 67: return "Freezing rain";
        case 71: case 73: case 75: case 77: return "Snow";
        case 80: case 81: return "Showers";
        case 82: return "Heavy showers";
        case 85: case 86: return "Snow showers";
        case 95: return "Thunderstorm";
        case 96: case 99: return "Thunderstorm, hail";
        default: return "";
    }
}

const Radios& RadioState() { return g_radios; }

void SetWifi(bool on) {
    SetProp("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.NetworkManager",
            "WirelessEnabled", g_variant_new_boolean(on));
    RefreshRadios();
}

void SetBluetooth(bool on) {
    SetProp("org.bluez", "/org/bluez/hci0", "org.bluez.Adapter1", "Powered", g_variant_new_boolean(on));
    RefreshRadios();
}

void SetDnd(bool on) {
    if (GSettings* s = NotificationSettings()) g_settings_set_boolean(s, "show-banners", !on);
    RefreshRadios();
}

const std::vector<std::string>& ClipHistory() { return g_clips; }

void CopyClip(int index) {
    if (index < 0 || index >= static_cast<int>(g_clips.size())) return;
    const std::string text = g_clips[static_cast<size_t>(index)];
    g_clipSelfSet = true;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
    // Copying an old one moves it to the top, as on Windows.
    g_clips.erase(g_clips.begin() + index);
    g_clips.insert(g_clips.begin(), text);
}

void ClearClips() { g_clips.clear(); }

const std::vector<std::wstring>& NoteLines() {
    ReadNote();
    return g_note;
}

bool NoteIsCheckbox(const std::wstring& line, bool* checked) {
    size_t i = 0;
    while (i < line.size() && line[i] == L' ') ++i;
    if (line.size() < i + 3 || line[i] != L'[' || line[i + 2] != L']') return false;
    *checked = line[i + 1] == L'x' || line[i + 1] == L'X';
    return line[i + 1] == L' ' || *checked;
}

void NoteToggle(int index) {
    ReadNote();
    if (index < 0 || index >= static_cast<int>(g_note.size())) return;
    std::wstring& line = g_note[static_cast<size_t>(index)];
    bool checked = false;
    if (!NoteIsCheckbox(line, &checked)) return;
    line[line.find(L'[') + 1] = checked ? L' ' : L'x';
    std::wstring text;
    for (const auto& l : g_note) text += l + L"\n";
    bb::plat::WriteFileAtomic(NotePath(), bb::plat::Utf8(text));
    g_noteCheckedAt = -10;
    g_noteWritten = {};
}

void NoteEdit() {
    if (!bb::plat::FileExists(NotePath())) {
        bb::plat::MakeDirs(bb::plat::ConfigDir());
        bb::plat::WriteFileAtomic(NotePath(), "My note\n[ ] Something to do\n");
    }
    bb::plat::OpenWithDefault(NotePath());
}

const std::vector<bb::todos::Item>& Todos() {
    ReadTodos();
    return g_todos;
}

void TodoToggle(int id) {
    ReadTodos(true);
    for (auto& t : g_todos)
        if (t.id == id) {
            t.done = !t.done;
            if (t.done && !t.repeat.empty()) {  // a repeat never finishes: skip to the next one
                t.done = false;
                t.due = bb::todos::NextOccurrence(t.repeat, t.due, std::max(bb::todos::Now(), t.due));
                t.notified = false;
            }
        }
    bb::todos::Save(TodosPath(), g_todos);
}

std::vector<std::wstring> TakeDueReminders() {
    ReadTodos();
    std::vector<std::wstring> due;
    const double now = bb::todos::Now();
    bool save = false;
    for (auto& t : g_todos) {
        if (t.done || t.notified || t.due <= 0 || t.due > now) continue;
        // Only reminders that came due lately; an old one isn't news after a restart.
        if (now - t.due < 15 * 60) due.push_back(t.text);
        t.notified = true;
        save = true;
    }
    if (save) bb::todos::Save(TodosPath(), g_todos);
    return due;
}

Focus& FocusState() {
    if (g_focus.active && g_focus.running) g_focus.remaining = std::max(0.0, g_focus.endsAt - Mono());
    return g_focus;
}

void FocusStart(int minutes) {
    if (minutes <= 0) return;
    g_focus.active = g_focus.running = true;
    g_focus.total = minutes * 60.0;
    g_focus.remaining = g_focus.total;
    g_focus.endsAt = Mono() + g_focus.total;
}

void FocusPause() {
    if (!g_focus.active) return;
    if (g_focus.running) {
        g_focus.remaining = std::max(0.0, g_focus.endsAt - Mono());
        g_focus.running = false;
    } else {
        g_focus.endsAt = Mono() + g_focus.remaining;
        g_focus.running = true;
    }
}

void FocusAdd(int minutes) {
    if (!g_focus.active) return;
    g_focus.total += minutes * 60.0;
    if (g_focus.running) g_focus.endsAt += minutes * 60.0;
    else g_focus.remaining += minutes * 60.0;
}

void FocusStop() { g_focus = Focus{false, false, 0, 0, 0, g_focus.pickH, g_focus.pickM}; }

bool FocusTakeFinished() {
    const bool f = g_focusFinished;
    g_focusFinished = false;
    return f;
}

}  // namespace bb::lsrc
