# AgentGlide

**面向 Coding Agent 的轻量工作目录：共享一份 Base，只把修改写入独立 Overlay。**

多个 Agent 同时尝试修复、重构或实验时，多个 Git Worktree 的工作文件、依赖和构建产物会占用额外磁盘空间。AgentGlide 使用 Windows 用户态文件 Hook 提供写时复制视图：未修改的文件从 Base 读取，首次写入时复制该文件到 Overlay，新文件及删除记录也留在 Overlay，从而避免为每次实验预先复制整个工作目录。

当前是 **Windows x64 实验性实现**。它提供适合 Coding Agent 的工作目录视图，**尚未实现 Git 分支或 `git worktree` 生命周期管理**，没有自动提交、合并或回收命令。完整 Git 工作流和大型仓库构建尚未验证。它也不是安全沙箱，Base 外访问不隔离。

适用于受控的代码编辑试验、同一 Base 的多个独立任务，以及需要退出后保留修改再继续的场景。实际磁盘用量取决于被修改的文件、客户端缓存和构建输出；没有节省比例或性能保证。

## 平台与依赖

- Windows x64；不支持 x86/WOW64、ARM64、Linux 或 macOS。
- PowerShell 7，`pwsh.exe` 必须在 PATH。
- 构建需要 Visual Studio C++ Build Tools（MSVC x64 C 工具链、Windows SDK 和 vswhere）。
- 使用 Agent 时需自行安装原生 `codex.exe`、`claude.exe` 或 `copilot.exe`，并准备相应模型服务；也可在配置中指定原生可执行文件路径。
- 构建依赖 MinHook、tomlc99，版本和逐文件 SHA-256 固定在 `dependencies.lock.json`。首次构建从固定的 GitHub 提交下载到本项目 `deps/`；不全局安装依赖。后续构建校验并复用本地文件，运行时不下载这些依赖。

第三方原始名称、署名和许可证见 [第三方声明](THIRD-PARTY-NOTICES.md)。项目自身许可证尚未选定，本次为本地交付。

## 构建

在项目根目录运行：

```powershell
pwsh -NoLogo -NoProfile -File .\build.ps1
```

产物在 `build/agentglide/`，日志在 `.agentglide/logs/build.log`。C 源码以 `/TC /std:c11 /MT` 编译。依赖缺失或哈希不匹配会终止构建；已有不匹配文件不会被覆盖。

打包为可解压使用的本地 ZIP：

```powershell
pwsh -NoLogo -NoProfile -File .\package.ps1
```

输出 `dist/agentglide-win-x64.zip`。以下六个运行文件必须放在一起，不能只复制 EXE：

```text
agentglide.exe
agentglide.ps1
agentglide-config.exe
default-agentglide.toml
agentglide-run.exe
agentglide.dll
```

ZIP 中也包含 README、使用文档和第三方许可证。解压版直接运行所在目录的 `agentglide.exe`；构建和测试命令需要完整源码目录。

## 最小使用流程

主入口是 `agentglide <BASE> <OVERLAY>`。Base 必须存在；Overlay 使用新的空目录，或同一 Base 已绑定的 Overlay。两者不能相同或互相包含。相对路径基于当前终端目录，带空格的路径须加引号。

以下示例在项目专用、已忽略的目录内创建试用数据。首次使用先生成配置，避免直接启动默认模型：

```powershell
# 在源码根目录执行；请为每个新实验选择新的目录名。
New-Item -ItemType Directory -Force .agentglide\examples\base | Out-Null
# 如要放入自己的代码，先准备 Base，再启动；不要把 Overlay 放进 Base。
.\build\agentglide\agentglide.exe .agentglide\examples\base .agentglide\examples\trial-1 --init
notepad .agentglide\examples\trial-1\vfs.toml
```

编辑生成文件中的 `agent.type`、`model`、`base_url`，必要时设置 `executable` 和 `credential_env`。默认模板使用 `codex`、`gpt-6-luna`、`http://127.0.0.1:1010/v1`，**只是沿用的本地示例，不表示机器上已有该服务或本项目提供模型**。Claude 需要 Anthropic Messages 兼容服务根地址（不带 `/v1`）；Codex 默认使用 Responses。字段与客户端差异见 [配置说明](docs/configuration.md)。

配置完成后启动交互任务：

```powershell
.\build\agentglide\agentglide.exe .agentglide\examples\base .agentglide\examples\trial-1
# 或附带初始任务：
.\build\agentglide\agentglide.exe .agentglide\examples\base .agentglide\examples\trial-1 --prompt "阅读当前目录并说明下一步修改计划"
```

自动任务使用同一入口：

