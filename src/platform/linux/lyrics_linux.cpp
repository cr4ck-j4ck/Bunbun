// Bunbun — synced lyrics on Linux (see lyrics_linux.h).
#include "features_linux.h"
#include "lyrics_linux.h"

#include <glib.h>
#include <libsoup/soup.h>
#include <sys/stat.h>
#include <utime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../common/json.h"
#include "../../island/modules/lyrics_text.h"  // the title / artist cleaning, shared with Windows
#include "island_api.h"
#include "platform.h"
#include "settings.h"

namespace bb::llyrics {
namespace {

using json::Value;

// The same timing as Windows (island_core.cpp, lyrics_module.inc).
constexpr double kBaseLookaheadSec = 0.49;
constexpr int64_t kGapMinMs = 14000;       // only gaps longer than this
constexpr int64_t kGapHoldMs = 9000;       // the line stays this long first
constexpr int64_t kLastLineHoldMs = 8000;  // after the final line
constexpr float kGapFadeMs = 600.0f;
constexpr int kNetEaseDurationToleranceSec = 3;
constexpr int64_t kNetEaseCreditWindowMs = 40000;

std::function<void()> g_changed;
State g_state;
SoupSession* g_soup = nullptr;
int g_gen = 0;  // drops answers that belong to a track we've moved on from

// Settings
bool g_page = false, g_pill = false, g_tab = false, g_media = true;
bool g_netEase = true, g_diskCache = true;
uint64_t g_cacheCap = 25ull << 20;
int g_offsetMs = 0;

bool Wanted() { return g_media && (g_page || g_pill || g_tab); }
void Changed() { if (g_changed) g_changed(); }

std::string Utf8(const std::wstring& w) { return bb::plat::Utf8(w); }
std::wstring Wide(const std::string& s) { return bb::plat::Wide(s); }

// ── Parsing ─────────────────────────────────────────────────────────────────

// "[00:12.30][01:40.10]chorus": a line can carry several timestamps, and the
// fraction 1-3 digits (.5 = 500 ms). Tags that aren't times ([ar:...]) are skipped.
std::vector<Line> ParseSynced(const std::wstring& text) {
    std::vector<Line> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        std::vector<int64_t> times;
        size_t pos = 0;
        while (pos < line.size() && line[pos] == L'[') {
            const size_t close = line.find(L']', pos);
            if (close == std::wstring::npos) break;
            const std::wstring ts = line.substr(pos + 1, close - pos - 1);
            const size_t colon = ts.find(L':');
            if (colon == std::wstring::npos || colon == 0 || !iswdigit(ts[0])) break;
            wchar_t* after = nullptr;
            const long minutes = wcstol(ts.c_str(), nullptr, 10);
            const long seconds = wcstol(ts.c_str() + colon + 1, &after, 10);
            if (!after || after == ts.c_str() + colon + 1) break;
            int64_t ms = minutes * 60000LL + seconds * 1000LL;
            if (*after == L'.' || *after == L':') {
                int64_t frac = 0;
                int digits = 0;
                for (const wchar_t* d = after + 1; iswdigit(*d) && digits < 3; ++d, ++digits) frac = frac * 10 + (*d - L'0');
                while (digits < 3) { frac *= 10; ++digits; }
                ms += frac;
            }
            times.push_back(ms);
            pos = close + 1;
        }
        if (!times.empty()) {
            std::wstring lyric = line.substr(pos);
            while (!lyric.empty() && lyric.front() == L' ') lyric.erase(lyric.begin());
            for (const int64_t t : times) lines.push_back({t, lyric});
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    // Repeated lines (several timestamps) are listed out of order; play order wins.
    std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.timeMs < b.timeMs; });
    return lines;
}

std::vector<Line> ParsePlain(const std::wstring& text) {
    std::vector<Line> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) lines.push_back({-1, line});
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

// ── Picking a result ────────────────────────────────────────────────────────

struct Pick {
    bool found = false, synced = false;
    bool instrumental = false;  // a source matched the track and said it has no vocals
    int score = -1;
    std::vector<Line> lines;
};

// Scores every candidate in an LRCLIB answer (one object or an array) and keeps
// the best. `trusted` = an exact /api/get answer, so the title checks are skipped.
void Consider(const std::string& body, const std::wstring& wantTitle, const std::wstring& wantArtist, int wantDurationSec,
              bool trusted, Pick* best) {
    Value v;
    if (body.empty() || !json::Parse(body, &v)) return;
    std::vector<const Value*> candidates;
    if (v.isArray()) for (const Value& c : v.arr) candidates.push_back(&c);
    else if (v.isObject()) candidates.push_back(&v);
    for (const Value* c : candidates) {
        if (!c->isObject()) continue;
        const std::wstring candTitle = Wide(c->getString("trackName"));
        const std::wstring candArtist = Wide(c->getString("artistName"));
        int durDiff = -1;
        const double candDur = c->getNumber("duration");
        if (wantDurationSec > 0 && candDur > 0) durDiff = std::abs(static_cast<int>(candDur + 0.5) - wantDurationSec);
        // An instrumental entry only counts when it is clearly this recording.
        if (c->getBool("instrumental")) {
            const bool same = trusted || (LyricsText::TextMatch(candTitle, wantTitle) == 2 &&
                                          (wantArtist.empty() || LyricsText::TextMatch(candArtist, wantArtist) > 0) &&
                                          durDiff >= 0 && durDiff <= 3);
            if (same) best->instrumental = true;
            continue;
        }
        const std::string syncedRaw = c->getString("syncedLyrics"), plainRaw = c->getString("plainLyrics");
        if (syncedRaw.empty() && plainRaw.empty()) continue;
        std::vector<Line> lines;
        bool synced = false;
        if (!syncedRaw.empty()) {
            lines = ParseSynced(Wide(syncedRaw));
            synced = !lines.empty();
        }
        if (!synced && !plainRaw.empty()) lines = ParsePlain(Wide(plainRaw));
        if (lines.empty()) continue;
        int score = 0;
        if (!trusted) {
            const int titleMatch = LyricsText::TextMatch(candTitle, wantTitle);
            if (titleMatch == 0) continue;  // a different song
            if (durDiff > 10) continue;     // a different cut: the sync would be off
            const int artistMatch = wantArtist.empty() ? 1 : LyricsText::TextMatch(candArtist, wantArtist);
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

// ── HTTP (libsoup) ──────────────────────────────────────────────────────────

struct Http {
    bool ok = false;  // the request completed
    int status = 0;
    std::string body;
    // A real answer (found or not), as opposed to a timeout, 429 or 5xx.
    bool Definitive() const { return ok && status != 429 && status < 500; }
};
using Done = std::function<void(const Http&)>;

std::string Encode(const std::wstring& s) {
    char* e = g_uri_escape_string(Utf8(s).c_str(), nullptr, FALSE);
    std::string out = e ? e : "";
    g_free(e);
    return out;
}

void GetOnce(const std::string& url, Done done) {
    SoupMessage* msg = soup_message_new("GET", url.c_str());
    if (!msg) {
        done(Http{});
        return;
    }
    soup_session_send_and_read_async(g_soup, msg, G_PRIORITY_DEFAULT, nullptr, [](GObject* src, GAsyncResult* res, gpointer p) {
        auto* cb = static_cast<Done*>(p);
        GError* err = nullptr;
        GBytes* bytes = soup_session_send_and_read_finish(SOUP_SESSION(src), res, &err);
        Http r;
        if (bytes) {
            SoupMessage* m = soup_session_get_async_result_message(SOUP_SESSION(src), res);
            r.ok = true;
            r.status = m ? static_cast<int>(soup_message_get_status(m)) : 0;
            gsize len = 0;
            const char* data = static_cast<const char*>(g_bytes_get_data(bytes, &len));
            r.body.assign(data ? data : "", len);
            g_bytes_unref(bytes);
        } else {
            Wh_Log(L"lyrics: %ls", Wide(err ? err->message : "request failed").c_str());
        }
        g_clear_error(&err);
        (*cb)(r);
        delete cb;
    }, new Done(std::move(done)));
    g_object_unref(msg);
}

// GET with one quick retry on a failure that isn't an answer, as on Windows.
void Get(const std::string& url, Done done) {
    GetOnce(url, [url, done = std::move(done)](const Http& r) mutable {
        if (r.Definitive()) {
            done(r);
            return;
        }
        auto* d = new std::pair<std::string, Done>(url, std::move(done));
        g_timeout_add(400, [](gpointer p) -> gboolean {
            auto* job = static_cast<std::pair<std::string, Done>*>(p);
            GetOnce(job->first, std::move(job->second));
            delete job;
            return G_SOURCE_REMOVE;
        }, d);
    });
}

// ── Cache ───────────────────────────────────────────────────────────────────
// One file per song in $XDG_CACHE_HOME/Bunbun/lyrics, named by a hash of the
// cleaned title, artist and length. "Not found" is remembered for 3 days (lyrics
// get added), "instrumental" for 30, found lyrics until the cache is full.

enum class Status { Found, NotFound, Instrumental };

struct Entry {
    Status status = Status::Found;
    bool synced = false;
    int64_t savedAt = 0;
    std::vector<Line> lines;
};

std::map<std::string, Entry> g_memory;

std::string CacheDir() {
    const char* xdg = getenv("XDG_CACHE_HOME");
    const std::string base = xdg && xdg[0] == '/' ? xdg : bb::plat::HomeDir() + "/.cache";
    return base + "/Bunbun/lyrics";
}

std::string CacheKey(const std::wstring& title, const std::wstring& artist, int durationSec) {
    const LyricsText::Query q = LyricsText::PrepareQuery(title, artist);
    return Utf8(LyricsText::Normalize(q.title)) + "|" + Utf8(LyricsText::Normalize(q.artist)) + "|" + std::to_string(durationSec / 2);
}

std::string CacheFile(const std::string& key) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (unsigned char c : key) { h ^= c; h *= 1099511628211ull; }
    char name[32];
    snprintf(name, sizeof name, "%016llx.json", static_cast<unsigned long long>(h));
    return CacheDir() + "/" + name;
}

bool Expired(const Entry& e) {
    const int64_t age = static_cast<int64_t>(time(nullptr)) - e.savedAt;
    if (e.status == Status::NotFound) return age > 3 * 86400;
    if (e.status == Status::Instrumental) return age > 30 * 86400;
    return false;
}

bool LookupCache(const std::string& key, Entry* out) {
    if (auto it = g_memory.find(key); it != g_memory.end() && !Expired(it->second)) {
        *out = it->second;
        return true;
    }
    if (!g_diskCache) return false;
    std::string data;
    Value v;
    if (!bb::plat::ReadFile(CacheFile(key), &data) || !json::Parse(data, &v) || v.getString("key") != key) return false;
    Entry e;
    const std::string status = v.getString("status");
    e.status = status == "notfound" ? Status::NotFound : status == "instrumental" ? Status::Instrumental : Status::Found;
    e.synced = v.getBool("synced");
    e.savedAt = static_cast<int64_t>(v.getNumber("savedAt"));
    if (const Value* lines = v.find("lines"); lines && lines->isArray())
        for (const Value& l : lines->arr)
            if (l.isArray() && l.arr.size() == 2) e.lines.push_back({static_cast<int64_t>(l.arr[0].num), Wide(l.arr[1].str)});
    if (Expired(e)) return false;
    utime(CacheFile(key).c_str(), nullptr);  // recently used: kept longest when the cache is trimmed
    g_memory[key] = e;
    *out = std::move(e);
    return true;
}

// Oldest files go first once the folder is over Modules.LyricsCacheMaxMB.
void TrimCache() {
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    uint64_t total = 0;
    for (const auto& f : std::filesystem::directory_iterator(CacheDir(), ec)) {
        if (f.path().extension() != ".json") continue;
        total += f.file_size(ec);
        files.push_back({f.last_write_time(ec), f.path()});
    }
    std::sort(files.begin(), files.end());
    for (const auto& [when, path] : files) {
        if (total <= g_cacheCap) break;
        const uint64_t size = std::filesystem::file_size(path, ec);
        if (std::filesystem::remove(path, ec)) total -= std::min(total, size);
    }
}

void StoreCache(const std::string& key, const Entry& e) {
    g_memory[key] = e;
    if (!g_diskCache) return;
    Value v = Value::Object();
    v["key"] = Value::String(key);
    v["status"] = Value::String(e.status == Status::NotFound ? "notfound" : e.status == Status::Instrumental ? "instrumental" : "found");
    v["synced"] = Value::Bool(e.synced);
    v["savedAt"] = Value::Number(static_cast<double>(e.savedAt));
    Value lines = Value::Array();
    for (const Line& l : e.lines) {
        Value pair = Value::Array();
        pair.push(Value::Number(static_cast<double>(l.timeMs)));
        pair.push(Value::String(Utf8(l.text)));
        lines.push(std::move(pair));
    }
    v["lines"] = std::move(lines);
    bb::plat::MakeDirs(CacheDir());
    bb::plat::WriteFileAtomic(CacheFile(key), json::Serialize(v));
    TrimCache();
}

// ── One lookup ──────────────────────────────────────────────────────────────
// The steps run one after another through callbacks; `gen` stops a lookup whose
// track is no longer playing.

struct Lookup {
    int gen = 0;
    std::wstring title, artist, album;
    int durationSec = 0;
    LyricsText::Query query;
    std::string key;
    Pick pick;
    bool definitive = true;
    int failCount = 0;
};

void Finish(std::shared_ptr<Lookup> job);
void NetEase(std::shared_ptr<Lookup> job);

bool Stale(const Lookup& job) { return job.gen != g_gen; }

// LRCLIB: exact lookups first, then tolerant searches; stops at synced lyrics or
// an exact "instrumental".
void Lrclib(std::shared_ptr<Lookup> job, int step) {
    if (Stale(*job)) return;
    const std::wstring searchTitle = job->query.title.empty() ? job->title : job->query.title;
    const std::wstring searchArtist = job->query.artist.empty() ? job->artist : job->query.artist;
    const bool done = (job->pick.found && job->pick.synced) || (!job->pick.found && job->pick.instrumental);
    std::string url;
    bool trusted = false;
    while (url.empty() && step < 4 && !(done && step > 0)) {
        switch (step) {
            case 0:  // exactly as the player reports it
                url = "https://lrclib.net/api/get?track_name=" + Encode(job->title) + "&artist_name=" + Encode(job->artist);
                if (!job->album.empty()) url += "&album_name=" + Encode(job->album);
                if (job->durationSec > 0) url += "&duration=" + std::to_string(job->durationSec);
                trusted = true;
                break;
            case 1:  // the cleaned title and the main artist
                if (searchTitle != job->title || searchArtist != job->artist) {
                    url = "https://lrclib.net/api/get?track_name=" + Encode(searchTitle) + "&artist_name=" + Encode(searchArtist);
                    if (job->durationSec > 0) url += "&duration=" + std::to_string(job->durationSec);
                    trusted = true;
                }
                break;
            case 2: url = "https://lrclib.net/api/search?q=" + Encode(searchTitle + L" " + searchArtist); break;
            case 3: url = "https://lrclib.net/api/search?track_name=" + Encode(searchTitle); break;
        }
        if (url.empty()) ++step;
    }
    if (url.empty() || (done && step > 0)) {
        NetEase(job);
        return;
    }
    Get(url, [job, step, trusted, searchTitle, searchArtist](const Http& r) {
        if (Stale(*job)) return;
        if (!r.Definitive()) {
            job->definitive = false;
            NetEase(job);
            return;
        }
        if (r.status == 200) Consider(r.body, searchTitle, searchArtist, job->durationSec, trusted, &job->pick);
        Lrclib(job, step + 1);
    });
}

// NetEase answers 200 even when it refuses ("code":-460), so the JSON code decides.
bool NetEaseAnswered(const Http& r, Value* v) {
    return r.ok && r.status == 200 && json::Parse(r.body, v) && v->isObject() && static_cast<int>(v->getNumber("code")) == 200;
}

std::string IdString(const Value& v) {
    if (v.isString()) return v.str;
    char b[32];
    snprintf(b, sizeof b, "%.0f", v.num);
    return b;
}

// Only asked when LRCLIB has no synced lyrics and Modules.LyricsNetEase is on.
// Strict on purpose: the name must match after cleaning, one artist must match
// and the length must be within 3 s, because a wrong match shows confidently
// wrong lyrics.
void NetEase(std::shared_ptr<Lookup> job) {
    if (Stale(*job)) return;
    const Pick& p = job->pick;
    if (!g_netEase || (p.found && p.synced) || (p.instrumental && !p.found) || job->query.title.empty()) {
        Finish(job);
        return;
    }
    std::wstring q = job->query.title;
    if (!job->query.artist.empty()) q += L" " + job->query.artist;
    Get("https://music.163.com/api/cloudsearch/pc?s=" + Encode(q) + "&type=1&limit=5", [job](const Http& r) {
        if (Stale(*job)) return;
        Value v;
        if (!NetEaseAnswered(r, &v)) {
            job->definitive = false;
            Finish(job);
            return;
        }
        const Value* result = v.find("result");
        const Value* songs = result ? result->find("songs") : nullptr;
        std::string songId;
        int bestDelta = 1 << 30;
        if (songs && songs->isArray())
            for (const Value& song : songs->arr) {
                const Value* id = song.find("id");
                if (!id) continue;
                const std::wstring name = Wide(song.getString("name"));
                std::wstring clean = LyricsText::CleanTitle(name);
                if (clean.empty()) clean = name;
                if (LyricsText::TextMatch(clean, job->query.title) != 2) continue;
                int artistMatch = job->query.artist.empty() ? 1 : 0;
                if (const Value* ar = song.find("ar"); ar && ar->isArray())
                    for (const Value& a : ar->arr)
                        artistMatch = std::max(artistMatch, LyricsText::TextMatch(Wide(a.getString("name")), job->query.artist));
                if (artistMatch == 0) continue;
                int delta = 0;
                const double dtMs = song.getNumber("dt");
                if (job->durationSec > 0) {
                    if (dtMs <= 0) continue;
                    delta = std::abs(static_cast<int>(dtMs / 1000.0 + 0.5) - job->durationSec);
                    if (delta > kNetEaseDurationToleranceSec) continue;
                } else if (artistMatch < 2) {
                    continue;  // no length to check against: only an exact artist will do
                }
                if (delta < bestDelta) {
                    bestDelta = delta;
                    songId = IdString(*id);
                }
            }
        if (songId.empty()) {
            Finish(job);
            return;
        }
        Get("https://music.163.com/api/song/lyric?id=" + songId + "&lv=1", [job](const Http& r2) {
            if (Stale(*job)) return;
            Value v2;
            if (!NetEaseAnswered(r2, &v2)) {
                job->definitive = false;
                Finish(job);
                return;
            }
            Pick ne;
            if (v2.getBool("pureMusic")) ne.instrumental = true;
            const Value* lrc = v2.find("lrc");
            const std::wstring text = lrc ? Wide(lrc->getString("lyric")) : std::wstring();
            if (!ne.instrumental && !text.empty()) {
                std::vector<Line> lines = ParseSynced(text);
                const bool synced = !lines.empty();
                if (!synced) lines = ParsePlain(text);
                // Drop "作词 : X" / "Composer: Y" credits at the top; they aren't sung.
                lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const Line& l) {
                                return (!synced || l.timeMs < kNetEaseCreditWindowMs) && LyricsText::IsCreditLine(l.text);
                            }),
                            lines.end());
                size_t sung = 0, pure = 0;
                for (const Line& l : lines) {
                    if (LyricsText::Trim(l.text).empty()) continue;
                    ++sung;
                    if (LyricsText::IsPureMusicText(l.text)) ++pure;
                }
                if (sung > 0 && pure == sung) ne.instrumental = true;  // "纯音乐，请欣赏"
                else if (sung > 0) {
                    ne.found = true;
                    ne.synced = synced;
                    ne.lines = std::move(lines);
                }
            }
            if (ne.found && (ne.synced || !job->pick.found)) {
                Wh_Log(L"lyrics: NetEase had them for %ls", job->title.c_str());
                job->pick = std::move(ne);
            } else if (!job->pick.found && ne.instrumental) {
                job->pick.instrumental = true;
            }
            Finish(job);
        });
    });
}

