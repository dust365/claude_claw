# claude_claw

claude_claw 是一个基于 ESP32-C6 的 Claude Code 状态屏。它通过 Claude Code hooks 接收当前工作状态，并在 1.47 英寸 ST7789 屏幕上显示 Idle、Working、Approval、Error 四种状态。

它的主要用途是：不用一直盯着终端，也能知道 Claude Code 是否正在工作、是否需要审批、是否已经完成回复。

## 功能概览

- Claude Code 提交问题后，屏幕显示 `Working`。
- Claude Code 需要权限审批时，屏幕显示 `Approval`。
- 审批通过后，工具执行完成会回到 `Working`。
- Claude Code 本轮回复结束后，屏幕显示 `Idle`。
- 工具调用失败时，屏幕显示 `Error`。
- C6 自带 WiFi 配网页面，首次启动可以通过手机连接热点配置 WiFi。
- 支持 HTTP API，可以用 `curl` 手动测试状态。

## 工作原理

```text
Mac / Claude Code                         ESP32-C6 / 1.47" ST7789
     |                                         |
     |  UserPromptSubmit hook                  |
     |  POST {"state":"working"}   --------->  屏幕显示 Working
     |                                         |
     |  PreToolUse hook                        |
     |  POST {"state":"working"}   --------->  屏幕显示 Working
     |                                         |
     |  Notification hook                      |
     |  POST {"state":"approval"}  --------->  屏幕显示 Approval
     |                                         |
     |  PermissionRequest hook                 |
     |  POST {"state":"approval"}  --------->  屏幕显示 Approval
     |                                         |
     |  PostToolUse hook                       |
     |  成功: POST {"state":"working"} ------>  屏幕显示 Working
     |  失败: POST {"state":"error"}   ------>  屏幕显示 Error
     |                                         |
     |  Stop hook                              |
     |  POST {"state":"idle"}      --------->  屏幕显示 Idle
     |                                         |
     |  Working 状态长期没有 Stop              |
     |  45 秒兜底超时               --------->  屏幕显示 Idle
```

hook 脚本会向 C6 的 HTTP 接口发送请求。设备地址由 `hooks/*.sh` 里的 `ESP32_HOST` 配置。

## 状态说明

| 状态 | 图标 | 文案 | 前景色 | 背景色 | 触发来源 |
| --- | --- | --- | --- | --- | --- |
| Idle | `~` | Idle | `#ffffff` | `#1a1a2e` | `Stop` hook 或 Working 超时兜底 |
| Working | `*` | Working | `#00ff00` | `#1a1a2e` | `UserPromptSubmit`、`PreToolUse`、成功的 `PostToolUse` |
| Approval | `!` | Approval | `#ffc400` | `#1a1a2e` | `Notification` 或 `PermissionRequest` |
| Error | `X` | Error | `#ff3030` | `#1a1a2e` | 失败的 `PostToolUse` |

状态流转：

- 任意状态 -> Working：提交问题、工具开始执行、审批后的工具成功结束。
- 任意状态 -> Approval：Claude Code 请求用户关注或权限审批。
- Working -> Error：工具调用失败。
- Working -> Idle：Claude Code 本轮回复结束，触发 `Stop`。
- Working -> Idle：如果 `Stop` 丢失，C6 固件会在 45 秒后兜底回 Idle。
- Approval / Error -> 下一个状态：后续 hook 事件会覆盖当前状态。

## 硬件

- 主控：Waveshare ESP32-C6
- 屏幕：1.47 英寸 ST7789，172 x 320，IPS，SPI
- 供电：USB
- 通信：WiFi，同一局域网 HTTP 请求

### 引脚配置

| 功能 | GPIO |
| --- | --- |
| SPI MOSI | 6 |
| SPI SCK | 7 |
| SPI CS | 14 |
| DC | 15 |
| RST | 21 |
| Backlight | 22 |

## 网络和配网

### 首次启动配网

没有保存 WiFi 配置时，设备会进入 AP 配网模式：

1. ESP32-C6 创建热点：`claude_claw`
2. 热点密码：`12345678`
3. 屏幕显示热点名、密码和 `192.168.4.1`
4. 手机或电脑连接该热点
5. 浏览器打开 `192.168.4.1`
6. 输入目标 WiFi 的 SSID 和密码
7. 设备保存配置并重启
8. 成功连接 WiFi 后，屏幕短暂显示 IP，然后进入 Idle

连接失败时，设备会重新进入 AP 配网模式。

### 设备地址

设备成功连接 WiFi 后会注册：

- mDNS 主机名：`claude-claw.local`
- HTTP 服务端口：`80`

