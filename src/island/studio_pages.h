// Bunbun — the Studio layout's page model, shared by the Windows island
// (studio_module.inc / studio_draw.inc) and the Linux island (studio_linux.cpp).
//
// Pure: no drawing, no platform calls. What's here decides the same things on
// both systems: which pages the tab bar has and in what order (Bunbun.PageOrder),
// what each page is called, the Home cards, the links under them, the colours
// that belong to a page or an agent, and how plan and question replies are built.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../agents/agent_view.h"
#include "../common/json.h"

namespace bb::studio {

// ── Metrics (content units) ──────────────────────────────────────────────────
// The open island: a 184-unit page between a 34-unit header and a 38-unit tab bar.
constexpr float kIslandW = 520.0f;
constexpr float kHeaderH = 34.0f;
constexpr float kFooterH = 38.0f;
constexpr float kSide = 24.0f;
constexpr float kPageH = 184.0f;
constexpr float kIslandH = kPageH + kHeaderH + kFooterH;

// ── Pages and tab ids ────────────────────────────────────────────────────────
enum class Page { Home, Music, Calendar, Weather, Hardware, Connectivity, Tray, Todo, Lyrics, Agent, Clipboard, Note, Focus, Apps, Mirror };

// Extra pages share their ids in both views; the built-in ids differ per view
// (context 0 = the media view, 1 = the idle view; see PageFor).
constexpr int kTabAgent = 20;
constexpr int kTabClipboard = 21;
constexpr int kTabNote = 22;
constexpr int kTabFocus = 23;
constexpr int kTabApps = 24;
constexpr int kTabMirror = 25;
constexpr int kTabHome = 26;  // Home in the media view (Studio), so it stays while music plays

// Which pages are switched on (and exist on this system). Calendar has no switch
// on Windows; Linux turns off what it hasn't ported.
struct PageSwitches {
    bool studio = true;              // Bunbun.Layout = studio
    bool lyricsPinnedFirst = false;  // the media view opens on Lyrics
    bool calendar = true, weather = true, system = true, toggles = true, tray = false, todo = true, lyrics = true;
    bool agent = true, clipboard = true, note = true, focus = true, mirror = false, apps = true;
    std::wstring order;              // Bunbun.PageOrder: page keys, comma-separated
};

inline bool EqualsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (towlower(a[i]) != towlower(b[i])) return false;
    return true;
}

struct OrderedPage { const wchar_t* key; int mediaId; int idleId; };
constexpr OrderedPage kOrderedPages[] = {
    {L"calendar", 1, 0},  {L"weather", 2, 1},           {L"system", 3, 2},           {L"toggles", 6, 5},
    {L"tray", 4, 4},      {L"todo", 7, 7},              {L"lyrics", 5, -1},          {L"agent", kTabAgent, kTabAgent},
    {L"clipboard", kTabClipboard, kTabClipboard},       {L"note", kTabNote, kTabNote},
    {L"focus", kTabFocus, kTabFocus},                   {L"mirror", kTabMirror, kTabMirror},
    {L"apps", kTabApps, kTabApps},
};

inline bool PageOn(std::wstring_view k, const PageSwitches& s) {
    if (k == L"calendar") return s.calendar;
    if (k == L"weather") return s.weather;
    if (k == L"system") return s.system;
    if (k == L"toggles") return s.toggles;
    if (k == L"tray") return s.tray;
    if (k == L"todo") return s.todo;
    if (k == L"lyrics") return s.lyrics;
    if (k == L"agent") return s.agent;
    if (k == L"clipboard") return s.clipboard;
    if (k == L"note") return s.note;
    if (k == L"focus") return s.focus;
    if (k == L"mirror") return s.mirror;
    return s.apps;
}

// The dashboard's tabs, in scroll order. The first page (player or Home) always
// leads; PageOrder orders the rest by key, and any page it doesn't name keeps its
// default place after those. Studio keeps Home first in both views, so the three
// cards don't vanish when music starts.
inline std::vector<int> TabIds(int context, const PageSwitches& s) {
    constexpr size_t kCount = std::size(kOrderedPages);
    size_t rank[kCount];
    for (size_t i = 0; i < kCount; ++i) rank[i] = 1000 + i;
    size_t pos = 0, at = 0;
    const std::wstring& order = s.order;
    while (at <= order.size()) {
        size_t comma = order.find(L',', at);
        if (comma == std::wstring::npos) comma = order.size();
        std::wstring key = order.substr(at, comma - at);
        key.erase(0, key.find_first_not_of(L" \t"));
        key.erase(key.find_last_not_of(L" \t") + 1);
        for (size_t i = 0; i < kCount; ++i)
            if (rank[i] >= 1000 && !key.empty() && EqualsNoCase(key, kOrderedPages[i].key)) rank[i] = pos++;
        at = comma + 1;
    }
    size_t idx[kCount];
    for (size_t i = 0; i < kCount; ++i) idx[i] = i;
    std::stable_sort(idx, idx + kCount, [&](size_t a, size_t b) { return rank[a] < rank[b]; });

    std::vector<int> ids;
    if (context == 0 && s.studio) ids.push_back(kTabHome);
    const bool lyricsFirst = context == 0 && s.lyricsPinnedFirst && PageOn(L"lyrics", s);
    if (lyricsFirst) ids.push_back(5);
    ids.push_back(context == 0 ? 0 : 3);
    for (size_t n = 0; n < kCount; ++n) {
        const OrderedPage& p = kOrderedPages[idx[n]];
        const int id = context == 0 ? p.mediaId : p.idleId;
        if (id < 0 || !PageOn(p.key, s)) continue;
        if (lyricsFirst && id == 5 && context == 0) continue;
        ids.push_back(id);
    }
    return ids;
}

