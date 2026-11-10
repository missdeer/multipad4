# Matepath Dock Pane

MultiPad4 embeds matepath's Shell directory-list implementation as an independent
Klimov docking pane, alongside the existing document pane. The build uses the
checked-in snapshot in `3rdparty/matepath`; it does not depend on `tmp/notepad4`
or start a separate matepath executable.

## Usage

- Window → Matepath 文件浏览器 toggles the pane (initially docked on the right).
- Enter a directory in the address field and press Enter or 转到. Relative paths
  resolve against the displayed directory, without changing the process working
  directory. Double-click a folder to enter it.
- ← / → navigate history; ↑ goes to the parent; 刷新 or F5 refreshes the list.
  Alt+Left/Right/Up also navigate; Backspace in the list goes to the parent.
- Enter patterns such as `*.cpp;*.h` and press Enter or 筛选. Folders remain visible.
- Double-click a file or press Enter to create/activate its editor tab. Existing
  open files are activated without discarding edits or opening a duplicate tab.
- Window → Matepath 定位活动文档 navigates to and selects the active document.
- The context menu exposes hidden files, sorting by name/size/type/date, reverse
  order, file properties, docking to left/right/bottom, floating and hiding.
- Drag a file from the list to another OLE drop target using matepath's original
  Shell data object and drop source.

Directory, filter, sorting, hidden-file visibility, pane visibility and docking
side are saved with the application's normal Save Settings preference. Mouse
docking, floating, splitters and title controls belong to the established docking
framework.

## Boundaries and verification

The imported directory API retains its MAX_PATH limit. Unsupported paths and
unavailable directories are rejected before changing navigation state. Shell
icons resolve on the UI STA; very large directories can pause navigation while
icons load. Refresh is explicit (F5); filesystem watching is not enabled.

This migration embeds the directory-browser workflow. Standalone matepath's
external-editor target-window discovery, separate application preferences,
favorites manager and destructive file-management dialogs are not embedded.
It does not claim complete standalone matepath UI parity.

`tests/MatepathPaneTests.h`, called by `editor_integration`, exercises Chinese
paths, folder navigation through filters, hidden files, back/forward/parent,
failed and overlong navigation, opening/activating MDI documents, document
isolation, revealing the active document, unchanged process directory,
left/right/bottom/floating transitions and hide/show. `--qa-render` additionally
exports docked and floating pane screenshots. Physical drag/drop, IME entry and
cross-monitor DPI transitions still require interactive verification.
