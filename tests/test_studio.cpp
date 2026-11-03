// Bunbun — unit tests for the Studio page model (src/island/studio_pages.h),
// which the Windows and Linux islands share. Portable: built by tests/run.sh on
// Windows and as `test_studio` by CMake on Linux.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/island/studio_pages.h"

namespace st = bb::studio;

static int g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                 \
        }                                                               \
    } while (0)

static std::vector<st::Page> Pages(int context, const st::PageSwitches& s) {
    std::vector<st::Page> out;
    for (int id : st::TabIds(context, s)) out.push_back(st::PageFor(context, id));
    return out;
}

static void TestPageOrder() {
    using P = st::Page;
    st::PageSwitches s;
    s.system = s.toggles = s.todo = s.lyrics = s.clipboard = s.note = s.focus = s.apps = false;
    // Default order: Home, then calendar, weather, agent.
    CHECK((Pages(1, s) == std::vector<P>{P::Home, P::Calendar, P::Weather, P::Agent}));
    // Studio keeps Home first in the media view too, then the player.
    CHECK((Pages(0, s) == std::vector<P>{P::Home, P::Music, P::Calendar, P::Weather, P::Agent}));
    // Classic: the player leads, no Home in the media view.
    s.studio = false;
    CHECK((Pages(0, s) == std::vector<P>{P::Music, P::Calendar, P::Weather, P::Agent}));
    s.studio = true;

    // PageOrder: named pages first in its order (any case, spaces), the rest after.
    s.order = L" Agent ,WEATHER";
    CHECK((Pages(1, s) == std::vector<P>{P::Home, P::Agent, P::Weather, P::Calendar}));
    // Unknown and repeated keys are ignored; switched-off pages stay out.
    s.order = L"nothing,agent,agent,,focus,calendar";
    CHECK((Pages(1, s) == std::vector<P>{P::Home, P::Agent, P::Calendar, P::Weather}));
    // Home can't be moved: it isn't a key.
    s.order = L"home,weather";
    CHECK(Pages(1, s).front() == P::Home);

    // Lyrics only in the media view; pinned first, it goes right after Home.
    s.order.clear();
    s.lyrics = true;
    s.lyricsPinnedFirst = true;
    CHECK((Pages(0, s) == std::vector<P>{P::Home, P::Lyrics, P::Music, P::Calendar, P::Weather, P::Agent}));
    CHECK((Pages(1, s) == std::vector<P>{P::Home, P::Calendar, P::Weather, P::Agent}));

    // Linux today: Home, Music and Agent only.
    st::PageSwitches lin;
    lin.calendar = lin.weather = lin.system = lin.toggles = lin.tray = lin.todo = lin.lyrics = false;
    lin.clipboard = lin.note = lin.focus = lin.mirror = lin.apps = false;
    CHECK((Pages(0, lin) == std::vector<P>{P::Home, P::Music, P::Agent}));
    CHECK((Pages(1, lin) == std::vector<P>{P::Home, P::Agent}));
    lin.agent = false;
    CHECK((Pages(1, lin) == std::vector<P>{P::Home}));

    // Ids from both views map to the same extra pages.
    CHECK(st::PageFor(0, st::kTabAgent) == P::Agent && st::PageFor(1, st::kTabAgent) == P::Agent);
    CHECK(st::PageFor(0, st::kTabHome) == P::Home && st::PageFor(1, 3) == P::Home && st::PageFor(0, 3) == P::Hardware);
}

