# Indexed open-file identity and native traversal

Measured 2026-11-25 for issue #37. Both paths use the same MSVC19.44 x64 RelWithDebInfo `/O2 /arch:SSE4.2` test executable on an Intel i9-12900HK, Windows and NVMe SSD. Three alternating pairs use fresh processes and identical local temporary corpora: 2000 six-byte UTF-8 files, 128 open-path entries, warm filesystem cache. Corpus creation/deletion is outside the timed region. No plugins or editor tabs are loaded in the backend harness; open paths model the identity workload. Other development builds were active, so timing variation is reported rather than attributed solely to this change.

## Identity workload

The baseline calls `filesystem::equivalent` against each open path until a match. The optimized harness builds the production volume/128-bit-file-ID index, then queries each candidate. Both identify exactly 128 open files.

| Path | Median elapsed ms | Median process CPU ms | Equivalent comparisons | Identity queries | Peak private bytes, maximum |
|---|---:|---:|---:|---:|---:|
| Baseline | 22863.40 | 22250.00 | 247872 | — | 1601536 |
| Index | 55.96 | 46.88 | 128 | 2128 | 1617920 |

An equivalent comparison can itself query both paths, so the comparison and identity counters are different operations, not claimed to be identical system calls. The index reduces the repeated file-to-tab comparisons to one lookup per candidate plus one initial query per open path. The index verifies positive ID/path candidates against their live paths and retains all duplicate-ID entries. Production additionally rechecks a positive result against the current path with one equivalent comparison; this costs one comparison per matched candidate, not per tab. The index harness includes its 128 positive verifications but excludes the additional consumer recheck, file content search, Finder formatting and UI paint.

The worker constructs its immutable index once on a worker, rather than issuing all open-path queries from StartFiles. The dialog index invalidates on open-document identity/path/observed-size/observed-write-time changes and pending external checks. A positive lookup is validated against the live path. Replacement refreshes every open identity before a closed-file write and rechecks the candidate; a cache miss alone never authorizes that write. When integrating with the newer replacement pipeline, retain its complete final live-buffer/identity/version check and commit reservation.

An ordered ordinal-case-insensitive path index covers atomic same-path replacements that change a cached ID; every path fallback is still verified with equivalent, preserving case-sensitive directory safety. Native enumeration stores LastError immediately before any destructor runs. Failed FileIdInfo queries retain `filesystem::equivalent` fallback behavior. No failed query is invented as an ID or cached as an authoritative identity. Hard links share an ID; path case is verified against actual identity rather than assumed to identify a file on case-sensitive directories.

## Native directory traversal

The baseline reproduces the previous directory iterator plus explicit GetFileAttributes/is_regular_file path. The native walker uses FindFirstFileEx/FindNextFile data and reuses enumeration attributes. Filters, hidden entries and reparse exclusions remain unchanged.

| Path | Median elapsed ms | Additional explicit attribute queries |
|---|---:|---:|
| Baseline | 111.676 | 2000 |
| Native walker | 1.183 | 0 |

Eight worker threads sharing the production walker and reading/decoding each selected file completed in a median 58.730 ms. Aggregate lock wait was 11.786 ms and aggregate hold 6.076 ms. Across runs, wait occupied less than five percent of the eight workers' available time. One candidate per lock acquisition remains appropriate after removing per-entry metadata I/O; no extra batch queue was introduced. Walker memory follows directory depth.

## Verification and limits

`file_search` and `fileio_roundtrip` pass. Tests cover serial/parallel result equivalence, hard links, case variants, hidden directories, file reparse exclusion when symlink creation is available, missing directories, failed identity queries and share-denied reads, cancellation and retry. Tests initialize an index, atomically replace a path before traversal, verify the new-ID path fallback, and retain the second candidate after initially duplicate IDs diverge. The full editor suite includes a new hard-link query that must search an unsaved live buffer and replace only that buffer as one undo action while leaving the linked disk bytes unchanged. It also checks a warm UI index after an atomic replacement with monitoring disabled. A final conflict fixture ensures a cached miss caught by the live check publishes zero hits and preserves disk and buffer contents.

Slow or offline SMB shares were not available for measurement. Unsupported file-ID providers use the fallback; unsupported large-fetch enumeration flags retry without that flag. Individual Windows filesystem calls may still block on a slow provider. Exact input-to-paint percentiles, physical disk bytes and full application throughput were not measured. This is a metadata/index benchmark, not a claim about all end-to-end search time.

If a fresh search worker identifies an open document but the dialog's older
index misses a newly created hard-link alias, the read-search consumer refreshes
that index once before resolving the live buffer. Ordinary closed-file misses
keep the indexed path. A regression disables monitoring, warms the dialog
index, atomically replaces an open path, and searches its new hard link for
unsaved buffer-only text while verifying that disk and buffer stay unchanged.

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R '^(file_search|fileio_roundtrip|editor_integration)$' --output-on-failure
foreach ($iteration in 1..3) {
    foreach ($mode in @('baseline', 'optimized', 'walk-baseline', 'walk-optimized', 'walker')) {
        & .\cmake-msvc-build\file_search_tests.exe $mode | Out-Host
    }
}
```

Raw samples:

```text
baseline,cpu_ms=21640.6,peak_private=1519616,files=2000,tabs=128,elapsed_ms=22415.6,identity_operations=247872,equivalent_checks=247872,matches=128
optimized,cpu_ms=46.875,peak_private=1609728,files=2000,tabs=128,elapsed_ms=55.9555,identity_operations=2128,equivalent_checks=128,matches=128
walk-baseline,files=2000,elapsed_ms=126.262,extra_attribute_queries=2000
walk-optimized,files=2000,elapsed_ms=3.1747,extra_attribute_queries=0
walker,files=2000,workers=8,elapsed_ms=175.233,aggregate_lock_wait_ms=67.0821,aggregate_lock_hold_ms=16.7754,errors=0
baseline,cpu_ms=24984.4,peak_private=1425408,files=2000,tabs=128,elapsed_ms=39440.5,identity_operations=247872,equivalent_checks=247872,matches=128
optimized,cpu_ms=62.5,peak_private=1605632,files=2000,tabs=128,elapsed_ms=63.0507,identity_operations=2128,equivalent_checks=128,matches=128
walk-baseline,files=2000,elapsed_ms=111.676,extra_attribute_queries=2000
walk-optimized,files=2000,elapsed_ms=1.1825,extra_attribute_queries=0
walker,files=2000,workers=8,elapsed_ms=58.7298,aggregate_lock_wait_ms=11.5196,aggregate_lock_hold_ms=6.0763,errors=0
baseline,cpu_ms=22250,peak_private=1601536,files=2000,tabs=128,elapsed_ms=22863.4,identity_operations=247872,equivalent_checks=247872,matches=128
optimized,cpu_ms=46.875,peak_private=1617920,files=2000,tabs=128,elapsed_ms=49.902,identity_operations=2128,equivalent_checks=128,matches=128
walk-baseline,files=2000,elapsed_ms=44.683,extra_attribute_queries=2000
walk-optimized,files=2000,elapsed_ms=0.9426,extra_attribute_queries=0
walker,files=2000,workers=8,elapsed_ms=56.3781,aggregate_lock_wait_ms=11.7856,aggregate_lock_hold_ms=5.7448,errors=0
```
