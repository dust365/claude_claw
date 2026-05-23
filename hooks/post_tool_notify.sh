#!/bin/bash
# AI Status - PostToolUse Hook
# Tool finished. If it errored, flip screen to red. Otherwise leave it
# in "working" — the PreToolUse hook already set that.
#
# Error indicator lives at tool_response.is_error (top-level fields are
# session metadata, not result data).

ESP32_HOST="ai-status.local"

INPUT=$(cat)

HAS_ERROR=$(printf '%s' "$INPUT" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
except Exception:
    print('no'); sys.exit(0)
resp = data.get('tool_response') or {}
if isinstance(resp, dict) and (resp.get('is_error') or resp.get('error')):
    print('yes')
else:
    print('no')
" 2>/dev/null)

# Only send when there's something worth showing. A successful tool finish
# leaves the screen blue (Working) until Stop hook flips it to Idle.
if [ "$HAS_ERROR" = "yes" ]; then
    curl -s --connect-timeout 1 --max-time 2 \
        -X POST "http://${ESP32_HOST}/status" \
        -H "Content-Type: application/json" \
        -d '{"state":"error"}' \
        > /dev/null 2>&1 &
fi

exit 0
