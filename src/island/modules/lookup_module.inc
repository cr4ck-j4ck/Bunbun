
// ── Quick Lookup ─────────────────────────────────────────────────────────────
// Press the Quick Lookup hotkey to look up whatever text is on the clipboard.
//
//   hotkey (render thread) -> StartQuickLookup()
//        1. read + normalise the clipboard text
//        2. in-memory cache hit?  -> publish the result at once
//        3. otherwise publish a "Looking up..." card and spawn ONE worker thread
//   worker thread          -> LookupText() -> publish the result if still wanted
//
// Privacy: nothing is looked up when the clipboard changes. Text is only sent over
// the network when the hotkey is pressed, only if it is a short word or phrase, and
// it is never written to the log or to disk.
//
// Adding a source later = write one function with the signature
//     Outcome LookupXxx(const std::wstring& query, Result* out)
// and add one line to LookupText().

// Forward declarations for Local AI helper
void BrainWarmup();
bool ComposeActive();  // Bunbun: an answer to an agent is being typed in the box (studio_module.inc)
bool BrainTryAnswer(const std::wstring& question, uint64_t lookupId);

// Wikimedia asks API clients to send a descriptive User-Agent with contact info.
constexpr wchar_t kLookupUserAgent[] =
    BUNBUN_USER_AGENT L" QuickLookup";

// Bumped for every new request AND when the card is dismissed. A worker only
// publishes if its id is still current, so a slow reply can never resurrect a card
// the user already closed. Only changed or compared while holding g_stateMutex.
std::atomic<uint64_t> g_lookupRequestSeq{0};
bool g_lookupHotkeyRegistered = false;

namespace QuickLookup {

constexpr double kPanelIdleSeconds = 25.0;       // panel closes this long after the last key / mouse move
constexpr double kComposeIdleSeconds = 120.0;    // Bunbun: ... while an answer to an agent is being typed
constexpr double kResultSeconds = kPanelIdleSeconds;
constexpr double kMessageSeconds = kPanelIdleSeconds;
constexpr double kCommandSeconds = 1.6;          // Bunbun: a done command's "Opening Spotify ✨" before the island goes home
constexpr size_t kRecentCapacity = 12;
constexpr double kLoadingTimeoutSeconds = 30.0;  // upper bound for "Looking up..."
constexpr size_t kMaxQueryChars = 300;           // Bunbun: room for a question to Bun; dictionary lookups cap at kMaxDictionaryChars
constexpr size_t kMaxBodyChars = 360;            // pre-cap on a definition
constexpr size_t kMaxExtractSentences = 3;
constexpr size_t kMaxExtractChars = 320;
constexpr size_t kCacheCapacity = 40;

struct Result {
    std::wstring title;
    std::wstring subtitle;
    std::wstring body;
    std::wstring example;
    std::wstring source;
    bool command = false;  // Bunbun: Bun carried out a command; confirm briefly, then go home
    bool unknown = false;  // Bunbun: Bun didn't understand; the local AI may take it
    bool confused = false; // Bunbun: no real answer (didn't understand, or a fetch failed): puzzled mascot
    // Bunbun: weather somewhere else (or tomorrow): fetched in the background.
    bool weatherFetch = false;
    std::wstring weatherPlace;
    int weatherDays = 0;
    // Bunbun: encyclopedia or dictionary lookup: fetched in the background.
    bool lookupFetch = false;
    std::wstring lookupTopic;
};

// Failed means "could not get a real answer" (timeout, 429, 5xx, no network).
// Only Found and NotFound are cached, so a flaky connection is never remembered.
enum class Outcome { Found, NotFound, Failed };

// ---- text helpers -------------------------------------------------------------
// Collapses runs of whitespace (including newlines and NBSP) into single spaces.
// Stops once the result is longer than maxChars, so a huge clipboard stays cheap.
std::wstring CollapseWhitespace(const std::wstring& in, size_t maxChars = std::wstring::npos) {
    std::wstring out;
    bool pendingSpace = false;
    for (wchar_t ch : in) {
        if (iswspace(ch) || ch == 0x00A0 || ch == L'\0') {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(L' ');
            pendingSpace = false;
        }
        out.push_back(ch);
        if (out.size() > maxChars) {
            break;
        }
    }
    return out;
}

bool IsEdgePunct(wchar_t c) {
    return c != 0 && wcschr(L".,;:!?\"'()[]{}<>\u201c\u201d\u2018\u2019\u00ab\u00bb", c) != nullptr;
}

// Clipboard text -> lookup query. Too-long text is returned still too long (one
// char over the limit) so the caller can reject it instead of silently truncating.
std::wstring NormalizeQuery(const std::wstring& raw) {
    std::wstring text = CollapseWhitespace(raw, kMaxQueryChars);
    if (text.size() > kMaxQueryChars) {
        return text;
    }
    // "hello," / (hello) / “hello” -> hello
    while (!text.empty() && (IsEdgePunct(text.front()) || text.front() == L' ')) {
        text.erase(text.begin());
    }
    while (!text.empty() && (IsEdgePunct(text.back()) || text.back() == L' ')) {
        text.pop_back();
    }
    return text;
}

// Cuts to maxChars on a word boundary and appends an ellipsis.
std::wstring ClipToChars(std::wstring text, size_t maxChars) {
    if (text.size() <= maxChars) {
        return text;
    }
    text.resize(maxChars);
    const size_t space = text.find_last_of(L' ');
    if (space != std::wstring::npos && space > maxChars / 2) {
        text.resize(space);
    }
    while (!text.empty() && (iswspace(text.back()) || text.back() == L',' ||
                             text.back() == L';' || text.back() == L':' || text.back() == L'-')) {
        text.pop_back();
    }
    if (!text.empty() && IS_HIGH_SURROGATE(text.back())) {
        text.pop_back();
    }
    text += L"\u2026";
    return text;
}

std::wstring TitleCase(std::wstring text) {
    bool wordStart = true;
    for (wchar_t& c : text) {
        if (c == L' ') {
            wordStart = true;
        } else {
            if (wordStart) {
                c = static_cast<wchar_t>(towupper(c));
            }
            wordStart = false;
        }
    }
    return text;
}

bool IsSentenceCloser(wchar_t c) {
    return c == L'"' || c == L'\'' || c == L')' || c == L'\u201d' || c == L'\u2019';
}

// A '.', '!' or '?' ends a sentence only when whitespace and then a capital, digit
// or opening quote follow, and (for '.') the word before it isn't an initial or a
// common abbreviation. Good enough for encyclopedia text, not a full tokenizer.
bool IsSentenceEnd(const std::wstring& t, size_t i) {
    const wchar_t c = t[i];
    if (c != L'.' && c != L'!' && c != L'?') {
        return false;
    }
    size_t j = i + 1;
    while (j < t.size() && IsSentenceCloser(t[j])) {
        ++j;
    }
    if (j >= t.size()) {
        return true;
    }
    if (!iswspace(t[j])) {
        return false;
    }
    while (j < t.size() && iswspace(t[j])) {
        ++j;
    }
    if (j >= t.size()) {
        return true;
    }
    if (!(iswupper(t[j]) || iswdigit(t[j]) || t[j] == L'"' || t[j] == L'\u201c' || t[j] == L'(')) {
        return false;
    }
    if (c != L'.') {
        return true;
    }

    size_t s = i;
    while (s > 0 && iswalpha(t[s - 1])) {
        --s;
    }
    const std::wstring word = ToLowerCopy(t.substr(s, i - s));
    if (word.size() == 1) {
        return false;  // initials: "J. R. R. Tolkien"
    }
    static const wchar_t* const kAbbrev[] = {
        L"mr", L"mrs", L"ms", L"dr", L"prof", L"st", L"jr", L"sr", L"vs", L"inc", L"ltd",
        L"co", L"no", L"ca", L"approx", L"mt", L"gen", L"col", L"lt", L"sgt", L"rev", L"fig", L"vol"};
    for (const wchar_t* abbrev : kAbbrev) {
        if (word == abbrev) {
            return false;
        }
    }
    return true;
}

// First `maxSentences` sentences, but stops early rather than exceed maxChars
// (the first sentence is always kept, clipped if it alone is too long).
std::wstring FirstSentences(const std::wstring& text, size_t maxSentences, size_t maxChars) {
    size_t sentences = 0;
    size_t acceptedEnd = 0;
    for (size_t i = 0; i < text.size() && sentences < maxSentences; ++i) {
        if (!IsSentenceEnd(text, i)) {
            continue;
        }
        size_t end = i + 1;
        while (end < text.size() && IsSentenceCloser(text[end])) {
            ++end;
        }
        if (sentences > 0 && end > maxChars) {
            break;
        }
        acceptedEnd = end;
        ++sentences;
        i = end - 1;
    }
    if (acceptedEnd == 0) {
        acceptedEnd = text.size();  // no sentence boundary found: use it all, clipped
    }
    std::wstring out = text.substr(0, acceptedEnd);
    while (!out.empty() && iswspace(out.back())) {
        out.pop_back();
    }
    return ClipToChars(out, maxChars);
}

// ---- JSON helper (same string-find style as the lyrics code) --------------------
// Returns the balanced {...} object that starts at `open`, string- and escape-aware.
std::string BalancedObjectAt(const std::string& json, size_t open) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = open; i < json.size(); ++i) {
        const char c = json[i];
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
            ++depth;
        } else if (c == '}') {
            if (--depth == 0) {
                return json.substr(open, i - open + 1);
            }
        }
    }
    return std::string();
}

