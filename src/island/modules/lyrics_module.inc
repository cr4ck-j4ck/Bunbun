// ── Lyrics (LRCLIB, https://lrclib.net; NetEase as an optional fallback) ─────
#include "lyrics_text.h"

// User-Agent sent to LRCLIB and NetEase.
constexpr wchar_t kLyricsUserAgent[] =
    BUNBUN_USER_AGENT;

bool IsUrlSafeChar(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '-' || c == '_' || c == '.' || c == '~';
}

std::string UrlEncodeComponent(const std::wstring& value) {
    int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8;
    if (len > 0) {
        utf8.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, utf8.data(), len, nullptr, nullptr);
    }

    std::string out;
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if (IsUrlSafeChar(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::wstring AsciiToWide(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) {
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return w;
}

// Unescapes a raw JSON string value (still containing \n, \", \uXXXX etc.)
// and decodes it from UTF-8 into a wide string.
std::wstring JsonStringUnescape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            char next = raw[i + 1];
            switch (next) {
                case 'n': out.push_back('\n'); ++i; break;
                case 'r': out.push_back('\r'); ++i; break;
                case 't': out.push_back('\t'); ++i; break;
                case '"': out.push_back('"'); ++i; break;
                case '\\': out.push_back('\\'); ++i; break;
                case '/': out.push_back('/'); ++i; break;
                case 'u': {
                    if (i + 5 < raw.size()) {
                        std::string hexNarrow = raw.substr(i + 2, 4);
                        std::wstring hexWide = AsciiToWide(hexNarrow);
                        wchar_t code = static_cast<wchar_t>(wcstoul(hexWide.c_str(), nullptr, 16));
                        char utf8buf[4] = {};
                        int outLen = WideCharToMultiByte(CP_UTF8, 0, &code, 1, utf8buf,
                                                         sizeof(utf8buf), nullptr, nullptr);
                        if (outLen > 0) {
                            out.append(utf8buf, outLen);
                        }
                        i += 5;
                    }
                    break;
                }
                default:
                    out.push_back(next);
                    ++i;
                    break;
            }
        } else {
            out.push_back(c);
        }
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, nullptr, 0);
    std::wstring wout;
    if (wlen > 0) {
        wout.resize(wlen - 1);
        MultiByteToWideChar(CP_UTF8, 0, out.c_str(), -1, wout.data(), wlen);
    }
    return wout;
}

// Finds "fieldName":"....." in a JSON blob and returns the raw (still
// escaped) contents between the quotes. Returns false if missing or null.
bool ExtractJsonStringField(const std::string& json, const char* fieldKey, std::string* outRaw) {
    const std::string key = std::string("\"") + fieldKey + "\":\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) {
        return false;
    }
    pos += key.size();

    std::string result;
    bool escaped = false;
    for (size_t i = pos; i < json.size(); ++i) {
        char c = json[i];
        if (escaped) {
            result.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            result.push_back(c);
            escaped = true;
            continue;
        }
        if (c == '"') {
            *outRaw = result;
            return true;
        }
        result.push_back(c);
    }
    return false;
}

// Splits a top-level JSON array "[ {...}, {...} ]" into the substrings for
// each object, respecting nested braces and quoted strings.
std::vector<std::string> SplitJsonObjectArray(const std::string& json) {
    std::vector<std::string> objects;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    size_t objectStart = std::string::npos;

    for (size_t i = 0; i < json.size(); ++i) {
        char c = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }

        if (c == '"') {
            inString = true;
        } else if (c == '{') {
            if (depth == 0) {
                objectStart = i;
            }
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && objectStart != std::string::npos) {
                objects.push_back(json.substr(objectStart, i - objectStart + 1));
                objectStart = std::string::npos;
            }
        }
    }

    return objects;
}

std::vector<LyricsLine> ParseSyncedLyrics(const std::wstring& text) {
    std::vector<LyricsLine> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        // Bunbun: a line can carry several timestamps ("[00:12.30][01:40.10]chorus"),
        // and the fraction can have 1-3 digits: .5 = 500 ms, .12 = 120 ms, .123 = 123 ms.
        // Tags that aren't times ([ar:...], [offset:...]) are skipped.
        std::vector<int64_t> times;
        size_t pos = 0;
        while (pos < line.size() && line[pos] == L'[') {
            const size_t close = line.find(L']', pos);
            if (close == std::wstring::npos) break;
            const std::wstring ts = line.substr(pos + 1, close - pos - 1);
            int minutes = 0, seconds = 0;
            wchar_t fraction[8] = {};
            const int got = swscanf_s(ts.c_str(), L"%d:%d.%7[0-9]", &minutes, &seconds, fraction,
                                      static_cast<unsigned>(ARRAYSIZE(fraction)));
            if (got < 2) break;
            int64_t ms = static_cast<int64_t>(minutes) * 60000 + static_cast<int64_t>(seconds) * 1000;
            if (got >= 3) {
                int64_t frac = 0;
                int digits = 0;
                for (const wchar_t* d = fraction; *d && digits < 3; ++d, ++digits) frac = frac * 10 + (*d - L'0');
                while (digits < 3) { frac *= 10; ++digits; }
                ms += frac;
            }
            times.push_back(ms);
            pos = close + 1;
        }
        if (!times.empty()) {
            std::wstring lyricText = line.substr(pos);
            while (!lyricText.empty() && lyricText.front() == L' ') {
                lyricText.erase(lyricText.begin());
            }
            for (const int64_t t : times) {
                LyricsLine ll;
                ll.timeMs = t;
                ll.text = lyricText;
                lines.push_back(std::move(ll));
            }
        }

        if (end == text.size()) break;
        start = end + 1;
    }
    // Repeated lines (several timestamps) are listed out of order; play order wins.
    std::stable_sort(lines.begin(), lines.end(),
                     [](const LyricsLine& a, const LyricsLine& b) { return a.timeMs < b.timeMs; });
    return lines;
}

