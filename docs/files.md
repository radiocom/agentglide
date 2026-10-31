# AgentGlide 正式文件清单

此清单列出本次在根目录准备纳入本地 Git 的全部正式文件。未提交、未创建远程或推送。

```text
.gitattributes
.gitignore
build.ps1
dependencies.lock.json
dependencies.ps1
docs/configuration.md
docs/development.md
docs/files.md
docs/verification.md
licenses/minhook/AUTHORS.txt
licenses/minhook/LICENSE.txt
licenses/tomlc99/LICENSE
package.ps1
README.md
src/agentglide.c
src/agentglide.ps1
src/cli.c
src/common.h
src/config.c
src/default-agentglide.toml
src/inject.c
src/launcher.c
tests/agents.ps1
tests/canonical-path.js
tests/claude.ps1
tests/cli-probe.c
tests/cli.ps1
tests/codex.ps1
tests/copilot.ps1
tests/debug-run.c
tests/fixture.c
tests/interactive.ps1
tests/package-check.ps1
tests/path-fixture.c
tests/paths.ps1
tests/run.ps1
tests/shell-operations.ps1
tests/shell.ps1
tests/try-codex.ps1
tests/verify-delivery.ps1
THIRD-PARTY-NOTICES.md
```

忽略项：`prototype/`、`.skills/`、`deps/`、`build/`、`dist/`、`.agentglide/`、`.vfs/`、历史测试输出、依赖/编辑器缓存及日志/编译中间文件；完整规则见根目录 `.gitignore`。

`prototype/` 中所有原型及历史产物保留；`.skills/` 属于原有工作区内容，不纳入正式项目。仅显式添加本清单文件，未使用 `git add .`。
