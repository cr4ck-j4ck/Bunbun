"""Checks that the settings app's list of settings that work on Linux
(LINUX_SETTINGS in settings-ui/src/platform.ts) matches the settings the Linux
build actually reads. A setting missing from the list wrongly shows "Not on
Linux yet"; one listed but never read shows a switch that does nothing.

    python tests/check_linux_settings.py
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
# Everything compiled into the Linux binaries that reads settings (CMakeLists.txt).
SOURCES = glob.glob(os.path.join(ROOT, "src/platform/linux/*.cpp")) + [
    os.path.join(ROOT, p) for p in ("src/mascot/mascot.cpp", "src/mascot/pack_sheet.cpp", "src/agents/hook_installer.cpp",
                                    "src/agents/agent_bridge.cpp", "src/hook/bunbun_hook.cpp")
] + glob.glob(os.path.join(ROOT, "src/island/*.h"))

schema = json.load(open(os.path.join(ROOT, "settings-ui/src/schema.json"), encoding="utf-8"))
code = "".join(open(p, encoding="utf-8", errors="ignore").read() for p in SOURCES if os.path.exists(p))
read = {k for k in schema if f'"{k}"' in code}

ts = open(os.path.join(ROOT, "settings-ui/src/platform.ts"), encoding="utf-8").read()
block = ts.split("LINUX_SETTINGS = new Set([", 1)[1].split("]);", 1)[0]
listed = set(re.findall(r'"([A-Za-z]+\.[A-Za-z0-9]+)"', block))

ok = True
if read - listed:
    ok = False
    print("Read on Linux but missing from LINUX_SETTINGS:", ", ".join(sorted(read - listed)))
if listed - read:
    ok = False
    print("In LINUX_SETTINGS but not read by the Linux build:", ", ".join(sorted(listed - read)))
print(f"linux settings: {len(read)} of {len(schema)} work on Linux" + ("" if ok else " (list out of date)"))
sys.exit(0 if ok else 1)