inline Page PageFor(int context, int id) {
    if (id == kTabHome) return Page::Home;
    if (id == kTabAgent) return Page::Agent;
    if (id == kTabClipboard) return Page::Clipboard;
    if (id == kTabNote) return Page::Note;
    if (id == kTabFocus) return Page::Focus;
    if (id == kTabApps) return Page::Apps;
    if (id == kTabMirror) return Page::Mirror;
    if (context == 0) {
        switch (id) {
            case 0: return Page::Music;
            case 1: return Page::Calendar;
            case 2: return Page::Weather;
            case 3: return Page::Hardware;
            case 4: return Page::Tray;
            case 5: return Page::Lyrics;
            case 6: return Page::Connectivity;
            default: return Page::Todo;
        }
    }
    switch (id) {
        case 3: return Page::Home;
        case 0: return Page::Calendar;
        case 1: return Page::Weather;
        case 2: return Page::Hardware;
        case 4: return Page::Tray;
        case 5: return Page::Connectivity;
        default: return Page::Todo;
    }
}

// ── Names ────────────────────────────────────────────────────────────────────
// The header's two-tone page name: `first` in white, `second` in the page colour.
inline void PageName(Page page, const std::wstring& agentName, std::wstring* first, std::wstring* second) {
    switch (page) {
        case Page::Home: *first = L"Bun"; *second = L"bun"; break;
        case Page::Music: *first = L"Now "; *second = L"Playing"; break;
        case Page::Calendar: *first = L"Your "; *second = L"Calendar"; break;
        case Page::Weather: *first = L"Weather "; *second = L"Now"; break;
        case Page::Hardware: *first = L"System "; *second = L"Monitor"; break;
        case Page::Connectivity: *first = L"Quick "; *second = L"Toggles"; break;
        case Page::Tray: *first = L"File "; *second = L"Tray"; break;
        case Page::Todo: *first = L"To-"; *second = L"do"; break;
        case Page::Lyrics: *first = L"Live "; *second = L"Lyrics"; break;
        case Page::Clipboard: *first = L"Clip"; *second = L"board"; break;
        case Page::Note: *first = L"Quick "; *second = L"Note"; break;
        case Page::Focus: *first = L"Focus "; *second = L"Timer"; break;
        case Page::Apps: *first = L"All "; *second = L"Apps"; break;
        case Page::Mirror: *first = L"Mir"; *second = L"ror"; break;
        case Page::Agent: {
            const std::wstring name = agentName.empty() ? L"Coding Agents" : agentName;
            const size_t space = name.find(L' ');
            if (space == std::wstring::npos) { *first = name; second->clear(); }
            else { *first = name.substr(0, space + 1); *second = name.substr(space + 1); }
            break;
        }
    }
}

inline const wchar_t* TabLabel(Page page) {
    switch (page) {
        case Page::Home: return L"Home";
        case Page::Music: return L"Music";
        case Page::Calendar: return L"Calendar";
        case Page::Weather: return L"Weather";
        case Page::Hardware: return L"System";
        case Page::Connectivity: return L"Toggles";
        case Page::Tray: return L"Tray";
        case Page::Todo: return L"To-do";
        case Page::Lyrics: return L"Lyrics";
        case Page::Agent: return L"Agent";
        case Page::Clipboard: return L"Clipboard";
        case Page::Note: return L"Note";
        case Page::Focus: return L"Focus";
        case Page::Apps: return L"Apps";
        case Page::Mirror: return L"Mirror";
    }
    return L"";
}