std::vector<LyricsLine> ParsePlainLyrics(const std::wstring& text) {
    std::vector<LyricsLine> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty()) {
            LyricsLine ll;
            ll.timeMs = -1;
            ll.text = std::move(line);
            lines.push_back(std::move(ll));
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

// ── Lyrics cache ─────────────────────────────────────────────────────────────
// Lookup order is memory -> disk -> network; a network result is written to both.
// Everything below runs on the lyrics thread ONLY, so the memory cache and the
// directory bookkeeping need no locks. The only cross-thread state is the four
// atomics here: the right-click menu reads the counters and sets the clear flag,
// and never touches the disk itself.
std::atomic<bool> g_lyricsCacheClearRequested{false};
std::atomic<bool> g_lyricsCacheCountsValid{false};   // false until the first folder scan
std::atomic<int> g_lyricsCacheFileCount{0};
std::atomic<uint64_t> g_lyricsCacheBytes{0};

namespace LyricsCache {

constexpr int64_t kFoundTtlSec = 90LL * 24 * 3600;     // found lyrics: refetched after 90 days
constexpr int64_t kNotFoundTtlSec = 3LL * 24 * 3600;   // "not found": retried after 3 days
constexpr size_t kMemoryCapacity = 30;
constexpr int kLooseDurationToleranceSec = 3;          // browsers disagree by a second or two
constexpr uint64_t kMaxEntryBytes = 1024 * 1024;       // anything bigger is treated as corrupt
constexpr double kCleanupMinIntervalSec = 60.0;

// Which sources answered a lookup. A cached miss only counts if it was made with
// at least the sources in use now: turning NetEase on retries songs LRCLIB alone
// had nothing for.
constexpr int kSourceLrclib = 1;
constexpr int kSourceNetEase = 2;
// Raise this when the lookup gets smarter (new source, better title cleaning):
// cached misses from older lookups are then retried once.
constexpr int kLookupVersion = 2;
// Sources the lyrics thread is using right now (lyrics thread only).
int g_currentSources = kSourceLrclib;

// Found: lyrics. NotFound: every source answered, none matched. Instrumental: a
// source said the track has no vocals.
enum class Status { Found, NotFound, Instrumental };

struct Keys {
    std::wstring loose;      // normalized artist|title
    std::wstring full;       // normalized artist|title|album|durationSec
    int durationSec = 0;
    uint64_t looseHash = 0;
    uint64_t fullHash = 0;
};

struct Entry {
    Status status = Status::Found;
    bool synced = false;
    int64_t savedAt = 0;     // unix seconds
    int durationSec = 0;
    int lookup = 0;          // kLookupVersion that produced a miss
    int sources = 0;         // kSource* bits that answered
    std::wstring looseKey;
    std::wstring fullKey;
    uint64_t looseHash = 0;
    uint64_t fullHash = 0;
    std::vector<LyricsLine> lines;
};

// ---- small helpers ----------------------------------------------------------
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

int64_t UnixNow() {
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u = {};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(u.QuadPart / 10000000ULL) - 11644473600LL;
}

uint64_t Fnv1a64(const std::string& bytes) {
    uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : bytes) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

uint64_t HashKey(const std::wstring& key) {
    return Fnv1a64(WideToUtf8(key));
}

bool EndsWith(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    return s.size() >= n && _wcsicmp(s.c_str() + s.size() - n, suffix) == 0;
}

// Lowercase, trim, collapse whitespace; '|' is replaced so a title can never
// forge the key separator.
std::wstring NormalizePart(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    bool pendingSpace = false;
    for (wchar_t ch : in) {
        if (iswspace(ch) || ch == L'|') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(L' ');
            pendingSpace = false;
        }
        out.push_back(static_cast<wchar_t>(towlower(ch)));
    }
    return out;
}

Keys BuildKeys(const std::wstring& title, const std::wstring& artist,
               const std::wstring& album, int64_t endTicks) {
    Keys k;
    k.durationSec = endTicks > 0 ? static_cast<int>(static_cast<double>(endTicks) / 10000000.0 + 0.5) : 0;
    k.loose = NormalizePart(artist) + L"|" + NormalizePart(title);
    k.full = k.loose + L"|" + NormalizePart(album) + L"|" + std::to_wstring(k.durationSec);
    k.looseHash = HashKey(k.loose);
    k.fullHash = HashKey(k.full);
    return k;
}

std::wstring FileNameFor(uint64_t looseHash, uint64_t fullHash) {
    wchar_t name[48] = {};
    swprintf_s(name, L"%016llx-%016llx.lrc", static_cast<unsigned long long>(looseHash),
               static_cast<unsigned long long>(fullHash));
    return name;
}

bool DurationCompatible(int a, int b) {
    if (a <= 0 || b <= 0) return true;  // unknown on either side: don't reject
    return std::abs(a - b) <= kLooseDurationToleranceSec;
}

bool EntryExpired(const Entry& e, int64_t nowUnix) {
    if (e.savedAt <= 0 || e.savedAt > nowUnix + 86400) return true;  // missing or future-dated
    if (e.status == Status::NotFound) {
        // A miss from an older or narrower lookup is worth one more try.
        if (e.lookup < kLookupVersion || (g_currentSources & ~e.sources) != 0) return true;
        return (nowUnix - e.savedAt) > kNotFoundTtlSec;
    }
    return (nowUnix - e.savedAt) > kFoundTtlSec;
}

void CountersAdjust(int dCount, int64_t dBytes) {
    // Single writer (the lyrics thread), so load/store is race-free; the menu only reads.
    int c = g_lyricsCacheFileCount.load(std::memory_order_relaxed) + dCount;
    if (c < 0) c = 0;
    g_lyricsCacheFileCount.store(c, std::memory_order_relaxed);
    int64_t b = static_cast<int64_t>(g_lyricsCacheBytes.load(std::memory_order_relaxed)) + dBytes;
    if (b < 0) b = 0;
    g_lyricsCacheBytes.store(static_cast<uint64_t>(b), std::memory_order_relaxed);
}

// ---- cache folder -------------------------------------------------------------
// Island storage directory \lyrics. Strictly kept inside storage.
const std::wstring& CacheDir(bool create) {
    static std::wstring dir;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        wchar_t storagePath[MAX_PATH] = {};
        if (Wh_GetModStoragePath(storagePath, ARRAYSIZE(storagePath))) {
            dir = std::wstring(storagePath) + L"\\lyrics";
        }
    }
    if (create && !dir.empty()) {
        // Cheap and idempotent; done on every write so a folder deleted by hand
        // while the mod runs is simply recreated.
        const std::wstring parent = dir.substr(0, dir.find_last_of(L'\\'));
        CreateDirectoryW(parent.c_str(), nullptr);
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    return dir;
}

// ---- memory layer ---------------------------------------------------------------
// MRU at the back. 30 entries, so linear scans are free.
std::vector<Entry>& Memory() {
    static std::vector<Entry> mem;
    return mem;
}

void PutMemory(Entry e) {
    auto& mem = Memory();
    mem.erase(std::remove_if(mem.begin(), mem.end(),
                             [&](const Entry& other) { return other.fullKey == e.fullKey; }),
              mem.end());
    mem.push_back(std::move(e));
    while (mem.size() > kMemoryCapacity) {
        mem.erase(mem.begin());
    }
}

// Exact key first, otherwise the best loose match. Moves the hit to the MRU slot;
// the returned pointer is valid until the next modification of the vector.
const Entry* FindInMemory(const Keys& k, int64_t nowUnix) {
    auto& mem = Memory();
    int exactIdx = -1;
    int looseIdx = -1;
    int looseScore = 1 << 30;
    for (int i = static_cast<int>(mem.size()) - 1; i >= 0; --i) {
        const Entry& e = mem[static_cast<size_t>(i)];
        if (EntryExpired(e, nowUnix)) continue;
        if (e.fullKey == k.full) {
            exactIdx = i;
            break;
        }
        if (e.looseKey == k.loose && DurationCompatible(e.durationSec, k.durationSec)) {
            const int delta = (e.durationSec > 0 && k.durationSec > 0) ? std::abs(e.durationSec - k.durationSec) : 0;
            const int score = delta + (e.status != Status::Found ? 1000 : 0);  // prefer real lyrics
            if (score < looseScore) {
                looseScore = score;
                looseIdx = i;
            }
        }
    }
    const int idx = exactIdx >= 0 ? exactIdx : looseIdx;
    if (idx < 0) return nullptr;
    std::rotate(mem.begin() + idx, mem.begin() + idx + 1, mem.end());
    return &mem.back();
}

// ---- file I/O -------------------------------------------------------------------
// "Touching" (last-write time = now) is what makes least-recently-USED eviction
// work. Expiry uses the saved-at stamp inside the file, which is not touched.
void TouchFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    SetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
}

