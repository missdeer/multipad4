# Automatic completion preparation (#43)

Measured 2026-11-26 on Windows, Core i9-12900HK, MSVC 19.44,
RelWithDebInfo `/O2`, SSE4.2, stock C++ lexer, document-word scanning and
plugins disabled. All compared paths run in one optimized executable with the
same configuration and corpus, warm process/cache, UTF-8 ordinary documents,
one active test document and the test frame's existing tabs. No storage I/O is
performed in the measured callback. Other worktree builds and tests were stopped
for the final run.

`MULTIPAD4_COMPLETION_BENCHMARK=1 ctest --test-dir cmake-msvc-build -R
'^ordinary_search_storage$' -V` produces eleven paired samples for each corpus.
The short-line corpus is 50,000 `int value;` lines followed by `in` (550,002
bytes); the long-line corpus is one comment containing 550,000 spaces and `in`
(550,004 bytes). Both sides reset endStyled to zero before timing. The baseline
performs the former synchronous styling prerequisite and the same candidate
generation. The changed input callback queues continuation work. The paint
comparison dispatches those posted messages before WM_PAINT, matching Win32's
normal ordering, and includes all that work in time to first correct paint.
Every measured UpdateWindow must return with endStyled at or beyond the caret;
every pair records one WM_PAINT. Candidate selection is checked against the
baseline. This is an in-process notification-to-paint measurement, not physical
keyboard input latency.

| Eleven paired samples, ms | Synchronous prerequisite | Posted prerequisite |
| --- | ---: | ---: |
| Short lines: input callback median / max | 216.339 / 225.134 | 0.042 / 0.108 |
| Short lines: first correct paint median | 218.754 | 222.803 |
| Short lines: first correct paint p95 / p99 / max | 227.874 | 228.267 |
| Short lines: completion median / max | 218.806 / 227.883 | 223.003 / 228.278 |
| Short lines: longest continuation callback | -- | 12.821 |
| Long line: input callback median / max | 706.024 / 719.711 | 0.048 / 0.053 |
| Long line: first correct paint median | 709.390 | 708.613 |
| Long line: first correct paint p95 / p99 / max | 723.236 | 743.181 |
| Long line: completion median / max | 709.398 / 723.245 | 708.621 / 743.190 |
| Long line: longest continuation callback | -- | 740.532 |

Percentiles use nearest rank; with eleven samples p95 and p99 both equal the
maximum. Raw rows, including CPU deltas and styled positions, are in
[completion-paint.csv](benchmarks/completion-paint.csv). There is no overall
first-paint speedup claim: short-line slices add about four milliseconds median,
and the long-line tail varied. Returning from the original input callback sooner
does not imply that all UI callbacks are short.

The earlier passive-paint design was rejected. An isolated eleven-pair run
measured correct first paint at 220.650 -> 263.280 ms median, with p95/p99/max
240.947 -> 271.256 ms. It allowed Paint to synchronously style the whole distant
gap. Both paths had one WM_PAINT, but styling during Paint adds its notification
and layout bookkeeping. The final implementation styles outside Paint through
coalesced posted continuations. The earlier raw rows remain in
[completion-paint-before-posting.csv](benchmarks/completion-paint-before-posting.csv).

Normal continuations request 4 KiB without crossing into a long logical line.
Only one continuation message is queued at once. Both admission and dispatch
check hardware input and queued keyboard messages; input takes precedence and
the existing 30 ms timer provides a retry. No document pointer crosses the
message queue. Every slice checks current caret/token, revision, focus epoch,
IME, read-only state and configuration/language/dialect/theme ownership.

A logical line longer than 64 KiB cannot be split safely by the existing lexer.
It runs once outside Paint after the originating input callback returns. The
740.532 ms continuation above is a real UI stall. This change does not impose
a hard one-millisecond deadline; interruptible lexer work would be needed for
that. Explicit completion retains synchronous behavior.

A separate consecutive-slice comparison isolates preparation overhead from timer
cadence. Eleven pairs measured synchronous/deferred callback medians
212.088/0.038 ms, maxima 215.347/0.225 ms, deferred total median/max
217.011/221.213 ms and longest slice 7.680 ms. Summed UI-thread CPU deltas were
2265.625/2156.250 ms (GetThreadTimes has coarse accounting); copied prefix bytes
22/44 and SCI_COLOURISE calls 11/1485. See
[completion-slices.csv](benchmarks/completion-slices.csv). These consecutive calls
are not physical input latency or the real timer cadence.

Steady, already styled completion with forced keyword-cache miss versus reuse
measured medians 4.212/4.447 ms and maxima 5.517/5.337 ms. The forced miss also
constructs the new owned cache, so this is not a byte-for-byte old implementation
baseline. There is no steady-time speedup claim. Cached string payload was
70,838 bytes excluding allocation overhead. Sampled process private bytes were
14,512,128 and peak process commit 24,002,560; these are whole-process counters,
not isolated cache allocation or peak-private-byte measurements. No temporary
file I/O is introduced.

