# Notepad++ search migration

The search baseline supersedes the notepad4 search-dialog requirement in
PROJECT_SCOPE.md. Reference checkout: Notepad++
`61e17dde113e01f097939b111cac38427879864c`.

## Implementation

- A single modeless Find / Replace / Find in Files / Mark dialog replaces the
  old Find and Replace templates in every compiled resource language. Its
  411 x 197 dialog-unit template and 8-point Windows dialog font follow the
  Notepad++ layout: four compact input rows, upper-right actions, directory
  options on the right, and side-by-side search-mode/transparency groups.
  Simplified Chinese labels follow the current Chinese UI language.
- Directory/filter combo boxes, browse/current-directory buttons, swap/copy
  Find and Replace, and compact/expanded layout controls are functional.
  Transparency supports always/on-losing-focus modes and an opacity slider.
- Normal, Extended (including embedded NUL), Boost regular expressions,
  whole words, case, dot-all, wrap, forward/backward, selection-scoped bulk
  operations, Count, current/all-open document results and replacements.
- All-open operations temporarily apply the query and restore each document's
  search context. The modeless dialog follows the active document while retaining
  its live query and options, including files opened from Search results.
  Commands resolve the active editor again before executing, even if a posted
  activation notification has not yet been processed. Closing the currently
  bound document destroys the dialog. F3 uses the live dialog query.
- File search walks directories through timer ticks, supports filename masks,
  subfolders, hidden folders, cancellation and error counts. Binary NUL content
  is skipped after decoding. Junctions/symlinks are not traversed.
- Replace in Files confirms disk writes, uses atomic saving and retains detected
  encoding and BOM. Open files use their current editor buffers; replacements
  remain unsaved and independently undoable. Disk-only replacements have no undo.
- Search results use the existing Klimov docking framework, with read-only
  Scintilla content, retained search sessions, search/file/line hierarchy,
  per-match highlighting, folding, copy/select/clear and double-click/Enter
  navigation. Clicking a particular match on a multi-hit line selects that match.
- SearchResultLexer adapts the upstream SearchResult styles and fold levels to
  SCLEX_CONTAINER. It uses typed UTF-8 match metadata instead of upstream's
  private Lexilla `@MarkingsStruct` pointer ABI. Source snapshots are shared
  across matches. Navigation verifies snapshot text before selecting a result.
- Ctrl+F / Ctrl+H open Find / Replace; Ctrl+Shift+F opens files, Ctrl+M opens
  Mark, F7 reveals results. The existing Search menu gains these commands.

## Validation and remaining differences

Build with the local MSVC wrapper and run CTest. SearchDialogFeatureTests uses
real Scintilla/WTL windows and covers Unicode byte positions, multiple matches
per line, lexer styles/folds, regex captures, undo, zero-width searches,
invalid regex status, extended newlines, markers, isolated document state,
recursive file filtering, disk replacement and UTF-16 / UTF-8 BOM preservation.
Layout checks cover compact dimensions, upper-right actions, width resizing,
collapse/expand, default action per tab, transparency modes and Chinese labels.
The file-result regression drives Find in Files, double-clicks a result, then
clicks Find Next, Find Previous, Find All in Current Document and Replace in the
newly opened editor, checking source-document isolation and live F3 queries.
QA images are generated with `multipad4 --qa-render tmp/search-layout-qa`.

This is a host adaptation, not the original Notepad++ dialog class linked into
WTL. Notepad++ project-panel searches have no corresponding project model here.
Incremental Find, Find in Finder, the optional two-button layout and Notepad++
theme/full translation-catalogue integration are not ported. The dialog uses
English or Simplified Chinese labels; other language catalogues are not ported. File cancellation occurs between
entries; reading or searching a single large file still occupies the UI thread.
Cross-monitor DPI dragging, IME input and sustained large-tree searches need
manual verification.

The GPL notices and original reference resources/lexer are retained under
3rdparty/notepad-plus-plus; see THIRD_PARTY.md.