// Write to <name>.tmp, then rename over the final name: a reader never sees a
// half-written file, and a crash leaves at worst a stray .tmp (swept by cleanup).
bool WriteFileAtomic(const std::wstring& finalPath, const std::string& data) {
    const std::wstring tmp = finalPath + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL wrote = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(h);
    if (!wrote || written != data.size()) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

std::wstring SingleLine(std::wstring s) {
    s.erase(std::remove(s.begin(), s.end(), L'\r'), s.end());
    std::replace(s.begin(), s.end(), L'\n', L' ');
    return s;
}

// File layout (UTF-8):
//   DIWLC 2            <- magic + format version (anything else = miss + delete)
//   saved=<unix>       <- (PeekHeader relies on saved/status being lines 2 and 3)
//   status=found|notfound|instrumental
//   synced=1|0
//   dur=<sec>
//   lookup=<kLookupVersion> / sources=<kSource* bits>   (what produced a miss)
//   lkey=<normalized artist|title>
//   fkey=<normalized artist|title|album|dur>
//   title=... / artist=...   (informational only)
//   ---
//   body: synced -> "<timeMs>\t<text>\n" per line, plain -> "<text>\n" per line
// Version 1 files (found lyrics only) are still read; a version 1 "notfound"
// is from a build that had fewer sources and is dropped.
const char* StatusName(Status s) {
    switch (s) {
        case Status::Found: return "found";
        case Status::Instrumental: return "instrumental";
        default: return "notfound";
    }
}

std::string SerializeEntry(const Entry& e, const std::wstring& title, const std::wstring& artist) {
    std::string out;
    out += "DIWLC 2\n";
    out += "saved=" + std::to_string(e.savedAt) + "\n";
    out += std::string("status=") + StatusName(e.status) + "\n";
    out += e.synced ? "synced=1\n" : "synced=0\n";
    out += "dur=" + std::to_string(e.durationSec) + "\n";
    out += "lookup=" + std::to_string(e.lookup) + "\n";
    out += "sources=" + std::to_string(e.sources) + "\n";
    out += "lkey=" + WideToUtf8(e.looseKey) + "\n";
    out += "fkey=" + WideToUtf8(e.fullKey) + "\n";
    out += "title=" + WideToUtf8(SingleLine(title)) + "\n";
    out += "artist=" + WideToUtf8(SingleLine(artist)) + "\n";
    out += "---\n";

    if (e.status == Status::Found) {
        std::wstring body;
        for (const LyricsLine& line : e.lines) {
            if (e.synced) {
                body += std::to_wstring(line.timeMs);
                body += L'\t';
            }
            body += SingleLine(line.text);
            body += L'\n';
        }
        out += WideToUtf8(body);
    }
    return out;
}

// Returns false for anything that is not a complete, current-version entry.
bool ParseEntry(const std::string& data, Entry* out) {
    size_t pos = 0;
    auto nextLine = [&](std::string* line) -> bool {
        if (pos >= data.size()) return false;
        const size_t end = data.find('\n', pos);
        if (end == std::string::npos) {
            *line = data.substr(pos);
            pos = data.size();
        } else {
            *line = data.substr(pos, end - pos);
            pos = end + 1;
        }
        return true;
    };

    std::string line;
    if (!nextLine(&line) || (line != "DIWLC 1" && line != "DIWLC 2")) return false;
    const bool v1 = (line == "DIWLC 1");

    Entry e;
    bool haveSaved = false, haveStatus = false, haveLoose = false, haveFull = false, headerEnded = false;
    while (nextLine(&line)) {
        if (line == "---") {
            headerEnded = true;
            break;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "saved") {
            e.savedAt = static_cast<int64_t>(strtoll(val.c_str(), nullptr, 10));
            haveSaved = e.savedAt > 0;
        } else if (key == "status") {
            if (val == "found") { e.status = Status::Found; haveStatus = true; }
            else if (val == "notfound") { e.status = Status::NotFound; haveStatus = true; }
            else if (val == "instrumental") { e.status = Status::Instrumental; haveStatus = true; }
        } else if (key == "synced") {
            e.synced = (val == "1");
        } else if (key == "dur") {
            e.durationSec = static_cast<int>(strtol(val.c_str(), nullptr, 10));
        } else if (key == "lookup") {
            e.lookup = static_cast<int>(strtol(val.c_str(), nullptr, 10));
        } else if (key == "sources") {
            e.sources = static_cast<int>(strtol(val.c_str(), nullptr, 10));
        } else if (key == "lkey") {
            e.looseKey = Utf8ToWide(val);
            haveLoose = !e.looseKey.empty();
        } else if (key == "fkey") {
            e.fullKey = Utf8ToWide(val);
            haveFull = !e.fullKey.empty();
        }
    }
    if (!headerEnded || !haveSaved || !haveStatus || !haveLoose || !haveFull) return false;
    if (v1 && e.status != Status::Found) return false;  // legacy "no lyrics": miss + delete

    if (e.status == Status::Found) {
        const std::wstring body = Utf8ToWide(data.substr(pos));
        size_t start = 0;
        while (start < body.size()) {
            size_t end = body.find(L'\n', start);
            if (end == std::wstring::npos) end = body.size();
            const std::wstring row = body.substr(start, end - start);
            start = end + 1;
            if (row.empty()) continue;

            LyricsLine ll;
            if (e.synced) {
                const size_t tab = row.find(L'\t');
                if (tab == std::wstring::npos || tab == 0) return false;  // corrupt row
                ll.timeMs = static_cast<int64_t>(wcstoll(row.c_str(), nullptr, 10));
                ll.text = row.substr(tab + 1);
            } else {
                ll.timeMs = -1;
                ll.text = row;
            }
            e.lines.push_back(std::move(ll));
        }
        if (e.lines.empty()) return false;
    }

    e.looseHash = HashKey(e.looseKey);
    e.fullHash = HashKey(e.fullKey);
    *out = std::move(e);
    return true;
}

enum class LoadResult { Ok, Missing, Invalid, Expired };

LoadResult LoadEntry(const std::wstring& path, int64_t nowUnix, Entry* out, uint64_t* sizeOut) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return LoadResult::Missing;

    LARGE_INTEGER size = {};
    std::string data;
    bool readOk = GetFileSizeEx(h, &size) && size.QuadPart > 0 &&
                  static_cast<uint64_t>(size.QuadPart) <= kMaxEntryBytes;
    if (readOk) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        readOk = ReadFile(h, data.data(), static_cast<DWORD>(size.QuadPart), &got, nullptr) &&
                 got == static_cast<DWORD>(size.QuadPart);
    }
    CloseHandle(h);
    if (sizeOut) *sizeOut = size.QuadPart > 0 ? static_cast<uint64_t>(size.QuadPart) : 0;

    Entry e;
    if (!readOk || !ParseEntry(data, &e)) return LoadResult::Invalid;
    if (EntryExpired(e, nowUnix)) return LoadResult::Expired;
    *out = std::move(e);
    return LoadResult::Ok;
}

void RemoveEntryFile(const std::wstring& path, uint64_t knownSize) {
    if (DeleteFileW(path.c_str())) {
        CountersAdjust(-1, -static_cast<int64_t>(knownSize));
    }
}

