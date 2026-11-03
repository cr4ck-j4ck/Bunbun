// Bunbun — Bun's local Windows assistant (see assistant.h).
#include "assistant.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <regex>

namespace bb::assistant {
namespace {

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::wstring Trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r\n?!.");
    if (a == std::wstring::npos) return {};
    const size_t b = s.find_last_not_of(L" \t\r\n?!.");
    return s.substr(a, b - a + 1);
}

bool StartsWith(const std::wstring& s, const std::wstring& p) { return s.rfind(p, 0) == 0; }
bool EndsWith(const std::wstring& s, const std::wstring& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }
bool Has(const std::wstring& s, const wchar_t* w) { return s.find(w) != std::wstring::npos; }

// Strips the first matching prefix; true if one matched.
bool Strip(std::wstring* s, std::initializer_list<const wchar_t*> prefixes) {
    for (const wchar_t* p : prefixes) {
        if (StartsWith(*s, p)) {
            *s = Trim(s->substr(wcslen(p)));
            return true;
        }
    }
    return false;
}

// "today's weather in mumbai", "mumbai weather", "will it rain in pune tomorrow":
// what's left after the weather words and filler is the place ("" = here).
// `days` is 1 for tomorrow.
std::wstring WeatherPlace(const std::wstring& t, int* days) {
    static const std::vector<std::wstring> filler = {
        L"today's", L"todays", L"today", L"tomorrow's", L"tomorrows", L"tomorrow", L"tonight", L"now", L"right",
        L"what's", L"whats", L"what", L"is", L"how's", L"hows", L"how", L"the", L"a", L"current", L"currently",
        L"like", L"weather", L"temperature", L"temp", L"rain", L"raining", L"rainy", L"forecast", L"going", L"gonna",
        L"to", L"will", L"it", L"be", L"outside", L"here", L"in", L"for", L"at", L"of", L"me", L"tell", L"show",
        L"check", L"please", L"bun", L"hey", L"and", L"do", L"does", L"my", L"area", L"city", L"location", L"local",
        L"there", L"any", L"chance", L"get", L"give", L"whether", L"degrees", L"hot", L"cold"};
    *days = (Has(t, L"tomorrow")) ? 1 : 0;
    std::wstring place;
    size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && (t[i] == L' ' || t[i] == L',' || t[i] == L'?')) ++i;
        size_t j = i;
        while (j < t.size() && t[j] != L' ' && t[j] != L',' && t[j] != L'?') ++j;
        if (j > i) {
            const std::wstring word = t.substr(i, j - i);
            if (std::find(filler.begin(), filler.end(), word) == filler.end()) place += (place.empty() ? L"" : L" ") + word;
        }
        i = j;
    }
    return place;
}

std::wstring UrlEncode(const std::wstring& w) {
    std::string utf8;
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    utf8.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), utf8.data(), n, nullptr, nullptr);
    std::wstring out;
    for (unsigned char c : utf8) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<wchar_t>(c);
        else if (c == ' ') out += L'+';
        else { wchar_t b[4]; swprintf(b, 4, L"%%%02X", c); out += b; }
    }
    return out;
}

struct Alias {
    const wchar_t* names;   // '|'-separated spoken names
    const wchar_t* target;  // exe / URI / URL
    const wchar_t* label;
};

