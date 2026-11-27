# MultiPad4

[English](README.md) | 简体中文

**MultiPad4** 是面向 Windows x64 的标签式 MDI 文本编辑器，使用 C++20、Win32 和 notepad4 优化版 Scintilla。它将 notepad4 的编辑功能扩展到多文档工作流，支持按需大文件编辑、Notepad++ 风格的查找界面和可停靠插件面板。

从当前源码构建的程序在“关于”对话框中显示 `v0.5`，Windows 可执行文件的版本信息为 `0.5.0.0`。

官方发布版本会自动检查更新。可通过 **帮助 → 检查更新** 手动检查，或通过 **帮助 → 自动检查更新** 更改设置。确认更新后，MultiPad4 将使用 GitHub 提供的 SHA-256 校验与当前编译器和 SIMD 指令集匹配的安装包，然后关闭并重启。安装目录需要可写。

设置默认保存在 `%APPDATA%/MultiPad4/MultiPad4.ini`，可通过 `MULTIPAD4_SETTINGS` 指定其他 INI 路径。要启用便携模式，请在可写的 `MultiPad4.exe` 所在目录创建空文件 `.portable`。下次启动时，设置和退出会话会保存到 `data`，未命名草稿的自动保存备份会写入 `data/AutoSave`。便携模式优先于 `MULTIPAD4_SETTINGS`。移动整个目录前，请先退出 MultiPad4。

与 notepad4 的行为一致性仍在逐项验收。参见[项目范围](docs/PROJECT_SCOPE.md)、[迁移记录](docs/MIGRATION_STATUS.md)和[验收清单](docs/MIGRATION_ACCEPTANCE.md)。

## 当前功能

- **多文档与停靠。** 支持真实 MDI 子窗口、标签切换和拖动排序、独立撤销栈、Ctrl+Tab、层叠和平铺，每个标签提供关闭按钮。右键单击标签可关闭当前、左侧、右侧、其他或全部标签。已有磁盘文件的标签还提供 Windows Shell 命令。搜索结果和插件面板可停靠、浮动或隐藏。通过**设置 → 文档标签设置**调整标签标题字体、字号和宽度，可启用固定宽度。文档最大化时，菜单栏旁显示文件类型图标。
- **文件与编码。** 支持多文件打开、拖放、命令行参数、单实例复用、保存、另存为、保存副本和全部保存。支持 UTF-8、UTF-16 和代码页检测与转换，保留 BOM 和换行符，并支持文件变量、备份、历史和收藏夹。自动解码失败时，编辑器使用 ISO-8859-1（Latin-1）将文件作为文本打开，未编辑的内容以该编码保存时保留原始字节。显式选择 UTF-8 时仍会严格校验。文件在外部发生变化时，可根据监视模式提示重载，或自动重载未修改的文档。
- **编辑与语言。** 支持高级选择与行操作、转换、排序、对齐、插入、语言检测、折叠、书签、匹配项高亮、自动补全、自动缩进、自动配对及 LaTeX 输入。**查看 → 修改历史标记**以蓝色显示未保存修改、绿色显示已保存修改；须在编辑前启动追踪，按需大文件模式不提供此功能。
- **查找与替换。** 无模式对话框提供查找、替换、文件中查找和标记，支持普通文本、扩展转义、正则表达式、计数，以及在当前文档或全部打开文档中查找和替换。Ctrl+F 聚焦查找输入框，Ctrl+H、Ctrl+Shift+F 和 Ctrl+M 打开其他对应页。F7 显示结果，支持折叠、命中高亮和跳转。查找对话框提供英文和简体中文界面。[查找界面](docs/SEARCH_MIGRATION.md)
- **目录搜索与替换。** 通过 Windows 线程池运行最多 8 个搜索任务，支持文件过滤、递归、隐藏目录选项、取消和错误统计。已打开的文件按当前缓冲区搜索，包含未保存的修改。大型解码快照和密集匹配列表会写入临时文件，结果分页显示。符合条件的区分大小写字面量搜索流式处理解码文本，不为未命中的文件保留快照。目录遍历使用对话框中的筛选条件，独立于 `.gitignore` 规则。替换逐文件处理：未打开文件在后台匹配和保存，提交前校验文件版本；已打开的普通文档使用修订号校验快照，在 UI 线程将准备好的修改应用为一次撤销操作。已打开的映射文档仍在所属线程执行匹配。[文件搜索](docs/FILE_SEARCH.md) · [流式搜索](docs/FILE_STREAM_PERFORMANCE.md) · [替换行为与检查](docs/FILE_REPLACEMENT_PERFORMANCE.md)
- **会话与 Windows 集成。** 启用**设置 → 启动时恢复上次会话**后，正常退出时会保存标签顺序、未保存的草稿和修改。下次启动时按原顺序恢复，草稿和修改仍标记为未保存。支持可定制工具栏、13 段状态栏、INI 设置、自动保存历史、运行命令和打印。“窗口”“插件”菜单及“插件管理”使用本地化资源。通过**设置 → 文件关联**为当前用户注册扩展名和内嵌文件类型图标，再在 Windows 的“默认应用”中选择 MultiPad4。[文件类型图标](res/icons/README.md)

