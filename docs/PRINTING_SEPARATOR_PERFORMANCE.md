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

## Actual PrintDocument / Microsoft Print to PDF verification

The optional MULTIPAD4_PRINT_DRIVER_TEST=1 workload supplies a native PDF DC and
output path to the existing PrintDocument implementation through a test hook.
The layout, header/footer text, Scintilla body formatting, StartPage/EndPage and
page-range loop all execute unchanged. A 2000-line CRLF document produced 40
pages; the range 2–3 produced two pages beginning at body line 50. Three pairs
were run through the old per-rule pen and the current stock pen. PyMuPDF verified
every page's extracted text and rendered pixels were identical, including
nonempty header/footer regions. Verification is reproducible with
`python tests/VerifyPrintDriverOutput.py cmake-msvc-build/printing-separator-test`.

Actual job medians were 3219.06→3116.12 ms for all 40 pages and
897.769→882.819 ms for pages 2–3. These three local pairs include layout, driver
and spool completion, so the small total-job difference is not an isolated pen
speedup. Pen creation/deletion falls from 80 to zero per full job (four to zero
for the range). See [print-driver-pairs.csv](benchmarks/print-driver-pairs.csv).
The driver workload and pixel verification passed; ordinary CI retains the
printer-independent DIB checks and does not require an installed PDF printer.
