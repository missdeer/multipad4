# 语言配置迁移基线（P0）

本阶段建立 `PLAN.md` 的迁移输入与可复现验收基线。当前运行时已切换为 XML；
静态目录保留在 `tools/StaticLexerCatalog.h`，只供独立基线导出器使用。
后续实施和使用说明见 `LANGUAGE_CONFIGURATION.md`。

## 文件与覆盖

- `tests/baselines/lexer-catalog.xml`：从实际编译的 90 个 `EDITLEXER` 导出，
  涵盖全部 83 个 `stl*.cpp`、32 个结构字段、1,403 条样式和 572 个关键词槽位。
  包括全局样式、第二文本方案、关键词的顺序/属性、补全片段和默认空值。
  C++ Doxygen 附加词表与 XSD、XSLT、Apache、Property List 补全词表也按实际对象导出。
- `tools/LexerBaselineIds.h`：冻结的语言与样式组身份。语言使用 `cpp`、`python` 等 ID，
  样式组使用 `cpp.keyword` 等 ID；不以显示名称或目录索引生成身份。
  对象指针仅用于导出器检查身份与目录位置一致，不写入 XML。
- XML 同时记录旧目录索引、语言资源 ID、样式索引、资源 ID、显示名和分组样式原值，
  为会话、默认方案、收藏、INI 按名称覆盖及持久样式覆盖提供迁移关系。
  `packed` 保留完整 `MULTI_STYLE` 位值。全局样式还可能表示元素、指示器、
  标记或 SCI 操作，后续绑定层必须区分，不能统一解包成文本样式。
- `docs/lexer-baseline-inventory.json`：完整源文件和 SHA-256、目录迁移关系、
  应用消费者及行号、应用侧检测/别名/变体/会话/补全绑定的位置，
  以及默认和 11 个本地化资源文件的语言显示项。
- `tests/baselines/lexer-behavior.txt`：13 组真实 Scintilla 文档的逐字节样式、
  每行完整折叠值、检测资源 ID、引擎 ID、方言命令和八组补全缓存位图。
  覆盖 C++、Python、JavaScript、TSX、HTML 嵌入语言、SCSS、JSON、XML、
  Bash、Markdown、Matlab、shebang 检测、中文 UTF-8 与 CRLF 文本。

XML 是无损的开发基线格式，不是正式运行时语言/主题 schema。引擎数值不构成
公开主题契约。逐词法类别的语义绑定、规则版本及旧设置迁移执行仍属于后续阶段。
行为基线覆盖代表性输入，不声称穷尽每个原生 lexer 的语法或增量编辑边界。

## 构建、验证与更新

按仓库约定使用 MSVC wrappers：

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build --output-on-failure
python tools/inventory_lexer_baseline.py --check
```

`BUILD_TESTING` 启用时构建独立的 `lexer_baseline_export` 开发工具。
它与应用编译同一批未修改的上游配置源文件，未被正式应用调用或链接。
正常构建和 CTest 只比较基线，不重新生成或覆盖 XML。

只有明确升级上游或审查过行为变化时才执行更新：

```powershell
.\cmake-msvc-build\lexer_baseline_export.exe --export tests/baselines/lexer-catalog.xml
$baseline = Join-Path $PWD 'tests/baselines/lexer-behavior.txt'
$run = Start-Process .\cmake-msvc-build\multipad4.exe -ArgumentList '--export-lexer-baseline', ('"' + $baseline + '"') -WindowStyle Hidden -PassThru -Wait
if ($run.ExitCode) { throw 'Behavior export failed' }
python tools/inventory_lexer_baseline.py
```

上例从当前仓库根目录解析输出路径；程序没有硬编码个人路径。
运行时支持 Unicode 文件路径。导出固定 UTF-8、LF 换行、十进制数值和目录顺序，
不包含时间戳、内存地址、个人环境或当前工作目录信息。
所有文本转义 XML 元字符与换行/制表符，区分 null 与空字符串。
未知样式资源、重复身份、错误名称长度、关键词属性宽度溢出及运行时缓冲区
已修改都会拒绝导出。清单校验还会在上游结构增加字段而导出器尚未覆盖时失败。

`--check-lexer-baseline FILE` 通过真实 WTL/Scintilla 窗口执行比较，使用临时设置，
禁用插件并跳过单实例复用，不读写用户持久首选项。测试交替配置两个文档，
验证样式/折叠/补全不串扰，并验证文档样式覆盖和后台编辑不会影响另一文档。
现有 smoke tests 继续负责会话恢复、INI、补全交互及设置取消行为。

## 来源和后续验收

导出 XML 中的语言、样式、词表和片段来自 notepad4 固定提交
`3fcfb70fd0d3c8802100c85f7293ccb326aa8b64`，补全适配代码来自同一上游。
遵循 `3rdparty/notepad4/License.txt`，与 `THIRD_PARTY.md` 一起保留和分发。
第三方源文件未修改。

P1 应用配置模型、P2 XML 事务加载及默认回退、P3 主题映射和交互已接入。
每次升级继续使用本基线逐字段与逐字节比较。
P4 性能试验单独验收，不改变此处的行为承诺。