// Well-known targets. Anything else is looked up in the Start menu.
constexpr Alias kAliases[] = {
    {L"notepad", L"notepad.exe", L"Notepad"},
    {L"calculator|calc", L"calc.exe", L"Calculator"},
    {L"paint", L"mspaint.exe", L"Paint"},
    {L"file explorer|explorer|files|my files|this pc", L"explorer.exe", L"File Explorer"},
    {L"downloads|my downloads", L"shell:Downloads", L"Downloads"},
    {L"documents|my documents", L"shell:Personal", L"Documents"},
    {L"desktop folder", L"shell:Desktop", L"Desktop"},
    {L"recycle bin|trash", L"shell:RecycleBinFolder", L"Recycle Bin"},
    {L"task manager", L"taskmgr.exe", L"Task Manager"},
    {L"terminal|windows terminal", L"wt.exe", L"Terminal"},
    {L"command prompt|cmd", L"cmd.exe", L"Command Prompt"},
    {L"powershell", L"powershell.exe", L"PowerShell"},
    {L"control panel", L"control.exe", L"Control Panel"},
    {L"snipping tool|snip", L"ms-screenclip:", L"Snipping Tool"},
    {L"camera", L"microsoft.windows.camera:", L"Camera"},
    {L"photos", L"ms-photos:", L"Photos"},
    {L"store|microsoft store", L"ms-windows-store:", L"Microsoft Store"},
    {L"mail|outlook mail", L"outlookmail:", L"Mail"},
    {L"clock|alarms", L"ms-clock:", L"Clock"},
    {L"spotify", L"spotify:", L"Spotify"},
    {L"edge|microsoft edge", L"microsoft-edge:", L"Edge"},
    {L"settings|windows settings", L"ms-settings:", L"Settings"},
    {L"wifi settings|wi-fi settings|wifi|wi-fi|network settings|internet settings", L"ms-settings:network-wifi", L"Wi-Fi settings"},
    {L"bluetooth settings|bluetooth|devices", L"ms-settings:bluetooth", L"Bluetooth settings"},
    {L"display settings|display|screen settings|brightness settings", L"ms-settings:display", L"Display settings"},
    {L"sound settings|sound|audio settings", L"ms-settings:sound", L"Sound settings"},
    {L"battery settings|power settings|battery saver", L"ms-settings:batterysaver", L"Battery settings"},
    {L"notification settings|notifications", L"ms-settings:notifications", L"Notification settings"},
    {L"focus|focus assist|do not disturb|dnd", L"ms-settings:quiethours", L"Focus settings"},
    {L"personalization|wallpaper|background|themes", L"ms-settings:personalization-background", L"Personalization"},
    {L"apps|installed apps|uninstall", L"ms-settings:appsfeatures", L"Installed apps"},
    {L"windows update|updates|update", L"ms-settings:windowsupdate", L"Windows Update"},
    {L"storage|disk space", L"ms-settings:storagesense", L"Storage settings"},
    {L"night light", L"ms-settings:nightlight", L"Night light"},
    {L"mouse settings|mouse", L"ms-settings:mousetouchpad", L"Mouse settings"},
    {L"keyboard settings|keyboard|language", L"ms-settings:regionlanguage", L"Language settings"},
    {L"youtube", L"https://www.youtube.com/", L"YouTube"},
    {L"github", L"https://github.com/", L"GitHub"},
    {L"gmail", L"https://mail.google.com/", L"Gmail"},
    {L"google", L"https://www.google.com/", L"Google"},
    {L"maps|google maps", L"https://www.google.com/maps", L"Maps"},
};

const Alias* FindAlias(const std::wstring& name) {
    for (const Alias& a : kAliases) {
        std::wstring names = a.names;
        size_t start = 0;
        while (start <= names.size()) {
            const size_t bar = names.find(L'|', start);
            if (names.compare(start, bar == std::wstring::npos ? std::wstring::npos : bar - start, name) == 0 &&
                (bar == std::wstring::npos ? names.size() - start : bar - start) == name.size())
                return &a;
            if (bar == std::wstring::npos) break;
            start = bar + 1;
        }
    }
    return nullptr;
}

// "5", "five", "a" -> number; -1 when absent.
int ParseCount(const std::wstring& s) {
    static const wchar_t* kWords[] = {L"zero", L"one", L"two", L"three", L"four", L"five", L"six", L"seven",
                                      L"eight", L"nine", L"ten"};
    for (int i = 0; i <= 10; ++i)
        if (s == kWords[i]) return i;
    if (s == L"a" || s == L"an") return 1;
    if (!s.empty() && std::all_of(s.begin(), s.end(), iswdigit)) return _wtoi(s.c_str());
    return -1;
}

// Minutes in "5 min", "10 minutes", "1 hour", "90 seconds", "pomodoro".
int ParseMinutes(const std::wstring& text) {
    if (Has(text, L"pomodoro")) return 25;
    std::vector<std::wstring> words;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !iswalnum(text[i])) ++i;
        size_t j = i;
        while (j < text.size() && iswalnum(text[j])) ++j;
        if (j > i) words.push_back(text.substr(i, j - i));
        i = j;
    }
    for (size_t k = 0; k < words.size(); ++k) {
        int n = ParseCount(words[k]);
        std::wstring unit = k + 1 < words.size() ? words[k + 1] : L"";
        // "5min" / "10m"
        if (n < 0) {
            size_t d = 0;
            while (d < words[k].size() && iswdigit(words[k][d])) ++d;
            if (d > 0 && d < words[k].size()) { n = _wtoi(words[k].substr(0, d).c_str()); unit = words[k].substr(d); }
        }
        if (n < 0) continue;
        // "a"/"an" only count before a unit ("an hour"), not in "set a timer".
        const bool article = words[k] == L"a" || words[k] == L"an";
        const bool isUnit = StartsWith(unit, L"min") || StartsWith(unit, L"h") || StartsWith(unit, L"sec") ||
                            unit == L"m" || unit == L"s";
        if (article && !isUnit) continue;
        if (StartsWith(unit, L"h")) return n * 60;
        if (StartsWith(unit, L"s")) return std::max(1, (n + 59) / 60);
        return n;  // minutes by default
    }
    return -1;
}

