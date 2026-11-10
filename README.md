# MultiPad4

目标是 **Windows only、WTL 框架、支持标签的 MDI notepad4**。完整保留 notepad4 的菜单、工具栏、状态栏、对话框及编辑功能，并将 SDI 行为适配为 MDI。完整目标、状态隔离规则和验收范围见 [PROJECT_SCOPE.md](docs/PROJECT_SCOPE.md)。

当前已完成真正的框架替换，并开始接入上游 UI 资源与首批 MDI 命令，**尚未完成 notepad4 的完整功能迁移**。已接通功能、禁用项和剩余依赖见 [MIGRATION_STATUS.md](docs/MIGRATION_STATUS.md)。

## 框架与源码

- WTL 10：`3rdparty/wtl`。
- Daniel Bowen 的 WTL Tabbing Framework：`3rdparty/TabbingFramework`。
- Sergey Klimov 的 WTL Docking Framework：`3rdparty/DockingFramework`。
- notepad4 应用源码与资源基线：`3rdparty/notepad4`。
- notepad4 优化版 Scintilla：`3rdparty/notepad4-scintilla/scintilla`。

主框架以 Docking Framework 的 `CDockingFrameImplBase` 包装 `CTabbedMDIFrameWindowImpl`；文档窗口派生自 `CTabbedMDIChildWindowImpl`。应用使用真实 `MDICLIENT` 和 `WS_EX_MDICHILD` 子窗口。原先自建的停靠算法及 `CTabViewImpl` 已退出正式实现。

两个老框架的上游文件保持原样，`cmake/FrameworkCompatibility.cmake` 在构建目录生成兼容现代 MSVC 的副本。来源、固定提交和许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。

## 构建

需要 Windows 10/11、VS 2022 的 C++ 桌面开发组件（含 ATL 与 Windows SDK）、CMake 3.25+、Ninja 和 C++20。无需 Qt 或 MFC。

本机使用既有 MSVC 包装脚本（首次先创建 `cmake-msvc-build` 目录）：

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build --output-on-failure
.\cmake-msvc-build\multipad4.exe + .\src\MainFrame.cpp .\src\FileIO.cpp
```

其他机器可在 x64 VS Developer Command Prompt 中使用 `msvc-x64` CMake configure/build/test presets。正式构建不依赖 `tmp` 或在线下载。

GitHub Actions 的 [MSVC Build](.github/workflows/msvc-build.yml) 在 push、PR 或手动触发时，使用 Windows 2022 / VS 2022 编译 x64 Release。成功后可从运行页面的 `multipad4-msvc-x64` artifact 下载 `multipad4.exe` 和配套的 `Notepad4 DarkTheme.ini`；该工作流只构建程序，不运行测试。

## 当前可运行功能

- 多文档标签、拖动排序、独立撤销栈、Ctrl+Tab、原生 MDI 层叠/平铺。
- 新建、打开多个文件、拖入文件、命令行文件参数、保存、另存为、全部保存和关闭保护。
- C/C++、Python、JSON 基础高亮、普通文本查找、自动换行。
- 文档面板停靠/浮动/隐藏；标题栏拖动、双击和分隔条由 Docking Framework 实现。
- 原始 notepad4 菜单、快捷键和图像工具栏；首批编辑/视图/编码/换行命令、保存副本、还原及分段状态栏。
- 使用上游模板的查找/替换、跳转、缩放、长行设置和关于对话框；查找/替换窗口绑定目标文档，关闭目标时销毁。
- UTF-8 和带 BOM 的 UTF-8/UTF-16 读写、保存保留编码与换行、临时文件替换保存。当前基础文件层限制 256 MiB，不代表最终 notepad4 的文件能力。

完整上游资源已进入可执行文件，但只接通首批业务过程，其余命令与高级对话框选项禁用；不得据此声称达到 notepad4 功能一致性。上游完整文件操作、编码检测、设置、lexer 样式、高级查找替换、自动保存和文件监视仍需迁移。

## 验证

- `fileio_roundtrip`：编码、中文/emoji、换行、空文件、错误输入和失败保存保护。
- `editor_integration`：真实 MDI 子窗口、文档隔离、标签重排、查找、C++ 词法样式、文件保存、窗口层叠/平铺和第三方停靠状态。
- `python tools/inventory_notepad4.py`：重建上游 UI 资源清单。清单不是功能通过报告。

界面截图、鼠标拖动、输入法和多显示器 DPI 仍待人工验收。本次图形检查的 Windows UI 服务返回 `Computer Use native pipe is unavailable`。
