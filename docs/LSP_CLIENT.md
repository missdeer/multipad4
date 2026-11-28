# LSP Client

Native Win32 DLL plugin, using Kate's `addons/lspclient` as the reference for
server lifecycle, shared project servers, revision guards, completion and
signature help. No Qt or KDE runtime is required. The reference license is
included in the plugin distribution.

1. Build normally (or disable with `MULTIPAD4_BUILD_LSP_CLIENT=OFF`).
2. LSP Client > More Options > Configure Servers creates and opens
   `plugins/Config/LSPClient.json` from the bundled example.
3. Install the desired language server separately, and adjust `command` (an
   executable and argument array), `extensions`, `languageId`, `rootMarkers`,
   optional `initializationOptions` and `settings`. Use absolute executable paths
   when necessary. Commands are local programs; edit this user configuration only
   with commands you intend to run. Project files cannot supply executable commands.
4. Save configuration, activate a source file, then choose LSP Client > Restart All LSP Servers.
   LSP remains off until explicitly started in each editor session. Stop restores
   built-in completion. A restart reloads configuration and clears server errors.

Completion triggers after typing an identifier or `.`, with a 200 ms delay.
Ctrl+Alt+Space requests completion explicitly. Ctrl+Alt+Shift+Space requests
signature help; typing `(` or `,` also triggers it. Signature calltips have
clickable overload arrows and highlight the active parameter. Hover is available
from the menu or mouse dwell. Alt+F12 opens the definition. Multiple matches open a results tab. Double-click a row or press Enter to jump to its location. Diagnostics use squiggles and clear immediately when the document
changes. Status / Last Server Message reports startup/protocol errors and server
messages. No feature writes files automatically.

Servers are shared by command and nearest project root. If no root marker is
found, the file's parent directory is used. Only opened/activated, saved-path,
UTF-8 Scintilla documents up to 8 MiB are attached. Untitled documents and fast
views are skipped. Background edits of attached documents are synchronized too.
Save As closes the old URI and attaches the new one. Closing a buffer sends
`didClose`. Local pipes use Content-Length framing; I/O runs outside the UI
thread, with message/queue bounds, initialization/request timeouts and process
job cleanup. Changed ranges are coalesced after edits, preserving UTF-8 and CRLF boundaries;
servers requesting full synchronization receive full text. Replies require matching document revision,
activation epoch, caret and editor focus.

Implemented: initialize/initialized, configuration requests, document
open/change/save/close, completion arrays/lists, insertText and TextEdit /
InsertReplaceEdit (replace range), additional text edits in one undo action,
hover, signature help, definition/declaration/type/implementation navigation,
references, workspace symbol search, document highlights, selection ranges,
document and on-type formatting, and pushed diagnostics.

More Options stores preferences in `plugins/Config/LSPClient.ini`.
Format on save waits up to one second before continuing with the current text.
Incremental synchronization sends the changed range when requested by the server.
With the option off, the client replaces the complete document range.
Completion advertises no snippets and ignores snippet items returned anyway.
Lists are limited to 1000 entries, diagnostics to 2000, calltips to 16000 bytes.

Remaining Kate features: completion resolve/itemDefaults, snippet tab stops,
rich Markdown rendering, diagnostic panel/actions, rename/workspace edits, semantic tokens, inlay hints, dynamic
registration, progress and server-command execution. Unsupported server requests
receive JSON-RPC errors; applyEdit is not advertised. Servers must support UTF-16
positions. Servers requiring dynamic registration may need configuration changes.
The current transport terminates server jobs on Stop; graceful shutdown/exit
negotiation is not yet implemented. Server stderr is retained in
`plugins/Config/LSPClient-server-<editor PID>-<session ID>.log`. Client errors and
server log paths are appended to `plugins/Config/LSPClient-client.log`. Transport
failures report the process exit code when available. A bad diagnostic or feature
response is logged and skipped without terminating an initialized server. Extra
`--log=verbose` output is possible for clangd, but includes source text; use it
only when needed and remove it afterward.

Host extensions are documented in `plugins/PluginApi.h`. A plugin may consume
an editor event before built-in handling; SCN_MODIFIED always reaches host
revision bookkeeping. Legacy plugins keep their original notification contract.
External completion ownership is per buffer and suppresses only the built-in
completion generator, preserving indentation and paired-character behavior.

Validation commands (MSVC build):

```powershell
ctest --test-dir cmake-msvc-build -R '^lsp_' --output-on-failure
# Optional real clangd completion + undo smoke test:
$env:MULTIPAD4_LSP_REAL_SERVER = 'D:/llvm/output/bin/clangd.exe'
ctest --test-dir cmake-msvc-build -R '^lsp_editor_integration$' --output-on-failure
Remove-Item Env:MULTIPAD4_LSP_REAL_SERVER
```

Completion lists marked `isIncomplete` are re-requested every 500 ms for up to
10 seconds while the document, caret and focus still match. This lets clangd
replace its initial fallback list after its first AST build. Mouse dwell ending
only invalidates hover replies. Hover calltips are suppressed while a completion
list is visible: Scintilla's standard CalltipShow cancels autocomplete.

The deterministic test server covers shared document lifecycle, incremental
changes, diagnostics, TextEdit with additional edits in one undo step, calltips,
overload switching, hover, definitions and rejection of delayed responses after
switching tabs. Malformed diagnostic and notification cases verify that one bad
message cannot kill a working server. Nullable signature fields and incomplete
completion retries are covered too. The real-server mode checks clangd completion
insertion, object-dot automatic completion and undo. Set
`MULTIPAD4_LSP_PROJECT_FILE` to an existing `.cpp` to test it with unsaved fixture
edits and the executable directory's compilation database; this mode does not
write to that source file.
Transport checks cover fragmented/coalesced messages, malformed lengths, UTF-16
positions, CRLF and non-BMP edit boundaries, argument quoting and pipe cleanup.

Native completion and signature popup captures from the integration test:

![Completion](screenshots/lsp-completion.png)

![Signature help](screenshots/lsp-signature.png)

![Next overload](screenshots/lsp-overload.png)

Hover calltips anchor to the symbol position captured by the dwell request,
independently of the insertion caret. Signature help anchors to its invocation
position. Integration tests check the popup rectangle against editor coordinates,
including hovering another line while the caret remains at file start.

Taking over completion cancels any existing host list. LSP lists temporarily
disable Scintilla single-choice insertion so every accepted item goes through
LSP TextEdit validation; the setting is restored when the list closes. The
real-server regression types the object name character by character, allowing
identifier suggestions to appear before typing the member-access dot.

If completion falls back to editor keywords, check `LSPClient-client.log` for
server failure. In local testing, the LLVM 24 development clangd recorded exits
with `0xC000001D` during CodeComplete in FileBrowse.cpp; this is a server failure,
and the plugin restores built-in completion afterward. Local Qt Creator clangd
22.1.8 is another installed server that can be selected using its absolute path.

Completion candidates retain server filtering and ranking, including fuzzy matches
and filterText values that differ from display labels. Scintilla displays the
returned list without an extra label-prefix filter; the original word start is
tracked separately for items without an explicit TextEdit.

LSP acceptance follows the editor's Enter/Tab completion options. Tentative or
already-handled character notifications do not trigger requests, and the host's
IME composition state suppresses both requests and late-arriving popup results.
