// Bunbun — INI-backed settings and storage implementation (see wh_shim.h).
#include "wh_shim.h"

#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>

namespace {

constexpr bb::SettingDefault kDefaults[] = {
#include "default_settings.inc"
};

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw)) && raw) out = raw;
    CoTaskMemFree(raw);
    return out;
}

std::wstring EnsureDir(REFKNOWNFOLDERID id) {
    // Bunbun: BUNBUN_DATA_DIR points everything at another folder (its Roaming and
    // Local subfolders), so tests can run on a copy instead of the real data.
    wchar_t over[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"BUNBUN_DATA_DIR", over, MAX_PATH) > 0) {
        const std::wstring test = std::wstring(over) + (id == FOLDERID_LocalAppData ? L"\\Local" : L"\\Roaming");
        SHCreateDirectoryExW(nullptr, test.c_str(), nullptr);
        return test;
    }
    std::wstring dir = KnownFolder(id) + L"\\Bunbun";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

const std::wstring& RoamingDir() {
    static const std::wstring d = EnsureDir(FOLDERID_RoamingAppData);
    return d;
}
const std::wstring& LocalDir() {
    static const std::wstring d = EnsureDir(FOLDERID_LocalAppData);
    return d;
}

std::wstring Format(PCWSTR format, va_list args) {
    wchar_t buf[1024];
    int n = vswprintf(buf, ARRAYSIZE(buf), format, args);
    if (n < 0) buf[ARRAYSIZE(buf) - 1] = L'\0';
    return buf;
}

// "Section.Key" -> ("Section", "Key"). Names without a dot land in [General].
void SplitName(const std::wstring& name, std::wstring* section, std::wstring* key) {
    const size_t dot = name.find(L'.');
    if (dot == std::wstring::npos) {
        *section = L"General";
        *key = name;
    } else {
        *section = name.substr(0, dot);
        *key = name.substr(dot + 1);
    }
}

const wchar_t* DefaultFor(const std::wstring& section, const std::wstring& key) {
    for (const auto& d : kDefaults)
        if (_wcsicmp(d.section, section.c_str()) == 0 && _wcsicmp(d.key, key.c_str()) == 0) return d.value;
    return nullptr;
}

std::wstring ReadSetting(const std::wstring& name) {
    std::wstring section, key;
    SplitName(name, &section, &key);
    // A sentinel distinguishes "absent" from "explicitly empty".
    constexpr wchar_t kMissing[] = L"\x01missing\x01";
    wchar_t buf[2048];
    GetPrivateProfileStringW(section.c_str(), key.c_str(), kMissing, buf, ARRAYSIZE(buf), bb::SettingsPath());
    if (wcscmp(buf, kMissing) != 0) return buf;
    const wchar_t* def = DefaultFor(section, key);
    return def ? def : L"";
}

std::mutex g_logMutex;

}  // namespace

namespace bb {

const wchar_t* SettingsPath() {
    static const std::wstring p = RoamingDir() + L"\\settings.ini";
    return p.c_str();
}

const wchar_t* StatePath() {
    static const std::wstring p = LocalDir() + L"\\state.ini";
    return p.c_str();
}

const SettingDefault* DefaultSettings(size_t* count) {
    *count = sizeof(kDefaults) / sizeof(kDefaults[0]);
    return kDefaults;
}

void EnsureSettingsFile() {
    if (GetFileAttributesW(SettingsPath()) != INVALID_FILE_ATTRIBUTES) return;

    // Written as UTF-16LE with a BOM: GetPrivateProfileString only reads
    // non-ASCII text correctly from a Unicode INI file.
    // Every key is written commented out, showing its default. Defaults stay
    // live (and can improve in later versions) until the user uncomments one.
    std::wstring text =
        L"; Bunbun settings. Uncomment a line (remove the ';') and change it; the island\r\n"
        L"; reloads on save. Booleans are true/false.\r\n";
    const wchar_t* current = nullptr;
    for (const auto& d : kDefaults) {
        if (!current || wcscmp(current, d.section) != 0) {
            text += L"\r\n[";
            text += d.section;
            text += L"]\r\n";
            current = d.section;
        }
        text += L";";
        text += d.key;
        text += L'=';
        text += d.value;
        text += L"\r\n";
    }

    HANDLE f = CreateFileW(SettingsPath(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    const wchar_t bom = 0xFEFF;
    WriteFile(f, &bom, sizeof bom, &written, nullptr);
    WriteFile(f, text.data(), static_cast<DWORD>(text.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(f);
}

}  // namespace bb

int Wh_GetIntSetting(PCWSTR valueName, ...) {
    va_list args;
    va_start(args, valueName);
    const std::wstring name = Format(valueName, args);
    va_end(args);

    const std::wstring v = ReadSetting(name);
    if (_wcsicmp(v.c_str(), L"true") == 0) return 1;
    if (_wcsicmp(v.c_str(), L"false") == 0) return 0;
    return static_cast<int>(wcstol(v.c_str(), nullptr, 10));
}

PCWSTR Wh_GetStringSetting(PCWSTR valueName, ...) {
    va_list args;
    va_start(args, valueName);
    const std::wstring name = Format(valueName, args);
    va_end(args);

    const std::wstring v = ReadSetting(name);
    wchar_t* out = new wchar_t[v.size() + 1];
    wmemcpy(out, v.c_str(), v.size() + 1);
    return out;
}

void Wh_FreeStringSetting(PCWSTR string) {
    delete[] string;
}

int Wh_GetIntValue(PCWSTR valueName, int defaultValue) {
    return static_cast<int>(GetPrivateProfileIntW(L"State", valueName, defaultValue, bb::StatePath()));
}

BOOL Wh_SetIntValue(PCWSTR valueName, int value) {
    wchar_t buf[16];
    swprintf(buf, ARRAYSIZE(buf), L"%d", value);
    return WritePrivateProfileStringW(L"State", valueName, buf, bb::StatePath());
}

BOOL Wh_GetModStoragePath(PWSTR pathBuffer, size_t bufferChars) {
    const std::wstring& dir = LocalDir();
    if (dir.size() + 1 > bufferChars) return FALSE;
    wmemcpy(pathBuffer, dir.c_str(), dir.size() + 1);
    return TRUE;
}

void BbLog(PCWSTR format, ...) {
    va_list args;
    va_start(args, format);
    std::wstring line = L"[bunbun] " + Format(format, args) + L"\n";
    va_end(args);
    OutputDebugStringW(line.c_str());

    static const bool toFile = GetEnvironmentVariableW(L"BUNBUN_LOG", nullptr, 0) > 0;
    if (!toFile) return;
    std::lock_guard lock(g_logMutex);
    static const std::wstring path = LocalDir() + L"\\bunbun.log";
    FILE* f = _wfopen(path.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    fputws(line.c_str(), f);
    fclose(f);
}
