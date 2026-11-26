# Search cancellation and retired work (#41)

Search callbacks own their immutable snapshots, cancellation state, results
and I/O sessions independently of editor/dialog objects. Cancel drops the UI's
handle, sets the token, and notifies bounded queue waiters. At most one background
helper per cancelled unfinished job requests `CancelSynchronousIo` and sleeps
10 ms between retries. The OS cancellation call itself blocked in the real SMB
fixture below, so it must never run on the UI thread. Repeating it also covers
cancellation between the token check and a later synchronous I/O submission.
A short per-thread lease protects the cancellation target without holding any
shared lock across the OS call. A finishing worker/reader waits for its active
leases before returning to the pool; a stale cancellation cannot reach unrelated
work after thread reuse. Those waits stay on worker threads. The UI never joins.
Discarded completed results and directory resources are also destroyed on a
background callback, so closing their files/archives cannot move a wait back to
the UI. Admission is held through that cleanup. The callback retains its state until return;
`CloseThreadpoolWork` releases its work object asynchronously when a callback
is still outstanding, as documented by
[Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-closethreadpoolwork).
The existing joining `work::Task` remains unchanged for callers that borrow data.

Eight admitted search groups bound active plus retired searches. A mapped view
retains only its latest queued request while admission is full, and retries from
its UI timer. An edit, Esc, tab close or newer request discards that pending
request. A directory dialog similarly replaces its pending search with the
latest request. Each directory group has at most eight enumerators and one
dependent open-document search; an atomic nine-callback limit also bounds retired
dependent search callbacks in each group. Cancellation retry callbacks are
separately bounded to one per cancelled unfinished search. Dependent ordinary/mapped searches inherit their
directory group's permit, avoiding a deadlock when every group is occupied by
directory enumeration awaiting open-document results.

Workers do not post completions to HWNDs. The view polls only its current job,
and applies results only if its snapshot still matches. Retired results are
destroyed without invoking their UI closures. Open-document workers own their
snapshot and job cancellation storage, rather than borrowing a task pointer. Dialog destruction cancels
all work without publishing into destroyed controls. Shutdown closes admission
and requests cancellation of registered jobs; callback-owned resources remain
alive until each callback returns.

Mapped search readers use independent read sessions. Source and piece-tree
line-index caches now build blocks/summaries outside their publication locks,
then publish immutable results under short locks. Thus a retired blocked read
cannot retain an ancestor/index lock needed by a new query. Simultaneous misses
can build the same block; duplicate work is bounded by admitted workers, with
one block being constructed per worker at a time. One read session is reused
through an entire line query, including its prefix probes. Journal read sessions
reuse one private handle per backing journal. A short journal lock copies the
pending memory tail and snapshots the persisted range; private-handle disk reads
then run outside that lock, preserving the append-block optimization.
Mapped-file clones keep their existing mapping objects and only reopen the
existing file handle when buffered fallback actually needs an independent file
position. They never reopen the pathname, including after a save replaces it.
If the filesystem refuses that reopen, the previous shared fallback remains
available for compatibility; read isolation in that fallback case is not guaranteed.

