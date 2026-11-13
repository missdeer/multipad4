# Scintilla 全文插入：测量与未采用的行表预留实验

> 历史实验归档：相关未采用原型已移除，保留原始测量记录。当前产品实现见 [按需可编辑文档](MAPPED_EDITING.md)。

**状态：扩大行表默认预留的修改已撤回，正常加载仍使用原 16777216 行上限。** 下表是实验结果，不代表当前默认行为。42074113 行来自首块外推，并非硬编码，但预留仍有 67108864 行上限，更大的行数仍会扩容；该策略只减少特定规模下的常数开销，没有解决扩展性问题。实验入口和诊断工具保留供复现。

日期：2026-11-13。本次优化接续[全文读取优化](LARGE_FILE_READ_PERFORMANCE.md)，读取后端及 UTF-8 并行校验策略保持一致，只比较 Scintilla 装载方式和行表预留策略。

## 实验结果（未采用）

同一 5 GiB ASCII / UTF-8 文件，正常 SSE4.2 构建，**关闭内部诊断插桩**。同进程、每个方案一轮预热，然后交错各五轮，奇数轮交换先后。每次都使用新的真实 MDI 文档，上一份已释放。

| 指标 | 原固定 1600 万行上限 | 新的内存预算策略 | 中位数耗时减少 |
|---|---:|---:|---:|
| Scintilla 插入区间中位数 | 1713.431 ms | 1477.360 ms | 13.78% |
| 完整可编辑加载中位数 | 4058.744 ms | 3818.705 ms | 5.91% |
| 完整加载最小–最大 | 4031.943–4118.519 ms | 3781.121–3887.723 ms | — |

完整计时包含创建文档、读取/校验、换行统计、行表构建、设置可编辑、刷新和同步首屏绘制，不包含进程启动、验证和关闭。各阶段中位数不必来自同一轮，不能直接相加得到总时间中位数。

每轮均验证完整长度 **5368709120 字节**、**41943041 行**、首/中/尾字节、EOF、编辑及撤销后保存点状态。没有用 Fast View 的局部显示代替完整载入。

输入 SHA-256：`5ac1ca64feb2c139decf370968c91cfd6122b25256645e8e3e2cb7821f941e27`。机器为 Intel Core i9-12900HK、32 GiB 内存、Windows 11，MSVC RelWithDebInfo `/O2 /Ob1`，与读取报告一致。先预热文件缓存，没有执行冷盘或跨机器测试；不同批次的绝对时间会波动，应比较本批交错对照，不能用此前 3.58 秒和本次 3.82 秒推断退化。

原始 [逐轮输出](performance/scintilla-insert-2026-11-13/final-uninstrumented.txt)、[CSV](performance/scintilla-insert-2026-11-13/final-uninstrumented.csv)、[全部实验汇总](performance/scintilla-insert-2026-11-13/summary.json)。插桩关闭时原始输出里的内部计数为 0，表示没有采集，不表示成本为零。

## 插入时间具体花在哪里

保持 `3rdparty/` 源文件不变，通过 `MULTIPAD4_PROFILE_SCINTILLA` 在构建目录生成 `CellBuffer.cxx` 诊断副本，使用高精度 steady_clock 分别计时：

- `substance.InsertFromArray`：文本写入，包括所需的首次写页和可能的分配，不等于纯 memcpy 指令时间。
- `LineVector::InsertLines`：批量行偏移插入，包括内部数组扩容。
- `CellBuffer::Allocate` 及 `AllocateLines`：显式预留；不包含之后首次触页和隐式扩容成本。

一轮原路径内部细分：

| 区间 | 耗时 |
|---|---:|
| 插入总区间 | 1922.925 ms |
| 文本写入 | 1170.316 ms |
| 批量行索引写入 | 316.551 ms |
| 差值：换行定位、文档/窗口维护、其他开销 | 约 436.058 ms |
| 显式文本预留（插入区间外） | 0.033 ms |
| 显式行表预留（应用换行区间内） | 0.022 ms |

文本复制与行索引计时不重叠；“差值”是剩余区间，未进一步拆分，不能声称已经测出其中每项的精确成本。行索引计数只统计批量 `InsertLines`，每块尾部的单行插入不在该计数中。原始数据见 [initial-profile.txt](performance/scintilla-insert-2026-11-13/initial-profile.txt)。

最主要的残余成本是将完整文本写入 Scintilla 文本存储，包含内存页首次写入。文件层读取、应用层换行统计在插入区间之外，不能混算。预留本身没有触碰整个数组，因为 fork 使用默认初始化分配器；不能把“预留快”理解为后续写入不会缺页。

## 单独验证优化来源

原应用只按首块估算行数，并将预分配限制在 **16777216 行**；本文件实际有 **41943041 行**。加载超过预留容量后，行偏移数组会增长并复制已有偏移。

插桩构建中的分离实验，每项一轮预热、交错各五轮：

| 对照 | 原方案 | 新方案 |
|---|---:|---:|
| 仅提高行表上限，窗口追加方式相同：行索引区间中位数 | 293.563 ms | 63.845 ms |
| 同上：文本写入区间中位数 | 1026.381 ms | 1019.254 ms |
| 同上：插入区间中位数 | 1690.531 ms | 1445.875 ms |
| 同上：完整加载中位数 | 4091.730 ms | 3853.577 ms |

