# Scintilla 文本写入区间的操作细分

> 历史实验归档：相关未采用原型已移除，保留原始测量记录。当前产品实现见 [按需可编辑文档](MAPPED_EDITING.md)。

日期：2026-11-13。针对之前报告的 `substance.InsertFromArray` 约 1.17 秒，新增函数内部计时和预触页对照，未更改正式文本存储算法、读取优化或默认行表预留。

## 直接测到的操作

源码位置：`CellBuffer::BasicInsertString` → `SplitVector<char>::InsertFromArray`。

它依次执行：

1. 检查长度和插入位置是否有效。
2. `RoomFor(insertLength)`：检查 gap 是否有足够空位，不足时扩容。
3. `GapTo(positionToInsert)`：把 gap 移到插入位置，必要时搬动已有文本。
4. `memcpy(body.data() + part1Length, arr, insertLength)`：复制本块文本。
5. 更新 `lengthBody`、`part1Length`、`gapLength` 三个字段。

使用同一个 5368709120 字节 ASCII 文件，预热一轮、交错各五轮。每次新建完整文档，按约 16 MiB 块追加，共 **320 次**，校验复制总字节、文档完整长度、末行位置、首/中/尾字节、编辑和撤销。以下采用普通路径**父区间中位数对应的同一轮**，各子区间可以加总：

| 操作 | 全文件累计时间 ms | 实际工作 |
|---|---:|---|
| 位置/长度检查 | 0.0120 | 320 次有效追加 |
| 容量检查 `RoomFor` | 0.0790 | **0 次扩容** |
| gap 检查 `GapTo` | 0.0466 | **0 次已有文本搬移，0 字节移动** |
| `memcpy` | **1067.5206** | 完整复制 5368709120 字节 |
| 三个长度字段更新 | 0.0679 | 320 次更新 |
| 外层差值（计时、缺页计数调用、分支等） | 1.9972 | 诊断额外开销及未单独计量的小操作 |
| **`InsertFromArray` 父区间** | **1069.7233** | 含上述所有内容 |

普通路径父区间五轮范围为 **1033.0822–1092.6630 ms**。此前 1170.316 ms 是更早的一次测量，不是固定常数；不能把新一轮子区间强行缩放到旧的 1.17 秒。

这些短操作的数字包含高精度计时本身的开销，只有“累计远小于 1 ms、不是瓶颈”的意义，不能解释为字段赋值具有这个精确的裸执行成本。文档文本在加载前已预留文件大小加 16 MiB，因此追加中没有扩容；每次追加都位于现有 gap，因而没有 gap 数据搬移。**当前这个约一秒区间几乎全部在 memcpy 调用中。**

## memcpy 区间还包含首次写页

普通路径中位轮，`memcpy` 区间内累计记录到 **1313290 次进程缺页**。5 GiB / 4 KiB = 1310720 页，数量接近，但计数是进程级的，不代表全部来自目标页，也未区分软/硬缺页。

为了验证目标页面的首次写入影响，加入一个**仅诊断用**的对照：每块复制前，先向目标范围的每个页面写入一个字节，再执行原来的 memcpy。没有预先扫描、读取或复制整个文件；触页写入范围始终属于即将被输入覆盖的 gap 空间。两种方案均使用原来的 16M 行表预留。

| 路径（各自父区间中位轮） | 预触页 ms | 随后的 memcpy ms | 父区间 ms | memcpy 内缺页 | 预触页内缺页 |
|---|---:|---:|---:|---:|---:|
| 普通复制 | 0 | 1067.5206 | 1069.7233 | 1313290 | 0 |
| 先触目标页再复制 | 848.9371 | 260.1013 | 1111.7816 | 0 | 1313299 |

这个对照支持：**原 memcpy 区间的大头与目标页面的首次写入相关，而非单纯复制指令计算。** 预触页后复制约为 0.26 秒，但总写入区间反而略增加，触页没有消除成本，仅把成本从 memcpy 中移出去，不能作为性能优化接入。

也不能说“1067.52 − 260.10 = 807.42 ms 就是精确的缺页耗时”：两组执行改变了页面驻留、缓存、TLB 和访存顺序；触页本身也包含循环、写入及内存管理。这是因果对照而不是可直接相减的系统内部计时。进一步区分内核缺页处理、零页供应、页表建立、缓存和内存带宽需要 ETW / 硬件计数器；本次未测这些内部项。

原始 [逐轮 CSV](performance/scintilla-copy-2026-11-13/paired.csv)、[完整输出](performance/scintilla-copy-2026-11-13/paired.txt)、[各项范围与中位数](performance/scintilla-copy-2026-11-13/summary.json)。每项的独立中位数与表中父区间中位轮不是同一概念，报告保留两种信息。

## 测量实现与限制

- 使用 MSVC RelWithDebInfo `/O2 /Ob1`、SSE4.2，i9-12900HK、约 32 GiB 内存、Windows 11。文件与此前报告相同，SHA-256 记录在汇总 JSON 中；未清空系统文件缓存。
- 原始 `3rdparty/` 源码和头文件不变。开启 `MULTIPAD4_PROFILE_SCINTILLA` 才在构建目录生成诊断源码和头文件。
- 全部 Scintilla 翻译单元使用同一份生成的 `SplitVector.h`，避免模板在不同翻译单元中出现不一致定义。只在 `CellBuffer` 的文本写入区间启用细分计数，不把行偏移数组等其他 `SplitVector` 的 memcpy 算进来。
- QPC/steady_clock 计量嵌套区间；320 次复制前后分别调用 `GetProcessMemoryInfo`，这类调用在父区间内、子区间外，差值约 2 ms，不能把差值当业务开销。
- 缺页计数包含进程其他线程的活动，未作为目标页独占计数。预触页对照在复制中出现 0 次缺页，是本次运行结果，不是所有环境保证。
- 测试结束后跑回归并恢复正常无插桩构建；预触页不进入产品加载路径。

## 复现

```powershell
$env:MULTIPAD4_PROFILE_SCINTILLA = 'ON'
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
Remove-Item Env:MULTIPAD4_PROFILE_SCINTILLA
$env:MULTIPAD4_LARGE_TEST_SOURCE = (Resolve-Path tmp/random-text-5GiB.txt).Path
$env:MULTIPAD4_LARGE_TEST_COPY = '1'
.\cmake-msvc-build\multipad4.exe --large-file-test
Remove-Item Env:MULTIPAD4_LARGE_TEST_SOURCE
Remove-Item Env:MULTIPAD4_LARGE_TEST_COPY
# 恢复默认构建
$env:MULTIPAD4_PROFILE_SCINTILLA = 'OFF'
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
Remove-Item Env:MULTIPAD4_PROFILE_SCINTILLA
ctest --test-dir cmake-msvc-build --output-on-failure
```

后续通过改变加载流程来减少复制的逐项实验见 [批量加载与片段存储实验](SCINTILLA_BULK_LOAD_EXPERIMENTS.md)。
