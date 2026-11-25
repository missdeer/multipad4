# Regex iterator decoding (#32)

Measured 2026-11-25 on Windows, Core i9-12900HK, MSVC19.44,
RelWithDebInfo `/O2`, SSE4.2. `regex_iterator_baseline benchmark` and
`regex_iterator_cached benchmark` compile the same source, matcher and tests;
the baseline compile definition restores the previous Iterator dereference,
advance and byte-block lookup behavior. Both use the same Boost expression
`(word)([0-9]+)|never_match`, native range limits, serial worker setting,
4,096 matches and eleven warm in-memory samples. UTF-8 and UTF-16 corpora repeat
`中😀 word123 alpha beta\n`; Shift-JIS repeats `ソ word123 alpha beta\n`.
The same native document remains alive throughout each run; no temp-file output
or UI callback is part of this console benchmark.

| Encoding / native bytes | Median ms before / after | p95=max ms before / after | Decode calls per sample before / after |
| --- | ---: | ---: | ---: |
| UTF-8 / 110,592 | 20.134 / 9.009 | 25.303 / 11.974 | 372,736 / 110,592 |
| UTF-16LE / 188,416 | 17.334 / 10.800 | 21.275 / 14.586 | 376,832 / 131,072 |
| Shift-JIS / 90,112 | 24.735 / 15.678 | 34.221 / 17.324 | 319,488 / 106,496 |

With only eleven samples, p95 and p99 are the maximum sample. Counters count
`Reader::At` calls, including hits served from byte cache; they are not a claim
that every call copies or rereads native bytes. Input and hit/capture checks are
identical for each binary. Baseline and changed runs were sequential under the
same desktop load, not interleaved. Run both commands to reproduce the comparison.

The changed iterator owns its decoded unit(s) and native width by value. Copies
and backtracking retain valid values independently of the four byte-block slots.
Increment consumes the cached width and still updates DBCS block checkpoints;
new positions are decoded only when reached. The reader remembers a hot byte
cache slot without borrowing pointers into evictable blocks. Cached accesses
retain periodic cancellation checks, even when repeatedly dereferencing a copy.
No eager block decoder or extra document copy is introduced.

Targeted regressions cover UTF-8, both UTF-16 endiannesses, Shift-JIS and GB18030,
supplementary characters, capture offsets, alternation/lookahead backtracking,
forward and previous-character traversal, copied iterators across all cache-slot
evictions, a regex match after more than four cache blocks, cancellation on cached
access, and invalid suffixes reached only after a successful early regex match.
The same checks run in both baseline and cached benchmark binaries.

Validation: optimized build, `regex_iterator` (0.45 s), `file_search`,
`lazy_document` and `fileio_roundtrip` (combined 50.35 s) passed.
Not measured: input-to-first-paint, UI callbacks, UI-thread CPU, isolated peak
private bytes and native bytes copied/read. GUI regression tests are reported
separately; this benchmark measures matcher completion and decoder invocation
counts, not application interaction latency.

GUI regression validation also passed: `large_file_integration` (14.63 s) and
`editor_integration` (24.38 s), including the existing mapped regex/search and
selection/replacement flows. No visual behavior or UI layout was changed.
