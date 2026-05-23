# AI Status

A physical status indicator for AI coding assistants. Monitor Claude Code's real-time state on an ESP32-C6 powered display — so you never miss an approval prompt.

## Overview

AI Status bridges Claude Code's hook system with an ESP32-C6 device over WiFi. When Claude Code needs your approval, your desk display turns orange instantly. No need to keep staring at the terminal.

## Architecture

```
Mac (Claude Code)                         ESP32-C6 (1.47" ST7789)
     │                                        │
     │  PreToolUse hook                        │
     │  POST {"state":"working"}  ─────────→  Screen → Blue * Working
     │                                        │
     │  Notification hook                      │
     │  POST {"state":"approval"} ─────────→  Screen → Orange ! Approval
     │                                        │
     │  PostToolUse hook (error)               │
     │  POST {"state":"error"}    ─────────→  Screen → Red X Error
     │                                        │
     │  Stop hook                              │
     │  POST {"state":"idle"}     ─────────→  Screen → Dark ~ Idle
     │                                        │
     │  Working state, no new events           │
     │  for N seconds                          Timer  → Dark ~ Idle
```

Hook scripts use mDNS: `http://ai-status.local` — no IP configuration needed.

## States

| State | Icon | Label | Background | Trigger |
|-------|------|-------|------------|---------|
| Idle | `~` | Idle | #1a1a2e (dark) | Stop hook (turn finished) or Working timeout |
| Working | `*` | Working | #1565c0 (blue) | PreToolUse / PostToolUse success |
| Approval | `!` | Approval | #e65100 (orange) | Notification hook (approval needed or idle input) |
| Error | `X` | Error | #b71c1c (red) | PostToolUse with `tool_response.is_error` |

> Icons are rendered as ASCII glyphs using the Arduino_GFX built-in font.
> A glyph font / bitmap upgrade can swap these for proper symbols.