// ---- in-memory cache (session only, MRU at the back) ----------------------------
struct CacheEntry {
    std::wstring key;
    bool found = false;
    Result result;
};

std::mutex g_cacheMutex;

std::vector<CacheEntry>& Cache() {
    static std::vector<CacheEntry> cache;
    return cache;
}

bool CacheLookup(const std::wstring& key, bool* found, Result* out) {
    std::lock_guard lock(g_cacheMutex);
    auto& cache = Cache();
    for (size_t i = cache.size(); i-- > 0;) {
        if (cache[i].key == key) {
            *found = cache[i].found;
            *out = cache[i].result;
            std::rotate(cache.begin() + static_cast<std::ptrdiff_t>(i),
                        cache.begin() + static_cast<std::ptrdiff_t>(i) + 1, cache.end());
            return true;
        }
    }
    return false;
}

void CacheStore(const std::wstring& key, bool found, const Result& result) {
    std::lock_guard lock(g_cacheMutex);
    auto& cache = Cache();
    cache.erase(std::remove_if(cache.begin(), cache.end(),
                               [&](const CacheEntry& e) { return e.key == key; }),
                cache.end());
    CacheEntry entry;
    entry.key = key;
    entry.found = found;
    entry.result = result;
    cache.push_back(std::move(entry));
    while (cache.size() > kCacheCapacity) {
        cache.erase(cache.begin());
    }
}

// ---- source 1: dictionary (single words) ----------------------------------------
// https://api.dictionaryapi.dev/api/v2/entries/en/<word>
// Shape: [{"word":..,"phonetic":..,"phonetics":[{"text":..}],
//          "meanings":[{"partOfSpeech":..,"definitions":[{"definition":..,"example":..}]}]}]
Outcome LookupDictionary(const std::wstring& word, Result* out) {
    const std::wstring path =
        L"/api/v2/entries/en/" + AsciiToWide(UrlEncodeComponent(ToLowerCopy(word)));
    const HttpResult res = HttpGetEx(L"api.dictionaryapi.dev", path.c_str(), true, kLookupUserAgent);
    if (!res.IsDefinitive()) {
        return Outcome::Failed;
    }
    if (res.status != 200) {
        return Outcome::NotFound;
    }

    const std::vector<std::string> entries = SplitJsonObjectArray(res.body);
    if (entries.empty()) {
        return Outcome::NotFound;
    }
    const std::string& entry = entries.front();

    std::string wordRaw, phoneticRaw;
    ExtractJsonStringField(entry, "word", &wordRaw);
    if (!ExtractJsonStringField(entry, "phonetic", &phoneticRaw) || phoneticRaw.empty()) {
        phoneticRaw.clear();
        ExtractJsonStringField(entry, "text", &phoneticRaw);  // first phonetics[].text
    }

    // First meaning's part of speech, then the first definition object after it.
    const size_t meaningsPos = entry.find("\"meanings\":[");
    if (meaningsPos == std::string::npos) {
        return Outcome::NotFound;
    }
    std::string posRaw;
    const size_t posKey = entry.find("\"partOfSpeech\":\"", meaningsPos);
    if (posKey != std::string::npos) {
        ExtractJsonStringField(entry.substr(posKey), "partOfSpeech", &posRaw);
    }
    const size_t defKey =
        entry.find("\"definition\":\"", posKey != std::string::npos ? posKey : meaningsPos);
    if (defKey == std::string::npos) {
        return Outcome::NotFound;
    }

    // Bound the search to this one definition object so a later definition's
    // "example" is never attributed to the first one.
    const size_t defOpen = entry.rfind('{', defKey);
    const std::string defObject = defOpen != std::string::npos ? BalancedObjectAt(entry, defOpen)
                                                               : entry.substr(defKey);
    std::string defRaw, exampleRaw;
    ExtractJsonStringField(defObject, "definition", &defRaw);
    ExtractJsonStringField(defObject, "example", &exampleRaw);

    const std::wstring definition = CollapseWhitespace(JsonStringUnescape(defRaw));
    if (definition.empty()) {
        return Outcome::NotFound;
    }

    Result r;
    r.title = CollapseWhitespace(JsonStringUnescape(wordRaw));
    if (r.title.empty()) {
        r.title = word;
    }
    r.subtitle = CollapseWhitespace(JsonStringUnescape(phoneticRaw));
    const std::wstring pos = CollapseWhitespace(JsonStringUnescape(posRaw));
    if (!pos.empty()) {
        if (!r.subtitle.empty()) {
            r.subtitle += L"  \u00b7  ";
        }
        r.subtitle += pos;
    }
    r.body = ClipToChars(definition, kMaxBodyChars);
    const std::wstring example = CollapseWhitespace(JsonStringUnescape(exampleRaw));
    if (!example.empty()) {
        r.example = L"\u201c" + ClipToChars(example, 200) + L"\u201d";
    }
    r.source = L"Dictionary";
    *out = std::move(r);
    return Outcome::Found;
}

