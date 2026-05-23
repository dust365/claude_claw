# Codex CLI 配置教程

这份文档说明如何把 claude_claw 接到 OpenAI Codex CLI 上,让 ESP32-C6 屏幕跟随 Codex 的状态变化。

Codex CLI(`openai/codex`)已经实现了和 Claude Code 几乎完全对齐的 hook 系统,所以可以**直接复用 Claude Code 那套 hook 脚本**,只需要新增一份 Codex 的 hooks 配置即可。Codex 和 Claude Code 可以共存,同一台 Mac 上两边的状态都会推到同一块屏。

## 最终效果

Codex CLI 触发的 hook 会向 C6 发送 HTTP 请求:

| Codex 事件 | 屏幕状态 | 脚本 |
| --- | --- | --- |
| `UserPromptSubmit` | Working | `user_prompt_notify.sh` |
| `PreToolUse` | Working | `pre_tool_notify.sh` |
| `PostToolUse` 成功 | Working | `post_tool_notify.sh` |
| `PostToolUse` 失败 | Error | `post_tool_notify.sh` |
| `PermissionRequest` | Approval | `notification_notify.sh` |
| `Stop` | Idle | `stop_notify.sh` |

和 Claude Code 的差异:

- Codex 没有 `Notification` 事件,审批场景由 `PermissionRequest` 单独覆盖。
- `PostToolUse` 的 `tool_response` 是无 schema 的 `Value`,本项目的错误判定脚本只识别常见的 `is_error`/`error` 字段。Codex 某些工具的报错字段如果不在这两个里面,屏幕会停在 Working 而不是切到 Error。这个降级不影响主流程。
- Codex 不存在等价于 Claude Code `Notification` 的"非权限提醒"事件,所以纯关注类提示不会触发 Approval。

## 前置条件

1. 已经按 [claude-code-setup.md](claude-code-setup.md) 把 hook 脚本装到 `~/.claude/hooks/`,并配好 `~/.claude/claude-claw.env` 里的 `ESP32_HOST`。
2. C6 可访问:

```bash
curl "http://<C6_IP>/status"
```

如果你只用 Codex 不用 Claude Code,也需要先装一遍 hook 脚本和环境文件:

```bash
mkdir -p ~/.claude/hooks
cp hooks/*.sh ~/.claude/hooks/
chmod +x ~/.claude/hooks/*.sh

cat > ~/.claude/claude-claw.env <<'EOF'
ESP32_HOST="<C6_IP>"
EOF
```

脚本目录沿用 `~/.claude/hooks/` 是为了让两边共享同一份脚本和环境变量,不用改路径也能升级。

## 1. 安装 Codex hooks 配置

Codex CLI 默认从 `~/.codex/hooks.json` 读取用户级 hook 配置。

如果你还没有 `~/.codex/hooks.json`,直接复制模板:

```bash
mkdir -p ~/.codex
cp config/codex-hooks.json ~/.codex/hooks.json
```

如果你已经有 `~/.codex/hooks.json`,**不要直接覆盖**。把模板里 `hooks` 字段下的每个事件合并进现有文件。同一个事件可以有多组 matcher,把 claude_claw 的那一组追加进去就行。

完整需要新增或合并的内容如下:

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
            "timeout": 3
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
            "timeout": 3
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
            "timeout": 3
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
            "timeout": 3
          }
        ]
      }
    ]
  }
}
```

> **关于 `async`:** Claude Code 那边的 hook 默认开了 `async: true`,本项目的 Codex 模板**没有开 async**。原因是早期 Codex 版本会在启动日志里报 `skipping async hook ... async hooks are not supported yet` 并直接跳过这些 hook。脚本本身有 `curl --max-time 2`,加上 hook 配置的 `timeout: 3` 兜底,同步执行最多让 Codex 等 2 秒,可以接受。如果你的 Codex 版本启动时没有这条警告(说明已支持 async),可以自行把除 `UserPromptSubmit` 外的几个 hook 加上 `"async": true`,以避免阻塞 Codex 回复。

### 这些配置具体改了什么

- `UserPromptSubmit`: 用户提交问题时发送 `working`。
- `PreToolUse`: 工具开始前发送 `working`。
- `PostToolUse`: 工具结束后发送 `working`(失败时发送 `error`)。
- `PermissionRequest`: Codex 弹出权限审批时发送 `approval`。
- `Stop`: Codex 本轮回复结束时发送 `idle`。
- `timeout: 3`: 给 hook 最多 3 秒执行时间。脚本里的 `curl --max-time 2` 会先超时。
- `matcher: ""`: 匹配所有工具。

## 2. 也可以改 `~/.codex/config.toml`

如果你更习惯把所有配置放在 `~/.codex/config.toml`,Codex 同样支持。等价配置(节选 PreToolUse 和 Stop)如下:

```toml
[[hooks.PreToolUse]]
matcher = ""

