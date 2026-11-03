// Bunbun — the Win32 calls of windows.h / shlobj.h in this folder, on POSIX.
#include "windows.h"
#include "shlobj.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

thread_local DWORD t_lastError = 0;

DWORD FromErrno(int e) {
    switch (e) {
        case ENOENT: return ERROR_FILE_NOT_FOUND;
        case ENOTDIR: return ERROR_PATH_NOT_FOUND;
        case EEXIST: return ERROR_FILE_EXISTS;
        case EACCES: case EPERM: return ERROR_ACCESS_DENIED;
        default: return 1000 + e;
    }
}

BOOL Fail() {
    t_lastError = FromErrno(errno);
    return FALSE;
}

int Fd(HANDLE h) { return static_cast<int>(reinterpret_cast<intptr_t>(h)) - 1; }
HANDLE FromFd(int fd) { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd) + 1); }

std::string Env(const char* name) {
    const char* v = getenv(name);
    return v ? v : "";
}

std::string Home() {
    std::string h = Env("HOME");
    return h.empty() ? "/tmp" : h;
}

std::string Xdg(const char* var, const char* fallback) {
    std::string v = Env(var);
    return !v.empty() && v[0] == '/' ? v : Home() + fallback;
}

}  // namespace

namespace bbcompat {

std::string Utf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t wc : w) {
        auto c = static_cast<uint32_t>(wc);
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::wstring Wide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        uint32_t c = 0xFFFD;
        size_t n = 1;
        if (b < 0x80) { c = b; }
        else if ((b >> 5) == 6 && i + 1 < s.size()) { c = ((b & 0x1F) << 6) | (s[i + 1] & 0x3F); n = 2; }
        else if ((b >> 4) == 14 && i + 2 < s.size()) { c = ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); n = 3; }
        else if ((b >> 3) == 30 && i + 3 < s.size()) {
            c = ((b & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            n = 4;
        }
        out += static_cast<wchar_t>(c);
        i += n;
    }
    return out;
}

std::string PosixPath(const std::wstring& path) {
    std::string p = Utf8(path);
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

}  // namespace bbcompat

using bbcompat::PosixPath;

int MultiByteToWideChar(UINT, DWORD, const char* in, int inLen, wchar_t* out, int outLen) {
    const std::wstring w = bbcompat::Wide(inLen < 0 ? std::string(in) + '\0' : std::string(in, inLen));
    if (!out || outLen == 0) return static_cast<int>(w.size());
    const int n = std::min<int>(outLen, static_cast<int>(w.size()));
    std::copy(w.begin(), w.begin() + n, out);
    return n;
}

int WideCharToMultiByte(UINT, DWORD, const wchar_t* in, int inLen, char* out, int outLen, const char*, BOOL*) {
    const std::string s = bbcompat::Utf8(inLen < 0 ? std::wstring(in) + L'\0' : std::wstring(in, inLen));
    if (!out || outLen == 0) return static_cast<int>(s.size());
    const int n = std::min<int>(outLen, static_cast<int>(s.size()));
    std::copy(s.begin(), s.begin() + n, out);
    return n;
}

DWORD GetLastError() { return t_lastError; }

DWORD GetFileAttributesW(PCWSTR path) {
    struct stat st{};
    if (stat(PosixPath(path).c_str(), &st) != 0) {
        Fail();
        return INVALID_FILE_ATTRIBUTES;
    }
    return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

HANDLE CreateFileW(PCWSTR path, DWORD access, DWORD, void*, DWORD disposition, DWORD, HANDLE) {
    int flags = (access & GENERIC_WRITE) ? ((access & GENERIC_READ) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (disposition == CREATE_NEW) flags |= O_CREAT | O_EXCL;
    else if (disposition == CREATE_ALWAYS) flags |= O_CREAT | O_TRUNC;
    const int fd = open(PosixPath(path).c_str(), flags | O_CLOEXEC, 0644);
    if (fd < 0) {
        Fail();
        return INVALID_HANDLE_VALUE;
    }
    return FromFd(fd);
}

BOOL ReadFile(HANDLE f, void* buf, DWORD size, DWORD* read, void*) {
    ssize_t n;
    do n = ::read(Fd(f), buf, size); while (n < 0 && errno == EINTR);
    if (n < 0) { *read = 0; return Fail(); }
    *read = static_cast<DWORD>(n);
    return TRUE;
}

BOOL WriteFile(HANDLE f, const void* buf, DWORD size, DWORD* written, void*) {
    size_t done = 0;
    while (done < size) {
        const ssize_t n = ::write(Fd(f), static_cast<const char*>(buf) + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { *written = static_cast<DWORD>(done); return Fail(); }
        done += static_cast<size_t>(n);
    }
    *written = static_cast<DWORD>(done);
    return TRUE;
}

BOOL GetFileSizeEx(HANDLE f, LARGE_INTEGER* size) {
    struct stat st{};
    if (fstat(Fd(f), &st) != 0) return Fail();
    size->QuadPart = st.st_size;
    return TRUE;
}

BOOL FlushFileBuffers(HANDLE f) { return fsync(Fd(f)) == 0 ? TRUE : Fail(); }
BOOL CloseHandle(HANDLE h) { return close(Fd(h)) == 0 ? TRUE : Fail(); }

BOOL CopyFileW(PCWSTR from, PCWSTR to, BOOL failIfExists) {
    std::error_code ec;
    const auto opt = failIfExists ? std::filesystem::copy_options::none : std::filesystem::copy_options::overwrite_existing;
    if (std::filesystem::copy_file(PosixPath(from), PosixPath(to), opt, ec)) return TRUE;
    t_lastError = ec == std::errc::file_exists ? ERROR_FILE_EXISTS : FromErrno(ec.value());
    return FALSE;
}

BOOL MoveFileExW(PCWSTR from, PCWSTR to, DWORD flags) {
    const std::string dst = PosixPath(to);
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && access(dst.c_str(), F_OK) == 0) {
        t_lastError = ERROR_FILE_EXISTS;
        return FALSE;
    }
    return rename(PosixPath(from).c_str(), dst.c_str()) == 0 ? TRUE : Fail();
}

BOOL DeleteFileW(PCWSTR path) { return unlink(PosixPath(path).c_str()) == 0 ? TRUE : Fail(); }

DWORD GetEnvironmentVariableW(PCWSTR name, wchar_t* buf, DWORD size) {
    const char* v = getenv(bbcompat::Utf8(name).c_str());
    if (!v) { t_lastError = 203; return 0; }  // ERROR_ENVVAR_NOT_FOUND
    const std::wstring w = bbcompat::Wide(v);
    if (!buf || size <= w.size()) return static_cast<DWORD>(w.size() + 1);
    std::copy(w.begin(), w.end(), buf);
    buf[w.size()] = 0;
    return static_cast<DWORD>(w.size());
}

DWORD GetModuleFileNameW(void*, wchar_t* buf, DWORD size) {
    char path[MAX_PATH];
    const ssize_t n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0 || size == 0) return 0;
    path[n] = 0;
    const std::wstring w = bbcompat::Wide(path);
    const size_t len = std::min<size_t>(w.size(), size - 1);
    std::copy(w.begin(), w.begin() + len, buf);
    buf[len] = 0;
    return static_cast<DWORD>(len);
}

// No 8.3 names here; a path with spaces is used as it is.
DWORD GetShortPathNameW(PCWSTR path, wchar_t* buf, DWORD size) {
    const size_t len = wcslen(path);
    if (len + 1 > size) return static_cast<DWORD>(len + 1);
    wcscpy(buf, path);
    return static_cast<DWORD>(len);
}

void GetLocalTime(SYSTEMTIME* t) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm lt{};
    localtime_r(&ts.tv_sec, &lt);
    *t = SYSTEMTIME{static_cast<WORD>(lt.tm_year + 1900), static_cast<WORD>(lt.tm_mon + 1), static_cast<WORD>(lt.tm_wday),
                    static_cast<WORD>(lt.tm_mday),        static_cast<WORD>(lt.tm_hour),    static_cast<WORD>(lt.tm_min),
                    static_cast<WORD>(lt.tm_sec),         static_cast<WORD>(ts.tv_nsec / 1000000)};
}

