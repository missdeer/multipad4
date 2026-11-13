# Third-party components

## WTL

User-provided WTL 10.0 checkout at `3rdparty/wtl`, copyright Microsoft Corporation and WTL Team. Headers state the Microsoft Public License (MS-PL). No upstream changes.

## WTL Tabbing Framework

Daniel Bowen, copyright 2002–2005, with CustomTabCtrl/atlgdix contributions by Bjarke Viksoe. Snapshot: `3rdparty/TabbingFramework`.

Source: https://github.com/simonsteele/pn/tree/618ea4ad538b26a8795b1cdaa684f08b72d31d09/pnwtl/TabbingFramework

Each header contains its author and license notice. It permits compiled use and redistribution with the notice retained, with a restriction on selling the source file for profit without written author consent. Preserve the original notices; do not replace them with the host repository's license.

## WTL Docking Framework

Sergey Klimov, copyright 2000–2002, with subsequent contributions identified in the files. Snapshot: `3rdparty/DockingFramework`.

Source: https://github.com/simonsteele/pn/tree/618ea4ad538b26a8795b1cdaa684f08b72d31d09/pnwtl/DockingFramework

Per-file notices permit compiled use and redistribution of the unmodified file with author notices, with a restriction on selling the source for profit without author consent. Retain all original notices.

Both framework snapshots are unmodified. `cmake/FrameworkCompatibility.cmake` produces build-directory compatibility copies. It adapts obsolete template declarations, qualifies dependent types, replaces bind2nd with a lambda, enables MSVC's auto_ptr compatibility for legacy tracker ownership, and corrects a Unicode ellipsis length and a missing virtual destructor. Layout container elements (`CPtrFrame`) use `std::shared_ptr`, matching the upstream Boost branch's shared ownership: copying a deque element must not steal another live frame's pointer. Modern MSVC compiles these legacy templates with permissive lookup. These build adaptations are separate from the upstream snapshot.

The compatibility copies also support the Intel oneAPI LLVM frontend: the MDI client is forward-declared before its use in a default template argument, dependent item/comparator types and window handles are qualified, and the splitter traits and orientation lookup are made explicit. The splitter adopts WTL's newly created halftone brush through its raw handle so the owning `CBrush` still releases it. The editor integration regression repeatedly draws/cleans splitter previews and checks GDI object usage.

Floating dock panes move their actual HWND during the framework's drag tracking,
avoiding desktop XOR feedback that may disappear under composition. Screen-space
grab offsets remain stable as the window moves; cancellation restores its original
rectangle. Dock-target detection and final docking still use the original framework.
Integration checks verify movement before release, repeated motion and cancellation.
They also cover middle insertions in the owning layout deque, exactly-once frame
destruction, and repeated DC_ACCEPT/DC_DOCK drops creating bars between existing
panes, which follow a different path from menu docking.

## notepad4 and its Scintilla fork

Source: https://github.com/zufuliu/notepad4 at commit `3fcfb70fd0d3c8802100c85f7293ccb326aa8b64` (copied from the user-provided `tmp/notepad4` checkout).

- Application code/resources: `3rdparty/notepad4/src` and `3rdparty/notepad4/res`.
- Application license/notices: `3rdparty/notepad4/License.txt` (BSD 3-Clause and component notices).
- Scintilla: `3rdparty/notepad4-scintilla/scintilla`.
- Scintilla/Lexilla license: `3rdparty/notepad4-scintilla/scintilla/License.txt`.
- Additional upstream notices: `3rdparty/notepad4-scintilla/Notepad4-License.txt`.

Preserve these notices in source and binary distributions as required by each license. All eleven `locale/*/Notepad4.rc` files were added from the same pinned GitHub revision. The complete lexer configuration catalogue is compiled directly; adapted editor algorithms retain their upstream provenance. Scintilla host services include custom transliteration and the bundled standalone Boost.Regex headers. Migration and behavioral validation status is tracked in `docs/MIGRATION_STATUS.md`.

No license has been selected for the new MultiPad4 application code.

## matepath

Imported without source changes from the user-provided `tmp/notepad4/matepath`
checkout: `3rdparty/matepath/src/{Dlapi,DropSource}.{h,cpp}`. The original
BSD 3-Clause license and source readme are in `3rdparty/matepath/doc`.
Copyright © 2011–2026 Zufu Liu and contributors; metapath copyright
© 1996–2012 Florian Balmer. Preserve the license in binary distributions.