// ---- source 2: Wikipedia summary (phrases, or words the dictionary lacks) ---------
// https://en.wikipedia.org/api/rest_v1/page/summary/<title>
// Titles are case-sensitive after the first letter, so a miss is retried once in
// Title Case ("machine learning" -> "Machine Learning").
Outcome LookupWikipedia(const std::wstring& query, Result* out) {
    const std::wstring candidates[2] = {query, TitleCase(query)};
    Outcome outcome = Outcome::NotFound;

    for (int i = 0; i < 2; ++i) {
        if (i == 1 && candidates[1] == candidates[0]) {
            break;
        }
        std::wstring title = candidates[i];
        std::replace(title.begin(), title.end(), L' ', L'_');
        const std::wstring path =
            L"/api/rest_v1/page/summary/" + AsciiToWide(UrlEncodeComponent(title));

        const HttpResult res = HttpGetEx(L"en.wikipedia.org", path.c_str(), true, kLookupUserAgent);
        if (!res.IsDefinitive()) {
            return Outcome::Failed;
        }
        if (res.status != 200) {
            continue;  // 404: try the next spelling
        }

        std::string extractRaw, titleRaw, descRaw;
        if (!ExtractJsonStringField(res.body, "extract", &extractRaw) || extractRaw.empty()) {
            continue;
        }
        ExtractJsonStringField(res.body, "title", &titleRaw);
        ExtractJsonStringField(res.body, "description", &descRaw);

        const std::wstring extract = CollapseWhitespace(JsonStringUnescape(extractRaw));
        if (extract.empty()) {
            continue;
        }

        Result r;
        r.title = CollapseWhitespace(JsonStringUnescape(titleRaw));
        if (r.title.empty()) {
            r.title = query;
        }
        r.subtitle = CollapseWhitespace(JsonStringUnescape(descRaw));
        r.body = FirstSentences(extract, kMaxExtractSentences, kMaxExtractChars);
        r.source = L"Wikipedia";
        *out = std::move(r);
        return Outcome::Found;
    }
    return outcome;
}

// ---- source 3: Wiktionary (primary dictionary) --------------------------------------
// https://en.wiktionary.org/api/rest_v1/page/definition/<word>
// Shape: {"en":[{"partOfSpeech":"Noun","language":"English","definitions":[
//          {"definition":"<html>","parsedExamples":[{"example":"<html>"}]}, ...]}, ...],
//         "fr":[...], ...}
// Definitions come back as HTML, so tags are stripped and entities decoded.

// Removes <tags>, decodes common HTML entities, collapses whitespace.
std::wstring StripHtml(const std::wstring& in) {
    std::wstring noTags;
    noTags.reserve(in.size());
    bool inTag = false;
    for (wchar_t c : in) {
        if (c == L'<') {
            inTag = true;
            continue;
        }
        if (c == L'>' && inTag) {
            inTag = false;
            continue;
        }
        if (!inTag) {
            noTags.push_back(c);
        }
    }

    std::wstring decoded;
    decoded.reserve(noTags.size());
    for (size_t i = 0; i < noTags.size(); ++i) {
        if (noTags[i] == L'&') {
            const size_t semi = noTags.find(L';', i);
            if (semi != std::wstring::npos && semi - i <= 9) {
                const std::wstring ent = noTags.substr(i + 1, semi - i - 1);
                wchar_t rep = 0;
                if (ent == L"amp") rep = L'&';
                else if (ent == L"lt") rep = L'<';
                else if (ent == L"gt") rep = L'>';
                else if (ent == L"quot") rep = L'"';
                else if (ent == L"apos") rep = L'\'';
                else if (ent == L"nbsp") rep = L' ';
                else if (ent == L"bull") rep = L'\u2022';
                else if (ent == L"hellip") rep = L'\u2026';
                else if (ent == L"mdash") rep = L'\u2014';
                else if (ent == L"ndash") rep = L'\u2013';
                else if (ent.size() > 1 && ent[0] == L'#') {
                    const unsigned long code =
                        (ent[1] == L'x' || ent[1] == L'X')
                            ? wcstoul(ent.c_str() + 2, nullptr, 16)
                            : wcstoul(ent.c_str() + 1, nullptr, 10);
                    if (code > 0 && code < 0x10000 && !(code >= 0xD800 && code <= 0xDFFF)) {
                        rep = static_cast<wchar_t>(code);
                    }
                }
                if (rep) {
                    decoded.push_back(rep);
                    i = semi;
                    continue;
                }
            }
        }
        decoded.push_back(noTags[i]);
    }
    return CollapseWhitespace(decoded);
}

// Reads the "en" section of a Wiktionary definition response.
bool ParseWiktionaryEnglish(const std::string& body, const std::wstring& title, Result* out) {
    const size_t enKey = body.find("\"en\":[");
    if (enKey == std::string::npos) {
        return false;  // the word exists, but only in other languages
    }

    auto skipSeparators = [](const std::string& s, size_t p) {
        while (p < s.size() && (s[p] == ' ' || s[p] == ',' || s[p] == '\n' ||
                                s[p] == '\r' || s[p] == '\t')) {
            ++p;
        }
        return p;
    };

    std::vector<std::wstring> partsOfSpeech;
    std::vector<std::wstring> definitions;
    std::wstring example;

    size_t p = enKey + 6;  // just past  "en":[
    while (p < body.size()) {
        p = skipSeparators(body, p);
        if (p >= body.size() || body[p] != '{') {
            break;
        }
        const std::string block = BalancedObjectAt(body, p);
        if (block.empty()) {
            break;
        }
        p += block.size();

        std::string posRaw;
        ExtractJsonStringField(block, "partOfSpeech", &posRaw);
        const std::wstring pos = ToLowerCopy(CollapseWhitespace(JsonStringUnescape(posRaw)));
        if (!pos.empty() && partsOfSpeech.size() < 3 &&
            std::find(partsOfSpeech.begin(), partsOfSpeech.end(), pos) == partsOfSpeech.end()) {
            partsOfSpeech.push_back(pos);
        }

        // Definitions are taken from the first part of speech only.
        if (!definitions.empty()) {
            continue;
        }
        const size_t defsKey = block.find("\"definitions\":[");
        if (defsKey == std::string::npos) {
            continue;
        }
        size_t q = defsKey + 15;  // just past  "definitions":[
        while (q < block.size() && definitions.size() < 3) {
            q = skipSeparators(block, q);
            if (q >= block.size() || block[q] != '{') {
                break;
            }
            const std::string defObj = BalancedObjectAt(block, q);
            if (defObj.empty()) {
                break;
            }
            q += defObj.size();

            std::string defRaw;
            if (!ExtractJsonStringField(defObj, "definition", &defRaw)) {
                continue;
            }
            const std::wstring text = StripHtml(JsonStringUnescape(defRaw));
            if (text.empty()) {
                continue;
            }
            definitions.push_back(text);

            // Only the first definition's example is shown.
            if (definitions.size() == 1) {
                std::string exRaw;
                if (ExtractJsonStringField(defObj, "example", &exRaw)) {
                    example = StripHtml(JsonStringUnescape(exRaw));
                }
            }
        }
    }

    if (definitions.empty()) {
        return false;
    }

    Result r;
    r.title = title;
    for (size_t i = 0; i < partsOfSpeech.size(); ++i) {
        if (i > 0) {
            r.subtitle += L"  \u00b7  ";
        }
        r.subtitle += partsOfSpeech[i];
    }

    std::wstring bodyText;
    for (size_t i = 0; i < definitions.size(); ++i) {
        if (i > 0) {
            bodyText += L"\n";
        }
        if (definitions.size() > 1) {
            bodyText += std::to_wstring(i + 1) + L". ";
        }
        bodyText += ClipToChars(definitions[i], 170);
    }
    r.body = ClipToChars(bodyText, kMaxBodyChars);

    if (!example.empty()) {
        r.example = L"\u201c" + ClipToChars(example, 200) + L"\u201d";
    }
    r.source = L"Wiktionary";
    *out = std::move(r);
    return true;
}

