# Save encoding (#27)

Ordinary plain UTF-8 saves validate and move their captured text into the writer
without a second full-document copy. Other saves retain the first encoded buffer through the
atomic chunk writer. Confirmed legacy conversion retains the approved lossy
bytes rather than encoding the replacement text again. An edit or encoding
change during the confirmation cancels preparation. Large Unicode saves remain
bounded to 16 MiB chunks; UTF-8 chunks validate and borrow their existing owned
storage, and UTF-16 output is sized once from its UTF-16 unit count. Large legacy
saves use the full preparation/confirmation path so that no destructive linked
write begins before approval. This can require whole-document memory for legacy
encodings.

## Reproduction

Build with the repository's `cmake-reconfigure` / `cmake-build` wrappers, then run:

```powershell
.\cmake-msvc-build\fileio_tests.exe --benchmark-save before
.\cmake-msvc-build\fileio_tests.exe --benchmark-save after
ctest --test-dir cmake-msvc-build --output-on-failure -R 'fileio_roundtrip|lazy_document|file_search|simd_boundaries'
ctest --test-dir cmake-msvc-build --output-on-failure -R 'editor_integration|large_file_integration'
```

The paired benchmark reproduces the previous discarded-validation-encoding plus
`WriteAtomic` path and the new one-encoding plus `WriteAtomicChunks` path in the
same executable. Both sides use the new UTF-16 sizing, isolating the removed
encoding pass. It copies the source as editor `Text()` preparation does, writes
and flushes an actual destination, and checks decoded output after every run.
It does not include lexer refresh, plugin notifications, or input-to-first-paint
latency. Reported duration is the synchronous save core, not a UI latency claim.

## Local measurements, 2026-11-25

Windows x64, MSVC 19.44, RelWithDebInfo `/O2`, SSE4.2, local temporary directory on
NTFS. Ordinary editor save simulation, one 16,777,228-byte UTF-8 source with
short CRLF lines containing `café`, no plugins. One warm-up and eleven retained
warm-cache samples per encoding. Other issue work was running on the machine;
these are local measurements with scheduling noise, not isolated CI thresholds.
The p95/p99 nearest-rank sample is the maximum for eleven samples.

| Output | Median ms before / after | p95/p99/max ms before / after | Median CPU ms before / after | Process peak private MiB before / after |
|---|---:|---:|---:|---:|
| UTF-8 | 50.39 / 43.73 | 53.66 / 49.61 | 46.88 / 31.25 | 72.36 / 72.31 |
| UTF-16LE BOM | 262.30 / 162.63 | 316.09 / 192.17 | 250.00 / 156.25 | 118.76 / 118.71 |
| Windows-1252 | 381.76 / 214.31 | 401.49 / 277.57 | 359.38 / 187.50 | 166.03 / 165.99 |

Successful ordinary encoder invocations fall from two to one by construction.
Output bytes per save are respectively 16,777,228, 32,671,446 and 16,335,722;
the atomic temporary-file payload is unchanged. Peak private bytes are the
process lifetime peak (including output verification and earlier encoding
cases), not per-save allocations. The UTF-8 chunk regression checks
that the consumer receives the original storage address; it also verifies that
an incomplete UTF-8 sequence delivers zero bytes.

After review, the plain UTF-8 ownership fix was measured with the same paired
executable: median before/after 65.46/41.53 ms, p95/p99/max 70.13/47.41 ms,
median CPU 62.50/46.88 ms, and process lifetime peak private 72.32/56.28 MiB.
These samples ran during other worktree builds and remain separate from the
initial table. Other encoding paths were unchanged; second-run medians were
299.26/220.39 ms (UTF-16LE) and 418.71/340.00 ms (1252). The UTF-16 baseline
had a 4345.46 ms scheduling/storage outlier under that load.

File regressions exercise malformed input, BOM/empty/NUL conversions, strict
legacy rejection, failed producers, hard-link identity, partial-write rollback,
sharing locks, read-only destinations and alternate streams. Dialog regressions
cover cancelled conversion, read-only Save Copy, approved conversion and undo,
and an edit dispatched during confirmation. Existing large-file tests cross
UTF-8 chunk boundaries in Unicode streaming saves.

Input-to-first-paint timing, exact heap allocation counts and physical storage
model were not measured. No visual layout changed.
