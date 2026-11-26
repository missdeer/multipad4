# RTF export performance

Issue #50 replaces per-character RTF queries and conversions with bounded planar styled-text reads and cached style controls. The pinned Scintilla fork returns `N styles + NUL + N text bytes + NUL`; both passes allocate `2N + 2` with an overflow check and reject short reads before changing the clipboard. Blocks end at native character boundaries. The renderer converts each block once and emits runs while preserving the original font/color/control order, tabs, supplementary characters, and EOL-filled backgrounds.

## Baseline and measurement

The test-only baseline is the complete `src/EditRichText.cpp` from `e22d1406ea76e7198905d201033b780331ed0a94` (original file SHA256 `71FF147912297275F858F1CE71955D43CC1877C44585967259D9AB9A1B1AEFC1`). Its notices remain; only exported function names and equivalent conversion counters change. Both implementations run in the same optimized executable through the same synchronous `WM_COMMAND` dispatch. No product preference or new dependency selects the baseline. `BUILD_TESTING=OFF` excludes the baseline and instrumentation.

`--rich-text-test` runs the existing editor feature/scope checks plus 31 paired clipboard cases. `--rich-text-benchmark` additionally runs three alternating before/after pairs for each workload. It is a separate optional entry, not part of default smoke timing. Each pair compares complete RTF bytes, explicit-length UTF-16 clipboard bytes including the terminator and content after embedded NULs, enumerated clipboard formats, document bytes, and every selection/caret/anchor/virtual-space field. Comparisons and setup occur outside timing. A failed styled read must leave the original clipboard intact.

Timing starts immediately before the real `WM_COMMAND`. Callback time ends when command dispatch returns; correct-redraw completion ends after the document notification and synchronous `RedrawWindow(...RDW_UPDATENOW|RDW_ALLCHILDREN)` return. This measures native paint completion on the private desktop, not physical screen presentation or compositor latency. UI CPU uses the command thread's kernel/user counters. A 1 ms sampler records process private bytes; this can miss shorter peaks. API counters measure style/attribute/position queries, planar bytes returned, application text-range bytes requested, and explicit UTF-8 conversion calls/bytes. These are not a global allocator/copy census. The commands perform no temporary-file I/O.

The executable first switches to a logon-scoped noninteractive window station and desktop, before COM or HWND creation, and verifies it is not `WinSta0` before opening any clipboard. It snapshots/restores that station's existing formats and fails before writing if a format cannot be duplicated. The interactive user's clipboard is never opened. The window station restriction also means physical keyboard, IME, clipboard-history UI, interprocess clipboard contention, and mixed-monitor presentation are not measured.

Environment: Windows 11 Pro build 26300, Intel Core i9-12900HK (14 cores/20 logical processors), MSVC 19.44, C++20, RelWithDebInfo `/O2 /Ob1 /arch:SSE4.2`. Both sides use the same in-memory UTF-8 full-editor document, one measured tab, plugins disabled, isolated nonpersistent English test settings, no wrapping, tab width 4, occurrence/completion work disabled, explicit style/font/color fixtures, and warmed in-process code/data. There is no filesystem cold-cache comparison. All other team builds/tests are paused for formal measurement.

## Compatibility checks and limits

Cases cover plain text without a style buffer; dense styles; embedded NULs; escaped braces/backslashes; UTF-8 and CRLF crossing block boundaries; supplementary characters whose continuation bytes carry different styles; tabs rendered as tabs or spaces; EOL-filled backgrounds; empty documents and disabled empty selections; forward/backward partial selections including individual CR/LF bytes; rectangular and multiple selections; and Copy All.

There is one intentional correction to old output: the old renderer calls native `POSITIONAFTER` on CR, which already advances over CRLF, then skips an additional following LF. For source `CRLF + LF + CR`, it emits two source line breaks instead of three. The new renderer preserves all three. This case uses an independent RTF-body parser: the old body has three paragraph/line controls including the mandatory terminal paragraph, the new body has four; header/palette, full Unicode bytes, clipboard formats and editor state remain identical. All other paired cases require exact RTF byte equality. This exception preserves input rather than reproducing the old missing blank line.

Plain Copy All retains its original `Text()` plus `SCI_COPYTEXT` implementation. `SCI_COPYRANGE` was evaluated but is not part of this change. In this pinned fork both native APIs pass through `SelectionText::FixSelectionForClipboard`, which substitutes spaces for NULs; the Copy All regression checks that original behavior and all subsequent bytes. RTF export separately retains NUL as `\u0?` and preserves the NUL in its explicit-length Unicode payload. Copy All measurements are an unchanged control, not an optimization claim.