// ── Arithmetic ──────────────────────────────────────────────────────────────
struct Calc {
    const std::wstring& s;
    size_t i = 0;
    bool ok = true;
    void Ws() { while (i < s.size() && iswspace(s[i])) ++i; }
    double Expr() {
        double v = Term();
        for (Ws(); ok && i < s.size() && (s[i] == L'+' || s[i] == L'-'); Ws()) {
            const wchar_t op = s[i++];
            const double r = Term();
            v = op == L'+' ? v + r : v - r;
        }
        return v;
    }
    double Term() {
        double v = Power();
        for (Ws(); ok && i < s.size() && (s[i] == L'*' || s[i] == L'/' || s[i] == L'%' || s[i] == L'x' || s[i] == L'×'); Ws()) {
            const wchar_t op = s[i++];
            const double r = Power();
            if (op == L'/') { if (r == 0) ok = false; else v /= r; }
            else if (op == L'%') { if (r == 0) ok = false; else v = std::fmod(v, r); }
            else v *= r;
        }
        return v;
    }
    double Power() {
        const double b = Unary();
        Ws();
        if (ok && i < s.size() && s[i] == L'^') { ++i; return std::pow(b, Power()); }
        return b;
    }
    double Unary() {
        Ws();
        if (i < s.size() && s[i] == L'-') { ++i; return -Unary(); }
        if (i < s.size() && s[i] == L'+') { ++i; return Unary(); }
        return Atom();
    }
    double Atom() {
        Ws();
        if (i < s.size() && s[i] == L'(') {
            ++i;
            const double v = Expr();
            Ws();
            if (i < s.size() && s[i] == L')') ++i; else ok = false;
            return v;
        }
        if (s.compare(i, 5, L"sqrt(") == 0) {
            i += 5;
            const double v = Expr();
            Ws();
            if (i < s.size() && s[i] == L')') ++i; else ok = false;
            if (v < 0) ok = false;
            return v >= 0 ? std::sqrt(v) : 0.0;
        }
        if (s.compare(i, 4, L"abs(") == 0) {
            i += 4;
            const double v = Expr();
            Ws();
            if (i < s.size() && s[i] == L')') ++i; else ok = false;
            return std::fabs(v);
        }
        if (s.compare(i, 2, L"pi") == 0 && (i + 2 == s.size() || !iswalnum(s[i + 2]))) {
            i += 2;
            return 3.14159265358979323846;
        }
        if (i < s.size() && s[i] == L'π') {
            i += 1;
            return 3.14159265358979323846;
        }
        const size_t st = i;
        while (i < s.size() && (iswdigit(s[i]) || s[i] == L'.' || s[i] == L',')) ++i;
        if (i == st) { ok = false; return 0; }
        std::wstring num = s.substr(st, i - st);
        num.erase(std::remove(num.begin(), num.end(), L','), num.end());
        return wcstod(num.c_str(), nullptr);
    }
};

std::wstring FormatNumber(double v) {
    wchar_t b[64];
    if (std::fabs(v - std::round(v)) < 1e-9 && std::fabs(v) < 1e15) swprintf(b, 64, L"%.0f", v);
    else swprintf(b, 64, L"%.10g", v);
    return b;
}

std::wstring SearchUrl(const std::wstring& engine, const std::wstring& q) {
    if (engine == L"bing") return L"https://www.bing.com/search?q=" + UrlEncode(q);
    if (engine == L"duckduckgo") return L"https://duckduckgo.com/?q=" + UrlEncode(q);
    return L"https://www.google.com/search?q=" + UrlEncode(q);
}

}  // namespace