系统集成对话框还可设置资源管理器右键菜单、任务栏跳转列表，以及通过注册表替换 Windows 记事本。启用或取消记事本替换时，随附的 `multipad4-registry.exe` 会请求管理员权限；如果其他程序已注册替换，须先在该程序中取消。移动或删除 MultiPad4 安装目录前，请先取消记事本替换。

### 响应性与 DPI

长行和选区统计增量执行，尚未完成的状态栏字段显示 `...`。自动词补全在时间预算内优先扫描附近文本，显式补全保留配置范围。普通文档的计数、标记和 Find All 使用可分批继续的 Scintilla 搜索，每页显示 64 条命中。取消或关闭搜索后，后台继续清理任务，并限制尚未退出的任务数量；程序退出仍可能等待未完成的 I/O。

自动保存自适应分块捕获文本，将完整、一致的快照交给后台写盘。标签与标题刷新、状态栏展示、设置写入、主题枚举、会话捕获、编辑转换和 RTF 导出也减少了重复扫描或复制。测量方法与适用条件见[性能审查](docs/PERFORMANCE_REVIEW.md)、[普通文档搜索](docs/ORDINARY_SEARCH_STORAGE.md)、[搜索取消](docs/SEARCH_CANCELLATION_PERFORMANCE.md)和[自动保存](docs/AUTOSAVE_CAPTURE_PERFORMANCE.md)。

原生控件（包括 Matepath 和 OpenDocuments 面板）使用对应窗口 DPI 的系统字体，编辑器正文保留配置的字号和缩放。真实混合 DPI 显示器拖动和输入法交互仍需人工验收。[DPI 验证](docs/DPI_FONTS.md)

### 语言与样式

编辑器从 `config/languages/default.xml` 加载 90 项语言配置，必要时使用内嵌默认配置。按 Ctrl+F12 打开**语言与样式**，可以编辑语法颜色、字体、关键词和附加扩展名，在活动文档预览修改，并将修改应用到当前文档或全部文档（包括之后新建的文档）。

随程序提供 24 套原生 XML 主题，包含浅色、深色和 22 套转换自 Notepad++ 的预设。主题导入使用 MultiPad4 的 XML 格式。[语言配置](docs/LANGUAGE_CONFIGURATION.md) · [主题列表](config/themes/README.md)

### 按需大文件编辑

默认 **≥300 MiB** 的文件进入按需可编辑文档：只读滑窗映射、持久化片段树、追加编辑日志和惰性行索引保存全文状态，Scintilla 仅显示当前位置附近约 128 KiB 的编辑窗口。打开和普通键入无需先复制全文或建立完整行表。

支持文件头/中部/尾部编辑、全局撤销/重做、原路径保存及保存副本；全文查找、正则、替换、计数、Find All、标记及行起点跳转直接作用于文档模型。单文档耗时操作可在后台执行并取消，普通文本扫描最多使用 8 个线程。

现有热缓存测试中，5 GiB 文件打开到可编辑首屏的五轮中位数约 **9.7 ms**；结果针对指定机器与数据，不包含进程启动，也不代表全文扫描耗时。全文保存和首次精确行索引仍有线性成本。编辑器插件、全局多选、全文件全选、打印及部分编码/高级操作需要通过 `Full editor...` 显式切换到完整 Scintilla 文档。

实现、性能条件、原始数据与兼容边界见 [按需可编辑文档](docs/MAPPED_EDITING.md) 和 [大文件并行查找](docs/PARALLEL_SEARCH_EXPERIMENT.md)。

### 随附插件

默认构建八个插件 DLL。将相应的 CMake 选项设为 `OFF` 即可跳过该插件。

