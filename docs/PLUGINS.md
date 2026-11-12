# Notepad++ 插件兼容层

MultiPad4 启动时从 **可执行文件所在目录** 的 `plugins/<名称>/<名称>.dll`
加载 Unicode x64 Notepad++ 插件。例如 `plugins/JsonTools/JsonTools.dll`。
`Config`、`disabled` 和其他资源目录不作为插件扫描。无需修改插件 DLL。
插件菜单位于顶层 Plugins 菜单，插件提供的快捷键也会注册。

配置与加载诊断位于 `plugins/Config`，加载日志为
`MultiPad4-plugin-load.log`。DLL 依赖从插件自身目录及 Windows 默认安全搜索路径加载。
缺少导出、位数错误、缺少依赖等加载失败会写入该日志。

兼容层沿用本地 Notepad++ 的公开头文件和初始化顺序（`isUnicode`、
`getName`、`setInfo`、`getFuncsArray`、通知与命令回调）。
固定代理 Scintilla HWND 将请求路由至当前 MDI 文档，直接调用函数指针也经过同一适配。
文档 buffer ID 在文档生命周期中稳定，独立于标签位置和编辑器 HWND。
转发活动文档的 Scintilla 通知，并提供打开、保存、关闭、激活及退出通知。
插件窗格使用项目既有 Klimov 停靠框架，modeless 对话框纳入消息预处理。

notepad4 与 Notepad++ 的 Scintilla fork 有 ABI 差异：兼容层补充旧的范围读取、
样式读取、查找和打印消息，转换 FindTextFull 的结构及样式字节格式，
并保留 Notepad++ fork 的选区长度语义。第三方框架及 Scintilla 源码不作修改。

## 验证

当构建目录内存在全部五个目标 DLL 时，CTest 自动注册 `notepadpp_plugins`。
也可运行 `multipad4.exe --plugin-test`。这是使用真实 DLL 的集成检查，会使用系统剪贴板。
报告写至 `plugins/Config/MultiPad4-plugin-test.log`，窗格截图写至
`plugins/Config/MultiPad4-QA`。普通 `--smoke-test` 不加载外部插件，以保持编辑器回归独立。

覆盖 mimeTools Base64 编解码、NppConverter ASCII/HEX 往返、XMLTools 格式化、
JsonTools 格式化/压缩/树面板、NppExport HTML/RTF 剪贴板导出，以及后台文档隔离和
缓存编辑器句柄在标签切换后的正确路由。

## 范围

这是面向上述五个插件的兼容层，并非完整 Notepad++ 应用。
未实现的 NPPM 消息返回失败；不提供插件管理器/自动安装、外部 lexer 注册、
Notepad++ 双视图与会话 XML 格式、完整暗色主题 API。
仅映射已实现的 Notepad++ 内置菜单命令，避免其 ID 与 notepad4 命令冲突。
网络获取、XSLT/XPath、插件自带设置和其余命令仍需按实际用途逐项验收。
原生插件运行于宿主进程，插件内部崩溃不提供进程隔离。
