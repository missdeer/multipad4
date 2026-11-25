# Mapped viewport coordinates (#26)

Measured 2026-11-25 on Windows, Intel Core i9-12900HK, MSVC 19.44,
RelWithDebInfo `/O2`, SSE4.2. Both algorithms run in the same executable with
identical Scintilla settings and the same warm mapped source. The corpus is
4,096 repetitions of `中😀 hit\n`, UTF-16LE without BOM: 64 KiB native,
48 KiB UTF-8. Other test documents remain open, identically for each pair.

With `MULTIPAD4_VIEWPORT_BENCHMARK=1`, `large_file_integration` runs 21 paired samples on the application UI thread.
The baseline reproduces the old implementation: read and decode the native
prefix twice per hit, then fill its Scintilla indicator. The changed path calls
`SetMarks` with the same archive and snapshot. Each sample completes pending
window painting with `UpdateWindow`. This is a paired algorithm comparison,
not a comparison of separately built application revisions or input-message
latency. It includes the real mark application path and keeps all 4,096 marks.

| Milliseconds per paint callback | Prefix decoder | Coordinate table |
| --- | ---: | ---: |
| Median | 677.693 | 41.713 |
| p95 | 736.404 | 46.323 |
| p99 / maximum (21 samples) | 742.994 | 46.370 |
| Native bytes read and decoded per repaint | 268,460,032 | 0 |

Median callback time fell 16.2 times. The new benchmark asserts that repainting
performs no source reads. No worker or temporary-file work replaces decoding.
The whole integration process peaked at 534 MiB working set; this includes
unrelated 320 MiB opening/streaming checks and is not an isolated allocation
comparison. The coordinate table stores two `uint32_t` values per complete native
character boundary. This corpus stores 229,384 bytes (28,673 entries) in a 262,152-byte reserved
vector allocation; the normal
128 KiB viewport is bounded to approximately 1 MiB of table entries in the
single-byte worst case. UTF-8 keeps the direct-offset path and allocates no table. Unusual installed multibyte code pages that do not expose DBCS lead-byte ranges retain the original prefix-conversion behavior.

A seek decodes its viewport once and builds its table in a linear pass over the
already available native and UTF-8 buffers. Caret/anchor capture and restoration
use table lookups, reusing the result when both positions are equal. A typed
insertion encodes only the inserted text; deletion and coordinate capture encode
or decode no viewport prefixes. Edits splice the character boundaries and adjust
the suffix in memory. Paint walks each retained mark group's coordinates once;
its cursor resets per group, including when both edges are clipped.

Regression checks exercise UTF-16LE/BE, surrogate pairs, Shift-JIS and GB18030,
selected bytes, insertion/deletion via replacement, cross-viewport extended
selection replacement and undo/redo selections,
bookmarks, retained mark groups and both viewport edges after seeking. Existing
UTF-8 mapped search, replacement and large-file checks remain enabled.

Typing one ASCII character at the beginning of this marked viewport took median
0.689 ms, p95 0.823 ms, maximum 7.940 ms (21 samples); no native prefix is decoded.
Seeking/rebuilding the same 64 KiB view including repaint took median 46.575 ms,
p95 52.175 ms, maximum 53.642 ms. Each seek decoded exactly 65,536 native bytes.
These two timings describe the new implementation only, not a paired speedup.
The paired samples ran while another worktree compiled, so absolute latencies
include system load; both algorithms were interleaved under that load.

Validation: optimized build succeeded; `file_search`, `lazy_document`,
`fileio_roundtrip`, `large_file_integration` (41.30 s, expanded checks and opt-in benchmark) and
`editor_integration` (25.81 s) all passed.

Not measured separately: input-to-first-paint latency, UI CPU time, isolated peak
private bytes and actual scroll-input latency. Manual IME, dragging and mixed
DPI checks were not performed. No appearance change is intended.
