# MultiPad4 功能迁移进度

更新：2026-11-10。基线为 notepad4 `3fcfb70fd0d3c8802100c85f7293ccb326aa8b64`，最终标准见 [PROJECT_SCOPE.md](PROJECT_SCOPE.md)。

**当前状态：静态菜单命令入口全部覆盖，现有自动回归通过；完整功能一致性验收进行中。不能据此宣称迁移全部完成。** 本文替换早期“首批功能迁移”的记录；早期列出的许多缺项已经有实现。

## 当前实现范围

| 范围 | 当前代码与接入情况 | 主要实现 / 回归入口 |
|---|---|---|
| MDI 与框架 | 真实 MDI 子窗口、标签切换/重排、层叠/平铺、停靠、关闭保存流程 | `MainFrame.*`、`Frameworks.*`、`DockHost.*`；`SmokeTest.h`、`MDILayoutTests.h`、`CloseReentrancyTests.h` |
| 上游 UI | 原始菜单、快捷键、图像、字符串、32 个对话框模板和语言资源；27 种工具栏按钮、定制布局恢复/保存、打开历史与折叠下拉；13 段状态栏及点击入口 | `UpstreamUI.cpp`、`Localization.cpp`、`FrameCommands.cpp`；`ToolbarOpenTests.h` |
| 文件与编码 | 打开/保存/副本/重载、编码目录和检测转换、换行、文件变量、大文件模式、备份、历史/收藏夹、命令行与外部打开请求 | `FileIO.cpp`、`FileCommands.cpp`、`FileDialogs.cpp`、`CommandLine.cpp`；`FileIOTests.cpp`、`FileCommandSmokeTest.h`、`FileDialogTests.h` |
| 编辑与查找 | 基础及高级选择/行操作、大小写/数字/编码转换、排序/对齐/插入等；正则/通配符/反斜杠查找替换、标记和多选；modeless 对话框绑定文档 | `Edit*.cpp`、`DocumentSearch.cpp`、`SearchDialog.cpp`；`EditorFeatureTests.h`、`EditDialogTests.h`、`SearchDialogFeatureTests.h`、`SearchHelpTests.h` |
| 样式与输入 | 上游完整 lexer 配置目录参与编译，语言检测、方案/字体/主题、折叠/书签、出现项、自动补全/缩进/配对及 LaTeX 输入已有接入 | `StyleManager.cpp`、`StyleDialogs.cpp`、`LexerDetection.cpp`、`Completion*.cpp`；`StyleFeatureTests.h`、`StyleDialogTests.h` |
| 设置与后台行为 | 全局/文档设置分层、INI 保存、自动保存历史、文件监视、会话恢复、窗口选项、语言切换、工具/运行和系统集成 | `AppSettings.cpp`、`BackgroundWork.cpp`、`AutoSave.cpp`、`Session.cpp`、`SystemIntegration.cpp`；`RuntimeFeatureTests.h`、`RuntimeDialogTests.h` 及专项回归 |
| 打印 | 打印、页面设置、页眉页脚/颜色/缩放设置和 Scintilla 排版已有实现 | `Printing.cpp`；实际打印输出仍须专门验收 |

“已有实现”表示源代码和调用入口存在，不表示所有选项及边界行为已逐项与上游核对。应用使用显式文档上下文和移植后的实现，并非直接整体链接上游 SDI 主程序。

## 已有验证证据

- 2026-11-10 15:48，修正 oneAPI 的旧框架模板兼容问题后，IntelLLVM 2026.2.0 / Release 与 MSVC / RelWithDebInfo 均完成构建，各自 CTest **2/2 通过**（26.27 秒 / 21.89 秒）。新增分隔条重复绘制的 GDI 资源回归；日志为 `tmp/oneapi-build.log`、`tmp/oneapi-ctest.log`、`tmp/oneapi-msvc-build.log`、`tmp/oneapi-msvc-ctest.log`。第三方头文件仍有编译警告；原始第三方快照未修改。
- 2026-11-10 15:19，使用 MSVC 包装脚本重新编译后，CTest **2/2 通过**（17.26 秒）。新增 `UIConsistencyTests.h`：13 段状态栏样例、选区/矩形选择/数字分组/大小格式、通知与取消路径、长行限制和临时自动缩进；27 个工具栏路由；204 条已编译快捷键记录（199 条上游 + 5 条 MDI）的目标/归属检查，以及 `Ctrl+N`、`Ctrl+Tab`、`Ctrl+Shift+Tab`、`Ctrl+W`、`Ctrl+F4` 实际快捷键转换。最终日志副本为 `tmp/acceptance-ctest-final.log`。
- 本轮修正状态栏统计与格式、CSV 状态栏入口及取消语义、三个遗漏的快捷键命令、浏览按钮图像及状态栏缩放对话框定位；见缺陷账本 B13–B18。菜单入口复查仍为 **418/418**。完整逐项索引已覆盖 418 个菜单命令、32 个对话框、27 个工具栏按钮、13 个状态栏字段及 199 条上游快捷键记录；索引中的测试引用不是覆盖率或通过判定。
- 2026-11-10 14:51（Asia/Singapore），对当前构建运行 `ctest --test-dir cmake-msvc-build --output-on-failure`：`fileio_roundtrip`、`editor_integration` **2/2 通过**，约 20 秒。此轮未重新编译；所用 `multipad4.exe` 修改时间为 14:47:53，晚于当时最新应用/测试源码。
- 重新运行 `multipad4.exe --migration-report tmp/migration-status-check.json`，再执行 `python tools/check_migration.py tmp/migration-status-check.json`：**418/418 菜单命令已建立路由**。此数字仅覆盖资源清单中的不同菜单命令，不等于全部功能行为通过。
- `editor_integration` 已调用缺陷账本 B01–B12 的对应回归，包括 MDI 布局、语言切换、注册表状态迁移、关闭重入和对话框尺寸手柄。系统集成只操作隔离 HKCU 测试键，不等于真实 HKLM/系统记事本替换验证。
- 当前版本 `--qa-render` 运行成功，输出 `tmp/qa-acceptance-final/` 中 172 张窗口渲染图及 172 份布局记录；本轮浏览了前一轮全部缩略图，并复查最终状态栏和运行对话框图片。示例见 [状态栏](images/mdi-status-acceptance.png)、[运行对话框](images/run-dialog-acceptance.png)。这属于当前环境下的渲染抽查，不能替代真实鼠标、输入法、打印或多显示器 DPI 检查。

## 完整一致性验收

验收记录见 [MIGRATION_ACCEPTANCE.md](MIGRATION_ACCEPTANCE.md)。以原始资源清单逐项建立证据，区分入口覆盖、行为回归、上游一致性比较和真实交互结果。

仍需完成：

1. 原始命令与快捷键的实际行为、可用条件、撤销/重做、只读保护及多文档状态隔离。
2. 动态语言/编码/方案/最近文件菜单，工具栏全部按钮、下拉、定制与重启恢复，状态栏全部字段和点击入口。
3. 32 个对话框的业务语义、确定/取消/应用、非法输入、键盘/鼠标和缩放布局。
4. 全部语言配置与补全/输入法行为，文件编码与保存失败、大文件、后台监视和恢复边界。
5. 实际打印与系统集成，以及拖动、输入法、窄窗口、多显示器 DPI 等真实环境检查。
6. 确认的差异应记录、修复并补回归；全部必要验收通过后再将状态改为完成。