[[hooks.PreToolUse.hooks]]
type = "command"
command = "~/.claude/hooks/pre_tool_notify.sh"
timeout = 3

[[hooks.Stop]]
matcher = ""

[[hooks.Stop.hooks]]
type = "command"
command = "~/.claude/hooks/stop_notify.sh"
timeout = 3
```

其余事件按同样模式追加。**不要在同一层同时写 `hooks.json` 和 `config.toml` 的 hooks**,Codex 会发警告并提示你只保留一种表达方式。

## 3. 重启 Codex CLI

修改 `~/.codex/hooks.json` 或 `~/.codex/config.toml` 后,需要完全退出并重新打开 Codex CLI。已经打开的会话不会自动重新加载 hook 配置。

## 4. 验证配置

检查 JSON 是否合法:

```bash
python3 -m json.tool ~/.codex/hooks.json >/dev/null && echo OK
```

检查脚本是否可执行:

```bash
ls -la ~/.claude/hooks/*_notify.sh
```

清空 hook 日志:

```bash
: > /tmp/claude-claw-hooks.log
```

用 Codex 实际的 payload 形状模拟一次完整状态流(payload 字段参考 Codex `hooks/src/schema.rs`):

```bash
printf '{"session_id":"s","turn_id":"t","cwd":"/","hook_event_name":"UserPromptSubmit","prompt":"manual"}' \
  | ~/.claude/hooks/user_prompt_notify.sh

printf '{"session_id":"s","turn_id":"t","cwd":"/","hook_event_name":"PostToolUse","tool_name":"shell","tool_input":{},"tool_response":{},"tool_use_id":"x"}' \
  | ~/.claude/hooks/post_tool_notify.sh

printf '{"session_id":"s","cwd":"/","hook_event_name":"Stop"}' \
  | ~/.claude/hooks/stop_notify.sh

cat /tmp/claude-claw-hooks.log
curl "http://<C6_IP>/status"
```

正常情况下日志里应该看到 `http_code=200`,最后状态应该是:

```json
{"state":"idle","uptime":123}
```

## 5. 正常测试流程

启动 Codex CLI 后,开一个终端持续看日志:

```bash
tail -f /tmp/claude-claw-hooks.log
```

然后在 Codex CLI 里测试:

1. 问一个普通问题:应该进入 `Working`,回复结束后回 `Idle`。
2. 触发一个需要权限的操作:应该进入 `Approval`。
3. 同意后:工具执行完后应该回 `Working`,Codex 回复结束后回 `Idle`。

## 排查

### 屏幕完全没反应

先确认 C6 可访问:

```bash
curl "http://<C6_IP>/status"
```

如果 curl 不通,检查 C6 IP、Mac 和 C6 是否在同一个 WiFi、路由器是否开启客户端隔离,以及 `~/.claude/claude-claw.env` 里的 `ESP32_HOST` 是否正确。

### curl 测试可以,Codex CLI 没反应

看 `/tmp/claude-claw-hooks.log` 是否有新日志:

```bash
tail -n 50 /tmp/claude-claw-hooks.log
```

如果没有日志,通常是 Codex 没加载新的 `~/.codex/hooks.json`。完全退出并重新启动 Codex CLI。

也可能是 `hooks.json` 里有语法错误导致整份配置被忽略。Codex 启动时会把 hook 加载警告打到 stderr,可以注意一下命令行输出。

### 一直停在 Working

看有没有 Stop 日志:

```bash
tail -n 50 /tmp/claude-claw-hooks.log | grep stop_notify
```

如果没有,说明 Codex 没触发 Stop 或当前会话没加载新配置。45 秒后 C6 固件会兜底回到 Idle。

### 一直停在 Approval

Codex 没有 `Notification` 事件,纯关注类提示不会清屏;只有审批通过后的 `PostToolUse` 才会把 Approval 切回 Working。如果你看到屏幕长时间停在 Approval,看一下日志:

```bash
tail -n 80 /tmp/claude-claw-hooks.log
```

应该出现:

```text
hook=notification_notify.sh post_state=approval ...
hook=post_tool_notify.sh post_state=working ...
```

只有 `approval` 没有后续 `working` 时,说明审批没进入或对应工具没执行完。

### 工具失败但屏幕没有变 Error

`post_tool_notify.sh` 只识别 `tool_response.is_error` 和 `tool_response.error` 两个字段。如果某个 Codex 工具用了别的报错形式,屏幕会停在 Working。这是已知的降级行为,不影响其余事件。