// Titles are case-sensitive, so lowercase is tried first, then the text as typed.
Outcome LookupWiktionary(const std::wstring& word, Result* out) {
    const std::wstring lower = ToLowerCopy(word);
    const std::wstring candidates[2] = {lower, word};

    for (int i = 0; i < 2; ++i) {
        if (i == 1 && candidates[1] == candidates[0]) {
            break;
        }
        const std::wstring path = L"/api/rest_v1/page/definition/" +
                                  AsciiToWide(UrlEncodeComponent(candidates[i]));
        const HttpResult res = HttpGetEx(L"en.wiktionary.org", path.c_str(), true, kLookupUserAgent);
        if (!res.IsDefinitive()) {
            return Outcome::Failed;  // timeout / 429 / 5xx: never cached as "not found"
        }
        if (res.status != 200) {
            continue;  // 404: try the next spelling
        }
        if (ParseWiktionaryEnglish(res.body, candidates[i], out)) {
            return Outcome::Found;
        }
    }
    return Outcome::NotFound;
}

// ---- source 4: Wikipedia search API (finds the right article even for loose queries)
Outcome LookupWikipediaSearch(const std::wstring& query, Result* out) {
    const std::wstring path = L"/w/api.php?action=query&list=search&srsearch=" +
                              AsciiToWide(UrlEncodeComponent(query)) + L"&format=json&utf8=1";
    const HttpResult res = HttpGetEx(L"en.wikipedia.org", path.c_str(), true, kLookupUserAgent);
    if (!res.IsDefinitive()) {
        return Outcome::Failed;
    }
    if (res.status != 200) {
        return Outcome::NotFound;
    }

    bb::json::Value v;
    if (!bb::json::Parse(res.body, &v) || !v.isObject()) {
        return Outcome::NotFound;
    }
    const bb::json::Value* qObj = v.find("query");
    if (!qObj || !qObj->isObject()) {
        return Outcome::NotFound;
    }
    const bb::json::Value* searchArr = qObj->find("search");
    if (!searchArr || !searchArr->isArray() || searchArr->arr.empty()) {
        return Outcome::NotFound;
    }

    // Try summary on top matching article title (1 request max to stay snappy)
    if (!searchArr->arr.empty()) {
        const std::string titleUtf8 = searchArr->arr[0].getString("title");
        if (!titleUtf8.empty()) {
            const std::wstring title = bb::todos::FromUtf8(titleUtf8);
            if (LookupWikipedia(title, out) == Outcome::Found) {
                return Outcome::Found;
            }
        }
    }

    // If summary wasn't found, use the search snippet directly
    const auto& topItem = searchArr->arr.front();
    const std::string titleUtf8 = topItem.getString("title");
    const std::string snipUtf8 = topItem.getString("snippet");
    if (titleUtf8.empty() || snipUtf8.empty()) {
        return Outcome::NotFound;
    }

    Result r;
    r.title = bb::todos::FromUtf8(titleUtf8);
    r.subtitle = L"Wikipedia";
    r.body = StripHtml(bb::todos::FromUtf8(snipUtf8));
    r.source = L"Wikipedia";
    *out = std::move(r);
    return Outcome::Found;
}

// ---- source 5: Web Search (DuckDuckGo first, Bing as a fallback) ---------------------
// Bunbun: scripted Bing alone often returns junk for a plain question, which the local AI then
// "answered" from. Each hit is now checked against the question's own words, so an off-topic page
// is dropped instead of becoming the evidence.
struct WebHit { std::wstring title, snippet; };

bool QueryWordsHit(const std::wstring& query, const WebHit& h) {
    static const wchar_t* const kStop[] = {L"the", L"and", L"for", L"how", L"what", L"why", L"when", L"where", L"who",
        L"can", L"you", L"are", L"does", L"did", L"was", L"with", L"from", L"that", L"this", L"about", L"tell", L"please",
        L"my", L"your", L"set", L"get", L"between"};
    const std::wstring hay = ToLowerCopy(h.title + L" " + h.snippet);
    int words = 0, hits = 0;
    std::wstring w;
    auto flush = [&] {
        if (w.size() > 2 && std::none_of(std::begin(kStop), std::end(kStop), [&](const wchar_t* t) { return w == t; })) {
            ++words;
            if (hay.find(w) != std::wstring::npos) ++hits;
        }
        w.clear();
    };
    for (wchar_t c : ToLowerCopy(query)) {
        if (iswalnum(c)) w += c; else flush();
    }
    flush();
    return words == 0 || hits * 5 >= words * 3 && (words < 3 || hits >= 2);   // most of the question's words must appear
}

std::vector<WebHit> ParseDuckDuckGoHtml(const std::string& html, size_t limit) {
    std::vector<WebHit> out;
    size_t pos = 0;
    while (out.size() < limit && (pos = html.find("class=\"result__a\"", pos)) != std::string::npos) {
        const size_t tStart = html.find('>', pos);
        const size_t tEnd = tStart == std::string::npos ? tStart : html.find("</a>", tStart);
        if (tEnd == std::string::npos) break;
        const size_t next = html.find("class=\"result__a\"", tEnd);
        const size_t sOpen = html.find("class=\"result__snippet\"", tEnd);
        pos = tEnd;
        if (sOpen == std::string::npos || (next != std::string::npos && sOpen > next)) continue;
        const size_t sStart = html.find('>', sOpen);
        const size_t sEnd = sStart == std::string::npos ? sStart : html.find("</a>", sStart);
        if (sEnd == std::string::npos) continue;
        WebHit h{StripHtml(bb::todos::FromUtf8(html.substr(tStart + 1, tEnd - tStart - 1))),
                 StripHtml(bb::todos::FromUtf8(html.substr(sStart + 1, sEnd - sStart - 1)))};
        if (!h.title.empty() && h.snippet.size() > 15) out.push_back(std::move(h));
    }
    return out;
}

std::vector<WebHit> ParseBingHtml(const std::string& html, size_t limit) {
    std::vector<WebHit> out;
    size_t pos = 0;
    while (out.size() < limit && (pos = html.find("<li class=\"b_algo\"", pos)) != std::string::npos) {
        const size_t end = html.find("</li>", pos);
        if (end == std::string::npos) break;
        const std::string block = html.substr(pos, end - pos);
        pos = end + 5;
        const size_t h2 = block.find("<h2");
        const size_t aOpen = h2 == std::string::npos ? h2 : block.find('>', h2);
        const size_t h2End = aOpen == std::string::npos ? aOpen : block.find("</h2>", aOpen);
        if (h2End == std::string::npos) continue;
        const size_t pOpen = block.find("<p", h2End);
        const size_t pText = pOpen == std::string::npos ? pOpen : block.find('>', pOpen);
        const size_t pEnd = pText == std::string::npos ? pText : block.find("</p>", pText);
        if (pEnd == std::string::npos) continue;
        WebHit h{StripHtml(bb::todos::FromUtf8(block.substr(aOpen + 1, h2End - aOpen - 1))),
                 StripHtml(bb::todos::FromUtf8(block.substr(pText + 1, pEnd - pText - 1)))};
        if (!h.title.empty() && h.snippet.size() > 15) out.push_back(std::move(h));
    }
    return out;
}

