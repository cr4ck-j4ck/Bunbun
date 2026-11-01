"""Checks bunbun-hook.exe's reply to each agent's hook dialect.

Runs the real hook against the running island (start Bunbun first) with
"Ask before shell commands" off, so nothing waits for a click. Every agent must
get exactly the "no opinion" reply its hooks documentation expects, quickly.

    python tests/test_dialects.py
"""
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HOOK = os.path.join(HERE, "..", "build", "bunbun-hook.exe")
CWD = os.path.abspath(os.path.join(HERE, ".."))

CASES = [
    # (label, agent, event arg, payload, expected stdout)
    ("Claude PreToolUse", None, "PreToolUse",
     {"session_id": "t-claude", "cwd": CWD, "tool_name": "Bash", "tool_input": {"command": "ls"}}, ""),
    ("Antigravity SessionStart", "antigravity", "SessionStart",
     {"conversationId": "t-ag", "workspacePaths": [CWD]}, "{}"),
    ("Antigravity PreToolUse run_command", "antigravity", "PreToolUse",
     {"conversationId": "t-ag", "workspacePaths": [CWD], "toolCall": {"name": "run_command", "args": {"CommandLine": "npm test"}}},
     '{"decision":"ask"}'),
    ("Antigravity PreToolUse view_file", "antigravity", "PreToolUse",
     {"conversationId": "t-ag", "workspacePaths": [CWD], "toolCall": {"name": "view_file", "args": {"AbsolutePath": "C:/x.txt"}}},
     '{"decision":"ask"}'),
    ("Antigravity PostToolUse", "antigravity", "PostToolUse",
     {"conversationId": "t-ag", "workspacePaths": [CWD], "toolCall": {"name": "view_file", "args": {"AbsolutePath": "C:/x.txt"}}}, "{}"),
    ("Antigravity Stop", "antigravity", "Stop",
     {"conversationId": "t-ag", "workspacePaths": [CWD], "terminationReason": "done", "fullyIdle": True}, "{}"),
    ("Cursor beforeSubmitPrompt", "cursor", "beforeSubmitPrompt",
     {"conversation_id": "t-cur", "workspace_roots": [CWD], "prompt": "fix the bug"}, ""),
    ("Cursor beforeShellExecution", "cursor", "beforeShellExecution",
     {"conversation_id": "t-cur", "workspace_roots": [CWD], "command": "npm run build", "cwd": CWD}, ""),
    ("Cursor afterFileEdit", "cursor", "afterFileEdit",
     {"conversation_id": "t-cur", "workspace_roots": [CWD], "file_path": "src/a.ts",
      "edits": [{"old_string": "a\nb", "new_string": "a\nB\nC"}]}, ""),
    ("Cursor stop", "cursor", "stop", {"conversation_id": "t-cur", "workspace_roots": [CWD], "status": "completed"}, ""),
    ("Copilot sessionStart", "copilot", "sessionStart", {"sessionId": "t-cop", "cwd": CWD, "source": "new"}, "{}"),
    ("Copilot preToolUse", "copilot", "preToolUse",
     {"sessionId": "t-cop", "cwd": CWD, "toolName": "bash", "toolArgs": "{\"command\": \"ls\"}"}, "{}"),
    ("Copilot agentStop", "copilot", "agentStop", {"sessionId": "t-cop", "cwd": CWD, "stopReason": "end_turn"}, "{}"),
]


def run(agent, event, payload):
    args = [HOOK] + (["--agent", agent] if agent else []) + [event]
    t = time.perf_counter()
    out = subprocess.run(args, input=json.dumps(payload).encode(), capture_output=True, timeout=30)
    return out.stdout.decode().strip(), (time.perf_counter() - t) * 1000, out.returncode


def main():
    failed = 0
    for label, agent, event, payload, expected in CASES:
        got, ms, rc = run(agent, event, payload)
        ok = got == expected and rc == 0 and ms < 2000
        failed += not ok
        print(f"{'ok  ' if ok else 'FAIL'} {label:<40} {ms:6.0f} ms  {got or '(no output)'}"
              + ("" if ok else f"   expected {expected or '(no output)'}"))
    print("all dialect checks passed" if not failed else f"{failed} dialect check(s) failed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
