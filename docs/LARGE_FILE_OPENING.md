# 超大文件打开机制：EmEditor 逆向分析与 MultiPad4 改造建议

编写日期：2026-11-12

分析对象：

- EmEditor：`tmp/emeditor/EmEditor.exe`（x64，30,286,008 字节，ImageBase `0x140000000`，
  以 IDA Pro 9.3 反编译结果为准；文中的地址/偏移只对这一个构建有效，版本升级后需重新核对）。
- 测试文件：`tmp/random-text-5GiB.txt`（约 5 GiB 的随机文本）。
- MultiPad4：当前 `master` 分支源码，编辑器控件为
  `3rdparty/notepad4-scintilla/scintilla`（Notepad4 的 Scintilla fork）。

结论摘要：

1. EmEditor 的「秒开」不是靠更快的解析，而是靠**根本不读文件**：只读内存映射 +
   64 KB 对齐滑窗 + 64 MB 读块 + 44 位文件偏移，首屏只 touch 可见区域的几十 KB；
   行索引是惰性物化的。
2. MultiPad4 当前打开路径是**整文件读入 + 再复制一份 + 逐字符校验 + 整文档写入 Scintilla +
   整文档同步着色**，5 GiB 文件需要十几 GiB 峰值内存、分钟级时间，且超过 2 GiB 后
   行索引会静默溢出。
3. 不修改 Scintilla 也能把 5 GiB 打开压到「数秒 + 内存 ≈ 文件大小 + 行索引」，
   前提是正确使用 vendored fork 已有的 `SC_DOCUMENTOPTION_TEXT_LARGE` /
   `SC_DOCUMENTOPTION_STYLES_NONE` / `SCI_ALLOCATELINES`，并移除 MultiPad4 侧的全量
   读、全量校验、全量着色、全量重建文档。
4. 要做到 EmEditor 的「mmap 秒开 + 常驻内存恒定」，必须替换 Scintilla 的文档存储层
   （`CellBuffer` 的 gap buffer、`ILineVector` 的整表二分、`Editor` 的可见行/滚动映射），
   属于重写编辑器内核，不建议以 fork 的形式承担；更划算的是为超大文件提供一个独立的
   只读虚拟视图。

---

## 1. 背景与目标

MultiPad4 使用 Scintilla 作为编辑控件。Scintilla 的文档模型是「整份文本放进一块可增长
内存 + 行起点分区表」，因此文件大小直接等于内存占用和装载时间。EmEditor 则采用了另一套
文档模型，能够以接近 O(1) 的时间打开任意大小的文件。

本文要回答三个问题：

1. EmEditor 到底怎么打开大文件（有哪些数据结构、以什么顺序调用哪些 API）；
2. MultiPad4 当前实现与它的差距在哪里（精确到函数与代码行）；
3. 要缩小差距，哪些改动是「用对现有能力」，哪些是「必须深度定制 Scintilla」。

---

## 2. 分析方法

- 使用无头 IDA：`idat.exe -A -L<log> -S<bridge.py> EmEditor.exe`，通过本地 broker
  把 `ida-pro-mcp` 工具接到无头实例上，再用反编译/交叉引用/字符串/PE 导入表定位
  `CreateFileW`、`CreateFileMappingW`、`MapViewOfFile`、`ReadFile` 等调用点。
- 只用静态分析；运行期行为（工作集、首屏时间）来自代码路径推理，未做进程级采样。
  文中的时间/内存估算都标注了假设。

---

## 3. EmEditor 的实现

### 3.1 总体架构

EmEditor 的文档对象同时挂两种底层存储：

| 存储 | 用途 | 关键实现 |
|---|---|---|
| 只读文件映射 `CMyFileMap<0>` | 打开未修改的文件，按 64 KB 对齐滑窗映射 | `sub_140FA7A00` / `sub_140FA6CF0` / `sub_140FA7AF0` |
| 可写临时映射（或堆内存）`CVirtualMap` | 编辑缓冲区、排序/拷贝/保存的溢出空间 | `sub_140931360` / `sub_140435860` |

文档对象里用 44 位偏移（16 TiB 上限）描述文件位置；行索引按固定行数分块存放，
每条目 34 字节（紧凑格式 24 字节），未建立的行有「无效」标记。这样「打开」的代价
可以与文件大小无关，只有滚动到某处或编辑时才把对应窗口换入、把对应行块补建。

### 3.2 打开流程

主入口是 `sub_1408B8B90`，它调用 `sub_140FA7E90` 完成实际打开：

```c
// 还原后的关键步骤（伪代码）
hFile = CreateFileW(path, GENERIC_READ,
                    FILE_SHARE_READ | (allowWriteShare ? FILE_SHARE_WRITE : 0),
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);   // sub_140FA7D00
GetFileSizeEx(hFile, &size);                                          // size -> doc+248

limitMb = sub_140FA6810();
if (limitMb == 0x7FFFFFFF || size < (unsigned long long)limitMb << 20)
    -> 内存模式：整文件读入分配的内存缓冲（sub_140FA78D0）
else if (setting371 开启 && 未强制只读 && doc+338 == 0)
    -> 磁盘模式：doc+338 = 1，重新以 FILE_SHARE_READ|WRITE 打开，
       之后按窗口映射（sub_140FA7A50 / sub_140FA7AF0）
```

- `CreateFileW` 的封装在 `sub_140FA7D00`：共享模式按参数拼成 `2 * (readOnly ^ 1) + 1`，
  即 `a2=1` 时 `FILE_SHARE_READ`，`a2=0` 时 `FILE_SHARE_READ | FILE_SHARE_WRITE`。
