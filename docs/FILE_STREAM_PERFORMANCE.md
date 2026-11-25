# Stream eligible directory literals (#38)

2026-11-25, Windows, Core i9-12900HK, MSVC19.44, RelWithDebInfo `/O2`, SSE4.2.
`file_stream_baseline` and `file_stream_streaming` run seven warm serial samples
on the same eight 2 MiB ASCII files in a local temporary directory. Both search
case-sensitive literal `needle`, check hit counts and retained snapshot bytes,
and use the same decoding and snapshot/archive implementation. The baseline
compile definition selects the prior spool-then-search path. The dense corpus
contains one match every 128 bytes (131,072 total matches). Misses contain none.
Executables ran sequentially on the same desktop, not interleaved.

| Eight-file workload | Median ms before / after | Max ms before / after | UTF-8 snapshot temp bytes before / after | Delivered decoded UTF-8 bytes before / after |
| --- | ---: | ---: | ---: | ---: |
| No matches | 95.319 / 49.765 | 102.587 / 52.754 | 16,777,216 / 0 | 16,777,216 / 16,777,216 |
| Dense matches | 185.728 / 124.775 | 199.583 / 145.287 | 16,777,216 / 16,777,216 | 16,777,216 / 33,554,432 |

Only seven samples were taken, so p95/p99 are not independently estimated.
The current private bytes after workload completion were 3,600,384 / 3,440,640
for misses and 3,833,856 / 3,948,544 for dense baseline/streaming runs. These are
not peaks or isolated allocation measurements. Counters report UTF-8 snapshot
writes and delivered decoded bytes; they exclude hit-archive temporary I/O,
encoding probes/validation reads, and physical storage traffic.

Eligible literals are nonempty, case-sensitive searches without regex, whole-word
or word-start constraints. Other modes keep their existing complete-snapshot
matcher semantics. The stream uses string search with a bounded unresolved suffix,
tracking CR/LF/CRLF line coordinates while consuming that same decoded stream.
A late decoded NUL vetoes the entire file, including provisional archive hits;
cancellation and decoder errors also prevent publication.

For a hit, a short-lived read-only source guard denies writes/deletion until a
second decode captures the exact searched version. The second pass increases
decoded bytes for dense hits but avoids re-searching or separately rereading line
prefixes. Misses never create a UTF-8 snapshot. The guard is released before
publishing, and retained results use independent memory/temp snapshots, so the
original file can immediately be saved, renamed or deleted. Small-result text
and match-vector duplicates were removed; both the pane and tests now read the
archive/snapshot representation.

Focused checks compare every chunk size from one byte through the full sample
for literals containing CR, LF, CRLF, Unicode and overlapping candidates against
`SearchSnapshot`, plus cancellation. File integration checks cover UTF-8 and both
UTF-16 endiannesses, chunk-spanning matches, late NUL rejection, malformed UTF-8
after early hits, declared legacy encoding, and snapshot access after source
replacement/deletion. Existing regex and parallel-result assertions migrated to
the archive/snapshot and remain enabled.

Optimized build, `file_stream` (0.38 s) and `file_search` (1.06 s) passed;
broader validation is reported separately. No appearance changed.
Not measured: UI input-to-first-paint, peak private bytes, worker/UI CPU,
actual native reads, and physical temporary-file I/O.

Final broader validation passed: four console tests (50.73 s combined),
`large_file_integration` (12.31 s), and `editor_integration` (25.11 s).
