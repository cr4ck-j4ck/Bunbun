// Bunbun — unit tests for the JSON library, the Claude Code hook installer and
// Bun's assistant parser.
// Build + run: ./tests/run.sh   (uses a throwaway CLAUDE_CONFIG_DIR)
#include <windows.h>

#include <cstdio>
#include <string>

#include "../src/agents/hook_installer.h"
#include "../src/assistant/assistant.h"
#include "../src/common/json.h"
#include "../src/common/update.h"
#include "../src/common/todos.h"

using bb::json::Value;

static int g_failed = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                 \
        }                                                               \
    } while (0)

static std::string ReadAll(const std::wstring& path) {
    std::string s;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

static void WriteAll(const std::wstring& path, const std::string& s) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

static void TestJson() {
    Value v;
    CHECK(bb::json::Parse(R"J({"a":1,"b":[true,false,null],"c":"x\"y\u00e9\n","d":{"e":-1.5e3}})J", &v));
    CHECK(v.getNumber("a") == 1);
    CHECK(v.find("b")->arr.size() == 3);
    CHECK(v.getString("c") == "x\"y\xC3\xA9\n");
    CHECK(v.find("d")->getNumber("e") == -1500);
    // Round trip keeps key order and number text.
    CHECK(bb::json::Serialize(v) == R"J({"a":1,"b":[true,false,null],"c":"x\"y)J" "\xC3\xA9" R"J(\n","d":{"e":-1.5e3}})J");
    // Surrogate pair.
    CHECK(bb::json::Parse("\"\\ud83d\\udc30\"", &v) && v.str == "\xF0\x9F\x90\xB0");
    // Strictness: these must all be rejected.
    for (const char* bad : {"{", "{\"a\":}", "[1,]", "{'a':1}", "01", "1.", "\"\x01\"", "{\"a\":1} x", "nul"}) {
        CHECK(!bb::json::Parse(bad, &v));
    }
    // BOM is tolerated.
    CHECK(bb::json::Parse("\xEF\xBB\xBF{}", &v) && v.isObject());
}

static void TestInstaller(const std::wstring& dir) {
    const std::wstring path = dir + L"\\settings.json";
    SetEnvironmentVariableW(L"CLAUDE_CONFIG_DIR", dir.c_str());
    CHECK(bb::hooks::ClaudeSettingsPath() == path);

    // Existing user config with an unrelated hook and other keys.
    WriteAll(path, R"J({
  "model": "opus",
  "permissions": {"allow": ["Bash(ls)"]},
  "hooks": {"PreToolUse": [{"matcher": "Bash", "hooks": [{"type": "command", "command": "my-linter"}]}]}
})J");
    std::wstring report;
    CHECK(!bb::hooks::ClaudeHooksInstalled());
    CHECK(bb::hooks::InstallClaudeHooks(&report));
    CHECK(bb::hooks::ClaudeHooksInstalled());

    Value root;
    CHECK(bb::json::Parse(ReadAll(path), &root));
    CHECK(root.getString("model") == "opus");
    CHECK(root.find("permissions") != nullptr);
    const Value* pre = root.find("hooks")->find("PreToolUse");
    CHECK(pre && pre->arr.size() == 2);  // user's hook kept, ours appended
    CHECK(pre->arr[0].find("hooks")->arr[0].getString("command") == "my-linter");
    const std::string cmd = pre->arr[1].find("hooks")->arr[0].getString("command");
    CHECK(cmd.find("bunbun-hook.exe\" PreToolUse") != std::string::npos);
    CHECK(cmd.find('\\') == std::string::npos);  // forward slashes for Git Bash
    CHECK(root.find("hooks")->find("PermissionRequest")->arr[0].find("hooks")->arr[0].getNumber("timeout") == 120);

    // Idempotent: installing twice does not duplicate entries.
    CHECK(bb::hooks::InstallClaudeHooks(&report));
    CHECK(bb::json::Parse(ReadAll(path), &root));
    CHECK(root.find("hooks")->find("PreToolUse")->arr.size() == 2);

    // Uninstall restores the user's hooks only.
    CHECK(bb::hooks::UninstallClaudeHooks(&report));
    CHECK(!bb::hooks::ClaudeHooksInstalled());
    CHECK(bb::json::Parse(ReadAll(path), &root));
    CHECK(root.find("hooks")->find("PreToolUse")->arr.size() == 1);
    CHECK(root.find("hooks")->find("Stop") == nullptr);

    // Invalid JSON is never overwritten.
    WriteAll(path, "{ \"model\": \"opus\", }");
    CHECK(!bb::hooks::InstallClaudeHooks(&report));
    CHECK(ReadAll(path) == "{ \"model\": \"opus\", }");

    // A hooks value of the wrong type is refused, not replaced.
    WriteAll(path, R"J({"hooks": []})J");
    CHECK(!bb::hooks::InstallClaudeHooks(&report));
    CHECK(ReadAll(path) == R"J({"hooks": []})J");

    // Missing file: created from scratch.
    DeleteFileW(path.c_str());
    CHECK(bb::hooks::InstallClaudeHooks(&report));
    CHECK(bb::json::Parse(ReadAll(path), &root) && root.find("hooks")->obj.size() == 12);
}


