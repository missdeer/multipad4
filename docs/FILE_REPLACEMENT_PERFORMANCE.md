# Whole-file replacement worker measurements

Measured 2026-11-25 for issue #28. Both paths run from the same MSVC 19.44 x64 RelWithDebInfo `/O2 /arch:SSE4.2` executable, with the pinned Scintilla implementation. Machine: Intel Core i9-12900HK, Windows, Crucial CT1000P3PSSD8 NVMe SSD. Other development work ran on this machine; these numbers are advisory, not an isolated-machine throughput claim.

## Workload and method

`file_replace_tests baseline sparse`, `worker sparse`, `baseline dense`, and `worker dense` each run in a fresh process. Three alternating pairs per shape follow creation of the same corpus in the local temporary directory, so filesystem cache is warm. Input and output verification and corpus creation are outside the timed region. The timed region includes full-file buffered read, matcher, full output serialization/encoding, temporary save and atomic destination replacement. No plugins, one hidden editor control, UTF-8 without BOM, one long line, no open document tabs.

The sparse corpus is 320 MiB with one `needle` per MiB, replaced with `replacement`: 320 replacements, 335,545,920 output bytes. The dense corpus is 2 MiB of `a`, replaced with `abcdefgh`: 2,097,152 replacements, 16 MiB output. Each path compares destination bytes with the generated output and computes the same FNV-1a output fingerprint. Sparse hash: `18041897950501752835`; dense hash: `1020408336679240579`.

The baseline uses the original sequential editor APIs, including both original-match enumeration and evolving replacement, and retains original text. It does not create the old per-match result vector or render Finder rows, so its memory and UI cost are a lower bound for the old dialog callback. The worker uses the production headless matcher, original-coordinate archive and conditional writer. The harness isolates the file-processing stage: it does not time traversal, dialog field handling, result-row publication, or an actual input-to-paint interaction. The separate dialog regression invokes the real timer path.

## Paired results

Times are milliseconds. With three samples, nearest-rank p95 and p99 both equal the maximum. The callback columns summarize each run's longest synchronous UI callback (baseline) or message-pump slice (worker), rather than an input-latency distribution. UI CPU uses `GetThreadTimes` and has a coarse 15.625 ms resolution. Peak private commit uses `PeakPagefileUsage` and includes process initialization/corpus setup, consistently on both paths.

| Corpus | Path | Completion median | Completion p95/p99 | Longest callback median | Callback p95/p99 | UI CPU median | Peak private MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| sparse | baseline | 7471.5 | 13705.3 | 7471.468 | 13705.238 | 5000.0 | 2149.9 |
| sparse | worker | 4414.9 | 4637.7 | 2.699 | 4.797 | 78.1 | 1072.6 |
| dense | baseline | 10351.4 | 15254.2 | 10351.377 | 15254.141 | 8843.8 | 135.1 |
| dense | worker | 1625.7 | 3078.5 | 0.038 | 6.582 | 15.6 | 37.6 |

The decoded delivered bytes are 335,544,320 (sparse) and 2,097,152 (dense); UTF-8 needs no character-set conversion. Logical save temporary writes equal the output sizes above. The worker additionally spools three dense archives (64 MiB each): original Finder positions, sequential edit spans, and final navigation targets. Sparse offsets remain in bounded archive buffers. The latest paired samples include the navigation correction; the earlier cancellation samples remain valid because canceled jobs stop before the new target mapping. The worker first validates the baseline encoding decision over a read-only mapped source with bounded decoder chunks, then delivers text through ReadChunks using that exact encoding. This preserves malformed-BOM fallback and first/tail modelines without allocating a complete raw string. Stateful UTF-7/ISO-2022 retain the complete decoder for compatibility; cancellation is checked around that decoder. Physical storage I/O and total allocator/decoder copy traffic were not instrumented.

## Cancellation and correctness

The cancellation workloads request cancellation after approximately 10 ms and immediately send a `WM_NULL` probe on the owner thread. Sparse cancel-to-probe: 0.027 ms; background retirement: 19.047 ms. Dense cancel-to-probe: 0.018 ms; retirement: 15.108 ms. Neither destination changed. This measures owner-thread responsiveness, not a painted-frame latency. A running read or matcher may take longer to retire; a single retained job bounds that memory, and another batch waits for retirement. Closing the application drains outstanding work.

Regression checks compare the headless matcher to actual sequential Scintilla for case folding, whole-word boundaries, captures, CRLF, zero-length matches, UTF-8/surrogate boundaries, dot-all, misses, empty input, original result coordinates, and replacement text that changes later matches. Plain literals record original positions in the replacement pass; word-boundary and regex modes retain the separate original-match pass. Open mapped documents retain their existing owner-thread matcher.

