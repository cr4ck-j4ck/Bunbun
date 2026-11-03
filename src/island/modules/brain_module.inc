// Bunbun: Bun's optional local AI (docs/local-ai.md).
//
// When Ask Bun's parser doesn't understand a question and Local AI is on, the
// question goes to bunbun-brain.exe: a helper next to bunbun.exe that runs a
// small language model offline with llama.cpp. The island starts it as a child
// process (only this island can talk to it, over its stdin / stdout), in a job
// object so it dies with the island, and closes it after a few idle minutes to
// free the memory.
//
// The model answers in one of two shapes (a grammar forces it):
//   SAY: <answer>   streamed into the Ask Bun card as it's written
//   RUN: <command>  an ordinary Ask Bun command, run through RunBun, so the
//                   model can only do what typing could, and "undo" works.
// A RUN the parser doesn't recognise, or that fails, is retried once in
// "say only" mode. Lock and screenshot are never run on the model's word.
//
// Included in the core's anonymous namespace after RunBun.

constexpr UINT WM_APP_BRAIN = WM_APP + 0x4B1;  // the reader thread hands a finished reply to the window thread

namespace Brain {

std::mutex g_mutex;                 // guards the process handles and the request below
HANDLE g_process = nullptr, g_job = nullptr, g_in = nullptr, g_out = nullptr;
std::thread g_reader;
double g_lastUse = 0.0;
int g_nextId = 1;

// The request in flight.
struct Pending {
    int id = 0;
    uint64_t lookupId = 0;    // the Ask Bun card it belongs to
    std::wstring question;
    std::string text;         // the reply so far (UTF-8)
    bool sayOnly = false;     // the second try
    bool answering = false;   // writing the answer from a plan's lookups
    std::wstring prefix;      // shown above the reply: what the plan already did
    bool finished = false;
    std::wstring error;
};
Pending g_pending;
std::atomic<uint64_t> g_busyLookup{0};  // the Ask Bun card a reply is being written for (0: none)

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path, n);
    return dir.substr(0, dir.find_last_of(L"\\/"));
}
std::wstring HelperPath() {
    return ExeDir() + L"\\bunbun-brain.exe";
}
std::wstring ModelsFolder() {
    PWSTR local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) dir = std::wstring(local) + L"\\Bunbun\\models";
    CoTaskMemFree(local);
    return dir;
}

