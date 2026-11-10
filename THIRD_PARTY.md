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

Both framework snapshots are unmodified. `cmake/FrameworkCompatibility.cmake` produces build-directory compatibility copies. It adapts obsolete template declarations, qualifies dependent types, replaces bind2nd with a lambda, restores MSVC's auto_ptr compatibility for the original transfer semantics, and corrects a Unicode ellipsis length and a missing virtual destructor. Modern MSVC compiles these legacy templates with permissive lookup. These build adaptations are separate from the upstream snapshot.

The compatibility copies also support the Intel oneAPI LLVM frontend: the MDI client is forward-declared before its use in a default template argument, dependent item/comparator types and window handles are qualified, and the splitter traits and orientation lookup are made explicit. The splitter adopts WTL's newly created halftone brush through its raw handle so the owning `CBrush` still releases it. The editor integration regression repeatedly draws/cleans splitter previews and checks GDI object usage.

## notepad4 and its Scintilla fork

Source: https://github.com/zufuliu/notepad4 at commit `3fcfb70fd0d3c8802100c85f7293ccb326aa8b64` (copied from the user-provided `tmp/notepad4` checkout).

- Application code/resources: `3rdparty/notepad4/src` and `3rdparty/notepad4/res`.
- Application license/notices: `3rdparty/notepad4/License.txt` (BSD 3-Clause and component notices).
- Scintilla: `3rdparty/notepad4-scintilla/scintilla`.
- Scintilla/Lexilla license: `3rdparty/notepad4-scintilla/scintilla/License.txt`.
- Additional upstream notices: `3rdparty/notepad4-scintilla/Notepad4-License.txt`.

Preserve these notices in source and binary distributions as required by each license. All eleven `locale/*/Notepad4.rc` files were added from the same pinned GitHub revision. The complete lexer configuration catalogue is compiled directly; adapted editor algorithms retain their upstream provenance. Scintilla host services include custom transliteration and the bundled standalone Boost.Regex headers. Migration and behavioral validation status is tracked in `docs/MIGRATION_STATUS.md`.

No license has been selected for the new MultiPad4 application code.
