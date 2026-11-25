# Ordinary open analysis (#40)

ASCII classification now runs only when the detected encoding is plain UTF-8
and a preferred extension/shebang or ASCII-as-UTF-8 preference needs the answer.
Preferred extension and shebang decisions still apply when ASCIIAsUTF8 is on.
Encoding declarations retain their existing precedence.

The successful staged load publishes the line-ending histogram it already
collects across chunk boundaries. The warning reuses it only while both the
Scintilla document pointer and edit revision still match; otherwise it performs
the original fresh scan. Failed or replaced loads cannot publish a histogram
for a different editor document.

## Reproduce

Build with the repository wrappers, then run:

```powershell
.\cmake-msvc-build\open_analysis_benchmark.exe
ctest --test-dir cmake-msvc-build -R 'fileio_roundtrip|editor_integration' --output-on-failure
```

The benchmark isolates repeated post-load analysis, after both variants have
already obtained the staged histogram. It models a decoded document whose
encoding does not use ASCII classification and whose mixed-EOL warning needs
counts. It compares the former ASCII scan plus full EOL rescan with the retained
histogram. It does not time disk read, decoding, editor insertion, language
configuration, warning presentation, user input or first paint.

Windows x64, MSVC 19.44, RelWithDebInfo `/O2`, SSE4.2; 67,108,894 bytes of short
mixed CRLF/LF/CR lines in warm memory, 21 samples per variant, same binary and
corpus. Other worktree builds ran on the same machine. The histogram checksum is
5,921,373 in both paths.

| Variant | Median ms | p95 ms | Maximum ms | Repeated scan bytes |
|---|---:|---:|---:|---:|
| Before | 14.4085 | 17.9086 | 18.5731 | 134,217,788 |
| After | 0.0001 | 0.0001 | 0.0002 | 0 |

The histogram-copy timing is near clock overhead; no whole-open speedup is
inferred from it. The default plain UTF-8 path with ASCIIAsUTF8 enabled also
avoids classification when no preferred encoding uses it.

Validation: optimized build, fileio_roundtrip (3.69 s) and editor_integration
(24.42 s) passed. Added checks cover CRLF split between load chunks, mixed-EOL
counts, warning refresh after an edit invalidates the load histogram, and
preferred extension/shebang behavior with ASCIIAsUTF8 enabled. Existing dialog
checks verify normalization, cancel, read-only warnings and chosen EOL mode.

UI latency, CPU/private-memory peaks and manual IME, dragging or mixed-DPI
interactions were not measured. The benchmark reports only the eliminated
repeated analysis.