// ---- disk layer ------------------------------------------------------------------
bool LookupDisk(const Keys& k, int64_t nowUnix, Entry* out) {
    const std::wstring& dir = CacheDir(false);
    if (dir.empty()) return false;

    // 1) Exact key: one direct open, no directory scan.
    const std::wstring exactName = FileNameFor(k.looseHash, k.fullHash);
    {
        const std::wstring path = dir + L"\\" + exactName;
        Entry e;
        uint64_t size = 0;
        const LoadResult r = LoadEntry(path, nowUnix, &e, &size);
        if (r == LoadResult::Ok && e.fullKey == k.full) {  // key check also guards hash collisions
            TouchFile(path);
            *out = std::move(e);
            return true;
        }
        if (r == LoadResult::Invalid || r == LoadResult::Expired) {
            RemoveEntryFile(path, size);  // corrupt / wrong version / expired: miss + delete
        }
    }

    // 2) Loose key: same artist|title, duration within tolerance (album may differ).
    wchar_t pattern[40] = {};
    swprintf_s(pattern, L"%016llx-*.lrc", static_cast<unsigned long long>(k.looseHash));
    std::vector<std::wstring> candidates;
    WIN32_FIND_DATAW fd = {};
    HANDLE find = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (_wcsicmp(fd.cFileName, exactName.c_str()) == 0) continue;
            if (candidates.size() < 16) candidates.push_back(dir + L"\\" + fd.cFileName);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }

    bool haveBest = false;
    int bestScore = 1 << 30;
    Entry best;
    std::wstring bestPath;
    for (const std::wstring& path : candidates) {
        Entry e;
        uint64_t size = 0;
        const LoadResult r = LoadEntry(path, nowUnix, &e, &size);
        if (r == LoadResult::Invalid || r == LoadResult::Expired) {
            RemoveEntryFile(path, size);
            continue;
        }
        if (r != LoadResult::Ok) continue;
        if (e.looseKey != k.loose || !DurationCompatible(e.durationSec, k.durationSec)) continue;

        const int delta = (e.durationSec > 0 && k.durationSec > 0) ? std::abs(e.durationSec - k.durationSec) : 0;
        const int score = delta + (e.status != Status::Found ? 1000 : 0);  // prefer real lyrics
        if (score < bestScore) {
            bestScore = score;
            best = std::move(e);
            bestPath = path;
            haveBest = true;
        }
    }
    if (!haveBest) return false;
    TouchFile(bestPath);
    *out = std::move(best);
    return true;
}

// Cleanup bookkeeping (lyrics thread only).
bool g_cleanupDue = false;
bool g_scannedOnce = false;
double g_lastCleanup = -1e9;
uint64_t g_lastCap = 0;

void WriteEntryToDisk(const Entry& e, const std::wstring& title, const std::wstring& artist) {
    const std::wstring& dir = CacheDir(true);
    if (dir.empty()) return;

    const std::wstring path = dir + L"\\" + FileNameFor(e.looseHash, e.fullHash);
    const std::string data = SerializeEntry(e, title, artist);

    WIN32_FILE_ATTRIBUTE_DATA old = {};
    const bool hadOld = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &old) != 0;
    const uint64_t oldSize = hadOld ? ((static_cast<uint64_t>(old.nFileSizeHigh) << 32) | old.nFileSizeLow) : 0;

    if (!WriteFileAtomic(path, data)) {
        Wh_Log(L"Lyrics cache: could not write %s (error %lu).", path.c_str(), GetLastError());
        return;
    }
    if (hadOld) {
        CountersAdjust(0, static_cast<int64_t>(data.size()) - static_cast<int64_t>(oldSize));
    } else {
        CountersAdjust(1, static_cast<int64_t>(data.size()));
    }
    g_cleanupDue = true;
}

// Reads just enough of a file to know its saved-at stamp and status.
bool PeekHeader(const std::wstring& path, int64_t* savedAt, Status* status) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char buf[256];
    DWORD got = 0;
    const BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    if (!ok || got < 24) return false;
    buf[got] = '\0';

    static const char kMagicV1[] = "DIWLC 1\n";
    static const char kMagicV2[] = "DIWLC 2\n";
    const bool v1 = strncmp(buf, kMagicV1, sizeof(kMagicV1) - 1) == 0;
    if (!v1 && strncmp(buf, kMagicV2, sizeof(kMagicV2) - 1) != 0) return false;
    const char* p = buf + sizeof(kMagicV1) - 1;
    if (strncmp(p, "saved=", 6) != 0) return false;
    *savedAt = static_cast<int64_t>(strtoll(p + 6, nullptr, 10));
    const char* nl = strchr(p, '\n');
    if (!nl) return false;
    p = nl + 1;
    if (strncmp(p, "status=found", 12) == 0) {
        *status = Status::Found;
    } else if (strncmp(p, "status=notfound", 15) == 0) {
        *status = Status::NotFound;
    } else if (strncmp(p, "status=instrumental", 19) == 0) {
        *status = Status::Instrumental;
    } else {
        return false;
    }
    if (v1 && *status != Status::Found) return false;  // legacy "no lyrics"
    return *savedAt > 0;
}

// One pass over the folder: sweep stray .tmp files, delete corrupt / wrong-version
// / expired entries, recompute the counters, then evict least-recently-used files
// down to 90% of the cap if it is exceeded. Files that don't end in .lrc are
// foreign and never touched.
void CleanupPass(uint64_t capBytes) {
    struct FileInfo {
        std::wstring path;
        uint64_t size;
        ULONGLONG lastWrite;
    };
    std::vector<FileInfo> files;
    uint64_t totalBytes = 0;
    const int64_t nowUnix = UnixNow();
    const std::wstring& dir = CacheDir(false);

    if (!dir.empty()) {
        WIN32_FIND_DATAW fd = {};
        HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                const std::wstring name = fd.cFileName;
                const std::wstring path = dir + L"\\" + name;
                if (EndsWith(name, L".tmp")) {
                    DeleteFileW(path.c_str());
                    continue;
                }
                if (!EndsWith(name, L".lrc")) continue;

                int64_t savedAt = 0;
                Status status = Status::Found;
                bool drop = !PeekHeader(path, &savedAt, &status);
                if (!drop) {
                    // Misses keep for days, lyrics and instrumentals for months. Lookup
                    // version / source checks happen when an entry is actually read.
                    const int64_t ttl = status == Status::NotFound ? kNotFoundTtlSec : kFoundTtlSec;
                    drop = savedAt > nowUnix + 86400 || (nowUnix - savedAt) > ttl;
                }
                if (drop) {
                    DeleteFileW(path.c_str());
                    continue;
                }

                ULARGE_INTEGER lw = {};
                lw.LowPart = fd.ftLastWriteTime.dwLowDateTime;
                lw.HighPart = fd.ftLastWriteTime.dwHighDateTime;
                const uint64_t size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                files.push_back(FileInfo{path, size, lw.QuadPart});
                totalBytes += size;
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    }

    size_t remaining = files.size();
    if (capBytes > 0 && totalBytes > capBytes) {
        std::sort(files.begin(), files.end(),
                  [](const FileInfo& a, const FileInfo& b) { return a.lastWrite < b.lastWrite; });
        const uint64_t target = capBytes - capBytes / 10;  // evict down to 90% so it doesn't thrash
        for (size_t i = 0; i < files.size() && totalBytes > target; ++i) {
            if (DeleteFileW(files[i].path.c_str())) {
                totalBytes -= files[i].size;
                --remaining;
            }
        }
    }

    g_lyricsCacheFileCount.store(static_cast<int>(remaining), std::memory_order_relaxed);
    g_lyricsCacheBytes.store(totalBytes, std::memory_order_relaxed);
    g_lyricsCacheCountsValid.store(true, std::memory_order_relaxed);
}