// Up to `limit` relevant web results. DuckDuckGo answers a burst of requests with a 202 challenge page
// (no results), so that case falls through to Bing.
std::vector<WebHit> WebSearchHits(const std::wstring& query, size_t limit, bool* reachable = nullptr) {
    constexpr wchar_t kBrowserUserAgent[] =
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36";
    constexpr wchar_t kHeaders[] = L"Accept-Language: en-US,en;q=0.9\r\n";
    const std::wstring q = AsciiToWide(UrlEncodeComponent(query));
    bool reached = false;
    std::vector<WebHit> hits;
    auto keep = [&](std::vector<WebHit> found) {
        for (auto& h : found) {
            if (hits.size() < limit && QueryWordsHit(query, h)) hits.push_back(std::move(h));
        }
    };

    const HttpResult ddg = HttpGetEx(L"html.duckduckgo.com", (L"/html/?q=" + q).c_str(), true, kBrowserUserAgent, kHeaders);
    if (ddg.ok) reached = true;
    if (ddg.ok && ddg.status == 200) keep(ParseDuckDuckGoHtml(ddg.body, limit + 3));
    if (hits.size() < limit) {
        const HttpResult bing = HttpGetEx(L"www.bing.com", (L"/search?q=" + q + L"&setlang=en&cc=US").c_str(), true,
                                          kBrowserUserAgent, kHeaders);
        if (bing.ok) reached = true;
        if (bing.ok && bing.status == 200) keep(ParseBingHtml(bing.body, limit + 3));
    }
    if (reachable) *reachable = reached;
    return hits;
}

Outcome LookupWebSearch(const std::wstring& query, Result* out) {
    bool reached = false;
    const std::vector<WebHit> hits = WebSearchHits(query, 1, &reached);
    if (hits.empty()) return reached ? Outcome::NotFound : Outcome::Failed;
    Result r;
    r.title = ClipToChars(hits[0].title, 80);
    r.subtitle = L"Web Search";
    r.body = ClipToChars(hits[0].snippet, kMaxBodyChars);
    r.source = L"Web Search";
    *out = std::move(r);
    return Outcome::Found;
}

std::wstring CleanSearchQuery(const std::wstring& raw) {
    std::wstring q = ToLowerCopy(raw);
    static const wchar_t* const kPrefixes[] = {
        L"who was the main star in ", L"who was the star in ", L"who was the main ", L"who was the ",
        L"who is the main star in ", L"who is the star in ", L"who is the ", L"who are the ",
        L"who was ", L"who is ", L"who were ",
        L"what is the date of ", L"what is the date for ", L"what is the ", L"what was the ",
        L"what are the ", L"when is the ", L"when was the ", L"when is ", L"when was ",
        L"where is the ", L"where is ", L"where was ",
        L"tell me about the ", L"tell me about ",
        L"can you tell me about ", L"can you tell me "
    };
    for (const wchar_t* p : kPrefixes) {
        const size_t n = wcslen(p);
        if (q.size() > n && q.compare(0, n, p) == 0) {
            std::wstring res = raw.substr(n);
            while (!res.empty() && (res.back() == L'?' || res.back() == L'.' || res.back() == L'!')) res.pop_back();
            return res;
        }
    }
    std::wstring res = raw;
    while (!res.empty() && (res.back() == L'?' || res.back() == L'.' || res.back() == L'!')) res.pop_back();
    return res;
}

// Fetches live search evidence for grounding the local AI: the top relevant web results first,
// Wikipedia only as a supplement. Empty when nothing relevant was found (the caller then says so
// instead of answering from the model's memory).
std::wstring FetchLiveSearchEvidence(const std::wstring& query) {
    const std::wstring clean = CleanSearchQuery(query);

    auto futWiki = std::async(std::launch::async, [clean, query]() -> std::wstring {
        Result rWiki;
        if ((clean != query && LookupWikipediaSearch(clean, &rWiki) == Outcome::Found) ||
            LookupWikipediaSearch(query, &rWiki) == Outcome::Found) {
            if (QueryWordsHit(clean, WebHit{rWiki.title, rWiki.body})) {
                return L"- Wikipedia (" + rWiki.title + L"): " + rWiki.body + L"\n";
            }
        }
        return L"";
    });

    std::wstring evidence;
    std::vector<WebHit> hits = WebSearchHits(clean, 3);
    if (hits.empty() && clean != query) hits = WebSearchHits(query, 3);
    for (const WebHit& h : hits) {
        evidence += L"- " + ClipToChars(h.title, 80) + L": " + ClipToChars(h.snippet, 300) + L"\n";
    }
    evidence += futWiki.get();
    return evidence;
}

// ---- router -----------------------------------------------------------------------
// Single words:  Wiktionary -> dictionaryapi.dev (backup) -> Wikipedia -> Web Search
// Phrases:       Wikipedia  -> Wikipedia Search -> Web Search -> Wiktionary
Outcome LookupText(const std::wstring& query, Result* out) {
    const bool singleWord = query.find(L' ') == std::wstring::npos;

    bool anyFailed = false;
    auto attempt = [&](Outcome outcome) -> bool {
        if (outcome == Outcome::Found) return true;
        if (outcome == Outcome::Failed) anyFailed = true;
        return false;
    };

    if (singleWord) {
        if (attempt(LookupWiktionary(query, out))) return Outcome::Found;
        if (attempt(LookupDictionary(query, out))) return Outcome::Found;
        if (attempt(LookupWikipedia(query, out))) return Outcome::Found;
        if (attempt(LookupWikipediaSearch(query, out))) return Outcome::Found;
        if (attempt(LookupWebSearch(query, out))) return Outcome::Found;
    } else {
        if (attempt(LookupWikipedia(query, out))) return Outcome::Found;
        if (attempt(LookupWikipediaSearch(query, out))) return Outcome::Found;
        if (attempt(LookupWebSearch(query, out))) return Outcome::Found;
        if (attempt(LookupWiktionary(query, out))) return Outcome::Found;
    }

    // "Nothing there" is only cacheable if every source gave a definite answer.
    return anyFailed ? Outcome::Failed : Outcome::NotFound;
}

// ---- Ask Bun (Bunbun's local assistant) --------------------------------------
//
// The search box doubles as Bun's command line: open apps and settings pages,
// control media and volume, start timers, search the web, answer quick
// questions about the PC (see src/assistant). Everything runs locally; no AI
// service or account is involved. "define <word>" / "wiki <topic>" keep the
// original dictionary and Wikipedia lookups.

constexpr size_t kMaxDictionaryChars = 80;

// Strips a "define " / "wiki " / "lookup " prefix. Returns true when the query is for Bun.
bool RouteToBun(std::wstring* query) {
    for (const wchar_t* prefix : {L"define ", L"wiki ", L"dict ", L"lookup ", L"wikipedia "}) {
        const size_t n = wcslen(prefix);
        if (query->size() > n && _wcsnicmp(query->c_str(), prefix, n) == 0) {
            query->erase(0, n);
            return false;
        }
    }
    return true;
}

