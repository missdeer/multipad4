# MultiPad4 运行时性能审查

以下为修改前的审查基线；12 项优化的实现和验证见 [性能优化实施记录](PERFORMANCE_CHANGES.md)。

日期：2026-11-14。性质：静态性能审查；未修改产品代码，未运行性能基准或功能测试。以下复杂度和调用关系来自当前源码，不代表已测得的耗时或提速比例。

扫描范围：自有 `src/`、`plugins/`、构建配置、测试与基准入口；针对调用语义追查了 vendored Scintilla 的字符统计实现。第三方完整源码未逐行审查，生成目录不作为优化对象。审查按用户指定覆盖现有代码，不限于 Git diff。

## must-fix：优先解决交互延迟和内存峰值

### 1. 状态栏在每次刷新时同步重复扫描长行和选区

- 位置：`src/UpstreamUI.cpp:181`、`:194`、`:195`、`:200`、`:215`；底层 `3rdparty/notepad4-scintilla/scintilla/src/Document.cxx:1698`、`:1727`。
- `UpdateStatus()` 对当前位置及行尾调用 `SCI_GETCOLUMN`、`SCI_COUNTCHARACTERS`，构造本地化字段时再次计算相同值。底层两个查询分别从行首或范围起点逐字符遍历，并非常数时间。选区字符数也逐次重算。
- 触发：长行 JSON/日志、接近文件尾部的长行光标、大选区、多选区；滚动引发的刷新同样执行统计。FastView 的状态字段是在这些查询执行完以后才被覆盖，仍会支付窗口片段的统计成本；隐藏状态栏也没有以 `statusVisible` 提前返回。
- 改进：一次刷新复用统计结果；按 revision、行号、选区及 tab width 缓存；纯滚动无需重算字符数。大范围统计使用可取消的分片任务，超过交互预算时延后显示精确值。先判断 FastView 和状态栏可见性。
- 验证：1/10/100 MiB 单行、100 MiB 全选、多光标；测按键/滚动到下一帧的 p50/p95/p99，不能只测打开时间。

### 2. 每次 SCN_UPDATEUI 都进入全局刷新，且标签查找为平方级

- 位置：`src/EditorDocument.h:99`、`:118`；`src/MainFrame.cpp:230`、`:392`；`src/UpstreamUI.cpp:50`、`:173`；`plugins/OpenDocuments/DocumentsPane.cpp:66`。
- `SCN_UPDATEUI` 无论更新原因都投递 `WM_DOCUMENT_CHANGED`，`OnChanged()` 无条件 `Refresh()`。刷新遍历所有标签；每个 `GetPageData()` 又通过 `Child()` 线性查找 children，所以遍历成本为 O(D²)。刷新还遍历整个菜单、更新全部工具栏按钮并广播 `MPN_DOCUMENTSCHANGED`。
- OpenDocuments 收到广播后再次枚举所有文档；按 buffer id 查询标题也线性扫描宿主文档。即便最终标签没有变化，查询和分配已经发生。
- 改进：区分内容、选择、滚动、激活和标题变化；合并待处理刷新消息；按 HWND/buffer id 建索引，保留框架的标签顺序；仅在文档列表或标题实际变化时通知插件。菜单状态在弹出菜单时刷新，工具栏只提交发生变化的状态。
- 验证：1/50/200/500 标签下持续输入与滚动，记录每秒 Refresh 次数、插件通知次数、UI 线程 CPU 和帧延迟。

### 3. 自动补全在输入通知内同步扫描文档，默认预算 500 ms

- 位置：`src/StyleManager.cpp:454`、`:531`、`:998`；`src/CompletionDocument.cpp:22`、`:74`。
- 输入字符触发 `Complete()`，文档候选收集从文件起点搜索，默认 `IDC_AUTOC_SCAN_WORDS_TIMEOUT` 为 500 ms。没有候选且带分隔符时还可能换前缀重试。扫描超时只在阶段之间检查；这不是对整个通知处理时间的硬上限。补全列表已激活时有提前返回，因此不是每个字符都会重扫。
- 触发：低于大文件保护阈值、启用了文档词补全的中大型文本，尤其候选稀少或弹出列表反复关闭时。
- 改进：自动触发优先使用附近文本及已缓存候选，UI 同步预算降到数毫秒；文档词索引按编辑行增量更新。后台扫描只能读取不可变快照，结果按 revision/前缀校验，不能在工作线程直接访问 Scintilla HWND。静态 lexer 关键词也可预先解析，避免反复流解析和 set 分配。
- 验证：10/100/250 MiB 文档，匹配密集、稀疏、无匹配三组，测连续输入延迟及候选正确性。

