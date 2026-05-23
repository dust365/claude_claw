#!/bin/bash
# AI Status - Notification Hook
# Claude Code fires Notification when it needs the user: pending approval
# or idle waiting for input. Either way the desk display should grab attention.

ESP32_HOST="ai-status.local"

cat > /dev/null

curl -s --connect-timeout 1 --max-time 2 \
    -X POST "http://${ESP32_HOST}/status" \
    -H "Content-Type: application/json" \
    -d '{"state":"approval"}' \
    > /dev/null 2>&1 &

exit 0
