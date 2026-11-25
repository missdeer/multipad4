# Consecutive mapped Previous searches

Measured 2026-11-25 for issue #31. Same MSVC19.44 x64 RelWithDebInfo `/O2 /arch:SSE4.2` executable, Intel i9-12900HK, Windows, NVMe SSD. Development work ran concurrently, so these are advisory warm-cache measurements. Every mode starts a fresh process, creates/maps the same 1 MiB UTF-8 source consisting of `Needle aa word\n` repetitions, and performs 100 consecutive Previous operations. No plugins or editor tabs are loaded in this backend harness. Timed operations include search and result selection; corpus setup is excluded.

The baseline independently reproduces the original forward-prefix algorithm with a fresh matcher per operation. The optimized path retains one matcher, as the view now does. Each pair returns identical 100-hit fingerprints (`11579272511834335674`). Three alternating pairs per query were run. `literal` searches case-insensitively for `needle`; `regex` searches the plain regex `Needle`; `regex-complex` searches `(Needle)` with capture 1.

| Query | Path | 100 Previous median ms | Click median ms | Click p95 ms | Click p99 ms | Longest click ms | CPU median ms | Source bytes requested MiB | Peak private MiB |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| literal | baseline | 1950.34 | 18.927 | 23.211 | 27.158 | 32.459 | 1890.6 | 199.94 | 5.75 |
| literal | optimized | 2.85 | 0.000 | 0.000 | 0.001 | 3.006 | 0.0 | 1.06 | 5.95 |
| regex | baseline | 10370.56 | 100.275 | 126.872 | 135.825 | 163.887 | 10203.1 | 100.01 | 5.93 |
| regex | optimized | 133.05 | 0.136 | 0.176 | 0.368 | 121.896 | 125.0 | 1.00 | 5.72 |
| regex-complex | baseline | 11964.93 | 118.079 | 135.253 | 139.486 | 148.876 | 11593.8 | 100.01 | 5.75 |
| regex-complex | optimized | 10804.16 | 107.180 | 123.726 | 129.823 | 143.747 | 10578.1 | 93.82 | 5.69 |

Click percentile columns are medians of the three runs' nearest-rank distributions; longest click is the maximum across all runs. CPU is process CPU (coarse 15.625 ms accounting), and peak private is `PeakPagefileUsage`, including setup. Source bytes requested come from the mapped document's existing counter; physical disk traffic, decoder copy totals and UI paint latency were not measured. The backend runs synchronously for measurement; product calls remain on the existing worker. UI-thread callback/first-paint percentiles are therefore not inferred from these click timings.

Literals now scan blocks backward with the same ordinal Unicode matching and word-boundary checks as the forward matcher. Candidates advance by character rather than match end, preserving overlapping Previous results. The view retains a single matcher only for the same snapshot root and query. Existing Launch drains the prior job before reuse; the cancellation flag belongs to the view, and the destructor drains before cached state is freed. Edits/query changes rebuild the matcher. Decoded reverse blocks require a halo of at most 512 KiB, guaranteeing substantial progress; larger native needles retain their existing reverse path, and larger decoded needles retain the exact prefix fallback. Overlap endpoints align outside UTF-8/DBCS characters and UTF-16 surrogate pairs.

Plain regex patterns containing no metacharacters cache the original forward, non-overlapping match sequence in the existing bounded disk archive. Lower-bound changes or a growing upper bound rebuild it; shrinking upper bounds select the last complete hit and reconstruct capture 0. This deliberately differs from literal overlap behavior. The cache may create up to 32 bytes of logical temporary-file records per hit (about 2.29 MiB for this corpus); its in-memory pending buffer is limited to 4096 records.

Arbitrary regex patterns retain the exact forward-prefix fallback. Captures, lookarounds, anchors, zero-width matches and greedy alternatives can change when the upper bound changes, so their hits are not reused. The `(Needle)` rows show that limitation: retained decoding buffers provide a small reduction in reads, but prefix replay remains. This change does not claim to eliminate prefix replay for all regexes.

Regression checks compare 100 consecutive Previous operations against independent fresh original-algorithm searches across UTF-8, UTF-16LE/BE, CP936 and Latin-1. They cover overlap, whole-word/word-start, misses, lower/upper changes, anchors, captures, lookarounds, zero-width progress, encoded characters crossing reverse block boundaries, cancellation, a 300 KiB native needle, all-multibyte 2 MiB reverse misses (including CP932 and GB18030), 262,141-character needles around the former degenerate-halo threshold, retained matches near the cached block beginning, and Forward-to-Previous transitions across encoded boundaries. Near-edge cache blocks reload when fewer than halo+4 bytes remain, preserving aligned backward progress. The mapped-view integration additionally runs 100 Previous operations per literal/plain-regex query, cancellation/retry and edit invalidation.