// ── Colours (0xRRGGBB) ───────────────────────────────────────────────────────
// Each coding agent keeps its own brand colour; everything else on the island
// follows the dynamic accent.
inline uint32_t AgentBrand(const std::wstring& agent) {
    if (agent.find(L"Codex") != std::wstring::npos) return 0x10a37f;  // OpenAI green
    if (agent.find(L"Antigravity") != std::wstring::npos || agent.find(L"Gemini") != std::wstring::npos)
        return 0x3186ff;                                               // Antigravity blue
    if (agent.find(L"Cursor") != std::wstring::npos) return 0xd6d6d6;  // Cursor's monochrome
    if (agent.find(L"Copilot") != std::wstring::npos || agent.find(L"VS Code") != std::wstring::npos)
        return 0x0098ff;                                               // VS Code blue
    return 0xd97757;                                                   // Claude
}

inline uint32_t AgentStatus(AgentPhase p) {
    switch (p) {
        case AgentPhase::Thinking:
        case AgentPhase::Working: return 0xff9a1f;
        case AgentPhase::Approval:
        case AgentPhase::Question: return 0xff9f0a;
        case AgentPhase::Finished: return 0x30d158;
        case AgentPhase::Error: return 0xff453a;
        default: return 0xb8c0b5;
    }
}

// Green, orange, red as a limit fills up.
inline uint32_t UsageColour(float percent) {
    return percent >= 85.0f ? 0xff3b3b : percent >= 60.0f ? 0xff9a1f : 0x22e05a;
}

// ── Home ─────────────────────────────────────────────────────────────────────
// What each of Home's three slots shows: Bunbun.HomeTile1-3. Auto = the agent
// (the weather when no agent is shown), music, focus. Home cards keep their own
// colours; only the Music card follows the album.
enum class HomeTile { Agent, Music, Focus, Weather, Calendar, Battery, System };
constexpr const wchar_t* kHomeTileIds[] = {L"agent", L"music", L"focus", L"weather", L"calendar", L"battery", L"system"};

// "agent", "music"… → its HomeTile index, -1 for auto or anything unknown.
inline int HomeTileSetting(const std::wstring& value) {
    for (int k = 0; k < static_cast<int>(std::size(kHomeTileIds)); ++k)
        if (EqualsNoCase(value, kHomeTileIds[k])) return k;
    return -1;
}

inline HomeTile HomeTileFor(int setting, int slot, bool agentsShown) {
    if (setting >= 0 && setting <= static_cast<int>(HomeTile::System)) {
        const HomeTile k = static_cast<HomeTile>(setting);
        return k == HomeTile::Agent && !agentsShown ? HomeTile::Weather : k;
    }
    if (slot == 0) return agentsShown ? HomeTile::Agent : HomeTile::Weather;
    return slot == 1 ? HomeTile::Music : HomeTile::Focus;
}

// "Name|address; Name|address" -> up to four (name, address). A bare address is
// named after its host or file name.
inline std::vector<std::pair<std::wstring, std::wstring>> ParseHomeLinks(const std::wstring& text) {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    auto trim = [](std::wstring t) {
        while (!t.empty() && iswspace(t.front())) t.erase(t.begin());
        while (!t.empty() && iswspace(t.back())) t.pop_back();
        return t;
    };
    size_t start = 0;
    while (start < text.size() && out.size() < 4) {
        size_t end = text.find(L';', start);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring item = trim(text.substr(start, end - start));
        start = end + 1;
        if (item.empty()) continue;
        const size_t bar = item.find(L'|');
        std::wstring name = bar == std::wstring::npos ? std::wstring() : trim(item.substr(0, bar));
        std::wstring target = trim(bar == std::wstring::npos ? item : item.substr(bar + 1));
        if (target.empty()) continue;
        if (name.empty()) {
            std::wstring t = target;
            if (const size_t scheme = t.find(L"://"); scheme != std::wstring::npos) t = t.substr(scheme + 3);
            if (t.rfind(L"www.", 0) == 0) t = t.substr(4);
            const size_t cut = t.find_first_of(L"/\\");
            name = cut == std::wstring::npos ? t : t.substr(0, cut);
            // A drive path ("C:\notes\todo.txt") is named after its file.
            if (name.size() >= 2 && name[1] == L':') name = t.substr(t.find_last_of(L"/\\") + 1);
        }
        out.emplace_back(name, target);
    }
    return out;
}

// ── Agent cards ──────────────────────────────────────────────────────────────

// A plan as plain lines: no '#' headings, "- " bullets as "• ", no ** or `.
inline std::wstring CleanPlanText(const std::wstring& plan) {
    std::wstring out;
    size_t start = 0;
    while (start <= plan.size()) {
        size_t end = plan.find(L'\n', start);
        if (end == std::wstring::npos) end = plan.size();
        std::wstring line = plan.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t i = 0;
        while (i < line.size() && line[i] == L'#') ++i;
        if (i > 0) { line = line.substr(i); while (!line.empty() && line.front() == L' ') line.erase(line.begin()); }
        size_t lead = 0;
        while (lead < line.size() && line[lead] == L' ') ++lead;
        if (lead + 1 < line.size() && (line[lead] == L'-' || line[lead] == L'*') && line[lead + 1] == L' ')
            line = line.substr(0, lead) + L"\x2022 " + line.substr(lead + 2);
        for (const std::wstring_view tag : {std::wstring_view(L"**"), std::wstring_view(L"`")}) {
            for (size_t p = line.find(tag); p != std::wstring::npos; p = line.find(tag, p)) line.erase(p, tag.size());
        }
        out += line;
        out += L'\n';
        if (end == plan.size()) break;
        start = end + 1;
    }
    while (!out.empty() && (out.back() == L'\n' || out.back() == L' ')) out.pop_back();
    return out;
}