State transitions:
- Any → Working: PreToolUse fires (tool about to run)
- Working → Error: PostToolUse with error
- Any → Approval: Notification (sticky — won't auto-clear, prevents missed prompts)
- Working → Idle: Stop hook (turn done) or 10s of inactivity
- Approval / Error → next state: any subsequent hook event clears them

## Hardware

- MCU: ESP32-C6 (Waveshare)
- Display: 1.47" ST7789, 172×320, IPS, SPI bus
- Power: USB
- Communication: WiFi (same LAN as Mac), mDNS discovery

### Pin Configuration

| Function | GPIO |
|----------|------|
| SPI MOSI | 6 |
| SPI SCK | 7 |
| SPI CS | 14 |
| DC | 15 |
| RST | 21 |
| Backlight | 22 |

## Network & Provisioning

### AP + Web Provisioning

On first boot (or no saved WiFi config):

1. ESP32-C6 starts AP hotspot: `AI-Status-Setup` (password `12345678`, WPA2)
2. Screen shows hotspot name, password, and `192.168.4.1`
3. User connects phone/laptop to the hotspot
4. Opens browser → `192.168.4.1`
5. Web page: enter WiFi SSID + password → submit
6. Credentials saved to Preferences, device reboots
7. Connects to target WiFi → registers mDNS: `ai-status.local`
8. Screen enters Idle state

On failure: re-enters AP provisioning mode.

### mDNS Service Discovery

After WiFi connection, ESP32 registers:
- Hostname: `ai-status.local`
- Mac natively resolves mDNS — hook scripts use this directly
- No need to configure IP addresses

## UI Design

- Screen: 172×320 pixels, portrait
- Layout: centered icon + one line of text
- Icon and text: white
- Background: full-screen solid color (changes per state)
- Minimal, readable from a distance

## Tech Stack

| Component | Choice | Reason |
|-----------|--------|--------|
| Display lib | Arduino_GFX | Consistent with existing ESP32 projects |
| HTTP server | WebServer (Arduino) | Lightweight, sufficient |
| Network | WiFi + mDNS (ESPmDNS) | Zero-config discovery |
| Provisioning | AP + WebServer | No app needed, universal |
| Config storage | Preferences | NVS-based, persists across reboots |
| Hook scripts | Bash + curl | Zero dependencies on Mac |
| Data format | JSON | Consistent with Claude Code hooks stdin |

## Project Structure

```
ai-status/
├── README.md
├── LICENSE
├── .gitignore
├── platformio.ini
├── src/
│   └── main.cpp
├── hooks/
│   ├── pre_tool_notify.sh       # PreToolUse → Working
│   ├── post_tool_notify.sh      # PostToolUse → Error (on failure)
│   ├── notification_notify.sh   # Notification → Approval
│   └── stop_notify.sh           # Stop → Idle
└── config/
    └── settings.json            # Claude Code hooks configuration
```

## Setup

### 1. Flash ESP32-C6

1. Open project in VS Code with PlatformIO
2. Build and upload to ESP32-C6
3. Device enters AP provisioning mode on first boot

### 2. Provision WiFi

1. Connect phone to `AI-Status-Setup` hotspot (password `12345678`)
2. Open `192.168.4.1` in browser
3. Enter your WiFi SSID and password
4. Device connects and screen shows IP briefly, then enters Idle

### 3. Install Hook Scripts

```bash
mkdir -p ~/.claude/hooks
cp hooks/*.sh ~/.claude/hooks/
chmod +x ~/.claude/hooks/*.sh
```

### 4. Configure Claude Code Hooks

Claude Code reads hooks from `~/.claude/settings.json`. If you don't have one yet, just copy:

```bash
cp config/settings.json ~/.claude/settings.json
```

If you already have a `~/.claude/settings.json`, merge the `hooks` field. The final file should contain:

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "",
        "hooks": [
          { "type": "command", "command": "~/.claude/hooks/pre_tool_notify.sh" }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "",
        "hooks": [
          { "type": "command", "command": "~/.claude/hooks/post_tool_notify.sh" }
        ]
      }
    ],
    "Notification": [
      {
        "matcher": "",
        "hooks": [
          { "type": "command", "command": "~/.claude/hooks/notification_notify.sh" }
        ]
      }
    ],
    "Stop": [
      {
        "matcher": "",
        "hooks": [
          { "type": "command", "command": "~/.claude/hooks/stop_notify.sh" }
        ]
      }
    ]
  }
}
```

**Hook roles:**

- `PreToolUse` — fires before each tool call → screen turns blue (Working)
- `PostToolUse` — fires after each tool call → turns red (Error) only when `tool_response.is_error` is set; otherwise no-op so the Working state sticks
- `Notification` — fires when Claude Code actually needs your attention (pending approval or long idle waiting on input) → turns orange (Approval). This replaces the unreliable "guess which tools need approval from a hard-coded list" approach — Claude Code tells you directly.
- `Stop` — fires when Claude finishes its turn → returns to Idle
- `matcher: ""` — empty string matches all tools. Note `matcher` is a regex, not a glob, so `"*"` is **not** valid.
- None of the scripts emit JSON on stdout, so they never interfere with Claude Code's approval flow.

**Verify the configuration:**

```bash
# File exists and is valid JSON
cat ~/.claude/settings.json | python3 -m json.tool

# All 4 hook scripts are executable
ls -la ~/.claude/hooks/*.sh
```

Restart Claude Code to pick up the new hooks.

### 5. Test

```bash
# Should turn screen orange (Approval)
curl -X POST http://ai-status.local/status -H "Content-Type: application/json" -d '{"state":"approval"}'

# Should turn screen blue (Working)
curl -X POST http://ai-status.local/status -H "Content-Type: application/json" -d '{"state":"working"}'

# Should turn screen red (Error)
curl -X POST http://ai-status.local/status -H "Content-Type: application/json" -d '{"state":"error"}'

# Should turn screen dark (Idle)
curl -X POST http://ai-status.local/status -H "Content-Type: application/json" -d '{"state":"idle"}'

# Get current state + uptime
curl http://ai-status.local/status

# Wipe saved WiFi credentials and reboot into AP provisioning mode
curl -X POST http://ai-status.local/reset
```

## HTTP API

| Method | Path | Body | Effect |
|--------|------|------|--------|
| `POST` | `/status` | `{"state": "idle\|working\|approval\|error"}` | Set displayed state |
| `GET` | `/status` | — | Returns `{"state": ..., "uptime": <seconds>}` |
| `POST` | `/reset` | — | Clear stored WiFi credentials, reboot into AP provisioning |

## License

MIT
