# Status statistics (#30)

Full-line character/column scanning now keeps character-aligned 16 KiB
checkpoints independently of caret movement. Each checkpoint includes cumulative
characters and the tab-expanded column. An indexed caret lookup scans at most
one checkpoint interval; a cold jump waits for the advancing line frontier.
Document pointer, identity, edit revision, line and tab width invalidate that
cache. Line progress and pending selection work alternate within one 2 ms slice.

Selections have a separate generation and enumeration/scan cursor. Completed
selections are consumed once, rather than re-enumerated on every update. Their
totals are published together after the full generation completes. Pending bytes
and line totals display `...`, including the localized status format. Rectangle
and overlapping-range summation retain the existing Scintilla range semantics.
The generated Scintilla core exposes a private selection-generation query,
updated by the central selection notification path plus the raw selection and
rectangle setters that bypass that path. Pinned upstream files remain unchanged.

## Reproduce

```
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R status_statistics --output-on-failure
cmake-msvc-build/multipad4.exe --status-statistics-benchmark status-statistics.csv
```

`tests/StatusStatisticsBaseline.h` preserves the original implementation from
e876e48 for a paired comparison in the same optimized binary. The benchmark
uses MSVC 19.44 RelWithDebInfo `/O2`, x64 SSE4.2, Windows 11 Pro 10.0.26300 on
an Intel i9-12900HK. One ordinary UTF-8 tab, no plugins, warm memory; text is
ASCII with a tab every 32 bytes. No disk document loading is timed. Three runs
per variant are recorded in `benchmarks/status-statistics.csv`.

| Workload | Before median completion | After median completion | Before callback p99 range | After callback p99 range |
|---|---:|---:|---:|---:|
| 100 MiB line, caret at end | 137.08 ms | 67.88 ms | 2.05–2.06 ms | 2.01–2.03 ms |
| 20,000 carets / one-byte selections in 100,000 bytes | 54.59 ms | 23.54 ms | 19.23–21.59 ms | 2.00–2.38 ms |
| 100 MiB line, caret moved before every slice | Did not converge in 200 slices | 67.06 ms | 2.09–2.16 ms | 2.01–2.29 ms |

The final exact 100 MiB line values are 104,857,600 characters and 114,688,000
tab columns. Many-caret totals are 19,999 selected characters (the first of
20,000 selections is an empty caret), matching the baseline exactly. Every
completed benchmark run asserts the line characters, columns and selected total.
The moving-caret baseline completes less than 5 MiB of line scanning before
being reset again; its CSV completion/paint values are `-1` rather than a
reported successful result.

Raw rows include callback median/p95/p99/max, elapsed completion, callback count,
UI-thread CPU from `GetThreadTimes`, sampled peak private-memory delta, first
correct status-label paint and process I/O counters. Percentiles use nearest
rank across that run's callbacks; many-caret runs have few callbacks, so p99 is
the maximum, not a statistically strong tail estimate. UI CPU granularity is
coarse. The sample thread requests 1 ms sleeps and can miss transient memory
peaks; warmed allocation reuse explains smaller later private deltas. The new
100 MiB checkpoint table uses about 154 KiB of logical entries, with vector
capacity and temporary UI allocations reflected in the raw sampled deltas.

The benchmark calls the actual statistics updater directly, then synchronously
redraws a status label containing its exact completed values. This measures
computation and application paint time, excluding the production status timer's
30 ms scheduling intervals and compositor presentation. It does not measure
end-to-end keyboard latency. No search matcher, background worker, decoding or
temporary-file I/O is involved; process I/O counters are zero in these runs.
Bytes copied are not separately instrumented. A 2 ms deadline bounds the work
between chunks, not OS preemption or an individual Win32/Scintilla call.

## Verification

`status_statistics` passes and checks UTF-8 checkpoint boundaries, tab-width
changes, overlap and rectangular totals, secondary-selection changes without a
main caret change, raw selection setters, edits and continued full-line progress
while the caret moves. `editor_integration` passes (25.7 s).

IME, dragging and mixed-monitor DPI were not manually exercised. No visual
layout changed; pending selection totals now use the existing ellipsis display.
