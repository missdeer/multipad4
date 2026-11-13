# MultiPad4 SIMD 迁移

基线：本地 `tmp/notepad4/src`，共享 SIMD 辅助头仍使用
`3rdparty/notepad4-scintilla/scintilla/include`。本次不修改第三方源码。

| 上游实现 | MultiPad4 实现及实际入口 |
|---|---|
| EditDetectEOLMode | simd::AnalyzeLineEndings；EditorDocument::Load、混合换行警告 |
| EditDetectIndentation | simd::DetectIndentation；DetectDocumentIndentation |
| DetectUTF16LatinExt | simd::DetectUtf16；files::Read |
| IsUTF8 / z_validate | simd::IsValidUtf8；files::Decode、保存前校验、重载检测 |
| CheckUTF7 高位扫描 | simd::IsAscii；打开与重载时 ASCII 编码策略 |
| BitmapMergeAlpha | simd::BitmapMergeAlpha；保留可复用接口，上游本身没有调用点，测试覆盖 |
| BitmapAlphaBlend | simd::BitmapAlphaBlend；工具栏禁用图像生成 |
| VerifyContrast | simd::VerifyContrast；光标颜色与背景对比 |
| HTML 标签 _mm_packus_epi16 | simd::NarrowAsciiTag；插入标签对话框的 void 元素判定 |
| stlCPP.cpp 中 SIMD 指令关键词 | 原 lexer 配置已参与编译，无需复制 |
| Scintilla 内部 SIMD | 继续直接编译原优化版 Scintilla，包括行索引等实现 |

上游 `#if 0` 中未启用的替代算法（例如 DetectUTF16Latin1）不作为新的
行为启用。UTF-8 校验使用上游有效的 AVX2 或 SSE 路径；AVX512 配置下，
UTF-8 校验和部分图形操作仍按上游设计复用较窄向量。

加载现在按 CRLF/CR/LF 的最大计数选择换行模式；并列时优先保留当前模式，
再按上游顺序选择。加载前调用 SCI_ALLOCATELINES，按实际总行数预分配。
缩进检测使用上游最多 1 MiB 的扫描及 1..8 空格/Tab 规则。混合换行警告
与缩进检测直接读取 Scintilla 字符缓冲，避免额外复制整个文档。

安全适配包括 UTF-8 下一向量的读取边界、AVX512 换行扫描尾部局部缓冲、
缩进检测末尾空格的边界、UTF-16/ASCII 扫描的局部填充，以及非整向量像素数。
确定性 UTF-16 零字节检测也改为 SIMD，并保持原有跳过统计检测选项的语义。

构建指令集由 MULTIPAD4_SIMD 选择 SSE4.2、AVX2 或 AVX512，是编译期
最低 CPU 要求，不是运行时自动分派。

`simd_boundaries` 使用两端不可访问的保护页、随机输入、CRLF 跨块、各种
UTF-8 截断/非法序列及 16 MiB 分块边界，对比标量换行统计、Windows 严格
UTF-8 验证和像素混合结果。编辑器集成检查多数换行、并列偏好与行数。

验证记录：SSE4.2、AVX2 构建及全部 5 项 CTest 通过；AVX512 完整编译通过，
本机 IsProcessorFeaturePresent(PF_AVX512F_INSTRUCTIONS_AVAILABLE) 为 false，
因此不运行 AVX512 二进制。最终构建恢复 SSE4.2。图形检查截图由应用
`--qa-render` 导出至 `tmp/simd-qa/toolbar.png`。
