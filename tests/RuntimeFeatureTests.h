#pragma once
#include "../src/MainFrame.h"
#include "../src/FrameCommands.h"
#include <source_location>
#include <stdexcept>
inline void RunRuntimeFeatureTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Runtime feature assertion at line " + std::to_string(where.line()));
    };
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-runtime-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    AppSettings original = frame.settings;
    frame.settings.path = directory / L"Settings.ini"; frame.settings.persistent = false;
    try {
        // INI keeps Unicode, booleans and independent caller sections across instances.
        AppSettings settings; settings.Initialize(true); settings.path = directory / L"Unicode.ini"; settings.persistent = true;
        settings.Set(IDM_VIEW_SHOWEOLS, 1); settings.WriteString(L"Font", L"Name", L"中文字体 😀");
        const std::wstring quotedCommand = L"\"C:\\中文路径\\program.exe\" \"quoted argument\"";
        const std::wstring multiline = L"  中文😀\r\nsecond line  ";
        settings.WriteString(L"Fixture", L"Command", quotedCommand); settings.WriteString(L"Fixture", L"Multiline", multiline);
        require(settings.Save());
        AppSettings restored; restored.Initialize(true); restored.path = settings.path; restored.persistent = true;
        require(restored.Get(IDM_VIEW_SHOWEOLS) == 1 && restored.ReadString(L"Font", L"Name") == L"中文字体 😀");
        require(restored.ReadString(L"Fixture", L"Command") == quotedCommand && restored.ReadString(L"Fixture", L"Multiline") == multiline);
        AppSettings disabled; disabled.Initialize(true); disabled.path = directory / L"Disabled.ini"; disabled.persistent = true;
        disabled.Set(IDM_VIEW_SHOWEOLS, 1); disabled.Set(IDM_VIEW_SAVESETTINGS, 0);
        require(disabled.SaveOption(IDM_VIEW_SAVESETTINGS));
        AppSettings disabledReload; disabledReload.Initialize(true); disabledReload.path = disabled.path; disabledReload.persistent = true;
        require(disabledReload.Get(IDM_VIEW_SAVESETTINGS, 1) == 0 && disabledReload.Get(IDM_VIEW_SHOWEOLS, 0) == 0);
        frame.NewDocument(); auto* first = frame.Active();
        first->Load({"first"}); require(frame.SaveTo(*first, directory / L"first.txt"));
        frame.NewDocument(); auto* second = frame.Active();
        second->Load({"second"}); require(frame.SaveTo(*second, directory / L"second.txt"));
        first->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("!"));
        second->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("?"));
        frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1);
        BackupDocuments(frame, true);
        require(!first->runtime.recoveryPath.empty() && !second->runtime.recoveryPath.empty());
        require(first->runtime.recoveryPath != second->runtime.recoveryPath);
        require(files::Read(first->runtime.recoveryPath).utf8 == "first!");
        require(files::Read(second->runtime.recoveryPath).utf8 == "second?");
        require(first->Modified() && second->Modified());
        require(files::Read(first->path).utf8 == "first" && files::Read(second->path).utf8 == "second");
        auto recovery = first->runtime.recoveryPath;
        require(frame.SaveTo(*first, first->path)); require(!std::filesystem::exists(recovery));
        require(!first->Modified() && second->Modified());
        // A background document watches its own path even when another is active.
        first->runtime.monitorMode = 2;
        files::WriteAtomic(first->path, {"disk replacement"});
        TickBackgroundWork(frame);
        require(first->Text() == "disk replacement" && second->Text() == "second?");
        // A dirty document is never overwritten by automatic reload.
        first->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("!"));
        files::WriteAtomic(first->path, {"next disk version"});
        // Render-mode tests have a visible owner, so explicitly decline the
        // real notification instead of relying on a hidden test window.
        auto timer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR, DWORD) {
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
                wchar_t title[128]{}; GetWindowTextW(window, title, _countof(title));
                if (wcscmp(title, L"File Change Notification") == 0) PostMessageW(window, WM_COMMAND, IDNO, 0);
                return TRUE;
            }, 0);
        });
        TickBackgroundWork(frame); KillTimer(nullptr, timer);
        require(first->Text() == "disk replacement!"); first->runtime.monitorMode = 0;
        // WM_QUERYENDSESSION may be followed by a cancelled shutdown. Declining
        // save during the query must never destroy the editor or clear dirty state.
        auto shutdownTimer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR, DWORD) {
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
                wchar_t type[32]{}, title[128]{}; GetClassNameW(window, type, _countof(type)); GetWindowTextW(window, title, _countof(title));
                if (!wcscmp(type, L"#32770") && !wcscmp(title, L"MultiPad4")) PostMessageW(window, WM_COMMAND, IDNO, 0);
                return TRUE;
            }, 0);
        });
        auto count = frame.documents.size();
        require(frame.SendMessage(WM_QUERYENDSESSION) == TRUE); KillTimer(nullptr, shutdownTimer);
        frame.SendMessage(WM_ENDSESSION, FALSE);
        require(frame.documents.size() == count && first->Modified() && second->Modified());
        // Full command options survive routing to a previously opened document.
        std::wstring query = second->path.wstring();
        COPYDATASTRUCT queryData{0x4D503451, static_cast<DWORD>((query.size() + 1) * sizeof(wchar_t)), query.data()};
        require(frame.SendMessage(WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&queryData)) == TRUE);
        auto payload = directory.wstring() + L"\n" + second->path.wstring() + L"\nmultipad4.exe /g 1,3 /ro ignored.txt";
        COPYDATASTRUCT routed{0x4D503452, static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t)), payload.data()};
        {
            FrameOperation operation(frame.runtime, frame);
            require(frame.SendMessage(WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&routed)) == TRUE);
            require(!frame.runtime.pendingCommands.empty());
        }
        frame.SendMessage(WM_APP + 0x104);
        require(frame.Active() == second && second->Send(SCI_GETCURRENTPOS) == 2 && second->Send(SCI_GETREADONLY));
        require(frame.documents.size() == count && frame.runtime.pendingCommands.empty());
        second->Send(SCI_SETREADONLY, FALSE);
        frame.SendMessage(WM_COMMAND, BME_EDIT_BOOKMARKTOGGLE);
        require(second->Send(SCI_MARKERGET, 0) & 1);
        frame.SendMessage(WM_COMMAND, BME_EDIT_BOOKMARKCLEAR);
        require(!(second->Send(SCI_MARKERGET, 0) & 1));
        for (auto* d : {second, first}) { d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false; }
        require(frame.Close(frame.tabs.GetActivePage()));
        // first was inserted immediately before second and is now active.
        require(frame.Active() == first); require(frame.Close(frame.tabs.GetActivePage()));
        frame.settings = original;
        std::filesystem::remove_all(directory);
    } catch (...) {
        frame.settings = original;
        std::error_code error; std::filesystem::remove_all(directory, error); throw;
    }
}