// The model to load: the one chosen in settings, else the first .gguf in the
// models folder (alphabetically, so it's stable).
std::wstring ModelPath(const Settings& settings) {
    if (!settings.localAiModel.empty() && GetFileAttributesW(settings.localAiModel.c_str()) != INVALID_FILE_ATTRIBUTES)
        return settings.localAiModel;
    std::vector<std::wstring> found;
    WIN32_FIND_DATAW fd;
    const std::wstring dir = ModelsFolder();
    HANDLE h = FindFirstFileW((dir + L"\\*.gguf").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    do {
        found.push_back(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(found.begin(), found.end());
    return found.empty() ? std::wstring() : found.front();
}

std::string Utf8(const std::wstring& w) {
    return bb::todos::ToUtf8(w);
}
std::wstring Wide(const std::string& s) {
    return bb::todos::FromUtf8(s);
}

// ── The prompt ──────────────────────────────────────────────────────────────
// Instructions and worked examples first (identical every time, so the helper
// keeps them in memory between questions), then the live context and question.
constexpr const char* kSystem =
    "You are Bun, a small friendly assistant and agent living in the Dynamic Island of a Windows laptop. You run offline on this PC.\n"
    "Reply in one of two forms:\n"
    "SAY: <a short, friendly answer>   (at most 3 sentences) when nothing needs doing\n"
    "or a plan: one to five lines RUN: <Bun command>, one step per line, optionally ending with SAY: <a short note>.\n"
    "Use a plan when the user wants things done on the PC, or when you need to look something up first.\n"
    "Bun commands (use these exact shapes, with real clock times like 2:55pm):\n"
    "open <app> | volume <0-100> | volume up | volume down | mute | unmute | play | pause | next song | previous song\n"
    "timer <N> min | focus <N> | stop focus | add <to-do> | remind me at <time> to <thing> | remind me in <N> min to <thing>\n"
    "remind me every day at <time> to <thing> | remind me every <weekday> at <time> to <thing> | done <to-do>\n"
    "wifi on | wifi off | bluetooth on | bluetooth off | brightness <0-100> | brighter | dim the screen | dnd on | dnd off\n"
    "Lookups (you get the results back, then answer): search <topic> | wiki <topic> | weather | weather in <city> | weather tomorrow | battery | cpu |\n"
    "what's playing | my list | what time is it\n"
    "Questions about the user's day (time, meetings, to-dos, music) are answered with SAY using the facts given.\n"
    "Answer calculations, arithmetic, and general reasoning directly with SAY.\n"
    "Never invent facts. If you don't know, say so.";

const std::pair<const char*, const char*> kExamples[] = {
    {"remind me before my 3pm meeting to grab my charger", "RUN: remind me at 2:55pm to grab my charger"},
    {"it's way too loud", "RUN: volume down"},
    {"what should I name my grey cat?", "SAY: How about Smokey, Ash or Pebble? Smokey suits a grey cat nicely."},
    {"what is 45 times 12?", "SAY: 45 times 12 is 540."},
    {"I need to concentrate for an hour", "RUN: focus 60"},
    {"who are you?", "SAY: I'm Bun, your little assistant in the island. I can set reminders, control music and volume, and answer quick questions."},
    {"put eggs on my shopping list", "RUN: add buy eggs"},
    {"[Now: Monday 9:10am. Next meeting: Design review at 11:00am.] when's my next meeting?", "SAY: Your next meeting is Design review at 11:00am."},
    {"explain what a dynamic island is", "SAY: It's a pill at the top of the screen that grows to show what's going on: music, timers, calls and alerts."},
    {"when is diwali in 2026?", "RUN: search diwali 2026 date"},
    {"[Results for: when is diwali in 2026?]\n- Web Search: Diwali 2026 falls on Sunday, 8 November (Lakshmi Puja).\nAnswer with SAY.",
     "SAY: Diwali in 2026 is celebrated on Sunday, November 8."},
    {"who walked in victoria's secret 2026?", "RUN: search victoria's secret 2026"},
    {"[Results for: who walked in victoria's secret 2026?]\n- Wikipedia (Victoria's Secret Fashion Show 2026): The show featured models Gigi Hadid, Bella Hadid, Adriana Lima, and Kylie Jenner.\nAnswer with SAY.",
     "SAY: The 2026 Victoria's Secret show featured stars including Kylie Jenner, Bella Hadid, Gigi Hadid, and Adriana Lima."},
    // Agent: several steps at once, and lookups whose results come back for the answer.
    {"get me ready for a meeting", "RUN: dnd on\nRUN: volume 20\nRUN: open teams\nSAY: You're all set. Good luck!"},
    {"wind down for the night", "RUN: dim the screen\nRUN: volume 15\nRUN: dnd on\nSAY: Lights low and quiet. Sleep well!"},
    {"is it warmer in delhi or mumbai right now?", "RUN: weather in delhi\nRUN: weather in mumbai"},
    {"[Results for: is it warmer in delhi or mumbai right now?]\n- weather in delhi: 34°C and Sunny.\n- weather in mumbai: 29°C and Light rain.\nAnswer with SAY.",
     "SAY: Delhi is warmer: 34°C and sunny, while Mumbai is 29°C with light rain."},
    {"tell me about the james webb telescope", "RUN: wiki james webb space telescope"},
    {"[Results for: tell me about the james webb telescope]\n- wiki james webb space telescope: James Webb Space Telescope (Wikipedia): The James Webb Space Telescope is a space telescope designed primarily to conduct infrared astronomy.\nAnswer with SAY.",
     "SAY: The James Webb Space Telescope is an advanced space observatory that conducts infrared astronomy to study the early universe and distant galaxies."},
};

// What Bun knows right now, for questions about the user's day.
std::wstring Context() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t date[64] = {}, clock[32] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, date, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, clock, 32);
    std::wstring c = std::wstring(date) + L", " + clock + L".";
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps) && ps.BatteryFlag != 128 && ps.BatteryLifePercent <= 100)
        c += L" Battery " + std::to_wstring(ps.BatteryLifePercent) + L"%" + (ps.ACLineStatus == 1 ? L", plugged in." : L".");
    std::lock_guard lock(g_stateMutex);
    const SharedState& s = g_state;
    if (s.weather.hasData) {
        wchar_t w[96];
        swprintf_s(w, L" Weather %.0f°C, %ls.", s.weather.temperature, s.weather.weatherDesc.c_str());
        c += w;
    }
    if (s.media.available && !s.media.title.empty())
        c += std::wstring(s.media.playing ? L" Playing: " : L" Paused: ") + s.media.title + (s.media.artist.empty() ? L"" : L" by " + s.media.artist) + L".";
    std::wstring todos;
    int n = 0;
    for (const TodoItem& t : s.todos) {
        if (t.done || n >= 5) continue;
        todos += (n++ ? L"; " : L" ") + t.text + (t.due > 0.0 ? L" (" + Todos::FormatDue(t.due) + L")" : L"");
    }
    c += n ? L" Open to-dos:" + todos + L"." : L" No open to-dos.";
    if (s.meeting.valid) c += L" Next meeting: " + s.meeting.title + L" at " + Todos::FormatDue(s.meeting.startsAt) + L".";
    return c;
}

