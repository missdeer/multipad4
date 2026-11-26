# Bounded modeline reads for preferences (#52)

`filevars::Region(const EditorDocument&)` shares the existing first/last-512-byte
rule between file-variable application and peer preference updates. It returns
the head if any recognized variable occurs there, even when its value is invalid
or unrelated to the requested preference. Otherwise it reads the last 512 bytes
at the actual document offset. At lengths 513–1023 that tail overlaps the head;
it is not a concatenated or truncated substitute region. No region is cached
across edits, and the parser/case/token-boundary rules are unchanged.

ApplyFileVariables no longer requests up to 4 KiB per end. Long-line visibility
toggles do not query fill-column; settings changes query it only for a peer whose
current column still equals its applied file value. File-variable restoration,
global baselines and explicit per-document overrides retain their order. Ordinary
read-only documents are supported. FastView application still returns early;
the new document overload rejects FastView as the old whole-document Text path did.

## Exact checks

The dedicated entry executes RunTabPreferenceTests, RunLongLinePreferenceTests
and RunSettingsCancelTests, then checks empty/short documents, 512/513/700/1023/
1024/1025/4097/8193-byte boundaries, head precedence, disabled variables, a
multibyte character crossing the head boundary, edited declarations and read-only
application. The bounded reader equals the unchanged string Region oracle and
performs zero reads for empty input, one head read, or two reads totaling at most
1024 bytes. Tests deliberately construct overlapping tails whose token boundary
differs from the full document's boundary.

The two original e22d140 functions are retained in
`tests/ModelinePreferenceBaseline.cpp` under distinct test-only names. Old/new
ApplyFileVariables results compare complete text, all captured effective style
preferences, file-variable baselines/applied values, native editor settings,
read-only/modified state and complete selection positions/modes/virtual spaces.
Real WM_COMMAND tab/long-line dialogs and visibility toggles enter the normal
MainFrame operation/command path, with only the preference function switched to
the old implementation by a test-only function pointer. An edited peer modeline
also receives a subsequent real preference command without reapplication first.
Full document text/settings/selection, defaults and preference-store values
are compared directly, not inferred from fingerprints. The short suite passed
in 2.07 s.

## Paired workload

Measured on Windows, Intel i9-12900HK, MSVC 19.44 x64 RelWithDebInfo `/O2`,
SSE4.2, in one optimized process with plugins/autosave disabled and nonpersistent
isolated test settings. The active ordinary UTF-8 document has 47 bytes. Three
peers each contain approximately 8 MiB of 255-character ASCII lines plus LF:
head declarations, tail declarations, and an unrelated recognized head overriding
tail declarations. Their total text is 25,165,957 bytes. One peer is read-only.
Tab width is four; wrapping is disabled. Both variants use the same configuration,
corpus, selection and preference/profile seeds. Document loads preserve some
native settings, so the fixture explicitly restores their identical initial state.

Seven pairs per command alternate variant order. Reset/loading, exact snapshots
and CSV writing are outside timing. Callback time covers real WM_COMMAND and the
native preference dialog where applicable; correct paint includes synchronous
frame/current-editor redraw. These operations complete synchronously. No physical
input/compositor latency or full-frame pixel equivalence is claimed.

| Command | Callback median ms before → after | Correct paint median ms before → after | Callback max ms before → after | Paint max ms before → after |
|---|---:|---:|---:|---:|
| Tabs/indentation preferences | 69.2311 → 53.3287 | 76.9497 → 64.0538 | 71.9589 → 76.9271 | 79.9552 → 83.6826 |
| Long-line column settings | 63.4035 → 57.3073 | 71.0336 → 66.1326 | 71.6883 → 60.9146 | 83.3570 → 72.2780 |
| Long-line visibility toggle | 9.8337 → 2.7726 | 16.8911 → 10.1094 | 11.2366 → 3.0476 | 19.8103 → 10.8125 |

With seven samples, nearest-rank p95 and p99 equal the observed maximum, not
population tail estimates. The tab-settings maximum did not improve. Median UI
CPU was 62.5→46.875 ms for both dialogs and 15.625→0 ms for visibility;
GetThreadTimes is quantized and zero does not mean no CPU work.

Actual Scintilla message observers record copied bytes and API calls. Tab settings
fall from three whole-text requests/25,165,957 bytes to zero; long-line settings
and visibility fall from four requests/25,166,004 bytes to zero. New settings paths
perform four 512-byte peer Range requests (one peer needs both ends); the active
document's separate legitimate two-byte Range gives five calls/2050 bytes overall.
The visibility toggle performs zero peer Text or Range requests; its independent
active-document status query remains one Range/two bytes.

A one-millisecond sampler runs during command/paint. Maximum sampled process
private bytes before/after are 101,855,232/93,773,824 for tab settings,
107,782,144/99,516,416 for column settings, and 107,687,936/99,282,944 for visibility.
These are process-wide samples affected by allocator reuse and retained comparison
snapshots, not isolated operation memory savings. CSV and temporary style/profile
setup are outside the interval. Physical disk transfer counters were not collected.
All 42 variant rows passed exact state comparisons; the process exited zero in
7.48 s. Raw data: [modeline-preferences.csv](benchmarks/modeline-preferences.csv).

Test hooks/oracles and CLI entries are excluded with BUILD_TESTING=OFF. Physical
keyboard/IME and mixed-monitor DPI behavior were not manually exercised; this
change has no intended visual layout effect.

After normally integrating master 57a2133, the wrapper build and modeline,
private-clipboard rich-text and both inventory component checks passed 4/4
in 30.33 s. Both test modes and the pre-COM/HWND clipboard isolation guard
are preserved; the unchanged 42-row preference measurements were not repeated.
