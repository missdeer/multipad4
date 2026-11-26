# Block comment preparation (#49)

Exact selected delimiters are checked with bounded reads before searching for
surrounding markers. General searches grow from 1 KiB to 64 KiB chunks, retain
marker overlap and remain unbounded in distance. The original case-insensitive
comparison and language-specific newline handling remain unchanged.

Embedded-language detection requests styling only through the caret's relevant
line. Unwrapping styles only the necessary selection from Scintilla's valid
restart state; already styled prefixes are reused. The pinned colourise API's
line-end rounding is included when checking validity, so a partially dirty line
cannot leave an old delimiter style cached. Fully styled non-comment starts can
wrap directly without searching the document. Wrapping copies selected text,
rather than the entire document.

Style filtering reads the pinned planar layout (N styles, NUL, N text bytes,
NUL) in bounded chunks. ASCII advances locally; CR and non-ASCII positions use
the original native POSITIONAFTER semantics, including its CRLF skip. A small
regression gives CR and LF different styles with CR at a 64 KiB chunk boundary.

The ade667d implementation is retained as a test-only oracle. Tests send the
same WM_COMMAND through the frame/editor route and compare complete bytes,
selection/virtual space and undo results for cold/warm styles, embedded
languages, string markers, case-insensitive delimiters, language line markers,
dirty restart boundaries, NUL, Unicode, read-only and rectangular selections.

Validation: the first wrapper build encountered an external simd_tests.exe file
lock; its trailing Done/zero exit was correctly rejected. A normal incremental
wrapper retry completed the real links, followed by comment_commands passing in
3.98 seconds. Per the user's latest instruction, local cross-review and the
large paired benchmark are not delivery gates and the latter was not run. The
optional harness remains available; no speedup or strict time-budget claim is
made. A logical lexer line can still be indivisible.

Normal integration through master 57a2133 preserves comment, status and rich-text
test sources/definitions/CLI routes, including pre-COM private clipboard setup
and final restoration. The joint wrapper build completed actual links with no
compiler/FAILED/ninja-stop errors. Basic component and automatic inventory
checks passed: comment_commands 3.57 s, coverage 0.17 s, hash tests 0.09 s
(3/3 in 3.84 s). No large benchmark or additional peer gate was added.

After #52 merged, the actual CMake/main conflict was normally integrated through
075eab2 while preserving modeline/comments modes, test definitions and clipboard
setup/restoration. An external search_work_tests.exe post-link lock was rejected
as a failed build; normal incremental retry completed actual links. Basic checks
then passed 4/4 in 5.63 s: modeline_preferences 2.18 s, comment_commands 3.16 s,
inventory coverage/hash 0.19/0.09 s. The comment producer is unchanged.
