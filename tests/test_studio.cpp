// Bunbun — unit tests for the Studio page model (src/island/studio_pages.h),
// which the Windows and Linux islands share. Portable: built by tests/run.sh on
// Windows and as `test_studio` by CMake on Linux.
#include <cstdio>
#include <string>
#include <vector>

#include "../src/common/timers.h"
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
    s.system = s.toggles = s.todo = s.lyrics = s.clipboard = s.note = s.focus = s.apps = s.water = false;
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

    // Studio merges hardware and connectivity into a single System page:
    st::PageSwitches m;
    m.studio = true;
    m.system = true;
    m.toggles = true;
    m.calendar = m.weather = m.agent = m.tray = m.todo = m.lyrics = false;
    m.clipboard = m.note = m.focus = m.mirror = m.apps = m.water = false;
    CHECK((Pages(1, m) == std::vector<P>{P::Home, P::Hardware}));
    m.system = false;
    m.toggles = true;
    CHECK((Pages(1, m) == std::vector<P>{P::Home, P::Hardware}));
    m.system = true;
    m.toggles = false;
    CHECK((Pages(1, m) == std::vector<P>{P::Home, P::Hardware}));
    m.system = false;
    m.toggles = false;
    CHECK((Pages(1, m) == std::vector<P>{P::Home}));
    st::PageSwitches cl;
    cl.studio = false;
    cl.system = true;
    cl.toggles = true;
    cl.calendar = cl.weather = cl.agent = cl.tray = cl.todo = cl.lyrics = false;
    cl.clipboard = cl.note = cl.focus = cl.mirror = cl.apps = cl.water = false;
    CHECK((Pages(1, cl) == std::vector<P>{P::Home, P::Hardware, P::Connectivity}));

    // Lyrics only in the media view; pinned first, it goes right after Home.
    s.order.clear();
    s.lyrics = true;
    s.lyricsPinnedFirst = true;
    CHECK((Pages(0, s) == std::vector<P>{P::Home, P::Lyrics, P::Music, P::Calendar, P::Weather, P::Agent}));
    CHECK((Pages(1, s) == std::vector<P>{P::Home, P::Calendar, P::Weather, P::Agent}));

    // Linux today: Home, Music and Agent only.
    st::PageSwitches lin;
    lin.calendar = lin.weather = lin.system = lin.toggles = lin.tray = lin.todo = lin.lyrics = false;
    lin.clipboard = lin.note = lin.focus = lin.mirror = lin.apps = lin.water = false;
    CHECK((Pages(0, lin) == std::vector<P>{P::Home, P::Music, P::Agent}));
    CHECK((Pages(1, lin) == std::vector<P>{P::Home, P::Agent}));
    lin.agent = false;
    CHECK((Pages(1, lin) == std::vector<P>{P::Home}));

    // The Water page: after Focus by default, its own id in both views, and named.
    st::PageSwitches w;
    w.system = w.toggles = w.todo = w.lyrics = w.clipboard = w.note = w.apps = w.agent = w.calendar = w.weather = false;
    CHECK((Pages(1, w) == std::vector<P>{P::Home, P::Focus, P::Water}));
    w.order = L"water";
    CHECK((Pages(1, w) == std::vector<P>{P::Home, P::Water, P::Focus}));
    CHECK(st::PageFor(0, st::kTabWater) == P::Water && st::PageFor(1, st::kTabWater) == P::Water);
    CHECK(std::wstring(st::TabLabel(P::Water)) == L"Water");

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
    CHECK(st::AgentBrand(L"Cursor") == 0x5bb2ff);
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
    CHECK(st::NeedsQuestionCard(a));  // every question: the text wraps, options are rows
    CHECK(st::AnswersOnPick(a));      // one single choice: a pick answers it
    CHECK(st::QuestionCardHeight(0, false, 2) == 58 + 60 + 62);
    CHECK(st::QuestionOptionsTop(200, true) == 27 + st::kQuestionTextMax + 4 + 16 + 6);
    a.questions.push_back({L"Sizes?", L"", true, {L"S", L"M", L"L"}});
    CHECK(st::NeedsQuestionCard(a));
    CHECK(!st::AnswersOnPick(a));

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

