#pragma once
#include "../src/MainFrame.h"
#include "../src/FrameCommands.h"
#include <source_location>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include "SettingsPerformanceBaseline.h"
inline void RunSettingsProfileTests(const std::filesystem::path& root) {
    auto require=[](bool ok,std::source_location where=std::source_location::current()){if(!ok)throw std::runtime_error("Settings assertion at line "+std::to_string(where.line()));};
    std::filesystem::create_directories(root);
    const auto directory=root/(L"profile-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    require(std::filesystem::create_directory(directory));
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}} cleanup{directory};
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
        // Read caches must not overwrite keys updated by the independent style writer.
        require(restored.ReadString(L"Fixture",L"ReadOnly",L"default")==L"default");
        require(WritePrivateProfileStringW(L"Fixture",L"ReadOnly",L"external",restored.path.c_str())!=FALSE);
        const auto optionKey=L"Command"+std::to_wstring(IDM_VIEW_SHOWEOLS);
        require(WritePrivateProfileStringW(L"Options",optionKey.c_str(),L"0",restored.path.c_str())!=FALSE);
        require(restored.Save());
        AppSettings observed;observed.Initialize(true);observed.path=restored.path;observed.persistent=true;
        require(observed.ReadString(L"Fixture",L"ReadOnly")==L"external");require(observed.Get(IDM_VIEW_SHOWEOLS)==0);
        AppSettings retry;retry.Initialize(true);retry.persistent=true;
        {std::ofstream blocker(directory/L"blocker");blocker<<"x";}
        retry.path=directory/L"blocker"/L"settings.ini";retry.Set(998877,42);require(!retry.Save());
        retry.path=directory/L"retry.ini";require(retry.Save());
        AppSettings retryRead;retryRead.path=retry.path;require(retryRead.Get(998877)==42);
        require(observed.ReadString(L"Fixture",L"Missing",L"fallback")==L"fallback");
        observed.WriteString(L"Fixture",L"Empty",L"");
        observed.WriteString(L"Fixture",L"Long",std::wstring(20000,L'x'));
        require(observed.Get(999999,37)==37);require(observed.Save());
        AppSettings checked;checked.Initialize(true);checked.path=observed.path;checked.persistent=true;
        require(checked.ReadString(L"Fixture",L"Empty",L"nonempty").empty());
        require(checked.ReadString(L"Fixture",L"Long")==std::wstring(20000,L'x'));
        require(checked.Get(999999,0)==37);
        checked.RecordRecentFile(L"C:/one.txt",4096);checked.RecordRecentFile(L"C:/two.txt",4096);checked.RecordRecentFile(L"c:/ONE.txt",4096);
        require(checked.ReadString(L"Recent Files",L"0")==L"c:/ONE.txt" && checked.ReadString(L"Recent Files",L"1")==L"C:/two.txt");
        checked.WriteString(L"Recent Files",L"0",L"");checked.RecordRecentFile(L"C:/three.txt",32);
        require(checked.ReadString(L"Recent Files",L"1")==L"C:/two.txt");require(checked.Save());
        if(GetEnvironmentVariableW(L"MULTIPAD4_SETTINGS_BENCHMARK",nullptr,0)){
        std::ofstream perf(directory/L"settings-pairs.csv");perf<<"case,mode,sample,milliseconds\n";
        for(int sample=0;sample<5;++sample){
            auto measure=[&](const char* name,const char* mode,auto action){auto begin=std::chrono::steady_clock::now();action();perf<<name<<','<<mode<<','<<sample<<','<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<'\n';};
            measure("nonpersistent-misses","before",[&]{settingsBaseline::AppSettings p;p.persistent=false;for(int i=0;i<512;++i)require(p.ReadString(L"x",std::to_wstring(i).c_str())==L"");});
            measure("nonpersistent-misses","after",[&]{AppSettings p;p.persistent=false;for(int i=0;i<512;++i)require(p.ReadString(L"x",std::to_wstring(i).c_str())==L"");});
            for(bool before:{true,false})measure("bulk-open-exit-4096",before?"before":"after",[&]{
                auto path=directory/(std::wstring(before?L"before":L"after")+std::to_wstring(sample)+L".ini");std::filesystem::remove(path);
                if(before){settingsBaseline::AppSettings p;p.path=path;for(int opened=0;opened<16;++opened){std::wstring name=L"C:/"+std::to_wstring(opened)+L".txt";std::vector<std::wstring> paths{name};for(int i=0;i<4096;++i){auto old=p.ReadString(L"Recent Files",std::to_wstring(i).c_str());if(!old.empty()&&old!=name)paths.push_back(old);}for(int i=0;i<4096;++i)p.WriteString(L"Recent Files",std::to_wstring(i).c_str(),i<static_cast<int>(paths.size())?paths[i]:L"");}require(p.Save());}
                else {AppSettings p;p.path=path;for(int opened=0;opened<16;++opened)p.RecordRecentFile(L"C:/"+std::to_wstring(opened)+L".txt",4096);require(p.Save());}
            });
        }
        perf.close();require(bool(perf));std::filesystem::copy_file(directory/L"settings-pairs.csv",std::filesystem::current_path()/L"settings-pairs.csv",std::filesystem::copy_options::overwrite_existing);
        }
}
inline void RunRuntimeFeatureTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Runtime feature assertion at line " + std::to_string(where.line()));
    };
    auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-runtime-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    AppSettings original = frame.settings;
    frame.settings.path = directory / L"Settings.ini"; frame.settings.persistent = false;
    frame.KillTimer(1);
    struct RestoreTimer { MainFrame& frame; ~RestoreTimer() { frame.SetTimer(1, 1000); } } restoreTimer{frame};
    auto waitFor = [&](const auto& done) {
        const auto deadline = GetTickCount64() + 3000;
        while (!done()) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            if (done()) break;
            const auto now = GetTickCount64();
            require(now < deadline);
            MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(deadline - now), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        }
    };
    try {
        RunSettingsProfileTests(directory);
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
        require(first->runtime.monitorMode == 2 && second->runtime.monitorMode == 2);
        files::WriteAtomic(first->path, {"disk replacement"});
        waitFor([&] { return first->Text() == "disk replacement"; });
        require(first->Text() == "disk replacement" && !first->Modified() && second->Text() == "second?");
        // Opening an existing file also enables automatic reload without configuration.
        const auto openedPath = directory / L"opened.txt";
        files::WriteAtomic(openedPath, {"before"});
        require(frame.Open(openedPath)); auto* opened = frame.Active();
        require(opened->runtime.monitorMode == 2);
        opened->Send(SCI_GOTOPOS, 3);
        files::WriteAtomic(openedPath, {"after external edit"});
        waitFor([&] { return opened->Text() == "after external edit"; });
        require(opened->Text() == "after external edit" && !opened->Modified());
        require(opened->Send(SCI_GETCURRENTPOS) == 3 && first->Text() == "disk replacement");
        // Changes received during modal operations wait until the operation ends.
        {
            FrameOperation operation(frame.runtime, frame);
            files::WriteAtomic(openedPath, {"deferred replacement"});
            waitFor([&] { return opened->runtime.externalCheckPending; });
            require(opened->Text() == "after external edit");
        }
        waitFor([&] { return opened->Text() == "deferred replacement"; });
        // A stale subscription token cannot target a reopened/reconfigured document.
        const auto staleToken = opened->runtime.directoryWatch->token;
        opened->runtime.directoryWatch.reset(); SnapshotDocument(*opened);
        require(opened->runtime.directoryWatch->token != staleToken);
        opened->runtime.externalCheckPending = false;
        frame.SendMessage(WM_FILE_CHANGE, opened->searchIdentity, staleToken);
        require(!opened->runtime.externalCheckPending);
        // Deleting then recreating a watched path keeps the directory subscription alive.
        std::filesystem::remove(openedPath);
        files::WriteAtomic(openedPath, {"recreated replacement"});
        waitFor([&] { return opened->Text() == "recreated replacement"; });
        // Retry a failed automatic reload on activation without another filesystem event.
        {
            FrameOperation operation(frame.runtime, frame);
            files::WriteAtomic(openedPath, {"retry after sharing denial"});
            waitFor([&] { return opened->runtime.externalCheckPending; });
        }
        HANDLE denied = CreateFileW(openedPath.c_str(), GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(denied != INVALID_HANDLE_VALUE);
        static unsigned failureDialogs = 0; failureDialogs = 0;
        auto dismissFailure = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR, DWORD) {
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
                wchar_t type[32]{}; GetClassNameW(window, type, _countof(type));
                if (!wcscmp(type, L"#32770")) {
                    if (!GetPropW(window, L"MultiPad4.CountedReloadFailure")) { SetPropW(window, L"MultiPad4.CountedReloadFailure", reinterpret_cast<HANDLE>(1)); ++failureDialogs; }
                    PostMessageW(window, WM_COMMAND, IDOK, 0);
                }
                return TRUE;
            }, 0);
        });
        CheckExternalChanges(frame);
        require(failureDialogs == 1);
        // Pump everything queued by the error dialog while the denial remains active.
        for (int turn = 0; turn < 3; ++turn) {
            MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            frame.SendMessage(WM_ENABLE, TRUE);
            frame.SendMessage(WM_FILE_CHANGE, first->searchIdentity, first->runtime.directoryWatch->token);
        }
        require(failureDialogs == 1);
        KillTimer(nullptr, dismissFailure); CloseHandle(denied);
        require(opened->Text() == "recreated replacement" && opened->runtime.externalChangePending);
        require(frame.runtime.pendingExternalChecks.contains(opened->searchIdentity));
        require(!opened->runtime.externalCheckPending);
        CheckExternalChanges(frame);
        require(opened->Text() == "retry after sharing denial" && !opened->runtime.externalChangePending);
        // An outstanding read can be cancelled when its tab closes.
        files::WriteAtomic(openedPath, {"closing replacement"});
        require(frame.Close(frame.tabs.GetActivePage()));
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
        waitFor([&] { return first->runtime.observedSize == std::string("next disk version").size(); });
        KillTimer(nullptr, timer);
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
