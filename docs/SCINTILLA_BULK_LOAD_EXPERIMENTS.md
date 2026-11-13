# 省掉全文复制的逐项实验

> 历史实验归档：相关未采用原型已移除，保留原始测量记录。当前产品实现见 [按需可编辑文档](MAPPED_EDITING.md)。

日期：2026-11-13。此处记录**实验原型**，不把结果提升为默认策略。`MULTIPAD4_EXPERIMENT_BULK` 默认 OFF；默认读取优化和原 16M 行表预留保持原样，`3rdparty/` 源文件不变。

目的：验证能否通过改变数据流和内存归属，减少 `InsertFromArray` 的复制字节数，而不是改写 memcpy 指令。

## 输入和计时条件

- 与此前相同的 5 GiB 文件：5368709120 字节、41943041 行，ASCII / 无 BOM UTF-8。SHA-256：`5ac1ca64feb2c139decf370968c91cfd6122b25256645e8e3e2cb7821f941e27`。
- 另生成 320000000 字节文件，内容为 `abc\n` 重复 80000000 次，Scintilla 行数 80000001，验证行数更大的输入。
- 同一 i9-12900HK / 32 GiB / Windows 11，MSVC RelWithDebInfo `/O2 /Ob1`、SSE4.2。内部诊断插桩 OFF，实验构建 ON。
- 每组先分别预热一次，随后交错各五轮，奇数轮交换顺序。上一文档已关闭，每轮新建真实 MDI 文档。
- 完整编辑器对照从创建文档前计时，到完整加载、设置可编辑、刷新和同步绘制完成；内存分配、预读和校验均计入，验证和关闭不计入。特殊的“保留源映射”控制组在下文单独标记。
- 每轮确认完整长度、行数、首中尾字节、EOF、编辑和撤销后保存点。旧读取路径已优化，所有基线均为现有 buffered ReadFile + 最多四线程 SIMD 校验，**不是最早的旧读取算法**。
- 文件缓存未清空，也没有 ETW 磁盘请求归因。本报告显示缓存/次序影响，不以最快单轮代替结果。

原始逐轮 TXT、CSV、各项中位数和范围见 [汇总 JSON](performance/bulk-load-2026-11-13/summary.json)。

## 一、直接填充最终缓冲区

流程：

```text
当前：ReadFile → 16 MiB 临时缓冲区 → 校验 → memcpy 到 Scintilla → 行索引
实验：取得 Scintilla gap 可写范围 → ReadFile 直接填入 → 校验 → 提交长度及行索引
```

专用 `PrepareAppend` 只在无样式、关闭撤销收集、可写的暂存文档上提供尾部空间。填充后仍走标准文档插入及行边界维护；源指针等于最终目标时跳过 memcpy。每次校验完成后才提交，失败时丢弃暂存文档。

| 5 GiB，真实编辑器 | 基线中位数 | 实验中位数 |
|---|---:|---:|
| 完整加载 | 4203.124 ms | 4092.147 ms |
| 最小–最大 | 4185.694–4240.125 ms | 4051.988–4106.041 ms |
| 插入区间（总时间中位轮） | 1689.969 ms | 647.108 ms |
| 读取相关区间（同轮） | 2129.497 ms | 3047.467 ms |

总耗时只减少 **2.64%**。约一秒的插入成本大部分转移到对最终文档页的 ReadFile，不能把“插入少了一秒”当作整体省掉一秒。初版直接填充实验尚未加入字节计数输出；随后在 8000 万行对照和专用验证中，断言了 Scintilla 文本 memcpy 字节数为 0、提交字节数等于完整正文。

[原始数据](performance/bulk-load-2026-11-13/direct-5GiB.csv)。

## 二、转移已读取内存的所有权

先按最终容量分配一块未初始化内存，直接读取和校验完整正文；随后把分配的所有权交给 Scintilla 文本数组，不再复制。此原型使用与原 allocator 兼容的分配和释放方式，只在精确容量匹配时接管。所有原始输入空间从开始读取就计入计时，没有把读取藏在计时区间外。

| 5 GiB，真实编辑器 | 基线 | 接管内存 |
|---|---:|---:|
| 完整加载中位数 | 4225.420 ms | 4167.720 ms |
| 最小–最大 | 4188.406–4231.485 ms | 4152.695–4175.243 ms |
| Scintilla 文本 memcpy 字节数 | 5368709120 | **0** |

减少约 **1.37%**，不值得作为当前产品改法。源缓冲已经是最终存储，但首次填充完整正文所需的物理页面仍然存在。

这是“接管一个连续缓冲区”的最小原型。真正接管多个块无需整块连续空间的实验见后面的片段存储部分，不能把二者混为一谈。

[原始数据](performance/bulk-load-2026-11-13/adopt-5GiB.csv)。

## 三、COW 文件映射作为 gap buffer 的后端