void MaybeCleanup(bool diskOn, uint64_t capBytes) noexcept {
    if (!diskOn) return;
    try {
        const double now = NowSeconds();
        const bool capChanged = g_scannedOnce && capBytes != g_lastCap;  // applies a shrunk cap at once
        const bool due = g_cleanupDue && (now - g_lastCleanup) >= kCleanupMinIntervalSec;
        if (!g_scannedOnce || capChanged || due) {
            CleanupPass(capBytes);
            g_scannedOnce = true;
            g_cleanupDue = false;
            g_lastCleanup = now;
            g_lastCap = capBytes;
        }
    } catch (...) {
    }
}

// Deletes every cache file and empties the memory layer. Runs on the lyrics thread
// (the menu only sets a flag). It does not touch g_state.lyrics, so lyrics already
// on screen stay; the track simply isn't cached until it is fetched again.
void ClearAll() noexcept {
    try {
        Memory().clear();
        const std::wstring& dir = CacheDir(false);
        if (!dir.empty()) {
            WIN32_FIND_DATAW fd = {};
            HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
            if (find != INVALID_HANDLE_VALUE) {
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    const std::wstring name = fd.cFileName;
                    if (EndsWith(name, L".lrc") || EndsWith(name, L".tmp")) {
                        DeleteFileW((dir + L"\\" + name).c_str());
                    }
                } while (FindNextFileW(find, &fd));
                FindClose(find);
            }
        }
        g_lyricsCacheFileCount.store(0, std::memory_order_relaxed);
        g_lyricsCacheBytes.store(0, std::memory_order_relaxed);
        g_scannedOnce = false;  // next housekeeping pass rescans for exact numbers
        g_cleanupDue = false;
        Wh_Log(L"Lyrics cache cleared.");
    } catch (...) {
    }
}

// Constructed at the top of every lyrics-loop iteration: the constructor services
// a pending "clear" request, the destructor runs housekeeping once the iteration
// is done (including on `continue`). So the first folder scan happens AFTER the
// first track's lyrics were handled, never in front of them.
class IterationGuard {
   public:
    IterationGuard(bool diskOn, uint64_t capBytes) : diskOn_(diskOn), capBytes_(capBytes) {
        if (g_lyricsCacheClearRequested.exchange(false)) {
            ClearAll();
        }
    }
    ~IterationGuard() { MaybeCleanup(diskOn_, capBytes_); }
    IterationGuard(const IterationGuard&) = delete;
    IterationGuard& operator=(const IterationGuard&) = delete;

   private:
    bool diskOn_;
    uint64_t capBytes_;
};

// ---- public API used by the lyrics thread -------------------------------------------
void FillSnapshot(const Entry& e, const std::wstring& title, const std::wstring& artist,
                  LyricsSnapshot* out) {
    *out = LyricsSnapshot{};
    out->matchedTitle = title;
    out->matchedArtist = artist;
    if (e.status == Status::Found) {
        out->hasData = true;
        out->synced = e.synced;
        out->lines = LyricsLines(std::vector<LyricsLine>(e.lines));
    } else {
        out->notFound = true;
        out->instrumental = (e.status == Status::Instrumental);
    }
}

// True on a hit (memory or disk); *out is then ready to publish: hasData for found
// lyrics, notFound for a cached "no lyrics", fetching never set.
bool Lookup(const Keys& k, bool diskOn, const std::wstring& title, const std::wstring& artist,
            LyricsSnapshot* out) {
    const int64_t nowUnix = UnixNow();

    if (const Entry* m = FindInMemory(k, nowUnix)) {
        FillSnapshot(*m, title, artist, out);
        if (diskOn) {
            // A song replayed from memory must still look "recently used" on disk.
            const std::wstring& dir = CacheDir(false);
            if (!dir.empty()) TouchFile(dir + L"\\" + FileNameFor(m->looseHash, m->fullHash));
        }
        return true;
    }

    if (!diskOn) return false;
    Entry e;
    if (!LookupDisk(k, nowUnix, &e)) return false;
    FillSnapshot(e, title, artist, out);
    PutMemory(std::move(e));
    return true;
}

// Stores a definitive outcome. Memory always; disk only when enabled. Misses are
// stored too (every source answered and none matched), so replaying a podcast or
// an obscure track doesn't hit the network again for kNotFoundTtlSec. Network
// failures never get here: the caller retries those instead.
void Store(const Keys& k, Status status, const LyricsSnapshot& snap, bool diskOn, int sources) {
    Entry e;
    e.status = status;
    e.synced = (status == Status::Found) && snap.synced;
    e.savedAt = UnixNow();
    e.durationSec = k.durationSec;
    e.lookup = kLookupVersion;
    e.sources = sources;
    e.looseKey = k.loose;
    e.fullKey = k.full;
    e.looseHash = k.looseHash;
    e.fullHash = k.fullHash;
    if (status == Status::Found) e.lines = snap.lines.vec();

    if (diskOn) WriteEntryToDisk(e, snap.matchedTitle, snap.matchedArtist);
    PutMemory(std::move(e));
}

}  // namespace LyricsCache

// ── Lyrics lookup helpers ────────────────────────────────────────────────────
// Title cleaning, the "Artist - Song" split and matching live in lyrics_text.h
// (pure, unit-tested). These names are what the rest of this file uses.
std::wstring CleanLyricsTitle(const std::wstring& title) { return LyricsText::CleanTitle(title); }
std::wstring PrimaryLyricsArtist(const std::wstring& artist) { return LyricsText::PrimaryArtist(artist); }
int LyricsTextMatch(const std::wstring& a, const std::wstring& b) { return LyricsText::TextMatch(a, b); }

bool ExtractJsonNumberField(const std::string& json, const char* key, double* out) {
    const std::string k = std::string("\"") + key + "\":";
    size_t pos = json.find(k);
    if (pos == std::string::npos) return false;
    pos += k.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size() || (json[pos] != '-' && (json[pos] < '0' || json[pos] > '9'))) return false;
    *out = strtod(json.c_str() + pos, nullptr);
    return true;
}

// End (one past) of the JSON value starting at `v`: a string, an object or array
// (nesting and strings respected), or a bare number / true / false / null.
size_t JsonValueEnd(const std::string& s, size_t v) {
    if (v >= s.size()) return std::string::npos;
    if (s[v] == '"') {
        bool esc = false;
        for (size_t i = v + 1; i < s.size(); ++i) {
            if (esc) esc = false;
            else if (s[i] == '\\') esc = true;
            else if (s[i] == '"') return i + 1;
        }
        return std::string::npos;
    }
    if (s[v] == '{' || s[v] == '[') {
        int depth = 0;
        bool inString = false, esc = false;
        for (size_t i = v; i < s.size(); ++i) {
            const char c = s[i];
            if (inString) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') {
                if (--depth == 0) return i + 1;
            }
        }
        return std::string::npos;
    }
    size_t i = v;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' &&
           s[i] != '\r' && s[i] != '\n' && s[i] != '\t') {
        ++i;
    }
    return i;
}

