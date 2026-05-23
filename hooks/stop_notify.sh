#!/bin/bash
# claude_claw - Stop Hook
# Claude finished its turn -> screen shows Idle immediately.

ESP32_ENV_FILE="${CLAUDE_CLAW_ENV:-$HOME/.claude/claude-claw.env}"
if [ -f "$ESP32_ENV_FILE" ]; then
    # shellcheck disable=SC1090
    . "$ESP32_ENV_FILE"
fi

: "${ESP32_HOST:=claude-claw.local}"
: "${LOG_FILE:=/tmp/claude-claw-hooks.log}"

INPUT=$(cat)

log_hook() {
    printf '[%s] hook=%s event_state=%s host=%s cwd=%s input_len=%s\n'         "$(date '+%Y-%m-%d %H:%M:%S')"         "$(basename "$0")"         "$1"         "$ESP32_HOST"         "$(pwd)"         "${#INPUT}" >> "$LOG_FILE" 2>/dev/null || true
}

post_state() {
    local state="$1"
    local delay_ms="${2:-0}"
    local payload
    if [ "$delay_ms" != "0" ]; then
        payload="{\"state\":\"${state}\",\"delay\":${delay_ms}}"
    else
        payload="{\"state\":\"${state}\"}"
    fi
    local code
    code=$(curl -s -o /dev/null -w '%{http_code}' --connect-timeout 0.5 --max-time 2         -X POST "http://${ESP32_HOST}/status"         -H "Content-Type: application/json"         -d "$payload")
    local rc=$?
    printf '[%s] hook=%s post_state=%s delay_ms=%s host=%s http_code=%s rc=%s\n'         "$(date '+%Y-%m-%d %H:%M:%S')"         "$(basename "$0")"         "$state"         "$delay_ms"         "$ESP32_HOST"         "$code"         "$rc" >> "$LOG_FILE" 2>/dev/null || true
}

log_hook "idle"
post_state "idle"

exit 0
