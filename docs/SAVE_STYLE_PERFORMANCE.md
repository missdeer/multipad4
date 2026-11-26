# Save style preparation (#51)

Effective configuration snapshots are retained per document and compared against
their actual base configuration, immutable theme, resolved scheme, palette, fonts,
and both override maps. Invalid styles still fail before publication. Every save
continues to resolve content-sensitive schemes and dialects. Appearance changes
continue to use the existing separate lexer-rule invalidation.

Extension and dialect association indexes preserve the original first match and
are rebuilt when the base configuration or theme changes. The UI-thread cache
retains only its last configuration/theme pair; alternating themes can rebuild it.

CSV detection examines the same two complete lines using borrowed spans of at
most 64 KiB. No editor calls occur while a span is scanned. Row state survives
chunk boundaries, including split CRLF. The old whole-line algorithm is retained
only in the test executable as a correctness and measurement reference.

## Measurements

Windows x64, MSVC 14.44, RelWithDebInfo (/O2), SSE4.2, local storage, warm cache,
UTF-8/ASCII input, full editor, one measured tab. The configuration integration
test alternates 15 pairs per workload in one executable. Other test tabs remain
idle. Raw samples: [save-style-pairs.csv](benchmarks/save-style-pairs.csv).

| Workload | Before median ms | After median ms | Before p95 / max ms | After p95 / max ms |
|---|---:|---:|---:|---:|
| SaveTo C++ | 9.301 | 8.646 | 10.942 / 20.009 | 10.442 / 11.572 |
| SaveTo Python | 9.196 | 8.891 | 9.605 / 10.252 | 9.495 / 9.745 |
| SaveTo HTML | 9.231 | 8.731 | 9.815 / 11.829 | 9.547 / 10.125 |
| CSV, two 2 MiB lines | 2.749 | 1.763 | 3.027 / 3.398 | 1.936 / 2.171 |

Save pairs use the actual SaveTo path and invalidate only the snapshot cache in
the before case, isolating repeated snapshot preparation from unchanged atomic
file I/O. They do not measure the removed association parsing separately. All
45 cached saves reused their snapshot. The CSV before case is the original
algorithm; its temporary copy contains 4,194,317 bytes including the terminator.
The after case copies no document bytes and retains no document-sized temporary.
At 15 samples, the floor-index p99 equals the reported p95; these are short local
measurements, not tail-latency guarantees. CPU time and process peak private bytes
were not instrumented, and paint/notification latency was not isolated.

Regression checks cover snapshot reuse and font invalidation, edited shebangs,
Save As, theme/override changes, rejected invalid styles, configuration reload,
document isolation, CSV empty/two-row/long-line/split-CRLF equivalence, and the
existing full lexer behavior baseline. No physical IME or mixed-DPI check was
needed for this nonvisual change.
