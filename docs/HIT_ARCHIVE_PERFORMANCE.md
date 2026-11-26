# Hit archive lookup and transfer (#36)

Spilled archives now retain a sparse index of the last endpoint in each group
of at most 16 records. Lookup selects the earliest qualifying group and binary
searches one cached raw-record group (at most 512 bytes). The in-memory tail is
searched directly. Index entries record actual positions/counts, including
partial flushes caused by `Page`, so no fixed physical-page alignment is assumed.
Duplicate endpoints spanning groups select their earliest occurrence.

The 4,096-record write buffer is unchanged. Accepted parallel-worker archives
are transferred in raw record blocks after the existing overlap/phase boundary
reconciliation, avoiding conversion to temporary `SearchHit` vectors and one
Append call per record. Transfer includes the unflushed tail and retains line
coordinates. Count-only archives retain their existing counter path.

## Measurement

```
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R hit_archive --output-on-failure
cmake-msvc-build/hit_archive_benchmark.exe hit-archive.csv
```

Windows 11 Pro 10.0.26300, Intel Core i9-12900HK, MSVC 19.44 RelWithDebInfo
`/O2`, x64 SSE4.2. A 200,000-record archive uses a Windows temporary file on
the local drive with warm OS cache. Five runs per variant use identical input
and deterministic query order. Timed lookup work performs 10,000 queries;
the mixed workload interleaves one random query with three sequential scroll
queries. Transfer copies the entire archive into a new retained archive.

The baseline reconstructs the previous binary-search-via-`Page(middle, 1)` and
Page/Append merge paths in the same binary. Test-only instrumentation records
actual archive read calls/bytes. A test-only global allocation counter records
ordinary `operator new` calls on the measurement thread. Fixture creation is
outside the timed region. CSV data is in `benchmarks/hit-archive.csv`.

| Workload | Median before / after | Read calls before / after | Read bytes before / after | Allocations before / after |
|---|---:|---:|---:|---:|
| Sequential scroll | 1209.57 / 5.68 ms | 176896 / 625 | 5660672 / 320000 | 353792 / 1 |
| Random | 1441.92 / 101.13 ms | 176899 / 9832 | 5660768 / 5033984 | 353798 / 1 |
| Transfer | 26.52 / 8.33 ms | 49 / 48 | 6400000 / 6291456 | 127 / 9 |
| Mixed scroll/random | 1284.31 / 47.15 ms | 176898 / 4923 | 5660736 / 2520576 | 353796 / 1 |

The first trial cached whole 4,096-record pages. Random queries reduced calls
but increased reads to 1.26 GB, so it was rejected. Sixteen-record groups keep
random read volume below the old tiny-read binary search while retaining fast
sequential access. The sparse index costs about 1.5 bytes per archived hit
(24-byte descriptor per 16 records, excluding vector capacity), and the cached
record buffer uses at most 512 bytes. Bulk-transfer buffers remain at most
128 KiB. Memory use is bounded by this index and the existing append buffer;
there is no full decoded archive retained.

Timing and counters are synchronous archive operations, not UI input-to-paint
measurements. UI CPU, peak private bytes, compositor latency and per-query
percentiles were not separately instrumented. Raw elapsed times include vector
allocation and temporary-file I/O. Read bytes are logical file reads, not
physical storage traffic. Output creation/CSV writes are outside the measured
regions. No document decoding is performed by these archive operations.

## Verification

`hit_archive` covers empty archives, duplicate/zero-length endpoints crossing
group and physical-page boundaries, Page-triggered partial flushes, appended
tails, random queries, exact transferred metadata and cancellation before
mutation. Existing lazy-document and file-search tests compare serial and
parallel archives, including overlapping patterns and encoded boundaries.
File-I/O tests also pass. Application large-file/editor suites are run to check
the scrolling, mark and results consumers.

IME, dragging and mixed-monitor DPI were not manually checked; there is no
visual-layout change.
