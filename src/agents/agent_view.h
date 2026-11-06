// Bunbun — what the island needs to know about coding-agent sessions.
//
// The agent bridge (agent_bridge.cpp) owns the real session table and publishes
// one AgentView, the session most worth showing, into the island's SharedState.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bb {

enum class AgentPhase {
    None,       // nothing to show
    Thinking,   // prompt submitted, no tool yet
    Working,    // running tools
    Approval,   // PermissionRequest waiting for Allow / Deny
    Question,   // AskUserQuestion waiting for an answer
    Finished,   // Stop: brief celebration, then hides
    Error,      // StopFailure / rate limit
};

struct AgentView {
    AgentPhase phase = AgentPhase::None;
    std::wstring agent;      // "Claude Code", "Codex", ...
    std::wstring project;    // last path component of cwd
    std::wstring step;       // current ticker line ("Edits main.cpp")
    std::wstring detail;     // approval body: command, file, or question text
    std::wstring tool;       // approval headline ("Wants to run a command")
    int added = -1;          // last diff counts, -1 when unknown
    int removed = -1;
    std::wstring diffOld;    // compact snippet of deleted lines
    std::wstring diffNew;    // compact snippet of added lines
    uint64_t requestId = 0;  // pending approval / question id
    int queued = 0;          // further requests waiting behind this one
    int sessions = 0;        // sessions currently working
    std::vector<std::wstring> options;  // AskUserQuestion labels (single question)
    double phaseSince = 0;   // NowSeconds() when the phase began
    uint64_t generation = 0; // bumps on every change, for redraw detection

    // ── Studio agent page ──
    double turnStart = 0;    // NowSeconds() when the current prompt was sent (0 = unknown)
    double turnSeconds = 0;  // how long the last finished turn took
    int stage = -1;          // furthest step this turn: 0 read, 1 edit, 2 run/test, 3 done
    std::wstring prompt;     // the prompt that started this turn
    std::wstring command;    // last shell command this turn
    std::vector<std::wstring> output;  // its last few output lines
    bool commandFailed = false;

    // Plan mode: the plan waiting for approval (Approval phase, ExitPlanMode).
    std::wstring plan;
    // AskUserQuestion with several questions, multi-select or free text: every question.
    struct Question {
        std::wstring text, header;
        bool multi = false;
        std::vector<std::wstring> options;
    };
    std::vector<Question> questions;
    // Reply from the island: the finished session is listening for your next message
    // (bunbun-hook --reply, a Claude Code asyncRewake Stop hook). 0 = not listening.
    uint64_t replyId = 0;
    std::wstring replyTo;    // "Claude Code · project" of the listening session

    // Not tied to a session: shown even when nothing is running.
    struct HistoryItem {
        std::wstring agent, project, prompt;
        long long finishedAt = 0;  // time(nullptr)
        int seconds = 0;
        bool failed = false;
    };
    std::vector<HistoryItem> history;  // newest first, at most 8
    int jobsToday = 0;
    // Claude Code's statusLine: rate limits and context use, -1 = unknown.
    float usage5h = -1, usage7d = -1, contextPct = -1;
    long long usage5hResets = 0;       // unix seconds, 0 = unknown
    long long usage7dResets = 0;       // the weekly limit's, unix seconds, 0 = unknown
    double usageAt = 0;                // NowSeconds() of the last statusLine update
};

// Decision sent back to a waiting hook.
enum class AgentDecision { Allow, Deny, Terminal };

namespace agents {
bool Start();   // starts the named-pipe server thread
void Stop();
// Resolves a pending request. answerIndex >= 0 picks an AskUserQuestion option.
void Decide(uint64_t requestId, AgentDecision decision, int answerIndex = -1);
// Resolves a pending request with a ready-made reply line for the hook:
// {"deny":"<message>"} (send a plan back) or {"answers":{...}} (several questions).
void DecideRaw(uint64_t requestId, const std::string& replyLine);
// Wakes a finished session with your next message (AgentView::replyId).
void SendReply(uint64_t replyId, const std::wstring& text);
}  // namespace agents

}  // namespace bb