`cmake/Matepath.cmake` generates a host adaptation of the directory API in the
build tree. It provides Windows SDK typed PIDL compatibility, releases filtered
enumeration items, frees deleted list items through notification data, initializes
Shell output data, and resolves icons on the owning STA instead of accessing
Shell interfaces and controls from an un-marshaled worker thread. Host window,
navigation history, settings and MDI dispatch are in `src/MatepathPane.*`.

## Notepad++ search UI and result lexer

Reference: user-provided `tmp/notepad-plus-plus` checkout at
`61e17dde113e01f097939b111cac38427879864c`. Original search resource,
resource IDs and LexSearchResult are preserved in `3rdparty/notepad-plus-plus`.
`src/SearchDialog.rc` adapts the dialog layout (Copyright 2021 Don HO,
GPL-3.0-or-later); `SearchDialog.cpp` and `SearchResultsPane.cpp` adapt its
Find/Replace/Find in Files/Mark and Finder interaction to explicit WTL documents.
`SearchResultLexer.cpp` adapts LexSearchResult's style/fold model (Copyright
1998-2001 Neil Hodgson, Copyright 2003 Don HO, GPL-2.0-or-later) using container
styling rather than the private Lexilla markings-pointer ABI. The upstream
GPL license is included in `LICENSE`; Scintilla's notice in
`COPYING.search-result` also applies to the original lexer. Distributing these
adaptations requires complying with the applicable GPL terms.


## Notepad++ plugin interface

`3rdparty/notepad-plus-plus-plugin-api` contains unmodified `PluginInterface.h`,
`Notepad_plus_msgs.h`, and `Docking.h` from the user-provided
`tmp/notepad-plus-plus` checkout, together with its GPL-3.0-or-later `LICENSE`.
The headers retain their copyright and license notices. `src/PluginHost.cpp`
implements the host adapter, using the upstream plugin loading sequence and ABI
as references; it does not import Notepad++'s application or docking manager.
Plugin DLLs in the generated build directory are user-provided runtime inputs,
not redistributed source dependencies. Their own licenses continue to apply.

Empty docking package windows are hidden synchronously when their last pane
leaves. Destruction still uses the upstream posted `WM_CLOSE`, preserving the
active docking callback. This prevents stale pane pixels from remaining visible
while a nested drag or modal loop delays that close. The compatibility regression
checks immediate invisibility and eventual HWND destruction with plugins loaded.

Repeated docking requests now replace a package's existing HWND records, and
undocking removes all records for that HWND. Same-bar ownership notifications
are idempotent. This addresses a captured failure where Matepath was floating
but a right-side package still referenced its HWND. Regressions inspect package
membership independently of HWND parenting, exercise repeated registration and
cancel/float transitions, and check deferred container destruction.

## Notepad++ Plugins Admin

`src/PluginAdmin.*` adapts the Plugins Admin menu/handler, modeless dialog,
five category lists, search and checked batch operations from the same local
Notepad++ reference. Original resources and GPL notices are retained under
`3rdparty/notepad-plus-plus/PluginsAdmin`. Menu command IDs 48014/48015 are
reserved from dynamic plugin commands and routed explicitly before their range.
`src/PluginAdminWorker.ps1` supplies a MultiPad4-specific exit/wait, package
verification, staging, rollback and restart adapter instead of N++'s GUP process.

The bundled x64 catalogue comes from the official nppPluginList repository;
`CATALOG_REVISION` pins its source revision and `README.catalog.md` retains its
upstream documentation. It is metadata, not a guarantee that every listed
plugin works with MultiPad4's partial N++ host API. The JSON parser is unmodified
nlohmann/json 3.12.0 from the reference checkout (MIT license included).

## Notepad4 SIMD application routines

`src/SimdText.*` adapts the active application SIMD routines from the local
`tmp/notepad4/src/Edit.cpp` and `EditEncoding.cpp`: EOL counting and mode
selection, indentation inference, UTF-16 Latin-extension detection, the
Zach Wegner UTF-8 validator, and `CheckUTF7`'s ASCII scan. Original Notepad4
licensing is retained under `3rdparty/notepad4/License.txt`; the UTF-8
validator's MIT notice is also retained in `SimdText.cpp`.

`src/SimdGraphics.*` adapts `Helpers.cpp` bitmap alpha merge/blend and contrast
verification, plus `Edit.cpp`'s UTF-16 HTML tag packing. GraphicUtils.h and
VectorISA.h are used directly from the pinned Scintilla fork. Adaptations
bound input reads, pad short tails locally, handle arbitrary bitmap widths,
and preserve MultiPad4's deterministic UTF-16 detection when statistical
Unicode detection is disabled. Vendored sources remain unchanged.