| 插件 | 功能 | CMake 选项 |
| --- | --- | --- |
| Matepath | 通过**插件 → Matepath → 文件浏览器**打开 Windows Shell 文件浏览器，可浏览目录、筛选或排序文件、显示隐藏文件、手动刷新并定位活动文档。路径长度受上游 `MAX_PATH` 限制，大目录加载图标时可能暂停导航。[Matepath 插件](docs/MATEPATH_MIGRATION.md) | `MULTIPAD4_BUILD_MATEPATH` |
| OpenDocuments | 通过**插件 → OpenDocuments → 打开的文档**打开可停靠的文档列表，查看标签顺序、活动文档和未保存标记。双击列表项或按 Enter 切换文档。[OpenDocuments 插件](docs/OPEN_DOCUMENTS_PLUGIN.md) | `MULTIPAD4_BUILD_OPEN_DOCUMENTS` |
| Project | 在 **Project → Edit Projects…** 中将文件和文件夹组织成项目，通过 **Project → Project Pane** 打开可停靠面板，切换项目或打开成员。加载文件夹时会打开其中的直属文件。[Project 插件](docs/PLUGINS.md#project-插件) | `MULTIPAD4_BUILD_PROJECT` |
| LSP Client | 配置并启动语言服务器，提供补全、参数提示、悬停、定义跳转和诊断。[配置与移植范围](docs/LSP_CLIENT.md) | `MULTIPAD4_BUILD_LSP_CLIENT` |
| Base64 | 对选区进行 Base64 编解码，支持 URL 安全编码、十六进制输出和生成 HTML 内嵌图片。 | `MULTIPAD4_BUILD_BASE64` |
| Action on Selection | 打开选中的路径或链接、计算表达式、在线搜索和执行自定义操作。 | `MULTIPAD4_BUILD_SELECTION_ACTIONS` |
| Text Transliteration | 支持全角与半角、简体与繁体中文、日文假名及其他文字的转换。 | `MULTIPAD4_BUILD_TEXT_TRANSLITERATION` |
| Web Tools | 格式化或压缩代码、计算 JavaScript 表达式、转换 HTML 实体和 URL 转义。 | `MULTIPAD4_BUILD_WEB_TOOLS` |

Matepath 和 OpenDocuments 分别通过 `plugins/Config/Matepath.ini` 和 `plugins/Config/OpenDocuments.ini` 恢复面板显隐、停靠位置和尺寸以及浮动窗口坐标。

### Notepad++ 插件与插件管理

从可执行文件目录的 `plugins/<名称>/<名称>.dll` 加载 Unicode x64 插件，提供插件菜单、快捷键、通知、活动文档消息路由及停靠面板。已有使用 JsonTools、XMLTools、mimeTools、NppConverter、NppExport 真实 DLL 的集成检查。

**插件 → 插件管理...**支持目录搜索及批量安装、更新、移除、禁用和启用。编辑器关窗并卸载插件 DLL 后，MultiPad4 使用 WinHTTP 按 Windows 代理设置通过 HTTPS 下载软件包，校验 SHA-256，并在重启后恢复会话。插件管理需要可写的安装目录。日志会记录失败原因及保留的备份路径。

兼容层仅实现部分 Notepad++ API，**目录中列出插件不代表该插件已兼容**。目录使用固定版本快照，不会自动联网刷新；外部 lexer、双视图和完整暗色主题 API 等尚未提供。加载诊断位于 `plugins/Config/MultiPad4-plugin-load.log`。兼容层说明见 [PLUGINS.md](docs/PLUGINS.md)，插件管理以更新的 [PLUGIN_ADMIN.md](docs/PLUGIN_ADMIN.md) 为准。

## 框架与源码

- notepad4 应用源码与资源基线：`3rdparty/notepad4`。
- notepad4 优化版 Scintilla：`3rdparty/notepad4-scintilla/scintilla`。
- Matepath Shell 目录列表：`3rdparty/matepath`。
- Notepad++ 查找/插件管理参考资源与插件 API：`3rdparty/notepad-plus-plus`、`3rdparty/notepad-plus-plus-plugin-api`。

主窗口和文档窗口使用 Windows 原生 MDI API，文档标签使用 Windows 通用控件。停靠和浮动面板使用 Win32 窗口布局，并沿用插件原有的布局设置。

`cmake/Matepath.cmake` 在构建目录中生成适配宿主的 Matepath 源码。notepad4 基线为 `3fcfb70fd0d3c8802100c85f7293ccb326aa8b64`。来源、版本和许可见 [THIRD_PARTY.md](THIRD_PARTY.md)。

## 许可证

MultiPad4 原创代码采用 GNU GPL 第 3 版或任何后续版本（GPL-3.0-or-later）。第三方代码、数据和资源沿用原许可条款。[GPL 原文](LICENSE)

第三方许可清单记录了随附主题、插件目录和工具栏资源的再分发条款及待确认事项。[第三方许可核查](THIRD_PARTY.md#redistribution-status)

## 构建

需要 Windows 10/11、VS 2022 的 C++ 桌面开发组件（含 Windows SDK）、CMake 3.25+、Ninja 和 C++20。无需 Qt 或 MFC。

在 x64 VS Developer Command Prompt 中切换到仓库根目录，执行以下命令。

```powershell
cmake --preset msvc-x64
cmake --build --preset msvc-x64
ctest --preset msvc-x64
.\cmake-msvc-build\multipad4.exe + .\src\MainFrame.cpp .\src\FileIO.cpp
```

`msvc-release` 预设为 Release 构建启用链接时优化。本地开发包装脚本及用法见 [AGENTS.md](AGENTS.md)。

Windows Build 工作流在 push、PR 或手动触发时运行。它在 Windows Server 2022 上用 8 套工具链（MSVC、Clang-CL、Intel oneAPI、MINGW64 GCC/Clang、UCRT64 GCC/Clang 和 CLANG64）和 3 档 SIMD（SSE4.2、AVX2 和 AVX512）构建 24 种 x64 Release 产物。每份产物包含 `multipad4.exe`、`multipad4-registry.exe` 辅助程序、`config/`、八个随附插件 DLL 和许可声明，并按需附带工具链运行库 DLL。推送 Git 标签后，工作流发布 `multipad4-<toolchain>-x64-<SIMD>.zip`，并根据提交信息生成发布说明。[Windows Build 工作流](.github/workflows/msvc-build.yml)

### MinGW-w64

MinGW-w64 x64 构建需要 GCC、binutils（windres）、CMake 和 Ninja。现有本地包装器会按构建目录名称选择 MinGW64：

```powershell
New-Item -ItemType Directory -Force cmake-mingw64-build
bash -lc "cmake-reconfigure cmake-mingw64-build"
bash -lc "cmake-build cmake-mingw64-build"
ctest --test-dir cmake-mingw64-build --output-on-failure
```

其它机器可在 MSYS2 MinGW64 终端使用 `mingw64-x64` configure/build/test presets。
运行程序时，需要将所用工具链的 `bin` 加入 PATH，或将相应运行库 DLL 放到程序旁边。

### SIMD 编译选项

`CMakeLists.txt` 提供三选一的 `MULTIPAD4_SIMD` 缓存选项，默认 `SSE4.2`。应用、文件层、原生 lexer 和 Scintilla 使用同一指令集设置。CMake 配置时会检查选项值和编译器支持情况。

| CMake 开关 | MSVC / oneAPI 编译参数 |
|---|---|
| `-DMULTIPAD4_SIMD=SSE4.2` | `/arch:SSE4.2` |
| `-DMULTIPAD4_SIMD=AVX2` | `/arch:AVX2` |
| `-DMULTIPAD4_SIMD=AVX512` | `/arch:AVX512` |

例如，在 VS Developer Command Prompt 中使用 `cmake --preset msvc-x64 -DMULTIPAD4_SIMD=AVX2` 配置，再使用对应 build preset 编译。三档均要求 x64 工具链；可执行文件要求 CPU 和操作系统支持所选指令集，不进行运行时自动降级。

应用侧 SIMD 已用于 UTF-8/UTF-16/ASCII 检测、换行统计、缩进检测和部分图形操作，边界回归与适用范围见 [SIMD_MIGRATION.md](docs/SIMD_MIGRATION.md)。

## 验证

集成测试和 QA/性能测试命令改由 `multipad4-tests.exe` 执行（`BUILD_TESTING=ON` 时构建），正式编辑器不包含这些测试入口。

启用 `BUILD_TESTING=ON` 时，CMake 当前无条件注册 46 项测试，最多再注册 5 项：3 项需要 Python，1 项需要启用 Matepath 和 OpenDocuments，1 项需要配置时已有五个第三方插件 DLL。这是测试覆盖清单，不表示当前检出版本已通过全部测试。CI 使用 `BUILD_TESTING=OFF`，检查构建产物和运行时配置；应用及插件测试可在本地通过 CTest 执行。

| CTest 名称 | 主要范围 |
|---|---|
| `scintilla_line_index` | 三种换行、标量/SIMD 边界、两种行索引宽度下的行数和行起始位置 |
| `fileio_roundtrip` | 编码、中文/emoji、换行、二进制文件无损回退、严格 UTF-8 校验及保存失败保护 |
| `editor_integration` | Win32/Scintilla 窗口、MDI 隔离、标签命令、查找替换、样式、设置、自动保存、未保存标签恢复及随附插件 |
| `simd_boundaries` | 保护页、随机输入、非法/截断编码、跨块换行及图形尾部边界 |
| `lazy_document` | 片段树、随机编辑、快照、事务、撤销/重做、索引复用及保存失败保护 |
| `large_file_integration` | 按需视图编辑/保存、全文查找替换、取消、编码及完整编辑器转换 |
| `file_search` | 串行/并行一致性、目录过滤、编码、正则、打开文档交接与取消 |
| `reverse_search`、`file_stream`、`regex_iterator`、`hit_archive` | 反向匹配、符合条件的字面量流式搜索、正则迭代和有界命中归档 |
| `file_replace`、`file_replacement_integration` | 后台文件替换、编码一致性、提交冲突、取消、打开文档撤销及结果导航 |
| `search_retirement`、`search_retirement_ui`、`ordinary_search_storage` | 取消任务生命周期、拒绝过期结果及可分批继续的计数/标记/Find All 存储 |
| `append_block`、`piece_read`、`directory_watch` | 紧凑编辑存储、片段直接读取和目录通知处理 |
| `single_instance_integration` | 进程启动和单实例/窗口复用；已有 MultiPad4 窗口时跳过 |
| `plugin_maintenance` | 隔离临时目录中的启用、禁用和移除，批处理回滚、下载失败及拒绝无效元数据 |
| `thread_pool` | 任务生命周期、异常传播、嵌套任务及并发上限 |
| `lexer_configuration_baseline`、`lexer_behavior_baseline` | 对照固定的上游基线检查语言数据和运行时词法行为 |
| `configuration_integration` | XML 语言配置、原生主题和语言样式配置器 |
| `editor_without_plugins` | 禁用插件加载时的编辑器与会话行为 |
| `tab_strip`、`tab_strip_layout`、`frame_refresh`、`dpi_fonts` | 标签关闭按钮与布局、主窗口刷新状态及 DPI 变化时的原生控件字体 |
| `status_statistics`、`status_presentation`、`indent_read`、`modeline_preferences` | 增量状态统计、展示缓存、缩进读取和文件变量首选项 |
| `edit_transforms`、`comment_commands`、`save_whitespace`、`rich_text` | 编辑/撤销一致性、注释、保存时空白处理及剪贴板导出 |
| `autosave_capture`、`session_capture`、`settings_profile`、`theme_enumeration` | 一致的备份、重启状态、INI 持久化及主题发现 |
| `printing_separator`、`deferred_queue`、`plugin_index`、`app_update` | 打印布局、延后任务、插件路由索引和应用更新校验 |
| `configuration_data_equivalence`、`lexer_inventory_coverage`、`lexer_inventory_hashes`（需 Python） | XML 数据、lexer 清单覆盖及按检出换行符规范化的哈希 |
| `bundled_pane_restart`（启用 Matepath 和 OpenDocuments） | 跨进程重启恢复停靠、浮动及隐藏的面板布局 |
| `notepadpp_plugins`（配置时具备五个 DLL） | 五个真实插件的命令、面板、导出及文档路由 |

资源清单、命令路由与窗口渲染可分别生成：

```powershell
python tools/inventory_notepad4.py
.\cmake-msvc-build\multipad4-tests.exe --migration-report tmp/migration-status-check.json
python tools/check_migration.py tmp/migration-status-check.json
.\cmake-msvc-build\multipad4-tests.exe --qa-render tmp/qa-render
```

资源数量和命令路由只证明入口覆盖；窗口渲染与自动回归也不替代真实交互验收。全部语言/对话框/动态菜单的行为一致性、鼠标拖动、输入法、多显示器 DPI、实际打印及真实系统集成仍需逐项验证。
