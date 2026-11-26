# Frame refresh with 500 tabs

Measured 2026-11-26 for issue #45, using MSVC19.44 x64 RelWithDebInfo `/O2 /arch:SSE4.2` on Intel i9-12900HK and Windows. The same executable contains both the original frame-refresh/coalescing logic and the optimized path. A single process holds the same 500 native MDI/Scintilla tabs for both paths; corpus and publication state reset before each case, outside measurement. The baseline runs first. The revised input-message run used an isolated interval with other worktree builds and tests paused. It is one paired run; small differences are not treated as established improvements.

The active document contains 20,211 UTF-8 bytes: a short first line plus 200 lines of 100 ASCII characters, with LF endings. Other documents are empty. There is no document-file I/O in the benchmark, no plugins, and the main frame is visible. Window creation and cleanup are outside timing; creating 500 native MDI children is expensive and dominates wall-clock harness setup.

Each case performs 100 operations. Typing sends WM_CHAR to the real editor window; scrolling sends WM_VSCROLL/SB_LINEDOWN. Tests require exactly 100 inserted characters or 100 scrolled lines. Callback timing starts after that input message and covers the coalesced refresh. Input-to-paint timing starts before WM_CHAR/WM_VSCROLL and ends after synchronous painting of the frame, active editor, tab strip and status bar. Inactive editor windows are not forcibly repainted. This measures synchronous message-to-correct-redraw completion, including the real editor input/notification path. It does not measure physical keyboard/IME delivery, queued-message latency, compositor presentation, or any later background work.

The baseline helper retains the original title-vector construction, per-tab title/window-text work, caption publication and excerpt/full-refresh decision. Both paths share the current status, menu-state and toolbar helpers, so the measurements isolate the global tab/caption work; they do not claim to measure the separate removal of read-only attribute queries. The optimized path enters through WM_DOCUMENT_CHANGED, while the baseline helper invokes the copied coalescing/refresh logic directly.

Matching cases assert exact serialized state equality across all document bytes,
ordered tab titles and published-document IDs/titles, native frame caption,
native menu enabled/checked state, toolbar state, status text, caret/anchor,
viewport and dirty state. Reported hashes identify this complete serialization;
they are not the correctness assertion. Percentiles are nearest-rank over 100
interactions. Times are milliseconds; UI CPU uses GetThreadTimes and its coarse
accounting resolution. Caption writes count actual WM_SETTEXT messages.

| Excerpt | Operation | Path | Callback median | p95 | p99 | Longest callback | Input→paint median | p95 | p99 | UI CPU total | Caption writes |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| off | scrolling | baseline | 0.221 | 0.481 | 5.152 | 8.330 | 4.640 | 9.099 | 13.454 | 296.875 | 1 |
| off | typing | baseline | 1.843 | 3.648 | 4.385 | 21.753 | 6.705 | 10.870 | 13.298 | 546.875 | 1 |
| on | scrolling | baseline | 5.107 | 7.827 | 9.782 | 10.009 | 8.483 | 10.986 | 13.951 | 671.875 | 100 |
| on | typing | baseline | 7.429 | 11.908 | 12.562 | 21.339 | 11.755 | 18.570 | 21.316 | 953.125 | 100 |
| off | scrolling | optimized | 0.220 | 0.395 | 2.475 | 3.972 | 3.968 | 8.562 | 9.947 | 250.000 | 0 |
| off | typing | optimized | 1.730 | 3.721 | 4.947 | 19.830 | 6.278 | 10.053 | 13.239 | 484.375 | 1 |
| on | scrolling | optimized | 2.380 | 3.978 | 5.545 | 5.860 | 5.685 | 8.474 | 10.335 | 468.750 | 0 |
| on | typing | optimized | 5.124 | 7.880 | 9.913 | 19.369 | 9.096 | 12.768 | 16.826 | 796.875 | 100 |

With excerpt enabled, median callback time fell from 5.107 to 2.380 ms while
scrolling and 7.429 to 5.124 ms while typing. Message-to-redraw median fell from
8.483 to 5.685 ms and 11.755 to 9.096 ms. Caption writes during scrolling fell
from 100 to zero. Typing still publishes changed visible excerpts. Excerpt-off
scrolling was 0.221 versus 0.220 ms; no meaningful speedup is claimed.

Peak private commit across the shared process was 78,675,968 bytes, including
500-tab setup and both variants. A shared monotonic peak cannot establish
before/after memory savings. Excerpt decoding reads at most 124 bytes when its
first line or mapped viewport changes. Ordered publication rebuilds on
insert/remove/reorder. File metadata queries, physical disk traffic and allocator
copy totals were not instrumented; this in-memory benchmark does not measure
the separate file-attribute-cache optimization.

## Behavior checks

`frame_refresh` covers ordered document publication, activation notification order, inactive tab text/dirty state, reorder, unchanged refresh notifications, native dynamic-menu state between popup openings, restoring a plugin-mutated enabled bit, excerpt stability on selection/scroll changes, invalidation on text and mapped viewport changes, cached read-only attributes and a command that refreshes older cached attributes before toggling the file.

