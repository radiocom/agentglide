# AgentGlide 配置与客户端适配

## Overlay 配置

```toml
version = 1

[overlay]
name = "修复试验"
description = "记录用途；这段文字不会作为任务传给 Agent。"

[agent]
type = "codex"
model = "gpt-6-luna"
base_url = "http://127.0.0.1:1010/v1"
instructions = """
只在当前 Base 工作目录内工作。
先阅读相关文件，修改后运行相关验证。
"""
# executable = 'C:\tools\codex.exe'
# credential_env = "MY_MODEL_KEY"

[agent.codex]
reasoning_effort = "low"
approval_policy = "on-request"

[agent.claude]
effort = "low"
permission_mode = "default"

[agent.copilot]
provider_type = "openai"
wire_api = "responses"
reasoning_effort = "low"
allow_all_tools = false
```

切换 Claude 时把 `agent.type` 改为 `"claude"`，本机模板的 `base_url` 改为 `"http://127.0.0.1:1010"`（无 `/v1`）。不需要每次在命令行重复模型或 Agent 参数。使用该模板验证的是 **Claude Code 客户端 + gpt-6-luna**，不是 Anthropic Claude 模型。

| 字段 | 说明 |
| --- | --- |
| `version` | 必须为整数 1 |
| `overlay.name` / `description` | 必填字符串，只记录和显示，不加入客户端任务、instructions 或启动参数 |
| `agent.type` | codex / claude / copilot |
| `model` / `base_url` / `instructions` | 必填字符串；instructions 可以为空。URL 不允许账号密码、query 或 fragment |
| `executable` | 可选原生 .exe 路径；相对路径基于 Overlay。未设置时通过 PATH 查找，不写死安装用户路径 |
| `credential_env` | 可选环境变量**名称**。凭据值只从显式指定变量读取并传入子进程环境，不写入 `vfs.toml` 或启动日志，不复制共享认证 |
| `agent.codex.reasoning_effort` | minimal / low / medium / high / xhigh，默认 low；提供商须支持相应设置 |
| `agent.codex.approval_policy` | on-request（默认）/ never |
| `agent.claude.effort` | low / medium / high / xhigh / max，默认 low |
| `agent.claude.permission_mode` | default（默认，交给客户端默认行为）/ acceptEdits / dontAsk / plan |

Copilot 使用本机模板时，只需将 `agent.type` 改为 `"copilot"`，保持 `/v1` URL。默认交互模式保留客户端权限确认；自动模式须显式设置 `[agent.copilot] allow_all_tools = true`。该选项允许客户端工具执行，不代表 AgentGlide 提供沙箱。

| Copilot 专属字段 | 说明 |
| --- | --- |
| `provider_type` | openai（默认）/ anthropic / azure；仅 openai 本机 Responses 已实测 |
| `wire_api` | responses（默认）/ completions；Anthropic 使用客户端自身 Messages 适配，URL 不带 /v1 |
| `reasoning_effort` | none / minimal / low（默认）/ medium / high / xhigh / max，实际支持取决于模型 |
| `allow_all_tools` | 布尔值，默认 false；非交互启动要求 true |

使用真正的 TOML 解析器，支持注释、转义、中文、单引号路径、多行字符串。未知字段、重复定义、错误类型、非法选项会报错，不静默忽略。v1 没有任意客户端参数透传。三个客户端专属表可同时保留。

本机 1010 和 gpt-6-luna 仅是可编辑模板，**不是 AgentGlide C 引擎的硬编码**。Codex 使用 Responses 协议；Claude 使用 Anthropic Messages 兼容协议，不能混用两个 URL 的含义。默认本机 Codex 不取用户认证；本机 Claude 使用无秘密的占位 key。远端 Claude 必须设置 credential_env；远端 Codex 是否需要 key 取决于提供商。缺少显式凭据变量或客户端会报错，不登录、不自动复制用户 auth.json。

## Agent 适配

- Codex：独立 `CODEX_HOME=.vfs/clients/codex`，instructions 使用官方 `developer_instructions`。受管理配置作为命令行覆盖传入，避免 Base 内的客户端配置覆盖模型、instructions 和 Fast。始终 `--no-daemon`，禁用 Fast / Ultrafast 和 login shell，不设置 priority service tier。自动模式额外忽略用户配置和自动规则发现，但保留显式 instructions。交互模式仍可读取 Base 中的项目规则；本实现不是恶意项目配置隔离器。
- Claude：独立 `CLAUDE_CONFIG_DIR=.vfs/clients/claude`，instructions 使用 `--append-system-prompt`。采用 bare、空 setting sources、空 MCP、禁用 slash commands，显式开放 Read/Edit/Write/PowerShell，不开放 Bash。启用原生 PowerShell。首次交互可能显示主题、占位 key 确认和目录信任提示，由客户端保存到该 Overlay 的独立目录。
- Copilot CLI：独立 `COPILOT_HOME=.vfs/clients/copilot`，通过 BYOK provider 环境变量连接配置中的服务；无需 GitHub 登录。instructions 写入独立目录的 `AGENTS.md`，通过 `COPILOT_CUSTOM_INSTRUCTIONS_DIRS` 交给客户端读取，原型曾用随机串验证。关闭自动更新、同步、内置 MCP、hooks、memory 和 IDE 自动连接，使用原生 PowerShell；避免复用已有索引服务。交互时先确认 Base 文件夹信任，任务完成后 `/exit` 退出。
- Copilot 额外清除继承的 COPILOT/GITHUB/GH/OTEL 环境，并设置独立 USERPROFILE/XDG 路径；不复制 GitHub 登录认证。Base 的项目指令仍可被客户端读取，description 不作为指令源。远端 provider 的凭据仅通过显式 credential_env 传入；GitHub 订阅路由不是本入口当前适配范围。
- TEMP/TMP/TMPDIR、HOME、APPDATA、LOCALAPPDATA 指向该 Overlay 的 `.vfs/cache`。清除继承的 Codex/Claude/OpenAI/Anthropic 路由和认证环境，只加入本次显式配置。不更改父进程或共享客户端配置。
- descriptions 不拼进模型任务；自动验收用随机串验证 instructions 生效，并检查 description 中要求的文件未被创建。
- Codex 工具使用 danger-full-access 以配合本 Hook 原型，默认仍使用 on-request 审批。这不构成 OS 沙箱，不能替代备份或真正的权限隔离。

参考：[OpenAI 配置文档](https://developers.openai.com/codex/config-reference/)、[CLI 文档](https://developers.openai.com/codex/cli/reference/)、[Claude 环境变量文档](https://code.claude.com/docs/en/env-vars)。原型历史验证版本（本次结果另见 verification.md）：Codex CLI 0.166.0、Claude Code 2.1.314、GitHub Copilot CLI 1.0.105；其他版本/提供商未全面验证。