这个原型尽量保持现有 Scintilla 连续指针契约：原始正文的完整页采用 PAGE_WRITECOPY 文件映射，尾部另接私有编辑空间。用虚拟地址 placeholder 保证连续地址，正文由文档数组接管；之后提交同地址的文本而不复制，仍建立完整行表。

非完整尾页最多读取 4095 字节到私有尾部，避免越过 section 最大长度。文件仅 GENERIC_READ 打开，修改触发 COW，不改写源文件。空文件、页边界、非整页尾部有单独检查。

### 缓存未固定时：没有稳定收益

| 5 GiB 实验组 | 基线中位数 | COW 中位数 | COW 最小–最大 |
|---|---:|---:|---:|
| 首轮 | 3211.207 ms | 4218.437 ms | 1833.403–4371.330 ms |
| 加页面共享抽样复测 | 3267.849 ms | 4840.315 ms | 1697.815–4896.531 ms |

同组交错仍有明显运行次序/页面驻留影响，不能根据其中约 1.7 秒的快样本宣称普通加载已经稳定加速。这两轮中位数实际上更差。尚未用 ETW 证明不同慢样本的具体磁盘/缺页来源。

采样的 64 个正文页面在载入后为 **64/64 有效、64/64 共享**，证明没有偷偷把所有正文写成私有副本；Scintilla 复制字节数为 0，提交字节数为 5368709120。PrivateUsage 含 COW 提交预算，不等同于已经复制出的物理私有页面，不能据此错误断言已复制全文。

### 明确控制源页面驻留的敏感性实验

另建一个共同的只读源映射，在所有计时开始前验证并持续保留；两种方案都面对这份相同的已驻留源页。**这项准备工作不计入加载时间，增加了测试进程的源映射驻留，不是普通打开场景，也不是锁页保证。** 它用于回答“数据已经驻留时，避免复制有没有收益”。

| 5 GiB，保留共同源映射 | 基线 | COW |
|---|---:|---:|
| 完整加载中位数 | 2478.459 ms | 1552.847 ms |
| 最小–最大 | 2401.251–2838.285 ms | 1476.831–1725.117 ms |

此条件下减少 **37.35%**，约 **1.60 倍**。后续加编辑探针的一轮为 2434.529 → 1520.658 ms，方向一致。

### 编辑会重新承担成本

保持上述共同源映射，在完成加载后，另外计时“在文档开头插入一个字符”，不计入加载时间。五个正式样本中位数 **2168.566 ms**，范围 **2167.936–2182.017 ms**。插入再撤销后，共享抽样由 64/64 变成 0/64：gap 移动写遍正文，引发整段私有化。源文件检查仍通过。

因此它只是减少初次加载的复制，gap buffer 编辑时移动文本的问题仍然存在；不作为默认实现。

原始数据：[首轮](performance/bulk-load-2026-11-13/cow-5GiB-initial.csv)、[共享抽样复测](performance/bulk-load-2026-11-13/cow-5GiB-shared.csv)、[共同源映射控制](performance/bulk-load-2026-11-13/cow-5GiB-retained.csv)、[首次编辑探针](performance/bulk-load-2026-11-13/cow-5GiB-edit.txt)。

## 四、真正接管数据块／原始映射的片段存储原型

不通过 Scintilla 连续 gap buffer，另做存储层原型：

- owned-blocks：读取时按块分配，读取校验后直接转移块所有权，初次文本复制 0 字节。
- mapped：原始只读映射由片段引用持有，初次文本复制 0 字节。
- 文本状态由片段引用组成，插入只分配新增文本；删除拆分/组合片段引用，不搬动原始正文。验证了源文件不变、Unicode 与跨块 CRLF、范围读取和流式保存。
- 建立完整 64 位行起点数组，记录正确总长度及总行数。

**这是独立存储层原型，不是可替换的 Scintilla 文档实现。** 尚未接入真实绘制、完整撤销、正则/lexer/插件的连续指针契约。片段元数据暂用数组，修改元数据为 O(片段数)，不是已经实现 O(log n) 的平衡片段树。编辑后下一次请求行数会重建行表，仍是全文工作；不能据此宣称编辑器的全部编辑动作已消除线性成本。

| 独立存储层，5 GiB | owned-blocks 中位数（最小–最大） | mapped 中位数（最小–最大） |
|---|---:|---:|
| 普通缓存状态 | 2793.168（2697.557–3785.972）ms | 3861.977（1418.831–3983.973）ms |
| 共同源映射已驻留 | 2146.172（2086.615–2203.048）ms | 1183.030（1147.401–1276.857）ms |

这些时间包含该原型的读取/校验和全量行索引，**不能与真实编辑器的端到端时间直接算加速比**。两种模型的行索引实现、视图和消息维护成本不同。

在首、中、尾各插入再移除一个字符，文本累计只复制 **3 字节**。普通缓存组片段引用修改的中位数约 0.128 / 0.131 ms；这个微测试**不包括后续行表重建**，也不是完整编辑器撤销响应时间。它只证明不搬动原始正文的存储操作可以成立。

