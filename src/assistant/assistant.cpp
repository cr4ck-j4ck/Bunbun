// Bunbun — Bun's local Windows assistant (see assistant.h).
#include "assistant.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <filesystem>

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
        else if (!(iswspace(c) || wcschr(L"+-*/%^().,x×", c))) return false;
    }
    if (!digit) return false;
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
           L"12*7 · lock · screenshot · dnd on / off · define serendipity";
}

std::wstring FindStartMenuApp(const std::wstring& rawName) {
    const std::wstring name = Lower(rawName);
    if (name.size() < 2) return {};
    std::wstring best;
    int bestScore = 0;
    for (REFKNOWNFOLDERID id : {FOLDERID_CommonPrograms, FOLDERID_Programs}) {
        PWSTR root = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &root))) continue;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            const auto& p = it->path();
            if (_wcsicmp(p.extension().c_str(), L".lnk") != 0) continue;
            const std::wstring stem = Lower(p.stem().wstring());
            if (Has(stem, L"uninstall") || Has(stem, L"readme") || Has(stem, L"help")) continue;
            int score = 0;
            if (stem == name) score = 100;
            else if (StartsWith(stem, name)) score = 80 - static_cast<int>(stem.size() - name.size()) / 4;
            else if (Has(stem, name.c_str())) score = 50 - static_cast<int>(stem.size()) / 8;
            if (score > bestScore) { bestScore = score; best = p.wstring(); }
        }
        CoTaskMemFree(root);
    }
    return best;
}

Intent Parse(const std::wstring& raw, const std::wstring& searchEngine) {
    Intent in;
    std::wstring t = Trim(Lower(raw));
    Strip(&t, {L"hey bun ", L"bun, ", L"bun ", L"please ", L"can you ", L"could you "});
    if (t.empty()) { in.reply = HelpText(); return in; }

    if (t == L"help" || t == L"what can you do" || t == L"commands") {
        in.action = Action::Reply;
        in.reply = HelpText();
        return in;
    }

    // Arithmetic: "12*7", "what is 2^10", "calc (3+4)/2"
    {
        std::wstring e = t;
        Strip(&e, {L"what is ", L"what's ", L"whats ", L"calc ", L"calculate ", L"compute "});
        double v;
        if (Evaluate(e, &v)) {
            in.action = Action::Reply;
            in.reply = e + L" = " + FormatNumber(v);
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
    if (t == L"dnd" || t == L"dnd on" || t == L"do not disturb" || t == L"do not disturb on" ||
        t == L"snack time" || t == L"snack mode" || t == L"bun dnd" || t == L"mascot dnd on" || t == L"quiet mascot") {
        in.action = Action::MascotDndOn;
        return in;
    }
    if (t == L"dnd off" || t == L"do not disturb off" || t == L"bun dnd off" || t == L"mascot dnd off" ||
        t == L"wake up" || t == L"wake up bun" || t == L"snack over") {
        in.action = Action::MascotDndOff;
        return in;
    }

    // System
    if (t == L"lock" || t == L"lock pc" || t == L"lock screen" || t == L"lock computer") { in.action = Action::Lock; return in; }
    if (Has(t, L"screenshot") || t == L"snip" || t == L"screen capture") { in.action = Action::Screenshot; return in; }

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
    if (Has(t, L"weather") || Has(t, L"temperature") || Has(t, L"rain")) { in.action = Action::Info; in.info = Info::Weather; return in; }
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
        const bool asked = Strip(&name, {L"open ", L"launch ", L"start ", L"run ", L"show ", L"go to "});
        Strip(&name, {L"the ", L"my "});
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
    return in;
}

}  // namespace bb::assistant