## Isolated results (2026-11-26)

All 24 samples completed successfully with empty stderr and exact paired output/state checks. Raw samples are in [RICH_TEXT_PERFORMANCE_SAMPLES.csv](RICH_TEXT_PERFORMANCE_SAMPLES.csv). Times below are milliseconds. Three samples give a median but do not establish a tail distribution: nearest-rank p95 and p99 both equal the longest sample listed.

| Workload | Input bytes | Median callback, old → new | Longest callback / p95 / p99, old → new | Median correct redraw, old → new | Longest redraw / p95 / p99, old → new | Median UI CPU, old → new |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| One long ASCII run + CRLF | 1,048,578 | 3572.267 → 13.502 | 3588.641 → 13.528 | 3574.600 → 16.109 | 3591.053 → 25.323 | 3312.500 → 15.625 |
| Dense style changes, escapes and tabs | 229,376 | 1860.708 → 17.948 | 1897.917 → 18.923 | 1863.203 → 20.320 | 1900.615 → 21.142 | 1703.125 → 15.625 |
| Mixed UTF-8/supplementary characters/CRLF/tabs | 491,520 | 2222.742 → 110.383 | 2225.998 → 134.550 | 2225.106 → 112.829 | 2228.351 → 137.261 | 1968.750 → 109.375 |
| Unchanged Copy All control | 8,388,614 | 18.486 → 18.737 | 20.124 → 19.273 | 21.540 → 21.865 | 24.506 → 22.471 | 15.625 → 31.250 |

The RTF commands remain synchronous. In particular, the multibyte/dense-EOL case can still hold the UI thread for 135 ms; this change does not add input yielding or promise a frame budget. CPU counters on this machine have coarse 15.625 ms increments, which also explains the unchanged Copy All control's apparent CPU difference.

| RTF workload | Style-position queries, old → new | Style attribute queries, old → new | Native position queries, old → new | Explicit UTF-16 conversions, old → new | Converted input bytes, old → new | New planar API bytes (two passes) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Long run | 2,097,154 → 0 | 18 → 20 | 2,097,154 → 32 | 1,048,579 → 19 | 2,097,170 → 1,048,594 | 4,194,380 |
| Dense styles | 458,752 → 0 | 1,146,912 → 80 | 458,752 → 6 | 196,617 → 12 | 426,036 → 229,428 | 917,520 |
| Multibyte | 557,056 → 32,768 | 1,302,560 → 80 | 524,288–524,319 → 14 | 196,617 → 16 | 884,788 → 491,572 | 1,966,112 |

The remaining multibyte style queries are the original per-line EOL-background lookup. Planar reads transfer both styles and text twice (about four input bytes per source byte), trading bounded bulk copies for millions of individual calls and allocations. Full-path text-range counts in the raw data can include notification/paint work; they are not presented as isolated exporter copy counts. The long-run attribute count increases slightly because the style cache fetches attributes eagerly, while dense workloads avoid repeated per-run lookups.

Median sampled process peak private bytes, old → new: long run 34,250,752 → 36,818,944; dense styles 39,964,672 → 50,008,064; multibyte 41,971,712 → 56,049,664; unchanged Copy All 199,335,936 → 216,842,240. Allocation reuse and alternating order affect absolute process peaks. There is no general memory-reduction claim: the exporter retains its complete RTF/Unicode payloads and uses a bounded styled buffer plus cached controls. Per-sample before/peak values are retained in the CSV.

Validation: actual wrapper reconfigure/full build succeeded, followed by incremental links with no `FAILED`/compiler errors; the focused CTest entry passed all 31 paired cases in 4.66 s, and the formal run independently repeated them before the 24 performance rows. Existing editor feature and edit-scope checks execute inside that entry. The canonical lexer inventory coverage gate passes without metadata changes. Broader physical clipboard/IME/DPI checks remain outside this isolated test; the private station protects the user's interactive clipboard.

Final integration with `b25a9ba` preserves the tab/status and RTF test definitions and command-line routes. Reconfigure succeeded. The first build encountered an external file lock on `regex_iterator_baseline.exe`; a normal incremental retry completed all 59 remaining steps and linked the current executables without compiler/link errors. Integrated `rich_text` passed in 4.82 s and both inventory gates passed (3/3, 5.15 s total). The RTF product, baseline and tests are unchanged from the measured version; performance samples were not repeated. Local cross-review was waived by the user's final instruction; no completed independent review is claimed.
