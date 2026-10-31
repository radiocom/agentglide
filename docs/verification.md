# AgentGlide 本地验证

验证日期：2026-11-01（Asia/Shanghai）。本次只做本地包装，不沿用历史原型的通过结论，不代表 Task 已验收或关闭。

## 环境与构建

Windows x64 `10.0.26200.0`，PowerShell `7.6.9`；使用本机 vswhere 找到的 MSVC x64 C 工具链和 Windows SDK。Node.js 用于路径探针。客户端为 Codex CLI `0.166.0`、Claude Code `2.1.316`、GitHub Copilot CLI `1.0.105`。

以下命令在项目根目录执行：

```powershell
pwsh -NoLogo -NoProfile -File .\dependencies.ps1
pwsh -NoLogo -NoProfile -File .\build.ps1
```

固定依赖从上游提交恢复到 `deps/`，逐文件 SHA-256 校验通过；未使用原型内编译产物、未全局安装。构建退出 0；自有源码和测试无编译警告，MinHook/tomlc99 仍有原有转换等警告，完整日志在 `.agentglide/logs/build.log`。

验证 DLL 为 `build/agentglide/agentglide.dll`，SHA-256：`45372B4E8DE42ED8DEFDB85C345A76D477999C5B6756E3706098601165785C9A`。dumpbin 确认 x64，DLL 和低层启动器的直接运行依赖为 `KERNEL32.dll`。命名兼容决策未返回，因此本次验证的 DLL 仍保留原始初始化导出；不能把它当作已完成最终导出更名的构建。以后改变导出后需要重建并验证调用链。

## 已执行测试

下表中的脚本均使用 `pwsh -NoLogo -NoProfile -File .\tests\<脚本>` 执行。证据根为 `.agentglide/tests/`，每次运行保留 `result.json`，不提交日志、客户端缓存或模型原始响应到正式 Git。

| 脚本 | 结果 | 本次证据目录 |
| --- | --- | --- |
| `run.ps1` | 通过；60 个 PASS，另含 x86 恢复前拒绝；原文件不变 | `fixture-20261101-005451-311` |
| `cli.ps1` | 36 项全部通过；配置、绑定、复用、错误路径、并发锁和注入失败关闭 | `cli-20261101-005451-315` |
| `paths.ps1` | 原生路径 API、Unicode、同进程复制句柄及 Node realpath 通过 | `paths-20261101-005451-310` |
| `shell.ps1` | 6 个 PASS；PowerShell、嵌套子进程和真实 Codex 本地 apply_patch | `shell-20261101-005451-282` |
| `codex.ps1` | 真实模型 14 项通过，退出 0，无超时 | `codex-20261101-005511-529` |
| `claude.ps1` | 真实模型 14 项通过，退出 0，无超时 | `claude-20261101-005521-746` |
| `copilot.ps1`（首次） | **失败**：15 项中 instructions 内容检查未通过，外层脚本退出 1 | `copilot-20261101-005532-574` |
| `copilot.ps1`（相同脚本复跑） | 15 项通过，退出 0，无超时；没有修改提示词或放宽断言 | `copilot-20261101-005705-901` |

真实模型使用本机已有 `gpt-6-luna` 服务：Codex/Copilot 使用 `http://127.0.0.1:1010/v1`，Claude 使用根地址 `http://127.0.0.1:1010`。这里是 Claude Code 客户端配合兼容服务，不是 Anthropic Claude 模型测试。测试没有复制共享认证或改变共享配置。

三个客户端实际执行了读取、修改、新建、文件删除、重命名和子 PowerShell 写入；修改位于 Overlay，删除标记正确，Base 前后 SHA-256 和修改时间一致。Copilot 首次把 `instructions.txt` 写成通用成功说明，未匹配配置中的随机串；客户端退出 0 仍被外层检查判为失败。复跑成功不能消除首次失败，根因尚未确定。

README 的 `--help`、新目录 `--init`、相同两目录重复 `--init` 均已实跑，配置哈希保持不变；证据在 `.agentglide/logs/readme-*.txt` 和 `readme-check.json`。实际文件操作由上表本地及真实模型流程覆盖，不把初始化等同于模型端到端通过。

## 包装与边界检查

运行命令：

```powershell
pwsh -NoProfile -File .\package.ps1
pwsh -NoProfile -File .\tests\package-check.ps1
pwsh -NoProfile -File .\tests\verify-delivery.ps1
```

打包和最终检查结果见 `.agentglide/logs/package-check.json`、`.agentglide/logs/final-check.json`。产物为 `dist/agentglide-win-x64.zip`，六个运行文件、文档、依赖锁定清单和原始法律文本一起交付；ZIP 不含测试数据或原型。

原型 95 个相关源码、构建、文档和依赖文件在整理前后 SHA-256 一致，记录在 `.agentglide/logs/prototype-preservation.json`。未修改另一原型或 `.skills/`，未清理任何原型数据。原型中庞大的历史缓存/输出未逐文件散列，本次没有向这些目录执行写入。

根目录已建立本地 Git `main` 分支，正式文件按 [文件清单](files.md) 显式暂存，未用 `git add .`。无 commit，无 remote，无推送。忽略规则已用 `git check-ignore -v` 对原型、工作区技能、依赖、构建、ZIP 和运行数据逐项检查。MinHook 原始法律文本保留 CRLF/BOM；`.gitattributes` 对法律文本保持原始字节并识别 CRLF，不修改其内容。

## 尚未完成或验证

- 用户尚未决定 DLL 旧导出和跨版本互斥协议的兼容处理，命名不是全部完成；具体冲突和拟议方案见开发说明。
- 没有可用的 Scout Session 创建/绑定工具，因此未声称创建了平台 Session，也未验收或关闭指定 Task。
- 项目自身许可证待用户决定；没有远程仓库。公开发布前仍需确认确切账号、目标、许可证和公开内容。
- 本次未执行交互 TUI、完整 Git/worktree 工作流、大型仓库构建、性能/空间基准、远端提供商或其他平台测试。
- 没有扩展功能或调整架构来解决 Copilot 首次指令遵循失败；原始失败保留，限制写入 README。