// ---- publishing ---------------------------------------------------------------------
void FillSnapshot(LookupSnapshot& lk, const std::wstring& query, bool found, const Result* r,
                  double now) {
    lk = LookupSnapshot{};
    lk.active = true;
    lk.query = query;
    if (found && r) {
        lk.status = LookupStatus::Found;
        lk.title = r->title;
        lk.subtitle = r->subtitle;
        lk.body = r->body;
        lk.example = r->example;
        lk.source = r->source;
        lk.confused = r->confused || r->unknown;  // Bunbun
        lk.expiresAt = now + kResultSeconds;
    } else {
        lk.status = LookupStatus::NoResult;
        lk.expiresAt = now + kMessageSeconds;
    }
}

// Moves `query` to the front of the recent list. Caller must hold g_stateMutex.
void PushRecentLocked(SharedState& st, const std::wstring& query, const Result& r) {
    LookupRecent entry;
    entry.query = query;
    entry.title = r.title;
    entry.source = r.source;

    std::wstring detail = r.body;
    std::replace(detail.begin(), detail.end(), L'\n', L' ');
    if (detail.rfind(L"1. ", 0) == 0) {
        detail.erase(0, 3);
    }
    entry.detail = ClipToChars(CollapseWhitespace(detail), 60);

    auto& list = st.lookupRecent;
    const std::wstring key = ToLowerCopy(query);
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const LookupRecent& e) { return ToLowerCopy(e.query) == key; }),
               list.end());
    list.insert(list.begin(), std::move(entry));
    if (list.size() > kRecentCapacity) {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(kRecentCapacity), list.end());
    }
}

// Worker thread -> island. Dropped if the request was superseded or dismissed, or
// if its "Looking up..." card has already timed out.
void ApplyResult(uint64_t id, const std::wstring& query, bool found, const Result& r) {
    if (!g_running.load()) {
        return;
    }
    const double now = NowSeconds();
    {
        std::lock_guard lock(g_stateMutex);
        LookupSnapshot& lk = g_state.lookup;
        if (g_lookupRequestSeq.load() != id || !lk.active || now >= lk.expiresAt) {
            return;
        }
        FillSnapshot(lk, query, found, &r, now);
        if (found) {
            PushRecentLocked(g_state, query, r);
        }
    }
    g_layoutDirty = true;  // repaint; the springs resize the island to the new card
}

}  // namespace QuickLookup

uint64_t PublishNewLookup(const LookupSnapshot& snap, bool nudge = true) {
    uint64_t id;
    {
        std::lock_guard lock(g_stateMutex);
        g_state.lookup = snap;
        id = ++g_lookupRequestSeq;
    }
    g_layoutDirty = true;
    if (nudge) {
        TriggerNudge();  // typing and searching stay quiet; only opening the panel bounces
    }
    return id;
}

// ── Quick Lookup panel: session + input ──────────────────────────────────────
// All of this runs on the render thread. g_state.lookup is the only part worker
// threads also touch, so it only changes under g_stateMutex.

// Keeps the panel alive while the user is interacting with it.
void TouchLookupPanel() {
    // Bunbun: someone answering an agent's question may take a while to think; the box
    // waits two minutes, not 25 s, and dropped the half-typed answer on the way.
    const double idle = ComposeActive() ? QuickLookup::kComposeIdleSeconds : QuickLookup::kPanelIdleSeconds;
    std::lock_guard lock(g_stateMutex);
    if (g_state.lookup.active) {
        g_state.lookup.expiresAt = std::max(g_state.lookup.expiresAt, NowSeconds() + idle);
    }
}

std::vector<LookupRow> CurrentLookupRows(LookupStatus* status) {
    std::lock_guard lock(g_stateMutex);
    if (status) *status = g_state.lookup.status;
    return BuildLookupRows(g_state.lookupRecent, g_lookupUi);
}

void ForceForeground(HWND hwnd) {
    INPUT input{};
    SendInput(1, &input, sizeof(INPUT));
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    DwmBackdrop::EnsureBehind(hwnd);
    SetFocus(hwnd);
}

// The island is WS_EX_NOACTIVATE so it never steals focus. While the search box is
// open it has to be able to receive keys, so the flag is dropped for the session.
void BeginLookupSession(HWND hwnd, bool warmModel = true) {
    if (warmModel) BrainWarmup();  // Pre-warm local AI helper in background so first query has 0ms cold-start
    HWND previous = GetForegroundWindow();
    g_lookupUi.prevForeground = (previous && previous != hwnd) ? previous : nullptr;

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_NOACTIVATE) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE));
    }
    if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    ForceForeground(hwnd);
}

bool SubmitCompose(const std::wstring& raw);  // Bunbun: text aimed at an agent (studio_module.inc)
void ClearCompose();

void EndLookupSession(HWND hwnd) {
    ClearCompose();  // Bunbun: closing the box drops a half-written reply's target
    if (!g_lookupUi.open) return;
    HWND previous = g_lookupUi.prevForeground;
    // Bunbun: or nothing has focus. A command that closes the box at once (a timer) let
    // the island drop focus first, so it went nowhere and was never handed back.
    const HWND now = GetForegroundWindow();
    const bool hadFocus = now == hwnd || now == nullptr;
    g_lookupUi = LookupUiState{};  // open = false first, so the focus change below can't re-enter

    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE);
    if (hadFocus && previous && IsWindow(previous) && IsWindowVisible(previous)) {
        INPUT unlock{};  // as ForceForeground: lets this process hand the foreground back
        SendInput(1, &unlock, sizeof(INPUT));
        SetForegroundWindow(previous);
    }
}

void CloseLookupPanel(HWND hwnd) {
    {
        std::lock_guard lock(g_stateMutex);
        g_state.lookup.active = false;
        ++g_lookupRequestSeq;  // a reply still in flight is now ignored
    }
    g_layoutDirty = true;
    EndLookupSession(hwnd);
}

// Runs a lookup and shows the outcome in the panel.
QuickLookup::Result RunBun(const std::wstring& question);  // Bunbun, defined with the media helpers
bool BrainTryAnswer(const std::wstring& question, uint64_t lookupId);  // Bunbun: brain_module.inc