### 4. 目录搜索按文件数限流，仍可同时持有多份超大全文

- 位置：`src/FileSearch.cpp:48`、`:64`、`:96`、`:104`、`:124`；`src/SearchResultsPane.cpp:51`。
- 每个 worker 将整个文件解码追加到 string，并将每个命中存入 vector；结果队列仅限制为 8 个文件，最多 8 个 worker 还可各持有一个结果。结果面板保存的 SearchMatch 又通过 shared_ptr 持有整份文件，即使只显示很少的命中行。没有字节预算或普通文件结果分页。
- 触发：多个数百 MiB/数 GiB 文件或命中非常密集的目录。只读大文件打开采用映射窗口，并没有保护这条目录搜索路径。
- 改进：对在途数据设置总字节预算；复用大文件 snapshot/HitArchive 思路，命中与预览分页或落盘。普通字面搜索可分块保留边界重叠；跨块正则需要保留既有语义，不能简单对各块独立匹配。导航保留文件版本校验。
- 验证：多大文件、百万命中、慢 UI 消费者；测 Private Bytes 峰值、队列字节数、取消延迟和结果导航。

## should-fix：消除额外全文工作和批量操作退化

### 5. 局部编辑命令先复制全文，再截取很小的范围

- 位置：`src/EditAdvanced.cpp:47`、`src/EditExtra.cpp:15`、`src/EditCodeFormat.cpp:382`、`src/EditExpression.cpp:68`、`src/EditRichText.cpp:141`；已有可用的 `src/EditorDocument.cpp:145` (`Range`)。
- 例如查看一个字符信息调用 `d.Text().substr(...)`；高级编辑公用 Text helper 和若干选区命令也这样做。读取 k 字节会先分配和复制 N 字节全文，成本 O(N+k)。
- 改进：改为 `Range(start, length)`；需要前后字符上下文的命令只扩展必要边界。明确要求全文的功能继续保留全文操作。
- 验证：200 MiB 文档中处理几十字节选区，测分配量和耗时，并回归 UTF-8 边界、矩形选区与空选区语义。

### 6. 自动保存虽称为后台工作，仍在 UI 定时器内串行复制和写盘

- 位置：`src/BackgroundWork.cpp:24`、`:45`、`:107`；`src/AutoSave.cpp:16`、`:67`、`:87`。
- 定时器调用 BackupDocuments/SaveDocumentRecovery，在 UI 线程生成全文 string、编码、写正文和元数据；回退目录还会复制 content。写盘延迟会直接阻塞所有 MDI 文档。代码已有 revision 去重和 LargeFile 排除，但阈值以下多文档仍受影响。
- 改进：UI 线程受控生成版本一致的快照，把编码和 I/O 交给有容量限制的任务队列；每文档合并待保存版本。仅在对应 revision 成功写入后更新恢复记录，并保留退出/挂起强制保存语义。
- 验证：多个 50–250 MiB 修改文档、慢盘/网络路径/写失败；同时持续输入，记录最长 UI 阻塞和恢复文件一致性。

### 7. 搜索 UI 的 12 ms 预算不能限制单个结果的处理耗时

- 位置：`src/SearchDialog.cpp:321`、`:337`、`:344`、`:349`；`src/SearchResultsPane.cpp:46`。
- FileStep 处理完一整个文件结果后才检查 deadline。`CollectMatches()`、`ArchiveMatches()` 或 `SearchResultsPane::Add()` 任一次就能处理全部命中、复制预览、追加全部结果行。普通文件虽在后台搜索，结果展示仍可长时间卡 UI。
- 改进：消费游标跨 tick 保存，按命中数/结果行和时间共同分片；预览限长，必要时延迟展开。只读结果控件应关闭 undo collection，并按批次追加及着色。打开文档的全文搜索也应移出单个 UI tick。
- 验证：一个文件百万命中以及一个超长命中行，测每次 FileStep 的最大耗时和取消响应。

### 8. 按列换行反复 erase 字符串前缀，长行出现平方级搬移

- 位置：`src/EditAdvanced.cpp:1543`、`:1559`、`:1561`。
- 每输出一段便 `wide.erase(0, split)`，然后逐个 erase 前导空格。固定列宽 w 时，长度 N 的行需要搬移约 N+(N-w)+(N-2w)+… 个 UTF-16 单元，即 O(N²/w)；大量空格也会放大成本。
- 改进：使用 offset/string_view 遍历原字符串，一次跳过整段空格，输出缓冲按合理大小预留；保留代理项边界。
- 验证：1/10/100 MiB 单行与长空格串按 80 列换行，观察耗时随输入长度的增长曲线。

### 9. 排序比较器反复转换和构造键，普通排序也计算去重表