- 文件大小写入文档对象 `+248`。
- 模式阈值来自 `sub_140FA6810()`：
  - 全局开关 `byte_141C057F0` 关闭时返回 `0x7FFFFFFF`，调用方把它当「无限制」，
    即总是内存模式（整文件读入）；
  - 该开关在本构建中**默认为 1**，配置值 `dword_141C057F4` **默认为 300**（单位 MB）；
    也就是说默认策略是「300 MB 以下读入内存，300 MB 及以上使用磁盘映射」，
    与官方文档「默认在 300 MB 以上启用临时文件」一致；
  - 即使超过阈值，还要满足配置项 371 打开、文件未被强制只读，才会切换到磁盘模式。
- 磁盘模式标志 `doc+338` 会出现在 EmEditor 的崩溃报告里（字符串 `DiskMode`），
  可用于交叉验证。

补充：官方从 v26.3 起把「不在内存中完整载入文件」的能力称为 **Fast View mode**
（默认 4096 MB 以上自动启用）；本二进制里直接观察到的是 300 MB 的存储切换阈值。
两者是不同层次的开关：前者决定是否解析/索引全文，后者决定文本实体放在内存还是
映射文件里。

### 3.3 只读映射层：64 KB 对齐的滑窗

EmEditor 不是一次性 `MapViewOfFile` 整个文件（大文件会失败且浪费地址空间），而是
维护一个可移动窗口：

1. **建立节对象**：`sub_140FA7A00` 调用

   ```c
   CreateFileMappingW(hFile, NULL, PAGE_READONLY /*2*/, high, low, NULL);
   ```

   句柄缓存在文档对象里。

2. **映射窗口**：`sub_140FA6CF0` 调用

   ```c
   MapViewOfFile(section, FILE_MAP_READ /*4*/, high32, offset & ~0xFFFF, size);
   ```

   并把结果写回文档对象：

   | 字段 | 含义 |
   |---|---|
   | `doc+96` | 当前视图基址（`MapView`，按 64 KB 对齐） |
   | `doc+104` | 视图起始地址（基址 + 起始偏移） |
   | `doc+168` | 视图结束文件偏移（`CurrViewEnd`） |
   | `doc+176` | 视图起始文件偏移（`CurrViewBegin`） |
   | `doc+256` | 本次映射长度 |
   | `doc+272` | 视图末地址（基址 + 结束偏移） |

3. **换窗**：`sub_140FA7AF0` 先 `UnmapViewOfFile`，再按文档对象 `+216` 记录的窗口大小
   重新映射；映射失败时窗口减半，最低到 64 MB（`0x4000000`），最多重试 3 次。

4. **回退路径**：`sub_140FA6DE0` 在映射不可用时改用 `ReadFile` 读入一个 64 MB 环形
   缓冲区（`doc+112 - offset + 0x4000000` 计算读指针）。

5. **释放**：`sub_14098AEA0` 是对 `UnmapViewOfFile` 的封装（`CMyFileMap` 的虚函数之一）。

`CMyFileMap<0>` 的对象布局（由 RTTI/vtable 还原，vftable `0x141ac4e58`）：

| 偏移 | 内容 |
|---|---|
| +0 | vftable |
| +8 | `hFile` |
| +16 | `hMapping` |
| +40 | 文件大小 |
| +48 | 当前视图基址 |
| +56 / +64 | 视图有效区间 |
| +72 | 「不要关闭句柄」标志 |

设计要点：

- 视图偏移按 64 KB 对齐（`offset & ~0xFFFF`），保证任意小于 64 KB 的读取最多跨两个视图；
- 窗口基准大小按机器内存动态计算（见 3.7 的 `dwNumberOfBytesToMap`），
  单次视图再封顶到 1 GB（`doc+216 = min(计算结果, 0x40000000)`）；
- 视图大小会随失败自适应缩小，因此网络盘/稀疏文件等极端情况也能退化工作。

### 3.4 可写映射与临时文件

编辑不能写回只读映射，EmEditor 的做法是把文档实体放进可写存储：

1. **创建临时文件映射**：`sub_140931360`

   ```c
   size = min(requested, 0xFFFFFFFFFFF);          // 2^44-1 = 16 TiB-1
   GetTempFileNameW(tempDir, L"EME", 0, name);    // 目录来自配置/环境
   h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                   0x10000100 | (mode << 26), NULL);
   // 0x10000100 = FILE_FLAG_RANDOM_ACCESS (0x10000000) | FILE_ATTRIBUTE_TEMPORARY (0x100)
   // mode<<26 按调用者选择 FILE_FLAG_DELETE_ON_CLOSE (0x04000000)
   // 或 FILE_FLAG_SEQUENTIAL_SCAN (0x08000000)
   CreateFileMappingW(h, NULL, PAGE_READWRITE, high, low, NULL);
   MapViewOfFile(..., 0, 0, 0x40000000);          // 首个视图 1 GB
   ```

   调用者包括保存流程 `sub_1408BD0C0` 和「复制全部」`sub_141847520`。

2. **可写映射的换窗**：`sub_140976D30`

   ```c
   begin     = offset & ~0xFFFF;
   remaining = min(mappingSize - begin, 0x40000000);        // 单视图上限 1 GB
   end       = ((len + 2*offset - begin) & ~0xFFFF) + 0x10000;
   end       = min(end, mappingSize);
   bytes     = max(end - begin, remaining);
   MapViewOfFile(section, FILE_MAP_WRITE, begin >> 32, begin, bytes);
   ```

   注意 `len + 2*offset` 的保守估算：保证一次映射覆盖「当前位置 + 预期写入长度」，
   避免频繁缺页式换窗。

3. **缓存复用**：`sub_140976C70`（`CMyFileMap::MapView`）先卸载当前视图，
   若 `offset + len <= mappingSize` 则直接在现有节上重新 `MapViewOfFile`；
   否则关闭旧节，按
   `newSize = (len + offset + min(qword_141C52688 / 2, 0x3FFFFFFF)) & ~0xFFFF`
   （下限 64 KB）重新建节和视图。