static void TestAssistant() {
    namespace as = bb::assistant;
    auto parse = [](const wchar_t* t) { return as::Parse(t, L"google"); };
    double v = 0;
    CHECK(as::Evaluate(L"2+3*4", &v) && v == 14);
    CHECK(as::Evaluate(L"(1+2)^2 / 3", &v) && v == 3);
    CHECK(as::Evaluate(L"-4 + 10", &v) && v == 6);
    CHECK(!as::Evaluate(L"1/0", &v));
    CHECK(!as::Evaluate(L"open notepad", &v));
    CHECK(!as::Evaluate(L"2+", &v));

    as::Intent in = parse(L"what is 12*7?");
    CHECK(in.action == as::Action::Reply && in.reply.find(L"84") != std::wstring::npos);
    in = parse(L"Open Notepad");
    CHECK(in.action == as::Action::Open && in.target == L"notepad.exe");
    in = parse(L"wifi settings");
    CHECK(in.action == as::Action::Open && in.target == L"ms-settings:network-wifi");
    in = parse(L"launch some unknown app");
    CHECK(in.action == as::Action::Open && in.target.empty() && in.label == L"some unknown");  // " app" is trimmed
    in = parse(L"hey bun volume 35");
    CHECK(in.action == as::Action::VolumeSet && in.number == 35);
    CHECK(parse(L"turn the volume up").action == as::Action::VolumeUp);
    CHECK(parse(L"mute").action == as::Action::Mute);
    CHECK(parse(L"dnd on").action == as::Action::MascotDndOn);
    CHECK(parse(L"snack time").action == as::Action::MascotDndOn);
    CHECK(parse(L"dnd off").action == as::Action::MascotDndOff);
    CHECK(parse(L"Hey Bun, please turn on DND!").action == as::Action::MascotDndOn);
    CHECK(parse(L"enable do not disturb").action == as::Action::MascotDndOn);
    CHECK(parse(L"turn off dnd please").action == as::Action::MascotDndOff);
    CHECK(parse(L"disable do not disturb").action == as::Action::MascotDndOff);
    CHECK(parse(L"open dnd settings").action == as::Action::Open);  // Windows Focus settings, not the mascot
    in = parse(L"can you open up notepad for me?");
    CHECK(in.action == as::Action::Open && in.target == L"notepad.exe");
    in = parse(L"launch the spotify app");
    CHECK(in.action == as::Action::Open && _wcsicmp(in.label.c_str(), L"spotify") == 0);
    CHECK(parse(L"next song").action == as::Action::MediaNext);
    // To-dos and reminders
    in = parse(L"add buy milk");
    CHECK(in.action == as::Action::TodoAdd && in.label == L"Buy milk" && in.dueHour < 0 && in.dueInMinutes < 0);
    in = parse(L"add call mom to my list");
    CHECK(in.action == as::Action::TodoAdd && in.label == L"Call mom");
    in = parse(L"remind me at 3pm to call Sam");
    CHECK(in.action == as::Action::TodoAdd && in.label == L"Call Sam" && in.dueHour == 15 && in.dueMinute == 0);
    in = parse(L"remind me to stretch in 20 minutes");
    CHECK(in.action == as::Action::TodoAdd && in.label == L"Stretch" && in.dueInMinutes == 20);
    in = parse(L"remind me tomorrow at 9:30 to send the report");
    CHECK(in.action == as::Action::TodoAdd && in.dueDays == 1 && in.dueHour == 9 && in.dueMinute == 30 && in.label == L"Send the report");
    in = parse(L"remind me to water plants tomorrow");
    CHECK(in.action == as::Action::TodoAdd && in.dueDays == 1 && in.dueHour == 9 && in.label == L"Water plants");
    in = parse(L"remind me in an hour to check the oven");
    CHECK(in.action == as::Action::TodoAdd && in.dueInMinutes == 60 && in.label == L"Check the oven");
    CHECK(parse(L"remind me in 10 min").action == as::Action::TimerStart);  // nothing to be reminded of: a timer
    CHECK(parse(L"what's on my list?").action == as::Action::TodoList);
    in = parse(L"done buy milk");
    CHECK(in.action == as::Action::TodoDone && in.label == L"buy milk");
    in = parse(L"mark call mom as done");
    CHECK(in.action == as::Action::TodoDone && in.label == L"call mom");
    CHECK(parse(L"clear done").action == as::Action::TodoClearDone);
    // Undo, radios, brightness
    CHECK(parse(L"undo").action == as::Action::Undo);
    CHECK(parse(L"turn wifi off").action == as::Action::WifiOff);
    CHECK(parse(L"wifi on").action == as::Action::WifiOn);
    CHECK(parse(L"disable bluetooth").action == as::Action::BluetoothOff);
    CHECK(parse(L"turn on bluetooth please").action == as::Action::BluetoothOn);
    CHECK(parse(L"wifi settings").action == as::Action::Open);  // the Settings page, as before
    in = parse(L"brightness 60");
    CHECK(in.action == as::Action::BrightnessSet && in.number == 60);
    CHECK(parse(L"brighter").action == as::Action::BrightnessUp);
    CHECK(parse(L"dim the screen").action == as::Action::BrightnessDown);
    // Weather: here, a named place, tomorrow
    in = parse(L"weather");
    CHECK(in.info == as::Info::Weather && in.target.empty() && in.number == 0);
    in = parse(L"today's weather in mumbai");
    CHECK(in.info == as::Info::Weather && in.target == L"mumbai" && in.number == 0);
    CHECK(parse(L"new york weather").target == L"new york");
    in = parse(L"will it rain in pune tomorrow?");
    CHECK(in.info == as::Info::Weather && in.target == L"pune" && in.number == 1);
    CHECK(parse(L"what's the weather like here").target.empty());
    // Fuzzy app names
    CHECK(as::AppMatchScore(L"Visual Studio Code", L"vsc") >= 60);
    CHECK(as::AppMatchScore(L"Visual Studio Code", L"code") >= 70);
    CHECK(as::AppMatchScore(L"Spotify", L"spot") > as::AppMatchScore(L"Microsoft Teams", L"spot"));
    CHECK(as::AppMatchScore(L"Spotify", L"spfy") > 0);
    CHECK(as::AppMatchScore(L"Notepad", L"notepad") == 100);
    CHECK(as::AppMatchScore(L"Calculator", L"zz") == 0);
    // Focus mode
    CHECK(parse(L"focus").action == as::Action::FocusStart && parse(L"focus").number == 25);
    CHECK(parse(L"focus 50").number == 50);
    CHECK(parse(L"start focus for 45 minutes").number == 45);
    CHECK(parse(L"stop focus").action == as::Action::FocusStop);
    CHECK(parse(L"open focus settings").action == as::Action::Open);
    // Repeating reminders
    in = parse(L"remind me every day at 9 to stand up");
    CHECK(in.action == as::Action::TodoAdd && in.repeat == L"daily" && in.dueHour == 9 && in.label == L"Stand up");
    in = parse(L"remind me every Monday at 10 to send the report");
    CHECK(in.action == as::Action::TodoAdd && in.repeat == L"weekly:1" && in.dueHour == 10 && in.label == L"Send the report");
    in = parse(L"remind me every mon and wed at 6pm to go to the gym");
    CHECK(in.repeat == L"weekly:1,3" && in.dueHour == 18 && in.label == L"Go to the gym");
    in = parse(L"remind me every weekday at 9:30 to check email");
    CHECK(in.repeat == L"weekdays" && in.dueHour == 9 && in.dueMinute == 30 && in.label == L"Check email");
    in = parse(L"remind me every morning to drink water");
    CHECK(in.repeat == L"daily" && in.dueHour == 9 && in.label == L"Drink water");
    in = parse(L"remind me to water plants on fridays");
    CHECK(in.repeat == L"weekly:5" && in.label == L"Water plants");
    CHECK(parse(L"pause").action == as::Action::MediaToggle);
    in = parse(L"set a timer for 10 minutes");
    CHECK(in.action == as::Action::TimerStart && in.number == 10);
    CHECK(parse(L"timer 1 hour").number == 60);
    CHECK(parse(L"timer 5min").number == 5);
    CHECK(parse(L"pomodoro timer").number == 25);
    CHECK(parse(L"stop the timer").action == as::Action::TimerStop);
    in = parse(L"search cute bunnies");
    CHECK(in.action == as::Action::WebSearch && in.target == L"https://www.google.com/search?q=cute+bunnies");
    CHECK(as::Parse(L"search a&b", L"duckduckgo").target == L"https://duckduckgo.com/?q=a%26b");
    CHECK(parse(L"what time is it").info == as::Info::Time);
    CHECK(parse(L"battery").info == as::Info::Battery);
    CHECK(parse(L"battery settings").action == as::Action::Open);
    CHECK(parse(L"what's playing").info == as::Info::NowPlaying);
    CHECK(parse(L"lock").action == as::Action::Lock);
    CHECK(parse(L"gibberish words").action == as::Action::None);
}

