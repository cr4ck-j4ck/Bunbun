// Bunbun — settings on Linux: $XDG_CONFIG_HOME/Bunbun/settings.ini.
//
// The same file format and rules as on Windows (wh_shim): [Section] Key=value,
// every key written commented out at first so defaults stay live, defaults
// generated from settings_schema.yaml into default_settings.inc. The file is
// UTF-8 here.
#pragma once

#include <cstddef>
#include <string>

namespace bb::settings {

struct Default {
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* value;
};
const Default* Defaults(size_t* count);

std::string Path();
// Writes settings.ini with every default commented out, if it doesn't exist.
void EnsureFile();

// "Section.Key" → the value set in the file, else its default ("" if unknown).
std::wstring Get(const std::wstring& name);
// Whether the file sets this key (an uncommented line).
bool IsSet(const std::wstring& name);
int GetInt(const std::wstring& name);
bool GetBool(const std::wstring& name);
double GetDouble(const std::wstring& name);
bool IsKnown(const std::wstring& name);

// Sets a key (uncommenting its line), or with value == nullptr puts it back to
// its default (commenting the line out again).
bool Set(const std::wstring& name, const std::wstring* value);

// Rereads the file on the next Get (call after it changes on disk).
void Invalidate();

}  // namespace bb::settings