4. **内存/磁盘二选一**：`CVirtualMap`（vftable `0x141ac1980`）的构造在 `sub_140435860`：
   - 若系统内存负载 ≤ 构造时设定的上限（默认 90%），
     `VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)`；
   - 否则建临时文件 + `PAGE_READWRITE` 映射 + 完整 `MapViewOfFile`；
   - 要求空闲磁盘 > size + 128 MB，否则失败。
   - 构造 `sub_140435D70` 分配 0x40 字节对象，最小 512 KB，按 64 KB 取整。

这套「小内存走堆、大内存走临时文件映射」的策略，使 EmEditor 在打开 20 GB 文件时
物理内存占用仍可控，操作系统负责把不活跃页换出。

### 3.5 行索引：块状 + 44 位偏移 + 惰性物化

`sub_1408F0800`（`GetLogicalFromFilePosForMapView`）把文件位置换算成行列：

- 行条目步长 34 字节；当 `doc+124` 非零时用 24 字节紧凑格式；
- 条目按固定数量分块，块指针数组在 `doc+8`，每块条目数在 `doc+112`——
  块是按**行号**划分的（`block = line / entriesPerBlock`），不是按字节；
- 条目字段：
  - `+0`：该行文本指针，非空表示内容已物化；
  - `+8`：文件偏移低 32 位；
  - `+12`：该行长度（低 32 位，用于判断是否跨越视图末端）；
  - `+20 & 0xFFF`：偏移高 12 位 → 合计 44 位（16 TiB）；
  - `+23 & 0x4`：该行有效标志。
- 文档对象 `+2505` 是「索引已可用」的快速路径开关；`+592` 与当前视图末端比较，
  用于判断目标行是否可直接命中。

关键点是**行条目可以不存在**：`(entry+23 & 4) == 0` 时 `sub_1408F0800` 返回 `-1`，
调用方据此触发对应块的扫描/建索引。也就是说行索引允许滞后于文件内容，
打开时不必为整个文件建立完整行表；只有在滚动或跳转到未物化区域时才补建。
（`GetLogicalFromFilePosForMapView` 的失败路径里带有 `nCurrentViewBegin` /
`nCurrentViewEnd` / `yMin` / `yMax`，正是这个「视图 + 目标行区间」的参数组合。）

### 3.6 分块 I/O 与保存

`sub_140AECAD0` 是通用的大块拷贝/落盘例程：

- 以 1 GB 为单位切分；
- 中间使用 64 MB 暂存缓冲，缓冲区满即 `WriteFile` 落盘（「spill」）；
- 单块 ≥ 256 KB 时走非时序 `memcpy`（`qword_141C52A88` 指向的实现），减少缓存污染。

保存入口 `sub_1408BD0C0` 复用临时映射 + 分块回写，不会把整份文档 `GETTEXT` 到一块内存。

### 3.7 全局上限与配置

`sub_140F71AE0` 在启动时调用 `GlobalMemoryStatusEx`，计算两个全局上限：

```c
qword_141C52688        = clamp(ullTotalPageFile / 2,            64 MB, 0x4000000000);  // 256 GB
dwNumberOfBytesToMap   = clamp(ullTotalPageFile * 2 / 3,        64 MB, 0x4000000000000); // 1 PB
MaxReserveBits         = 24 / 25 / 26 / 31（按页面文件 GB 数分档）
```

`sub_140F78A80` 读取配置并做上下限裁剪（配置键 `AutoMemorySizes=1` 时跳过读取，
直接用硬件计算值）：

| 配置项 | 默认值 | 裁剪范围 |
|---|---|---|
| `MaxReserveBits` | 硬件计算结果（24/25/26/31） | 16 ~ 31 |
| `MemoryFileMB` | `(qword_141C52688 + 1) >> 20` | 64 MB ~ 1 PB |
| `MaxMemoryMB` | `(dwNumberOfBytesToMap + 1) >> 20` | 64 MB ~ 1 PB |
| `SortMemoryPercent` | 90 | 1 ~ 99 |

最终 `qword_141C52688 = MemoryFileMB << 20`，
`dwNumberOfBytesToMap = max(MaxMemoryMB << 20, qword_141C52688)`。

`sub_140F90310` 计算**后续映射窗口的基准大小**（写入 `doc+208`，再取
`doc+216 = min(该值, 1 GB)`）：

```c
if (sub_140F90270())                       // 内存负载 ≥ SortMemoryPercent（默认 90）
    return 64 MB;                          // 退化为小窗口，减少压力
window = dwNumberOfBytesToMap - qword_141C52220;   // 全局映射预算减去已用
window = max(window, 64 MB);
return min(window, qword_141C52688);               // 再受「页文件一半」上限约束
```

也就是说：**窗口不是写死的常量，而是按机器内存动态调整的**，并且同时受全局地址空间
预算与「页文件一半」两个上限约束；单次视图最大 1 GB。

### 3.8 设计要点小结

| 设计点 | EmEditor 做法 | 效果 |
|---|---|---|
| 打开 | 只读映射，滑窗按需换入 | 打开时间与文件大小基本无关 |
| 首屏 | 只 touch 可见区域所在窗口 | 首屏几十 KB 缺页，毫秒级 |
| 行索引 | 按行分块 + 未物化标记 + 44 位偏移 | 未建索引的行返回 -1，可延后扫描 |
| 编辑 | 临时文件 `PAGE_READWRITE` 映射 | 内存由 OS 管理，可超物理内存 |
| 窗口 | 64 KB 对齐、失败减半重试、64 MB 下限 | 极端文件/网络盘可退化 |
| 保存 | 1 GB 分块 + 64 MB 暂存 + 非时序 memcpy | 峰值内存有界 |
| 上限 | 按 `GlobalMemoryStatusEx` 动态计算 | 不同机器行为一致 |

---

## 4. MultiPad4 现状

### 4.1 技术栈与数据流

MultiPad4 = C++20 + WTL + Scintilla（Notepad4 fork）。文件打开的真实路径：