// Version comparison decides whether "an update is available"; a wrong answer
// either nags about old versions or hides real updates.
void TestUpdateVersions() {
    using bb::update::CompareVersions;
    using bb::update::PackVersion;
    CHECK(CompareVersions("0.2.0", "0.1.0") > 0);
    CHECK(CompareVersions("v0.1.0", "0.1.0") == 0);
    CHECK(CompareVersions("0.1", "0.1.0") == 0);
    CHECK(CompareVersions("0.10.0", "0.9.9") > 0);  // numeric, not string order
    CHECK(CompareVersions("1.0.0", "0.99.99") > 0);
    CHECK(CompareVersions("0.2.0-beta", "0.2.0") < 0);
    CHECK(CompareVersions("0.2.0-beta", "0.1.9") > 0);
    CHECK(CompareVersions("0.1.0", "0.1.1") < 0);
    CHECK(PackVersion("v1.2.3") == 1002003);
    CHECK(PackVersion("0.10") == 10000);
}

// The to-do file and repeat maths shared by the island and the settings app.
void TestTodos() {
    namespace td = bb::todos;
    // Old files (no repeat column) still load; new ones round-trip.
    auto items = td::Parse(L"1\t0\t0\t0\tBuy milk\n2\t1\t1700000000\t1\tCall Sam\n");
    CHECK(items.size() == 2 && items[0].text == L"Buy milk" && items[0].repeat.empty() && items[1].done);
    td::Item r;
    r.id = 3;
    r.text = L"Stand up";
    r.due = 1700000000;
    r.repeat = L"weekdays";
    items.push_back(r);
    const auto back = td::Parse(td::Serialize(items));
    CHECK(back.size() == 3 && back[2].repeat == L"weekdays" && back[2].text == L"Stand up" && td::NextId(back) == 4);
    CHECK(td::RepeatDays(L"weekdays") == 0x3E && td::RepeatDays(L"weekly:1,3") == 0x0A && td::RepeatDays(L"") == 0);
    CHECK(td::DescribeRepeat(L"weekly:1,3") == L"Every Mon, Wed");

    // Daily: the next one is a day later at the same local time.
    SYSTEMTIME t{};
    t.wYear = 2026; t.wMonth = 11; t.wDay = 2; t.wHour = 9;  // Monday 2 Nov 2026, 9:00 local
    const double monday9 = td::FromLocal(t);
    double next = td::NextOccurrence(L"daily", monday9, monday9);
    SYSTEMTIME n = td::ToLocal(next);
    CHECK(n.wDay == 3 && n.wHour == 9 && n.wMinute == 0);
    // Weekdays from Friday 9:00 skips to Monday.
    t.wDay = 6;  // Friday
    next = td::NextOccurrence(L"weekdays", td::FromLocal(t), td::FromLocal(t));
    n = td::ToLocal(next);
    CHECK(n.wDay == 9 && n.wDayOfWeek == 1 && n.wHour == 9);
    // Weekly on Wednesday, asked on Monday morning before 9: this Wednesday.
    t.wDay = 2; t.wHour = 8;
    next = td::NextOccurrence(L"weekly:3", monday9, td::FromLocal(t));
    n = td::ToLocal(next);
    CHECK(n.wDay == 4 && n.wDayOfWeek == 3 && n.wHour == 9);
    CHECK(td::NextOccurrence(L"", monday9, monday9) == 0.0);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::printf("usage: test_core <scratch dir>\n");
        return 2;
    }
    TestJson();
    TestInstaller(argv[1]);
    TestAssistant();
    TestUpdateVersions();
    TestTodos();
    std::printf(g_failed ? "%d check(s) failed\n" : "all checks passed\n", g_failed);
    return g_failed ? 1 : 0;
}