void Start(std::shared_ptr<Lookup> job);

void Finish(std::shared_ptr<Lookup> job) {
    if (Stale(*job)) return;
    State s;
    s.title = Utf8(job->title);
    s.artist = Utf8(job->artist);
    Pick& p = job->pick;
    Entry e;
    e.savedAt = static_cast<int64_t>(time(nullptr));
    if (p.found) {
        s.hasData = true;
        s.synced = p.synced;
        s.lines = p.lines;
        // Plain lyrics while a source was unreachable: show them, but don't
        // remember them, so a later play can still find synced ones.
        if (p.synced || job->definitive) {
            e.status = Status::Found;
            e.synced = p.synced;
            e.lines = std::move(p.lines);
            StoreCache(job->key, e);
        }
    } else if (p.instrumental) {
        s.notFound = s.instrumental = true;
        e.status = Status::Instrumental;
        StoreCache(job->key, e);
    } else if (job->definitive) {
        s.notFound = true;  // every source answered and none had this track
        e.status = Status::NotFound;
        StoreCache(job->key, e);
    } else {
        // Network trouble, 429 or 5xx: not "not found". Back off and try again.
        ++job->failCount;
        static const int kBackoff[] = {4, 10, 30, 60};
        s.fetching = job->failCount < 3;
        s.notFound = job->failCount >= 3;
        auto* retry = new std::shared_ptr<Lookup>(job);
        g_timeout_add_seconds(kBackoff[std::min(job->failCount - 1, 3)], [](gpointer p) -> gboolean {
            auto* j = static_cast<std::shared_ptr<Lookup>*>(p);
            if (!Stale(**j) && Wanted()) Start(*j);
            delete j;
            return G_SOURCE_REMOVE;
        }, retry);
    }
    g_state = std::move(s);
    Changed();
}

