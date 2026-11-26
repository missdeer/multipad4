# Save-time trailing blank removal (#53)

`PrepareFileSave` now scans borrowed 64 KiB Scintilla ranges and records only
trailing space/tab runs. A run survives range boundaries; CR/LF are recognized
independently, including split CRLF. All ranges expire before reverse deletions.
The final unterminated line is included. Scintilla's boundaries remain authoritative
when Unicode line endings are active. Long average lines (over 4 KiB) use indexed
line boundaries and scan only their tails backward in at most 64 KiB spans, so
long nonblank prefixes are skipped. The threshold selects an algorithm, never a
content cutoff. Deletions and subsequent EOL conversion
remain one undo action; read-only documents remain unchanged.

## Reproduction and correctness

Build using `bash -lc "cmake-build cmake-msvc-build"`. Run CTest's
`save_whitespace` or `multipad4-tests.exe --save-whitespace-benchmark` (wait for
the GUI process to exit). The latter writes `save-whitespace-results.csv`.
The test-only baseline reproduces the former implementation in the same binary.
Both paths are checked for identical bytes, selection state, bookmark masks,
and single-undo state. Forty-five regression pairs cover empty/clean input, mixed
CR/LF/CRLF, UTF-8, embedded NUL, only blanks, split CRLF, a tail spanning two
chunks, unterminated EOF, normalization, read-only, multiple and rectangular
selections. The benchmark adds four 8 MiB full-editor buffers and seven pairs
per case (182 recorded before/after samples total); every pair passed.

2026-11-26: optimized MSVC 19.44 `/O2` RelWithDebInfo, SSE4.2, Windows x64,
Intel i9-12900HK. One active ordinary editor tab, UTF-8, CRLF, wrap disabled,
plugins disabled, warm in-memory corpus. The multiline corpora have 32,768 lines of
256 bytes: clean lines contain 254 nonblanks; tails contain `x` plus 253 spaces.
The single-line corpora contain 8 MiB of `x`, with either no tail or a two-byte
space/tab tail. No disk input, decode, or temporary-file I/O occurs in the measured callback.
The unchanged actual save/encoding/write path is covered by file-I/O tests and
the complete editor integration smoke test, rather than included in these timings.

## Measured results

Raw samples: [SAVE_WHITESPACE_PERFORMANCE_SAMPLES.csv](SAVE_WHITESPACE_PERFORMANCE_SAMPLES.csv).
Times are milliseconds. With seven samples, nearest-rank p95 and p99 both equal
the maximum. Thread CPU uses Windows thread times (coarse 15.625 ms increments).
Peak private bytes are sampled every 1 ms; the table reports maximum increase
above each sample's starting private bytes. This final run used a hidden process
launch: the CSV paint column measures redraw return, not displayed first paint.
The initial visible run before adding the long-line strategy measured median
first-paint completion of 48.6 ms for clean lines and 428.9 ms for dense tails.
Those initial paint results do not establish final-head presentation latency.

| 8 MiB corpus/path | callback median | p95/p99/max | CPU median | peak private increase | editor calls | char queries | range calls |
|---|---:|---:|---:|---:|---:|---:|---:|
| clean/before | 132.061 | 242.031 | 93.750 | 32,768 | 98,310 | 32,768 | 0 |
| clean/after | 7.794 | 10.166 | 0 | 32,768 | 134 | 0 | 128 |
| tails/before | 9,726.120 | 15,091.500 | 8,937.500 | 14,663,680 | 8,519,687 | 8,323,072 | 0 |
| tails/after | 291.466 | 548.350 | 250.000 | 15,335,424 | 131,207 | 0 | 128 |
| single line clean/before | 0.0148 | 0.0166 | 0 | 0 | 7 | 1 | 0 |
| single line clean/after | 0.0181 | 0.0213 | 0 | 0 | 10 | 0 | 1 |
| single line short tail/before | 0.0483 | 0.0611 | 0 | 0 | 14 | 3 | 0 |
| single line short tail/after | 0.0470 | 0.0529 | 0 | 0 | 15 | 0 | 1 |

Each optimized multiline scan borrows 8,388,608 bytes; each single-line scan
borrows just 65,536 bytes. Neither makes application-owned text copies (Scintilla
may move its internal gap).
The tails case allocates a run vector (up to 16 bytes per run plus vector capacity),
while both versions retain Scintilla undo deletion content. Editor-call counters
include reentrant notification work; they are not just explicit deletion calls.

Validation passed: wrapper build with actual link success; `save_whitespace`
(45 pairs, 6.89 s); `fileio_roundtrip`; both inventory checks; full
`editor_integration` before the long-line refinement (56.64 s); benchmark exit 0 with all 182 records.

The first benchmark samples overlapped other local build/smoke activity, so wall
times include host contention. No hard UI latency bound is claimed: the operation
is still synchronous and dense deletions can exceed a frame budget. Physical IME,
dragging and mixed-DPI interactions were not manually exercised; no visual layout
or input handling was changed. Unicode-line-ending fallback is preserved but not
independently timed in this corpus.