// `results`: the lookups a plan ran ("- weather in pune: 31°C…" lines). Then
// the model only writes the answer, from those results.
std::string Request(int id, uint64_t lookupId, const std::wstring& question, bool sayOnly, const std::wstring& results = {}) {
    using bb::json::Value;
    Value msgs = Value::Array();
    auto add = [&](const char* role, const std::string& content) {
        Value m = Value::Object();
        m["role"] = Value::String(role);
        m["content"] = Value::String(content);
        msgs.push(std::move(m));
    };
    if (!results.empty()) {
        // Lean, lightning-fast grounded prompt (evaluates in ~30ms instead of 1000ms)
        add("system", "You are Bun, the little assistant inside the user's desktop dynamic island.\n"
                      "Answer the question concisely in 1-2 sentences using only the search results provided.\n"
                      "Always answer directly with SAY: <answer>.");
        add("user", "[Results for: when is diwali in 2026?]\n- Web Search: Diwali 2026 is celebrated on Sunday, November 8, 2026.\nAnswer with SAY.");
        add("assistant", "SAY: Diwali in 2026 is celebrated on Sunday, November 8.");
        add("user", "[Results for: " + Utf8(question) + "]\n" + Utf8(results) + "Answer with SAY.");
        Value req = Value::Object();
        req["id"] = Value::Number(id);
        req["messages"] = std::move(msgs);
        req["max_tokens"] = Value::Number(90);
        req["mode"] = Value::String("say");
        return bb::json::Serialize(req) + "\n";
    }

    add("system", kSystem);
    for (const auto& [q, a] : kExamples) {
        add("user", q);
        add("assistant", a);
    }
    // Multi-turn conversational context: include up to 3 previous completed turns from g_chat
    const size_t nTurns = g_chat.size();
    const size_t startTurn = (nTurns > 3) ? (nTurns - 3) : 0;
    for (size_t k = startTurn; k < nTurns; ++k) {
        const auto& t = g_chat[k];
        if (t.seq == lookupId) continue;
        if (t.question.empty() || t.answer.body.empty() || t.answer.status != LookupStatus::Found) continue;
        add("user", Utf8(t.question));
        add("assistant", "SAY: " + Utf8(t.answer.body));
    }
    add("user", "[Now: " + Utf8(Context()) + "] " + Utf8(question) + (sayOnly ? " (Answer in words.)" : ""));
    Value req = Value::Object();
    req["id"] = Value::Number(id);
    req["messages"] = std::move(msgs);
    req["max_tokens"] = Value::Number(120);
    if (sayOnly) req["mode"] = Value::String("say");
    return bb::json::Serialize(req) + "\n";
}

// ── The card ────────────────────────────────────────────────────────────────
// Shows the reply so far in the Ask Bun card, if that card is still the one on
// screen. "RUN: ..." shows a "working on it" line until the command has run.
// `talking`: the answer is still being written (the mascot talks); `confused`:
// no answer came (the mascot looks puzzled).
void ShowInCard(uint64_t lookupId, const std::wstring& body, bool final, bool talking = false, bool confused = false) {
    {
        std::lock_guard lock(g_stateMutex);
        LookupSnapshot& lk = g_state.lookup;
        if (g_lookupRequestSeq.load() != lookupId || !lk.active) return;
        lk.status = LookupStatus::Found;
        lk.body = body;
        lk.talking = talking && !final;
        lk.confused = confused;
        lk.source = L"Bun";
        lk.subtitle = L"Bun · local AI";
        // Keep the card up while it's being written, and long enough to read after.
        lk.expiresAt = std::max(lk.expiresAt, NowSeconds() + (final ? QuickLookup::kResultSeconds : 30.0));
    }
    if (!g_chat.empty() && g_chat.back().seq == lookupId) {
        g_chat.back().answer.body = body;
        g_chat.back().answer.status = LookupStatus::Found;
    }
    g_layoutDirty = true;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
}

