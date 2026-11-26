# Save-time trailing blank removal (#53)

`PrepareFileSave` now scans borrowed 64 KiB Scintilla ranges and records only
trailing space/tab runs. A run survives range boundaries; CR/LF are recognized
independently, including split CRLF. All ranges expire before reverse deletions.
The final unterminated line is included. Scintilla's boundaries remain authoritative
when Unicode line endings are active. Deletions and subsequent EOL conversion
remain one undo action; read-only documents remain unchanged.

## Reproduction and correctness

Build using `bash -lc "cmake-build cmake-msvc-build"`. Run CTest's
`save_whitespace` or `multipad4-tests.exe --save-whitespace-benchmark` (wait for
the GUI process to exit). The latter writes `save-whitespace-results.csv`.
The test-only baseline reproduces the former implementation in the same binary.
Both paths are checked for identical bytes, selection state, bookmark masks,
and single-undo state. Forty regression pairs cover empty/clean input, mixed
CR/LF/CRLF, UTF-8, embedded NUL, only blanks, split CRLF, a tail spanning two
chunks, unterminated EOF, normalization, read-only, multiple and rectangular
selections. The benchmark adds two 8 MiB full-editor buffers and seven pairs
per case (140 recorded before/after samples total); every pair passed.

2026-11-26: optimized MSVC 19.44 `/O2` RelWithDebInfo, SSE4.2, Windows x64,
Intel i9-12900HK. One active ordinary editor tab, UTF-8, CRLF, wrap disabled,
plugins disabled, warm in-memory corpus. Each large corpus has 32,768 lines of
256 bytes: clean lines contain 254 nonblanks; tails contain `x` plus 253 spaces.
No disk input, decode, or temporary-file I/O occurs in the measured callback.
The unchanged actual save/encoding/write path is covered by file-I/O tests and
the complete editor integration smoke test, rather than included in these timings.

## Measured results

Raw samples: [SAVE_WHITESPACE_PERFORMANCE_SAMPLES.csv](SAVE_WHITESPACE_PERFORMANCE_SAMPLES.csv).
Times are milliseconds. With seven samples, nearest-rank p95 and p99 both equal
the maximum. Painting is synchronous invalidation through the first correct paint,
measured after the real save-preparation callback. Thread CPU uses Windows thread
times (coarse 15.625 ms increments). Peak private bytes are sampled every 1 ms;
the table reports maximum increase above each sample's starting private bytes.

| 8 MiB corpus/path | callback median | p95/p99/max | paint median | CPU median | peak private increase | editor calls | char queries | range calls |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| clean/before | 478.446 | 983.204 | 507.747 | 156.250 | 110,592 | 98,310 | 32,768 | 0 |
| clean/after | 12.609 | 15.358 | 48.626 | 15.625 | 122,880 | 133 | 0 | 128 |
| tails/before | 11,252.700 | 19,677.400 | 11,278.300 | 9,546.880 | 9,109,504 | 8,519,687 | 8,323,072 | 0 |
| tails/after | 396.823 | 1,576.850 | 428.922 | 343.750 | 10,379,264 | 131,206 | 0 | 128 |

Each optimized large scan borrows 8,388,608 bytes with zero text-copy bytes.
The tails case allocates a run vector (up to 16 bytes per run plus vector capacity),
while both versions retain Scintilla undo deletion content. Editor-call counters
include reentrant notification work; they are not just explicit deletion calls.

Validation passed: wrapper build with actual link success; `save_whitespace`
(40 pairs, 5.17 s); `fileio_roundtrip`; both inventory checks; full
`editor_integration` (56.64 s); benchmark exit 0 with all 140 records.

The first benchmark samples overlapped other local build/smoke activity, so wall
times include host contention. No hard UI latency bound is claimed: the operation
is still synchronous and dense deletions can exceed a frame budget. Physical IME,
dragging and mixed-DPI interactions were not manually exercised; no visual layout
or input handling was changed. Unicode-line-ending fallback is preserved but not
independently timed in this corpus.