void Start(std::shared_ptr<Lookup> job) {
    job->pick = Pick{};
    job->definitive = true;
    Wh_Log(L"lyrics: looking up %ls - %ls", job->query.artist.c_str(), job->query.title.c_str());
    Lrclib(job, 0);
}

std::string g_trackTitle, g_trackArtist;  // the track the current lookup is for

}  // namespace

void Start(std::function<void()> changed) {
    g_changed = std::move(changed);
    std::string ua = std::string("Bunbun/") + BUNBUN_VERSION_STR;
#ifdef BUNBUN_HOMEPAGE_STR
    ua += std::string(" (+") + BUNBUN_HOMEPAGE_STR + ")";
#endif
    g_soup = soup_session_new_with_options("user-agent", ua.c_str(), "timeout", 15u, nullptr);
}

void LoadSettings() {
    namespace s = bb::settings;
    g_page = s::GetBool(L"Modules.Lyrics");
    g_pill = s::GetBool(L"Bunbun.PillLyrics");
    g_tab = s::GetBool(L"Modules.MediaTabLyrics");
    g_media = s::GetBool(L"Modules.Media");
    g_netEase = s::GetBool(L"Modules.LyricsNetEase");
    g_cacheCap = static_cast<uint64_t>(std::max(0, s::GetInt(L"Modules.LyricsCacheMaxMB"))) << 20;
    g_diskCache = s::GetBool(L"Modules.LyricsCache") && g_cacheCap > 0;
    g_offsetMs = std::clamp(s::GetInt(L"Modules.LyricsOffsetMs"), -3000, 3000);
    if (!Wanted()) {
        ++g_gen;
        g_trackTitle.clear();
        g_trackArtist.clear();
        g_state = State{};
    }
}

