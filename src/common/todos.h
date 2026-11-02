// Bunbun — the to-do list file, shared by bunbun.exe (Ask Bun, the To-do tab,
// reminders) and bunbun-settings.exe (the To-dos page).
//
// One line per to-do in todos.tsv next to settings.ini, UTF-8, tab-separated:
//   id  done  due  notified  repeat  text
// `due` is Unix seconds (0 = no reminder). `repeat` is "", "daily", "weekdays" or
// "weekly:1,3" (days of the week, 0 = Sunday). Files written before repeats
// existed have no repeat column and still load. Pure apart from file I/O and
// Windows' time-zone conversion, so the repeat maths is unit-tested.
#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace bb::todos {

struct Item {
    int id = 0;
    std::wstring text;
    double due = 0.0;       // Unix seconds; 0 = no reminder
    bool done = false;
    bool notified = false;  // this occurrence's reminder has fired
    std::wstring repeat;    // "", "daily", "weekdays", "weekly:1,3"
};

// ── Time ─────────────────────────────────────────────────────────────────────
inline double ToUnix(const FILETIME& ft) {
    const ULONGLONG t = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return static_cast<double>(t) / 1e7 - 11644473600.0;
}
inline FILETIME FromUnix(double unix) {
    const ULONGLONG t = static_cast<ULONGLONG>((unix + 11644473600.0) * 1e7);
    return FILETIME{static_cast<DWORD>(t), static_cast<DWORD>(t >> 32)};
}
inline double Now() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ToUnix(ft);
}
// Unix seconds <-> the PC's local wall-clock time.
inline SYSTEMTIME ToLocal(double unix) {
    const FILETIME ft = FromUnix(unix);
    SYSTEMTIME utc{}, local{};
    FileTimeToSystemTime(&ft, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
    return local;
}
inline double FromLocal(const SYSTEMTIME& local) {
    SYSTEMTIME utc{};
    FILETIME ft{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &ft)) return 0.0;
    return ToUnix(ft);
}
// Local midnight-based day arithmetic: the same wall-clock time `days` later
// (so 9:00 stays 9:00 across daylight-saving changes).
inline SYSTEMTIME AddDays(SYSTEMTIME t, int days) {
    FILETIME ft{};
    SYSTEMTIME noon = t;  // step from midday so DST can't push the date off by one
    noon.wHour = 12;
    noon.wMinute = noon.wSecond = noon.wMilliseconds = 0;
    SystemTimeToFileTime(&noon, &ft);
    ULONGLONG v = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    v += static_cast<ULONGLONG>(static_cast<LONGLONG>(days) * 864000000000LL);
    ft = FILETIME{static_cast<DWORD>(v), static_cast<DWORD>(v >> 32)};
    SYSTEMTIME out{};
    FileTimeToSystemTime(&ft, &out);  // also fills wDayOfWeek
    out.wHour = t.wHour;
    out.wMinute = t.wMinute;
    out.wSecond = t.wSecond;
    out.wMilliseconds = 0;
    return out;
}

// ── Repeats ──────────────────────────────────────────────────────────────────
// Which days of the week a repeat fires on, as a bit mask (bit 0 = Sunday); 0 for
// no repeat.
inline int RepeatDays(const std::wstring& repeat) {
    if (repeat == L"daily") return 0x7F;
    if (repeat == L"weekdays") return 0x3E;  // Monday to Friday
    if (repeat.rfind(L"weekly:", 0) == 0) {
        int mask = 0;
        for (wchar_t c : repeat.substr(7))
            if (c >= L'0' && c <= L'6') mask |= 1 << (c - L'0');
        return mask;
    }
    return 0;
}

// The next time a repeating reminder fires strictly after `after`, keeping the
// wall-clock time of `due`. 0 when it doesn't repeat.
inline double NextOccurrence(const std::wstring& repeat, double due, double after) {
    const int mask = RepeatDays(repeat);
    if (!mask || due <= 0.0) return 0.0;
    SYSTEMTIME base = ToLocal(due);
    base = AddDays(base, 0);  // normalise wDayOfWeek
    // Start from today (or the due day, if later) and walk forward a week at most.
    const SYSTEMTIME today = AddDays(ToLocal(after), 0);
    SYSTEMTIME day = today;
    day.wHour = base.wHour;
    day.wMinute = base.wMinute;
    day.wSecond = base.wSecond;
    for (int i = 0; i < 9; ++i) {
        const SYSTEMTIME candidate = AddDays(day, i);
        if (!(mask & (1 << candidate.wDayOfWeek))) continue;
        const double t = FromLocal(candidate);
        if (t > after + 0.5) return t;
    }
    return 0.0;
}

// "Every day", "Weekdays", "Every Mon, Wed".
inline std::wstring DescribeRepeat(const std::wstring& repeat) {
    if (repeat.empty()) return {};
    if (repeat == L"daily") return L"Every day";
    if (repeat == L"weekdays") return L"Weekdays";
    static const wchar_t* names[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    std::wstring out = L"Every ";
    const int mask = RepeatDays(repeat);
    bool first = true;
    for (int d = 0; d < 7; ++d) {
        if (!(mask & (1 << d))) continue;
        if (!first) out += L", ";
        out += names[d];
        first = false;
    }
    return out;
}

// ── File ─────────────────────────────────────────────────────────────────────
inline std::string ToUtf8(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(0, n)), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}
inline std::wstring FromUtf8(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

inline std::vector<Item> Parse(const std::wstring& text) {
    std::vector<Item> items;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::vector<std::wstring> f;
        size_t a = 0;
        for (;;) {
            const size_t b = line.find(L'\t', a);
            if (b == std::wstring::npos) {
                f.push_back(line.substr(a));
                break;
            }
            f.push_back(line.substr(a, b - a));
            a = b + 1;
        }
        if (f.size() != 5 && f.size() != 6) continue;  // 5 = written before repeats
        Item t;
        t.id = _wtoi(f[0].c_str());
        t.done = f[1] == L"1";
        t.due = _wtof(f[2].c_str());
        t.notified = f[3] == L"1";
        if (f.size() == 6) t.repeat = f[4];
        t.text = f.back();
        if (t.id > 0 && !t.text.empty()) items.push_back(std::move(t));
    }
    return items;
}

inline std::wstring Serialize(const std::vector<Item>& items) {
    std::wstring out;
    for (const Item& t : items) {
        std::wstring text = t.text, repeat = t.repeat;
        for (std::wstring* s : {&text, &repeat})
            std::replace_if(s->begin(), s->end(), [](wchar_t c) { return c == L'\t' || c == L'\n' || c == L'\r'; }, L' ');
        wchar_t head[96];
        swprintf_s(head, L"%d\t%d\t%.0f\t%d\t", t.id, t.done ? 1 : 0, t.due, t.notified ? 1 : 0);
        out += head + repeat + L"\t" + text + L"\n";
    }
    return out;
}

inline std::vector<Item> Load(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string bytes;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(f, buf, sizeof(buf), &read, nullptr) && read > 0) bytes.append(buf, read);
    CloseHandle(f);
    return Parse(FromUtf8(bytes));
}

// Written to a temporary file and moved into place, so a crash or a reader in
// the other process never sees half a list.
inline bool Save(const std::wstring& path, const std::vector<Item>& items) {
    const std::string bytes = ToUtf8(Serialize(items));
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(f);
    return ok && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

inline int NextId(const std::vector<Item>& items) {
    int id = 1;
    for (const Item& t : items) id = std::max(id, t.id + 1);
    return id;
}

}  // namespace bb::todos