```powershell
.\build\agentglide\agentglide.exe .agentglide\examples\base .agentglide\examples\trial-1 --non-interactive --prompt "创建 hello.txt，内容为 Hello AgentGlide，并读回确认"
```

Copilot 非交互模式必须显式设置 `[agent.copilot] allow_all_tools = true`。执行会消耗配置服务的用量。凭据使用 `credential_env` 指定的环境变量名称，不把密钥写入 TOML；入口不会复制共享登录认证。配置中的 `instructions` 传给客户端，`overlay.description` 仅作显示元数据。

Agent 的工作目录仍为 Base。注入进程读取合并视图，普通终端/编辑器看到的 Base 仍是物理原文件。修改位于 `OVERLAY/files/`，删除标记位于 `OVERLAY/.vfs/deleted/`。退出后再次以相同两目录启动会恢复文件视图并重新读取配置，不会覆盖配置，也不会自动续接旧聊天。需要另一组修改时，用同一 Base 和另一 Overlay。

Codex/Copilot 可用 `/exit` 退出；当前 Claude 最小适配禁用 slash commands，连续两次 Ctrl+C 退出。不要直接照抄客户端原生 resume 命令绕开 AgentGlide。

## 保留修改与回收空间

AgentGlide **不自动将修改合并回 Base**。先退出该 Overlay 的全部 Agent 和子进程，再审查 `files/` 与 `.vfs/deleted/`。仅复制 `files/` 不能表达删除操作；需要人工审查后把选定变更应用到目标代码库。

保留整个 Overlay 可继续使用。放弃实验时，在确认无需保留修改、会话和日志后，手工删除该实验的整个 Overlay；没有内置 cleanup 命令。不要在运行时删除 `.vfs/`、状态或删除标记。`lock` 文件存在本身不表示会话仍在运行。

项目的 `build/`、`dist/`、`deps/` 和 `.agentglide/` 均被忽略；其中 `.agentglide/` 也可能保存你按示例创建的 Overlay，不能当作全部可丢弃的缓存。不要清理共享工作区资源或保留的 `prototype/`。

## 限制与已知问题

- 这是合作式用户态 Hook，不提供 OS 安全隔离。Base 外 I/O 原样放行，直接系统调用、已有可写句柄、可写 mmap、外部服务、未覆盖的进程创建路径及 daemon 没有隔离保证；loader/TLS 初始化可能早于 Hook。
- Base 不是一致性快照，外部进程仍可修改它；CoW、删除记录不是完整事务系统。多个 Overlay 可共享 Base，单个 Overlay 只允许一个主入口会话。
- 不支持 Git Bash/MSYS fork。目录删除/重命名、硬链接、跨 Base 边界的重命名/链接在覆盖的调用处拒绝。
- 主入口支持本地盘子目录，规范化路径最多 220 字符；拒绝重叠路径、reparse point/junction、UNC、卷根、ADS、8.3 别名及尾随点/空格等路径。非空无配置目录不接管，绑定信息丢失不自动重绑，旧格式不自动迁移。
- 句柄名称虚拟化覆盖 GetFinalPathNameByHandleW/A 和 NtQueryInformationFile 的名称信息类 9/48、同进程复制与打开后重命名；不覆盖 NtQueryObject、FileAllInformation 或跨进程/继承句柄标记传递。
- 尚未验证完整 Git 操作、大型构建、目录通知、异步/APC 枚举、完整 ACL/ADS 保真、崩溃事务恢复与压力性能。不能据此宣称支持任意 Coding Agent、版本、提供商或生产工作流。
- 本次 Copilot 真实测试曾未遵循配置中的 instructions 随机串要求，相同脚本复跑通过；首次失败保留，原因未定，不能保证模型每次遵循指令。详见验证记录。
- 注入失败终止目标，返回 125，不退回普通运行；配置/路径错误返回 2，目标创建失败通常为 3，客户端退出码透传。失败不会自动清除已有修改。

## 开发与验证

```powershell
pwsh -NoProfile -File .\tests\run.ps1
pwsh -NoProfile -File .\tests\cli.ps1
pwsh -NoProfile -File .\tests\paths.ps1
pwsh -NoProfile -File .\tests\shell.ps1
pwsh -NoProfile -File .\tests\package-check.ps1
pwsh -NoProfile -File .\tests\verify-delivery.ps1
```

`paths.ps1` 另需 Node.js；`shell.ps1` 另需实际 Codex 原生程序（本地 apply_patch，不请求模型）；打包验证须先执行 `package.ps1`。结果在 `.agentglide/` 下，必须检查退出码及 `result.json` 的 `passed`。真实 Agent 测试、源码布局和协议边界见 [开发说明](docs/development.md)。本次实际结果见 [验证记录](docs/verification.md)，历史原型结果不等于本次验证。