实际使用中，mDNS 可能受路由器或系统环境影响。更稳定的方式是把 hook 脚本里的 `ESP32_HOST` 改成设备 IP，例如：

```bash
ESP32_HOST="10.0.0.182"
```

## UI 设计

- 屏幕尺寸：172 x 320，竖屏
- 布局：居中的图标和状态文案
- 背景色：统一 `#1a1a2e`
- 前景色：随状态变化
- 字体：Arduino_GFX 内置 ASCII 字体
- 设计目标：远距离可读，状态一眼可见

## 技术栈

| 模块 | 选择 | 说明 |
| --- | --- | --- |
| 屏幕库 | Arduino_GFX | 适配 ST7789，使用简单 |
| HTTP 服务 | WebServer / Arduino | 轻量，足够处理状态请求 |
| 网络 | WiFi + ESPmDNS | 支持局域网访问和 mDNS |
| 配网 | SoftAP + WebServer | 不需要额外 App |
| 配置存储 | Preferences | 使用 NVS 持久保存 WiFi |
| hook 脚本 | Bash + curl | Mac 上无额外依赖 |
| 数据格式 | JSON | 和 Claude Code hook 输入输出习惯一致 |

## 项目结构

```text
claude_claw/
├── README.md
├── LICENSE
├── .gitignore
├── platformio.ini
├── src/
│   └── main.cpp
├── hooks/
│   ├── user_prompt_notify.sh    # UserPromptSubmit -> Working
│   ├── pre_tool_notify.sh       # PreToolUse -> Working
│   ├── post_tool_notify.sh      # PostToolUse -> Working / Error
│   ├── notification_notify.sh   # Notification / PermissionRequest -> Approval
│   └── stop_notify.sh           # Stop -> Idle
├── docs/
│   └── claude-code-setup.md     # Claude Code hook 配置教程
└── config/
    └── settings.json            # Claude Code hooks 配置模板
```

## 快速开始

### 1. 烧录 ESP32-C6

使用 PlatformIO：

```bash
pio run -t upload
```

烧录完成后，设备会重启。如果没有保存过 WiFi，屏幕会进入配网模式。

### 2. 配置 WiFi

1. 手机或电脑连接 `claude_claw`
2. 密码输入 `12345678`
3. 浏览器打开 `192.168.4.1`
4. 输入目标 WiFi 名称和密码
5. 提交后设备重启
6. 屏幕短暂显示设备 IP，然后进入 Idle

### 3. 安装 hook 脚本

```bash
mkdir -p ~/.claude/hooks
cp hooks/*.sh ~/.claude/hooks/
chmod +x ~/.claude/hooks/*.sh
```

把脚本里的 C6 地址改成你的设备 IP：

```bash
sed -i '' 's/^ESP32_HOST=.*/ESP32_HOST="10.0.0.182"/' ~/.claude/hooks/*.sh
```

如果你也要同步修改项目里的默认地址：

```bash
sed -i '' 's/^ESP32_HOST=.*/ESP32_HOST="10.0.0.182"/' hooks/*.sh
```

### 4. 配置 Claude Code

详细教程见：[docs/claude-code-setup.md](docs/claude-code-setup.md)

Claude Code 从 `~/.claude/settings.json` 读取 hook 配置。

如果你没有这个文件，可以直接复制模板：

```bash
cp config/settings.json ~/.claude/settings.json
```

如果你已经有 `~/.claude/settings.json`，不要直接覆盖。把 [config/settings.json](config/settings.json) 里的 `hooks` 字段合并进现有配置，并保留原来的 `permissions`、`model`、`mcpServers` 等字段。

