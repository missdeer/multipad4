# Direct piece reads (#34)

2026-11-25, Windows, Core i9-12900HK, MSVC19.44, RelWithDebInfo `/O2`,
SSE4.2. `piece_read_baseline benchmark` and `piece_read_direct benchmark` use
the same source, snapshots and warm local temporary-file storage. A compile-time
baseline switch restores the former per-piece temporary-string append algorithm;
all correctness checks run in both executables. No editor tabs, encoding decode,
plugins or UI callbacks are involved in this console read benchmark.

Each row reports 21 complete reads; output bytes are checked after timing. The
shared path uses `Document::Read`; independent uses one retained
`Document::ReadSession`. Before/after executables ran sequentially on the same
desktop. The journal case contains 4,096 adjacent 16-byte insertions plus a later
5-byte prefix in a separate backing region. The mixed case has 1,024 8-byte
replacements alternating with memory-backed original text and that 5-byte prefix.
The mapped case retains a 128 KiB mapped source plus the prefix after saving.

| Shared read workload | Native bytes | Median ms before / after | p95 before / after | Maximum before / after |
| --- | ---: | ---: | ---: | ---: |
| Contiguous memory | 4,194,304 | 2.4905 / 1.4698 | 3.2191 / 2.0708 | 4.0251 / 2.1172 |
| Adjacent journal fragments | 65,541 | 29.5393 / 0.2006 | 32.4932 / 0.2937 | 33.2706 / 0.3403 |
| Mixed fragments | 65,541 | 7.5235 / 6.8022 | 8.0555 / 7.5884 | 8.0803 / 7.9031 |
| Mapped + journal | 131,090 | 0.0222 / 0.0214 | 0.0339 / 0.0275 | 0.5697 / 0.0280 |

| Independent read workload | Median ms before / after | p95 before / after | Maximum before / after |
| --- | ---: | ---: | ---: |
| Contiguous memory | 2.6364 / 1.4125 | 3.6122 / 2.0215 | 3.8799 / 2.4365 |
| Adjacent journal fragments | 30.0271 / 0.1805 | 34.7760 / 0.2447 | 38.4415 / 0.5458 |
| Mixed fragments | 7.1505 / 7.4480 | 8.2945 / 9.9550 | 9.1322 / 11.3468 |
| Mapped + journal | 0.0218 / 0.0219 | 0.0301 / 0.0285 | 0.1072 / 0.1052 |

The mixed independent case does not show a speed improvement in this run; its
backing layout prevents coalescing. The measured benefit there is removal of the
per-piece temporary string and second logical copy, not reduced read calls.

Both shared/independent variants halve logical copied bytes: 8,388,608→4,194,304
for contiguous memory, 131,082→65,541 for both fragmented cases, and
262,180→131,090 for mapped+journal. Backing-range requests remain 1, 2,049 and 2
for contiguous memory, mixed, and mapped respectively; adjacent journal requests
fall from 4,097 to 2. Counters are instrumented traversal requests and explicit
logical copy lengths, not kernel read syscall counts, zero-fill traffic, mapping
page faults or fallback-buffer copies. Read-result string initialization is not
counted as a logical copy. Each byte still belongs to the exact requested output;
no wider unrelated physical range is read to create the reduction.

The destination is allocated once. Mapped spans are copied synchronously while
the view is valid; the only callback under the source lock is the internal memcpy.
Journal runs read directly into the destination under the existing journal lock.
Contiguous ranges may cross separate edit Source objects sharing one journal;
immutable snapshots keep those sources and their journal alive during traversal.
Independent file read sessions and existing source-bytes-read accounting remain.

Checks cover empty documents, zero/one/large/SIZE_MAX lengths, exact/clamped and
invalid offsets, mapped metrics, memory/journal/mixed pieces, snapshots retained
after edit and save, and four concurrent shared/independent readers while a later
edit is published. Optimized build and `piece_read` (3.97 s) passed; broader
validation is reported by the workflow.

Not measured: input-to-first-paint, UI-thread CPU, isolated private bytes, or
physical disk I/O. No application appearance changed. p99 is the maximum of the
21 samples.

Broader validation also passed: `file_search`, `lazy_document`, `fileio_roundtrip`
and `piece_read` together (49.20 s), plus `large_file_integration` (11.52 s) and
`editor_integration` (22.13 s), on the final optimized build.

Review verification: coalesced mapped reads now visit at most 16 MiB per chunk
inside the existing shared lock or independent session, avoiding a large request
falling back to the mapping layer's intermediate read buffer. The default test
build reads an 80 MiB file split by an insertion/deletion and verifies exact bytes
for shared and independent sessions, a maximum Visit length of 16 MiB and a
single logical output copy. It also asserts adjacent-journal coalescing remains
two requests and one logical copy in the normal non-benchmark run. Optimized
rebuild and `piece_read` passed (3.03 s). A >4 GiB output allocation was not tested;
the same bounded chunk loop limits all Visit lengths to 16 MiB regardless of the
total caller request.
