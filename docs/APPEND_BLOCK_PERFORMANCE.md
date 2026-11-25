# Packed append journal and pieces (#35)

2026-11-25, Windows, Core i9-12900HK, MSVC19.44, RelWithDebInfo `/O2`,
SSE4.2, warm local temporary-file storage. Paired executables
`append_block_baseline benchmark` and `append_block_packed benchmark` compile
the same source and checks. The baseline switch restores per-insertion journal
writes, ordinary tree merges, the zero-length split and per-piece output.
Both perform 100,000 separate single-character edits, keep each undo revision,
serialize identical 100,000-byte ASCII output, and run the same old-root,
undo/redo, Abort, concurrent snapshot search and save-retention checks.

| Counter / measurement | Baseline | Packed |
| --- | ---: | ---: |
| Time for 100,000 edits, ms | 2273.614 | 386.018 |
| Journal WriteFile calls during edits | 100,000 | 1 |
| Persistent nodes allocated during edits | 2,145,697 | 374,401 |
| Process private-byte increase with undo retained | 310,366,208 | 73,674,752 |
| Save copy completion, ms | 6360.109 | 75.936 |
| Nonempty output chunks | 100,000 | 2 |

These are one paired end-to-end sample, not median/p95/p99 estimates. Executables
ran sequentially on the same desktop; background compilation can influence
absolute time. Private-byte delta is a process-level measurement, not an exact
sum of undo object sizes. Write counters count actual journal WriteFile calls;
output chunks count producer calls, not kernel destination WriteFile calls.
Only one full 64 KiB journal block is written during the 100 KiB edit workload;
the remaining published bytes stay readable in the bounded tail buffer. The
source document, buffer and snapshots remain alive through save and the checks.

A fixed 64 KiB journal tail packs small insertions. The existing mutex protects
appends, partial-block flushes and reads spanning persisted/tail bytes; no pointer
into the tail escapes the lock. Source lengths and indexes remain immutable.
Compatible neighboring ranges in the same journal block create a new immutable
Source view and coalesce persistent pieces; old roots retain their original view.
Original backing ranges also coalesce when their Source and offsets agree.
Pure insertion skips splitting the rest at zero. Save assembles small chunks in a
bounded 64 KiB buffer and passes larger chunks through directly.

Correctness checks retain roots at ten edit checkpoints, read them after undo,
redo and Abort, and search/read an immutable snapshot concurrently while editing.
A test-only fault fails a journal flush after thirteen written bytes; old published
tail bytes remain intact, and retry succeeds. A producer failure after partial
save output leaves the pre-existing destination intact and keeps the document
dirty. The final successful save is checked byte-for-byte.

Validation: optimized build and `append_block` (0.46 s) passed; broader regression
results are recorded by the workflow. No public model API or UI layout changed.
Not measured: interaction-to-first-paint, UI-thread CPU, peak private bytes,
physical disk I/O, or a statistical distribution for the full 100,000-edit run.

Broader validation passed on the final build: `file_search`, `lazy_document` and
`fileio_roundtrip` (47.03 s combined), `large_file_integration` (16.62 s), and
`editor_integration` (24.02 s).
