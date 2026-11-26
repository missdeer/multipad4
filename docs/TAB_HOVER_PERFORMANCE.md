# Tab hover repaint and native order (#47)

Hover, press, leave and capture changes invalidate the old/new close-button
rectangles. WM_PAINT retains the exact update region before native painting
validates it, then intersects the overlay DC with that region and the visible
tab viewport. Normal painting queries the dirty visible part of the single row;
large tab sets use a direction-aware binary search, including reversed RTL
coordinates. Overflow buttons are excluded on whichever side they occupy.
WM_PRINTCLIENT preserves the caller's complex clip and DC state. Resource-error
fallbacks still paint the visible overlay; a NULLREGION does not add drawing.

One live order vector and HWND-to-position map are maintained after successful
native insert/remove/move operations. Activation, icon updates and plugin buffer
position queries reuse this index. Plugin lookup combines the existing buffer-ID
and child-window maps with the document's bounded window ancestry, including a
mapped view converted to full editing. It never treats the frame's separately
published tab index as a synchronous position.

## Paired hover measurements

Windows, Intel i9-12900HK, MSVC 19.44 x64 RelWithDebInfo `/O2 /arch:SSE4.2`,
default native theme and font, 100 logical-pixel fixed tabs, ordinary unmodified
empty documents, and plugins disabled. Scintilla's internal text mode is UTF-8;
the default save encoding is ANSI and no text/storage I/O occurs during hover.
The same machine's companion layout check reports DPI 192; the timing CSV does
not log a DPI field. Corpus setup and caches are warm before each measured run.

Twenty-one matched transitions per variant alternate between the first two close
buttons. Both variants run in the same optimized process with identical tab
order/selection and the same order index. A test-only replay selects the former
full-invalidation/all-tab-overlay path, so timing isolates repaint preparation.
Counters are enabled on both sides. The oracle/replay is absent with
BUILD_TESTING=OFF and has no production CLI entry.

The interval begins immediately before actual WM_MOUSEMOVE dispatch. Callback
time ends on its return; paint time ends after synchronous UpdateWindow. Captured
complete tab-control RGB images compare byte-for-byte for every pair; tab-order/
active-child fingerprints also match, with exact native-order and plugin-position
checks around structural operations. It measures a correct application paint, not
physical mouse delivery or compositor presentation. Dirty-region probing occurs
before UpdateWindow on both sides; image capture, comparison, resource sampling
and CSV output occur after the timed region.

| Tabs / measurement | Before median | After median | Before p95 / max | After p95 / max |
|---|---:|---:|---:|---:|
| 4: callback ms | 0.0264 | 0.0343 | 0.0602 / 0.0761 | 0.0760 / 0.1055 |
| 4: input-to-paint ms | 0.8741 | 0.6380 | 2.0613 / 2.9269 | 1.7693 / 2.3119 |
| 500: callback ms | 0.0263 | 0.0359 | 0.0310 / 0.0455 | 0.0418 / 0.3447 |
| 500: input-to-paint ms | 1.7310 | 0.6070 | 2.3187 / 2.8429 | 0.9162 / 1.0769 |

With 21 samples, nearest-rank p99 equals the observed maximum; these are not
population tail estimates. The narrower invalidation slightly increases the
mouse callback while reducing time to correct paint. Median GetItemRect calls
fall from 501 to 18 at 500 tabs; overlay draws fall from 5 to 2. At four tabs,
queries change from 5 to 7 and draws from 4 to 2. Dirty pixels fall from 58,760
to 20,400 in both cases. Native themed hover also invalidates its old/new tab
backgrounds, which are included in that measured region.

GetThreadTimes is coarse here: per-event CPU medians are zero and nonzero rows
are quantized to 15.625 ms. No fine-grained CPU speedup is claimed. Point-in-time
private bytes are equal across variants: 17,948,672 at four tabs and 83,361,792 at
500 tabs. These are process-wide observations including preallocated image
oracles, not sampled peaks or an isolated measurement of index memory overhead.
Raw rows are in [tab-hover.csv](benchmarks/tab-hover.csv).

## Verification and limits

The dedicated check passes full RGB/state comparisons at 4/500 tabs, complex
print-clip sentinel protection and DC restoration, RTL full/partial images,
overflow scrolling, real drag-reorder messages, capture loss, icon/selection/
plugin lookup traffic, immediate native-order agreement and mapped/full-view
positions. Font and synthetic DPI messages are exercised with few tabs; they
do not establish physical mixed-monitor behavior.

An independent review found the new update-region error path could omit close
glyphs. The fixed fallback is covered separately with two tabs: injected ERROR
after native repaint must retain the complete image, and a clean NULLREGION must
produce zero overlay draws. This focused check also runs the existing
RunMDILayoutTests and RunTabCloseTests without the unrelated full-editor docking
prerequisite. No existing assertion or timeout was weakened. The normal 500-tab
measurement was not repeated for this error-only fix.

The normal dedicated check passed in 69.70 s. Its initial fixture created 500
documents visibly and exhausted its 120 s safety timeout; the successful fixture
prepares them hidden, captures only the tab control at 500 tabs, and tests DPI/
font refresh with few tabs. Preparation/cleanup are outside hover measurements;
the successful 500-document preparation itself took 54.44 s. This is not a tab
creation-performance claim.

Reviewed screenshots retain the expected close glyphs and overflow clipping:
[few tabs](screenshots/issue-47-few-tabs.png),
[500-tab overflow](screenshots/issue-47-overflow.png),
[RTL overflow](screenshots/issue-47-rtl.png).
Physical dragging, keyboard/IME and cross-monitor DPI interaction remain manual
verification items. No visual layout change is intended.

```powershell
bash -lc "cmake-reconfigure cmake-msvc-build"
bash -lc "cmake-build cmake-msvc-build"
ctest --test-dir cmake-msvc-build -R '^tab_strip(_layout)?$' --output-on-failure -j1
```