// The reply so far, as the card shows it: the answer, or the plan's steps as
// they're written ("• dnd on"), then its closing note.
std::wstring Visible(const std::string& text) {
    if (text.rfind("SAY: ", 0) == 0) return Wide(text.substr(5));
    if (text.rfind("RUN: ", 0) != 0) return Loc(L"Thinking…");
    std::wstring out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        const bool last = end == std::string::npos;
        const std::string line = text.substr(pos, last ? std::string::npos : end - pos);
        if (line.rfind("SAY: ", 0) == 0) {
            out += (out.empty() ? L"" : L"\n") + Wide(text.substr(pos + 5));  // the note may span lines
            break;
        }
        if (line.rfind("RUN: ", 0) == 0) out += (out.empty() ? L"• " : L"\n• ") + Wide(line.substr(5));
        if (last) break;
        pos = end + 1;
    }
    return out;
}

// ── The helper process ──────────────────────────────────────────────────────
void ReaderLoop(HANDLE out) {
    std::string pending;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(out, buf, sizeof(buf), &read, nullptr) && read > 0) {
        pending.append(buf, read);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            bb::json::Value v;
            if (!bb::json::Parse(line, &v) || !v.isObject()) continue;
            uint64_t lookupId = 0;
            std::wstring show;
            bool done = false;
            bool talking = false;
            {
                std::lock_guard lock(g_mutex);
                if (v.find("ready")) continue;
                if (const bb::json::Value* e = v.find("error"); e && (!v.find("id") || static_cast<int>(v.getNumber("id")) == g_pending.id)) {
                    g_pending.error = Wide(e->str);
                    g_pending.finished = true;
                    done = true;
                } else if (static_cast<int>(v.getNumber("id")) != g_pending.id) {
                    continue;  // a reply to a question that was replaced
                } else if (const bb::json::Value* t = v.find("t")) {
                    g_pending.text += t->str;
                } else if (v.getBool("done")) {
                    g_pending.finished = true;
                    done = true;
                }
                lookupId = g_pending.lookupId;
                show = g_pending.prefix + Visible(g_pending.text);
                talking = g_pending.text.rfind("SAY: ", 0) == 0 || g_pending.text.find("\nSAY: ") != std::string::npos;
            }
            if (done) {
                if (g_hwnd) PostMessageW(g_hwnd, WM_APP_BRAIN, 0, 0);  // RUN / errors finish on the window thread
            } else {
                ShowInCard(lookupId, show, false, talking);
            }
        }
    }
}

void Stop() {
    HANDLE process = nullptr, in = nullptr, out = nullptr, job = nullptr;
    std::thread reader;
    {
        std::lock_guard lock(g_mutex);
        process = std::exchange(g_process, nullptr);
        in = std::exchange(g_in, nullptr);
        out = std::exchange(g_out, nullptr);
        job = std::exchange(g_job, nullptr);
        reader = std::move(g_reader);
    }
    if (in) CloseHandle(in);  // end of input: the helper exits on its own
    if (process && WaitForSingleObject(process, 2000) == WAIT_TIMEOUT) TerminateProcess(process, 0);
    if (reader.joinable()) reader.join();
    for (HANDLE h : {process, out, job})
        if (h) CloseHandle(h);
}