void OnTrack(const bb::mpris::Track& t) {
    if (!g_soup || !Wanted()) return;
    if (!t.active || t.title.empty()) {
        if (!g_trackTitle.empty()) {
            ++g_gen;
            g_trackTitle.clear();
            g_trackArtist.clear();
            g_state = State{};
        }
        return;
    }
    if (t.title == g_trackTitle && t.artist == g_trackArtist) return;
    g_trackTitle = t.title;
    g_trackArtist = t.artist;
    const int gen = ++g_gen;
    g_state = State{};
    g_state.title = t.title;
    g_state.artist = t.artist;
    g_state.fetching = true;
    // Title, artist and length don't always arrive together: let them settle.
    g_timeout_add(1500, [](gpointer p) -> gboolean {
        const int gen = GPOINTER_TO_INT(p);
        if (gen != g_gen) return G_SOURCE_REMOVE;
        const bb::mpris::Track now = bb::mpris::Current();
        if (now.title != g_trackTitle || now.artist != g_trackArtist) return G_SOURCE_REMOVE;  // OnTrack follows
        auto job = std::make_shared<Lookup>();
        job->gen = gen;
        job->title = Wide(now.title);
        job->artist = Wide(now.artist);
        job->album = Wide(now.album);
        job->durationSec = now.length > 0 ? static_cast<int>(now.length + 0.5) : 0;
        job->query = LyricsText::PrepareQuery(job->title, job->artist);
        job->key = CacheKey(job->title, job->artist, job->durationSec);
        Entry cached;
        if (LookupCache(job->key, &cached)) {
            State s;
            s.title = now.title;
            s.artist = now.artist;
            s.hasData = cached.status == Status::Found;
            s.synced = cached.synced;
            s.notFound = cached.status != Status::Found;
            s.instrumental = cached.status == Status::Instrumental;
            s.lines = std::move(cached.lines);
            g_state = std::move(s);
            Changed();
            return G_SOURCE_REMOVE;
        }
        Start(job);
        return G_SOURCE_REMOVE;
    }, GINT_TO_POINTER(gen));
    Changed();
}

