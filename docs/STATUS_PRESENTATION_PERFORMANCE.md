# Status presentation cache (#46)

Measured 2026-11-26 on Windows, Intel i9-12900HK, MSVC 19.44 x64,
RelWithDebInfo `/O2 /arch:SSE4.2`. One process holds one ordinary UTF-8 document
and uses both the frozen f9d684f UpdateStatus implementation and the changed
implementation. Five pairs alternate which variant runs first. Each side
performs 100 operations for caret movement, typing and unchanged status, giving
500 operations per variant/workload and 3,000 operations overall.

Captured conditions: 4,209 initial bytes, a first line of 200 ASCII characters,
a tab, CJK and emoji, then 1,000 `row` lines; 192 DPI; 1,254 x 690 client pixels;
zoom 100; Georgia line-number font; Segoe UI status font, height -24, weight 400;
English resource language 1033 and comma thousands separator. Plugins,
completion, occurrence marking and caption excerpts are disabled. Corpus,
selection and presentation caches reset outside measurement. Both paths use
warm fonts/data; no other build/test runs during measurement. There is no
application document I/O inside the measured operations.

Caret movement sends WM_KEYDOWN/VK_RIGHT, typing sends WM_CHAR, and unchanged
status sends WM_MOUSEMOVE followed by the same explicit coalesced document-change
notification. Timing starts before the input message and includes the normal
frame/editor notification path and status completion. Synchronous redraw covers
the frame, active editor, tabs and status bar. This is input-message-to-correct-
redraw timing; it does not include physical input delivery, queued-message
latency or compositor presentation. The unchanged-status workload is an explicit
refresh probe, not a claim that every mouse move normally requests a status update.

| Workload, ms: baseline / changed | Callback p50 | p95 | p99 | max | Correct paint p50 | p95 | p99 | max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Caret | 2.231 / 0.900 | 4.583 / 1.836 | 5.489 / 2.400 | 8.689 / 5.058 | 5.190 / 3.541 | 13.423 / 8.276 | 18.145 / 10.489 | 21.733 / 14.001 |
| Typing | 2.131 / 1.017 | 3.862 / 2.277 | 5.558 / 4.546 | 6.575 / 7.626 | 4.780 / 3.745 | 9.794 / 7.845 | 11.829 / 9.681 | 16.947 / 15.778 |
| Unchanged | 0.243 / 0.204 | 0.481 / 0.392 | 0.724 / 0.701 | 1.818 / 1.008 | 2.485 / 2.458 | 6.170 / 6.051 | 9.394 / 9.199 | 13.539 / 11.503 |

Percentiles pool the 500 operations per variant/workload and use nearest rank.
The five process-local pairs do not establish production tail latency; the
changed typing callback had a higher observed maximum. Unchanged-status paint
medians are close, so no material paint speedup is claimed there. Status
completion is recorded separately just after statistics become ready and the
native fields publish: medians were 2.231/0.900 ms for caret, 2.132/1.017 ms for
typing and 0.243/0.204 ms for unchanged status.

| Totals over 500 operations, baseline / changed | GDI text measurements | SCI_TEXTWIDTH | SB_SETTEXT | SB_SETPARTS | UI CPU, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Caret | 6500 / 1000 | 500 / 0 | 6500 / 1000 | 500 / 10 | 3031.250 / 2000.000 |
| Typing | 6500 / 1045 | 500 / 0 | 6500 / 1045 | 500 / 10 | 2375.000 / 2109.375 |
| Unchanged | 0 / 0 | 500 / 0 | 0 / 0 | 0 / 0 | 1359.375 / 1312.500 |

The baseline also queries the separator and reloads the format once per operation
in this corpus, versus zero timed queries/loads after the changed path warms.
UI CPU uses coarse GetThreadTimes deltas around input through redraw, excluding
snapshot comparisons and output. All cases report zero process read/write bytes.
Counters cover UpdateStatus's explicit GDI/Scintilla width calls and observed
status messages, rather than internal measurements performed by native controls.
Sampled private bytes span 11,399,168 to 11,476,992; whole-process peak commit is
24,371,200 bytes and includes fixtures/both variants. These are not isolated
cache allocations or a before/after memory-saving claim.

Raw operations are in [status-presentation.csv](benchmarks/status-presentation.csv)
and case summaries in [status-presentation-summary.csv](benchmarks/status-presentation-summary.csv).
Each operation compares all 13 native strings, drawing types, part edges and
line-number margin with the opposite variant. The CSV hash covers text and edges;
full equality assertions also compare types and margin. Exact document edits
and caret movement are checked. Measurements were rerun after fixing the font
fixture to reset the control's native default instead of restoring its discarded
default font handle; final capture requires a valid GetObject/font height.

Implementation reuses StatusStatistics. It caches only presentation inputs,
format arguments, text and extents. Resize reuses extents; parts publish only
when edges change, and text only when changed/invalidated. The status control
observes ANSI/Unicode text writes, drawing flags and parts from any caller,
including FindNext and NPPM_SETSTATUSBAR, while simple help part 255 stays separate.
The line-number key uses the complete line count, actual font name, size,
weight, italic, stretch, charset, quality, technology, locale, zoom and DPI.
It also repairs directly changed visible margin widths. DPI invalidation is
posted after parent handling so child controls can update their fonts first.

Localized format/locale data invalidate on language/settings changes. English
fields are only built on formatting failure or omitted fields. Regression
checks compare full baseline output for missing, partial and overflowing formats,
raw native/plugin writes and flags, simple mode, resize, actual font properties,
theme/font/zoom/DPI notifications, ready/pending statistics, multiple/rectangular
selections, active-document switches and mapped viewports. The test executable
compiles its own UpstreamUI object for counters/reference routing; the product
uses the ordinary static-library object and includes neither counters nor baseline.
MainFrame layout is identical in both builds.

Validation so far: optimized full wrapper build (418 targets), status_presentation
passed in 0.92 s, frame_refresh in 0.27 s and status_statistics in 0.23 s. The final
paired run also executes the complete presentation regression first and finishes
with all 30 summaries and no errors. Existing editor_integration fails at the
same early Dock layout line 74 already reproduced by the unchanged root control
for #48; that assertion was not changed. Physical mixed-DPI migration, IME and
compositor output were not tested. This change intends identical rendering, not
a new layout. The source received a clean non-author static review; final
integration validation is recorded by the delivery workflow.

After normal integration of master e22d140 (#48 and the canonical hash helper),
wrapper reconfigure/build passed. Targeted checks all passed in 5.91 seconds:
frame_refresh (0.67 s), status_presentation (0.68 s, including UTF-8/tab selection
byte/character distinction), edit_transforms (4.05 s), inventory coverage (0.18 s),
inventory hash regressions (0.09 s) and status_statistics (0.22 s). A focused
non-author review confirmed both features' test translation units, compile
definitions and CLI routes remain intact. The measured producer/benchmark did
not change, so the valid paired measurements were not repeated.