// Raw text of `key`'s value in the JSON object `obj`, looking only at the
// object's own members. NetEase nests objects that reuse "id" and "name"
// (album, privilege) ahead of the song's own, so a plain find() picks the wrong one.
bool JsonMemberRaw(const std::string& obj, const char* key, std::string* raw) {
    const size_t keyLen = strlen(key);
    size_t i = obj.find('{');
    if (i == std::string::npos) return false;
    int depth = 0;
    for (; i < obj.size(); ++i) {
        const char c = obj[i];
        if (c == '"') {
            const size_t end = JsonValueEnd(obj, i);  // one past the closing quote
            if (end == std::string::npos) return false;
            if (depth == 1) {
                size_t k = end;
                while (k < obj.size() && (obj[k] == ' ' || obj[k] == '\r' || obj[k] == '\n' || obj[k] == '\t')) ++k;
                if (k < obj.size() && obj[k] == ':' && end - i - 2 == keyLen &&
                    obj.compare(i + 1, keyLen, key) == 0) {
                    size_t v = k + 1;
                    while (v < obj.size() && (obj[v] == ' ' || obj[v] == '\r' || obj[v] == '\n' || obj[v] == '\t')) ++v;
                    const size_t vEnd = JsonValueEnd(obj, v);
                    if (vEnd == std::string::npos || vEnd <= v) return false;
                    *raw = obj.substr(v, vEnd - v);
                    return true;
                }
            }
            i = end - 1;
            continue;
        }
        if (c == '{' || c == '[') {
            ++depth;
        } else if (c == '}' || c == ']') {
            if (--depth <= 0) return false;
        }
    }
    return false;
}

// A raw JSON string value ("...") decoded to text; empty for anything else.
std::wstring JsonRawString(const std::string& raw) {
    if (raw.size() < 2 || raw.front() != '"' || raw.back() != '"') return std::wstring();
    return JsonStringUnescape(raw.substr(1, raw.size() - 2));
}

struct LyricsPick {
    bool found = false;
    bool synced = false;
    bool instrumental = false;  // a source matched the track and said it has no vocals
    int score = -1;
    std::vector<LyricsLine> lines;
};

// Scores every candidate in an LRCLIB response (single object or array) and keeps
// the best one in *best. `trusted` = exact /api/get answer, so title checks are skipped.
void ConsiderLyricsBody(const std::string& body, const std::wstring& wantTitle,
                        const std::wstring& wantArtist, int wantDurationSec, bool trusted,
                        LyricsPick* best) {
    if (body.empty()) return;

    std::vector<std::string> candidates;
    const size_t first = body.find_first_not_of(" \t\r\n");
    if (first != std::string::npos && body[first] == '[') {
        candidates = SplitJsonObjectArray(body);
    } else {
        candidates.push_back(body);
    }

    for (const std::string& c : candidates) {
        std::string nameRaw, artistRaw;
        ExtractJsonStringField(c, "trackName", &nameRaw);
        ExtractJsonStringField(c, "artistName", &artistRaw);
        const std::wstring candTitle = JsonStringUnescape(nameRaw);
        const std::wstring candArtist = JsonStringUnescape(artistRaw);

        int durDiff = -1;
        double candDur = 0.0;
        if (wantDurationSec > 0 && ExtractJsonNumberField(c, "duration", &candDur) && candDur > 0.0) {
            durDiff = std::abs(static_cast<int>(candDur + 0.5) - wantDurationSec);
        }

        // An instrumental entry only counts when it is clearly this recording.
        if (c.find("\"instrumental\":true") != std::string::npos) {
            const bool sameTrack = trusted ||
                (LyricsTextMatch(candTitle, wantTitle) == 2 &&
                 (wantArtist.empty() || LyricsTextMatch(candArtist, wantArtist) > 0) &&
                 durDiff >= 0 && durDiff <= 3);
            if (sameTrack) best->instrumental = true;
            continue;
        }

        std::string syncedRaw, plainRaw;
        const bool hasSynced = ExtractJsonStringField(c, "syncedLyrics", &syncedRaw) && !syncedRaw.empty();
        const bool hasPlain = ExtractJsonStringField(c, "plainLyrics", &plainRaw) && !plainRaw.empty();
        if (!hasSynced && !hasPlain) continue;

        std::vector<LyricsLine> lines;
        bool synced = false;
        if (hasSynced) {
            lines = ParseSyncedLyrics(JsonStringUnescape(syncedRaw));
            synced = !lines.empty();
        }
        if (!synced && hasPlain) {
            lines = ParsePlainLyrics(JsonStringUnescape(plainRaw));
        }
        if (lines.empty()) continue;

        int score = 0;
        if (!trusted) {
            const int titleMatch = LyricsTextMatch(candTitle, wantTitle);
            if (titleMatch == 0) continue;                       // different song
            if (durDiff > 10) continue;                          // different cut: sync would be off
            const int artistMatch = wantArtist.empty() ? 1 : LyricsTextMatch(candArtist, wantArtist);
            if (artistMatch == 0 && !(durDiff >= 0 && durDiff <= 3)) continue;  // maybe a cover
            score += titleMatch * 30 + artistMatch * 20;
        }
        if (durDiff >= 0) score += durDiff <= 2 ? 40 : (durDiff <= 5 ? 25 : 5);
        if (synced) score += 20;

        if (!best->found || score > best->score) {
            best->found = true;
            best->synced = synced;
            best->score = score;
            best->lines = std::move(lines);
        }
    }
}

// GET with one quick retry on a transport failure.
HttpResult LyricsRequest(const wchar_t* host, const std::wstring& path) {
    HttpResult r = HttpGetEx(host, path.c_str(), true, kLyricsUserAgent);
    if (!r.IsDefinitive()) {
        if (WaitForSingleObject(g_stopEvent, 400) != WAIT_TIMEOUT) return r;
        r = HttpGetEx(host, path.c_str(), true, kLyricsUserAgent);
    }
    return r;
}

std::wstring LyricsUrlEncode(const std::wstring& s) { return AsciiToWide(UrlEncodeComponent(s)); }

// Tries exact lookups first, then tolerant searches. Returns true if usable lyrics
// were found. *definitive becomes false when a request failed (timeout / 429 / 5xx),
// meaning "no lyrics" can't be trusted and the caller should retry later.
// `query` is the cleaned title / artist (YouTube "Artist - Song" already split).
bool FetchLyricsFromLrclib(const std::wstring& title, const std::wstring& artist,
                           const std::wstring& album, int durationSec,
                           const LyricsText::Query& query, LyricsPick* pick, bool* definitive) {
    *definitive = true;

    const std::wstring searchTitle = query.title.empty() ? title : query.title;
    const std::wstring searchArtist = query.artist.empty() ? artist : query.artist;
    auto enc = &LyricsUrlEncode;

    auto handle = [&](const HttpResult& r, bool trusted) -> bool {
        if (!r.IsDefinitive()) {
            *definitive = false;
            return false;
        }
        if (r.status == 200) {
            ConsiderLyricsBody(r.body, searchTitle, searchArtist, durationSec, trusted, pick);
        }
        return true;
    };
    // Stop once there are synced lyrics, or the exact lookup said "instrumental".
    auto done = [&]() { return (pick->found && pick->synced) || (!pick->found && pick->instrumental); };

    // 1) exact lookup with the metadata exactly as reported
    {
        std::wstring path = L"/api/get?track_name=" + enc(title) + L"&artist_name=" + enc(artist);
        if (!album.empty()) path += L"&album_name=" + enc(album);
        if (durationSec > 0) path += L"&duration=" + std::to_wstring(durationSec);
        if (!handle(LyricsRequest(L"lrclib.net", path), true)) return pick->found;
    }

    // 2) exact lookup with a cleaned title and the main artist only
    if (!done() && (searchTitle != title || searchArtist != artist)) {
        std::wstring path = L"/api/get?track_name=" + enc(searchTitle) + L"&artist_name=" + enc(searchArtist);
        if (durationSec > 0) path += L"&duration=" + std::to_wstring(durationSec);
        if (!handle(LyricsRequest(L"lrclib.net", path), true)) return pick->found;
    }

    // 3) tolerant free-text search, results scored and filtered
    if (!done()) {
        const std::wstring path = L"/api/search?q=" + enc(searchTitle + L" " + searchArtist);
        if (!handle(LyricsRequest(L"lrclib.net", path), false)) return pick->found;
    }

    // 4) title-only search (artist spelled differently, or wrong artist in metadata)
    if (!done()) {
        const std::wstring path = L"/api/search?track_name=" + enc(searchTitle);
        if (!handle(LyricsRequest(L"lrclib.net", path), false)) return pick->found;
    }

    return pick->found;
}