static void TestTimers() {
    namespace tm = bb::timers;
    using P = tm::Phase;
    tm::PomodoroConfig c;  // 25 / 5 / 15, 4 cycles
    tm::Pomodoro p;
    CHECK(tm::PhaseLabel(p, c) == L"Focus 1 of 4");
    // focus -> short break -> focus 2 ... focus 4 -> long break -> focus 1
    std::vector<P> seen;
    for (int i = 0; i < 9; ++i) {
        tm::Advance(&p, c, 20261106, true);
        seen.push_back(p.phase);
    }
    CHECK((seen == std::vector<P>{P::ShortBreak, P::Focus, P::ShortBreak, P::Focus, P::ShortBreak, P::Focus, P::LongBreak,
                                  P::Focus, P::ShortBreak}));
    CHECK(p.cycle == 1 && p.sessionsToday == 5);
    // A skipped focus doesn't count; a new day starts at zero.
    tm::Pomodoro q;
    tm::Advance(&q, c, 20261106, false);
    CHECK(q.sessionsToday == 0 && q.phase == P::ShortBreak);
    tm::Advance(&q, c, 20261107, true);  // the break ends the next day
    CHECK(q.sessionsToday == 0 && q.day == 20261107 && q.cycle == 2);
    CHECK(tm::DoneMessage(P::Focus, P::LongBreak) == L"Cycle done \x00B7 Take a long break");
    tm::PomodoroConfig one = tm::Sanitize({50, 10, 30, 1, true});
    tm::Pomodoro r;
    CHECK(tm::NextPhase(r, one) == P::LongBreak);  // one cycle: every focus earns the long break

    // The dial: a turn is an hour, snapped to minutes; 12 o'clock is a full turn.
    CHECK(tm::SecondsFromAngle(3.14159265358979) == 30 * 60);
    CHECK(tm::SecondsFromAngle(0.0) == 60 * 60);
    CHECK(tm::SecondsFromAngle(-1.5707963267949) == 45 * 60);
    CHECK(tm::DialFraction(15 * 60) == 0.25 && tm::DialFraction(3600) == 1.0 && tm::DialFraction(0) == 0.0);
    CHECK(tm::StepCountdown(25 * 60 + 30, 1, false) == 26 * 60);
    CHECK(tm::StepCountdown(60, -3, false) == 60 && tm::StepCountdown(60, -2, true) == 50);
    // The Focus dial: two hours a turn, 5-minute steps.
    CHECK(tm::FocusMinutesFromAngle(0.0) == 120 && tm::FocusMinutesFromAngle(3.14159265) == 60);
    CHECK(tm::FocusMinutesFromAngle(-3.14159265 / 2) == 90 && tm::FocusMinutesFromAngle(0.27) == 5);
    CHECK(tm::FocusDialFraction(30) == 0.25 && tm::FocusDialFraction(200) == 1.0);
    CHECK(tm::StepFocusMinutes(25, 1) == 30 && tm::StepFocusMinutes(5, -1) == 5 && tm::StepFocusMinutes(178, 2) == 180);

    // Presets: minutes or m:ss, no duplicates, at most six.
    const auto pr = tm::ParsePresets(L" 5, 10,1:30 , 10, x, 0, 25, 45, 60, 90");
    CHECK((pr == std::vector<int>{300, 600, 90, 1500, 2700, 3600}));
    CHECK(tm::SerializePresets({300, 90}) == L"5, 1:30");
    CHECK(tm::PresetLabel(90 * 60) == L"1 h 30" && tm::PresetLabel(25 * 60) == L"25 min" && tm::PresetLabel(90) == L"1:30");
    CHECK((tm::AddPreset(pr, 600) == std::vector<int>{600, 300, 90, 1500, 2700, 3600}));
    CHECK(tm::AddPreset(pr, 120).size() == 6 && tm::AddPreset(pr, 120).front() == 120);

    // Stopwatch: laps, marks and text.
    std::vector<tm::Lap> laps;
    CHECK(tm::MarkLaps(laps).fastest == -1);
    tm::AddLap(&laps, 10.0);
    tm::AddLap(&laps, 18.5);
    tm::AddLap(&laps, 30.0);
    const tm::LapMarks m = tm::MarkLaps(laps);
    CHECK(m.fastest == 1 && m.slowest == 2);
    CHECK(tm::StopwatchText(247.256) == L"4:07.25" && tm::StopwatchText(3723.4) == L"1:02:03.40");
    CHECK(tm::LapsText(laps).rfind(L"Lap\tSplit\tTotal\n1\t0:10.00\t0:10.00\n2\t0:08.50\t0:18.50", 0) == 0);
}

int main() {
    TestPageOrder();
    TestNamesAndColours();
    TestHome();
    TestAgentCards();
    TestTimers();
    if (g_failed) {
        std::printf("%d studio check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("all studio checks passed\n");
    return 0;
}
