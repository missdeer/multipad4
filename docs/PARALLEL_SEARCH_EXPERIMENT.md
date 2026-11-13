# 大文件查找并行实验

结论：普通文本查找已正式接入并行扫描。下面先记录正式入口的验证，再保留先前的读取器对照实验。任意正则和 DBCS 仍沿用串行匹配。

## 正式实现和入口实测

大范围（≥64 MiB）普通文本查找、计数、Find All、标记及无上下文依赖的普通文本全部替换发现阶段，自动使用最多 8 个工作线程；小范围、正则、DBCS 和超过 1 MiB 的原生查找串沿用串行路径。UTF-8/UTF-16 和单字节代码页可走并行路径。并行只作用于发现阶段，修改仍在单个私有分支按原顺序执行，成功后一次提交，保留撤销及取消保护。

每个线程的 `Document::ReadSession` 遍历**同一个不可变片段树快照**，并按文本来源缓存独立的文件读取窗口。底层复制已有文件/映射句柄，绝不重新打开路径，因此保存替换原路径后仍可查询旧快照；追加日志片段也保留。映射失败时的读取回退共享文件指针锁，避免复制句柄后同时寻址造成数据错乱。

扫描按有序的 64 MiB 块成批调度，每批最多约 8 块。块起点和末端按字符边界对齐，保留与查找串长度相适应的重叠，按匹配起点决定所属块；结果按全局位置合并。“查找下一个/上一个”先检查附近约 256 KiB，避免近距离命中启动全文扫描。计数只合并数量，结果列表/标记使用临时偏移档案；遇到前块末尾匹配影响后块的非重叠匹配起点时，从已接受的末端重扫该块，保证重复文本的全局贪婪非重叠语义。工作线程使用原有取消标志，完成时仍检查快照版本。

正式入口使用同一份 5 GiB 文件及唯一末尾标记。每种模式先预热一次，再测三轮；交替单线程/自动并行顺序。单线程使用相同正式算法但设置 `workers=1`；计时包括实际 `StartSearch` / `StartEnumerate` / `StartReplaceEvery`、工作线程、消息处理、结果应用，未包含进程启动及打开文件。不是以下独立模型的原型扫描时间。

| 操作，三轮中位数 | 单线程 | 正式自动并行 | 倍率 |
|---|---:|---:|---:|
| 区分大小写查找到文件尾 | 5658.169 ms | **1578.073 ms** | 3.59× |
| 忽略大小写全文计数 | 6457.034 ms | **1849.832 ms** | 3.49× |
| 普通文本全文替换唯一命中 | 5688.345 ms | **1642.928 ms** | 3.46× |

附近文件头命中另测三轮，实际入口中位数 **9.575 ms**，每轮读取请求 **491604 字节**（含查找和结果视图刷新），没有扫描 5 GiB。该时间包含消息泵的等待粒度，不能等同于匹配计算本身的耗时。

并行完整扫描每轮读取请求约 5.375 GB，比单线程约多 0.11%，来自边界重叠、对齐和结果刷新；没有每个线程各读一遍全文。每次替换仅新增 8 个原生字节，逐轮验证精确位置（5368709121）、计数、替换输出与一次撤销恢复原快照；额外检查捕获组和取消。进程退出码 0。计时点最大私有提交约 8.1 MiB；由于最多 8 个 64 MiB 映射窗口，进程峰值工作集约 **558.3 MiB**，高于旧串行测试，不能继续套用单窗口约 100 MiB 的峰值数据。

[正式入口逐轮 CSV](performance/parallel-search-production-2026-11-13/5GiB.csv)、[正式入口汇总 JSON](performance/parallel-search-production-2026-11-13/summary.json)。这组数据仅针对该缓存状态下的 ASCII 字面量扫描，不能推广到任意编码/正则/磁盘冷读。

新增回归覆盖 UTF-8/UTF-16 的真实 64 MiB 分块、字符跨块、大小写、前后查找、零命中、跨块重复文本非重叠计数、分页偏移、未保存片段、保存替换路径后的旧快照读取、正式编辑窗口中的匹配/替换/撤销和扫描途中取消。原正则与 DBCS 回归继续运行。

正式入口同条件复现（其他配置与后文相同）：

```powershell
$env:MULTIPAD4_LARGE_TEST_SOURCE = (Resolve-Path tmp/random-text-5GiB.txt).Path
$env:MULTIPAD4_LARGE_TEST_SEARCH = '1'
$env:MULTIPAD4_LARGE_TEST_SEARCH_COMPARE = '1'
$run = Start-Process .\cmake-msvc-build\multipad4.exe --large-file-test -Wait -PassThru -RedirectStandardOutput tmp/parallel-production-5GiB.csv -RedirectStandardError tmp/parallel-production-5GiB.err
$run.ExitCode
Remove-Item Env:MULTIPAD4_LARGE_TEST_SOURCE
Remove-Item Env:MULTIPAD4_LARGE_TEST_SEARCH
Remove-Item Env:MULTIPAD4_LARGE_TEST_SEARCH_COMPARE
```