- 位置：`src/EditAdvanced.cpp:1454`、`:1495`、`:1514`。
- 每次比较都构造两份 UTF-16 key，忽略大小写时还调用 LCMapStringEx；按列或扩展名分组时有额外切片和 fullKey。排序后无条件构造 count map、再次遍历转换，即使 remove/unique/merge 全部未启用。
- 改进：每行一次性缓存完整键、列键和分组键，稳定排序索引/记录；只在对应选项启用时建立去重计数。字符串比较本身的成本仍存在，但可消除 O(L log L) 次重复转换与分配。
- 验证：10万/100万行，分别普通、忽略大小写、逻辑排序、列排序和去重；核对稳定顺序及尾行 EOL。

### 10. 重新加载会重复读取和解码同一文件

- 位置：`src/FileIO.cpp:475`、`:478`、`:484`；调用端 `src/FileCommands.cpp:331`。
- ReadForReload 先 ReadBytes，再调用 Read(path, ...)，后者再次 ReadBytes；UTF-8 分支还可能重新 Validate/Decode 已读取的数据。失败回退可能再读一次。未跨越 LargeFile 阈值的监控重载也会支付这些成本。
- 改进：编码决策及兼容回退均复用同一份 bytes，以 DetectBytes/Decode 完成；保留 BOM 优先级、当前编码和 fallback 的现有规则。额外好处是避免多次读文件时看到不同磁盘版本。
- 验证：ASCII、UTF-8、UTF-16、传统代码页及无效输入，监测读字节数和峰值内存。

### 11. occurrence 后台扫描的时间预算按文档累加

- 位置：`src/BackgroundWork.cpp:71`、`:78`；`src/StyleManager.cpp:1099`、`:1107`。
- 定时器遍历所有文档，每个 ProcessStyleBackground 最多约 8 ms；D 个文档同时 pending 时，单次定时器可消耗接近 D×8 ms，且 UI 通知还会额外调用扫描。文件监视和自动保存开销不在这个预算内。
- 改进：使用主窗口级总预算与轮转队列，优先活动文档；后台标签延后扫描。不要简单移到工作线程调用窗口 API；如需并行，使用版本化快照并批量回写标记。
- 验证：50/200 个同时 pending 文档，测 timer 最大耗时及活动标签高亮完成时间。

### 12. 搜索结果分页追加后会重新着色全部历史结果

- 位置：`src/SearchResultsPane.cpp:70`、`:84`、`:93`；`src/SearchResultLexer.cpp:44`。
- 每次 LoadMore 后 Colourise 从第 0 行遍历整个 lines；每次 Finish 也重做全部历史搜索。固定页大小下，连续加载 P 页会重复处理 1+2+…+P 页的行。
- 改进：Colourise 接受 dirty 起止行，只处理追加页及必要的折叠边界；Finish 只更新被改变的搜索头和受影响位置。将老结果历史纳入内存预算。
- 验证：连续加载 100 页及多次搜索历史，比较第 1 页与第 100 页的增量展示耗时。

## nit：测量后再决定的优化

- `src/EditorDocument.h:48` 所有编辑器查询都走 SendMessageW。缓存 Scintilla direct function/pointer 可减少同线程高频消息分发成本，但必须保持 UI 线程约束、document/window 生命周期及当前 GETCOLUMN 默认参数适配。先消除上述扫描与复制，再用微基准确认收益。
- 构建默认已有 `/O2`，不能把当前构建当作 Debug 性能。可以用独立发布配置评估 LTO/PGO 和更积极内联；应同时测启动、热编辑和二进制体积，不预设提升幅度。固定 ISA 的 AVX2/AVX512 不是所有机器上的通用收益。

## 验证安排与约束

建议第一批处理 1、2、5：影响高频路径或不必要全文复制，且修改边界相对清晰。随后处理补全、目录搜索内存预算和结果分片，再处理自动保存与批量算法。

统一记录 UI 线程 CPU、Private Bytes 峰值、p95/p99 输入延迟、最长阻塞、处理字节数与结果正确性。覆盖 1/50/200/500 标签、UTF-8/UTF-16/传统编码、长行/短行、稀疏/密集匹配、冷缓存/热缓存和本地/慢存储。现有 FileReadBenchmark/ParallelSearchBenchmark 可复用测量结构，但其吞吐量结果不能代替交互延迟测试。

按 audit 的五类检查口径：以上主要属于性能实现与边界规模问题；修改时需同时保持正确性（编码、选区、撤销、版本归属）和兼容性（插件 API、框架及上游语义）。本次没有据此提出安全漏洞结论，也不将未测量的 SIMD、线程池或渲染猜测列为确定缺陷。