void SubmitLookup(const std::wstring& rawText) {
    if (SubmitCompose(rawText)) return;  // Bunbun: plan feedback, a reply, a typed answer
    const std::wstring query = QuickLookup::NormalizeQuery(rawText);
    if (query.empty() || query.size() > QuickLookup::kMaxQueryChars) {
        return;
    }

    const double now = NowSeconds();
    LookupUiState& ui = g_lookupUi;
    // Bunbun: the box empties once a query is sent, ready for the next one; the
    // answer card shows what was asked, and Recent keeps it for re-use.
    ui.text.clear();
    ui.caret = 0;
    ui.selectAll = false;
    ui.selected = 0;
    ui.caretResetAt = now;

    LookupSnapshot snap;
    snap.active = true;
    snap.query = query;

    // Bunbun: questions go to Bun; "define ..." / "wiki ..." stay dictionary lookups.
    std::wstring term = query;
    const bool toBun = GetSettingsCopy().bunbunAssistant && QuickLookup::RouteToBun(&term);
    if (toBun) {
        // Local and instant: runs here on the window thread (the volume endpoint
        // lives on it) and the answer card replaces the panel's contents.
        const QuickLookup::Result result = RunBun(query);
        if (result.weatherFetch) {
            // Bunbun: weather for a named place (or tomorrow) comes from the network.
            snap.status = LookupStatus::Loading;
            snap.source = L"Bun";
            snap.expiresAt = now + QuickLookup::kLoadingTimeoutSeconds;
            const uint64_t id = PublishNewLookup(snap, false);
            ui.historyPos = -1;
            std::thread([id, query, place = result.weatherPlace, days = result.weatherDays]() {
                QuickLookup::Result r;
                r.source = L"Bun";
                const bool ok = FetchWeatherReport(place, days, &r.title, &r.subtitle, &r.body);
                if (!ok) {
                    r.confused = true;
                    r.title = place.empty() ? std::wstring(L"Weather") : place;
                    r.body = place.empty() ? L"Couldn’t get the forecast. Is the network up?"
                                           : L"Couldn’t find the weather for “" + place + L"”. Check the spelling, or add the country.";
                }
                QuickLookup::ApplyResult(id, query, true, r);
            }).detach();
            return;
        }
        if (result.lookupFetch) {
            // Bunbun: online encyclopedia or dictionary lookup from Bun.
            snap.status = LookupStatus::Loading;
            snap.source = L"Bun";
            snap.expiresAt = now + QuickLookup::kLoadingTimeoutSeconds;
            const uint64_t id = PublishNewLookup(snap, false);
            ui.historyPos = -1;
            std::thread([id, query, topic = result.lookupTopic]() {
                QuickLookup::Result r;
                r.source = L"Bun";
                const QuickLookup::Outcome outcome = QuickLookup::LookupText(topic, &r);
                if (outcome == QuickLookup::Outcome::Found) {
                    QuickLookup::ApplyResult(id, query, true, r);
                } else {
                    r.confused = true;
                    r.title = topic;
                    r.body = L"Couldn’t find an encyclopedia or dictionary entry for “" + topic + L"”.";
                    QuickLookup::ApplyResult(id, query, true, r);
                }
            }).detach();
            return;
        }
        if (result.unknown) {
            if (GetSettingsCopy().localAi) {
                // Bunbun: nothing matched, so the local AI takes it; its reply streams
                // into this card (brain_module.inc). Off or not set up: fall back to internet lookup.
                QuickLookup::Result thinking = result;
                thinking.body = Loc(L"Thinking…");
                thinking.unknown = false;  // not puzzled yet: the local AI is on it
                QuickLookup::FillSnapshot(snap, query, true, &thinking, now);
                const uint64_t id = PublishNewLookup(snap, false);
                if (BrainTryAnswer(query, id)) {
                    ui.historyPos = -1;
                    std::lock_guard lock(g_stateMutex);
                    QuickLookup::PushRecentLocked(g_state, query, result);
                    return;
                }
            }
            // Bunbun: Local AI is off or unavailable; seamlessly fall back to internet lookup
            // (Wikipedia / Wiktionary) so the user still gets a real answer.
            snap.status = LookupStatus::Loading;
            snap.source = L"Bun";
            snap.expiresAt = now + QuickLookup::kLoadingTimeoutSeconds;
            const uint64_t id = PublishNewLookup(snap, false);
            ui.historyPos = -1;
            std::thread([id, query, fallback = result]() {
                QuickLookup::Result r;
                const QuickLookup::Outcome outcome = QuickLookup::LookupText(query, &r);
                if (outcome == QuickLookup::Outcome::Found) {
                    QuickLookup::ApplyResult(id, query, true, r);
                } else {
                    QuickLookup::ApplyResult(id, query, true, fallback);
                }
            }).detach();
            return;
        }
        QuickLookup::FillSnapshot(snap, query, true, &result, now);
        // Bunbun: a command that ran (open, dnd, volume, timer…) only flashes its
        // confirmation. The render loop then closes the panel, hands focus back
        // to the previous app and the island returns home. Answers to questions,
        // and commands that failed, stay up to be read.
        if (result.command) snap.expiresAt = now + QuickLookup::kCommandSeconds;
        {
            std::lock_guard lock(g_stateMutex);
            QuickLookup::PushRecentLocked(g_state, query, result);  // Bunbun: for Up-arrow history
        }
        ui.historyPos = -1;
        PublishNewLookup(snap, false);
        return;
    }
    if (term.size() > QuickLookup::kMaxDictionaryChars) {
        return;
    }
    const std::wstring key = ToLowerCopy(term);
    bool cachedFound = false;
    QuickLookup::Result cached;
    if (QuickLookup::CacheLookup(key, &cachedFound, &cached)) {
        QuickLookup::FillSnapshot(snap, query, cachedFound, &cached, now);
        if (cachedFound) {
            std::lock_guard lock(g_stateMutex);
            QuickLookup::PushRecentLocked(g_state, query, cached);  // bump it to the top
        }
        PublishNewLookup(snap, false);
        return;
    }

    snap.status = LookupStatus::Loading;
    snap.expiresAt = now + QuickLookup::kLoadingTimeoutSeconds;
    const uint64_t id = PublishNewLookup(snap, false);

    // The query text is deliberately not logged.
    Wh_Log(L"Quick Lookup: request started.");
    std::thread([id, query, dictQuery = term, key]() {
        try {
            QuickLookup::Result result;
            const QuickLookup::Outcome outcome = QuickLookup::LookupText(dictQuery, &result);
            if (outcome != QuickLookup::Outcome::Failed) {
                QuickLookup::CacheStore(key, outcome == QuickLookup::Outcome::Found, result);
            }
            QuickLookup::ApplyResult(id, query, outcome == QuickLookup::Outcome::Found, result);
            Wh_Log(L"Quick Lookup: finished (outcome %d).", static_cast<int>(outcome));
        } catch (...) {
            Wh_Log(L"Quick Lookup: lookup threw an exception.");
        }
    }).detach();
}

void SubmitLookupSelection() {
    LookupStatus status = LookupStatus::Search;
    const std::vector<LookupRow> rows = CurrentLookupRows(&status);
    if (status == LookupStatus::Search && !rows.empty()) {
        const int idx = ClampInt(g_lookupUi.selected, 0, static_cast<int>(rows.size()) - 1);
        SubmitLookup(rows[static_cast<size_t>(idx)].query);
    } else {
        SubmitLookup(g_lookupUi.text);
    }
}

// Call after any change to the text. Editing leaves the result view and goes back
// to the list, and drops any reply still in flight.
void LookupUiEdited() {
    LookupUiState& ui = g_lookupUi;
    const double now = NowSeconds();
    ui.historyPos = -1;  // Bunbun: typing leaves the history walk
    ui.selected = 0;
    ui.selectAll = false;
    ui.caretResetAt = now;

    bool leaveResult = false;
    {
        std::lock_guard lock(g_stateMutex);
        leaveResult = g_state.lookup.active && g_state.lookup.status != LookupStatus::Search;
    }
    if (leaveResult) {
        LookupSnapshot snap;
        snap.active = true;
        snap.status = LookupStatus::Search;
        snap.expiresAt = now + QuickLookup::kPanelIdleSeconds;
        PublishNewLookup(snap, false);
    } else {
        TouchLookupPanel();
    }
    g_layoutDirty = true;
}

void InsertIntoLookupField(const std::wstring& raw) {
    LookupUiState& ui = g_lookupUi;
    std::wstring s;
    s.reserve(raw.size());
    for (wchar_t c : raw) {
        s.push_back((c < 0x20 || c == 0x7F) ? L' ' : c);  // single-line field
    }
    if (s.empty()) return;

    if (ui.selectAll) {
        ui.text.clear();
        ui.caret = 0;
        ui.selectAll = false;
    }
    ui.caret = std::min(ui.caret, ui.text.size());

    const size_t maxChars = QuickLookup::kMaxQueryChars;
    const size_t room = maxChars > ui.text.size() ? maxChars - ui.text.size() : 0;
    if (room == 0) return;
    if (s.size() > room) {
        s.resize(room);
        if (!s.empty() && IS_HIGH_SURROGATE(s.back())) s.pop_back();
    }
    ui.text.insert(ui.caret, s);
    ui.caret += s.size();
    LookupUiEdited();
}