Validation: wrapper build, `reverse_search` (19.24 s after review fixes), `lazy_document`, `file_search`, `fileio_roundtrip`, and `large_file_integration` passed. Full editor smoke and actual input-to-paint measurements were not run for #31. No visual layout changes were made.

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R '^(reverse_search|lazy_document|file_search|fileio_roundtrip|large_file_integration)$' --output-on-failure
foreach ($iteration in 1..3) {
    foreach ($query in @('literal', 'regex', 'regex-complex')) {
        foreach ($mode in @('baseline', 'optimized')) {
            & .\cmake-msvc-build\reverse_search_tests.exe $mode $query | Out-Host
        }
    }
}
```

Raw samples:

```text
baseline,literal,100_previous_ms=2072.459,click_median_ms=21.129,click_p95_ms=23.211,click_p99_ms=25.435,click_max_ms=32.459,cpu_ms=2015.625,source_bytes_read=209655150,peak_private=6025216,hash=11579272511834335674
optimized,literal,100_previous_ms=1.926,click_median_ms=0.000,click_p95_ms=0.000,click_p99_ms=0.001,click_max_ms=1.892,cpu_ms=0.000,source_bytes_read=1114120,peak_private=5992448,hash=11579272511834335674
baseline,regex,100_previous_ms=11059.468,click_median_ms=110.610,click_p95_ms=126.872,click_p99_ms=133.975,click_max_ms=134.507,cpu_ms=10734.375,source_bytes_read=104870414,peak_private=6221824,hash=11579272511834335674
optimized,regex,100_previous_ms=133.048,click_median_ms=0.136,click_p95_ms=0.176,click_p99_ms=0.214,click_max_ms=120.222,cpu_ms=125.000,source_bytes_read=1048718,peak_private=5656576,hash=11579272511834335674
baseline,regex-complex,100_previous_ms=11515.599,click_median_ms=115.703,click_p95_ms=133.744,click_p99_ms=135.286,click_max_ms=137.482,cpu_ms=11156.250,source_bytes_read=104870414,peak_private=5652480,hash=11579272511834335674
optimized,regex-complex,100_previous_ms=10804.162,click_median_ms=107.180,click_p95_ms=123.627,click_p99_ms=128.457,click_max_ms=131.202,cpu_ms=10578.125,source_bytes_read=98381558,peak_private=5677056,hash=11579272511834335674
baseline,literal,100_previous_ms=1736.683,click_median_ms=16.888,click_p95_ms=22.268,click_p99_ms=27.158,click_max_ms=27.388,cpu_ms=1750.000,source_bytes_read=209655150,peak_private=6000640,hash=11579272511834335674
optimized,literal,100_previous_ms=3.056,click_median_ms=0.000,click_p95_ms=0.000,click_p99_ms=0.001,click_max_ms=3.006,cpu_ms=0.000,source_bytes_read=1114120,peak_private=6238208,hash=11579272511834335674
baseline,regex,100_previous_ms=10150.490,click_median_ms=98.795,click_p95_ms=119.533,click_p99_ms=143.482,click_max_ms=148.081,cpu_ms=9968.750,source_bytes_read=104870414,peak_private=5599232,hash=11579272511834335674
optimized,regex,100_previous_ms=135.267,click_median_ms=0.125,click_p95_ms=0.167,click_p99_ms=0.439,click_max_ms=121.896,cpu_ms=140.625,source_bytes_read=1048718,peak_private=5632000,hash=11579272511834335674
baseline,regex-complex,100_previous_ms=12247.453,click_median_ms=127.835,click_p95_ms=137.834,click_p99_ms=141.842,click_max_ms=148.876,cpu_ms=12078.125,source_bytes_read=104870414,peak_private=5656576,hash=11579272511834335674
optimized,regex-complex,100_previous_ms=11300.712,click_median_ms=112.348,click_p95_ms=131.193,click_p99_ms=142.305,click_max_ms=143.747,cpu_ms=11062.500,source_bytes_read=98381558,peak_private=5963776,hash=11579272511834335674
baseline,literal,100_previous_ms=1950.343,click_median_ms=18.927,click_p95_ms=27.141,click_p99_ms=30.030,click_max_ms=30.896,cpu_ms=1890.625,source_bytes_read=209655150,peak_private=5685248,hash=11579272511834335674
optimized,literal,100_previous_ms=2.851,click_median_ms=0.000,click_p95_ms=0.000,click_p99_ms=0.002,click_max_ms=2.800,cpu_ms=0.000,source_bytes_read=1114120,peak_private=6025216,hash=11579272511834335674
baseline,regex,100_previous_ms=10370.557,click_median_ms=100.275,click_p95_ms=129.213,click_p99_ms=135.825,click_max_ms=163.887,cpu_ms=10203.125,source_bytes_read=104870414,peak_private=6205440,hash=11579272511834335674
optimized,regex,100_previous_ms=121.117,click_median_ms=0.209,click_p95_ms=0.269,click_p99_ms=0.368,click_max_ms=102.558,cpu_ms=125.000,source_bytes_read=1048718,peak_private=5996544,hash=11579272511834335674
baseline,regex-complex,100_previous_ms=11964.932,click_median_ms=118.079,click_p95_ms=135.253,click_p99_ms=139.486,click_max_ms=143.068,cpu_ms=11593.750,source_bytes_read=104870414,peak_private=6033408,hash=11579272511834335674
optimized,regex-complex,100_previous_ms=10578.402,click_median_ms=103.310,click_p95_ms=123.726,click_p99_ms=129.823,click_max_ms=130.987,cpu_ms=10421.875,source_bytes_read=98381558,peak_private=5615616,hash=11579272511834335674
```