bool Evaluate(const std::wstring& expr, double* out) {
    bool digit = false;
    for (wchar_t c : expr) {
        if (iswdigit(c)) digit = true;
        else if (!(iswspace(c) || wcschr(L"+-*/%^().,x×π", c) || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z'))) return false;
    }
    if (!digit && expr.find(L"pi") == std::wstring::npos && expr.find(L"π") == std::wstring::npos) return false;
    Calc c{expr};
    const double v = c.Expr();
    c.Ws();
    if (!c.ok || c.i != expr.size() || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

std::wstring HelpText() {
    return L"Try: open notepad · wifi settings · play / pause / next · volume 40 · mute · "
           L"timer 10 min · search cute bunnies · battery · weather · what's playing · "
           L"12*7 · lock · screenshot · dnd on / off · add buy milk · remind me at 3pm to call Sam · "
           L"my list · done buy milk · remind me every day at 9 to stand up · focus 25 · wifi off · "
           L"brightness 60 · undo · define serendipity";
}

// Takes a due time out of a to-do / reminder: "in 20 min", "at 3pm", "at 9:30",
// "tomorrow (at 9)", "tonight". Whatever is left is the to-do's text.
// Takes a repeat out of a reminder: "every day", "daily", "every weekday",
// "every morning" (9 am), "every Mon and Wed", "on Fridays", "every week".
void TakeRepeat(std::wstring* text, Intent* in) {
    static const std::wregex daysRe(
        LR"((^|\s)(?:every|each|on)\s+((?:(?:mon|tue|tues|wed|wednes|thu|thur|thurs|fri|sat|satur|sun)(?:day)?s?(?:\s*(?:,|and|&)\s*)?)+)(?=\s|$))",
        std::regex::icase);
    static const std::wregex wordRe(LR"((^|\s)(?:(?:every|each)\s+(day|weekday|working day|morning|evening|night|week)|(daily)|on (weekdays))(?=\s|$))",
                                    std::regex::icase);
    std::wsmatch m;
    if (std::regex_search(*text, m, daysRe)) {
        static const wchar_t* keys[] = {L"sun", L"mon", L"tue", L"wed", L"thu", L"fri", L"sat"};
        const std::wstring days = Lower(m[2].str());
        std::wstring list;
        for (int d = 0; d < 7; ++d) {
            if (days.find(keys[d]) == std::wstring::npos) continue;
            if (!list.empty()) list += L',';
            list += std::to_wstring(d);
        }
        if (!list.empty()) {
            in->repeat = L"weekly:" + list;
            text->erase(m.position(0), m.length(0));
            return;
        }
    }
    if (std::regex_search(*text, m, wordRe)) {
        const std::wstring w = Lower(m[2].matched ? m[2].str() : m[3].matched ? m[3].str() : m[4].str());
        if (w == L"weekday" || w == L"weekdays" || w == L"working day") in->repeat = L"weekdays";
        else if (w == L"week") in->repeat = L"weekly";
        else in->repeat = L"daily";
        if (w == L"morning") in->dueHour = 9;
        if (w == L"evening") in->dueHour = 18;
        if (w == L"night") in->dueHour = 21;
        text->erase(m.position(0), m.length(0));
    }
}

void TakeDue(std::wstring* text, Intent* in) {
    TakeRepeat(text, in);
    static const std::wregex inRe(LR"((^|\s)in (an?|\d+) ?(minutes?|mins?|m|hours?|hrs?|h)(?=\s|$))", std::regex::icase);
    static const std::wregex atRe(LR"((^|\s)at (\d{1,2})(?:[:.](\d{2}))? ?(am|pm|a\.m\.|p\.m\.)?(?=\s|$))", std::regex::icase);
    std::wsmatch m;
    if (std::regex_search(*text, m, inRe)) {
        const std::wstring n = m[2].str(), unit = m[3].str();
        const int count = iswdigit(n[0]) ? _wtoi(n.c_str()) : 1;
        in->dueInMinutes = unit[0] == L'h' ? count * 60 : count;
        text->erase(m.position(0), m.length(0));
    }
    if (std::regex_search(*text, m, atRe)) {
        int h = _wtoi(m[2].str().c_str());
        const int mi = m[3].matched ? _wtoi(m[3].str().c_str()) : 0;
        const std::wstring ap = Lower(m[4].str());
        if (!ap.empty() && ap[0] == L'p' && h < 12) h += 12;
        if (!ap.empty() && ap[0] == L'a' && h == 12) h = 0;
        if (h <= 23 && mi <= 59) {
            in->dueHour = h;
            in->dueMinute = mi;
            text->erase(m.position(0), m.length(0));
        }
    }
    for (const wchar_t* word : {L"tomorrow", L"tonight"}) {
        const size_t at = text->find(word);
        if (at == std::wstring::npos) continue;
        if (word[2] == L'm') in->dueDays = 1;
        else if (in->dueHour < 0) in->dueHour = 20;
        text->erase(at, wcslen(word));
        if (in->dueDays == 1 && in->dueHour < 0 && in->dueInMinutes < 0) in->dueHour = 9;  // "tomorrow" alone: 9 am
    }
    std::wstring t = Trim(*text);
    while (StartsWith(t, L"to ")) t = Trim(t.substr(3));
    for (const wchar_t* tail : {L" to my list", L" to my to do list", L" to my todo list", L" to the list", L" to my todos"})
        if (t.size() > wcslen(tail) && t.compare(t.size() - wcslen(tail), std::wstring::npos, tail) == 0) t.erase(t.size() - wcslen(tail));
    *text = Trim(t);
}

int AppMatchScore(const std::wstring& appName, const std::wstring& typed) {
    const std::wstring name = Lower(appName), q = Lower(Trim(typed));
    if (q.size() < 2 || name.empty()) return 0;
    if (name == q) return 100;
    if (StartsWith(name, q)) return 90 - std::min(20, static_cast<int>(name.size() - q.size()) / 4);
    // Words and their initials: "visual studio code" -> "vsc".
    std::wstring initials;
    bool wordPrefix = false;
    for (size_t i = 0; i < name.size(); ++i) {
        const bool start = i == 0 || !iswalnum(name[i - 1]);
        if (!start || !iswalnum(name[i])) continue;
        initials += name[i];
        if (name.compare(i, q.size(), q) == 0) wordPrefix = true;
    }
    if (wordPrefix) return 75;
    if (initials.size() >= 2 && StartsWith(initials, q)) return 70 - static_cast<int>(initials.size() - q.size());
    if (name.find(q) != std::wstring::npos) return 50 - std::min(20, static_cast<int>(name.size()) / 8);
    // All the typed letters, in order ("spfy" -> spotify), for 3+ letters.
    if (q.size() >= 3) {
        size_t at = 0, gaps = 0;
        for (wchar_t c : q) {
            const size_t found = name.find(c, at);
            if (found == std::wstring::npos) return 0;
            gaps += found - at;
            at = found + 1;
        }
        return std::max(1, 30 - static_cast<int>(gaps));
    }
    return 0;
}

namespace {

struct AppEntry {
    std::wstring name;  // the shortcut's name, "Visual Studio Code"
    std::wstring path;  // the .lnk
};
std::mutex g_appsMutex;
std::vector<AppEntry> g_apps;
ULONGLONG g_appsAt = 0;

std::vector<AppEntry> ScanStartMenu() {
    std::vector<AppEntry> apps;
    for (REFKNOWNFOLDERID id : {FOLDERID_CommonPrograms, FOLDERID_Programs}) {
        PWSTR root = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &root))) continue;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const auto& p = it->path();
            if (_wcsicmp(p.extension().c_str(), L".lnk") != 0) continue;
            const std::wstring stem = p.stem().wstring();
            const std::wstring lower = Lower(stem);
            if (Has(lower, L"uninstall") || Has(lower, L"readme") || Has(lower, L"help") || Has(lower, L"release notes")) continue;
            apps.push_back({stem, p.wstring()});
        }
        CoTaskMemFree(root);
    }
    return apps;
}

}  // namespace