```
MainFrame::Open
  -> files::Read(path)                      // FileIO.cpp
       -> ReadBytes(path)                   // 整文件读进 std::string
       -> Decode(bytes, encoding)           // 又复制一份，再做编码转换/校验
  -> EditorDocument::Create
  -> EditorDocument::Load(text)             // 一次性灌入 Scintilla
       -> SCI_CREATEDOCUMENT(len, options)
       -> SCI_ADDTEXT(len, text.data())
       -> ConfigureLexer -> ConfigureDocumentStyle -> SCI_COLOURISE(0, -1)
```

### 4.2 Scintilla fork 已有的能力

必须先说明：本项目 vendored 的 Notepad4 fork **已经具备**大文档所需的底层机制，
不需要改 `3rdparty/`：

| 能力 | 位置 | 说明 |
|---|---|---|
| 64 位位置 | `include/Sci_Position.h:13-14`、`src/Position.h:18` | `Sci::Position`、`Sci::Line` 都是 `ptrdiff_t` |
| 大文档行索引 | `src/CellBuffer.cxx:336-341` | `SC_DOCUMENTOPTION_TEXT_LARGE` → `LineVector<Sci::Position>` |
| 去掉 2 GB 限制 | `src/CellBuffer.cxx:579-581` | fork 已把上游的 `INT32_MAX` 抛错注释掉 |
| 不分配 style 缓冲 | `SC_DOCUMENTOPTION_STYLES_NONE (0x1)` | 5 GiB 文件可省 5 GiB 内存 |
| 预分配行表 | `SCI_ALLOCATELINES`（`src/Editor.cxx:6566`） | 避免逐行扩容 |
| 按需渲染 | 行布局缓存 / `PositionCache` / `SCI_SETIDLESTYLING` | 只布局可视行 |
| 官方用法模板 | `3rdparty/notepad4/src/Edit.cpp:137-192`、`Edit.h:511` | 阈值 `2^31-16`；`TEXT_LARGE` + `STYLES_NONE`、`WM_SETREDRAW FALSE`、`SC_MOD_NONE`、`AllocateLines`、`AppendText` |

Notepad4 的阈值常量：

```c
#define MAX_SMALL_FILE_SIZE ((1U << 31) - 16)   // Edit.h:511
```

超过时切换到 `SC_DOCUMENTOPTION_TEXT_LARGE | SC_DOCUMENTOPTION_STYLES_NONE`，
并把 `bLargeFileMode` 设为真，之后应用层关闭着色等重操作。**它依然是全内存模型**，
只是把行索引扩到 64 位并去掉 style 缓冲。

### 4.3 打开路径逐段分析

| 步骤 | 代码位置 | 行为 | 5 GiB 代价 |
|---|---|---|---|
| 读取 | `src/FileIO.cpp:147-155` `ReadBytes` | `ifstream` + `ate`，整文件读入 `std::string` | 5 GiB 常驻 |
| 解码入口 | `src/FileIO.cpp:97-98` `Decode` | 形参是 `const std::string&`，函数体第一行 `std::string bytes = input;` **无条件复制** | 再 5 GiB，峰值 10 GiB |
| UTF-8 校验 | `src/FileIO.cpp:21-27` `Validate` | 逐字符调用 `MultiByteToWideChar`，且 `Read` 内可能调用 1~2 次、`Decode` 内再一次 | 数十亿次 Win32 调用，分钟级 |
| 编码猜测 | `src/FileIO.cpp:174-194`、`115-142` | 随机文本大概率不是合法 UTF-8 → 落到 ANSI/代码页分支，16 MB 分块 `MultiByteToWideChar` + `WideCharToMultiByte` 全量转码 | 分钟级 + 多份 5 GiB 中间缓冲 |
| 文档选项 | `src/EditorDocument.cpp:44` | `Send(SCI_GETDOCUMENTOPTIONS) | additionalDocumentOptions`；默认打开点 `MainFrame.cpp:91` 传 0 | >2 GiB 时 `LineVector<int>` 中 `static_cast<int>(position)` 溢出（`src/CellBuffer.cxx:176-178`），行号/滚动/跳转静默错乱 |
| 装载 | `src/EditorDocument.cpp:50` | 一次 `SCI_ADDTEXT` 灌入整份文本，无 `SCI_ALLOCATELINES` | 一次 5 GiB 分配 + 5 GiB memcpy + 全量换行扫描 |
| 着色 | `src/StyleManager.cpp:649`（`ConfigureDocumentStyle` 末尾；另有 `:438` 的折叠路径） | `SCI_COLOURISE, 0, -1`，在 `Load()` 中经 `ConfigureLexer()` 同步执行（`src/EditorDocument.cpp:63`） | 全文档词法 + 5 GiB style 缓冲 |
| 自动备份 | `src/AutoSave.cpp:66` | 修改后按周期调用 `document.Text()` 再写快照 | 又一次 5 GiB 拷贝 + 5 GiB 磁盘写 |
| 保存 | `src/EditorDocument.cpp:84-89`、`src/FileIO.cpp:305-306` | `Text()` 全量 GETTEXT → `PrepareEncodingSave` → `Encode` 全量复制 → 5 GiB 临时文件 → `ReplaceFileW` | 峰值约 15 GiB，额外 5 GiB 磁盘写 |

补充：

- 唯一的「大文件模式」入口在 `src/FileCommands.cpp:100-105`，但它是
  `d->Text()`（先全量拷出）再 `Load(..., SC_DOCUMENTOPTION_TEXT_LARGE)`，
  且**只开 `TEXT_LARGE`，不开 `STYLES_NONE`**，也没有跳过 `SCI_COLOURISE`；
- 全项目没有出现 `SC_DOCUMENTOPTION_STYLES_NONE`（`grep -r` 为空），
  也就是说 style 缓冲永远按 1 字节/字符分配；
