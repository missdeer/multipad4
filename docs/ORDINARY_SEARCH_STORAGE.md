# Ordinary search storage (#25)

Count now resumes a Scintilla cursor without copying document text or retaining
individual matches. Mark retains the existing fixed-size hit archive and optional bookmark
coordinates, then preloads compact spans before atomic publication. It publishes marks and purge only after the
document search completes. Finder keeps the hit archive and one snapshot per
matching line, including any bytes needed to validate multiline matches. It
renders 64 hits at a time using the existing result-page interaction. Loading
later pages does not read the live document; navigation still checks the original
line and matched bytes, allowing unrelated edits as before.

Each cursor batch restores the caller's target range and search flags. It uses
the existing Scintilla matcher, including both regex engines and character-aware
advancement after empty matches. Search All Open captures document identities in
tab order and handles mapped peers asynchronously. An edit, close, cancellation,
or replacement of the results pane discards the unfinished document result;
completed document results remain available and the status reports interruption.

## Reproduction

```
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R ordinary_search_storage --output-on-failure
cmake-msvc-build/multipad4.exe --ordinary-search-benchmark ordinary-search-benchmark.csv
```

The benchmark compares the unchanged `CollectMatches` plus eager `Add` path
against the new actual Count/Finder/Mark dialog commands in the same optimized binary.
Both run on the UI thread, with the same Scintilla document, corpus and search
flags. Each workload has five runs per variant. Workload 0 is a three-byte
selection containing three hits in a 32,000,000-byte single line; workload 1 is a
full-document miss in that document; workload 2 is 1,000,000 hits in a
2,000,000-byte single line. All documents are ordinary UTF-8, one tab, no plugins,
normal case-insensitive literal matching, warm memory. Only archived hit offsets
use temporary storage (the Windows temporary directory on the local disk).

Machine: Windows 11 Pro 10.0.26300, Intel Core i9-12900HK; MSVC 19.44,
RelWithDebInfo `/O2`, x64 SSE4.2. Raw results are in
`benchmarks/ordinary-search-storage.csv`.

`elapsed_ms` measures through completion; `first_correct_paint_ms` includes a
synchronous redraw of the visible frame after the completed document result is
published. It measures application paint completion, not compositor presentation
or physical display latency. `longest_callback_ms` covers the initiating command
and each resumed timer callback. Timer callbacks are explicitly dispatched by
the harness, so elapsed times exclude timer scheduling delays. `ui_cpu_ms` comes
from UI-thread kernel/user times. Private memory peaks are sampled with a requested 1 ms sleep by
the same sampler for both variants, and can miss shorter-lived allocations.
Private deltas can be zero when the allocator reuses committed pages. Process I/O
counts include all process traffic and are not exclusively temporary-file I/O.
Bytes copied/decoded are not instrumented. Five runs do not establish reliable
tail-latency statistics; nearest-rank p95/p99 both select the maximum sample.

## Results

