"""Drives bunbun-hook with fake Claude Code events, to try the island
without a real agent session.

    python tests/simulate_agent.py work       # prompt + a few tool calls
    python tests/simulate_agent.py approve    # Bash permission request (blocks for a click)
    python tests/simulate_agent.py question   # AskUserQuestion with options
    python tests/simulate_agent.py questions  # two questions, one multi-select (the step-through card)
    python tests/simulate_agent.py plan       # ExitPlanMode: a plan to approve or send back
    python tests/simulate_agent.py finish     # Stop -> Bun hops
    python tests/simulate_agent.py demo       # all of the above in order
"""
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
# Windows: build/bunbun-hook.exe. Linux: the CMake build in build-linux/ (or set BUNBUN_HOOK).
HOOK = os.environ.get("BUNBUN_HOOK") or (os.path.join(HERE, "..", "build", "bunbun-hook.exe") if os.name == "nt"
                                         else os.path.join(HERE, "..", "build-linux", "bunbun-hook"))
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


def edit():
    # A long edit: the card grows to 12 lines and the wheel scrolls the rest.
    old = "\n".join([
        "    router.get('/users', asyncHandler(async (req, res) => {",
        "        log.debug('API Request: GET /users');",
        "        const users = await User.find();",
        "        res.json(users);",
        "    }));",
    ])
    new = "\n".join([
        "    router.get('/users', requireAuth, asyncHandler(async (req, res) => {",
        "        log.debug('API Request: GET /users', { by: req.user.id });",
        "        const page = Math.max(1, Number(req.query.page) || 1);",
        "        const limit = Math.min(100, Number(req.query.limit) || 20);",
        "        const [users, total] = await Promise.all([",
        "            User.find().skip((page - 1) * limit).limit(limit),",
        "            User.countDocuments(),",
        "        ]);",
        "        res.json({",
        "            users,",
        "            page,",
        "            pages: Math.ceil(total / limit),",
        "            total,",
        "        });",
        "    }));",
        "",
        "    router.get('/users/:id', requireAuth, asyncHandler(async (req, res) => {",
    ])
    inp = {"file_path": "src/controllers/users.js", "old_string": old, "new_string": new}
    print("Scroll the diff, then click Allow or Deny on the island...")
    reply = send("PermissionRequest", tool_name="Edit", tool_input=inp)
    if reply:
        print("decision:", json.loads(reply)["hookSpecificOutput"]["decision"]["behavior"])


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


def questions():
    inp = {"questions": [
        {"question": "Which color should Bun's ears be?", "header": "Ears", "multiSelect": False,
         "options": [{"label": "Pink", "description": ""}, {"label": "Lavender", "description": ""},
                     {"label": "Peach", "description": ""}]},
        {"question": "Which hats can Bun wear?", "header": "Hats", "multiSelect": True,
         "options": [{"label": "Crown", "description": ""}, {"label": "Beanie", "description": ""},
                     {"label": "Party hat", "description": ""}, {"label": "Sunhat", "description": ""}]},
    ]}
    print("Answer both questions on the island...")
    send("PermissionRequest", tool_name="AskUserQuestion", tool_input=inp)


def plan():
    inp = {"plan": "# Give Bun a hop\n\n1. Add a **Hop** mood to `mascot.cpp`.\n"
                   "2. Trigger it from the agent bridge when a task finishes.\n"
                   "- Keep the animation under 0.6 s\n- Respect *Reduce motion*\n\n"
                   "## Tests\n- Render a contact sheet with the new mood\n- Run tests/run.sh"}
    print("Approve the plan, or send it back, on the island...")
    send("PermissionRequest", tool_name="ExitPlanMode", tool_input=inp)


def finish():
    send("Stop", last_assistant_message="Bun now hops when a task finishes.")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "demo"
    steps = {"work": [work], "approve": [approve], "edit": [edit], "question": [question], "questions": [questions], "plan": [plan],
             "finish": [finish],
             "demo": [work, approve, question, finish]}[mode]
    for step in steps:
        step()
