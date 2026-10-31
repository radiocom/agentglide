# AgentGlide 开发说明

## 文件结构

```text
README.md                       痛点、构建、最小使用与边界
build.ps1 / package.ps1          MSVC 构建 / 本地 ZIP
dependencies.ps1                恢复并校验固定依赖
dependencies.lock.json          提交、文件与 SHA-256
src/
  agentglide.c                   CoW、枚举、句柄路径、子进程传播
  inject.c / launcher.c          挂起启动、注入和初始化
  common.h                      内部共享结构
  cli.c                         原生 argv 转交 PowerShell 7
  config.c                      TOML 解析到受支持的 JSON 类型
  agentglide.ps1                 路径、配置、绑定、锁与客户端适配
  default-agentglide.toml        首次初始化模板
tests/                          原有测试及诊断源码
docs/                           配置、开发、验证与交付文件清单
licenses/                       原始第三方许可证和署名
THIRD-PARTY-NOTICES.md           来源说明；自有许可证待定
```

以下目录不入库：`prototype/`、`.skills/`（保留的工作区内容），`deps/`（依赖），`build/`、`dist/`（构建和 ZIP），`.agentglide/`（下载、缓存、日志、测试和示例数据）。正式项目直接位于工作区根，没有另建项目子目录。

## 构建与命名

从根目录运行 README 中的 PowerShell 7 构建命令。构建先恢复缺失依赖并核验哈希，再通过 vswhere 定位 x64 工具链。没有使用全局依赖安装或原型的已编译文件。离线构建需预先准备与锁定清单哈希完全一致的 `deps/` 文件。

产品入口、脚本、DLL、配置解析器、启动器、默认配置模板和 ZIP 均以 `agentglide` 命名；构建、加载 DLL 的路径、脚本调用和现有测试同步更新。自有 C 结构/常量/注入函数使用 `AGENTGLIDE_` / `agentglide_` 前缀。编译器仍会产生对应 `.obj`、导入库及 `.exp`，它们仅留在忽略的构建目录。

`vfs.toml`、`.vfs/`、`files/`、`deleted/`、`.deleted`、格式版本和二进制 magic 是现有持久化或内部协议，保持不变，不是正式项目名称。配置语义、默认模型、权限设置和传给客户端的 instructions 机制不变；本次没有增加 CLI、修改系统 Prompt 或扩展引擎能力。

DLL 原始导出 `UsvfsInitialize` 及调用它的 `GetProcAddress` 暂时保留：直接删除会破坏原有 DLL 调用方。跨进程互斥键 `Local\usvfs-c-<hash>` 也暂时保留：单纯改名会使同一实例的新旧进程使用不同的锁。它们是明确待确认的兼容协议例外，不是正式产品名。拟议方案为新增 `AgentGlideInitialize` 并保留旧导出别名；是否保留兼容或接受断开仍需用户决定。确认前不将命名工作标为全部完成。

## 现有入口与数据格式

主入口：`agentglide BASE OVERLAY [--init] [--prompt TEXT] [--non-interactive]`。

低层入口：`agentglide-run.exe BASE INSTANCE -- PROGRAM [ARGS...]`。它保留原有低层布局（`files/`、顶层 `deleted/` 和 trace）；不会执行主入口的 TOML、绑定和独占锁校验。`--layout-v1` 是主入口传给低层启动器的协议参数，不是自动迁移命令。不要在同一 Overlay 混用不同布局或同时使用原型与正式版本。

主入口目录：

```text
OVERLAY/
  vfs.toml                      可编辑配置，启动时不覆盖
  files/                        修改或新建文件
  .vfs/
    state.json                  格式版本及绑定的绝对 Base
    lock                        运行期间独占打开
    deleted/                    删除标记
    clients/                    客户端配置和会话
    cache/                      客户端临时文件与缓存
    logs/                       注入 trace 和启动元数据
```

## 测试

README 列出的本地回归验证 CoW、子孙进程、独立实例、持久化、路径 API、Node realpath、配置/锁/绑定错误和打包。`cli-probe.exe` 是本地测试程序，不能当作真实 Agent 模型验证。`debug-run.c` 为原有诊断源码，不在常规构建和运行 ZIP 中。

真实模型测试会调用已配置的服务并消耗用量，默认沿用本地 1010 / gpt-6-luna 模板，需环境已就绪：

```powershell
pwsh -NoProfile -File .\tests\codex.ps1
pwsh -NoProfile -File .\tests\claude.ps1
pwsh -NoProfile -File .\tests\copilot.ps1
```

Codex/Claude 脚本接受 `-BaseUrl`、`-Model`、客户端路径和超时参数；公共 `tests/agents.ps1` 接受 `-Agent`、`-ClientPath`、`-BaseUrl`、`-Model`、`-TimeoutSeconds`。测试写入独立目录并比较 Base 前后哈希和时间。现有 `tests/interactive.ps1 -Agent codex|claude|copilot -Prompt ...` 用于人工交互任务，退出后检查实际文件，不能只打开界面便算通过。

使用独立 `pwsh -NoProfile -File` 进程运行构建和测试，脚本临时环境变量仅影响该进程。运行痕迹全部保存在项目 `.agentglide/` 中，未自动删除。
