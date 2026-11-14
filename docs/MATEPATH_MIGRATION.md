# Matepath Dock Pane Plugin

Matepath is an optional DLL plugin in `plugins/Matepath`, loaded by MultiPad4's
existing plugin host. Its UI, Shell directory list, navigation, docking and
settings live in the DLL; the executable contains no Matepath pane implementation.
The build uses the
checked-in snapshot in `3rdparty/matepath`; it does not depend on `tmp/notepad4`
or start a separate matepath executable.

## Usage

- 插件 → Matepath → 文件浏览器 toggles the pane (initially docked on the right).
- Enter a directory in the address field and press Enter or 转到. Relative paths
  resolve against the displayed directory, without changing the process working
  directory. Double-click a folder to enter it.
- ← / → navigate history; ↑ goes to the parent; 刷新 or F5 refreshes the list.
  Alt+Left/Right/Up also navigate; Backspace in the list goes to the parent.
- Enter patterns such as `*.cpp;*.h` and press Enter or 筛选. Folders remain visible.
- Double-click a file or press Enter to create/activate its editor tab. Existing
  open files are activated without discarding edits or opening a duplicate tab.
- 插件 → Matepath → 定位活动文档 navigates to and selects the active document.
- The context menu exposes hidden files, sorting by name/size/type/date, reverse
  order, file properties, docking to left/right/bottom, floating and hiding.
- Drag a file from the list to another OLE drop target using matepath's original
  Shell data object and drop source.

Directory, filter, sorting, hidden-file visibility, pane visibility and docking
side are saved independently in `plugins/Config/Matepath.ini` (UTF-16). Automated
smoke, rendering and plugin tests use defaults and do not write this file. Mouse
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

`tests/MatepathPaneTests.h`, called by `editor_integration`, exercises the loaded
DLL through window messages and plugin command dispatch. It covers Chinese
paths, folder navigation through filters, hidden files, back/forward/parent,
failed and overlong navigation, opening/activating MDI documents, document
isolation, revealing the active document, unchanged process directory,
left/right/bottom/floating transitions and hide/show. `--qa-render` additionally
exports docked and floating pane screenshots. Physical drag/drop, IME entry and
cross-monitor DPI transitions still require interactive verification.

## Building and plugin boundary

The default build produces `cmake-msvc-build/plugins/Matepath/Matepath.dll`.
The CMake target is `Matepath`; `MULTIPAD4_BUILD_MATEPATH=OFF` omits it.
The plugin version is `0.1.0.0`, maintained independently via `MATEPATH_VERSION`
in `plugins/Matepath/CMakeLists.txt`. CMake generates the DLL version resource
from this value, so Explorer and the plugin manager report the same version.
The executable does not link to the DLL. To disable an installed copy, use the
plugin manager and restart; removing the plugin folder also leaves the editor
fully usable. `editor_without_plugins` runs the editor suite with no plugins
selected. Both editor suites verify that shutdown leaves no Matepath window or
loaded DLL. Framework drag regressions use independent panes.

The DLL exports the Unicode Notepad++ plugin entry points for discovery,
commands and lifecycle notifications. File opening and active-path lookup use
`NPPM_DOOPEN` and `NPPM_GETFULLCURRENTPATH`. Native docking uses MultiPad4's Klimov
framework, so this plugin targets MultiPad4 with a matching framework/toolchain;
it is not a standalone Notepad++ docking plugin.

The host supports two optional C exports for native plugins:

- `BOOL __cdecl multipadPreTranslateMessage(MSG*)`: return TRUE only when input
  was consumed. Called on the UI thread before modeless dialog translation.
  Matepath handles Tab directly because `IsDialogMessage` conflicts with the
  framework's `WM_USER` docking notification.
- `BOOL __cdecl multipadSupportsFastView()`: opt in to command dispatch when a
  large document uses the fast viewer. Without this export, existing plugins
  retain their Scintilla-only command behavior.

All callbacks and window destruction occur before `FreeLibrary`. No STL objects
or pane implementation pointers cross the plugin boundary.

The DLL also includes plugin/product names, a feature description, contributor
attribution, the project homepage, upstream copyright and license-notice fields.
Plugin administration reads these from the version resource when catalogue
metadata is absent, including for disabled plugins; existing catalogue values
remain authoritative. The plugin package includes `THIRD_PARTY.md` and
`Matepath.License.txt` beside the DLL.
