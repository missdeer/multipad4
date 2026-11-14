# 语言配置与主题性能验证

测量日期：2026-11-14。Windows x64，Intel Core i9-12900HK，MSVC 19.44，C++20，Release /O2，SSE4.2，LTO 关闭。

原始数据见 `lexer-configuration-release.csv`。测试构建启用可选逐字符读取跟踪；普通构建默认关闭该跟踪。比较双方使用同一测量构建、相同文本及同一 UI 线程。

## 主题切换的同机对照

`forced-full-baseline` 强制使词法规则版本失效，复现旧配置路径每次切换均重新安装 lexer/关键词并全文着色的行为；它不是另一份旧版本可执行文件。`current` 只切换主题。下表为五次同步配置调用的中位数，重绘单列记录在 CSV 中。

| 输入 | 强制全文基线 ms | 当前 ms | 当前词法调用次数 |
|---|---:|---:|---:|
| comments.cpp | 8.603 | 0.489 | 0 |
| embedded.html | 0.757 | 0.872 | 0 |
| long-lines.cpp | 4.586 | 0.545 | 0 |
| nested.py | 0.394 | 0.413 | 0 |

所有当前主题切换均未调用 lexer/fold、未提交词法样式字节；着色字节及逐行折叠与从头计算的结果一致。小输入的总耗时主要是样式设置与控件工作，数据不支持对所有文件承诺固定加速倍数。

## 其他测量与边界

- 首次打开从临时文件读取、解码并加载文档；输入刚写入，属于温文件系统缓存，未宣称物理磁盘冷启动。覆盖约 512 KiB 长行、1 MiB 长注释、嵌套 Python 插值及 HTML 内嵌 JavaScript/CSS。
- 同一文档执行局部编辑、跨行边界编辑、跳转文件尾及 TOVISIBLE idle styling 的可见区域编辑。最终均与新文档完整着色/折叠比较。
- `lexer_fetched_bytes` 计数 LexAccessor 的字符读取及范围读取，包含重复访问和前瞻，不等于文件大小或唯一字符数。`requested_bytes` 是 Scintilla 请求范围；`style_submitted_bytes` 是样式提交长度（可能包含尾部哨兵）；`backtrack_bytes` 是入口之前的最大观测距离。三者不能互换。
- `lexer_ms` 与 `fold_ms` 分开计时。`layout_redraw_ms` 是同步 RedrawWindow 的耗时；`total_ms` 是调用期间 UI 线程同步占用时间，不代表操作系统输入队列的端到端延迟。样式提交成本包含在引擎调用内。
- 工作集列记录进程绝对工作集，不用于宣称内存下降。语言配置加载单次测得 29.438 ms，原生深色主题加载 3.205 ms，包括解析、校验及快照构造，不包含整个进程启动。
- 本组样本最大词法阶段 4.548 ms，最大观测回溯 22 字节。没有长距离反复回扫的证据，因此本轮未启用状态检查点、后缀复用、线程或单文件分块。
- idle styling 已实测为 UI 线程工作，可见区域编辑数据见 CSV；长行/跨行状态仍可能扩大着色范围，当前保留同步正确性入口，不将 idle styling 宣称为后台执行。
- 已审核 `LexCPP.cxx` 的函数静态 `isObjCSource`，它是并发前必须隔离的共享可变状态。当前实现只在 UI 线程执行原生 lexer，不开展不安全的并行实验。
- SIMD 两阶段扫描、检查点、工作线程、LTO/PGO、二进制缓存均按计划的触发条件保留；本组证据不要求引入这些复杂度。

## 兼容与发布验证

`notepad-theme-compatibility.tsv` 使用参考目录的全部 22 个主题，通过同一原生导入器生成。每个主题都实际应用到 C++、Python、JavaScript 文档，核对数字类别颜色及字体装饰（含 0 重置），其余缺失类别执行内部回退。列出 exact、approximate、fallback、unsupported 的逐主题数量；逐项报告可在应用主题对话框查询，保存后的原生主题保留近似/回退/未支持诊断。参考主题文件及其色彩内容未纳入仓库。

已验证将 EXE 单独复制到新目录、从不同工作目录运行、缺失全部外部 XML 和损坏语言 XML 时，P0 行为基线仍通过内嵌同源资源恢复。正式链接规则不含任何 stl 配置对象。锁定目标主题文件时保存失败，原文件保持有效。

截图在 `../screenshots/`：浅色、深色、导入主题、150% 编辑器缩放与主题对话框。截图核对了声明颜色、独立行/块注释、字体、行号、选区、光标及折叠栏。150% 图是编辑器缩放，真实跨显示器 DPI、输入法与人工 Notepad++ 并排显示检查仍需要相应硬件/交互环境，未计作已完成的验收。

## 复现

```powershell
$env:MULTIPAD4_BUILD_CONFIGURATION='Release'
$env:MULTIPAD4_LEXER_METRICS='ON'
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
Remove-Item Env:MULTIPAD4_BUILD_CONFIGURATION
Remove-Item Env:MULTIPAD4_LEXER_METRICS
$exe = Join-Path $PWD 'cmake-msvc-build/multipad4.exe'
$out = Join-Path $PWD 'tmp/lexer-configuration-release.csv'
$run = Start-Process $exe -ArgumentList '--configuration-benchmark', ('"' + $out + '"') -WindowStyle Hidden -PassThru -Wait
if ($run.ExitCode) { throw 'Benchmark failed' }
```

常规发布重新配置时让 `MULTIPAD4_LEXER_METRICS` 保持默认 OFF；本次最终验证也采用关闭逐字符跟踪的普通 Release 构建。
