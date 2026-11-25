#include "FrameCommands.h"
#include "MainFrame.h"
#include <shellapi.h>
#include "StyleManager.h"
#include "AutoSave.h"
#include <chrono>
#include <system_error>
void SnapshotDocument(EditorDocument& document) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    auto& state = document.runtime;
    if (document.path.empty() || !state.monitorMode) state.directoryWatch.reset();
    else if (!state.directoryWatch || state.directoryWatch->path != document.path) {
        state.directoryWatch.reset();
        const auto owner = GetAncestor(document.hwnd, GA_ROOT);
        state.directoryWatch = std::make_unique<DirectoryWatch>(owner, document.searchIdentity, document.path);
        if (!state.directoryWatch->Arm()) PostMessageW(owner, WM_FILE_CHANGE, document.searchIdentity, state.directoryWatch->token);
    }
    state.snapshotValid = !document.path.empty() && GetFileAttributesExW(document.path.c_str(), GetFileExInfoStandard, &data);
    if (state.snapshotValid) {
        state.observedWriteTime = data.ftLastWriteTime;
        state.observedSize = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    }
    state.externalChangePending = false;
    state.externalCheckPending = false;
}
void RemoveDocumentRecovery(MainFrame& frame, EditorDocument& document, bool keepBackup) {
    RemoveDocumentRecoveries(frame, document, keepBackup);
}
void BackupDocuments(MainFrame& frame, bool force) {
    if (!frame.IsWindowEnabled() || frame.runtime.operationDepth) return;
    const int options = frame.settings.Get(IDM_SET_FILE_AUTOSAVE, 6);
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG period = AutoSavePeriodMilliseconds(frame);
    if (!force && period == 0) return;
    std::vector<HWND> windows; for (const auto& document : frame.documents) windows.push_back(document->hwnd);
    for (HWND window : windows) {
        auto found = std::find_if(frame.documents.begin(), frame.documents.end(), [&](const auto& d) { return d->hwnd == window; });
        if (found == frame.documents.end()) continue;
        auto& d = **found;
        if (d.LargeFile()) continue;
        if (!d.Modified() || d.Send(SCI_GETLENGTH) == 0 || (!force && (now - d.runtime.lastAutoSave < period))) continue;
        CancelDocumentBackup(frame, d);
        d.runtime.lastAutoSave = now;
        if (!force && (options & 16) && !d.path.empty()) {
            // EndSession-style autosave must retain the dirty buffer and must
            // never enter SaveTo's encoding/error dialogs from a timer.
            const auto revision = d.style.revision;
            const auto encoding = static_cast<unsigned>(d.encoding);
            const int eol = static_cast<int>(d.Send(SCI_GETEOLMODE));
            if (d.runtime.lastRecoveryRevision == revision && d.runtime.lastRecoveryEncoding == encoding
                && d.runtime.lastRecoveryEol == eol) continue;
            try {
                files::WriteAtomic(d.path, {d.Text(), d.encoding});
                SnapshotDocument(d);
                d.runtime.lastRecoveryRevision = revision;
                d.runtime.lastRecoveryEncoding = encoding;
                d.runtime.lastRecoveryEol = eol;
                continue;
            } catch (const std::exception&) {
                // The upstream implementation falls back to a backup.
            }
        }
        try {
            SaveDocumentRecovery(frame, d, force);
        } catch (const std::exception&) {} // AutoSave_DoWork is silent on failure.
    }
}
void TickBackgroundWork(MainFrame& frame) {
    if (!frame.IsWindowEnabled() || frame.runtime.operationDepth || frame.runtime.inBackgroundWork) return;
    if (frame.settings.Get(IDM_SET_FILE_AUTOSAVE, 6) & 1) QueueDocumentBackups(frame);
}
void CheckExternalChanges(MainFrame& frame, bool retryPendingChanges) {
    if (frame.runtime.inBackgroundWork) return;
    frame.runtime.inBackgroundWork = true;
    struct Reset { MainFrame& frame; ~Reset() {
        frame.runtime.inBackgroundWork = false;
        if (!frame.runtime.pendingCommands.empty() || !frame.runtime.pendingTabCloses.empty()) frame.PostMessage(WM_APP + 0x104);
        if (frame.IsWindowEnabled() && !frame.runtime.operationDepth && std::any_of(frame.runtime.pendingExternalChecks.begin(), frame.runtime.pendingExternalChecks.end(),
            [&](auto id) { const auto found = frame.tabs.documentsById.find(id); return found != frame.tabs.documentsById.end() && found->second->runtime.externalCheckPending; })) frame.PostMessage(WM_FILE_CHANGE);
    } } reset{frame};
    // Modal commands own a stable document snapshot until OK/Cancel. A notification
    // must not reload that buffer underneath a pending edit or settings dialog.
    if (!frame.IsWindowEnabled() || frame.runtime.operationDepth) return;
    const std::vector<UINT_PTR> identities(frame.runtime.pendingExternalChecks.begin(), frame.runtime.pendingExternalChecks.end());
    for (const auto identity : identities) {
        frame.runtime.pendingExternalChecks.erase(identity);
        auto found = frame.tabs.documentsById.find(identity);
        if (found == frame.tabs.documentsById.end()) continue;
        auto& d = *found->second; auto& state = d.runtime;
        if (!state.monitorMode || d.path.empty()) { state.externalCheckPending = false; continue; }
        if (!state.externalCheckPending && !state.externalChangePending) continue;
        if (!state.externalCheckPending && !retryPendingChanges) { frame.runtime.pendingExternalChecks.insert(identity); continue; }
        state.externalCheckPending = false;
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(d.path.c_str(), GetFileExInfoStandard, &data)) continue;
        ULONGLONG size = (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        bool changed = !state.snapshotValid || CompareFileTime(&data.ftLastWriteTime, &state.observedWriteTime) != 0 || size != state.observedSize;
        if (changed) {
            state.observedWriteTime = data.ftLastWriteTime; state.observedSize = size; state.snapshotValid = true;
            state.externalChangePending = true;
        }
        if (!state.externalChangePending) continue;
        if (state.monitorMode == 2 && !d.Modified()) {
            const auto reloaded = frame.Reload(d);
            const auto current = frame.tabs.documentsById.find(identity);
            if (current != frame.tabs.documentsById.end()) {
                auto& live = *current->second;
                if (reloaded) { if (live.runtime.keepAtEnd) live.Send(SCI_GOTOPOS, live.Send(SCI_GETLENGTH)); }
                else {
                    live.runtime.externalChangePending = true; live.runtime.externalCheckPending = false;
                    frame.runtime.pendingExternalChecks.insert(identity);
                }
            }
            continue;
        }
        if (GetForegroundWindow() != frame.m_hWnd) { frame.runtime.pendingExternalChecks.insert(identity); continue; }
        state.externalChangePending = false;
        const auto question = L"The file changed on disk:\n" + d.path.wstring() + L"\n\nReload it? Unsaved changes in this document will be discarded.";
        if (frame.MessageBox(question.c_str(), L"File Change Notification", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            // A modal prompt can pump tab closures; reacquire by identity.
            const auto current = frame.tabs.documentsById.find(identity);
            if (current != frame.tabs.documentsById.end()) {
                auto& live = *current->second;
                if (frame.Reload(live) && live.runtime.keepAtEnd) live.Send(SCI_GOTOPOS, live.Send(SCI_GETLENGTH));
            }
        }
    }
}
LRESULT MainFrame::OnFileChange(UINT, WPARAM identity, LPARAM token, BOOL&) {
    auto changes = DirectoryWatch::Drain(m_hWnd);
    if (identity) changes.push_back({identity, static_cast<UINT_PTR>(token), ERROR_SUCCESS});
    DWORD failure = ERROR_SUCCESS;
    for (const auto& change : changes) {
        const auto found = tabs.documentsById.find(change.identity);
        if (found == tabs.documentsById.end()) continue;
        auto& state = found->second->runtime;
        auto* watch = state.directoryWatch.get();
        if (!watch || watch->token != change.token) continue;
        if (change.error != ERROR_SUCCESS || !watch->Arm()) {
            failure = change.error != ERROR_SUCCESS ? change.error : watch->error.load();
            state.directoryWatch.reset(); continue;
        }
        state.externalCheckPending = true;
        runtime.pendingExternalChecks.insert(change.identity);
    }
    if (failure != ERROR_SUCCESS) Error(std::system_error(failure, std::system_category(), "Cannot monitor external file changes"));
    CheckExternalChanges(*this, false);
    return 0;
}
LRESULT MainFrame::OnWatchEnable(UINT, WPARAM enabled, LPARAM, BOOL& handled) {
    if (enabled && !runtime.inBackgroundWork && std::any_of(runtime.pendingExternalChecks.begin(), runtime.pendingExternalChecks.end(), [&](auto identity) {
        const auto found = tabs.documentsById.find(identity);
        return found != tabs.documentsById.end() && found->second->runtime.externalCheckPending;
    })) PostMessage(WM_FILE_CHANGE);
    handled = FALSE; return 0;
}
LRESULT MainFrame::OnTimer(UINT, WPARAM id, LPARAM, BOOL& handled) {
    if (id == 0x282) { searchDialog.ServiceReplacement(); return 0; }
    if (id == 3) {
        if (!IsWindowEnabled() || runtime.operationDepth || runtime.inBackgroundWork) return 0;
        auto* activeDocument = Active();
        const auto before = activeDocument ? activeDocument->style.occurrences : 0;
        const bool wasPending = activeDocument && activeDocument->style.occurrencePending;
        PumpDocumentBackup(*this);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4);
        // The first slice favors the active buffer; the rest rotates fairly.
        if (auto* d = Active(); d && d->style.occurrencePending) ProcessStyleBackground(*d, 1);
        for (size_t visited = 0; visited < documents.size() && std::chrono::steady_clock::now() < deadline; ++visited) {
            auto& d = *documents[runtime.styleCursor++ % documents.size()];
            if (d.style.occurrencePending) ProcessStyleBackground(d, 1);
        }
        if (activeDocument && (before != activeDocument->style.occurrences || wasPending != activeDocument->style.occurrencePending)) UpdateStatus();
        if (!runtime.autoSaveTask && !std::any_of(documents.begin(), documents.end(), [](const auto& d) { return d->style.occurrencePending; })) {
            KillTimer(3); runtime.workTimerArmed = false;
        }
        return 0;
    }
    if (id == 2) { UpdateStatus(); return 0; }
    if (id != 1) { handled = FALSE; return 0; }
    TickBackgroundWork(*this);
    if (appUpdater.enabled && IsWindowEnabled() && !runtime.operationDepth && !runtime.inBackgroundWork && !pendingPluginMaintenance) {
        FrameOperation operation(runtime, m_hWnd);
        appUpdater.Poll(*this);
    }
    return 0;
}
LRESULT MainFrame::OnActivateApp(UINT, WPARAM active, LPARAM, BOOL& handled) {
    SetFrameTransparency(*this, active != 0); if (active) { TickBackgroundWork(*this); CheckExternalChanges(*this); }
    handled = FALSE; return 0;
}
LRESULT MainFrame::OnPower(UINT, WPARAM event, LPARAM, BOOL& handled) {
    if (event == PBT_APMSUSPEND && (settings.Get(IDM_SET_FILE_AUTOSAVE, 6) & 2)) BackupDocuments(*this, true);
    handled = FALSE; return TRUE;
}
LRESULT MainFrame::OnQueryEndSession(UINT, WPARAM, LPARAM, BOOL&) {
    if (settings.Get(IDM_SET_FILE_AUTOSAVE, 6) & 4) BackupDocuments(*this, true);
    FrameOperation operation(runtime, m_hWnd);
    // The OS can cancel after this query, including when another application
    // vetoes shutdown. Keep every editor alive until WM_ENDSESSION confirms it.
    std::vector<HWND> windows; for (const auto& d : documents) windows.push_back(d->hwnd);
    for (HWND window : windows) {
        auto found = std::find_if(documents.begin(), documents.end(), [&](const auto& d) { return d->hwnd == window; });
        if (found != documents.end() && !ConfirmClose(**found, true)) return FALSE;
    }
    if (settings.Get(IDM_VIEW_SAVESETTINGS, 1)) { SaveFramePreferences(*this); settings.Save(); }
    else settings.SaveOption(IDM_VIEW_SAVESETTINGS);
    return TRUE;
}
LRESULT MainFrame::OnEndSession(UINT, WPARAM ending, LPARAM, BOOL&) {
    if (ending) {
        for (auto& d : documents) { d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; }
        PostMessage(WM_CLOSE);
    }
    return 0;
}
LRESULT MainFrame::OnTray(UINT, WPARAM, LPARAM event, BOOL&) {
    if (LOWORD(event) == WM_LBUTTONUP || LOWORD(event) == WM_LBUTTONDBLCLK) {
        SetTrayVisible(*this, false); ShowWindow(SW_RESTORE); SetForegroundWindow(m_hWnd);
    } else if (LOWORD(event) == WM_RBUTTONUP) {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_TRAY_RESTORE, L"Restore MultiPad4");
        AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");
        POINT cursor{}; GetCursorPos(&cursor); SetForegroundWindow(m_hWnd);
        auto command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, m_hWnd, nullptr);
        DestroyMenu(menu);
        if (command == IDM_TRAY_RESTORE) { SetTrayVisible(*this, false); ShowWindow(SW_RESTORE); }
        else if (command == IDM_TRAY_EXIT) PostMessage(WM_CLOSE);
        PostMessage(WM_NULL);
    }
    return 0;
}
