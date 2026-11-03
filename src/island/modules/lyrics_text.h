// ── Lyrics text helpers ──────────────────────────────────────────────────────
// Pure string functions used to turn what a player reports ("Artist - Song
// (Official Video)" by "ArtistVEVO") into a clean title and artist for the
// lyrics lookups, plus the match and credit-line checks for the results.
//
// No #includes on purpose: island_core.cpp includes this (through
// lyrics_module.inc) inside its anonymous namespace. Whoever includes it must
// already have <algorithm>, <string>, <vector>, <cwchar> and <cwctype>. tests/test_core.cpp
// includes it directly to unit-test it.
#pragma once

namespace LyricsText {

inline std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

inline std::wstring Trim(const std::wstring& s) {
    const size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return std::wstring();
    const size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Lowercase words of letters and digits, joined by single spaces. Punctuation
// and symbols only separate words, so "M/V" -> "m v" and "Ft." -> "ft".
inline std::wstring Normalize(const std::wstring& in) {
    std::wstring out;
    bool space = false;
    for (wchar_t c : in) {
        if (iswalnum(c)) {
            if (space && !out.empty()) out.push_back(L' ');
            space = false;
            out.push_back(static_cast<wchar_t>(towlower(c)));
        } else {
            space = true;
        }
    }
    return out;
}

// Normalize() without the spaces: "Daft Punk" and "DaftPunk" compare equal.
inline std::wstring Compact(const std::wstring& in) {
    std::wstring out = Normalize(in);
    out.erase(std::remove(out.begin(), out.end(), L' '), out.end());
    return out;
}

inline std::vector<std::wstring> Words(const std::wstring& in) {
    std::vector<std::wstring> words;
    const std::wstring n = Normalize(in);
    size_t start = 0;
    while (start < n.size()) {
        size_t end = n.find(L' ', start);
        if (end == std::wstring::npos) end = n.size();
        words.push_back(n.substr(start, end - start));
        start = end + 1;
    }
    return words;
}

// True when a bracketed tag or a " - suffix" is packaging, not part of the
// song's name. Whole words only: "(Mixtape)" and "(Lovestoned)" stay, while
// "(Official Music Video)", "[HD]", "(feat. X)" and "- Remastered 2011" go.
inline bool IsNoiseTag(const std::wstring& text) {
    // Anywhere in the tag.
    static const wchar_t* const kAnywhere[] = {
        L"remaster", L"remastered", L"version", L"edit", L"mix", L"remix", L"official",
        L"video", L"audio", L"lyric", L"lyrics", L"explicit", L"clean", L"deluxe", L"bonus",
        L"mono", L"stereo", L"single", L"radio", L"soundtrack", L"ost", L"anniversary", L"hd",
        L"hq", L"4k", L"mv", L"visualizer", L"visualiser", L"karaoke", L"instrumental"};
    // Only as the tag's first word: "(with X)", "(Live at Wembley)", "(From "Frozen")".
    static const wchar_t* const kLeading[] = {
        L"feat", L"ft", L"featuring", L"with", L"prod", L"live", L"from"};

    const std::vector<std::wstring> words = Words(text);
    if (words.empty()) return false;
    for (const wchar_t* w : kLeading) {
        if (words[0] == w) return true;
    }
    for (size_t i = 0; i < words.size(); ++i) {
        for (const wchar_t* w : kAnywhere) {
            if (words[i] == w) return true;
        }
        // "M/V" splits into "m" "v".
        if (words[i] == L"m" && i + 1 < words.size() && words[i + 1] == L"v") return true;
    }
    return false;
}

// Finds the first " - " style separator (hyphen, en dash or em dash with a
// space either side). Returns npos, or the index with *sepLen set.
inline size_t FindDashSeparator(const std::wstring& s, size_t from, size_t* sepLen) {
    for (size_t i = from; i + 2 < s.size(); ++i) {
        if (s[i] == L' ' && (s[i + 1] == L'-' || s[i + 1] == L'–' || s[i + 1] == L'—') &&
            s[i + 2] == L' ') {
            *sepLen = 3;
            return i;
        }
    }
    return std::wstring::npos;
}

inline size_t FindLastDashSeparator(const std::wstring& s, size_t* sepLen) {
    size_t last = std::wstring::npos;
    size_t pos = 0;
    size_t len = 0;
    for (;;) {
        const size_t at = FindDashSeparator(s, pos, &len);
        if (at == std::wstring::npos) break;
        last = at;
        *sepLen = len;
        pos = at + 1;
    }
    return last;
}

// "Song (feat. X) [Official Video] - Remastered 2011 | Channel" -> "Song"
inline std::wstring CleanTitle(std::wstring title) {
    struct Pair { wchar_t open, close; };
    static const Pair kPairs[] = {
        {L'(', L')'}, {L'[', L']'}, {L'（', L'）'} /* （） */, {L'【', L'】'} /* 【】 */};
    for (size_t i = 0; i < title.size();) {
        wchar_t close = 0;
        for (const Pair& p : kPairs) {
            if (title[i] == p.open) close = p.close;
        }
        if (close) {
            const size_t end = title.find(close, i + 1);
            if (end != std::wstring::npos && IsNoiseTag(title.substr(i + 1, end - i - 1))) {
                size_t from = i;
                while (from > 0 && title[from - 1] == L' ') --from;
                title.erase(from, end - from + 1);
                i = from;
                continue;
            }
        }
        ++i;
    }

    const size_t bar = title.find(L" | ");
    if (bar != std::wstring::npos && bar > 0) title.resize(bar);

    // "Song feat. X" without brackets.
    {
        const std::wstring lower = Lower(title);
        static const wchar_t* const kFeat[] = {L" feat. ", L" feat ", L" ft. ", L" featuring "};
        size_t cut = std::wstring::npos;
        for (const wchar_t* f : kFeat) {
            const size_t at = lower.find(f);
            if (at != std::wstring::npos && at > 0 && at < cut) cut = at;
        }
        if (cut != std::wstring::npos) title.resize(cut);
    }

    for (;;) {
        size_t len = 0;
        const size_t dash = FindLastDashSeparator(title, &len);
        if (dash == std::wstring::npos || dash == 0) break;
        if (!IsNoiseTag(title.substr(dash + len))) break;
        title.resize(dash);
    }

    title = Trim(title);
    // 'Artist - "Song"' style quotes around the whole title.
    if (title.size() >= 2) {
        const wchar_t a = title.front();
        const wchar_t b = title.back();
        if ((a == L'"' && b == L'"') || (a == L'“' && b == L'”') ||
            (a == L'\'' && b == L'\'') || (a == L'「' && b == L'」')) {
            title = Trim(title.substr(1, title.size() - 2));
        }
    }
    return title;
}

inline bool EndsWithLower(const std::wstring& lower, const wchar_t* suffix) {
    const size_t n = wcslen(suffix);
    return lower.size() >= n && lower.compare(lower.size() - n, n, suffix) == 0;
}

// "A, B" / "A feat. B" / "A - Topic" / "AVEVO" -> "A"
inline std::wstring PrimaryArtist(std::wstring artist) {
    artist = Trim(artist);
    std::wstring lower = Lower(artist);
    if (lower.size() > 8 && EndsWithLower(lower, L" - topic")) artist.resize(artist.size() - 8);
    else if (lower.size() > 4 && EndsWithLower(lower, L"vevo")) artist.resize(artist.size() - 4);
    else if (lower.size() > 9 && EndsWithLower(lower, L" official")) artist.resize(artist.size() - 9);
    artist = Trim(artist);
    lower = Lower(artist);

    static const wchar_t* const kSeps[] = {L",", L";", L"/", L" feat", L" ft.", L" ft ",
                                           L" featuring ", L" x ", L" × "};
    size_t cut = std::wstring::npos;
    for (const wchar_t* sep : kSeps) {
        const size_t pos = lower.find(sep);
        if (pos != std::wstring::npos && pos > 0 && pos < cut) cut = pos;
    }
    if (cut != std::wstring::npos) artist.resize(cut);
    return Trim(artist);
}

// The "artist" a video site reports is often the uploading channel.
inline bool ArtistLooksLikeChannel(const std::wstring& artist) {
    const std::wstring lower = Lower(Trim(artist));
    return lower.empty() || EndsWithLower(lower, L" - topic") || EndsWithLower(lower, L"vevo") ||
           EndsWithLower(lower, L"official");
}

// 2 = same, 1 = one contains the other, 0 = unrelated
inline int TextMatch(const std::wstring& a, const std::wstring& b) {
    const std::wstring na = Normalize(a);
    const std::wstring nb = Normalize(b);
    if (na.empty() || nb.empty()) return 0;
    if (na == nb) return 2;
    if (na.find(nb) != std::wstring::npos || nb.find(na) != std::wstring::npos) return 1;
    return 0;
}

struct Query {
    std::wstring title;   // cleaned song name
    std::wstring artist;  // main artist, cleaned
    bool split = false;   // artist came out of an "Artist - Song" title
};

// Turns player metadata into what the lyrics sites should be asked for.
// YouTube and friends report "Artist - Song (Official Video)" as the title and
// the channel ("ArtistVEVO", "Artist - Topic", "Some Uploader") as the artist.
inline Query PrepareQuery(const std::wstring& rawTitle, const std::wstring& rawArtist) {
    Query q;
    std::wstring title = CleanTitle(rawTitle);
    if (title.empty()) title = Trim(rawTitle);
    std::wstring artist = PrimaryArtist(rawArtist);
    if (artist.empty()) artist = Trim(rawArtist);

    size_t len = 0;
    const size_t dash = FindDashSeparator(title, 0, &len);
    if (dash != std::wstring::npos && dash > 0 && dash + len < title.size()) {
        const std::wstring left = Trim(title.substr(0, dash));
        const std::wstring right = Trim(title.substr(dash + len));
        const bool channel = ArtistLooksLikeChannel(rawArtist);
        // Spaces dropped, so the channel "DaftPunkVEVO" still finds "Daft Punk".
        const std::wstring artistKey = Compact(artist);
        const bool artistInTitle = !artistKey.empty() &&
            Compact(title).find(artistKey) != std::wstring::npos;
        const bool leftIsArtist = !artistKey.empty() && Compact(left).find(artistKey) != std::wstring::npos;
        const bool rightIsArtist = !artistKey.empty() && Compact(right).find(artistKey) != std::wstring::npos;
        if (!left.empty() && !right.empty() && (channel || !artistInTitle || leftIsArtist)) {
            // "Song - Artist" (reversed) when only the right side names the artist.
            const bool reversed = rightIsArtist && !leftIsArtist;
            const std::wstring song = CleanTitle(reversed ? left : right);
            const std::wstring who = PrimaryArtist(reversed ? right : left);
            if (!song.empty() && !who.empty()) {
                q.title = song;
                q.artist = who;
                q.split = true;
                return q;
            }
        }
    }
    q.title = title;
    q.artist = artist;
    return q;
}

// Credit lines some sources put at the top of synced lyrics
// ("作词 : X", "Composer: Y"). Only lines with a colon count, so a sung line
// that happens to contain one of these words is kept.
inline bool IsCreditLine(const std::wstring& text) {
    if (text.find(L':') == std::wstring::npos && text.find(L'：') == std::wstring::npos) {
        return false;
    }
    static const wchar_t* const kMarkers[] = {
        L"作词", L"作曲", L"编曲", L"制作人", L"作詞", L"編曲", L"監製", L"监制", L"混音",
        L"母带", L"录音", L"和声", L"出品", L"制作", L"企划", L"统筹",
        L"lyricist", L"composer", L"arranger", L"producer", L"lyrics by", L"composed by",
        L"written by", L"produced by", L"mixed by", L"mastered by"};
    const std::wstring lower = Lower(text);
    for (const wchar_t* m : kMarkers) {
        if (lower.find(m) != std::wstring::npos) return true;
    }
    return false;
}

// NetEase marks instrumentals with a single "纯音乐，请欣赏" ("pure music, enjoy") line.
inline bool IsPureMusicText(const std::wstring& text) {
    const std::wstring t = Trim(text);
    return t.rfind(L"纯音乐", 0) == 0 || t.rfind(L"純音樂", 0) == 0;
}

}  // namespace LyricsText