// Plain INI reading: [Section] then key=value, ';' or '#' comments, case-
// insensitive names. Reads UTF-8, or UTF-16LE with a BOM (Windows' own format).
DWORD GetPrivateProfileStringW(PCWSTR section, PCWSTR key, PCWSTR def, wchar_t* out, DWORD size, PCWSTR path) {
    const auto give = [&](const std::wstring& v) -> DWORD {
        if (!out || size == 0) return 0;
        const size_t n = std::min<size_t>(v.size(), size - 1);
        std::copy(v.begin(), v.begin() + n, out);
        out[n] = 0;
        return static_cast<DWORD>(n);
    };
    std::ifstream f(PosixPath(path), std::ios::binary);
    if (!f) return give(def ? def : L"");
    std::stringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();
    std::wstring text;
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF && static_cast<unsigned char>(raw[1]) == 0xFE) {
        for (size_t i = 2; i + 1 < raw.size(); i += 2)
            text += static_cast<wchar_t>(static_cast<unsigned char>(raw[i]) | (static_cast<unsigned char>(raw[i + 1]) << 8));
    } else {
        if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);
        text = bbcompat::Wide(raw);
    }
    const auto trim = [](std::wstring s) {
        const size_t a = s.find_first_not_of(L" \t\r");
        const size_t b = s.find_last_not_of(L" \t\r");
        return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
    };
    std::wstring current;
    std::wistringstream lines(text);
    for (std::wstring line; std::getline(lines, line);) {
        line = trim(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') {
            const size_t close = line.find(L']');
            current = trim(line.substr(1, close == std::wstring::npos ? std::wstring::npos : close - 1));
            continue;
        }
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        if (wcscasecmp(current.c_str(), section) == 0 && wcscasecmp(trim(line.substr(0, eq)).c_str(), key) == 0) {
            std::wstring v = trim(line.substr(eq + 1));
            if (v.size() >= 2 && (v.front() == L'"' || v.front() == L'\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
            return give(v);
        }
    }
    return give(def ? def : L"");
}

HRESULT SHGetKnownFolderPath(KNOWNFOLDERID id, DWORD flags, HANDLE, PWSTR* path) {
    std::string p;
    switch (id) {
        case FOLDERID_Profile: p = Home(); break;
        case FOLDERID_RoamingAppData: p = Xdg("XDG_CONFIG_HOME", "/.config"); break;
        case FOLDERID_LocalAppData: p = Xdg("XDG_STATE_HOME", "/.local/state"); break;
    }
    if (flags & KF_FLAG_CREATE) {
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
    }
    const std::wstring w = bbcompat::Wide(p);
    *path = static_cast<wchar_t*>(malloc((w.size() + 1) * sizeof(wchar_t)));
    if (!*path) return E_OUTOFMEMORY;
    wcscpy(*path, w.c_str());
    return S_OK;
}

void CoTaskMemFree(void* p) { free(p); }

int SHCreateDirectoryExW(void*, PCWSTR path, void*) {
    std::error_code ec;
    std::filesystem::create_directories(PosixPath(path), ec);
    return ec ? static_cast<int>(FromErrno(ec.value())) : ERROR_SUCCESS;
}
