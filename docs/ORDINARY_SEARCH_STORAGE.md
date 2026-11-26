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
cmake-msvc-build/multipad4-tests.exe --ordinary-search-benchmark ordinary-search-benchmark.csv
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
and each resumed timer callback. The harness pumps the real Windows message queue. Ordinary batches post
continuations, yielding via the mapped-work polling timer only when input or
paint is pending. A per-task token rejects stale posted messages after cancellation
or reopening. Elapsed times include actual scheduling and paint delays; no
synthetic timer messages drive the benchmark. `ui_cpu_ms` comes
from UI-thread kernel/user times. Private memory peaks are sampled with a requested 1 ms sleep by
the same sampler for both variants, and can miss shorter-lived allocations.
Private deltas can be zero when the allocator reuses committed pages. Process I/O
counts include all process traffic and are not exclusively temporary-file I/O.
Bytes copied/decoded are not instrumented. Five runs do not establish reliable
tail-latency statistics; nearest-rank p95/p99 both select the maximum sample.

## Results

| Workload / operation | Variant | Completion median / p95 / p99 (ms) | Paint median (ms) | UI CPU median (ms) | Longest callback max (ms) | Sampled peak private delta median (MiB) |
|---|---|---:|---:|---:|---:|---:|
| 0 / count | before | 8.72 / 10.16 / 10.16 | 8.94 | 15.62 | 10.16 | 30.62 |
| 0 / count | after | 0.29 / 0.55 / 0.55 | 0.41 | 0.00 | 0.54 | 0.03 |
| 0 / finder | before | 62.51 / 63.84 / 63.84 | 62.71 | 62.50 | 63.84 | 183.50 |
| 0 / finder | after | 81.02 / 344.63 / 344.63 | 81.14 | 62.50 | 196.62 | 183.51 |
| 0 / mark | before | 9.51 / 10.65 / 10.65 | 9.74 | 15.62 | 10.65 | 30.61 |
| 0 / mark | after | 0.29 / 0.39 / 0.39 | 0.37 | 0.00 | 0.38 | 0.03 |
| 1 / count | before | 72.29 / 73.25 / 73.25 | 72.51 | 62.50 | 73.25 | 30.61 |
| 1 / count | after | 62.55 / 65.61 / 65.61 | 62.64 | 62.50 | 65.60 | 0.03 |
| 1 / finder | before | 70.02 / 72.94 / 72.94 | 70.21 | 78.12 | 72.94 | 30.61 |
| 1 / finder | after | 62.65 / 63.11 / 63.11 | 62.73 | 62.50 | 63.10 | 0.03 |
| 1 / mark | before | 70.99 / 73.47 / 73.47 | 71.21 | 62.50 | 73.47 | 30.61 |
| 1 / mark | after | 62.62 / 65.82 / 65.82 | 62.70 | 62.50 | 65.81 | 0.03 |
| 2 / count | before | 765.79 / 1556.94 / 1556.94 | 766.06 | 750.00 | 1556.94 | 282.84 |
| 2 / count | after | 432.15 / 454.37 / 454.37 | 432.24 | 406.25 | 16.40 | 0.03 |
| 2 / finder | before | 1222.07 / 2004.47 / 2004.47 | 1222.34 | 1218.75 | 2004.47 | 525.19 |
| 2 / finder | after | 676.20 / 697.25 / 697.25 | 676.28 | 640.62 | 11.27 | 3.86 |
| 2 / mark | before | 10229.20 / 17573.30 / 17573.30 | 10229.60 | 8843.75 | 17573.30 | 282.84 |
| 2 / mark | after | 651.51 / 660.38 / 660.38 | 651.60 | 609.38 | 202.12 | 31.54 |

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
notification. Final publication remains synchronous to preserve atomic mark/purge
semantics. The raw data retains desktop/load variation, including two baseline
dense-Mark outliers above 13 seconds. The queued-input regression dispatches a
caret movement and services an invalidated editor paint before search completion.
The tiny-selection Finder case has no
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
