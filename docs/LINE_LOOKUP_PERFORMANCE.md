# Mapped line lookup and previews (#33)

`PositionFromLine` now descends the piece tree and source index by cached
newline counts, then selects directly from the block's sorted newline offsets.
Unknown nodes are visited in order only until the requested break is found.
Summaries are cached after an exhausted subtree has been visited, never by
eagerly describing an unknown left subtree before deciding whether it contains
the requested line. CR/LF boundary state is carried through the descent; the
existing final CRLF boundary normalization remains in place.

Mapped result previews build a native-character-boundary to UTF-8 offset table
while traversing the already decoded preview. Adjacent hits in that same line
and retained window reuse its line bounds, text and offsets. Multiline or
out-of-window hits retain the existing fallback conversion. UTF-16 surrogate
pairs, DBCS and GB18030 four-byte characters are represented at complete character
boundaries. Preview windows can begin earlier for a later hit when the same
retained window already includes it; the native navigation coordinates and
matched bytes remain exact.

## Paired measurements

Windows 11 Pro 10.0.26300, Intel i9-12900HK, MSVC 19.44 RelWithDebInfo `/O2`,
x64 SSE4.2. Both variants run in the same binary with the same corpus and
deterministic query order. Files reside in the local Windows temporary directory;
the OS cache is warm. "Cold" means an empty application line-index cache, while
"warm" means `LineCount` has populated that cache. Five runs per variant. Raw
measurements are checked in under `benchmarks/`.

| 10,000 random line queries, 8,000,000 bytes / 400,000 CRLF lines | Before median | After median | Before source bytes read | After source bytes read |
|---|---:|---:|---:|---:|
| Cold application index | 146.33 ms | 21.60 ms | 8,407,702 | 8,020,000 |
| Warm application index | 147.53 ms | 11.99 ms | 407,702 | 20,000 |

Every result is checked against its exact line-start position; checksums match.
The first cold near-start lookup is separately constrained to one 64 KiB block
plus four boundary bytes. Regression tests also enforce that bound after edits
have split a 66 MiB source into multiple pieces, for UTF-8 and UTF-16LE/BE.

| 20 batches of 64 adjacent previews | Before median | After median | Before source bytes read | After source bytes read |
|---|---:|---:|---:|---:|
| UTF-8 | 3.54 ms | 0.54 ms | 1,766,900 | 25,820 |
| UTF-16LE | 10.37 ms | 0.62 ms | 2,048,840 | 31,160 |
| UTF-16BE | 10.15 ms | 0.65 ms | 2,048,840 | 31,160 |
| GBK | 12.85 ms | 0.75 ms | 629,120 | 12,960 |
| GB18030 | 20.50 ms | 1.08 ms | 1,118,080 | 23,120 |

Preview tests compare all native coordinates and matched UTF-8 bytes against
the original implementation. Samples include Chinese, supplementary characters
where representable, line boundaries and repeated hits. The benchmark uses one
mapped document with 128 repetitions on its first line and an already warm
line index. No plugins or background search are involved in the timed preview
calls. The original preview function is kept as a benchmark-only fixture; the
old line-position algorithm is compiled only into the standalone benchmark.

These are synchronous query/batch timings and mapped-source byte counters,
not an end-to-end input-to-paint benchmark. UI-thread CPU, peak private bytes,
per-query tail latency, compositor latency and copied/decoded byte counts were
not independently instrumented. There is no new temporary-file output in these
operations; benchmark fixture creation and CSV output are outside the timed
regions. Mapped reads and offset-table allocations are included in elapsed time.

## Reproduce and verify

```
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
cmake-msvc-build/line_lookup_benchmark.exe line-lookup.csv
ctest --test-dir cmake-msvc-build -R lazy_document --output-on-failure
$env:MULTIPAD4_PREVIEW_BENCHMARK='1'
ctest --test-dir cmake-msvc-build -R large_file_integration --output-on-failure
Remove-Item Env:MULTIPAD4_PREVIEW_BENCHMARK
```

`lazy_document`, `large_file_integration` and `editor_integration` pass. New
coverage includes CR/LF split between original/journal pieces, mixed endings,
the final empty line, out-of-range lines, cold and warm random jumps, and preview
coordinates in five encodings. Existing random-edit, undo and snapshot tests
remain enabled. IME, dragging and mixed-monitor DPI were not manually checked.
