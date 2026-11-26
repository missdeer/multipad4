# Theme enumeration and keyword overlays (#58)

The theme combo now reads only XML root metadata through the same XmlLite reader
and 8 MiB input limit as full parsing. DTD processing remains prohibited and the
resolver remains disabled. Root parsing preserves the attribute-count limit.
Labels are reread on each opening, so updates need no cache invalidation and
duplicate labels retain separate file rows. This is metadata inspection, not
theme validation: malformed bodies can have a label, but selecting a theme still
calls unchanged `LoadTheme` before preview/publication. Cancel restores the active
document; peer document themes remain isolated.

Keyword application uses `lower_bound({languageId, 0})` and visits only that
language's ordered range. Slot order and the existing XOR/multiply hash sequence
remain unchanged. The implementation retains the original `Seal`, keyword
attribute changes, and dialect-pointer repair.

## Actual dialog measurement

Run `multipad4-tests.exe --theme-enumeration-benchmark` and wait for process exit.
The test-only baseline switches to the former full-tree label parse and full-map
keyword walk within the same optimized binary. The workload opens and cancels the
real modal theme dialog through its actual command, reads its combo rows and
compares every old/new label in order. Seven pairs (14 records) passed.

2026-11-26, Windows x64, MSVC 19.44 `/O2` RelWithDebInfo, SSE4.2, Intel i9-12900HK,
CT1000P3PSSD8 NVMe, warm cache. Two ordinary UTF-8 editor tabs, plugins disabled.
128 added theme files each contain 2,000 optional XML children under one named
root, alongside three small test themes and bundled themes. The measured combo
has 156 rows including Default. Keyword comparison uses all 90 catalog language
IDs with slots 0/2/7 and both append/replace values. The entire resulting keyword
arrays, keyword attributes, language rule hashes and configuration rule hash are
checked for exact equality.

Raw results: [THEME_ENUMERATION_PERFORMANCE_SAMPLES.csv](THEME_ENUMERATION_PERFORMANCE_SAMPLES.csv).
Times are milliseconds. Seven samples make nearest-rank p95/p99 equal the maximum.
`dialog_ms` includes the real command, dialog construction/enumeration, timer
automation and cancellation. `enumeration_ms` isolates directory iteration and
metadata label reads. It does not include font enumeration, preview creation or
driver/display work. Input-to-first-paint was not separately measured. UI CPU uses
Windows thread-time resolution. Peak private bytes are sampled every 1 ms.

| Metric | Before median / p95 / p99 / max | After median / p95 / p99 / max |
|---|---:|---:|
| Dialog open/cancel | 458.466 / 493.532 / 493.532 / 493.532 | 193.186 / 228.938 / 228.938 / 228.938 |
| Enumeration | 321.855 / 350.655 / 350.655 / 350.655 | 55.836 / 86.358 / 86.358 / 86.358 |
| UI CPU | 437.500 / 468.750 / 468.750 / 468.750 | 171.875 / 234.375 / 234.375 / 234.375 |
| Keyword overlay | 0.916 / 1.190 / 1.190 / 1.190 | 0.969 / 1.197 / 1.197 / 1.197 |

Maximum sampled dialog private-byte increase fell from 2,220,032 to 389,120.
Keyword-map entry visits fell from 24,300 to 359 (including range-end probes),
but keyword latency did not improve in this small run: allocation/copy/sealing
dominates. The range traversal removes redundant work; no measured keyword
speedup is claimed. File reading remains the existing bounded full-file input
read, with no added cache, decode pass or temporary-file writes. Only parsing
stops at root metadata, so cold storage I/O can still dominate large collections.

## Validation

Wrapper build linked successfully. `theme_enumeration` passed (1.42 s): updated
files, duplicate labels, DTD rejection, malformed selected-body rejection,
preview/cancel and peer isolation, and exact keyword/hash equivalence. The actual
dialog benchmark exited 0 with all 14 samples. `configuration_integration`
passed (4.71 s) and complete `editor_integration` passed (41.96 s). Source changes
required regenerating the inventory fingerprints; the inventory check then passed
(0.31 s). No numerical assertion was weakened.

No physical IME, dragging or mixed-DPI interaction was exercised. Labels/layout
are unchanged for valid themes; no new screenshots are needed for a layout change.
Metadata alone is intentionally insufficient to accept a theme. Full validation
on selection remains mandatory, and the tests do not claim malformed XML became
valid merely because its root name can be displayed.
