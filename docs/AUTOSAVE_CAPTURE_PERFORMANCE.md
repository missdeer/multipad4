# Autosave capture and lifetime (#54)

Capture now appends borrowed Scintilla spans into the reserved snapshot directly.
It adapts the next span from its measured copy time (64 KiB–1 MiB), targets a
2 ms cooperative capture budget, and caps one callback at 8 MiB. The timer still
runs at 30 ms. A round-robin due-document cursor prevents revision restarts at
the first tab from starving later tabs. Only a complete, revision/encoding/EOL
consistent snapshot is handed to the worker.

Completed captures leave the capture slot while independent writes finish. A
retired write owns only immutable text/request data; the frame retains it until
completion and then keeps or removes both body and metadata as requested. Closing
an independent backup does not wait. Explicit save/reload/close still drain an
overwrite of that document/path before touching the destination. Frame teardown
can still wait for outstanding I/O so worker captures cannot outlive their owner.

Fallback writes validate the UTF-8 header and body before file creation and write
them as separate borrowed spans. `CREATE_NEW`, write-through, flushing and partial
file cleanup remain unchanged. Existing backup encoding/metadata conventions and
forced suspend behavior are preserved.

## Measurement

Run `multipad4-tests.exe --autosave-capture-benchmark` and wait for exit. The
test-only baseline restores the old 256 KiB `Range` allocation plus append once
per pump; worker writing and scheduling are otherwise shared. Thus the comparison
isolates capture, and does not claim a complete old-versus-new scheduler benchmark.
The actual production queue/pump and backup writer are exercised, with a nominal
30 ms interval between pumps. No benchmark baseline switch exists without the
test-build macro.

2026-11-26, Windows x64, MSVC 19.44 `/O2` RelWithDebInfo, SSE4.2, Intel i9-12900HK,
CT1000P3PSSD8 NVMe (temporary files on C:). Two dirty ordinary full-editor tabs,
plugins disabled, warm in-memory UTF-8 corpora: a 16 MiB unterminated line with a
multibyte suffix and a 64 KiB peer. Seven paired iterations passed byte-for-byte
snapshot checks, with each tab's recoverable revision age measured separately.
Both paths use the same encoding/settings and real write-through backup files.
The benchmark was recorded before integrating unrelated lexer-cache changes from
master `34d2f37`; basic tests were repeated after that integration.

Raw data: [job/tab samples](AUTOSAVE_CAPTURE_PERFORMANCE_SAMPLES.csv) and
[every timer pump/input probe](AUTOSAVE_PUMP_PERFORMANCE_SAMPLES.csv).
All times below are milliseconds. Seven job samples make nearest-rank p95/p99
equal the maximum. Age ends when the completed write is applied to document state,
so a backup is actually recoverable; `complete_ms` also includes the poller's last
30 ms wait. Input probes are real queued `WM_NULL` messages posted before the pump
and dispatched after it, not physical typing or presentation measurements.

| Metric | Before | After |
|---|---:|---:|
| Large tab age, median / p95 / p99 / max | 2370.970 / 2379.100 / 2379.100 / 2379.100 | 80.028 / 109.738 / 109.738 / 109.738 |
| Peer age, median / p95 / p99 / max | 2440.280 / 2443.930 / 2443.930 / 2443.930 | 112.080 / 142.266 / 142.266 / 142.266 |
| UI CPU median (Windows thread-time resolution) | 93.750 | 0 (below clock resolution) |
| Maximum sampled private-byte increase | 17,117,184 | 16,846,848 |
| Temporary Range copy bytes per pair | 16,842,760 | 0 |
| Destination capture copy bytes per pair | 16,842,760 | 16,842,760 |
| Borrowed/read chunks per pair | 66 | 18–19 |
| Pump median / p95 / p99 / max | 0.531 / 1.371 / 1.725 / 2.150 | 1.040 / 2.104 / 2.201 / 2.201 |
| Queued input probe median / p95 / p99 / max | 0.633 / 1.642 / 64.970 / 68.045 | 1.061 / 2.128 / 2.233 / 2.233 |

Private bytes are sampled every 1 ms and can miss shorter peaks. UI CPU excludes
the background writer and uses the coarse Windows thread-time counter. Capture
does no decoding or temporary-file I/O; the writer emits both snapshots and their
metadata. Each pair writes 16,842,760 UTF-8 body bytes plus metadata, without a
fallback prefix for these unnamed tabs. Internal Scintilla gap movement is not
counted as an application text copy. This is one warm machine run, not a hard
2 ms latency guarantee or a cold-storage throughput estimate.

## Correctness and limits

`autosave_capture` runs the existing forced-backup/suspend, write-failure/retry,
fallback and snapshot-retention checks. New checks cover UTF-8 validation before
creation, existing-file protection, another due tab during continuous edits,
worker ownership after close, retired snapshot cleanup, save winning against an
in-flight overwrite, and close during overwrite. The full editor smoke also
exercises asynchronous revision invalidation, explicit save, and recovery bytes.
After integrating master `34d2f37`, the wrapper build linked successfully;
`autosave_capture` passed in 1.47 s, inventory coverage passed, and full
`editor_integration` passed in 39.63 s (3/3 CTests). `fileio_roundtrip` also
passed in 2.10 s before that unrelated lexer integration. The capture benchmark
exited 0 with all 28 tab records and 512 individual pump/input-probe records.

Immutable capture was evaluated against the current backends: the mapped
`large::Document::Snapshot` does not represent a mutable Scintilla full-editor
buffer, and borrowed Scintilla ranges cannot survive edits. Using those pointers
on a worker would race. This change retains revision restarts for full-editor
buffers and rotates to other due tabs. A continuously edited large tab can still
retain an older recoverable revision until it allows capture to finish; forced
suspend remains synchronous. Eliminating that limit requires a versioned/COW
full-editor document representation rather than an unsafe borrowed snapshot.

No physical power suspend, hardware input/IME, mixed-DPI, or forced process-kill
test was performed. These changes do not alter visual layout. Independent writes
are retired safely, while foreground operations against overwrite targets can
still wait for storage, as required to prevent stale overwrites.
