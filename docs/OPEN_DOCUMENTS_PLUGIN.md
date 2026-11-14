# OpenDocuments 插件

“打开的文档”停靠面板由 `plugins/OpenDocuments` 中的独立 DLL 实现，
宿主不再创建或维护该列表窗口，也不链接该 DLL。

- 启动时不打开面板；首次从插件菜单打开时停靠在左侧，列表与标签顺序、活动文档、未保存标记同步。
- 双击列表项或按 Enter 切换文档；列表项以稳定的 buffer ID 关联文档。
- 插件 → OpenDocuments 提供显示/隐藏、左/右/底部停靠与浮动命令。
- 面板右键菜单提供停靠、浮动与隐藏；关闭按钮隐藏面板。
- 在插件管理中禁用并重启，或移走插件目录，即可停用面板。

默认构建生成 `plugins/OpenDocuments/OpenDocuments.dll`，CMake 目标为
`OpenDocuments`，`MULTIPAD4_BUILD_OPEN_DOCUMENTS=OFF` 可跳过构建。
版本独立于宿主，由 `plugins/OpenDocuments/CMakeLists.txt` 的
`OPEN_DOCUMENTS_VERSION` 维护，初始值为 `0.1.0.0`。
DLL 的 VERSIONINFO 包含名称、文件/产品版本、描述、功能说明、作者、主页、
版权与许可说明，供资源管理器和插件管理读取。发行目录同时附带 `THIRD_PARTY.md`。

插件沿用 Unicode Notepad++ 入口与生命周期通知，使用
`NPPM_GETNBOPENFILES`、`NPPM_GETBUFFERIDFROMPOS`、`NPPM_GETPOSFROMBUFFERID`、
`NPPM_GETCURRENTDOCINDEX` 和 `NPPM_ACTIVATEDOC` 查询/切换文档。
MultiPad4 扩展定义于 `plugins/PluginApi.h`：

- `MPN_DOCUMENTSCHANGED`：宿主刷新文档状态后同步通知，包括 Fast View 文档。
- `MPM_GETBUFFERTITLE`：wParam 为 buffer ID，lParam 指向 `MultiPadBufferTitle`；
  text 为空时查询 UTF-16 字符数（不含终止符），否则校验 capacity 后复制标题。
  成功返回字符数，无效文档/请求或容量不足返回 -1。

停靠、浮动、拖动和分隔条仍由 Klimov 框架实现，未修改 vendored 源码。
该插件需要 MultiPad4 扩展，不宣称可直接在 Notepad++ 内运行。

`editor_integration` 通过加载真实 DLL 检查列表同步、修改标记、文档激活、
停靠/浮动/隐藏、元数据与关闭后的卸载；`editor_without_plugins` 验证无插件运行。
`--qa-render <目录>` 可导出停靠与浮动面板的截图。
Windows CI 的 MSVC 系列与 MinGW 系列包均包含该插件；MinGW 同时收集 DLL 的运行库依赖。
