# Streaming read probes (#39)

Forced encoding reads no longer allocate or read a detection sample. Automatic
reads reuse the sample for the first modeline region and any overlapping tail
region, reading only missing tail bytes. First-region precedence is unchanged.

`DetectSample` and the streaming probe share the same encoding decision tree.
Failed candidate validations are remembered, so falling through heuristics or
retrying the fallback does not decode the same failed encoding again. Streaming
probes select an encoding without constructing decoded text: UTF-8 is validated
directly, UTF-16 code units are checked for valid surrogate pairs, and legacy
encodings use the platform's required-length validation call. Normal conversion
still happens on delivery. Whole-file parity gates BOM-less UTF-16 heuristics,
so an even-sized sample cannot misclassify an odd-length file as UTF-16.

A valid UTF-8 sample records its last complete character boundary. Full-stream
validation resumes there and completes before automatic UTF-8 delivery. BOM
UTF-8 also validates fully before publication; invalid BOM UTF-8 remains an
error. Forced invalid UTF-8 remains an error, while `DetectSample` retains its
Latin-1 fallback behavior. Declared modeline encodings retain their existing
streaming behavior.

## Paired measurements

Windows 11 Pro 10.0.26300, Intel i9-12900HK, MSVC 19.44 RelWithDebInfo `/O2`,
x64 SSE4.2. Both implementations run in one binary, using the pre-change
ReadChunks/DetectSample fixtures and the current public reader. Five runs per
variant, local temporary files, warm OS cache, plain ASCII UTF-8, file variables
enabled, default validation-worker settings. Small-file batches contain 100
distinct 1,500-byte files. Large workloads repeatedly read one 32 MiB file.
The timed consumer verifies delivered bytes and lengths; it does not allocate
an editor document or append copies to an output string.

| Workload | Median elapsed before / after | Median probe elapsed bucket before / after | Logical source bytes before / after |
|---|---:|---:|---:|
| 100 small forced UTF-8 reads | 10.69 / 9.22 ms | 0.047 / 0.035 ms | 300000 / 150000 |
| 100 small automatic reads | 15.74 / 13.05 ms | 0.977 / 0.916 ms | 552400 / 300000 |
| 32 MiB forced UTF-8 reload | 22.74 / 21.18 ms | 0.002 / 0.002 ms | 34603024 / 33554440 |
| 32 MiB automatic read | 33.64 / 34.57 ms | 0.494 / 0.021 ms | 68158488 / 67109400 |

`probeMs` is elapsed time after excluding mapping, copying/ordinary reads and
UTF-8 validation charged to their existing buckets. It is not thread CPU time.
The benchmark retains the automatic large-file elapsed variation instead of
claiming a throughput improvement: the change reduces redundant probe work and
traffic, while the full-file passes dominate total runtime. Logical source
bytes include requested lookahead and repeated mapped views, rather than
physical storage reads. Every UTF-8 variant validates exactly the full payload
once, and the raw CSV records validation, decode and delivered byte totals.

No UI input-to-paint, independent peak private bytes or allocation counter was
instrumented. No temporary-file output is added by ReadChunks. Fixture creation
and CSV writes are outside the measured region. Raw results are in
`benchmarks/read-chunks-probe.csv`.

## Reproduce and verify

```
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
cmake-msvc-build/read_chunks_probe_benchmark.exe read-chunks-probe.csv
ctest --test-dir cmake-msvc-build -R fileio_roundtrip --output-on-failure
```

File-I/O checks cover forced probe-byte counts and invalid UTF-8 rejection,
DetectSample fallback, BOM-only/empty input, BOM-less UTF-16, an odd-length file
with an even probe, modelines whose last region overlaps the probe boundary,
first/last precedence, late invalid BOM UTF-8 without partial delivery, and the
existing multibyte sample/chunk boundaries and late legacy-byte fallback.
The file-search, lazy-document, large-file integration (12.77 s) and editor
integration (29.71 s) suites also pass.

IME, dragging and mixed-monitor DPI were not manually exercised; this change
does not alter visual layout.
