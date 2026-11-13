# 项目目标与验收范围

项目正式名称为 **MultiPad4**，目标是 Windows only、基于 WTL 框架、支持标签的 MDI 编辑器。功能和交互以本地 notepad4 源码为基准，包括全部菜单项、工具栏、状态栏和对话框；产品差异仅来自将 SDI 调整为多文档。首版原型只是历史基础，不代表最终功能范围。

产品菜单、对话框和提示统一使用 MultiPad4。上游多语言资源由 `cmake/Notepad4Resources.cmake` 在构建时转换文案，保留第三方原件。notepad4 名称仅用于上游来源、署名、许可证、迁移基线及旧配置兼容；上游 UI 清单中的原始标签也予以保留。

## 已确定的框架

- 标签与 MDI：Daniel Bowen 的 WTL Tabbing Framework，使用 `CTabbedMDIFrameWindowImpl`、`CTabbedMDIChildWindowImpl`、`CTabbedMDIClient` 和 `CDotNetTabCtrl`。
- Docking：Sergey Klimov 的 WTL Docking Framework，使用 `CDockingFrameImplBase` 和 `CTitleDockingWindowImpl`。
- 来源：Programmer's Notepad 的框架目录，固定提交 `618ea4ad538b26a8795b1cdaa684f08b72d31d09`。该项目同时使用这两套库，可参考集成方式，但应用功能仍以 notepad4 为准。
- 正式构建不再使用 `CTabViewImpl` 或自行实现停靠、浮动、拖动和分隔条算法。`DockHost` 仅作为第三方框架的应用适配层。
- 保留上游源码及作者许可；MSVC 兼容变换在 `cmake/FrameworkCompatibility.cmake`，只生成构建目录副本。

## notepad4 基线

源码版本为 `3fcfb70fd0d3c8802100c85f7293ccb326aa8b64`。

- 应用源码、编辑命令、全部 lexer 配置、对话框和资源：`3rdparty/notepad4/src`。
- 图标、不同 DPI 的工具栏位图等：`3rdparty/notepad4/res`。
- 优化版 Scintilla：`3rdparty/notepad4-scintilla/scintilla`。
- `tools/inventory_notepad4.py` 可从原始源码重新生成 `docs/notepad4-ui-inventory.json`。

清单包括条件编译分支，因此它是上游静态资源全集，不是某个运行配置的菜单快照。当前统计为 448 条菜单记录、418 个不同菜单命令、32 个对话框资源、27 个工具栏命令、13 个状态栏枚举项（含空白分隔项）。动态生成的语言/编码/最近文件/方案菜单还需要运行时验收。344 个原始 WM_COMMAND case 不能与菜单数量直接一一比较，部分命令通过范围或其他入口处理。

## MDI 适配规则

| 上游行为 | MDI 行为 |
|---|---|
| 新建 / 打开 / 最近文件 / 收藏夹 / 外部打开请求 | 创建或激活目标文档标签；“新窗口”保留创建另一主框架的语义 |
| 编辑、编码、换行、lexer、书签、折叠、缩放、只读 | 对活动文档执行；切换文档更新菜单勾选、工具栏和状态栏 |
| 保存 / 另存为 / 保存副本 / 时间戳 / 打印 | 使用对应文档的路径、编码、修改标记与打印状态 |
| 查找 / 替换 / 标记所有 / modeless 对话框 | 明确绑定目标文档；切换文档后不能继续修改错误的 Scintilla 实例 |
| 自动保存 / 文件监视 / 重新加载 / 定时器 | 状态及回调按文档归属；后台文档也保持正确行为 |
| 全局首选项 / 主题 / 字体 / 快捷键 / 工具栏定制 | 主框架或应用设置；按上游语义更新已打开和新建的文档 |
| 关闭文档 / 关闭全部 / 退出 | 每个文档的保存、放弃、取消有独立结果；取消和失败停止关闭流程 |
| 系统菜单 / Ctrl+Tab / 层叠 / 平铺 | 使用真实 MDI 子窗口及第三方标签框架，保持 HWND 与文档关联 |

## 迁移边界

保留原始命令 ID、资源、图标和对话框过程。复用 `Edit.cpp`、`EditEncoding.cpp`、`EditAutoC.cpp`、`Styles.cpp`、`Dialogs.cpp` 等实现；将 `Notepad4.cpp` 中单主窗口的调度拆成 WTL 主框架调度和文档调度。

首先必须拆分文档上下文：`hwndEdit` / `g_hScintilla`、文件路径、编码与 EOL、`fvCurFile`、修改与只读标记、lexer 和语言索引、查找替换实例、自动保存记录、文件监视句柄和时间戳。随后审核对话框静态变量、通知、异步标记任务和定时器；仅替换一个全局编辑器句柄不足以完成 MDI 迁移。

## 当前完成度

已完成：两套网上框架的源码引入、现代 MSVC 兼容、真实 MDI 宿主替换，以及现有基础功能的回归。测试验证 MDICLIENT、WS_EX_MDICHILD、文档隔离、标签重排、层叠/平铺、保存和停靠状态转换。

后续迁移已经接通资源清单中的全部 418 个不同菜单命令，并扩展文件/编码、高级编辑/查找、上游 lexer 配置、补全、对话框、设置持久化、后台任务和会话恢复等实现。业务命令采用显式文档上下文。详细范围和验证见 [MIGRATION_STATUS.md](MIGRATION_STATUS.md)。

**尚未完成完整功能一致性验收。** 当前命令路由检查及两项 CTest 通过，但不代表全部命令、工具栏、状态栏、对话框及设置行为都已与上游逐项验证。应用使用移植后的显式文档后端；不能把“资源已编译”“菜单已列出”或“底层 lexer 已编译”视为功能已实现。逐项证据和差异见 [MIGRATION_ACCEPTANCE.md](MIGRATION_ACCEPTANCE.md)。

验收时逐项检查原始菜单命令及快捷键、动态菜单、工具栏自定义和下拉按钮、状态栏点击、全部对话框确认/取消路径，并对多个不同编码/语言/修改状态的文档交替操作。MDI 状态隔离和功能一致性都通过后，才算达到项目目标。
