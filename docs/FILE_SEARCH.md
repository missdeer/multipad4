# 文件/目录搜索实现

实现参考 ripgrep 的职责划分，而非调用 `rg.exe` 或移植 Rust 源码：

- [core/main.rs](https://github.com/BurntSushi/ripgrep/blob/master/crates/core/main.rs)：搜索调度与文件 worker 分离。
- [core/search.rs](https://github.com/BurntSushi/ripgrep/blob/master/crates/core/search.rs)：每个线程拥有并复用 matcher/searcher。
- [ignore/walk.rs](https://github.com/BurntSushi/ripgrep/blob/master/crates/ignore/src/walk.rs)：遍历、过滤、错误和取消独立于输出。

`SearchWalker` 增量深度优先遍历；隐藏项和文件名过滤在读取前完成，目录和文件的 reparse point 均不跟随。文件类型过滤继续使用 Windows `PathMatchSpecW`，兼容原有空格/分号分隔及 `*.*` 行为。不会自动引入 `.gitignore` 或跳过 Git 工作目录文件。

`FileSearch` 最多使用 8 个后台线程，目录枚举通过锁分发，每线程复用一个 `large::Search`，禁用其内部并行以避免嵌套线程。文件通过现有 `ReadChunks` 检测编码、解码，解码后的 NUL 触发二进制跳过，因此正常 UTF-16 文件不会被原始 NUL 字节误判。UTF-8 文本由结果和只读搜索快照共享，不创建临时编辑器或编辑日志。搜索支持跨行正则与零长度匹配。

完成结果通过容量为 8 个文件的队列交给 UI，队列满时 worker 等待，取消同时唤醒等待者。UI 每次定时器约使用 12ms 消费结果（单文件展示可能超过预算），按文件分组展示；文件之间的顺序取决于后台完成顺序。结果 generation 防止已清空或已开始另一轮搜索的结果被旧任务污染。文件读取在验证、解码块之间检查取消，匹配使用现有搜索器的取消检查。

搜索开始时已打开的文件交回 UI 搜索当前编辑缓冲区，保留未保存修改；worker 不持有 EditorDocument 指针或 HWND。磁盘结果通过路径及 UTF-8 偏移导航，打开文件后仍校验源文本。替换继续在 UI 逐文件执行，使用同一遍历器，保留确认、撤销和原子保存规则。

当前限制：每个正在处理或保留结果的文件仍保留完整解码文本及匹配偏移，队列限制的是文件数量而非字节数。打开文档的搜索及单文件结果渲染仍在 UI 线程。该改动没有实现 ripgrep 的完整流式行搜索、ignore 规则、压缩文件搜索或正则引擎替换。

`file_search` 覆盖串行/并行一致性、递归、过滤、隐藏目录、UTF-16、二进制跳过、跨行及零长度正则、CR/LF 行号、打开文档交接、无效正则和满队列取消；`editor_integration` 验证结果跳转及文件替换行为。
