// Bunbun — settings on Linux (see settings.h).
#include "settings.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <ctime>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

#include "island_api.h"
#include "platform.h"

namespace bb::settings {
namespace {

constexpr Default kDefaults[] = {
#include "../../island/default_settings.inc"
};

std::mutex g_mutex;
bool g_loaded = false;
std::map<std::wstring, std::wstring> g_values;  // lower-cased "section.key" → value

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::wstring Trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r");
    const size_t b = s.find_last_not_of(L" \t\r");
    return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
}

// Reads UTF-8, or UTF-16LE with a BOM (a file copied over from Windows).
std::wstring ReadText() {
    std::string raw;
    if (!plat::ReadFile(Path(), &raw)) return {};
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF && static_cast<unsigned char>(raw[1]) == 0xFE) {
        std::wstring w;
        for (size_t i = 2; i + 1 < raw.size(); i += 2)
            w += static_cast<wchar_t>(static_cast<unsigned char>(raw[i]) | (static_cast<unsigned char>(raw[i + 1]) << 8));
        return w;
    }
    if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);
    return plat::Wide(raw);
}

std::vector<std::wstring> Lines(const std::wstring& text) {
    std::vector<std::wstring> out;
    std::wistringstream in(text);
    for (std::wstring line; std::getline(in, line);) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

void LoadLocked() {
    if (g_loaded) return;
    g_loaded = true;
    g_values.clear();
    std::wstring section;
    for (const std::wstring& raw : Lines(ReadText())) {
        const std::wstring line = Trim(raw);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') {
            section = Trim(line.substr(1, line.find(L']') - 1));
            continue;
        }
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring v = Trim(line.substr(eq + 1));
        if (v.size() >= 2 && (v.front() == L'"' || v.front() == L'\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
        g_values[Lower(section + L"." + Trim(line.substr(0, eq)))] = v;
    }
}

const Default* Find(const std::wstring& name) {
    const std::wstring want = Lower(name);
    for (const auto& d : kDefaults)
        if (Lower(std::wstring(d.section) + L"." + d.key) == want) return &d;
    return nullptr;
}

}  // namespace

const Default* Defaults(size_t* count) {
    *count = sizeof kDefaults / sizeof kDefaults[0];
    return kDefaults;
}

std::string Path() { return plat::ConfigDir() + "/settings.ini"; }

void EnsureFile() {
    if (plat::FileExists(Path())) return;
    std::wstring text =
        L"; Bunbun settings. Uncomment a line (remove the ';') and change it; the island\n"
        L"; reloads on save. Booleans are true/false.\n";
    const wchar_t* current = nullptr;
    for (const auto& d : kDefaults) {
        if (!current || wcscmp(current, d.section) != 0) {
            text += L"\n[";
            text += d.section;
            text += L"]\n";
            current = d.section;
        }
        text += L";";
        text += d.key;
        text += L'=';
        text += d.value;
        text += L"\n";
    }
    plat::WriteFileAtomic(Path(), plat::Utf8(text));
}

std::wstring Get(const std::wstring& name) {
    {
        std::lock_guard lock(g_mutex);
        LoadLocked();
        if (auto it = g_values.find(Lower(name)); it != g_values.end()) return it->second;
    }
    const Default* d = Find(name);
    return d ? d->value : L"";
}

bool IsSet(const std::wstring& name) {
    std::lock_guard lock(g_mutex);
    LoadLocked();
    return g_values.count(Lower(name)) > 0;
}

int GetInt(const std::wstring& name) {
    const std::wstring v = Lower(Get(name));
    if (v == L"true") return 1;
    if (v == L"false") return 0;
    return static_cast<int>(wcstol(v.c_str(), nullptr, 10));
}

bool GetBool(const std::wstring& name) { return GetInt(name) != 0; }
double GetDouble(const std::wstring& name) { return wcstod(Get(name).c_str(), nullptr); }
bool IsKnown(const std::wstring& name) { return Find(name) != nullptr; }

bool Set(const std::wstring& name, const std::wstring* value) {
    const Default* d = Find(name);
    if (!d) return false;
    EnsureFile();
    std::lock_guard lock(g_mutex);
    std::vector<std::wstring> lines = Lines(ReadText());
    const std::wstring wantSection = Lower(d->section), wantKey = Lower(d->key);
    const std::wstring newLine = value ? std::wstring(d->key) + L"=" + *value : L";" + std::wstring(d->key) + L"=" + d->value;
    std::wstring section;
    long sectionEnd = -1, active = -1, commented = -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::wstring t = Trim(lines[i]);
        if (!t.empty() && t[0] == L'[') {
            section = Lower(Trim(t.substr(1, t.find(L']') - 1)));
            continue;
        }
        if (section != wantSection) continue;
        sectionEnd = static_cast<long>(i);
        const bool isComment = !t.empty() && t[0] == L';';
        const std::wstring body = isComment ? Trim(t.substr(1)) : t;
        const size_t eq = body.find(L'=');
        if (eq == std::wstring::npos || Lower(Trim(body.substr(0, eq))) != wantKey) continue;
        if (isComment && commented < 0) commented = static_cast<long>(i);
        if (!isComment) active = static_cast<long>(i);
    }
    if (active >= 0) lines[active] = newLine;
    else if (commented >= 0 && value) lines[commented] = newLine;
    else if (value && sectionEnd >= 0) lines.insert(lines.begin() + sectionEnd + 1, newLine);
    else if (value) {
        lines.push_back(L"");
        lines.push_back(L"[" + std::wstring(d->section) + L"]");
        lines.push_back(newLine);
    }
    std::wstring text;
    for (const auto& l : lines) text += l + L"\n";
    g_loaded = false;
    return plat::WriteFileAtomic(Path(), plat::Utf8(text));
}

void Invalidate() {
    std::lock_guard lock(g_mutex);
    g_loaded = false;
}

}  // namespace bb::settings

int Wh_GetIntSetting(const wchar_t* format, ...) {
    wchar_t name[256];
    va_list args;
    va_start(args, format);
    vswprintf(name, 256, format, args);
    va_end(args);
    return bb::settings::GetInt(name);
}

void BbLog(const wchar_t* format, ...) {
    static const bool on = [] {
        const char* v = getenv("BUNBUN_LOG");
        return v && *v && *v != '0';
    }();
    if (!on) return;
    wchar_t buf[2048];
    va_list args;
    va_start(args, format);
    vswprintf(buf, 2048, format, args);
    va_end(args);
    static std::mutex m;
    std::lock_guard lock(m);
    bb::plat::MakeDirs(bb::plat::StateDir());
    if (FILE* f = fopen((bb::plat::StateDir() + "/bunbun.log").c_str(), "a")) {
        const time_t now = time(nullptr);
        char stamp[32];
        strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "%s %s\n", stamp, bb::plat::Utf8(buf).c_str());
        fclose(f);
    }
}
