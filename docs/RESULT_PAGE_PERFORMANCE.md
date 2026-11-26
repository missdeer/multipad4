# Result page append/style batching (#42)

2026-11-25, Windows, Core i9-12900HK, MSVC19.44, RelWithDebInfo `/O2`, SSE4.2.
`MULTIPAD4_RESULTS_BENCHMARK=1 ctest -R ordinary_search_storage` runs paired
measurements on the real UI thread and Scintilla controls with plugins disabled.
The same 64-hit page has 32 grouped result rows, one file header and one paging
marker, with two hits per line. The new sample invokes actual `AddOrdinary`;
the baseline reproduces the previous per-row append/colourise then whole-page
colourise algorithm in an equivalent scratch Scintilla. Both use the same row
text/style metadata and current lexer. Baseline row preparation is outside its
timed section; the new path includes archive-to-row preparation. The benchmark
checks actual append, read-only, styling and fold messages with window subclasses.

| 21 paired samples | Previous page algorithm | Batched page |
| --- | ---: | ---: |
| Median callback wall time, ms | 1.322 | 0.909 |
| p95, ms | 2.617 | 1.374 |
| Maximum / p99, ms | 3.330 | 1.985 |
| SCI_APPENDTEXT calls per page | 34 | 1 |
| SCI_SETREADONLY calls | 68 | 2 |
| SCI_SETSTYLING calls | 260 | 130 |
| SCI_SETFOLDLEVEL calls | 68 | 34 |

GetThreadTimes summed deltas across those tiny sample intervals were 62.5/0 ms.
Its resolution is inadequate for reliable CPU attribution at this duration; zero
must not be interpreted as no CPU work or used to estimate a CPU speedup.
Samples run on the same desktop with ordinary system load, with each new page
followed by its baseline counterpart. No worker offload replaces this work.

A separate 4,096-hit single-row check selects hit 4,095 through actual Navigate.
The navigation data-copy microbenchmark compares the previous whole Target copy
and selection with copying identity/path plus only the selected SearchMatch.
Median was 74.7/0.1 microseconds, maximum 199.1/0.2 microseconds; SearchMatch copies
fall from 4,097 objects to one. These times cover target preparation, not complete
navigation/paint latency. The 100-row/200-hit new Add sample took 2.685 ms, with
one append, 401 style calls and 101 fold-level calls.

Rows are staged before publication. A page publishes matching target/style
metadata before one text append. Synchronous SCN_STYLENEEDED requests are deferred
through both text mutation and explicit styling, preventing style/fold updates
from recursively styling the same rows. The lexer retains its complete styled
range after segment highlighting, and header completion preserves the styled
suffix. Fold levels are still assigned to every dirty row, including offscreen
rows. Navigation retains only the selected target data across potential reentry.

Regression checks count real SC_MOD_CHANGESTYLE notifications and verify text,
style and target metadata consistency during notifications. They cover individual
hit selection, grouping, offscreen fold levels, header completion, pending style
requests, full styled range, ordinary paging/load-next and retained search history.
Optimized build and `ordinary_search_storage` passed (1.79 s).

Not measured: input-to-first-paint, peak private bytes, full-navigation timing or
reliable UI CPU deltas. No visual layout/appearance changes were intended.

Final GUI validation passed: `large_file_integration` (14.84 s) and
`editor_integration` (22.79 s), alongside the focused ordinary-search suite.
