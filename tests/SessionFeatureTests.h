#pragma once
#include "../src/MainFrame.h"
#include "../src/FileCommands.h"
#include "../src/Session.h"
#include "QAArtifacts.h"
#include <source_location>
#include <stdexcept>

// Real files, MDI children and Scintilla state; no extra process or user INI.
inline void RunSessionFeatureTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Session feature assertion at line " + std::to_string(where.line()));
    };
    const int baseline = frame.tabs.GetPageCount();
    auto* originalActive = frame.Active();
    const auto originalSettings = frame.settings;
    const auto originalUntitledCount = frame.untitledCount;
    const auto originalPasteBoard = frame.runtime.pasteBoard;
    const auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-session-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    frame.settings.Initialize(true); frame.settings.path = directory / L"settings.ini"; frame.settings.persistent = false;
    frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 0);
    auto cleanup = [&] {
        while (frame.tabs.GetPageCount() > baseline) {
            auto* d = static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetPageCount() - 1));
            d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false;
            frame.Close(frame.tabs.GetPageCount() - 1);
        }
        frame.settings = originalSettings; frame.untitledCount = originalUntitledCount;
        frame.runtime.pasteBoard = originalPasteBoard;
        for (int i = 0; originalActive && i < baseline; ++i) if (frame.tabs.GetPageData(i) == originalActive) frame.tabs.SetActivePage(i);
        std::error_code error; std::filesystem::remove_all(directory, error);
    };
    try {
        const auto firstPath = directory / L"会话 first.cpp";
        const auto secondPath = directory / L"second.txt";
        files::WriteAtomic(firstPath, {"int first = 1;\nint second = 2;\n", files::Encoding::Utf16LE});
        files::WriteAtomic(secondPath, {"on disk\n", files::Encoding::Utf8Bom});
        require(frame.Open(firstPath)); auto* first = frame.Active();
        require(frame.Open(secondPath)); auto* second = frame.Active();
        auto* untitled = frame.NewDocument(); require(untitled != nullptr);
        untitled->untitled = L"会话 \"title\" "; untitled->encoding = files::Encoding::Utf16BE;
        untitled->Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("discard untitled text"));
        first->style.wrapMode = SC_WRAP_CHAR; first->style.wrapIndent = 7;
        first->style.wrapSymbols = 11; first->style.wrapSymbolsShown = true;
        first->style.fontKind = 0; first->style.codeFont = L"Consolas"; first->style.codeFontSize = 15;
        first->style.useSchemeTabs = false; first->style.tabWidth = 7; first->style.indentWidth = 3;
        first->style.autoIndent = false; first->style.completionIgnoreCase = true;
        first->style.overrides[0] = L"font:Consolas; size:14; fore:#112233";
        first->style.completionOptions[1] = 3; first->ConfigureLexer();
        first->Send(SCI_SETTABWIDTH, 7); first->Send(SCI_SETINDENT, 3); first->Send(SCI_SETUSETABS, FALSE);
        first->Send(SCI_SETWRAPMODE, SC_WRAP_CHAR); first->Send(SCI_SETZOOM, 135);
        first->Send(SCI_SETVIEWWS, SCWS_VISIBLEALWAYS); first->Send(SCI_SETVIEWEOL, TRUE);
        first->Send(SCI_SETMULTIPLESELECTION, TRUE); first->Send(SCI_SETSELECTION, 8, 4);
        first->Send(SCI_ADDSELECTION, 25, 19); first->Send(SCI_SETMAINSELECTION, 0);
        first->Send(SCI_MARKERADD, 1, 0); first->Send(SCI_SETREADONLY, TRUE);
        first->search.findText = L" quoted=\"汉字\" \n"; first->search.replaceText = L"\\path\\";
        first->search.closeAfterFind = true; first->search.transparent = true;
        first->search.findHistory = {L"first", L"汉字\nsecond"};
        first->runtime.timestampTemplate = L"\"custom\" ";
        second->Send(SCI_APPENDTEXT, 9, reinterpret_cast<LPARAM>("discarded"));
        second->SetEncoding(files::Encoding::Utf16BE); // Declined encoding changes must be discarded too.
        require(frame.GetMDITabCtrl().MoveItem(baseline + 1, baseline));
        frame.tabs.SetActivePage(baseline + 1); require(frame.Active() == first);
        const auto session = directory / L"roundtrip.ini";
        SaveRelaunchSession(frame, session, true, first);
        // Keep unrelated smoke-test tabs out of the restore operation. Copy raw
        // UTF-16 profile values so AppSettings' string escaping stays intact.
        std::vector<std::vector<wchar_t>> sections;
        for (int i = 0; i < 3; ++i) {
            std::vector<wchar_t> buffer(65536);
            auto section = L"Document" + std::to_wstring(baseline + i);
            auto size = GetPrivateProfileSectionW(section.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()), session.c_str());
            require(size && size < buffer.size() - 2); sections.push_back(std::move(buffer));
        }
        for (int i = 0; i < 3; ++i) require(WritePrivateProfileSectionW((L"Document" + std::to_wstring(i)).c_str(), sections[i].data(), session.c_str()) != FALSE);
        require(WritePrivateProfileStringW(L"Session", L"Count", L"3", session.c_str()) != FALSE);
        require(WritePrivateProfileStringW(L"Session", L"Active", L"1", session.c_str()) != FALSE);
        // Discard is simulated only after the snapshot has observed the dirty flags.
        while (frame.tabs.GetPageCount() > baseline) {
            const int last = frame.tabs.GetPageCount() - 1;
            auto* d = static_cast<EditorDocument*>(frame.tabs.GetPageData(last));
            d->Send(SCI_SETSAVEPOINT); d->encodingDirty = false;
            require(frame.Close(last));
        }
        RestoreRelaunchSession(frame, session);
        require(frame.tabs.GetPageCount() == baseline + 3);
        second = static_cast<EditorDocument*>(frame.tabs.GetPageData(baseline));
        first = static_cast<EditorDocument*>(frame.tabs.GetPageData(baseline + 1));
        untitled = static_cast<EditorDocument*>(frame.tabs.GetPageData(baseline + 2));
        require(second->path == secondPath && first->path == firstPath && frame.Active() == first);
        require(first->encoding == files::Encoding::Utf16LE && second->encoding == files::Encoding::Utf8Bom);
        require(first->Text() == "int first = 1;\nint second = 2;\n" && second->Text() == "on disk\n");
        require(untitled->path.empty() && untitled->Text().empty() && !untitled->Modified());
        require(untitled->encoding == files::Encoding::Utf16BE && untitled->untitled == L"会话 \"title\" ");
        require(first->Send(SCI_GETREADONLY) && first->Send(SCI_GETZOOM) == 135);
        require(first->Send(SCI_GETWRAPMODE) == SC_WRAP_CHAR && first->style.wrapMode == SC_WRAP_CHAR);
        require(first->style.wrapIndent == 7 && first->Send(SCI_GETWRAPINDENTMODE) == SC_WRAPINDENT_DEEPINDENT);
        require(first->Send(SCI_GETTABWIDTH) == 7 && first->Send(SCI_GETINDENT) == 3);
        require(first->style.codeFontSize == 15 && !first->style.autoIndent && first->style.completionIgnoreCase);
        require(first->style.overrides[0] == L"font:Consolas; size:14; fore:#112233" && first->style.completionOptions[1] == 3);
        require(first->Send(SCI_GETSELECTIONS) == 2 && first->Send(SCI_GETMAINSELECTION) == 0);
        require(!first->Send(SCI_GETMOVEEXTENDSSELECTION));
        require(first->Send(SCI_GETSELECTIONNCARET, 0) == 8 && first->Send(SCI_GETSELECTIONNANCHOR, 0) == 4);
        require(first->Send(SCI_GETSELECTIONNCARET, 1) == 25 && first->Send(SCI_GETSELECTIONNANCHOR, 1) == 19);
        require(first->Send(SCI_GETVIEWWS) == SCWS_VISIBLEALWAYS && first->Send(SCI_GETVIEWEOL));
        require(first->Send(SCI_MARKERGET, 1) & 1);
        require(first->search.findText == L" quoted=\"汉字\" \n" && first->search.replaceText == L"\\path\\");
        require(first->search.findHistory == std::vector<std::wstring>({L"first", L"汉字\nsecond"}));
        require(first->search.closeAfterFind && first->search.transparent && first->runtime.timestampTemplate == L"\"custom\" ");
        require(files::Read(secondPath).utf8 == "on disk\n");
        require(std::filesystem::exists(session)); // Caller-owned sessions are retained.
        // Clone only the selected tab and retain a rectangular selection.
        first->Send(SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE);
        first->Send(SCI_SETRECTANGULARSELECTIONANCHOR, 4); first->Send(SCI_SETRECTANGULARSELECTIONCARET, 21);
        auto rectAnchor = first->Send(SCI_GETRECTANGULARSELECTIONANCHOR), rectCaret = first->Send(SCI_GETRECTANGULARSELECTIONCARET);
        const auto clone = directory / L"clone.ini";
        SaveRelaunchSession(frame, clone, false, first);
        require(frame.Close(baseline + 1)); RestoreRelaunchSession(frame, clone);
        first = frame.Active(); require(frame.tabs.GetPageCount() == baseline + 3 && first->path == firstPath);
        require(first->Send(SCI_GETSELECTIONMODE) == SC_SEL_RECTANGLE);
        require(first->Send(SCI_GETRECTANGULARSELECTIONANCHOR) == rectAnchor && first->Send(SCI_GETRECTANGULARSELECTIONCARET) == rectCaret);
        // An absent or directory path must never produce an empty savable document.
        AppSettings bad; bad.Initialize(true); bad.path = directory / L"missing.ini"; bad.persistent = true;
        bad.WriteString(L"Session", L"Owner", L"MultiPad4.MDI"); bad.WriteString(L"Session", L"Count", L"2");
        bad.WriteString(L"Document0", L"Path", (directory / L"missing.txt").wstring());
        bad.WriteString(L"Document1", L"Path", directory.wstring()); require(bad.Save());
        const auto timer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR, DWORD) {
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
                wchar_t type[32]{}, title[128]{}; GetClassNameW(window, type, 32); GetWindowTextW(window, title, 128);
                if (!wcscmp(type, L"#32770") && !wcscmp(title, L"MultiPad4")) PostMessageW(window, WM_COMMAND, IDOK, 0);
                return TRUE;
            }, 0);
        });
        try { RestoreRelaunchSession(frame, bad.path); } catch (...) { KillTimer(nullptr, timer); throw; }
        KillTimer(nullptr, timer); require(frame.tabs.GetPageCount() == baseline + 3);
        cleanup();
    } catch (...) { cleanup(); throw; }
}

