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