// ── NetEase fallback (music.163.com) ─────────────────────────────────────────
// Only asked when LRCLIB has no synced lyrics and Modules.LyricsNetEase is on.
// Strict on purpose: the song name must match after cleaning, one of the
// artists must match, and the length must be within 3 s, because a wrong
// match shows confidently wrong lyrics.
constexpr int kNetEaseDurationToleranceSec = 3;
constexpr int64_t kNetEaseCreditWindowMs = 40000;  // credit lines sit at the very top

// NetEase answers HTTP 200 even when it refuses ("code":-460 when it thinks it
// is being scraped), so the JSON code decides whether the answer is real.
bool NetEaseAnswered(const HttpResult& r) {
    if (!r.ok || r.status != 200 || r.body.empty()) return false;
    std::string code;
    return JsonMemberRaw(r.body, "code", &code) && strtol(code.c_str(), nullptr, 10) == 200;
}

bool FetchLyricsFromNetEase(const LyricsText::Query& query, int durationSec, LyricsPick* pick,
                            bool* definitive) {
    *definitive = true;
    if (query.title.empty()) return false;
    const wchar_t* kHost = L"music.163.com";

    // 1) search
    std::string songId;
    {
        std::wstring q = query.title;
        if (!query.artist.empty()) q += L" " + query.artist;
        const HttpResult r =
            LyricsRequest(kHost, L"/api/cloudsearch/pc?s=" + LyricsUrlEncode(q) + L"&type=1&limit=5");
        if (!NetEaseAnswered(r)) {
            *definitive = false;
            return false;
        }
        std::string result, songs;
        if (!JsonMemberRaw(r.body, "result", &result) || !JsonMemberRaw(result, "songs", &songs)) {
            return false;  // answered, nothing found
        }

        int bestDelta = 1 << 30;
        for (const std::string& song : SplitJsonObjectArray(songs)) {
            std::string idRaw, nameRaw, dtRaw, arRaw;
            if (!JsonMemberRaw(song, "id", &idRaw) || !JsonMemberRaw(song, "name", &nameRaw)) continue;
            if (idRaw.empty() || idRaw[0] < '0' || idRaw[0] > '9') continue;

            const std::wstring name = JsonRawString(nameRaw);
            std::wstring cleanName = LyricsText::CleanTitle(name);
            if (cleanName.empty()) cleanName = name;
            if (LyricsText::TextMatch(cleanName, query.title) != 2) continue;

            int artistMatch = query.artist.empty() ? 1 : 0;
            if (JsonMemberRaw(song, "ar", &arRaw)) {
                for (const std::string& a : SplitJsonObjectArray(arRaw)) {
                    std::string arName;
                    if (!JsonMemberRaw(a, "name", &arName)) continue;
                    artistMatch = std::max(artistMatch, LyricsText::TextMatch(JsonRawString(arName), query.artist));
                }
            }
            if (artistMatch == 0) continue;

            int delta = 0;
            const double dtMs = JsonMemberRaw(song, "dt", &dtRaw) ? strtod(dtRaw.c_str(), nullptr) : 0.0;
            if (durationSec > 0) {
                if (dtMs <= 0.0) continue;
                delta = std::abs(static_cast<int>(dtMs / 1000.0 + 0.5) - durationSec);
                if (delta > kNetEaseDurationToleranceSec) continue;
            } else if (artistMatch < 2) {
                continue;  // no length to check against: only an exact artist will do
            }
            if (delta < bestDelta) {
                bestDelta = delta;
                songId = idRaw;
            }
        }
        if (songId.empty()) return false;
    }

    // 2) lyrics for the chosen song
    const HttpResult r = LyricsRequest(kHost, L"/api/song/lyric?id=" + AsciiToWide(songId) + L"&lv=1");
    if (!NetEaseAnswered(r)) {
        *definitive = false;
        return false;
    }
    std::string pure;
    if (JsonMemberRaw(r.body, "pureMusic", &pure) && pure == "true") {
        pick->instrumental = true;
        return false;
    }
    std::string lrc, lyric;
    if (!JsonMemberRaw(r.body, "lrc", &lrc) || !JsonMemberRaw(lrc, "lyric", &lyric)) return false;
    const std::wstring text = JsonRawString(lyric);
    if (text.empty()) return false;

    std::vector<LyricsLine> lines = ParseSyncedLyrics(text);
    bool synced = !lines.empty();
    if (!synced) lines = ParsePlainLyrics(text);
    // Drop "作词 : X" / "Composer: Y" credits at the top; they aren't sung.
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [&](const LyricsLine& l) {
                                   return (!synced || l.timeMs < kNetEaseCreditWindowMs) &&
                                          LyricsText::IsCreditLine(l.text);
                               }),
                lines.end());

    size_t sung = 0, pureMusic = 0;
    for (const LyricsLine& l : lines) {
        if (LyricsText::Trim(l.text).empty()) continue;
        ++sung;
        if (LyricsText::IsPureMusicText(l.text)) ++pureMusic;
    }
    if (sung == 0) return false;
    if (pureMusic == sung) {  // "纯音乐，请欣赏": pure music, no lyrics
        pick->instrumental = true;
        return false;
    }

    pick->found = true;
    pick->synced = synced;
    pick->score = 0;
    pick->lines = std::move(lines);
    return true;
}

// ── Instrumental gaps ────────────────────────────────────────────────────────
// A line stays on screen until the next one starts, which in a long solo means
// the last sung line lingers for half a minute. When the next line is far away,
// the current one fades to the note glyph a few seconds in; after the last line,
// a little sooner. Returns 0 (show the line) .. 1 (show the note).
constexpr int64_t kLyricsGapMinMs = 14000;       // only gaps longer than this
constexpr int64_t kLyricsGapHoldMs = 9000;       // line stays this long first
constexpr int64_t kLyricsLastLineHoldMs = 8000;  // after the final line
constexpr float kLyricsGapFadeMs = 600.0f;

int64_t LyricsGapHoldMs(const LyricsLines& lines, int idx) {
    if (idx < 0 || idx >= static_cast<int>(lines.size())) return -1;
    if (lines[idx].text.empty()) return -1;  // already the note glyph
    if (idx + 1 < static_cast<int>(lines.size())) {
        return (lines[idx + 1].timeMs - lines[idx].timeMs > kLyricsGapMinMs) ? kLyricsGapHoldMs : -1;
    }
    return kLyricsLastLineHoldMs;
}

float LyricsGapNoteMix(const LyricsLines& lines, int idx, int64_t posMs) {
    const int64_t hold = LyricsGapHoldMs(lines, idx);
    if (hold < 0) return 0.0f;
    const float past = static_cast<float>(posMs - lines[idx].timeMs - hold);
    if (past <= 0.0f) return 0.0f;
    return std::min(1.0f, past / kLyricsGapFadeMs);
}