Conditional-write checks cover stale identity/version, cancellation, injected precommit failure, hard-link identity preservation, and existing alternate streams. The real dialog test covers closed UTF-16/BOM files, an unsaved open buffer, one undo action, an edit during chunked snapshot capture that must skip the buffer and preserve disk contents, failed commits replaying deferred opens, and canceled retirement preserving another page’s status. Read parity includes malformed UTF-8 BOMs, late invalid bytes, declared encodings, first/tail modelines, auto-detected UTF-16, odd-length UTF-16, UTF-7, and 17 MiB runs of invalid UTF-8 continuation bytes. UTF-8 detection validates borrowed spans directly; cancellation bypasses encoding fallbacks. The pathological-input test has a 60-second timeout. Runtime regex failure rejects the result, and dot-all zero-width CRLF behavior matches the editor. Replacement results retain original Finder display coordinates and separate post-edit UTF-8/native navigation spans. Committed file metadata and applied buffer revisions accompany those spans; immediate navigation after closed UTF-16 and open-buffer growing multiline replacement is covered. Embedded NUL bytes remain supported in open buffers. Owner-thread application replays the prepared sequential ranges as one undo group, preserving bookmarks and other line metadata on untouched lines. Instruction spool reads/ranges/final size are validated before edits; an application failure closes the group and undoes changes. Marker, fold, selection and undo behavior is compared with the sequential C++ editor. Per-file conflicts appear in Finder, and successful commits remain counted when later cleanup fails or a following file is canceled.

Validation: wrapper build succeeded; `file_replace`, `file_replacement_integration`, `thread_pool`, `file_search`, `lazy_document`, and `fileio_roundtrip` passed. An earlier broad run also passed the lexer configuration/data, portability, SIMD and plugin-maintenance checks, but the generated lexer inventory check failed in this worktree; it was not regenerated for this change. Full `editor_integration` also passed after the navigation correction (27.40 s), and the focused dialog suite passed with all three new regressions in its exercised scope (2.28 s). Cold-cache measurements and input-to-paint percentiles were not run for this issue.

## Reproduction

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R '^(file_replace|file_replacement_integration|file_search|fileio_roundtrip|lazy_document|thread_pool)$' --output-on-failure
foreach ($iteration in 1..3) {
    foreach ($shape in @('sparse', 'dense')) {
        foreach ($mode in @('baseline', 'worker')) {
            & .\cmake-msvc-build\file_replace_tests.exe $mode $shape | Out-Host
        }
    }
}
.\cmake-msvc-build\file_replace_tests.exe worker-cancel sparse | Out-Host
.\cmake-msvc-build\file_replace_tests.exe worker-cancel dense | Out-Host
```

## Raw samples

```text
baseline,sparse,elapsed_ms=7471.480,longest_ui_ms=7471.468,ui_cpu_ms=5000.000,peak_working_set=2090205184,peak_private_bytes=2254381056,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
worker,sparse,elapsed_ms=4414.866,longest_ui_ms=2.699,ui_cpu_ms=78.125,peak_working_set=1083797504,peak_private_bytes=1124696064,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
baseline,dense,elapsed_ms=15254.155,longest_ui_ms=15254.141,ui_cpu_ms=8843.750,peak_working_set=141762560,peak_private_bytes=141684736,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
worker,dense,elapsed_ms=3078.542,longest_ui_ms=6.582,ui_cpu_ms=46.875,peak_working_set=47190016,peak_private_bytes=39432192,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
baseline,sparse,elapsed_ms=13705.252,longest_ui_ms=13705.238,ui_cpu_ms=5156.250,peak_working_set=2090192896,peak_private_bytes=2254372864,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
worker,sparse,elapsed_ms=4637.653,longest_ui_ms=4.797,ui_cpu_ms=93.750,peak_working_set=1083826176,peak_private_bytes=1124671488,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
baseline,dense,elapsed_ms=10351.386,longest_ui_ms=10351.377,ui_cpu_ms=9671.875,peak_working_set=141832192,peak_private_bytes=141697024,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
worker,dense,elapsed_ms=1599.462,longest_ui_ms=0.030,ui_cpu_ms=15.625,peak_working_set=47153152,peak_private_bytes=39444480,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
baseline,sparse,elapsed_ms=3689.905,longest_ui_ms=3689.895,ui_cpu_ms=3500.000,peak_working_set=2090225664,peak_private_bytes=2254364672,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
worker,sparse,elapsed_ms=2362.478,longest_ui_ms=0.117,ui_cpu_ms=15.625,peak_working_set=1083817984,peak_private_bytes=1124679680,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
baseline,dense,elapsed_ms=6873.747,longest_ui_ms=6873.736,ui_cpu_ms=6781.250,peak_working_set=141815808,peak_private_bytes=141701120,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
worker,dense,elapsed_ms=1625.666,longest_ui_ms=0.038,ui_cpu_ms=15.625,peak_working_set=47194112,peak_private_bytes=39436288,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
worker-cancel,sparse,elapsed_ms=37.723,longest_ui_ms=0.019,ui_cpu_ms=0.000,peak_working_set=1016041472,peak_private_bytes=1010388992,cancel_ui_ms=0.027,cancel_retire_ms=19.047,count=0,output_bytes=335544320,hash=6953299357734116483
worker-cancel,dense,elapsed_ms=28.897,longest_ui_ms=0.003,ui_cpu_ms=0.000,peak_working_set=16896000,peak_private_bytes=8724480,cancel_ui_ms=0.018,cancel_retire_ms=15.108,count=0,output_bytes=2097152,hash=1781528684171953027
```
