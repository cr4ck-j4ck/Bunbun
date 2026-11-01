// Protocol: the hook connects, writes one JSON object
// terminated by '\n', and for blocking events waits for one reply line:
//   "allow" | "deny" | {"answers":{...}}
// The island closes the pipe without replying when it has no decision; the hook
// then prints nothing and the agent falls back to its own terminal prompt.
//
// Only the current user can open the pipe (explicit DACL), and the hook refuses
// to talk to a server process running as anyone else (anti-squatting).
#pragma once

#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <string>

namespace bb::pipe {

constexpr DWORD kMaxMessage = 4u * 1024u * 1024u;

inline std::wstring CurrentUserSid() {
    std::wstring out;
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return out;
    BYTE buf[256];
    DWORD len = 0;
    if (GetTokenInformation(token, TokenUser, buf, sizeof buf, &len)) {
        LPWSTR s = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &s)) {
            out = s;
            LocalFree(s);
        }
    }
    CloseHandle(token);
    return out;
}

inline std::wstring Name() {
    std::wstring sid = CurrentUserSid();
    if (sid.empty()) {
        wchar_t user[256] = L"user";
        DWORD n = ARRAYSIZE(user);
        GetUserNameW(user, &n);
        sid = user;
    }
    return L"\\\\.\\pipe\\bunbun-" + sid;
}

// True when the process serving `pipe` runs as the same user as we do.
inline bool ServerIsSameUser(HANDLE pipe) {
    ULONG pid = 0;
    if (!GetNamedPipeServerProcessId(pipe, &pid)) return false;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return false;
    bool same = false;
    HANDLE token = nullptr;
    if (OpenProcessToken(proc, TOKEN_QUERY, &token)) {
        BYTE buf[256];
        DWORD len = 0;
        if (GetTokenInformation(token, TokenUser, buf, sizeof buf, &len)) {
            LPWSTR s = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf)->User.Sid, &s)) {
                same = (CurrentUserSid() == s);
                LocalFree(s);
            }
        }
        CloseHandle(token);
    }
    CloseHandle(proc);
    return same;
}

inline bool WriteAll(HANDLE h, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        DWORD n = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(data.size() - off, 64 * 1024));
        if (!WriteFile(h, data.data() + off, chunk, &n, nullptr) || n == 0) return false;
        off += n;
    }
    return true;
}

// Reads until '\n', EOF or kMaxMessage. Returns false on error with no data.
inline bool ReadLine(HANDLE h, std::string* out) {
    out->clear();
    char buf[4096];
    while (out->size() < kMaxMessage) {
        DWORD n = 0;
        BOOL ok = ReadFile(h, buf, sizeof buf, &n, nullptr);
        if (!ok && GetLastError() != ERROR_MORE_DATA) break;
        if (n == 0) break;
        out->append(buf, n);
        const size_t nl = out->find('\n');
        if (nl != std::string::npos) {
            out->resize(nl);
            return true;
        }
    }
    return !out->empty();
}

}  // namespace bb::pipe