size_t LookupStepBack(const std::wstring& t, size_t i) {
    if (i == 0) return 0;
    --i;
    if (i > 0 && IS_LOW_SURROGATE(t[i]) && IS_HIGH_SURROGATE(t[i - 1])) --i;
    return i;
}

size_t LookupStepForward(const std::wstring& t, size_t i) {
    if (i >= t.size()) return t.size();
    return (IS_HIGH_SURROGATE(t[i]) && i + 1 < t.size()) ? i + 2 : i + 1;
}

size_t LookupPrevWord(const std::wstring& t, size_t i) {
    while (i > 0 && iswspace(t[i - 1])) --i;
    while (i > 0 && !iswspace(t[i - 1])) --i;
    return i;
}

size_t LookupNextWord(const std::wstring& t, size_t i) {
    while (i < t.size() && !iswspace(t[i])) ++i;
    while (i < t.size() && iswspace(t[i])) ++i;
    return i;
}

// Called on the render thread when the hotkey fires.
// `fromButton`: opened by a click on the island (answering an agent, "Change city", clipboard
// search), not by the hotkey. Such a box never toggles shut (it was: clicking "Other..." while
// the box was open closed it, and the answer had nowhere to go), works while Ask Bun's own
// switch is off (it is also the answer box for agents), and doesn't wake the model.
void StartQuickLookup(HWND hwnd, bool fromButton = false) {
    bool enabled = false;
    {
        std::lock_guard lock(g_settingsMutex);
        enabled = g_settings.quickLookup;
    }
    // A manually hidden island has nothing to show the panel on.
    if (!fromButton && (!enabled || g_manuallyHidden.load())) {
        return;
    }

    if (g_lookupUi.open) {
        CloseLookupPanel(hwnd);  // hotkey again = close
        if (!fromButton) return;
        // A click: start over with an empty box, and the foreground handed back first.
    }

    // Clipboard text is only offered as a suggestion row; nothing is sent anywhere yet.
    std::wstring clip = QuickLookup::NormalizeQuery(ReadClipboardText(hwnd));
    if (clip.size() > QuickLookup::kMaxQueryChars) {
        clip.clear();
    }

    const double now = NowSeconds();
    g_lookupUi = LookupUiState{};
    g_lookupUi.open = true;
    g_lookupUi.clipboardQuery = clip;
    g_lookupUi.caretResetAt = now;

    LookupSnapshot snap;
    snap.active = true;
    snap.status = LookupStatus::Search;
    snap.expiresAt = now + QuickLookup::kPanelIdleSeconds;
    PublishNewLookup(snap);

    BeginLookupSession(hwnd, !fromButton);
}

// WM_KEYDOWN. Printable characters arrive separately through WM_CHAR.
bool HandleLookupKeyDown(HWND hwnd, WPARAM vk) {
    LookupUiState& ui = g_lookupUi;
    if (!ui.open) return false;
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool moved = false;

    switch (vk) {
        case VK_ESCAPE:
            CloseLookupPanel(hwnd);
            return true;
        case VK_RETURN:
            SubmitLookupSelection();
            return true;
        case VK_UP:
        case VK_DOWN: {
            // Bunbun: in an empty box (or while stepping through it), Up / Down walk
            // back through earlier queries like a terminal's history.
            if (ui.text.empty() || ui.historyPos >= 0) {
                std::vector<std::wstring> history;
                {
                    std::lock_guard lock(g_stateMutex);
                    for (const LookupRecent& e : g_state.lookupRecent) history.push_back(e.query);
                }
                if (!history.empty()) {
                    const int n = static_cast<int>(history.size());
                    ui.historyPos = vk == VK_UP ? std::min(ui.historyPos + 1, n - 1) : ui.historyPos - 1;
                    ui.text = ui.historyPos >= 0 ? history[static_cast<size_t>(ui.historyPos)] : std::wstring();
                    ui.caret = ui.text.size();
                    ui.selectAll = false;
                    ui.caretResetAt = NowSeconds();
                    g_layoutDirty = true;
                    return true;
                }
            }
            LookupStatus status = LookupStatus::Search;
            const std::vector<LookupRow> rows = CurrentLookupRows(&status);
            if (status == LookupStatus::Search && !rows.empty()) {
                const int n = static_cast<int>(rows.size());
                ui.selected = (ClampInt(ui.selected, 0, n - 1) + (vk == VK_DOWN ? 1 : n - 1)) % n;
            }
            moved = true;
            break;
        }
        case VK_LEFT:
            ui.caret = ui.selectAll ? 0 : (ctrl ? LookupPrevWord(ui.text, ui.caret)
                                                : LookupStepBack(ui.text, ui.caret));
            ui.selectAll = false;
            moved = true;
            break;
        case VK_RIGHT:
            ui.caret = ui.selectAll ? ui.text.size() : (ctrl ? LookupNextWord(ui.text, ui.caret)
                                                             : LookupStepForward(ui.text, ui.caret));
            ui.selectAll = false;
            moved = true;
            break;
        case VK_HOME:
            ui.caret = 0;
            ui.selectAll = false;
            moved = true;
            break;
        case VK_END:
            ui.caret = ui.text.size();
            ui.selectAll = false;
            moved = true;
            break;
        case VK_BACK:
            if (ui.selectAll) {
                ui.text.clear();
                ui.caret = 0;
            } else if (ui.caret > 0) {
                const size_t from = ctrl ? LookupPrevWord(ui.text, ui.caret)
                                         : LookupStepBack(ui.text, ui.caret);
                ui.text.erase(from, ui.caret - from);
                ui.caret = from;
            } else {
                return true;
            }
            LookupUiEdited();
            return true;
        case VK_DELETE:
            if (ui.selectAll) {
                ui.text.clear();
                ui.caret = 0;
            } else if (ui.caret < ui.text.size()) {
                const size_t to = LookupStepForward(ui.text, ui.caret);
                ui.text.erase(ui.caret, to - ui.caret);
            } else {
                return true;
            }
            LookupUiEdited();
            return true;
        case 'A':
            if (!ctrl) return false;
            ui.selectAll = !ui.text.empty();
            moved = true;
            break;
        case 'V':
            if (!ctrl) return false;
            InsertIntoLookupField(QuickLookup::CollapseWhitespace(
                ReadClipboardText(hwnd), QuickLookup::kMaxQueryChars));
            return true;
        case 'Z':
            // Bunbun: Ctrl+Z in an empty box undoes Bun's last command.
            if (!ctrl || !ui.text.empty()) return false;
            SubmitLookup(L"undo");
            return true;
        default:
            return false;
    }

    if (moved) {
        ui.caretResetAt = NowSeconds();
        TouchLookupPanel();
        g_layoutDirty = true;
    }
    return true;
}

bool HandleLookupChar(wchar_t ch) {
    if (!g_lookupUi.open) return false;
    if (ch < 0x20 || ch == 0x7F) return true;  // control keys are handled in WM_KEYDOWN
    InsertIntoLookupField(std::wstring(1, ch));
    return true;
}