- `EditorDocument::Load` 会创建 staging Scintilla 窗口并在其上完成装载，随后整体
  替换文档指针（`src/EditorDocument.cpp:39-61`），这个设计对「失败保留旧文档」是正确的，
  但对大文件意味着必须同时容纳旧文档和新文档两份内存。

### 4.4 关键缺陷清单

按严重程度排序：

1. **>2 GiB 必然出错**：默认路径不带 `TEXT_LARGE`，`LineVector<int>` 的行起点在
   2 GiB 之后溢出为负数，`PartitionFromPosition` 二分失效。表现是行号错乱、
   跳转定位错误、滚动条范围异常，且没有报错。
2. **全量逐字符校验**：`Validate` 对每个字符调一次 `MultiByteToWideChar`，
   5 GiB 文件就是 50 亿次调用，是当前打开时间的主要来源。
3. **两份（甚至三份）全量拷贝**：`ReadBytes` → `Decode` 内复制 → Scintilla 缓冲；
   若走 ANSI 转换，中间还有 wide/UTF-8 两份。
4. **全文档同步着色**：`SCI_COLOURISE, 0, -1` 在装载后立即执行，既慢又分配 5 GiB style。
5. **保存/自动备份也是全量**：`Text()` + `Encode` + 临时文件，峰值内存和磁盘 I/O 都翻倍。
6. **无 `AllocateLines`**：行表按几何增长多次扩容（`src/SplitVector.h:100-109`），
   每次扩容都要整体搬移。

### 4.5 成本估算（5 GiB、约 40 字节/行 ≈ 1.34 亿行）

假设：NVMe 顺序读 2.5~3.5 GB/s，内存 memcpy 5~10 GB/s，词法 ~100 MB/s。

| 阶段 | 现状 | 仅修复 MultiPad4 侧 | EmEditor |
|---|---|---|---|
| 读取 | 5 GiB 顺序读（约 2 s） | mmap 分块读（同等 I/O，无中间拷贝） | 不读，只缺页可见区（<10 ms） |
| 编码校验/转换 | 逐字符校验 + 可能全量转码（数十秒~分钟） | 只嗅探前 64 KB（<10 ms） | 按行解码到显示为止 |
| 行索引 | 全量扫描（约 1 s） | 全量扫描（约 1 s） | 惰性，块按需（≈0） |
| 写入 Scintilla | 5 GiB 分配 + memcpy（约 1 s） | 同上 | 无 |
| 着色 | 全文档（约 30~100 s） | 跳过（仅可视区/仅词法按需） | 仅可见行 |
| 峰值内存 | >15 GiB | 文件 + 行索引 ≈ 6.5~7 GiB | 几十 MB 量级（工作集） |
| 打开总时间 | 分钟级 | 3~5 s | <0.5 s |
| 滚动 | 会先卡在装载/着色 | 平滑 | 平滑 |

（估算只用于量级判断，实际取决于磁盘、CPU 与文件内容。）

### 4.6 小结

MultiPad4 的问题不在 Scintilla，而在：

- 没有使用 fork 已经提供的大文档选项；
- 把「读文件」实现成了多次全量内存操作；
- 把「配置外观」实现成了整文档同步着色；
- 把「保存/备份」实现成了整文档导出。

这四点都可以在不改动 `3rdparty/` 的前提下修掉。

---

## 5. 改造建议

### 5.1 决策矩阵

| 文件大小 | 打开策略 |
|---|---|
| < 64 MiB | 现有路径（`fileio_roundtrip` 行为保持不变） |
| 64 MiB ~ 2 GiB | 流式 mmap 装载；仍可全功能编辑/着色，但着色改为 idle |
| ≥ 2 GiB（或 `size + 预估行数 ≥ 2^31-16`） | 强制 `TEXT_LARGE` + `STYLES_NONE`，进入「大文件模式」：只读或受限编辑、无词法、无自动备份 |
| ≫ 物理内存（如 20 GiB+） | 建议不载入 Scintilla，改用独立只读虚拟视图（见 5.4） |

判定应在 `MainFrame::Open` 的 `files::Read` 之前完成，避免「先读进来再判断」。

### 5.2 阶段一：流式装载（不改 Scintilla，收益最大）

#### 5.2.1 新增映射读取

建议新增 `src/LargeFile.h/.cpp`：

```cpp
namespace files {

class MappedFile {
public:
    explicit MappedFile(const std::filesystem::path& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    unsigned long long Size() const noexcept { return size; }
    // 按 64 MiB 窗口回调；窗口地址只在回调期间有效。
    template <typename Fn>
    void ForEachChunk(size_t chunkSize, Fn&& fn) const;

private:
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    unsigned long long size = 0;
};

}
```

实现要点：

- `CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)`；
- `CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr)`；
- 每次 `MapViewOfFile(mapping, FILE_MAP_READ, high, low, chunk)`，偏移按 64 KB 对齐，
  回调结束立即 `UnmapViewOfFile`；
- 映射失败时窗口减半重试（借鉴 EmEditor `sub_140FA7AF0`），下限 4 MiB。

#### 5.2.2 `EditorDocument` 增加大文件装载入口

保留现有 `Load`（测试和小文件路径不变），新增：

```cpp
struct LoadOptions {
    bool largeFile = false;      // TEXT_LARGE
    bool noStyles = false;       // STYLES_NONE
    bool deferStyling = false;   // 不调用 SCI_COLOURISE
    size_t chunkSize = 64u << 20;
};

void EditorDocument::LoadMapped(const files::MappedFile& file, const LoadOptions& options);
```

核心序列（在 staging 窗口上执行）：

