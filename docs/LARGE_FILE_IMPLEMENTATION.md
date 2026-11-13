# 大文件打开与编辑实现

默认 ≥300 MiB 文件使用**按需可编辑文档**：只读文件映射、持久片段树、追加编辑日志和惰性分块行索引。Scintilla 负责局部窗口绘制与输入，完整文本不灌入连续 gap buffer。

文件头、中部和尾部均可直接编辑、全局撤销/重做、保存副本或原路径保存，无需点击按钮等待全文载入。全文件普通/正则查找、替换、计数、Find All、标记及行起点跳转直接作用于片段树；单文档耗时操作在后台按需执行，可取消。需要完整 Scintilla 语义的高级命令通过 `Full editor...` 明确切换。

当前实现、性能数据、原始逐轮记录、复现命令和兼容边界见 [按需可编辑文档](MAPPED_EDITING.md)。

此前的只读 Fast View 首屏验证是历史阶段；读取优化已保留，直接填充、接管连续内存、COW gap buffer 和扩大默认行表预留的未采用原型代码已移除。旧实验结果保留用于溯源，不再作为当前默认路径的说明：

- [全文读取与并行校验](LARGE_FILE_READ_PERFORMANCE.md)
- [行表预留实验](SCINTILLA_INSERT_PERFORMANCE.md)
- [文本复制成本细分](SCINTILLA_TEXT_COPY_BREAKDOWN.md)
- [省复制的原型验证](SCINTILLA_BULK_LOAD_EXPERIMENTS.md)
- [初始设计参考](LARGE_FILE_OPENING.md)
