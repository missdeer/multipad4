# Line transformation preparation (#48)

The maximum display-width pass now runs only for alignment. Sorting, duplicate
filtering and column wrapping retain their own necessary conversions, without
first converting every line just to compute an unused maximum. Entry selection
text and its output copy are deferred until a command needs them. The line loop
moves each processed payload into the result; it keeps the next original line
available for paragraph justification and saves the terminal EOL before moving
anything. Sorting no longer rereads and splits the selection to recover that EOL.
The same saved value covers duplicate filtering and the final output path.

The tab/UTF-16/supplementary-character rules, positional sort EOLs, stable sort
keys, selection expansion, rectangular sort and undo semantics are retained.
No whitespace-command or padding algorithm was changed. The GUID creation
failure path still retains the original selected bytes.

## Oracle and exact behavior checks

`tests/EditAdvancedBaseline.cpp` retains the implementation and notices from
`src/EditAdvanced.cpp` at f9d684fd5d6f941b37b51c255451791a079e4650. The original
checkout's SHA256 was
`83BB06939652125B9B01956C014ADDA555B114864273C3F93AF5F7AD0F6D9F6A`.
Only four exported names and test instrumentation differ from that frozen
source. The baseline is compiled exclusively into multipad4-tests.exe.
BUILD_TESTING=OFF excludes its routing pointer, counters and baseline code.

Both variants receive the same actual WM_COMMAND and run through
MainFrame::OnCommand and ExecuteEditorCommand. A test-only dispatch hook selects
the old implementation at the ExecuteAdvancedEdit boundary. Both therefore
include the same frame operation, native options dialog, editing notifications
and frame refresh. Dialog controls are set through the existing EditDialogTest
timer; cancelled dialogs are also checked.

The dedicated `edit_transforms` check runs the existing RunEditScopeTests and
RunEditDialogTests, then 26 paired cases: LF/CRLF/CR and missing terminal EOL,
both duplicate filters and an empty result, partial-line wrap, tab/emoji text,
surrogate wrapping at one UTF-16 column, backwards/line-boundary selections,
rectangular column sort, current-line alignment, all five alignment modes and
dialog cancellation. It compares exact complete document bytes and all relevant
selection positions/modes/virtual spaces after the command and after one undo.
Undo must restore the input bytes. The million-line pairs use the same exact
comparisons; correctness is not inferred from a hash.

## Million-line paired measurements

Measured 2026-11-26 on Windows, Intel i9-12900HK, MSVC 19.44 x64 RelWithDebInfo
`/O2 /arch:SSE4.2`. Other worktree builds/tests were paused. One ordinary UTF-8
editor tab holds 1,000,000 terminated content lines (11,889,000 input bytes):
`" " + decimal((1000000-i)%10000) + "\t😀x\n"`, for i=0 through 999999.
Tab width is four, EOL mode LF, wrap mode none, plugins and autosave disabled,
undo selection history enabled, and the full text selected. The document uses
the test application's default language configuration on both sides. The source
corpus and configuration are identical within each pair; this run did not log a
separate lexer-identity field.

Three pairs per operation use the same optimized binary and process, alternating
before/after order between pairs. Input loading, selection setup, correctness
capture and undo are outside timing. Callback time starts immediately before
WM_COMMAND and ends when command dispatch returns, including the dialog timer
and editing. Redraw time includes a subsequent WM_DOCUMENT_CHANGED and
synchronous RedrawWindow. It measures message-to-correct-application-redraw,
not physical input delivery or compositor presentation. The transformations
finish synchronously; their final bytes and selection are checked outside the
timed interval. There is no claim about every intermediate frame's pixels.

| Operation | Callback median ms before → after | Callback p95/p99/max ms | Correct redraw median ms | Correct redraw p95/p99/max ms | UI CPU median ms |
|---|---:|---:|---:|---:|---:|
| Align right | 773.795 → 710.936 | 777.730 → 759.994 | 778.322 → 715.627 | 782.215 → 765.160 | 765.625 → 703.125 |
| Stable ascending sort | 754.180 → 512.879 | 941.363 → 626.667 | 758.729 → 518.225 | 951.785 → 631.642 | 734.375 → 515.625 |
| Merge duplicate lines | 308.564 → 199.775 | 447.379 → 203.691 | 311.118 → 202.285 | 450.497 → 206.475 | 312.500 → 187.500 |
| Wrap at column seven | 482.275 → 353.737 | 495.573 → 354.830 | 487.111 → 358.428 | 500.594 → 359.750 | 484.375 → 343.750 |

With only three samples per side, nearest-rank p95/p99 equal the maximum and do
not establish tail-latency behavior. GetThreadTimes CPU accounting is coarse.
Raw per-pair rows are in [edit-transforms.csv](benchmarks/edit-transforms.csv).

All changed variants request one 11,889,000-byte selected range instead of two
for alignment/dedup/wrap or three for sorting. Sorting also splits once instead
of twice. The explicit eager output-copy and per-line copy counters each fall
from 11,889,000 payload bytes to zero. These counters cover those specified
copies, not all allocations/copies in the editor, sorting keys or output builder.

Display-width calls remain 1,000,000 for alignment and fall to zero for the other
three operations. Total measured UTF-8→UTF-16 calls are 5,000,000 on both sides
for alignment, 2,000,000→1,000,000 for sorting and wrapping, and 1,000,000→0 for
duplicate merging. Necessary sort-key and wrap conversions are preserved.
Instrumentation is active on both sides of this test build, so these timings
are not an uninstrumented release comparison.

A sampler requests one-millisecond intervals during command/redraw, before
correctness snapshots are taken. Maximum sampled private bytes before/after
were 268,943,360/255,188,992 for alignment, 498,421,760/438,685,696 for sort,
224,256,000/220,782,592 for dedup, and 295,088,128/260,108,288 for wrapping.
Samples can miss shorter peaks; process allocator reuse and earlier cases affect
these process-wide figures. They are not isolated per-operation memory savings.
No corpus/temporary-file I/O is performed in the timed region; CSV output is
written after each measured operation. Physical disk traffic was not instrumented.

## Validation and reproduction

The full wrapper build passed. The original 26-case paired check passed in
3.18 s; after adding the existing EditScope/EditDialog helpers, `edit_transforms`
passed in 4.68 s. All 24 million-line variant rows passed exact state/undo
comparison and the test process exited zero within its external 15-minute cap.

`editor_integration` failed before reaching document-edit tests at the existing
FrameworkCompatibilityTests.h:74 docking assertion. Two issue-worktree attempts
and the unchanged root executable all failed at that same assertion in about
0.2 s. That assertion was not modified or relaxed. The dedicated entry executes
the affected existing edit checks without that unrelated docking prerequisite;
the full editor suite is not claimed green for this run.

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R '^edit_transforms$' --output-on-failure
# Optional million-line run; keep it separate from default smoke-test timeouts.
.\cmake-msvc-build\multipad4-tests.exe --edit-transform-benchmark
```

Physical keyboard/IME, dragging and mixed-DPI interactions were not manually
verified. No visual layout change is intended.