```cpp
auto stage = [&](UINT message, WPARAM w = 0, LPARAM l = 0) {
    return ::SendMessageW(staging.value, message, w, l);
};

int options = 0;
if (load.largeFile) options |= SC_DOCUMENTOPTION_TEXT_LARGE;
if (load.noStyles)  options |= SC_DOCUMENTOPTION_STYLES_NONE;

stage(SCI_SETCODEPAGE, SC_CP_UTF8);
auto document = stage(SCI_CREATEDOCUMENT,
                      static_cast<WPARAM>(file.Size() + 1), options);
stage(SCI_SETDOCPOINTER, 0, document);
stage(SCI_RELEASEDOCUMENT, 0, document);
stage(SCI_SETUNDOCOLLECTION, FALSE);
stage(SCI_SETSTATUS, SC_STATUS_OK);
stage(SCI_ALLOCATELINES, estimatedLines);          // 见 5.2.3
stage(SCI_SETMODEVENTMASK, SC_MOD_NONE);           // 装载期间不通知

file.ForEachChunk(load.chunkSize, [&](const void* data, size_t length) {
    stage(SCI_APPENDTEXT, length, reinterpret_cast<LPARAM>(data));
});

stage(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
// 校验：SCI_GETSTATUS / SCI_GETLENGTH，失败则保留旧文档（沿用现有 staging 语义）
```

窗口侧：

```cpp
::SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
// ... 装载 ...
::SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
::InvalidateRect(hwnd, nullptr, TRUE);
```

#### 5.2.3 行数预估

两种做法，任选：

- 装载前用一次 `mmap` 扫描统计 `\n`（额外一次内存遍历，约 0.2~0.5 s/GiB）；
- 或按 `size / 40` 估算，低估时由 `Partitioning` 自行扩容（每块只搬行表，不搬文本）。

推荐第二种起步，实测后再决定是否值得预扫描。

#### 5.2.4 样式策略

`ConfigureDocumentStyle`（`src/StyleManager.cpp:555`）目前承担两件事：
设置颜色/字体等外观，以及末尾 `SCI_COLOURISE, 0, -1` 触发整文档着色。建议拆分：

```cpp
void ConfigureDocumentAppearance(EditorDocument& d);   // 只做 STYLESET* / MARGIN / ELEMENT
void ConfigureDocumentStyle(EditorDocument& d);        // 外观 + SCI_COLOURISE（小文件）
```

大文件模式下：

- 只调用 `ConfigureDocumentAppearance`；
- `SCI_SETLEXER(SCLEX_NULL)`；
- `SCI_SETIDLESTYLING(SC_IDLESTYLING_NONE)`，避免后台补全整文档；
- 关闭自动折叠（`SCI_SETAUTOMATICFOLD(0)`）、关闭 wrap（`SCI_SETWRAPMODE(SC_WRAP_NONE)`）、
  关闭 `SCI_SETSCROLLWIDTHTRACKING`，用固定 `SCI_SETSCROLLWIDTH`；
- 行号边距按当前行数计算宽度（`digits(lineCount) * 平均字符宽`），否则固定 54 px 不够。

#### 5.2.5 编码策略

大文件不应做全量校验/转码：

1. 只读前 64 KiB：
   - BOM 为 UTF-8 → UTF-8；
   - BOM 为 UTF-16LE/BE → 若文件 > 256 MiB，提示「大文件暂不支持 UTF-16」并只读打开；
   - 无 BOM 且前 64 KiB 是合法 UTF-8 → 按 UTF-8 处理（不校验剩余部分）；
   - 否则 → 按字节流处理（Latin-1 语义），状态栏提示编码为「检测失败」。
2. Scintilla 在 `SC_CP_UTF8` 下对非法序列是容忍的，且**存储的是原始字节**
   （非法序列在屏幕上可能显示为替换字形，但不会改写缓冲区内容），
   因此保存时可以字节级往返，不会破坏文件；这与 `Validate` 的严格拒绝策略不同，
   需要在 UI 上明确告知用户。
3. 若用户主动选择 ANSI/其他代码页，再做分块转码（16 MiB 一块，边转边 `APPENDTEXT`），
   并接受额外的时间和内存。

#### 5.2.6 保存与自动备份

- 大文件模式禁用自动备份（`SaveDocumentRecovery` 直接返回 false，
  见 `src/AutoSave.cpp:57`）；
- 保存改为流式：按 64 MiB 用 `SCI_GETTEXTRANGE` 取证，直接 `WriteFile` 到临时文件，
  完成后 `ReplaceFileW`（沿用 `src/FileIO.cpp:305` 的原子替换语义，但不构造整份 `Encode`）；
- 若暂不实现流式保存，则大文件模式只允许「另存为」，并在保存前提示内存需求。

### 5.3 阶段二：大文件模式产品化

需要一并处理的交互与功能：

| 功能 | 大文件模式策略 |
|---|---|
| 撤销 | 关闭 `SCI_SETUNDOCOLLECTION`，或限制历史长度 |
| 查找/替换 | 允许，但「全部替换」先提示影响行数；结果面板分页 |
| 查找全部/字数统计 | 禁止使用 `EditorDocument::Text()`，改为 `SCI_GETTEXTRANGE` 分块统计 |
| 拖放/复制全部 | 同上，分块 |
| 语法着色 | 关闭；可提供「为本屏幕着色」手动命令 |
| 折叠 | 关闭（`ContractionState` 会按行分配，1.34 亿行代价高） |
| 自动备份 | 关闭 |
| 崩溃恢复 | 记录路径与修改状态，不保存全文 |
| 状态栏 | 显示 `Large file (read-only)` / 文件大小 / 编码检测结果 |

### 5.4 阶段三：EmEditor 级（需要重写 Scintilla 文档模型）

如果产品要求「打开 20 GiB 文件、常驻内存与文件大小无关」，只靠配置 Scintilla 做不到。
必须改的三层：

