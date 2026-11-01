"""Drives bunbun-hook.exe with fake Claude Code events, to try the island
without a real agent session.

    python tests/simulate_agent.py work       # prompt + a few tool calls
    python tests/simulate_agent.py approve    # Bash permission request (blocks for a click)
    python tests/simulate_agent.py question   # AskUserQuestion with options
    python tests/simulate_agent.py finish     # Stop -> Bun hops
    python tests/simulate_agent.py demo       # all of the above in order
"""
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HOOK = os.path.join(HERE, "..", "build", "bunbun-hook.exe")
BASE = {"session_id": "demo-1", "cwd": os.path.abspath(os.path.join(HERE, ".."))}


def send(event, **fields):
    payload = dict(BASE, **fields)
    out = subprocess.run([HOOK, event], input=json.dumps(payload).encode(), capture_output=True, timeout=130)
    reply = out.stdout.decode().strip()
    print(f"{event:<18} -> {reply or '(no output)'}")
    return reply


def work():
    send("SessionStart")
    send("UserPromptSubmit", prompt="Give the island a bunny mascot that hops when tasks finish")
    time.sleep(1.5)
    for tool, inp in [
        ("Read", {"file_path": "src/mascot/bun.cpp"}),
        ("Grep", {"pattern": "DrawAgent"}),
        ("Edit", {"file_path": "src/mascot/bun.cpp", "old_string": "a\nb\nc", "new_string": "a\nB\nB2\nc"}),
        ("Bash", {"command": "./build.sh"}),
    ]:
        send("PreToolUse", tool_name=tool, tool_input=inp)
        time.sleep(1.2)
        send("PostToolUse", tool_name=tool, tool_input=inp)


def approve():
    inp = {"command": "git push origin main", "description": "Push the release"}
    send("PreToolUse", tool_name="Bash", tool_input=inp)
    print("Click Allow or Deny on the island...")
    reply = send("PermissionRequest", tool_name="Bash", tool_input=inp)
    if reply:
        decision = json.loads(reply)["hookSpecificOutput"]["decision"]["behavior"]
        print(f"decision: {decision}")
        if decision == "allow":
            send("PostToolUse", tool_name="Bash", tool_input=inp)


def question():
    inp = {"questions": [{
        "question": "Which color should Bun's ears be?",
        "header": "Ears",
        "multiSelect": False,
        "options": [{"label": "Pink", "description": ""}, {"label": "Lavender", "description": ""},
                    {"label": "Peach", "description": ""}],
    }]}
    print("Pick an answer on the island...")
    send("PermissionRequest", tool_name="AskUserQuestion", tool_input=inp)


def finish():
    send("Stop", last_assistant_message="Bun now hops when a task finishes.")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "demo"
    steps = {"work": [work], "approve": [approve], "question": [question], "finish": [finish],
             "demo": [work, approve, question, finish]}[mode]
    for step in steps:
        step()