The retained-matcher change (#31) must hand a matcher to a new job only
after the previous job finishes. Its cancellation token and reader-thread
registration must be rebound on each job and cleared before parking the matcher
when idle; retaining stale registration could target an unrelated pooled thread. A cancelled job can continue running after its view releases it.
The line-selector change (#33) must use the same short publication locks and
per-query sessions when integrated; it must not reintroduce I/O under a lock.

## Verification

```
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R search_retirement --output-on-failure
cmake-msvc-build/search_cancellation_benchmark.exe search-cancellation.csv
```

The console regression blocks eight workers, cancels their UI handles, checks
admission remains closed at the cap and confirms payloads live until release.
It also cancels real blocked synchronous anonymous-pipe `ReadFile` calls on the
parent and nested compute threads and checks `ERROR_OPERATION_ABORTED`. A
barrier also delays a pipe read until after cancellation, verifying the retry
path. Nested reads share a pipe handle, exercising kernel-serialized reads where
one call can begin its actual I/O only after cancellation releases another. A result
whose destructor blocks verifies that completed-result cleanup is off the UI and
keeps its admission slot until finished. It then verifies shutdown stops admission without
waiting for a deliberately uncancellable event wait.

The dedicated Win32/Scintilla regression blocks actual mapped workers and
exercises Esc, editing, latest-request replacement, full admission, tab close,
new-document isolation, directory relaunch/cancellation, direct dialog destruction,
open-document snapshot cancellation/reopen and shutdown. Both ordinary and mapped
dependent searches are checked with seven other groups reserved. A separate
index barrier stalls one block build while a concurrent query must finish before
that barrier is released. A forced-buffered session also stalls while holding
its fallback lock; another session must still read correctly before release.
Test hooks are compiled only with `BUILD_TESTING`.

After the asynchronous OS-cancellation fix, the affected console suite
passed all eight checks in 24.46 s: reverse search, file replacement, retirement,
file search, lazy documents, append blocks, piece reads and file I/O. The final
shared-pipe and delayed-read cancellation regression passed in 0.35 s. The five
application checks passed in 34.77 s: replacement (1.84 s), large files
(10.18 s), retirement UI (1.70 s), ordinary search (1.03 s), and the full editor
integration (20.01 s). Journal persisted-prefix/pending-tail reads across CRLF
and retained matcher reuse after rebinding cancellation tokens are also checked.
After integrating #44 (`cd6f299`), the build passed and the focused indent,
retirement UI and full editor checks passed again in 31.48 s (1.09/2.86/27.51 s).
Both test-only CLI routes remain present in the separate test executable.
After sharing the SMB fixture with the GUI test, console retirement, retirement UI
and full editor regression checks passed again in 26.43 s (0.55/1.94/23.94 s).
After merging #37 (`21477d7`), the build, all eight affected console checks
(24.44 s), all five application checks (43.07 s), and the actual SMB frame-close
check (3.86 s) passed. The open-file identity index stays inside the admitted
shared state, is constructed on a worker, and retires together with native
directory handles and result archives. The fresh-identity/cached-negative alias
regression and final live replacement check remain intact.
After integrating #45 (`64801ed`), configuration/build and focused frame refresh,
retirement UI and full editor checks passed in 29.58 s (0.70/1.86/27.01 s).
The actual SMB frame-close scenario passed again in 3.80 s. Frame publication
caches and mapped-view excerpt invalidation coexist with asynchronous retirement;
frame-refresh and retirement test CLI routes are both preserved.

## Paired measurements and limits

Windows 11 Pro 10.0.26300, Intel i9-12900HK, MSVC 19.44 RelWithDebInfo `/O2`,
x64 SSE4.2. Twenty-one runs per variant compare the previous joining task Reset
with the new search-task Reset on the same thread. Each worker retains a 1 MiB
payload and waits on an event; a helper requests release after 50 ms. Windows
timer granularity makes actual release typically around 62 ms. This deliberately
models a callback that cannot respond to a cooperative token.

| Cancellation callback | Median | p95 | p99 / maximum |
|---|---:|---:|---:|
| Joining Reset | 57.4487 ms | 67.4120 ms | 67.8795 ms |
| Retiring Reset | 0.0050 ms | 0.0136 ms | 0.0141 ms |

Payload retention after the new Reset is intentional and verified; it is
released after the blocked callback finishes. CSV rows record both point-in-time
private bytes/handle counts and sampled peaks during the blocked interval, using
the same sampler for both variants. The sample thread requests 1 ms sleeps and
can miss shorter peaks. Maximum observed sampled private bytes were 2,215,936
before and 2,330,624 after; handle peaks were 69 and 70 respectively. These
include sampler/helper threads and are process-wide, not per-job allocation
claims. The application regression observed eight
blocked mapped groups at 11,374,592 private bytes and 242 handles. Its blocked
mapped cancellation through synchronous application redraw took 2.370 ms.
Those application values are single-run observations, not tail-latency claims.
Raw paired results are in `benchmarks/search-cancellation.csv`.

The benchmark has no document encoding, decoding, storage reads or output writes
inside its cancellation measurement. Corpus creation/CSV output are outside the
timed region. UI CPU, compositor presentation and input-to-paint percentiles
were not separately measured. Full search completion is deliberately deferred
until the event releases; cancellation responsiveness is the measured behavior.

## Controlled SMB shutdown

The machine already had a local SMB share. A unique `CREATE_NEW` file in a
worktree temporary folder was held under an exclusive oplock using its local
path. A real `work::SearchTask` opened the same file through its UNC alias.
`FileRemoteProtocolInfo` confirmed SMB 3.1.1, and the oplock-break event plus an
unfinished job confirmed an actual blocked operation before cancellation.
No share/service/firewall/network settings were changed.

The first run exposed a real bug: the main thread blocked inside
`NtCancelSynchronousIoFile`, reached through `StopSearchAdmission`, while the
worker blocked in the UNC `NtCreateFile`. Moving all OS cancellation to the
bounded helper and adding the target-thread lease fixed that UI stall.

On the final run, `StopSearchAdmission` plus dropping the UI's search handle
returned in **0.0166 ms**, while admission remained closed and the payload stayed
alive during the blocked operation. An independent two-second watchdog then
released only the owned oplock. The open eventually returned **ERROR_SUCCESS
(0)**: this provider did not interrupt it before acknowledgement. That late result
was discarded. The callback/helper released their resources and the admission
count drained to zero. The test process exited normally with code 0, and no
owned temporary files or test processes remained.

The actual editor GUI test also transfers that blocked `SearchTask<FileSearchResult>`
into the real search dialog's `openSearch`, then sends the main frame `WM_CLOSE`.
The close callback returned in **28.1102 ms** and the complete shutdown callback
in **28.1178 ms**. It destroyed the frame, retired/cleared the dialog's search,
and closed admission before acknowledgement. After the watchdog released the
oplock, the late open result (again **0**) was discarded, all permits/payloads
were released, and assertions confirmed that the destroyed frame and cleared
search did not reappear. CTest observed the actual test-editor process exit
normally with code 0 in 3.80 s; an independent process/file scan was empty.
This scenario uses the existing `--search-retirement-test` GUI entry and the
same test-only fixture as the console check, with no product configuration.

These checks verify both the actual search-work shutdown entry point and
MainFrame close, dialog retirement and resource lifetime on a controlled stalled
SMB open. It does not establish that the full editor
process can exit before the server acknowledges, nor that disconnected reads or
mapped-page faults can be interrupted. Synchronous cancellation remains best
effort; bounded retired jobs keep their resources until callbacks finish.

To reproduce on an existing share, set both test-only environment variables to
aliases of the same writable temporary folder, then run `search_work_tests`:

```powershell
$env:MULTIPAD4_SMB_TEST_ROOT='\\localhost\existing-share\temporary-folder'
$env:MULTIPAD4_SMB_TEST_LOCAL_ROOT='D:\temporary-folder'
.\cmake-msvc-build\search_work_tests.exe
```

Unset these variables after the test. The fixture requests the oplock through
the **local** handle. Direct remote oplock FSCTLs are denied by design, as
[Microsoft documents](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_request_oplock);
the initial `ERROR_INVALID_FUNCTION` probes did not show a server limitation.
The watchdog releases the blocker even when a subsequent assertion fails, and
RAII removes only the successfully created test file.

IME, dragging and mixed-monitor DPI were not manually exercised.