const State& Current() { return g_state; }

bool Usable(const bb::mpris::Track& t) {
    return g_state.hasData && g_state.synced && !g_state.lines.empty() && g_state.title == t.title && g_state.artist == t.artist;
}

int LineAt(double positionSec, bool playing, int64_t* posMs) {
    double pos = positionSec;
    // Modules.LyricsOffsetBluetoothMs replaces the usual offset while Bluetooth audio
    // plays (headphones lag behind the player).
    const auto& feat = bb::lfeat::Get();
    const int offset = feat.btOffsetMs != 0 && bb::lfeat::BluetoothAudio() ? feat.btOffsetMs : g_offsetMs;
    if (playing) pos += kBaseLookaheadSec + offset / 1000.0;
    *posMs = std::max<int64_t>(0, static_cast<int64_t>(pos * 1000.0));
    const auto& lines = g_state.lines;
    int lo = 0, hi = static_cast<int>(lines.size());
    while (lo < hi) {  // the last line whose time has come
        const int mid = (lo + hi) / 2;
        if (lines[mid].timeMs <= *posMs) lo = mid + 1;
        else hi = mid;
    }
    return lo - 1;
}

// A line stays up until the next one starts, which in a long solo means the last
// sung line lingers for half a minute. When the next line is far away, the line
// fades to the note a few seconds in; after the last line, a little sooner.
float GapNoteMix(int idx, int64_t posMs) {
    const auto& lines = g_state.lines;
    if (idx < 0 || idx >= static_cast<int>(lines.size()) || lines[idx].text.empty()) return 0.0f;
    int64_t hold = kLastLineHoldMs;
    if (idx + 1 < static_cast<int>(lines.size())) {
        if (lines[idx + 1].timeMs - lines[idx].timeMs <= kGapMinMs) return 0.0f;
        hold = kGapHoldMs;
    }
    const float past = static_cast<float>(posMs - lines[idx].timeMs - hold);
    return past <= 0.0f ? 0.0f : std::min(1.0f, past / kGapFadeMs);
}

bool PillOn() { return g_pill && g_media; }
bool PageOn() { return g_page && g_media; }

}  // namespace bb::llyrics
