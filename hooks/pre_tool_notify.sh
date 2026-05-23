#!/bin/bash
# AI Status - PreToolUse Hook
# Tool call started → screen shows "Working".
# Approval needed is signaled separately by the Notification hook,
# so we don't need to guess which tools require approval.

ESP32_HOST="ai-status.local"

# Drain stdin (we don't read fields here, but Claude Code expects the hook
# to consume the JSON payload).
cat > /dev/null

curl -s --connect-timeout 1 --max-time 2 \
    -X POST "http://${ESP32_HOST}/status" \
    -H "Content-Type: application/json" \
    -d '{"state":"working"}' \
    > /dev/null 2>&1 &

exit 0
