# Deferred command and tab-close queues (#59)

Both queues keep their existing vectors and advance a head cursor. Exhaustion
clears the vector and resets the cursor, so existing empty checks describe live
work. Compaction starts after 1024 consumed entries and only when the consumed
prefix is at least the live suffix. The moved elements are therefore amortized
over already consumed requests. Close deduplication scans only the live suffix.
Cancellation resets both the close vector and its cursor. The front request is
removed before any reentrant action; close priority and FIFO order are unchanged.

The deferred_queue CTest exercises compaction, append while draining, consumed
close re-enqueue/deduplication, close-before-command dispatch, cancellation reset,
and the existing modal close/open regression with Cancel, No and Yes through
three close routes. The same test runs seven paired drain measurements using
the real queue element types and current TakePending implementation versus the
original erase(begin) loop. Both validate order/checksums.

Windows x64, MSVC 14.44 RelWithDebInfo (/O2), SSE4.2, warm memory, one process,
seven alternating pairs. Data is in
[deferred-queue-pairs.csv](benchmarks/deferred-queue-pairs.csv).

| Drain workload | Before median / max ms | After median / max ms |
|---|---:|---:|
| 4096 commands, 40-character payload | 20.9298 / 22.6202 | 0.0896 / 0.1954 |
| 16384 close handles | 17.1061 / 17.4704 | 0.0334 / 0.0369 |

These isolate queue bookkeeping from unchanged command execution and document
close/save costs. Before the change the workloads shift 8,386,560 command records
and 134,209,536 handles; after compaction shifts at most the linear-size suffixes.
No per-pop allocation or temporary-file I/O is added. Vector storage is retained
as before; two size_t cursors are added per frame. UI CPU, peak private bytes and
first-paint latency were not instrumented, and seven samples do not establish
p95/p99 guarantees. No visual layout was changed; physical IME and mixed-DPI
interactions were not exercised.