[普通组](performance/bulk-load-2026-11-13/pieces-5GiB.csv)、[共同源映射组](performance/bulk-load-2026-11-13/pieces-5GiB-retained.csv)。

## 8000 万行复核

没有提高正常的行表预留上限。使用 320000000 字节、80000001 行输入，所有方案均校验完整行数及内容。

| 真实 Scintilla 对照 | 基线中位数 | 候选中位数 | 耗时减少 |
|---|---:|---:|---:|
| 直接填充 | 742.483 ms | 741.924 ms | 0.08% |
| 接管连续内存 | 739.024 ms | 734.218 ms | 0.65% |
| COW 映射 | 744.279 ms | 665.538 ms | 10.58% |

独立片段存储构建完整行表的中位数为 owned-blocks **581.771 ms**、mapped **509.965 ms**；仍是存储层指标。结果表明收益随字节数、行密度与缓存状态变化；没有把 4200 万行文件外推成通用结果。

## 正确性、范围与当前决策

- 原型正确性测试覆盖 Unicode 字符跨读块、CRLF、正文 U+FEFF、文件首中尾插入与撤销、删除替换、跨 gap 读取、保存完整副本、扩容后释放外部内存/映射、后段非法 UTF-8 保留原文档、空文件和 4 KiB 页边界。
- `ReadUtf8Into` 可以去掉 UTF-8 BOM。接管及 COW 的端到端比较和独立片段模型针对无 BOM UTF-8，不宣称已完成其他编码和自动探测/回退的统一接入。
- 协议只用于私有暂存文档，不允许在可编辑活动缓冲区里暴露未提交内存。现有默认加载路径不调用该协议。
- COW 映射持有只读共享源句柄，会影响原路径原子保存；扩容仍会退回整体复制。其平台 API、所有权和错误处理需要正式设计后才能成为产品功能。
- 保留现有读取/校验优化；不把直接填充的 2.6%、连续接管的 1.4% 当作重大改进；COW 有缓存条件下的潜力，但普通缓存波动和首次编辑代价尚未解决。
- 真正减少编辑时正文搬移，需要分段/片段存储及增量行索引，而不是把 COW 接到原 gap buffer 就结束。本轮完成存储可行性和基准验证，未完成整个编辑器内核替换。
- 全部第三方改动都通过构建目录实验副本完成，实验选项默认 OFF，运行后恢复正常构建。

## 复现

```powershell
$env:MULTIPAD4_PROFILE_SCINTILLA = 'OFF'
$env:MULTIPAD4_EXPERIMENT_BULK = 'ON'
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
Remove-Item Env:MULTIPAD4_PROFILE_SCINTILLA
Remove-Item Env:MULTIPAD4_EXPERIMENT_BULK
# 正确性专项
$env:MULTIPAD4_LARGE_TEST_BULK_VERIFY = '1'
.\cmake-msvc-build\multipad4.exe --large-file-test
Remove-Item Env:MULTIPAD4_LARGE_TEST_BULK_VERIFY
# 真实编辑器端到端，BULK 分别取 direct / adopt / cow
$env:MULTIPAD4_LARGE_TEST_SOURCE = (Resolve-Path tmp/random-text-5GiB.txt).Path
$env:MULTIPAD4_LARGE_TEST_E2E = '1'
$env:MULTIPAD4_LARGE_TEST_BULK = 'direct'
.\cmake-msvc-build\multipad4.exe --large-file-test
# COW 缓存条件 / 首次编辑控制（可选）
$env:MULTIPAD4_LARGE_TEST_BULK = 'cow'
$env:MULTIPAD4_LARGE_TEST_RETAIN_SOURCE = '1'
$env:MULTIPAD4_BULK_EDITPROBE = '1'
.\cmake-msvc-build\multipad4.exe --large-file-test
Remove-Item Env:MULTIPAD4_BULK_EDITPROBE
Remove-Item Env:MULTIPAD4_LARGE_TEST_RETAIN_SOURCE
Remove-Item Env:MULTIPAD4_LARGE_TEST_BULK
Remove-Item Env:MULTIPAD4_LARGE_TEST_E2E
# 独立片段存储测试，计时与完整编辑器分开解读
$env:MULTIPAD4_LARGE_TEST_PIECES = '1'
.\cmake-msvc-build\multipad4.exe --large-file-test
Remove-Item Env:MULTIPAD4_LARGE_TEST_PIECES
Remove-Item Env:MULTIPAD4_LARGE_TEST_SOURCE
# 恢复产品构建
$env:MULTIPAD4_EXPERIMENT_BULK = 'OFF'
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
Remove-Item Env:MULTIPAD4_EXPERIMENT_BULK
ctest --test-dir cmake-msvc-build --output-on-failure
```
