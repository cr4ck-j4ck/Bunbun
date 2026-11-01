"""Generates settings-ui/src/schema.json from the settings block in
src/island/island_core.cpp (names, descriptions, options and defaults), so the
settings app always matches what the island understands.

    python scripts/gen_schema.py
"""
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(ROOT, "src", "island", "settings_schema.yaml"), encoding="utf-8").read()
block = src.split("// ==BunbunSettings==", 1)[1].split("// ==/BunbunSettings==", 1)[0]

settings = {}
section = None
current = None
in_options = False
for line in block.splitlines():
    m = re.match(r"^- (\w+):\s*$", line)
    if m:
        section, current, in_options = m.group(1), None, False
        continue
    m = re.match(r"^  - (\w+):\s*(.*)$", line)
    if m and section:
        key, value = m.groups()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        current = f"{section}.{key}"
        settings[current] = {"default": value, "name": key, "description": "", "options": []}
        in_options = False
        continue
    if current is None:
        continue
    m = re.match(r"^    \$name:\s*(.*)$", line)
    if m:
        settings[current]["name"] = m.group(1).strip().strip("'\"")
        continue
    m = re.match(r"^    \$description:\s*(.*)$", line)
    if m:
        settings[current]["description"] = m.group(1).strip().strip("'\"")
        continue
    if re.match(r"^    \$options:", line):
        in_options = True
        continue
    m = re.match(r"^      - ['\"]?([^:'\"]+)['\"]?:\s*(.*)$", line)
    if m and in_options:
        settings[current]["options"].append({"value": m.group(1).strip(), "label": m.group(2).strip()})

out = os.path.join(ROOT, "settings-ui", "src", "schema.json")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "w", encoding="utf-8") as f:
    json.dump(settings, f, indent=1, ensure_ascii=False)
print(f"{len(settings)} settings -> {os.path.relpath(out, ROOT)}")