Main-menu command states remain synchronized at the previous full/excerpt-refresh points. Popup handling still evaluates dynamic entries; no command that plugins can observe earlier was moved to popup-only evaluation. Unchanged native enabled/checked states avoid redundant writes. Read-only file state comes from open/save/snapshot/watch metadata; the explicit toggle queries current attributes before acting, preserving command correctness without a share query in the typing refresh path.

The original excerpt extraction and formatting are retained. There is no intended visual layout change. Final wrapper build and `git diff --check` pass. `frame_refresh` passed in 0.62 s and full `editor_integration` passed in 20.91 s. Slow/offline SMB behavior, physical input/IME, and compositor-present latency were not measured.

After integrating master fb8344d on 2026-11-26, the optimized wrapper build,
file_search, directory_watch, and fileio_roundtrip passed. The isolated GUI
checks passed again: frame_refresh in 0.61 s and editor_integration in 21.39 s.
Tests and the paired benchmark now run through multipad4-tests.exe, keeping
their code out of the production executable.

After the subsequent issue #44 integration (master cd6f299), the complete
wrapper reconfigure/build passed. Both test dispatch paths remain valid:
indent_read passed in 0.76 s, frame_refresh in 0.24 s, and editor_integration
in 24.66 s. This integration changes test dispatch and unrelated auto-indent;
the frame optimization and measured input benchmark code remain unchanged.

## Reproduction

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R '^(frame_refresh|editor_integration)$' --output-on-failure
# Measures baseline then optimized against the same 500 tabs.
.\cmake-msvc-build\multipad4-tests.exe --frame-refresh-benchmark baseline | Out-Host
# Reverse order if desired; this creates a fresh 500-tab process.
.\cmake-msvc-build\multipad4-tests.exe --frame-refresh-benchmark optimized | Out-Host
```

Raw controlled WM_CHAR/WM_VSCROLL samples (the earlier SCI_* stimulus run is superseded):

```text
baseline,excerpt=0,scrolling,tabs=500,callback_median_ms=0.221,callback_p95_ms=0.481,callback_p99_ms=5.152,callback_max_ms=8.330,paint_median_ms=4.640,paint_p95_ms=9.099,paint_p99_ms=13.454,ui_cpu_ms=296.875,peak_private=78352384,caption_writes=1,state_hash=5391539885810548162
baseline,excerpt=0,typing,tabs=500,callback_median_ms=1.843,callback_p95_ms=3.648,callback_p99_ms=4.385,callback_max_ms=21.753,paint_median_ms=6.705,paint_p95_ms=10.870,paint_p99_ms=13.298,ui_cpu_ms=546.875,peak_private=78401536,caption_writes=1,state_hash=9575037178042638721
baseline,excerpt=1,scrolling,tabs=500,callback_median_ms=5.107,callback_p95_ms=7.827,callback_p99_ms=9.782,callback_max_ms=10.009,paint_median_ms=8.483,paint_p95_ms=10.986,paint_p99_ms=13.951,ui_cpu_ms=671.875,peak_private=78454784,caption_writes=100,state_hash=10873341755711398289
baseline,excerpt=1,typing,tabs=500,callback_median_ms=7.429,callback_p95_ms=11.908,callback_p99_ms=12.562,callback_max_ms=21.339,paint_median_ms=11.755,paint_p95_ms=18.570,paint_p99_ms=21.316,ui_cpu_ms=953.125,peak_private=78532608,caption_writes=100,state_hash=4463381654263453525
optimized,excerpt=0,scrolling,tabs=500,callback_median_ms=0.220,callback_p95_ms=0.395,callback_p99_ms=2.475,callback_max_ms=3.972,paint_median_ms=3.968,paint_p95_ms=8.562,paint_p99_ms=9.947,ui_cpu_ms=250.000,peak_private=78626816,caption_writes=0,state_hash=5391539885810548162
optimized,excerpt=0,typing,tabs=500,callback_median_ms=1.730,callback_p95_ms=3.721,callback_p99_ms=4.947,callback_max_ms=19.830,paint_median_ms=6.278,paint_p95_ms=10.053,paint_p99_ms=13.239,ui_cpu_ms=484.375,peak_private=78675968,caption_writes=1,state_hash=9575037178042638721
optimized,excerpt=1,scrolling,tabs=500,callback_median_ms=2.380,callback_p95_ms=3.978,callback_p99_ms=5.545,callback_max_ms=5.860,paint_median_ms=5.685,paint_p95_ms=8.474,paint_p99_ms=10.335,ui_cpu_ms=468.750,peak_private=78675968,caption_writes=0,state_hash=10873341755711398289
optimized,excerpt=1,typing,tabs=500,callback_median_ms=5.124,callback_p95_ms=7.880,callback_p99_ms=9.913,callback_max_ms=19.369,paint_median_ms=9.096,paint_p95_ms=12.768,paint_p99_ms=16.826,ui_cpu_ms=796.875,peak_private=78675968,caption_writes=100,state_hash=4463381654263453525
```