bool Start(const std::wstring& model) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE childIn = nullptr, ourIn = nullptr, ourOut = nullptr, childOut = nullptr;
    if (!CreatePipe(&childIn, &ourIn, &sa, 0)) return false;
    if (!CreatePipe(&ourOut, &childOut, &sa, 0)) {
        CloseHandle(childIn);
        CloseHandle(ourIn);
        return false;
    }
    SetHandleInformation(ourIn, HANDLE_FLAG_INHERIT, 0);  // our ends stay ours
    SetHandleInformation(ourOut, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = childIn;
    si.hStdOutput = childOut;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + HelperPath() + L"\" --model \"" + model + L"\"";
    const bool ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                   ExeDir().c_str(), &si, &pi) != FALSE;
    CloseHandle(childIn);
    CloseHandle(childOut);
    if (!ok) {
        CloseHandle(ourIn);
        CloseHandle(ourOut);
        return false;
    }
    // A job that ends the helper when the island exits, however it exits.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
        AssignProcessToJobObject(job, pi.hProcess);
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    std::lock_guard lock(g_mutex);
    g_process = pi.hProcess;
    g_job = job;
    g_in = ourIn;
    g_out = ourOut;
    g_reader = std::thread(ReaderLoop, ourOut);
    Wh_Log(L"Local AI: helper started.");
    return true;
}

bool Running() {
    std::lock_guard lock(g_mutex);
    return g_process && WaitForSingleObject(g_process, 0) == WAIT_TIMEOUT;
}

bool Send(const std::string& line) {
    std::lock_guard lock(g_mutex);
    if (!g_in) return false;
    DWORD written = 0;
    return WriteFile(g_in, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) && written == line.size();
}

// Pre-warms the local AI helper in the background so the model is loaded when the user enters text
void Warmup() {
    const Settings settings = GetSettingsCopy();
    if (!settings.localAi) return;
    const std::wstring model = ModelPath(settings);
    if (model.empty() || GetFileAttributesW(HelperPath().c_str()) == INVALID_FILE_ATTRIBUTES) return;
    if (!Running()) {
        Start(model);
    }
}

