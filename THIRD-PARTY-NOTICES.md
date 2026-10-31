# AgentGlide 第三方来源与许可证

AgentGlide 的本地原型及历史报告保留在工作区 `prototype/`，不加入正式仓库。自有源码整理自其中用户指定的原型；原型未附项目自身许可证，当前也未代选许可证或声明第三方代码为自有代码。公开发布前需确定项目许可证及自有源码的授权来源。

| 依赖 | 固定来源 | 许可证与署名 |
| --- | --- | --- |
| MinHook | https://github.com/TsudaKageyu/minhook/tree/c3fcafdc10146beb5919319d0683e44e3c30d537 | BSD-2-Clause；Tsuda Kageyu；HDE 部分 Vyacheslav Patkov |
| tomlc99 | https://github.com/cktan/tomlc99/tree/29076dfd095bbbbd50a3c1b2760d29f4b83e74ac | MIT；CK Tan |

MinHook 版本通过原型内依赖仓库的 HEAD、origin 和干净工作树核验。tomlc99 版本来自原型的 ORIGIN.md，并将构建用源文件与上述固定提交逐文件 SHA-256 核对。`dependencies.lock.json` 固定构建输入；MinHook 的校验值使用 Git 原始 blob 字节，避免本机 CRLF 转换影响。

完整许可文本：[MinHook 与 HDE](licenses/minhook/LICENSE.txt)、[MinHook 作者](licenses/minhook/AUTHORS.txt)、[tomlc99](licenses/tomlc99/LICENSE)。这些文本保持原样，打包时随运行产物分发。运行文件静态包含上述依赖代码；许可证文本不可因项目更名而去除。依赖源码下载在忽略的 `deps/` 下，不携带依赖自身的 Git 仓库。

Windows SDK/MSVC、PowerShell、Node.js 和各 Agent 客户端是外部工具，不随 ZIP 分发，适用各自的许可和服务条件。第三方许可证不自动成为 AgentGlide 自有代码的许可证。
