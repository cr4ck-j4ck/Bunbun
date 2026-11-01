#!/usr/bin/env sh
# Regenerates src/island/default_settings.inc from the ==WindhawkModSettings==
# YAML block at the top of src/island/island_core.cpp. Run after editing it.
set -e
# Values are emitted verbatim; none of the defaults contain quotes or backslashes.
cd "$(dirname "$0")/.."
awk '
/==BunbunSettings==/ { on = 1; next }
/==\/BunbunSettings==/ { on = 0 }
!on { next }
/^- [A-Za-z]+:[ ]*$/ { sub(/^- /, ""); sub(/:.*/, ""); section = $0; next }
/^  - [A-Za-z]+:/ {
    line = $0; sub(/^  - /, "", line)
    key = line; sub(/:.*/, "", key)
    val = line; sub(/^[^:]*:[ ]*/, "", val)
    if (val ~ /^".*"$/ || val ~ /^\x27.*\x27$/) val = substr(val, 2, length(val) - 2)
    printf "    {L\"%s\", L\"%s\", L\"%s\"},\n", section, key, val
}' src/island/settings_schema.yaml > src/island/default_settings.inc
echo "$(wc -l < src/island/default_settings.inc) settings written"