需要添加的 `hooks` 配置如下：

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/user_prompt_notify.sh",
            "timeout": 3
          }
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/pre_tool_notify.sh",
            "timeout": 3,
            "async": true
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/post_tool_notify.sh",
            "timeout": 3,
            "async": true
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
            "command": "~/.claude/hooks/notification_notify.sh",
            "timeout": 3,
            "async": true
          }
        ]
      }
    ],
    "PermissionRequest": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/notification_notify.sh",
            "timeout": 3,
            "async": true
          }
        ]
      }
    ],
    "Stop": [
      {
        "matcher": "",
        "hooks": [
          {
            "type": "command",
            "command": "~/.claude/hooks/stop_notify.sh",
            "timeout": 3,
            "async": true
          }
        ]
      }
    ]
  }
}
```

这些配置的作用：

- `UserPromptSubmit`：提交问题时进入 Working，覆盖纯聊天场景。
- `PreToolUse`：工具开始前进入 Working。
- `PostToolUse`：工具成功后进入 Working，工具失败后进入 Error。
- `Notification`：Claude Code 请求用户关注时进入 Approval。
- `PermissionRequest`：权限审批弹窗出现时进入 Approval。
- `Stop`：Claude Code 本轮回复结束后进入 Idle。
- `timeout: 3`：hook 最多执行 3 秒。
- `async: true`：异步执行 hook，减少对 Claude Code 回复速度的影响。
- `matcher: ""`：匹配所有工具，不要写 `"*"`。

修改 `~/.claude/settings.json` 后，需要完全重启 Claude Code。

### 5. 验证配置

检查 JSON 是否合法：

```bash
python3 -m json.tool ~/.claude/settings.json >/dev/null && echo OK
```

检查 hook 脚本是否可执行：

```bash
ls -la ~/.claude/hooks/*_notify.sh
```

查看 hook 日志：

```bash
tail -f /tmp/claude-claw-hooks.log
```

手动模拟一次完整状态流：

```bash
: > /tmp/claude-claw-hooks.log
printf '{"prompt":"manual"}' | ~/.claude/hooks/user_prompt_notify.sh
printf '{"tool_response":{}}' | ~/.claude/hooks/post_tool_notify.sh
printf '{}' | ~/.claude/hooks/stop_notify.sh
cat /tmp/claude-claw-hooks.log
curl http://10.0.0.182/status
```

正常情况下，日志里应该看到 `http_code=200`，最后设备状态应该是 `idle`。

## 手动测试状态

把下面命令里的地址替换成你的 C6 IP 或 `claude-claw.local`。

```bash
# Idle
curl -X POST http://10.0.0.182/status \
  -H "Content-Type: application/json" \
  -d '{"state":"idle"}'

# Working
curl -X POST http://10.0.0.182/status \
  -H "Content-Type: application/json" \
  -d '{"state":"working"}'

# Approval
curl -X POST http://10.0.0.182/status \
  -H "Content-Type: application/json" \
  -d '{"state":"approval"}'

# Error
curl -X POST http://10.0.0.182/status \
  -H "Content-Type: application/json" \
  -d '{"state":"error"}'

# 查看当前状态
curl http://10.0.0.182/status

# 清除 WiFi 配置并重启到 AP 配网模式
curl -X POST http://10.0.0.182/reset
```

## HTTP API

| 方法 | 路径 | 请求体 | 作用 |
| --- | --- | --- | --- |
| `POST` | `/status` | `{"state":"idle"}` | 显示 Idle |
| `POST` | `/status` | `{"state":"working"}` | 显示 Working |
| `POST` | `/status` | `{"state":"approval"}` | 显示 Approval |
| `POST` | `/status` | `{"state":"error"}` | 显示 Error |
| `POST` | `/status` | `{"state":"idle","delay":12000}` | 延迟 12 秒后显示 Idle |
| `GET` | `/status` | 无 | 返回当前状态和运行时间 |
| `POST` | `/reset` | 无 | 清除 WiFi 配置并重启到 AP 配网模式 |

`GET /status` 返回示例：

```json
{"state":"idle","uptime":123}
```

## 排查

### curl 能控制屏幕，但 Claude Code 没反应

看 hook 日志是否有新内容：

```bash
tail -n 80 /tmp/claude-claw-hooks.log
```

如果没有日志，通常是 Claude Code 没重新加载 `~/.claude/settings.json`。完全退出并重新启动 Claude Code。

### 一直停在 Working

检查是否有 `Stop` hook：

```bash
tail -n 80 /tmp/claude-claw-hooks.log | grep stop_notify
```

正常应该看到：

```text
hook=stop_notify.sh post_state=idle delay_ms=0 host=... http_code=200 rc=0
```

如果没有，说明 `Stop` 没触发或当前 Claude Code 会话没有加载新配置。

### 一直停在 Approval

审批通过后，正常应该出现：

```text
hook=notification_notify.sh post_state=approval ...
hook=post_tool_notify.sh post_state=working ...
```

如果只有 `approval`，没有后续 `working`，说明审批后的工具没有执行或 `PostToolUse` hook 没触发。

### 请求很慢或偶尔失败

hook 脚本里的请求超时配置是：

```bash
curl --connect-timeout 0.5 --max-time 2
```

如果日志里经常看到 `http_code=000` 或 `rc=28`，优先检查：

- C6 IP 是否正确
- Mac 和 C6 是否在同一个 WiFi
- 路由器是否开启了客户端隔离
- C6 WiFi 信号是否太弱

## 许可证

MIT
