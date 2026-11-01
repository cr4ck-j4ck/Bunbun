// Bunbun — settings, storage, and state management backend.
//
// Provides settings, state storage, and runtime logging backed by two INI files:
//   %APPDATA%\Bunbun\settings.ini      user settings, [Section] Key=value
//   %LOCALAPPDATA%\Bunbun\state.ini    values the island remembers itself
//
// Settings missing from settings.ini fall back to the defaults generated from
// default_settings.inc (see scripts/gen_defaults.sh).
#pragma once

#include <windows.h>

#define WH_MOD_ID L"bunbun"

// Version and homepage are injected by build.sh: the version from the VERSION
// file, the homepage from BUNBUN_HOMEPAGE (CI sets it to the GitHub repo).
#ifndef BUNBUN_VERSION_STR
#define BUNBUN_VERSION_STR "0.0.0-dev"
#endif
#define BB_WIDEN2(s) L##s
#define BB_WIDEN(s) BB_WIDEN2(s)
#define WH_MOD_VERSION BB_WIDEN(BUNBUN_VERSION_STR)
// User-Agent for the island's own web requests (lyrics, dictionary).
#ifdef BUNBUN_HOMEPAGE_STR
#define BUNBUN_USER_AGENT L"Bunbun/" WH_MOD_VERSION L" (+" BB_WIDEN(BUNBUN_HOMEPAGE_STR) L")"
#else
#define BUNBUN_USER_AGENT L"Bunbun/" WH_MOD_VERSION
#endif

int Wh_GetIntSetting(PCWSTR valueName, ...);
PCWSTR Wh_GetStringSetting(PCWSTR valueName, ...);
void Wh_FreeStringSetting(PCWSTR string);
int Wh_GetIntValue(PCWSTR valueName, int defaultValue);
BOOL Wh_SetIntValue(PCWSTR valueName, int value);
BOOL Wh_GetModStoragePath(PWSTR pathBuffer, size_t bufferChars);

void BbLog(PCWSTR format, ...);
#define Wh_Log(...) BbLog(__VA_ARGS__)

namespace bb {
// Full paths of the two INI files (directories are created on first use).
const wchar_t* SettingsPath();
const wchar_t* StatePath();
// Writes settings.ini with every default if it does not exist yet.
void EnsureSettingsFile();

// Every known setting and its default (from the settings block).
struct SettingDefault {
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* value;
};
const SettingDefault* DefaultSettings(size_t* count);
}  // namespace bb