1. **存储层 `CellBuffer`**
   - 把 `SplitVector<char> substance` 换成「按 64 KB 对齐的文件映射窗口 + 脏页覆盖」；
   - `CharAt` / `GetCharRange` / `AllView` 可以适配，但
     `BufferPointer()`（`src/SplitVector.h:406`，保存与词法依赖它返回连续内存）和
     `RangePointer()`（`src/SplitVector.h:416`，undo/搜索依赖）必须改成
     「分块迭代 + 跨块拼接」的新 API，并修改所有调用方；
   - 编辑写入走临时文件 `PAGE_READWRITE` 映射（对应 EmEditor 的 `CMyFileMap::MapView`）。

2. **行索引层 `ILineVector` / `Partitioning`**
   - 现模型假设「整份行起点表在内存、任意行可 O(log n) 二分」；
   - 需要引入「按固定行数分块的行条目 + 未物化标记」的惰性结构
     （思路与 EmEditor 的 `doc+8` / `doc+112` 一致）；
   - 受影响的调用方包括滚动条映射、可见行枚举、`SCI_GETLINECOUNT`、
     `SCI_GOTOLINE`、`SCI_POSITIONAFTER` 等视图层逻辑。

3. **视图层 `Editor`**
   - 行布局缓存、`PositionCache`、滚动范围计算都要接受「尚未物化的区域」；
   - 需要定义未物化时的行为（显示占位、后台扫描、或阻塞到扫描完成）。

工作量与风险：这是一个多月量级的改动，且会与上游 Scintilla 持续分叉；
Notepad++ 与 Notepad4 都没有走这条路。**不建议在 MultiPad4 里 fork 这条路线。**

更划算的替代方案：为超大文件提供**独立的只读浏览视图**：

- 自定义虚拟列表控件（或 `ListView` 的虚拟模式）+ mmap 滑窗 + 稀疏行索引；
- 只在可见行上做 UTF-8 解码与绘制；
- 「转为可编辑」时再按用户确认载入 Scintilla（并给出内存/时间提示）。

这可以在几百行代码内实现「秒开 + 恒定内存」，且完全不影响现有编辑器功能。

### 5.5 不建议的方案

- **给 Scintilla 打补丁把 `SplitVector` 换成文件映射**：改动面覆盖 `CellBuffer`、
  `Document`、`Editor`、`UndoHistory`、`RunStyles`，回归成本远超收益。
- **用 `SCI_SETREADONLY` 解决一切**：只读并不能减少装载和着色成本。
- **依赖操作系统文件缓存**：第一次打开仍然要把 5 GiB 读一遍，达不到 EmEditor 的首屏时间。

---

## 6. 验证计划

### 6.1 自动化

在 `tests/FileIOTests.cpp` 增加：

- 生成 128 MiB 合成文件（ASCII、UTF-8 多字节、CRLF/LF 混合各一份）；
- 校验 `MappedFile::ForEachChunk` 拼接结果与 `ReadBytes` 逐字节一致；
- 校验大文件路径的 `SCI_GETLENGTH`、行数、首行/末行内容；
- 校验 `SCI_GETDOCUMENTOPTIONS` 含 `TEXT_LARGE | STYLES_NONE`。

在 `src/SmokeTest.h` / `FileCommandSmokeTest.h` 增加：

- 打开 128 MiB 文件后 `SCI_GETLINECOUNT > 0`；
- `SCI_GETLINEENDPOSITION(最后一行) == SCI_GETLENGTH`，且跳转到最后一行后
  `SCI_GETCURRENTPOS >= SCI_POSITIONFROMLINE(最后一行)`；
- 保存副本后与源文件哈希一致（UTF-8 无 BOM 情形）。

CI 不加入 5 GiB 用例；用 128~256 MiB 覆盖逻辑，5 GiB 作为手工验收。

### 6.2 手工验收

| 场景 | 检查点 |
|---|---|
| 5 GiB UTF-8 (LF) | 首屏时间、滚动到末尾、跳转到指定行、内存峰值 |
| 5 GiB 含 CRLF | 行数正确、`SCI_GETEOLMODE` 正确 |
| 2 GiB+1 字节 | 行号/跳转正确（验证 `TEXT_LARGE` 生效） |
| 非 UTF-8 随机字节 | 提示编码、字节级往返保存一致 |
| 只读文件 | 不尝试写入、状态栏提示 |
| 网络盘/慢盘 | 窗口映射失败时可退化，不崩溃 |
| 修改后 5 分钟 | 不产生 5 GiB 自动备份 |

### 6.3 度量

- 首次可见时间（从 `Open` 到首帧绘制完成）；
- 峰值工作集（`GetProcessMemoryInfo` 的 `PeakWorkingSetSize`）；
- 滚动帧率（拖动滚动条 10 秒的掉帧数）；
- 保存耗时与峰值内存。

---

## 7. 风险与未决问题

1. **字节级往返**：绕过 `Validate` 后，非法 UTF-8 被 Scintilla 原样保存，
   与现有「拒绝非法编码」的策略不同，需要产品确认。
2. **Scintilla 的 2 GiB 以上行为**：fork 已支持，但本项目尚未用 2 GiB+ 文件做过
   回归；需要补一条明确的测试。
3. **`Text()` 调用点**：全项目多处（保存、备份、查找全部、导出、插件）都会整文档取文本，
   大文件模式下必须逐一改造，否则「能打开但不能用」。
4. **staging 文档的内存峰值**：`Load` 期间新旧文档同时存在；大文件模式下建议先关闭
   或释放旧文档（在确认新文档创建成功之后），或直接复用同一窗口。
5. **映射窗口与 32 位进程**：本项目为 x64，暂不需要考虑 2 GB 地址空间；
   但 `MapViewOfFile` 失败仍需按 EmEditor 的策略减半重试。
6. **UTF-16 大文件**：转码会放大内存（UTF-8 结果通常小于 UTF-16，但中间 `std::wstring`
   仍是 2 字节/字符），建议先只读打开并提示。

---

## 附录 A：EmEditor 地址速查

