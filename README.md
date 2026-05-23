# AI Status

A physical status indicator for AI coding assistants. Monitor Claude Code's real-time state on an ESP32-C6 powered display — so you never miss an approval prompt.

## Overview

AI Status bridges Claude Code's hook system with an ESP32-C6 device over WiFi. When Claude Code needs your approval, your desk display switches to an orange foreground instantly. No need to keep staring at the terminal.

## Architecture

```
Mac (Claude Code)                         ESP32-C6 (1.47" ST7789)
     │                                        │
     │  UserPromptSubmit hook                  │
     │  POST {"state":"working"}  ─────────→  Screen → Green text/icon
     │                                        │
     │  PreToolUse hook                        │
     │  POST {"state":"working"}  ─────────→  Screen → Green text/icon
     │                                        │
     │  Notification hook                      │
     │  POST {"state":"approval"} ─────────→  Screen → Orange text/icon
     │                                        │
     │  PermissionRequest hook                 │
     │  POST {"state":"approval"} ─────────→  Screen → Orange text/icon
     │                                        │
     │  PostToolUse hook (error)               │
     │  POST {"state":"error"}    ─────────→  Screen → Red text/icon
     │                                        │
     │  Working state, no new events           │
     │  for 60 seconds                         Timer  → White text/icon
```

Hook scripts use mDNS: `http://ai-status.local` — no IP configuration needed.

## States

| State    | Icon | Label    | Foreground | Background     | Trigger                                           |
| -------- | ---- | -------- | ---------- | -------------- | ------------------------------------------------- |
| Idle     | `~`  | Idle     | #ffffff    | #1a1a2e (dark) | Working timeout                                   |
| Working  | `*`  | Working  | #00ff00    | #1a1a2e (dark) | PreToolUse / PostToolUse success                  |
| Approval | `!`  | Approval | #ffc400    | #1a1a2e (dark) | Notification or PermissionRequest hook            |
| Error    | `X`  | Error    | #ff3030    | #1a1a2e (dark) | PostToolUse with `tool_response.is_error`         |

> Icons are rendered as ASCII glyphs using the Arduino_GFX built-in font.
> A glyph font / bitmap upgrade can swap these for proper symbols.

State transitions:

- Any → Working: UserPromptSubmit fires (prompt submitted) or PreToolUse fires (tool about to run)
- Working → Error: PostToolUse with error
- Any → Approval: Notification or PermissionRequest (sticky — won't auto-clear, prevents missed prompts)
- Working → Idle: 60s of inactivity
- Approval / Error → next state: any subsequent hook event clears them

## Hardware

- MCU: ESP32-C6 (Waveshare)
- Display: 1.47" ST7789, 172×320, IPS, SPI bus
- Power: USB
- Communication: WiFi (same LAN as Mac), mDNS discovery

### Pin Configuration

| Function  | GPIO |
| --------- | ---- |
| SPI MOSI  | 6    |
| SPI SCK   | 7    |
| SPI CS    | 14   |
| DC        | 15   |
| RST       | 21   |
| Backlight | 22   |

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
- Background: full-screen #1a1a2e
- Icon and text: color changes per state
- Minimal, readable from a distance

## Tech Stack

| Component      | Choice                | Reason                                  |
| -------------- | --------------------- | --------------------------------------- |
| Display lib    | Arduino_GFX           | Consistent with existing ESP32 projects |
| HTTP server    | WebServer (Arduino)   | Lightweight, sufficient                 |
| Network        | WiFi + mDNS (ESPmDNS) | Zero-config discovery                   |
| Provisioning   | AP + WebServer        | No app needed, universal                |
| Config storage | Preferences           | NVS-based, persists across reboots      |
| Hook scripts   | Bash + curl           | Zero dependencies on Mac                |
| Data format    | JSON                  | Consistent with Claude Code hooks stdin |

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
│   ├── user_prompt_notify.sh    # UserPromptSubmit → Working
│   ├── pre_tool_notify.sh       # PreToolUse → Working
│   ├── post_tool_notify.sh      # PostToolUse → Error (on failure)
│   ├── notification_notify.sh   # Notification / PermissionRequest → Approval
│   └── stop_notify.sh           # Manual/debug idle script, not installed as a Claude hook
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
    "UserPromptSubmit": [
      {
        "matcher": "",
        "hooks": [
          { "type": "command", "command": "~/.claude/hooks/user_prompt_notify.sh" }
        ]
      }
    ],
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
          {
            "type": "command",
            "command": "~/.claude/hooks/post_tool_notify.sh"
          }
        ]
      }
    ],
    "Notification": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/notification_notify.sh"
          }
        ]
      }
    ],
    "PermissionRequest": [
      {
        "matcher": "",
        "hooks": [
          { "type": "command", "command": "~/.claude/hooks/notification_notify.sh", "timeout": 1, "async": true }
        ]
      }
    ],
  }
}
```

**Hook roles:**

- `UserPromptSubmit` — fires when you submit a prompt → green text/icon (Working), including pure chat turns with no tool calls
- `PreToolUse` — fires before each tool call → green text/icon (Working)
- `PostToolUse` — fires after each tool call → red text/icon (Error) only when `tool_response.is_error` is set; otherwise no-op so the Working state sticks
- `Notification` — fires when Claude Code actually needs your attention → orange text/icon (Approval)
- `PermissionRequest` — fires when a tool approval prompt is shown → orange text/icon (Approval)
- `Stop` is intentionally not installed. Claude Code can emit Stop before the visible thinking UI is done, so Idle is controlled by the ESP32 working timeout instead.
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


# Idle
curl -X POST http://ai-status.local/status \
  -H "Content-Type: application/json" \
  -d '{"state":"idle"}'

# Working
curl -X POST http://ai-status.local/status \
  -H "Content-Type: application/json" \
  -d '{"state":"working"}'

# Approval
curl -X POST http://ai-status.local/status \
  -H "Content-Type: application/json" \
  -d '{"state":"approval"}'

# Error
curl -X POST http://ai-status.local/status \
  -H "Content-Type: application/json" \
  -d '{"state":"error"}'

# 查看当前状态
curl http://ai-status.local/status

# Wipe saved WiFi credentials and reboot into AP provisioning mode
curl -X POST http://ai-status.local/reset
```

## HTTP API

| Method | Path      | Body                                          | Effect                                                     |
| ------ | --------- | --------------------------------------------- | ---------------------------------------------------------- |
| `POST` | `/status` | `{"state": "idle\|working\|approval\|error"}` | Set displayed state                                        |
| `GET`  | `/status` | —                                             | Returns `{"state": ..., "uptime": <seconds>}`              |
| `POST` | `/reset`  | —                                             | Clear stored WiFi credentials, reboot into AP provisioning |

## License

MIT