// Every question with its questions listed gets the step-through card: the question
// wraps in full and each option is a row (a row of quick buttons cut both short).
inline bool NeedsQuestionCard(const AgentView& a) {
    return a.phase == AgentPhase::Question && !a.questions.empty();
}

// One single-choice question: picking an option answers it, no Send needed.
inline bool AnswersOnPick(const AgentView& a) {
    return a.questions.size() == 1 && !a.questions[0].multi;
}

// The question card's layout, shared by Windows and Linux. textH is the question's
// wrapped height (at most kQuestionTextMax shows); offsets are from the card's top.
constexpr int kQuestionRowsMax = 8;
constexpr double kQuestionTextMax = 88;
inline double QuestionTextShown(double textH) { return textH < 18 ? 18 : (textH > kQuestionTextMax ? kQuestionTextMax : textH); }
inline double QuestionOptionsTop(double textH, bool subLine) {
    const double y = 27 + QuestionTextShown(textH) + 4 + (subLine ? 16 : 0) + 6;
    return y < 58 ? 58 : y;
}
inline int QuestionRows(const AgentView::Question& q) {
    return q.options.size() < static_cast<size_t>(kQuestionRowsMax) ? static_cast<int>(q.options.size()) : kQuestionRowsMax;
}
inline double QuestionCardHeight(double textH, bool subLine, int rows) {
    return QuestionOptionsTop(textH, subLine) + 30.0 * rows + 62;  // the "Your answer" line, the buttons, the margin
}

// Your answers while you step through the questions of one request.
struct Answers {
    uint64_t requestId = 0;
    int index = 0;                              // the question on screen
    std::vector<std::vector<int>> picks;        // chosen option indices per question
    std::vector<std::wstring> other;            // typed answers per question

    // Starts over when a different request (or a changed one) is on screen.
    void Sync(const AgentView& a) {
        if (requestId == a.requestId && picks.size() == a.questions.size()) return;
        *this = Answers{};
        requestId = a.requestId;
        picks.resize(a.questions.size());
        other.resize(a.questions.size());
    }
    bool Answered(int i) const {
        if (i < 0 || i >= static_cast<int>(picks.size())) return false;
        return !picks[i].empty() || !other[i].empty();
    }
    // Picks option `o` of the current question (toggles it for multi-select).
    void Toggle(const AgentView& a, int o) {
        Sync(a);
        if (index < 0 || index >= static_cast<int>(a.questions.size())) return;
        auto& p = picks[index];
        other[index].clear();
        if (a.questions[index].multi) {
            auto it = std::find(p.begin(), p.end(), o);
            if (it == p.end()) p.push_back(o); else p.erase(it);
        } else {
            p.assign(1, o);
        }
    }
};

inline std::string Utf8(const std::wstring& w) {
    std::string out;
    for (size_t i = 0; i < w.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(w[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp < 0xDC00 && i + 1 < w.size()) {
            const uint32_t lo = static_cast<uint32_t>(w[i + 1]);
            if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); ++i; }
        }
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

// {"answers":{"<question>":"<answer>"}} for every question, or "" while one is
// unanswered. Several picks join with ", "; a typed answer wins over picks.
inline std::string QuestionsReply(const AgentView& a, const Answers& ans) {
    json::Value reply = json::Value::Object();
    for (size_t i = 0; i < a.questions.size(); ++i) {
        std::wstring answer = i < ans.other.size() ? ans.other[i] : std::wstring();
        if (answer.empty() && i < ans.picks.size()) {
            for (int o : ans.picks[i]) {
                if (o < 0 || o >= static_cast<int>(a.questions[i].options.size())) continue;
                if (!answer.empty()) answer += L", ";
                answer += a.questions[i].options[o];
            }
        }
        if (answer.empty()) return {};
        reply["answers"][Utf8(a.questions[i].text)] = json::Value::String(Utf8(answer));
    }
    return json::Serialize(reply);
}

// {"deny":"<what to change>"}: sends a plan back so the agent keeps planning.
inline std::string PlanChangeReply(const std::wstring& note) {
    json::Value reply = json::Value::Object();
    reply["deny"] = json::Value::String(Utf8(note));
    return json::Serialize(reply);
}

}  // namespace bb::studio