static void TestNamesAndColours() {
    std::wstring a, b;
    st::PageName(st::Page::Music, L"", &a, &b);
    CHECK(a == L"Now " && b == L"Playing");
    st::PageName(st::Page::Agent, L"Claude Code", &a, &b);
    CHECK(a == L"Claude " && b == L"Code");
    st::PageName(st::Page::Agent, L"Codex", &a, &b);
    CHECK(a == L"Codex" && b.empty());
    st::PageName(st::Page::Agent, L"", &a, &b);
    CHECK(a == L"Coding " && b == L"Agents");
    CHECK(std::wstring(st::TabLabel(st::Page::Hardware)) == L"System");

    CHECK(st::AgentBrand(L"Claude Code") == 0xd97757);
    CHECK(st::AgentBrand(L"Antigravity") == 0x3186ff);
    CHECK(st::AgentBrand(L"VS Code") == 0x0098ff && st::AgentBrand(L"Copilot") == 0x0098ff);
    CHECK(st::AgentBrand(L"Cursor") == 0xd6d6d6);
    CHECK(st::AgentBrand(L"Codex") == 0x10a37f);
    CHECK(st::AgentStatus(bb::AgentPhase::Finished) == 0x30d158);
    CHECK(st::UsageColour(10) == 0x22e05a && st::UsageColour(60) == 0xff9a1f && st::UsageColour(90) == 0xff3b3b);
}

static void TestHome() {
    using T = st::HomeTile;
    CHECK(st::HomeTileSetting(L"Music") == 1 && st::HomeTileSetting(L"auto") == -1 && st::HomeTileSetting(L"") == -1);
    CHECK(st::HomeTileFor(-1, 0, true) == T::Agent);
    CHECK(st::HomeTileFor(-1, 0, false) == T::Weather);
    CHECK(st::HomeTileFor(-1, 1, true) == T::Music && st::HomeTileFor(-1, 2, true) == T::Focus);
    CHECK(st::HomeTileFor(0, 2, false) == T::Weather);  // an agent card with no agent shows the weather
    CHECK(st::HomeTileFor(6, 0, true) == T::System);

    const auto links = st::ParseHomeLinks(L" Docs | https://docs.example.com/a ; https://www.github.com/x;;C:\\notes\\todo.txt; a|b; c|d");
    CHECK(links.size() == 4);
    CHECK(links[0].first == L"Docs" && links[0].second == L"https://docs.example.com/a");
    CHECK(links[1].first == L"github.com");
    CHECK(links[2].first == L"todo.txt");
    CHECK(links[3].first == L"a");
}

static void TestAgentCards() {
    CHECK(st::CleanPlanText(L"# Plan\r\n- **one**\n  * `two`\n\n") == L"Plan\n\x2022 one\n  \x2022 two");

    bb::AgentView a;
    a.phase = bb::AgentPhase::Question;
    a.requestId = 7;
    a.questions.push_back({L"Colour?", L"Pick", false, {L"Red", L"Blue"}});
    a.options = {L"Red", L"Blue"};
    CHECK(!st::NeedsQuestionCard(a));  // one plain question: the quick buttons do
    a.questions.push_back({L"Sizes?", L"", true, {L"S", L"M", L"L"}});
    CHECK(st::NeedsQuestionCard(a));

    st::Answers ans;
    ans.Sync(a);
    CHECK(!ans.Answered(0) && st::QuestionsReply(a, ans).empty());
    ans.Toggle(a, 1);
    ans.Toggle(a, 0);  // single choice: replaces
    CHECK(ans.picks[0] == std::vector<int>{0});
    ans.index = 1;
    ans.Toggle(a, 0);
    ans.Toggle(a, 2);
    ans.Toggle(a, 0);  // multi: toggles off
    CHECK(ans.picks[1] == std::vector<int>{2});
    ans.other[1] = L"XL";
    bb::json::Value reply;
    CHECK(bb::json::Parse(st::QuestionsReply(a, ans), &reply));
    const bb::json::Value* answers = reply.find("answers");
    CHECK(answers && answers->getString("Colour?") == "Red" && answers->getString("Sizes?") == "XL");

    // A new request starts over.
    a.requestId = 8;
    ans.Sync(a);
    CHECK(ans.index == 0 && !ans.Answered(0));

    CHECK(bb::json::Parse(st::PlanChangeReply(L"Use \x00e9t\x00e9 \U0001F600"), &reply) &&
          reply.getString("deny") == "Use \xC3\xA9t\xC3\xA9 \xF0\x9F\x98\x80");
}

int main() {
    TestPageOrder();
    TestNamesAndColours();
    TestHome();
    TestAgentCards();
    if (g_failed) {
        std::printf("%d studio check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("all studio checks passed\n");
    return 0;
}
