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
| sparse | baseline | 3273.6 | 3330.8 | 3273.566 | 3330.744 | 3109.4 | 2150.0 |
| sparse | worker | 2336.5 | 2350.8 | 0.800 | 7.968 | 15.6 | 1072.6 |
| dense | baseline | 6352.7 | 6550.0 | 6352.664 | 6549.949 | 6203.1 | 135.2 |
| dense | worker | 718.0 | 739.0 | 0.011 | 0.048 | 15.6 | 36.4 |

The decoded delivered bytes are 335,544,320 (sparse) and 2,097,152 (dense); UTF-8 needs no character-set conversion. Logical save temporary writes equal the output sizes above. The worker additionally spools 64 MiB of dense hit records (32 bytes × 2,097,152); sparse offsets remain in the bounded archive buffer. The worker first validates the baseline encoding decision over a read-only mapped source with bounded decoder chunks, then delivers text through ReadChunks using that exact encoding. This preserves malformed-BOM fallback and first/tail modelines without allocating a complete raw string. Stateful UTF-7/ISO-2022 retain the complete decoder for compatibility; cancellation is checked around that decoder. Physical storage I/O and total allocator/decoder copy traffic were not instrumented.

## Cancellation and correctness

The cancellation workloads request cancellation after approximately 10 ms and immediately send a `WM_NULL` probe on the owner thread. Sparse cancel-to-probe: 0.027 ms; background retirement: 19.047 ms. Dense cancel-to-probe: 0.018 ms; retirement: 15.108 ms. Neither destination changed. This measures owner-thread responsiveness, not a painted-frame latency. A running read or matcher may take longer to retire; a single retained job bounds that memory, and another batch waits for retirement. Closing the application drains outstanding work.

Regression checks compare the headless matcher to actual sequential Scintilla for case folding, whole-word boundaries, captures, CRLF, zero-length matches, UTF-8/surrogate boundaries, dot-all, misses, empty input, original result coordinates, and replacement text that changes later matches. Plain literals record original positions in the replacement pass; word-boundary and regex modes retain the separate original-match pass. Open mapped documents retain their existing owner-thread matcher.

Conditional-write checks cover stale identity/version, cancellation, injected precommit failure, hard-link identity preservation, and existing alternate streams. The real dialog test covers closed UTF-16/BOM files, an unsaved open buffer, one undo action, an edit during chunked snapshot capture that must skip the buffer and preserve disk contents, failed commits replaying deferred opens, and canceled retirement preserving another page’s status. Read parity includes malformed UTF-8 BOMs, late invalid bytes, declared encodings, first/tail modelines, auto-detected UTF-16, odd-length UTF-16, UTF-7, and 17 MiB runs of invalid UTF-8 continuation bytes. UTF-8 detection validates borrowed spans directly; cancellation bypasses encoding fallbacks. The pathological-input test has a 60-second timeout. Runtime regex failure rejects the result, and dot-all zero-width CRLF behavior matches the editor. Per-file conflicts appear in Finder, and successful commits remain counted when later cleanup fails or a following file is canceled.

Validation: wrapper build succeeded; `file_replace`, `file_replacement_integration`, `thread_pool`, `file_search`, `lazy_document`, and `fileio_roundtrip` passed. An earlier broad run also passed the lexer configuration/data, portability, SIMD and plugin-maintenance checks, but the generated lexer inventory check failed in this worktree; it was not regenerated for this change. Full `editor_integration` also passed (29.66 s), and the focused dialog suite passed with all three new regressions in its exercised scope (2.28 s). Cold-cache measurements and input-to-paint percentiles were not run for this issue.

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
baseline,sparse,elapsed_ms=3330.768,longest_ui_ms=3330.744,ui_cpu_ms=3109.375,peak_working_set=2090131456,peak_private_bytes=2254348288,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
worker,sparse,elapsed_ms=2350.801,longest_ui_ms=0.145,ui_cpu_ms=15.625,peak_working_set=1083691008,peak_private_bytes=1124663296,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
baseline,dense,elapsed_ms=6549.959,longest_ui_ms=6549.949,ui_cpu_ms=6390.625,peak_working_set=141025280,peak_private_bytes=141058048,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
worker,dense,elapsed_ms=738.984,longest_ui_ms=0.009,ui_cpu_ms=0.000,peak_working_set=46166016,peak_private_bytes=38182912,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
baseline,sparse,elapsed_ms=3273.576,longest_ui_ms=3273.566,ui_cpu_ms=3109.375,peak_working_set=2090188800,peak_private_bytes=2254356480,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
worker,sparse,elapsed_ms=2336.467,longest_ui_ms=0.800,ui_cpu_ms=31.250,peak_working_set=1083686912,peak_private_bytes=1124663296,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
baseline,dense,elapsed_ms=6352.674,longest_ui_ms=6352.664,ui_cpu_ms=6203.125,peak_working_set=141717504,peak_private_bytes=141717504,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
worker,dense,elapsed_ms=717.991,longest_ui_ms=0.011,ui_cpu_ms=15.625,peak_working_set=46063616,peak_private_bytes=38047744,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
baseline,sparse,elapsed_ms=3199.380,longest_ui_ms=3199.370,ui_cpu_ms=2953.125,peak_working_set=2090164224,peak_private_bytes=2254405632,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
worker,sparse,elapsed_ms=2254.145,longest_ui_ms=7.968,ui_cpu_ms=15.625,peak_working_set=1083666432,peak_private_bytes=1124655104,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=320,output_bytes=335545920,hash=18041897950501752835
baseline,dense,elapsed_ms=6241.231,longest_ui_ms=6241.221,ui_cpu_ms=6093.750,peak_working_set=141688832,peak_private_bytes=141721600,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
worker,dense,elapsed_ms=709.294,longest_ui_ms=0.048,ui_cpu_ms=15.625,peak_working_set=46088192,peak_private_bytes=38055936,cancel_ui_ms=0.000,cancel_retire_ms=0.000,count=2097152,output_bytes=16777216,hash=1020408336679240579
worker-cancel,sparse,elapsed_ms=37.723,longest_ui_ms=0.019,ui_cpu_ms=0.000,peak_working_set=1016041472,peak_private_bytes=1010388992,cancel_ui_ms=0.027,cancel_retire_ms=19.047,count=0,output_bytes=335544320,hash=6953299357734116483
worker-cancel,dense,elapsed_ms=28.897,longest_ui_ms=0.003,ui_cpu_ms=0.000,peak_working_set=16896000,peak_private_bytes=8724480,cancel_ui_ms=0.018,cancel_retire_ms=15.108,count=0,output_bytes=2097152,hash=1781528684171953027
```