void WarmStartMenuIndex() {
    std::vector<AppEntry> apps = ScanStartMenu();
    std::lock_guard lock(g_appsMutex);
    g_apps = std::move(apps);
    g_appsAt = GetTickCount64();
}

std::wstring FindStartMenuApp(const std::wstring& rawName) {
    if (Trim(rawName).size() < 2) return {};
    {
        std::lock_guard lock(g_appsMutex);
        if (g_apps.empty() || GetTickCount64() - g_appsAt > 10 * 60 * 1000) {
            // First use, or stale (apps get installed): rescan, a few hundred ms.
            g_apps = ScanStartMenu();
            g_appsAt = GetTickCount64();
        }
        std::wstring best;
        int bestScore = 29;  // below this it's a guess, not a match
        for (const AppEntry& app : g_apps) {
            const int score = AppMatchScore(app.name, rawName);
            if (score > bestScore) {
                bestScore = score;
                best = app.path;
            }
        }
        return best;
    }
}

Intent Parse(const std::wstring& raw, const std::wstring& searchEngine) {
    Intent in;
    std::wstring t = Trim(Lower(raw));
    // Talk the way people type: "hey bun, please turn on dnd!" → "turn on dnd".
    while (!t.empty() && (t.back() == L'.' || t.back() == L'!' || t.back() == L'?')) t.pop_back();
    t = Trim(t);
    Strip(&t, {L"hey bun, ", L"hey bun ", L"ok bun ", L"bun, ", L"bun "});
    Strip(&t, {L"please ", L"can you ", L"could you ", L"would you "});
    Strip(&t, {L"please "});
    for (const wchar_t* tail : {L" please", L" for me", L" now"})
        if (t.size() > wcslen(tail) && t.compare(t.size() - wcslen(tail), std::wstring::npos, tail) == 0)
            t.erase(t.size() - wcslen(tail));
    if (t.empty()) { in.reply = HelpText(); return in; }

    if (t == L"help" || t == L"what can you do" || t == L"commands") {
        in.action = Action::Reply;
        in.reply = HelpText();
        return in;
    }

    // Arithmetic: "12*7", "what is 2^10", "calc (3+4)/2", "15% of 200", "how much is 50+20"
    {
        std::wstring e = t;
        Strip(&e, {L"what is ", L"what's ", L"whats ", L"how much is ", L"calc ", L"calculate ", L"compute ", L"eval "});
        // Percentage of: "15% of 200", "20 percent of 150"
        size_t pctPos = e.find(L"% of ");
        size_t pctLen = 5;
        if (pctPos == std::wstring::npos) {
            pctPos = e.find(L" percent of ");
            pctLen = 12;
        }
        if (pctPos != std::wstring::npos) {
            std::wstring pStr = e.substr(0, pctPos);
            std::wstring totalStr = e.substr(pctPos + pctLen);
            double pVal = 0.0, totalVal = 0.0;
            if (Evaluate(pStr, &pVal) && Evaluate(totalStr, &totalVal)) {
                const double ans = (pVal / 100.0) * totalVal;
                in.action = Action::Reply;
                in.reply = e + L" = " + FormatNumber(ans);
                return in;
            }
        }
        double v;
        if (Evaluate(e, &v)) {
            in.action = Action::Reply;
            in.reply = e + L" = " + FormatNumber(v);
            return in;
        }
    }

    // Lookups: "wiki <topic>", "lookup <topic>", "define <word>"
    {
        std::wstring q = t;
        if (Strip(&q, {L"wiki ", L"wikipedia ", L"lookup ", L"look up ", L"define ", L"dict ", L"meaning of ", L"definition of "}) && !q.empty()) {
            in.action = Action::Lookup;
            in.target = q;
            in.label = q;
            return in;
        }
    }

    // Media
    if (t == L"play" || t == L"pause" || t == L"resume" || t == L"play pause" || t == L"stop music" ||
        t == L"pause music" || t == L"play music" || t == L"toggle music") { in.action = Action::MediaToggle; return in; }
    if (t == L"next" || t == L"skip" || t == L"next song" || t == L"next track" || t == L"skip song") { in.action = Action::MediaNext; return in; }
    if (t == L"previous" || t == L"prev" || t == L"back" || t == L"previous song" || t == L"previous track" || t == L"last song") {
        in.action = Action::MediaPrevious;
        return in;
    }

    // Volume
    if (t == L"mute" || t == L"mute volume" || t == L"silence") { in.action = Action::Mute; return in; }
    if (t == L"unmute" || t == L"unmute volume") { in.action = Action::Unmute; return in; }
    if (Has(t, L"volume") || Has(t, L"louder") || Has(t, L"quieter")) {
        std::wstring digits;
        for (wchar_t c : t) if (iswdigit(c)) digits += c;
        if (!digits.empty()) { in.action = Action::VolumeSet; in.number = std::clamp(_wtoi(digits.c_str()), 0, 100); return in; }
        if (Has(t, L"up") || Has(t, L"louder") || Has(t, L"increase") || Has(t, L"raise")) { in.action = Action::VolumeUp; return in; }
        if (Has(t, L"down") || Has(t, L"quieter") || Has(t, L"decrease") || Has(t, L"lower")) { in.action = Action::VolumeDown; return in; }
        if (Has(t, L"max")) { in.action = Action::VolumeSet; in.number = 100; return in; }
    }

    // Mascot do-not-disturb: Bunbun's own, separate from Windows' Do Not Disturb.
    // Any phrasing that mentions dnd / do not disturb works ("turn on dnd",
    // "disable do not disturb", "dnd mode off"), except asking for the Windows
    // Focus settings page ("open dnd settings").
    if ((Has(t, L"dnd") || Has(t, L"do not disturb") || Has(t, L"snack")) && !Has(t, L"settings") &&
        !StartsWith(t, L"open ") && !StartsWith(t, L"show ")) {
        const bool off = Has(t, L"off") || Has(t, L"disable") || Has(t, L"stop") || Has(t, L"end ") || EndsWith(t, L" end") ||
                         Has(t, L"exit") || Has(t, L"over") || Has(t, L"no more");
        in.action = off ? Action::MascotDndOff : Action::MascotDndOn;
        return in;
    }
    if (t == L"quiet mascot" || t == L"quiet bun") { in.action = Action::MascotDndOn; return in; }
    if (t == L"wake up" || t == L"wake up bun") { in.action = Action::MascotDndOff; return in; }

    // System
    if (t == L"lock" || t == L"lock pc" || t == L"lock screen" || t == L"lock computer") { in.action = Action::Lock; return in; }
    if (Has(t, L"screenshot") || t == L"snip" || t == L"screen capture") { in.action = Action::Screenshot; return in; }

    // Undo: put back what the last command changed (the core remembers how).
    if (t == L"undo" || t == L"undo that" || t == L"undo last" || t == L"undo the last one") {
        in.action = Action::Undo;
        return in;
    }

    // Wi-Fi and Bluetooth on / off ("turn wifi off", "disable bluetooth"). Alone,
    // or with "settings", they still open their Settings page (aliases below).
    {
        const bool wifi = Has(t, L"wifi") || Has(t, L"wi-fi") || Has(t, L"wi fi");
        const bool bt = Has(t, L"bluetooth");
        if ((wifi || bt) && !Has(t, L"settings") && !StartsWith(t, L"open ")) {
            const bool off = EndsWith(t, L" off") || Has(t, L" off ") || StartsWith(t, L"turn off") || Has(t, L"disable") ||
                             StartsWith(t, L"switch off") || StartsWith(t, L"stop ");
            const bool on = !off && (EndsWith(t, L" on") || Has(t, L" on ") || StartsWith(t, L"turn on") || Has(t, L"enable") ||
                                     StartsWith(t, L"switch on") || StartsWith(t, L"start "));
            if (off || on) {
                in.action = wifi ? (off ? Action::WifiOff : Action::WifiOn) : (off ? Action::BluetoothOff : Action::BluetoothOn);
                return in;
            }
        }
    }

    // Screen brightness: "brightness 60", "brighter", "dim the screen".
    if ((Has(t, L"brightness") || Has(t, L"brighter") || Has(t, L"dimmer") || StartsWith(t, L"dim ")) && !Has(t, L"settings")) {
        std::wstring digits;
        for (wchar_t c : t) if (iswdigit(c)) digits += c;
        if (!digits.empty()) { in.action = Action::BrightnessSet; in.number = std::clamp(_wtoi(digits.c_str()), 0, 100); return in; }
        if (Has(t, L"brighter") || Has(t, L"up") || Has(t, L"increase") || Has(t, L"raise")) { in.action = Action::BrightnessUp; return in; }
        if (Has(t, L"dim") || Has(t, L"down") || Has(t, L"decrease") || Has(t, L"lower")) { in.action = Action::BrightnessDown; return in; }
        if (Has(t, L"max") || Has(t, L"full")) { in.action = Action::BrightnessSet; in.number = 100; return in; }
    }

    // Focus mode: "focus", "focus 50", "start focus for 45 minutes", "deep work".
    // "open focus settings" still opens Windows' Focus page (an alias below).
    if (!Has(t, L"settings") && !StartsWith(t, L"open ")) {
        const bool stop = t == L"stop focus" || t == L"end focus" || t == L"focus off" || t == L"stop focusing" ||
                          t == L"stop focus mode" || t == L"end focus mode" || t == L"focus mode off";
        if (stop) {
            in.action = Action::FocusStop;
            return in;
        }
        for (const wchar_t* p : {L"start focus mode", L"start deep work", L"start focus", L"focus mode", L"deep work", L"focus"}) {
            const size_t len = wcslen(p);
            if (!StartsWith(t, p) || (t.size() > len && !iswspace(t[len]))) continue;  // "focus 50", not "focused"
            const int m = ParseMinutes(t.substr(len));
            in.action = Action::FocusStart;
            in.number = m > 0 ? std::min(m, 8 * 60) : 25;
            return in;
        }
    }

    // To-dos and reminders. "remind me in 10 min" with nothing to be reminded of
    // stays a timer (below).
    {
        std::wstring rest = t;
        const bool remind = Strip(&rest, {L"remind me to ", L"remind me ", L"reminder to ", L"reminder ", L"set a reminder to ", L"set a reminder "});
        const bool add = !remind && Strip(&rest, {L"add todo ", L"add to do ", L"add a todo ", L"todo ", L"to do ", L"add ", L"remember to "});
        if (remind || add) {
            Intent r;
            r.action = Action::TodoAdd;
            TakeDue(&rest, &r);
            r.label = rest;
            // Keep the user's own capitals ("call Sam"): the parser works in lower case.
            if (const size_t at = Lower(raw).find(rest); !rest.empty() && at != std::wstring::npos) r.label = raw.substr(at, rest.size());
            const bool hasDue = r.dueInMinutes >= 0 || r.dueHour >= 0 || r.dueDays > 0 || !r.repeat.empty();
            if (!r.label.empty() && (add || hasDue)) {
                r.label[0] = towupper(r.label[0]);
                return r;
            }
            if (remind && r.label.empty() && !hasDue) {
                in.reply = L"Remind you of what? Try “remind me at 3pm to call Sam”.";
                return in;
            }
            // "remind me in 10 min": no text, so it's a plain timer.
        }
    }
    if (t == L"todos" || t == L"todo" || t == L"to do" || t == L"to do list" || t == L"todo list" || t == L"my list" ||
        t == L"my todos" || t == L"my to do list" || t == L"my todo list" || t == L"list" || t == L"reminders" ||
        t == L"my reminders" || Has(t, L"on my list") || t == L"show my list" || t == L"show todos") {
        in.action = Action::TodoList;
        return in;
    }
    if (t == L"clear done" || t == L"clear completed" || t == L"clear finished" || t == L"clear done todos") {
        in.action = Action::TodoClearDone;
        return in;
    }
    {
        std::wstring rest = t;
        if (Strip(&rest, {L"mark ", L"done ", L"complete ", L"completed ", L"finished ", L"finish ", L"tick ", L"check off "})) {
            for (const wchar_t* tail : {L" as done", L" done", L" off"})
                if (rest.size() > wcslen(tail) && rest.compare(rest.size() - wcslen(tail), std::wstring::npos, tail) == 0) rest.erase(rest.size() - wcslen(tail));
            rest = Trim(rest);
            if (!rest.empty()) {
                in.action = Action::TodoDone;
                in.label = rest;
                return in;
            }
        }
    }

    // Timers
    if (Has(t, L"timer") || Has(t, L"pomodoro") || StartsWith(t, L"remind me in ")) {
        if (Has(t, L"stop") || Has(t, L"cancel")) { in.action = Action::TimerStop; return in; }
        const int m = ParseMinutes(t);
        if (m > 0 && m <= 24 * 60) { in.action = Action::TimerStart; in.number = m; return in; }
        in.reply = L"How long? Try “timer 10 min”.";
        return in;
    }

    // Quick facts
    if (t == L"time" || Has(t, L"what time")) { in.action = Action::Info; in.info = Info::Time; return in; }
    if (t == L"date" || t == L"today" || Has(t, L"what day") || Has(t, L"what's the date") || Has(t, L"what is the date")) {
        in.action = Action::Info; in.info = Info::Date; return in;
    }
    if (Has(t, L"battery") && !Has(t, L"settings")) { in.action = Action::Info; in.info = Info::Battery; return in; }
    if (Has(t, L"weather") || Has(t, L"temperature") || Has(t, L"rain") || Has(t, L"forecast")) {
        in.action = Action::Info;
        in.info = Info::Weather;
        in.target = WeatherPlace(t, &in.number);  // "" = the island's own weather
        return in;
    }
    if (Has(t, L"playing") || Has(t, L"this song") || Has(t, L"which song") || Has(t, L"what song")) {
        in.action = Action::Info; in.info = Info::NowPlaying; return in;
    }
    if (Has(t, L"cpu") || Has(t, L"memory") || Has(t, L"ram") || Has(t, L"system") || Has(t, L"how is my pc")) {
        in.action = Action::Info; in.info = Info::System; return in;
    }

    // Web search
    {
        std::wstring q = t;
        if (Strip(&q, {L"search the web for ", L"search for ", L"search ", L"google ", L"bing ", L"look up ", L"find "}) && !q.empty()) {
            in.action = Action::WebSearch;
            in.target = SearchUrl(searchEngine, q);
            in.label = q;
            return in;
        }
    }

    // Open / launch
    {
        std::wstring name = t;
        const bool asked = Strip(&name, {L"open up ", L"open ", L"launch ", L"start ", L"run ", L"show ", L"go to ", L"fire up "});
        Strip(&name, {L"the ", L"my "});
        for (const wchar_t* tail : {L" app", L" application", L" program"})
            if (name.size() > wcslen(tail) && name.compare(name.size() - wcslen(tail), std::wstring::npos, tail) == 0)
                name.erase(name.size() - wcslen(tail));
        if (const Alias* a = FindAlias(name)) {
            in.action = Action::Open;
            in.target = a->target;
            in.label = a->label;
            return in;
        }
        if (asked && !name.empty()) {
            in.action = Action::Open;
            in.label = name;  // resolved against the Start menu when executed
            return in;
        }
    }

    in.reply = L"I didn’t catch that. " + HelpText();
    in.unknown = true;
    return in;
}

}  // namespace bb::assistant
