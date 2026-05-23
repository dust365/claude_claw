# Claude Code 配置教程

这份文档说明如何把 claude_claw 接到 Claude Code 上，让 ESP32-C6 屏幕跟随 Claude 的状态变化。

## 最终效果

Claude Code 触发的 hook 会向 C6 发送 HTTP 请求：

| Claude Code 事件 | 屏幕状态 | 脚本 |
| --- | --- | --- |
| `UserPromptSubmit` | Working | `user_prompt_notify.sh` |
| `PreToolUse` | Working | `pre_tool_notify.sh` |
| `PostToolUse` 成功 | Working | `post_tool_notify.sh` |
| `PostToolUse` 失败 | Error | `post_tool_notify.sh` |
| `Notification` | Approval | `notification_notify.sh` |
| `PermissionRequest` | Approval | `notification_notify.sh` |
| `Stop` | Idle | `stop_notify.sh` |

实际流程是：

1. 提交问题后进入 `Working`。
2. 需要审批时进入 `Approval`。
3. 审批通过后，工具执行完的 `PostToolUse` 会回到 `Working`。
4. Claude 完成本轮回复后，`Stop` 会立刻回到 `Idle`。
5. 如果 `Stop` 丢失，C6 固件会在 Working 超时后兜底回到 `Idle`。

## 前置条件

先确认 C6 已经连上 WiFi，并能从 Mac 访问：

```bash
curl "http://<C6_IP>/status"
```

`<C6_IP>` 以设备刚连上 WiFi 时屏幕短暂显示的 IP、串口心跳日志或路由器后台为准。如果你的网络能稳定解析 mDNS，也可以使用 `claude-claw.local`。

## 1. 安装 hook 脚本

从项目根目录执行：

```bash
mkdir -p ~/.claude/hooks
cp hooks/*.sh ~/.claude/hooks/
chmod +x ~/.claude/hooks/*.sh
```

然后把设备地址写到统一配置文件：

```bash
cat > ~/.claude/claude-claw.env <<'EOF'
ESP32_HOST="<C6_IP>"
EOF
```

后续设备 IP 变化时只需要改 `~/.claude/claude-claw.env`。不要逐个修改 hook 脚本。没有这个文件时，脚本默认访问 `claude-claw.local`。

## 2. 修改 `~/.claude/settings.json`

Claude Code 从 `~/.claude/settings.json` 读取 hooks。

如果你还没有这个文件，可以直接复制项目模板：

```bash
cp config/settings.json ~/.claude/settings.json
```

如果你已经有 `~/.claude/settings.json`，不要直接覆盖。只需要把下面这个 `hooks` 字段合并进去，保留你原来已有的 `permissions`、`model`、`mcpServers` 等其他配置。

完整需要新增或合并的内容如下：

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

### 这些配置具体改了什么

- `UserPromptSubmit`: 用户提交问题时触发，发送 `working`。这能覆盖“纯聊天、不调用工具”的场景。
- `PreToolUse`: 工具开始前触发，发送 `working`。
- `PostToolUse`: 工具结束后触发。失败时发送 `error`；成功时发送 `working`，用于清掉审批后的 `approval`。
- `Notification`: Claude 需要用户关注时触发，发送 `approval`。
- `PermissionRequest`: Claude 弹出权限审批时触发，发送 `approval`。
- `Stop`: Claude 本轮回复结束时触发，发送 `idle`。
- `timeout: 3`: 给 hook 最多 3 秒执行时间。脚本里的 `curl --max-time 2` 会先超时，避免卡住 Claude。
- `async: true`: 除 `UserPromptSubmit` 外，其余 hook 后台执行，避免影响 Claude 正常回复速度。
- `matcher: ""`: 匹配所有工具。这里必须是空字符串，不要写 `"*"`。

## 3. 重启 Claude Code

修改 `~/.claude/settings.json` 后，需要完全退出并重新打开 Claude Code。已经打开的 Claude 会话不一定会重新加载 hook 配置。

## 4. 验证配置

先检查 JSON 是否合法：

```bash
python3 -m json.tool ~/.claude/settings.json >/dev/null && echo OK
```

检查脚本是否可执行：

```bash
ls -la ~/.claude/hooks/*_notify.sh
```

清空 hook 日志：

```bash
: > /tmp/claude-claw-hooks.log
```

手动模拟一次完整状态流：

```bash
printf '{"prompt":"manual"}' | ~/.claude/hooks/user_prompt_notify.sh
printf '{"tool_response":{}}' | ~/.claude/hooks/post_tool_notify.sh
printf '{}' | ~/.claude/hooks/stop_notify.sh
cat /tmp/claude-claw-hooks.log
curl "http://<C6_IP>/status"
```

正常情况下日志里应该看到 `http_code=200`，最后状态应该是：

```json
{"state":"idle","uptime":123}
```

## 5. 正常测试流程

启动 Claude Code 后，开一个终端持续看日志：

```bash
tail -f /tmp/claude-claw-hooks.log
```

然后在 Claude Code 里测试：

1. 问一个普通问题：应该进入 `Working`，回复结束后回 `Idle`。
2. 触发一个需要权限的操作：应该进入 `Approval`。
3. 点击同意：工具执行完后应该回 `Working`。
4. Claude 回复结束：应该回 `Idle`。

## 排查

### 屏幕完全没反应

先确认 C6 可访问：

```bash
curl "http://<C6_IP>/status"
```

如果 curl 不通，检查 C6 IP、Mac 和 C6 是否在同一个 WiFi、路由器是否开启客户端隔离，以及 `~/.claude/claude-claw.env` 里的 `ESP32_HOST` 是否正确。

### curl 测试可以，Claude Code 没反应

看 `/tmp/claude-claw-hooks.log` 是否有新日志：

```bash
tail -n 50 /tmp/claude-claw-hooks.log
```

如果没有日志，通常是 Claude Code 没加载新的 `~/.claude/settings.json`，完全重启 Claude Code。

### 一直停在 Working

看有没有 Stop 日志：

```bash
tail -n 50 /tmp/claude-claw-hooks.log | grep stop_notify
```

正常应该看到：

```text
hook=stop_notify.sh post_state=idle delay_ms=0 host=... http_code=200 rc=0
```

如果没有，说明 Claude 没触发 Stop 或当前会话没加载新配置。

### 一直停在 Approval

看有没有成功的 `PostToolUse` 日志：

```bash
tail -n 80 /tmp/claude-claw-hooks.log
```

正常审批后应该看到：

```text
hook=notification_notify.sh post_state=approval ...
hook=post_tool_notify.sh post_state=working ...
```

如果只有 `approval` 没有后续 `working`，说明审批后的工具没有进入或没有执行完。

### 请求很慢

脚本里已经限制了：

```bash
curl --connect-timeout 0.5 --max-time 2
```

如果日志里经常出现 `http_code=000 rc=28`，说明到 C6 的网络请求超时。优先检查 C6 的 IP、WiFi 信号和路由器隔离设置。