| Workload / operation | Variant | Completion median / p95 / p99 (ms) | Paint median (ms) | UI CPU median (ms) | Longest callback max (ms) | Sampled peak private delta median (MiB) |
|---|---|---:|---:|---:|---:|---:|
| 0 / count | before | 8.47 / 8.81 / 8.81 | 8.68 | 0.00 | 8.81 | 30.61 |
| 0 / count | after | 0.38 / 0.50 / 0.50 | 0.46 | 0.00 | 0.50 | 0.03 |
| 0 / finder | before | 66.99 / 72.71 / 72.71 | 67.19 | 62.50 | 72.71 | 183.50 |
| 0 / finder | after | 61.99 / 66.03 / 66.03 | 62.06 | 62.50 | 61.98 | 183.50 |
| 0 / mark | before | 8.51 / 9.69 / 9.69 | 8.73 | 15.62 | 9.69 | 30.61 |
| 0 / mark | after | 0.48 / 0.78 / 0.78 | 0.60 | 0.00 | 0.77 | 0.03 |
| 1 / count | before | 72.63 / 81.29 / 81.29 | 72.86 | 78.12 | 81.29 | 30.61 |
| 1 / count | after | 64.64 / 66.62 / 66.62 | 64.72 | 62.50 | 66.61 | 0.03 |
| 1 / finder | before | 74.25 / 75.78 / 75.78 | 74.44 | 62.50 | 75.78 | 30.61 |
| 1 / finder | after | 65.55 / 74.22 / 74.22 | 65.63 | 62.50 | 74.21 | 0.03 |
| 1 / mark | before | 72.82 / 75.31 / 75.31 | 73.02 | 78.12 | 75.31 | 30.61 |
| 1 / mark | after | 62.55 / 63.38 / 63.38 | 62.63 | 62.50 | 63.38 | 0.03 |
| 2 / count | before | 1570.04 / 1700.70 / 1700.70 | 1570.35 | 1562.50 | 1700.70 | 282.85 |
| 2 / count | after | 824.00 / 861.48 / 861.48 | 824.15 | 796.88 | 2.13 | 0.03 |
| 2 / finder | before | 2608.52 / 2638.60 / 2638.60 | 2608.88 | 2484.38 | 2638.60 | 525.19 |
| 2 / finder | after | 1364.74 / 1442.54 / 1442.54 | 1364.79 | 1328.12 | 13.19 | 5.77 |
| 2 / mark | before | 7559.16 / 7760.62 / 7760.62 | 7559.58 | 7046.88 | 7760.62 | 282.85 |
| 2 / mark | after | 1288.42 / 1461.97 / 1461.97 | 1288.57 | 1281.25 | 437.11 | 32.54 |

The original baseline has one synchronous callback, so its complete elapsed
measurement is used as the callback duration, including local match-vector
destruction. Thread CPU timing has Windows timer granularity, so very short
runs can report zero CPU time. The raw CSV includes per-run paint latencies;
paint p95/p99 also equal the maximum of the five per-variant rows.

Dense Finder writes 32,000,000 bytes of archived offsets and reads 2,048 bytes
for its initial page. Dense Mark writes and subsequently reads 32,000,000 bytes.
The baseline does not spool matches. Other measured operation variants report
zero process read/write transfer bytes in this run. Temporary-file I/O is
reported through these process counters; there is no separate file-only tracer.

The dense Count trades a small total-time cost for sub-2-ms batches and bounded
match storage. Dense Finder substantially reduces callback duration and memory.
Dense Mark aggregates decoration-change notifications in the generated Scintilla
core, avoiding a million repaint/plugin notifications. Compact spans are loaded
before purge, then the complete result is published with one aggregate
notification. The median total falls from 7.56 s to 1.29 s; the worst final
publication callback is 437 ms, down from the previous unbatched 6.1 s. Final
publication remains synchronous to preserve atomic mark/purge semantics. The tiny-selection Finder case has no
material improvement because its matching line is itself 32 MB; preserving that
line's display and navigation costs dominates both paths.

## Correctness and limits

The dedicated regression test covers nested notification batching, aggregate
changed extents, restored notifications, direct dialog destruction, Count
independence from result history, both regex modes, empty matches, UTF-8,
target restoration, selection boundaries, navigation after unrelated edits,
later-page navigation, and cancellation/edit interruption without purging marks.
The editor, file-I/O, lazy-document and file-search suites also pass. The
large-file suite passed before batching; its first post-batch run hit the
timing-sensitive async replacement assertion at MappedSearchTests.h:91. An
immediate isolated rerun passed (11.68 s).

Scintilla search calls are indivisible. In particular, a whole-document miss or
an expensive regular expression can exceed the 4 ms inter-hit batch budget.
Preserving the existing regex semantics takes precedence over dividing a target
into smaller searches. Final Mark publication is also one synchronous operation
so a cancelled search cannot leave half-purged markings. Full matching lines are
retained and displayed without truncation: a three-byte Finder selection in a
32 MB line still needs that line. The benchmark exposes this cost rather than
claiming all searches have bounded callback duration.

IME, dragging and mixed-monitor DPI were not manually exercised; the change
reuses the existing results editor and page controls.