文本写入基本相同，改善集中在行索引区间，支持“避免行表加载中扩容”的归因。提高预留只消除了行表的反复增长，仍需写入最终的完整行偏移数组。

另一个实验保持更充分的行表预留相同，仅比较隐藏窗口追加和无窗口 `ILoader`。完整加载中位数为 **3798.839 ms → 3807.670 ms**，没有测出整体收益。因此最终默认路径仍使用原有暂存窗口，没有引入 `ILoader` 的额外生命周期及接口转换复杂度。

原始数据：[reserve-profile](performance/scintilla-insert-2026-11-13/reserve-profile.csv)、[loader-profile](performance/scintilla-insert-2026-11-13/loader-profile.csv)。这些是中间诊断实验；其日志中的 `loader64M` / `window16M` 标签沿用初始组合实验命名，具体策略以本节的分离条件为准。最终保留的基准入口只对照原 16M 上限与默认自适应策略，ILoader 实验代码未留在产品路径。

## 未采用的实验策略

`EditorDocument::LoadStream` 继续只分析第一块来估算行表容量，不增加全文预扫描。实验显式选择以下预留上限：

```text
min(首块外推的行数, 64 × 1024 × 1024 行, 可用物理内存 / 16 / sizeof(Sci_Position))
```

64 位偏移下上限约 512 MiB，实际预留不超过当前可用物理内存的 1/16。预算限制的是推测性的预留，不是限制真实文档最终可以有多少行；后续有更多行仍由 Scintilla 正常增长。

本机该文件预留 **42074113 行**，略多于实际行数；原方案为 16777216 行。偏移表只是预留地址空间和相应分配，不因本次策略而立刻初始化所有元素；真实行偏移仍在插入时写入。

`LoadStream` 的默认预留上限维持原 16M；基准显式传 0 才选择实验内存预算。对照读取路径完全相同。计量增加 `reservedLines`，可以检查实际使用的预留，而不是只看配置值。

## 复现

正常、无插桩的端到端对照：

```powershell
bash -lc "cmake-build cmake-msvc-build"
$env:MULTIPAD4_LARGE_TEST_SOURCE = (Resolve-Path tmp/random-text-5GiB.txt).Path
$env:MULTIPAD4_LARGE_TEST_E2E = '1'
$env:MULTIPAD4_LARGE_TEST_INSERT = '1'
.\cmake-msvc-build\multipad4.exe --large-file-test
Remove-Item Env:MULTIPAD4_LARGE_TEST_SOURCE
Remove-Item Env:MULTIPAD4_LARGE_TEST_E2E
Remove-Item Env:MULTIPAD4_LARGE_TEST_INSERT
```

生成并运行内部诊断构建（默认关闭，wrapper 通过环境变量传递配置）：

```powershell
$env:MULTIPAD4_PROFILE_SCINTILLA = 'ON'
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
Remove-Item Env:MULTIPAD4_PROFILE_SCINTILLA
$env:MULTIPAD4_LARGE_TEST_SOURCE = (Resolve-Path tmp/random-text-5GiB.txt).Path
$env:MULTIPAD4_LARGE_TEST_PROFILE = '1'
.\cmake-msvc-build\multipad4.exe --large-file-test
Remove-Item Env:MULTIPAD4_LARGE_TEST_SOURCE
Remove-Item Env:MULTIPAD4_LARGE_TEST_PROFILE
```

恢复正常构建：

```powershell
$env:MULTIPAD4_PROFILE_SCINTILLA = 'OFF'
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
Remove-Item Env:MULTIPAD4_PROFILE_SCINTILLA
ctest --test-dir cmake-msvc-build --output-on-failure
```

诊断转换按精确源码锚点执行，锚点缺失时配置失败，防止上游更新后静默得到全零结果。诊断数据只能用来定位成本，最终收益以上述关闭插桩的交错对照为准。

## 验证与剩余限制

- 诊断构建与正常构建均跑 6 项 CTest。
- 新增极低预留、显式上限及默认预算三种情况下的混合 CRLF/CR/LF 分块装载，验证完整文本、行数、末尾位置、修改/撤销状态和失败加载保留原文档。
- 5 GiB 交错对照保持完整长度、行数、头中尾内容、EOF、编辑和撤销的检查。
- `3rdparty/` 保持不变，无内核补丁进入正式构建；只读 Fast View 的按需首屏路径保持不变。
- 实验中的插入收益约 14%，整体约 6%，不是数量级优化，已决定不更改默认策略。内存受限或行数外推偏差较大的文件可能得到不同预留和收益；没有用本机数据保证其他机器的结果。
- 下一轮若要进一步优化文本写入，需要区分内存页提交、内存带宽和复制本身；本次未以不安全的直接写缓冲、关闭正确性检查或降低文件完整性来换取速度。

## 后续方向

完整加载的读取、写入及建立完整行索引至少需要处理每个字节和每条行记录，工作量仍是线性的。需要消除的是可避免的整表搬移和重复复制，而非用更大的估计容量遮住一次基准的扩容。后续应测量分段行索引、保留虚拟地址空间后按需提交等结构方案在不同规模（包含 8000 万行）下的增长曲线，并核对内存与编辑契约后再决定实现。

后续已将文本写入区间拆分到 `RoomFor`、`GapTo`、`memcpy` 和长度更新，并进行首次写页对照，见 [文本写入细分报告](SCINTILLA_TEXT_COPY_BREAKDOWN.md)。
