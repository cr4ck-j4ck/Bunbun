// Bunbun — the small slice of Win32 that shared files use, for the Linux build.
//
// Only the Linux build puts this folder on the include path, and only for the
// shared sources that still include <windows.h> (the mascot, the skin-pack
// loader, the hook installer). It lets those files compile unchanged, so the
// Windows build never sees a difference. New Linux code should not use it:
// it talks to POSIX / GLib directly (see src/platform/linux).
//
// Paths: shared code joins paths with L"\\". Every function here that takes a
// path turns backslashes into slashes first, so "C:\x" style joins still land
// in the right place under $HOME.
#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>

using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using UINT = unsigned int;
using UINT32 = uint32_t;
using UINT64 = uint64_t;
using ULONGLONG = uint64_t;
using LONG = int32_t;
using BOOL = int;
using HRESULT = int32_t;
using WCHAR = wchar_t;
using PWSTR = wchar_t*;
using LPWSTR = wchar_t*;
using PCWSTR = const wchar_t*;
using LPCWSTR = const wchar_t*;
using HANDLE = void*;
using LPARAM = intptr_t;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define WINAPI
#define MAX_PATH 4096
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))

#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_FILE_EXISTS 80
#define CP_UTF8 65001

union LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; };
    int64_t QuadPart;
};

struct SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
};

struct RECT {
    LONG left, top, right, bottom;
};

// UTF-8 ⇄ wchar_t (UTF-32 on Linux). Same contract as the Win32 originals:
// with a null output they return the length needed.
int MultiByteToWideChar(UINT codePage, DWORD flags, const char* in, int inLen, wchar_t* out, int outLen);
int WideCharToMultiByte(UINT codePage, DWORD flags, const wchar_t* in, int inLen, char* out, int outLen,
                        const char* defaultChar, BOOL* usedDefault);

DWORD GetLastError();
DWORD GetFileAttributesW(PCWSTR path);
HANDLE CreateFileW(PCWSTR path, DWORD access, DWORD share, void* security, DWORD disposition, DWORD flags,
                   HANDLE templ);
BOOL ReadFile(HANDLE f, void* buf, DWORD size, DWORD* read, void* overlapped);
BOOL WriteFile(HANDLE f, const void* buf, DWORD size, DWORD* written, void* overlapped);
BOOL GetFileSizeEx(HANDLE f, LARGE_INTEGER* size);
BOOL FlushFileBuffers(HANDLE f);
BOOL CloseHandle(HANDLE h);
BOOL CopyFileW(PCWSTR from, PCWSTR to, BOOL failIfExists);
BOOL MoveFileExW(PCWSTR from, PCWSTR to, DWORD flags);
BOOL DeleteFileW(PCWSTR path);
DWORD GetEnvironmentVariableW(PCWSTR name, wchar_t* buf, DWORD size);
DWORD GetModuleFileNameW(void* module, wchar_t* buf, DWORD size);
DWORD GetShortPathNameW(PCWSTR path, wchar_t* buf, DWORD size);
void GetLocalTime(SYSTEMTIME* t);
DWORD GetPrivateProfileStringW(PCWSTR section, PCWSTR key, PCWSTR def, wchar_t* out, DWORD size, PCWSTR path);

inline int _wtoi(const wchar_t* s) { return static_cast<int>(wcstol(s, nullptr, 10)); }
inline double _wtof(const wchar_t* s) { return wcstod(s, nullptr); }
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return wcscasecmp(a, b); }
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }

namespace bbcompat {
// A Windows-style path (backslashes) as a UTF-8 POSIX path.
std::string PosixPath(const std::wstring& path);
std::string Utf8(const std::wstring& w);
std::wstring Wide(const std::string& s);
}  // namespace bbcompat