## 前期读取器对照实测

i9-12900HK、约 32 GiB、Windows 11；MSVC RelWithDebInfo、SSE4.2。同一份 5368709120 字节 ASCII 随机正文，在片段树末尾追加唯一标记，原文件不改写。每组配置预热一次，随后测三轮，正反交替配置顺序，不清系统缓存。以下是**本轮同条件对照**的墙钟中位数，包含线程创建、全文扫描与汇总，不包含进程启动、打开文件或 UI 结果绘制。

| 读取方式 | 线程 | 区分大小写 | 忽略大小写 |
|---|---:|---:|---:|
| 当前共享读取器 | 1 | 6014.750 ms | 6976.071 ms |
| 当前共享读取器 | 2 | 4524.212 ms | 4467.451 ms |
| 当前共享读取器 | 4 | 4560.164 ms | 4377.785 ms |
| 当前共享读取器 | 8 | 4371.868 ms | 4454.615 ms |
| 独立映射读取器 | 2 | 3802.541 ms | 4214.896 ms |
| 独立映射读取器 | 4 | 2534.958 ms | 2719.615 ms |
| 独立映射读取器 | 8 | **1625.421 ms** | **1935.709 ms** |

8 线程独立读取器相对本轮单线程约快 **3.70 倍 / 3.60 倍**；共享读取器 8 线程仅约快 1.38 倍 / 1.57 倍。区分大小写的进程 CPU 时间中位数从 5890.625 ms 增至 12718.750 ms，说明墙钟加速需要更多总 CPU 时间，不能把 8 个线程理解为固定 8 倍收益。

每项读取请求均约 5 GiB，而非每线程重复读取全文。每轮验证总命中数为 1、首命中原生位置为 5368709121，覆盖超过 32 位的偏移；完整实验进程退出码为 0。另运行默认 `large_file_integration` 回归通过。

[逐轮原始 CSV](performance/parallel-search-2026-11-13/5GiB.csv)、[中位数/最小/最大/CPU 时间 JSON](performance/parallel-search-2026-11-13/summary.json)。

## 为什么读取器会影响收益

`Source::Read()` 对同一来源持有 `readMutex` 才调用 `MappedFile::Read()`。底层 `MappedInput` 仅保存一个可变映射窗口，在读取位置离开窗口时执行取消映射和重新映射。多个线程同时扫描相距很远的范围，会竞争锁并切换窗口。

实验中的独立读取器使用各自的文件映射、窗口和搜索缓存。为了不先改变正式模型接口，实验建立了多个内容相同的只读来源和相同的追加标记。这只验证了隔离读取状态的收益；正式实现必须让每个线程拥有独立读取会话，同时仍共享**同一个不可变文档快照**，保留全部编辑片段及原始文件版本，不能简单重新打开路径代替当前文档。

这组对照同时隔离了锁和映射窗口状态，尚未进一步拆出锁等待与映射切换分别占多少时间；不能仅凭上述表格把所有差异归因于其中一项。

## 边界与后续范围

- 字面量可分区扫描，边界保留重叠，按命中起点归属分区；Unicode/DBCS 还需正确定位字符边界。计数/Find All 必须合并跨区结果，保持原有全局非重叠匹配规则，不能对所有表达式直接相加局部计数。
- “查找下一个”应按位置顺序调度有限数量的块，找到命中后取消后续块，并确认更早的块没有命中。本次唯一末尾命中需要扫描全文，不能据此承诺文件头很快命中时也能获得相同加速。
- 任意跨行正则可能跨越多个块，具有无界匹配、回溯或前后文依赖；固定重叠长度无法普遍保证正确。目前已接入可证明边界正确的普通文本路径，正则另行验证算法。
- 正式任务仍需保留快照版本检查、取消、结果顺序与编辑隔离。并行替换的发现阶段可复用搜索，但上下文相关替换不能直接在多个线程修改同一棵树。

## 复现

```powershell
bash -lc "cmake-build cmake-msvc-build"
$env:MULTIPAD4_LARGE_TEST_SOURCE = (Resolve-Path tmp/random-text-5GiB.txt).Path
$env:MULTIPAD4_LARGE_TEST_PARALLEL_SEARCH = '1'
$run = Start-Process .\cmake-msvc-build\multipad4.exe --large-file-test -Wait -PassThru -RedirectStandardOutput tmp/parallel-search-5GiB.csv -RedirectStandardError tmp/parallel-search-5GiB.err
$run.ExitCode
Remove-Item Env:MULTIPAD4_LARGE_TEST_SOURCE
Remove-Item Env:MULTIPAD4_LARGE_TEST_PARALLEL_SEARCH
```

历史原型代码：[ParallelSearchBenchmark.h](../tests/ParallelSearchBenchmark.h)。该独立模型入口仅用于评估读取器，正式实现及端到端对照使用上文 `MULTIPAD4_LARGE_TEST_SEARCH_COMPARE` 入口。
