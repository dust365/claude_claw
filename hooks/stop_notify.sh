#!/bin/bash
# AI Status - Stop Hook
# Claude finished its turn -> screen shows Idle.

ESP32_HOST="10.0.0.182"
LOG_FILE="/tmp/ai-status-hooks.log"

INPUT=$(cat)

log_hook() {
    printf '[%s] hook=%s event_state=%s host=%s cwd=%s input_len=%s\n'         "$(date '+%Y-%m-%d %H:%M:%S')"         "$(basename "$0")"         "$1"         "$ESP32_HOST"         "$(pwd)"         "${#INPUT}" >> "$LOG_FILE" 2>/dev/null || true
}

post_state() {
    local state="$1"
    local code
    code=$(curl -s -o /dev/null -w '%{http_code}' --connect-timeout 0.3 --max-time 1         -X POST "http://${ESP32_HOST}/status"         -H "Content-Type: application/json"         -d "{\"state\":\"${state}\"}")
    local rc=$?
    printf '[%s] hook=%s post_state=%s host=%s http_code=%s rc=%s\n'         "$(date '+%Y-%m-%d %H:%M:%S')"         "$(basename "$0")"         "$state"         "$ESP32_HOST"         "$code"         "$rc" >> "$LOG_FILE" 2>/dev/null || true
}

log_hook "idle"
post_state "idle" &

exit 0