// Starts a question (or its "say only" retry). Window thread.
bool Ask(const std::wstring& question, uint64_t lookupId, bool sayOnly, const std::wstring& results = {},
         const std::wstring& prefix = {}) {
    const Settings settings = GetSettingsCopy();
    const std::wstring model = ModelPath(settings);
    if (model.empty() || GetFileAttributesW(HelperPath().c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    if (!Running()) {
        Stop();
        if (!Start(model)) return false;
    }
    int id = 0;
    {
        std::lock_guard lock(g_mutex);
        id = g_nextId++;
        g_pending = Pending{};
        g_pending.id = id;
        g_pending.lookupId = lookupId;
        g_busyLookup = lookupId;
        g_pending.question = question;
        g_pending.sayOnly = sayOnly;
        g_pending.answering = !results.empty();
        g_pending.prefix = prefix;
        g_lastUse = NowSeconds();
    }
    if (!Send(Request(id, lookupId, question, sayOnly, results))) {
        Stop();
        return false;
    }
    return true;
}

}  // namespace Brain

void BrainWarmup() {
    Brain::Warmup();
}

// Ask Bun: a question the parser didn't understand. Returns false when Local AI
// is off or not set up (no helper, no model), so the usual reply is shown.
bool BrainTryAnswer(const std::wstring& question, uint64_t lookupId) {
    const Settings settings = GetSettingsCopy();
    if (!settings.localAi) return false;
    const std::wstring model = Brain::ModelPath(settings);
    if (model.empty() || GetFileAttributesW(Brain::HelperPath().c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    Brain::ShowInCard(lookupId, Loc(L"Thinking…"), false, true);
    Brain::g_busyLookup = lookupId;

    const std::wstring lower = ToLowerCopy(question);
    const bool isGreeting = (lower == L"hi" || lower == L"hello" || lower == L"hey" ||
                             lower == L"who are you" || lower == L"who are you?" ||
                             lower == L"tell me a joke" || lower == L"tell a joke");

    const bool isCreativeOrMath = (lower.find(L"haiku") != std::wstring::npos ||
                                   lower.find(L"poem") != std::wstring::npos ||
                                   lower.find(L"story") != std::wstring::npos ||
                                   lower.find(L"joke") != std::wstring::npos ||
                                   lower.find(L"advice") != std::wstring::npos ||
                                   lower.find(L"calc") != std::wstring::npos ||
                                   lower.find(L"sqrt(") != std::wstring::npos);

    if (isGreeting || isCreativeOrMath) {
        if (!Brain::Ask(question, lookupId, false)) return false;
        return true;
    }

    std::thread([question, lookupId]() {
        std::wstring evidence = QuickLookup::FetchLiveSearchEvidence(question);
        if (!evidence.empty()) {
            Brain::Ask(question, lookupId, true, evidence);
        } else {
            Brain::Ask(question, lookupId, false);
        }
    }).detach();

    return true;
}

namespace Brain {
// A plan in progress: what it did, what its lookups found, and its closing note.
// Weather elsewhere is fetched on a worker; the plan waits here until then.
struct PlanState {
    uint64_t lookupId = 0;
    std::wstring question;
    std::wstring done;     // "✓ Do not disturb is on\n" lines, shown above the answer
    std::wstring results;  // "- weather in pune: …\n" lines, for the model to answer from
    std::wstring note;     // the plan's own SAY, if it had one
    bool ready = false;
};
PlanState g_plan;  // g_mutex
}  // namespace Brain

// Ends a plan: with lookups, the model answers from their results; otherwise the
// card shows what was done and the plan's note. Window thread.
void BrainFinishPlan(const Brain::PlanState& plan) {
    if (!plan.results.empty() && Brain::Ask(plan.question, plan.lookupId, true, plan.results, plan.done)) return;
    std::wstring body = plan.done + (plan.note.empty() ? plan.results : plan.note);
    while (!body.empty() && (body.back() == L'\n' || body.back() == L' ')) body.pop_back();
    if (body.empty()) body = Loc(L"Done.");
    Brain::ShowInCard(plan.lookupId, body, true);
    if (plan.results.empty() && plan.note.empty()) {
        // Only commands: like typed ones, the card goes home soon after.
        std::lock_guard lock(g_stateMutex);
        if (g_lookupRequestSeq.load() == plan.lookupId) g_state.lookup.expiresAt = NowSeconds() + 3.0;
    }
}

// WM_APP_BRAIN: a reply is complete (wParam 0), or a plan's web lookups came
// back (wParam 1). Window thread: plans run their steps through Ask Bun here.
void BrainOnReply(WPARAM what) {
    if (what == 1) {
        Brain::PlanState plan;
        {
            std::lock_guard lock(Brain::g_mutex);
            if (!Brain::g_plan.ready) return;
            plan = Brain::g_plan;
            Brain::g_plan = Brain::PlanState{};
            Brain::g_busyLookup = 0;
        }
        BrainFinishPlan(plan);
        return;
    }
    Brain::Pending p;
    {
        std::lock_guard lock(Brain::g_mutex);
        if (!Brain::g_pending.finished) return;
        p = Brain::g_pending;
        Brain::g_pending.finished = false;
        Brain::g_busyLookup = 0;
        Brain::g_lastUse = NowSeconds();
    }
    if (!p.error.empty()) {
        const uint64_t lookupId = p.lookupId;
        const std::wstring q = p.question;
        std::thread([lookupId, q, err = p.error]() {
            QuickLookup::Result r;
            const QuickLookup::Outcome outcome = QuickLookup::LookupText(q, &r);
            if (outcome == QuickLookup::Outcome::Found) {
                QuickLookup::ApplyResult(lookupId, q, true, r);
            } else {
                Brain::ShowInCard(lookupId, L"Local AI: " + err, true, false, true);
            }
        }).detach();
        return;
    }
    if (p.answering || p.text.rfind("RUN: ", 0) != 0) {
        Brain::ShowInCard(p.lookupId, p.prefix + Brain::Visible(p.text), true);
        return;
    }

    // A plan: up to five "RUN:" lines, then maybe "SAY: <note>".
    namespace as = bb::assistant;
    const std::wstring engine = ToLowerCopy(GetStringSettingCopy(L"Bunbun.SearchEngine"));
    std::vector<std::pair<std::wstring, as::Intent>> steps;
    Brain::PlanState plan;
    plan.lookupId = p.lookupId;
    plan.question = p.question;
    for (size_t pos = 0; pos < p.text.size();) {
        const size_t end = p.text.find('\n', pos);
        const std::string line = p.text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (line.rfind("SAY: ", 0) == 0) {
            plan.note = Brain::Wide(p.text.substr(pos + 5));
            break;
        }
        if (line.rfind("RUN: ", 0) == 0 && steps.size() < 5) {
            const std::wstring command = Brain::Wide(line.substr(5));
            const as::Intent in = as::Parse(command, engine);
            // Only real commands, and none that shouldn't run on the model's word.
            const bool blocked = in.action == as::Action::Lock || in.action == as::Action::Screenshot ||
                                 in.action == as::Action::Undo;
            if (in.action != as::Action::None && in.action != as::Action::Reply && !blocked) steps.emplace_back(command, in);
        }
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    if (steps.empty()) {  // nothing it could actually do: answer in words instead
        if (p.sayOnly || !Brain::Ask(p.question, p.lookupId, true))
            Brain::ShowInCard(p.lookupId, Loc(L"I'm not sure how to help with that one."), true, false, true);
        return;
    }

    struct Fetch {
        std::wstring command;
        std::wstring place;
        int days = 0;
        bool isLookup = false;
        std::wstring topic;
    };
    std::vector<Fetch> fetches;
    for (const auto& [command, in] : steps) {
        const QuickLookup::Result r = RunBun(command);
        // "open <nonsense>" as the whole plan: it misread the question, so answer in words.
        if (steps.size() == 1 && !r.command && in.action == as::Action::Open && !p.sayOnly &&
            Brain::Ask(p.question, p.lookupId, true))
            return;
        if (r.weatherFetch) {
            fetches.push_back({command, r.weatherPlace, r.weatherDays, false, L""});
        } else if (r.lookupFetch) {
            fetches.push_back({command, L"", 0, true, r.lookupTopic});
        } else if (in.action == as::Action::WebSearch) {
            fetches.push_back({command, L"", 0, true, in.label.empty() ? command : in.label});
        } else if (in.action == as::Action::Info || in.action == as::Action::TodoList) {
            plan.results += L"- " + command + L": " + r.body + L"\n";
        } else {
            plan.done += (r.command ? L"✓ " : L"✗ ") + r.body + L"\n";
        }
    }
    if (fetches.empty()) {
        BrainFinishPlan(plan);
        return;
    }
    // Weather or lookups elsewhere come from the network: fetch on a worker, then finish
    // back here (WM_APP_BRAIN, wParam 1). The mascot thinks meanwhile.
    Brain::ShowInCard(p.lookupId, plan.done + Loc(L"Looking it up…"), false);
    Brain::g_busyLookup = p.lookupId;
    std::thread([plan, fetches]() mutable {
        for (const Fetch& f : fetches) {
            if (f.isLookup) {
                QuickLookup::Result lr;
                const QuickLookup::Outcome outcome = QuickLookup::LookupText(f.topic, &lr);
                if (outcome == QuickLookup::Outcome::Found) {
                    plan.results += L"- " + f.command + L": " + lr.title + L" (" + lr.source + L"): " + lr.body + L"\n";
                    plan.done += L"✓ Looked up " + lr.title + L"\n";
                } else {
                    plan.results += L"- " + f.command + L": No result found for " + f.topic + L".\n";
                    plan.done += L"✗ No entry found for " + f.topic + L"\n";
                }
            } else {
                std::wstring title, subtitle, body;
                plan.results += L"- " + f.command + L": " +
                                (FetchWeatherReport(f.place, f.days, &title, &subtitle, &body) ? title + L": " + body
                                                                                               : std::wstring(L"couldn't get the weather")) +
                                L"\n";
                if (!title.empty()) plan.done += L"✓ Weather for " + title + L"\n";
            }
        }
        {
            std::lock_guard lock(Brain::g_mutex);
            Brain::g_plan = std::move(plan);
            Brain::g_plan.ready = true;
        }
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_BRAIN, 1, 0);
    }).detach();
}

// Frees the model's memory after the idle time set in Settings. Render loop.
void BrainIdleCheck(const Settings& settings) {
    double last = 0.0;
    {
        std::lock_guard lock(Brain::g_mutex);
        if (!Brain::g_process) return;
        last = Brain::g_lastUse;
    }
    const double idle = std::max(1, settings.localAiIdleMinutes) * 60.0;
    if (!settings.localAi || NowSeconds() - last > idle) {
        Brain::Stop();
        Wh_Log(L"Local AI: helper closed (idle).");
    }
}

void BrainShutdown() {
    Brain::Stop();
}

// True while the local AI is writing a reply for the Ask Bun card on screen,
// so the mascot in the box can look thoughtful.
bool BrainBusyForCurrentCard() {
    const uint64_t id = Brain::g_busyLookup.load();
    return id != 0 && id == g_lookupRequestSeq.load();
}