Automatic prefixes longer than 124 bytes are rejected before text retrieval
(zero copied bytes and zero styling calls asserted). Parsed keyword lists use
owned content keys, preserve first-seen casing/order and share their cache when
style state is copied. Changing effective configuration/language/dialect or
keyword content invalidates the relevant cache. Focus and IME epochs live
outside copyable style state, preventing restored dialog state from reviving an
obsolete request.

Current validation: optimized full build passed; file_search, lazy_document and
fileio_roundtrip passed. The final ordinary_search_storage run, including the
benchmark and new queue-admission checks, passed in 54.15 seconds with its
unchanged 60-second timeout. Final editor_integration passed in 27.52 seconds.
Regression checks cover the byte guard, real timer
scheduling, repeated-request coalescing, pending keyboard input, latest
same-token prefix, small remaining gaps, caret movement, multiple selections,
edit revision, focus loss/restored style, IME state transitions, ownership
changes, explicit completion, long-line completion and cached casing/reuse.

Not checked: physical keyboard/IME hardware, hardware QS_INPUT routing on this
background test desktop, mixed-DPI interactions, peak private bytes, or steady
typing with document-word scanning enabled. The deterministic queued-key test
uses the production admission check; failed attempts to inject hardware input
were environment precondition failures and are not counted as passing checks.

## Actual editing-message verification

A separate run on 2026-11-26 used
`MULTIPAD4_COMPLETION_INPUT_BENCHMARK=1` with `ordinary_search_storage` and passed
in 25.88 seconds. This supplements the notification-only measurements above;
it does not relabel them as physical input. Both sides call
`SendMessageW(editor, WM_CHAR, character, 1)` so Scintilla performs the actual
text edit and sends its normal SCN_CHARADDED notification. Text bytes and caret
styling are checked after every measured edit. Whole candidate lists and their
actual displayed indices/text are compared outside the timing window, with the
original popup selection restored before the next edit. All 70 measured edits
produced exactly one WM_PAINT.

The reference hooks EditorChild's notification entry and performs the old
synchronous styling prerequisite before the normal completion handler. It also
forces a keyword-cache miss when the popup is not already active. That miss
still constructs the new owned cache; this is a synchronous-prerequisite/cache-
miss reference, **not the complete old executable or an exact reproduction of
its parsing allocation costs**. Both sides retain the original active-popup and
read-only short circuits. Candidate construction, editing, painting and sampling
otherwise follow the same code. There is no whole-version performance claim.

Captured settings: one tab, ordinary UTF-8 document, 550,001 initial bytes
(50,000 `int value;` lines and `f`), stock C++ lexer, SSE4.2 optimized build,
minimum prefix one byte, ignore-case on, document-word scanning off,
only-document-words off, English-IME-only off, plugins off, warm process/cache,
no measured temporary-file I/O. A real theme appends
`foozebra foobar FooBar foobaz Foobang` to C++ keyword slot zero. The first
WM_CHAR inserts `o`; visible-popup steady typing then inserts `o`, `b`, `a`.
A second steady sequence starts from `fo` with styling ready, dismisses the
popup before each of those three characters and exercises preparation/cache
reuse. Five paired runs give five first edits and fifteen edits for each steady
scenario on each side.

| Actual WM_CHAR, ms: reference / changed | Input callback median | Input callback p95/p99/max | Correct paint median | Correct paint p95/p99/max | Completion median | Completion p95/p99/max |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| First input after distant jump | 292.406 / 0.501 | 296.426 / 0.672 | 295.268 / 301.315 | 299.458 / 308.591 | 295.341 / 301.321 | 299.465 / 308.597 |
| Steady typing, popup visible | 0.258 / 0.323 | 0.363 / 0.645 | 2.597 / 2.694 | 3.542 / 3.459 | 2.652 / 2.789 | 3.643 / 3.612 |
| Steady typing, popup reopened | 6.675 / 5.890 | 9.178 / 8.891 | 9.400 / 9.159 | 11.963 / 13.608 | 9.567 / 9.306 | 12.102 / 13.664 |

Nearest-rank p95/p99 equal the maximum for these sample counts. Longest measured
posted handler was 7.744 ms in the first-input workload. Raw editing, paint,
completion, handler, prefix-copy/style/paint and candidate counters are in
[completion-wmchar.csv](benchmarks/completion-wmchar.csv). First correct paint
in this message-level workload remains about six milliseconds slower median;
there is no overall paint-latency or steady-typing speedup claim. The earlier
740.532 ms long-line handler limitation remains applicable.

The added direct theme regression goes through ThemeDefinition and
ConfigureDocumentStyle, rather than replacing a private cached list. It checks
that `foobar` wins over the later `FooBar`, then replaces the theme's appended
words with `foozebra FOOBAZ foobee` while keeping its nominal version unchanged.
The cache owner changes, new `FOOBAZ` appears and obsolete `foobar` disappears.
The message-level pairs compare every candidate's displayed index and exact
casing for initial completion and both steady paths. This closes the earlier
notification-only input, partial-candidate and theme-content evidence gaps.