inline void RunExitSessionTests(MainFrame& frame, const std::filesystem::path& directory) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Exit session assertion at line " + std::to_string(where.line()));
    };
    const auto settings = frame.settings;
    const auto untitledCount = frame.untitledCount;
    frame.settings = AppSettings{};
    frame.settings.path = directory / L"exit-settings.ini";
    frame.exitSessionEligible = true;
    auto cleanup = [&] {
        while (frame.tabs.GetPageCount()) frame.Close(frame.tabs.GetPageCount() - 1, false);
        frame.exitSessionLock.reset();
        frame.exitSessionEligible = false;
        frame.settings = settings;
        frame.untitledCount = untitledCount;
    };
    try {
        require(!SaveExitSession(frame));
        frame.SendMessage(WM_COMMAND, ID_RESTORE_EXIT_SESSION);
        require(frame.State(ID_RESTORE_EXIT_SESSION).checked);
        require(GetPrivateProfileIntW(L"Options", L"Command45164", 0, frame.settings.path.c_str()) == 1);
        const auto saved = directory / L"exit-saved.txt";
        const auto savedEncoding = files::FromCodePage(1252);
        const std::string savedText = "caf\xC3\x83\xC2\xA9\n";
        files::WriteAtomic(saved, {savedText, savedEncoding});
        require(ReadFileForFrame(frame, saved).encoding == files::Encoding::Utf8);
        require(frame.Open(saved, savedEncoding));
        frame.SendMessage(WM_COMMAND, IDM_FILE_READONLY_MODE);
        auto* named = frame.Active();
        require(named->Send(SCI_GETREADONLY) && !named->Modified());
        require(!(GetFileAttributesW(saved.c_str()) & FILE_ATTRIBUTE_READONLY));
        auto* untitled = frame.NewDocument();
        require(untitled != nullptr);
        const std::string body("汉字\r\nfirst\0second\n", 21);
        untitled->Send(SCI_ADDTEXT, body.size(), reinterpret_cast<LPARAM>(body.data()));
        untitled->SetEncoding(files::Encoding::Utf16LE);
        untitled->Send(SCI_SETEOLMODE, SC_EOL_LF);
        const auto title = untitled->untitled;
        auto mapped = std::make_unique<EditorDocument>();
        mapped->path = directory / L"exit-mapped.txt";
        files::WriteAtomic(mapped->path, {"original", files::Encoding::Utf8Bom});
        require(mapped->Create(frame.tabs));
        mapped->OpenFastFile(mapped->path, {}, files::Encoding::Utf8Bom);
        mapped->encoding = files::Encoding::Utf8Bom;
        auto& model = mapped->fastView->Model();
        model.Begin({});
        model.Replace(0, model.Size(), "edited mapped");
        model.End({});
        frame.AddDocument(std::move(mapped));
        require(frame.GetMDITabCtrl().MoveItem(0, 2));
        frame.tabs.SetActivePage(0);
        require(frame.Active() == untitled && frame.tabs.GetPageData(2) == named);

        const auto session = directory / L"exit-settings.ini.session";
        std::filesystem::create_directory(session);
        bool errorShown = false;
        const auto timer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR, DWORD) {
            EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM) -> BOOL {
                wchar_t type[32]{}, title[128]{};
                GetClassNameW(window, type, _countof(type));
                GetWindowTextW(window, title, _countof(title));
                auto* shown = static_cast<bool*>(GetPropW(GetWindow(window, GW_OWNER), L"ExitSessionTestError"));
                if (shown && !wcscmp(type, L"#32770") && !wcscmp(title, L"MultiPad4")) {
                    *shown = true;
                    PostMessageW(window, WM_COMMAND, IDOK, 0);
                }
                return TRUE;
            }, 0);
        });
        require(timer != 0);
        SetPropW(frame, L"ExitSessionTestError", &errorShown);
        frame.SendMessage(WM_CLOSE);
        KillTimer(nullptr, timer);
        RemovePropW(frame, L"ExitSessionTestError");
        require(errorShown && frame.IsWindow() && frame.tabs.GetPageCount() == 3);
        require(frame.Active() == untitled && untitled->Text() == body && untitled->Modified());
        std::filesystem::remove(session);
        require(SaveExitSession(frame));
        while (frame.tabs.GetPageCount()) require(frame.Close(frame.tabs.GetPageCount() - 1, false));
        RestoreExitSession(frame);
        require(frame.tabs.GetPageCount() == 3);
        untitled = frame.Active();
        require(untitled && frame.tabs.GetPageData(0) == untitled && untitled->untitled == title && untitled->path.empty());
        require(untitled->Text() == body && untitled->Modified() && untitled->encoding == files::Encoding::Utf16LE);
        require(untitled->Send(SCI_GETEOLMODE) == SC_EOL_LF);
        auto* mappedTab = static_cast<EditorDocument*>(frame.tabs.GetPageData(1));
        require(mappedTab->FastView() && mappedTab->Modified() && mappedTab->fastView->Model().Read(0, 100) == "edited mapped");
        mappedTab->fastView->LoadForEditing();
        require(mappedTab->Modified() && mappedTab->Text() == "edited mapped");
        named = static_cast<EditorDocument*>(frame.tabs.GetPageData(2));
        require(named->path == saved && named->Text() == savedText && !named->Modified());
        require(named->encoding == savedEncoding);
        require(named->Send(SCI_GETREADONLY) != 0);
        named->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("!"));
        require(named->Text() == savedText && !named->Modified());
        CaptureQAWindow(frame, L"restored-exit-session");
        untitled->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("!"));
        require(frame.SaveTo(*untitled, directory / L"exit-save-as.txt", false) && !untitled->Modified());
        require(files::Read(directory / L"exit-save-as.txt").utf8 == body + "!");

        untitled->Send(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("?"));
        named->SetEncoding(files::Encoding::Utf16LE);
        require(untitled->Modified() && mappedTab->Modified() && named->Modified());
        require(SaveExitSession(frame, ExitSessionContents::SavedOnly));
        while (frame.tabs.GetPageCount()) require(frame.Close(frame.tabs.GetPageCount() - 1, false));
        RestoreExitSession(frame);
        require(frame.tabs.GetPageCount() == 3);
        auto restored = [&](int i) { return static_cast<EditorDocument*>(frame.tabs.GetPageData(i)); };
        require(restored(0)->Text() == body + "!" && !restored(0)->Modified());
        require(restored(1)->Text() == "original" && !restored(1)->Modified());
        require(restored(2)->encoding == files::Encoding::Utf8 && !restored(2)->Modified());
        cleanup();
    } catch (...) { cleanup(); throw; }
}
