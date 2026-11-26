# Printing separator pens (#61)

Header/footer rules use GDI's stock BLACK_PEN, which matches the previous solid
black width-one pen. Each rule restores the prior pen immediately, before body
formatting. GDI owns the stock object and it is never deleted. Page layout,
page-range filtering, text rendering and driver calls are unchanged.

The printing_separator CTest compares the old drawing sequence with the actual
new helper on a native 600×800 32-bit DIB, checking exact pixel equality at two
mapping scales. It selects a red dashed pen first and verifies restoration after
every workload. GDI object counts remain unchanged after seven alternating pairs
of 10,000 simulated pages with both rules.

Windows x64, MSVC 14.44 RelWithDebInfo (/O2), SSE4.2, warm memory/GDI caches, one
process. Paired elapsed times include drawing and GdiFlush; no printer driver or
page layout is included. Raw samples:
[printing-separator-pairs.csv](benchmarks/printing-separator-pairs.csv).

| 10,000 pages × two rules | Median ms | Maximum ms | CreatePen / DeleteObject calls |
|---|---:|---:|---:|
| Original per-rule pen | 59.3472 | 64.1915 | 20,000 / 20,000 |
| Stock black pen | 12.8030 | 19.6120 | 0 / 0 |

The current path has no per-rule allocation; selection, line drawing and restore
remain. Seven samples do not establish p95/p99 guarantees. UI CPU and peak private
bytes were not instrumented. Native print-to-file driver, complete long print
jobs and page-range interaction were not exercised; these results establish
separator output/resource equivalence and local drawing cost, not end-to-end
printing speed. Physical IME and mixed-DPI interactions were not exercised.