DWORD WINAPI LyricsThreadProc(void*) {
    WaitForSingleObject(g_stopEvent, 4000);

    std::wstring lastTitle;
    std::wstring lastArtist;
    double retryAtSec = 0.0;
    int failCount = 0;

    while (WaitForSingleObject(g_stopEvent, 0) == WAIT_TIMEOUT) {
        bool lyricsEnabled = true;
        bool diskCacheOn = false;
        bool netEaseOn = false;
        uint64_t diskCapBytes = 0;
        {
            std::lock_guard lock(g_settingsMutex);
            lyricsEnabled = (g_settings.lyrics || g_settings.collapsedLyrics ||
                             g_settings.mediaTabLyrics) && g_settings.media;
            diskCacheOn = g_settings.lyricsCache && g_settings.lyricsCacheMaxMB > 0;
            diskCapBytes = static_cast<uint64_t>(std::max(0, g_settings.lyricsCacheMaxMB)) * 1024ull * 1024ull;
            netEaseOn = g_settings.lyricsNetEase;
        }
        LyricsCache::g_currentSources =
            LyricsCache::kSourceLrclib | (netEaseOn ? LyricsCache::kSourceNetEase : 0);
        LyricsCache::IterationGuard cacheGuard(diskCacheOn, diskCapBytes);
        if (!lyricsEnabled) {
            WaitForSingleObject(g_stopEvent, 1000);
            continue;
        }
        const bool lyricsViewVisible =
            (NowSeconds() - g_lastLyricsViewTime.load(std::memory_order_relaxed)) < 3.0;

        bool available = false;
        std::wstring title, artist, album;
        int64_t endTicks = 0;
        auto readMedia = [&]() {
            std::lock_guard lock(g_stateMutex);
            available = g_state.media.available;
            title = g_state.media.title;
            artist = g_state.media.artist;
            album = g_state.media.albumTitle;
            endTicks = g_state.media.endTicks;
        };
        readMedia();

        if (!available || title.empty()) {
            if (!lastTitle.empty()) {
                lastTitle.clear();
                lastArtist.clear();
                retryAtSec = 0.0;
                failCount = 0;
                std::lock_guard lock(g_stateMutex);
                g_state.lyrics = LyricsSnapshot{};
            }
            WaitForSingleObject(g_stopEvent, 1000);
            continue;
        }

        const bool sameTrack = (title == lastTitle && artist == lastArtist);
        if (sameTrack) {
            // Already handled. Only continue if a failed fetch is due for a retry,
            // and only while a lyrics view is on screen.
            if (!lyricsViewVisible || retryAtSec <= 0.0 || NowSeconds() < retryAtSec) {
                WaitForSingleObject(g_stopEvent, 1000);
                continue;
            }
        } else {
            // A new track is fetched right away, whether or not a lyrics view is
            // open, so opening one shows lyrics at once. The cache absorbs repeats.
            failCount = 0;
            // Title / artist / duration don't always update together. Let them settle.
            if (WaitForSingleObject(g_stopEvent, 1500) != WAIT_TIMEOUT) break;
            readMedia();
            if (!available || title.empty()) continue;
        }
        retryAtSec = 0.0;
        lastTitle = title;
        lastArtist = artist;

        const LyricsCache::Keys cacheKeys = LyricsCache::BuildKeys(title, artist, album, endTicks);
        {
            LyricsSnapshot cached;
            if (LyricsCache::Lookup(cacheKeys, diskCacheOn, title, artist, &cached)) {
                Wh_Log(L"Lyrics: cache hit for %s - %s", artist.c_str(), title.c_str());
                std::lock_guard lock(g_stateMutex);
                if (g_state.media.title == title && g_state.media.artist == artist) {
                    g_state.lyrics = std::move(cached);
                }
                continue;
            }
        }

        if (!sameTrack) {
            std::lock_guard lock(g_stateMutex);
            g_state.lyrics = LyricsSnapshot{};
            g_state.lyrics.fetching = true;
            g_state.lyrics.matchedTitle = title;
            g_state.lyrics.matchedArtist = artist;
        }

        const int durationSec = endTicks > 0 ? static_cast<int>(endTicks / 10000000.0 + 0.5) : 0;
        const LyricsText::Query query = LyricsText::PrepareQuery(title, artist);
        Wh_Log(L"Lyrics: requesting %s - %s as %s - %s (attempt %d)", artist.c_str(), title.c_str(),
               query.artist.c_str(), query.title.c_str(), failCount + 1);

        LyricsPick pick;
        bool definitive = true;
        int sources = LyricsCache::kSourceLrclib;
        FetchLyricsFromLrclib(title, artist, album, durationSec, query, &pick, &definitive);

        if (netEaseOn && !(pick.found && pick.synced) && !(pick.instrumental && !pick.found)) {
            LyricsPick ne;
            bool neDefinitive = true;
            FetchLyricsFromNetEase(query, durationSec, &ne, &neDefinitive);
            if (ne.found && (ne.synced || !pick.found)) {
                Wh_Log(L"Lyrics: NetEase had them for %s - %s", artist.c_str(), title.c_str());
                pick = std::move(ne);
            } else if (!pick.found && ne.instrumental) {
                pick.instrumental = true;
            }
            if (neDefinitive) sources |= LyricsCache::kSourceNetEase;
            definitive = definitive && neDefinitive;
        }

        LyricsSnapshot snap;
        snap.matchedTitle = title;
        snap.matchedArtist = artist;

        if (pick.found) {
            snap.hasData = true;
            snap.synced = pick.synced;
            snap.lines = LyricsLines(std::move(pick.lines));
            failCount = 0;
            // Plain lyrics while a source was unreachable: show them, but don't
            // remember them, so a later play can still find synced ones.
            if (pick.synced || definitive) {
                LyricsCache::Store(cacheKeys, LyricsCache::Status::Found, snap, diskCacheOn, sources);
            }
        } else if (pick.instrumental) {
            snap.notFound = true;
            snap.instrumental = true;
            failCount = 0;
            LyricsCache::Store(cacheKeys, LyricsCache::Status::Instrumental, snap, diskCacheOn, sources);
            Wh_Log(L"Lyrics: instrumental: %s - %s", artist.c_str(), title.c_str());
        } else if (definitive) {
            snap.notFound = true;   // every source answered and none had this track
            failCount = 0;
            LyricsCache::Store(cacheKeys, LyricsCache::Status::NotFound, snap, diskCacheOn, sources);
            Wh_Log(L"Lyrics: genuinely no lyrics for %s - %s", artist.c_str(), title.c_str());
        } else {
            // Network / 429 / 5xx: NOT "not found". Back off and retry; never cached.
            ++failCount;
            static const double kBackoff[] = {4.0, 10.0, 30.0, 60.0};
            retryAtSec = NowSeconds() + kBackoff[std::min<size_t>(failCount - 1, 3)];
            snap.fetching = (failCount < 3);
            snap.notFound = (failCount >= 3);
            Wh_Log(L"Lyrics: request FAILED for %s - %s (fail #%d), retrying later.",
                   artist.c_str(), title.c_str(), failCount);
        }

        {
            std::lock_guard lock(g_stateMutex);
            if (g_state.media.title == title && g_state.media.artist == artist) {
                g_state.lyrics = std::move(snap);
            }
        }
        WaitForSingleObject(g_stopEvent, 500);
    }
    return 0;
}
