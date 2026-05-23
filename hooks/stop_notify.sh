#!/bin/bash
# AI Status - Stop Hook
# Claude Code finished its turn (or a subagent finished). Return to idle.

ESP32_HOST="ai-status.local"

cat > /dev/null

curl -s --connect-timeout 1 --max-time 2 \
    -X POST "http://${ESP32_HOST}/status" \
    -H "Content-Type: application/json" \
    -d '{"state":"idle"}' \
    > /dev/null 2>&1 &

exit 0
