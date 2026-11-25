# Shared directory watch measurements (#29)

Measured 2026-11-25 on Windows, Intel Core i9-12900HK, MSVC 19.44,
RelWithDebInfo `/O2`, SSE4.2; local temporary directory, warm cache.
`directory_watch` creates 200 files with tiny ASCII decimal contents, then
subscribes the new shared watcher and a reproduction of the former per-document
watcher to the same directory. These represent 200 document subscriptions; the
focused test does not instantiate 200 Scintilla tabs or plugins.

For each of 21 writes to an unrelated file, the test pumps the same hidden UI
window until all baseline callbacks have dispatched. The old dispatcher
reproduces the snapshot/linear document lookup and queries the notified
subscription's file attributes. Both algorithms receive the same actual Windows
directory events, in the same optimized executable. No source change or matcher
substitution occurs between samples.

| Counter / timing | Previous per-file watcher | Shared directory watcher |
| --- | ---: | ---: |
| UI messages, 21 unrelated writes | 8,400 | 0 |
| Document attribute queries | 8,400 | 0 |
| Directory handles for 200 subscriptions | 200 | 1 |
| Write to baseline dispatch median / p95 / maximum | 33.507 / 38.000 / 40.078 ms | No UI callback |

The new watcher performs at most one file-identity lookup for each unknown changed
name to detect newly created hard-link aliases. This is worker work, not a
per-document attribute query; known names use a filename-to-subscription index.
Unknown names whose identity cannot be read trigger conservative reconciliation.
Rename events, short names with `~`, completion overflow, malformed notification
records and zero-byte completions also reconcile all directory subscribers.

The test asserts the shared implementation adds fewer than 20 process handles
for all 200 subscriptions; this run added exactly one. It asserts no shared UI
messages during the paired unrelated-write workload. It also checks precise
filename dispatch, one surviving subscriber after the other 199 close,
rename/recreation, a hard-link alias introduced after subscription, and repeated
replacement/teardown while writes arrive.

Callbacks parse notifications and rearm under the same mutex used to begin
teardown, then merge identities into one pending UI batch. The UI validates the
subscription token and uses `EditorTabs::documentsById` for each affected identity.
It reacquires document identity after a modal reload question and retains pending
work while the frame is disabled or processing a modal command.

Not measured: actual 200-tab application input-to-first-paint latency, separate
worker callback counts/CPU, peak private bytes or driver I/O. The timing above is
write-to-baseline-dispatch, not a claimed new UI latency; the new implementation
has no UI dispatch for this workload. The comparison keeps all old watchers
active and therefore includes their overhead in the shared implementation's
execution environment. Manual inactive-window prompts and GUI checks are
reported separately by the implementation workflow.

Validation: optimized build and `directory_watch` (2.72 s), `file_search`,
`lazy_document`, `fileio_roundtrip` (combined 47.44 s), and
`editor_integration` (41.69 s after follow-up fixes) passed. The real application checks automatic
reload of inactive tabs, deferred notifications during modal commands,
unsaved-buffer protection, subscription replacement/stale-token rejection,
and deletion/recreation. Overflow/zero-byte/malformed-record branches were
reviewed in source but not injected by the regression harness.

Follow-up review verification: known-name dispatch expands each distinct captured
file identity to all sibling subscriptions, including aliases introduced after
their filename subscriptions existed. Failed cached backends are replaced while
older failed subscriptions remain alive. The dedicated test injects that failure
only in its test build and verifies the replacement receives actual disk events.
The case-sensitive-directory test was enabled and passed on this NTFS volume;
unknown names preserve their original case for identity reads. Once an unknown
missing file triggers directory reconciliation, the callback stops processing
that batch rather than repeating reconciliation for every missing name.
Name deduplication now touches only the newly inserted subscription; weak registry
entries for former directories remain cached and can be pruned separately.

A subsequent lifecycle regression deletes an already subscribed filename, replaces
it with a hard link and writes it before pumping any UI notification or refreshing
subscription identities. Known-name additions/removals now request directory-wide
reconciliation so this case cannot rely on stale captured identity. Ordinary
modified-name events retain indexed dispatch. Rebuild and `directory_watch`
passed again (2.74 s).

PR review regression: two child directories `foo` and `FOO` beneath the enabled
case-sensitive parent each contain `same.txt`. The test watches both concurrently
and verifies a write in each directory reaches only its own subscription. Shared
backend keys now use the opened directory handle's volume/file identity; when
identity lookup is unavailable, the canonical path fallback retains exact case.
The real NTFS case-sensitive checks and complete `directory_watch` suite passed
(3.09 s) after this fix.

PR follow-up alias regression: an unsubscribed hard-link name exists before its
original target is subscribed, then is deleted and relinked to another watched
file. After draining lifecycle events and rearming, a later alias write must
notify the new target and not the old target. Eager hard-link-name enumeration
was removed; only subscription names/long names are cached. Other hard links
resolve their current file identity on every notification. Full dedicated
`directory_watch` passed (3.29 s), including this regression.

PR reload-retry regression: deny read sharing on a changed monitored file,
dismiss the automatic reload failure, release the sharing handle, and call the
activation check without a new filesystem write. Failed automatic reload now
retains its identity for a later activation check while clearing the immediate
check flag to avoid a retry/error loop. The document is reacquired after the
potentially modal reload before accessing it. Real editor integration passed
(23.56 s) with this case.

Round-six retry verification keeps the sharing-denial handle open while pumping
all queued dialog messages, sending repeated WM_ENABLE and unrelated-file
notifications. Exactly one error dialog is allowed before the handle is released.
Filesystem dispatch now skips/requeues previously failed change-only identities;
activation explicitly retries them. WM_ENABLE posts only for fresh pending file
checks outside background work. Editor integration passed (45.92 s), including
release and retry without another filesystem change.