| 地址 | 作用 |
|---|---|
| `0x1408B8B90` | 主打开流程（调用 `sub_140FA7E90`） |
| `0x140FA7E90` | 打开/读取核心，模式判定 |
| `0x140FA7D00` | `CreateFileW` 封装 |
| `0x140FA7A00` | `CreateFileMappingW(PAGE_READONLY)` |
| `0x140FA6CF0` | 只读 `MapViewOfFile`，更新视图字段 |
| `0x140FA7AF0` | 卸载旧视图、按窗口大小重映射、失败减半 |
| `0x140FA6DE0` | 映射不可用时的 `ReadFile` 回退（64 MB 环形缓冲） |
| `0x140F71AE0` | `GlobalMemoryStatusEx`，计算全局内存/映射上限 |
| `0x140F78A80` | 读取 `MemoryFileMB` / `MaxMemoryMB` / `SortMemoryPercent` |
| `0x140F90310` | 映射窗口基准大小（下限 64 MB，受全局上限约束） |
| `0x140931360` | 临时文件 + `PAGE_READWRITE` 映射（上限 16 TiB-1） |
| `0x140976E00` | `CreateFileMappingW(PAGE_READWRITE)` |
| `0x140976D30` | 可写映射窗口计算（64 KB 对齐，单视图 ≤1 GB） |
| `0x140976C70` | `CMyFileMap::MapView`（复用/增长） |
| `0x140435860` | `CVirtualMap`：内存 ≤90% 走 `VirtualAlloc`，否则临时映射 |
| `0x140435D70` | `CVirtualMap` 构造（最小 512 KB，64 KB 取整） |
| `0x1408F0800` | 文件位置 → 行号，44 位偏移行索引 |
| `0x140AECAD0` | 1 GB 分块拷贝/保存，64 MB 暂存 |
| `0x1408BD0C0` | 保存流程 |
| `0x14098AEA0` | `UnmapViewOfFile` 封装 |

Imports（IAT 槽位）：

| API | 槽位 |
|---|---|
| `CreateFileW` | `0x141989500` |
| `ReadFile` | `0x141989508` |
| `SetFilePointerEx` | `0x1419895a8` |
| `GetFileSizeEx` | `0x141989608` |
| `WriteFile` | `0x141989670` |
| `CreateFileMappingW` | `0x141989680` |
| `MapViewOfFile` | `0x141989688` |
| `UnmapViewOfFile` | `0x141989690` |

崩溃报告字符串（可用于确认结构字段语义）：
`MapView`、`CurrViewBegin`、`CurrViewEnd`、`DiskMode`、`BufMode`、`TempFileValid`、
`FileReadValid`、`InsideReading`、`InsideSaving`。

## 附录 B：MultiPad4 相关代码位置

| 位置 | 说明 |
|---|---|
| `src/MainFrame.cpp:78-100` | `MainFrame::Open`，读取整文件后 `Load` |
| `src/MainFrame.cpp:131-146` | `MainFrame::Reload` |
| `src/MainFrame.cpp:111-130` | `MainFrame::SaveTo` |
| `src/FileIO.cpp:21-27` | `Validate`（逐字符） |
| `src/FileIO.cpp:97-145` | `Decode`（复制 + 编码转换） |
| `src/FileIO.cpp:147-155` | `ReadBytes`（整文件读入） |
| `src/FileIO.cpp:159-195` | `Read`（BOM/UTF-16/UTF-8 探测） |
| `src/FileIO.cpp:305-340` | `WriteAtomic`（`Encode` 全量 + 临时文件） |
| `src/EditorDocument.cpp:35-80` | `Load`（staging 文档 + `ADDTEXT`） |
| `src/EditorDocument.cpp:84-89` | `Text()`（整文档 GETTEXT） |
| `src/StyleManager.cpp:555-650` | `ConfigureDocumentStyle`，末尾 `SCI_COLOURISE`（649） |
| `src/FileCommands.cpp:100-105` | 现有「大文件模式」命令 |
| `src/AutoSave.cpp:57-94` | 恢复备份（整文档 `Text()`） |
| `src/BackgroundWork.cpp:23-56` | 自动备份调度 |
| `3rdparty/notepad4/src/Edit.cpp:137-192` | Notepad4 大文件装载模板 |
| `3rdparty/notepad4/src/Edit.h:511` | `MAX_SMALL_FILE_SIZE` |
| `3rdparty/notepad4-scintilla/scintilla/src/CellBuffer.cxx:336-341` | 行索引 32/64 位选择 |
| `3rdparty/notepad4-scintilla/scintilla/src/CellBuffer.cxx:579-581` | fork 去掉 2 GB 限制 |
| `3rdparty/notepad4-scintilla/scintilla/src/SplitVector.h:100-165` | 增长与整体重分配 |
| `3rdparty/notepad4-scintilla/scintilla/src/SplitVector.h:406-428` | `BufferPointer` / `RangePointer`（连续内存假设） |

## 附录 C：参考资料

- EmEditor 官方「Large Files Up to 16 TB」：
  <https://www.emeditor.com/text-editor-features/large-file-support/files-up-to-248gb/>
  （64 位版上限 16 TB；默认在 300 MB 以上使用临时文件后端，可在
  Customize → Advanced 调整）
- EmEditor Fast View 模式说明：
  <https://help.emeditor.com/zh-cn/howto/file/file_fast_view.html>
  （v26.3 引入，不在内存中完整载入文件；默认在 4096 MB 以上自动启用，
  设为 0 可关闭）
- EmEditor v20.5 版本历史（引入 16 TB 文件支持）：
  <https://www.emeditor.org/en/history/v20_5.html>
- Scintilla 文档模型：`3rdparty/notepad4-scintilla/scintilla/src/CellBuffer.cxx`、
  `Document.cxx`、`Partitioning.h`
- Notepad4 源码：`3rdparty/notepad4/src/Edit.cpp`、`src/Notepad4.cpp`
- 项目范围：`docs/PROJECT_SCOPE.md`
