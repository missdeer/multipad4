#pragma once
#include "../src/MainFrame.h"
#include <chrono>
#include <thread>
#include <source_location>
#include <psapi.h>
#include "SmbShutdownFixture.h"
#ifdef MULTIPAD4_SEARCH_TEST_HOOKS
inline void RunSearchRetirementTests(MainFrame& frame) {
    auto check = [](bool condition, std::source_location where = std::source_location::current()) {
        if (!condition) throw std::runtime_error("Search cancellation regression at " + std::to_string(where.line()));
    };
    auto pump = [&] {
        MSG message{}; if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        else MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
    };
    auto until = [&](auto predicate) {
        const auto deadline = GetTickCount64() + 10000;
        while (!predicate()) { check(GetTickCount64() < deadline); pump(); }
    };
    auto drained = [&] { const auto registry = work::searchDetail::RegistryOwner(); std::lock_guard guard(registry->mutex); return registry->groups == 0; };
    struct Gate {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Gate() { work::searchDetail::gate = event; }
        void Release() { work::searchDetail::gate = nullptr; SetEvent(event); }
        ~Gate() {
            Release(); std::vector<std::shared_ptr<work::searchDetail::StateBase>> states;
            const auto registry = work::searchDetail::RegistryOwner();
            { std::lock_guard guard(registry->mutex); for (const auto& item : registry->states) if (auto state = item.lock()) states.push_back(std::move(state)); }
            const auto deadline = GetTickCount64() + 10000;
            for (const auto& state : states) while (!state->ready.load() && GetTickCount64() < deadline) Sleep(1);
            CloseHandle(event);
        }
    } gate;
    const auto directory = std::filesystem::temp_directory_path() / (L"MultiPad4-retirement-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(directory); const auto path = directory / L"source.txt"; files::WriteAtomic(path, {"one two one"});
    auto* document = frame.NewDocument(); document->path = path; document->OpenFastFile(path, {}, files::Encoding::Utf8);
    auto* child = frame.tabs.Child(frame.tabs.GetActivePage()); child->m_hWndClient = document->ViewWindow(); SetParent(document->ViewWindow(), child->m_hWnd); child->UpdateLayout();
    auto& view = *document->fastView;
    frame.ShowWindow(SW_SHOWNORMAL);
    const auto entered = work::searchDetail::entered.load();
    view.StartFind(L"one", false, true, false, false); until([&] { return work::searchDetail::entered > entered; });
    auto begin = std::chrono::steady_clock::now(); view.StopWork();
    RedrawWindow(frame.m_hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    fprintf(stdout, "Blocked mapped cancellation to application paint: %.3f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
    document->Send(SCI_SETSEL, 0, 0); document->Send(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("X"));
    check(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(100));
    for (int i = 0; i < 7; ++i) { view.StartFind(L"one", false, true, false, false); view.StopWork(); }
    until([&] { return work::searchDetail::entered == entered + 8; });
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles);
    fprintf(stdout, "Retirement cap: 8 blocked groups, private bytes %llu, handles %lu\n", static_cast<unsigned long long>(memory.PrivateUsage), handles);
    view.StartFind(L"missing", false, true, false, false); view.StartFind(L"two", false, true, false, false);
    check(view.Busy() && work::searchDetail::entered == entered + 8 && !work::TrySearchPermit());
    SendMessageW(document->hwnd, WM_KEYDOWN, VK_ESCAPE, 0); check(!view.Busy());
    frame.ShowSearch(false); auto& queuedDialog = frame.searchDialog; queuedDialog.SetPage(2);
    SetDlgItemTextW(queuedDialog.hwnd, IDC_SEARCH_DIRECTORY, directory.c_str()); SetDlgItemTextW(queuedDialog.hwnd, IDC_SEARCH_FILTER, L"*.txt");
    SetDlgItemTextW(queuedDialog.hwnd, IDC_FINDTEXT, L"missing"); queuedDialog.Execute(IDC_SEARCH_FILES);
    SetDlgItemTextW(queuedDialog.hwnd, IDC_FINDTEXT, L"two"); queuedDialog.Execute(IDC_SEARCH_FILES);
    check(queuedDialog.fileSearch.findText == L"two" && !queuedDialog.fileTask->WorkerPermit()); queuedDialog.Close();
    view.StartFind(L"missing", false, true, false, false); view.StartFind(L"two", false, true, false, false);
    gate.Release(); until([&] { return !view.Busy(); });
    check(view.GlobalSelection().anchor == 5 && view.GlobalSelection().caret == 8 && view.Status() == "Match found.");
    until(drained);
    {
        std::vector<std::shared_ptr<work::SearchPermit>> reserved;
        for (int i = 0; i < 7; ++i) { reserved.push_back(work::TrySearchPermit()); check(reserved.back() != nullptr); }
        frame.ShowSearch(false); auto& dependent = frame.searchDialog; dependent.SetPage(2);
        SetDlgItemTextW(dependent.hwnd, IDC_FINDTEXT, L"one"); SetDlgItemTextW(dependent.hwnd, IDC_SEARCH_DIRECTORY, directory.c_str()); SetDlgItemTextW(dependent.hwnd, IDC_SEARCH_FILTER, L"*.txt");
        dependent.Execute(IDC_SEARCH_FILES); until([&] { SendMessageW(dependent.hwnd, WM_TIMER, 1, 0); return !dependent.fileRunning; });
        check(dependent.fileHits == 2); dependent.Close();
    }
    until(drained);
    ResetEvent(gate.event); work::searchDetail::gate = gate.event;
    const auto beforeClose = work::searchDetail::entered.load(); view.StartFind(L"one", false, true, false, false);
    until([&] { return work::searchDetail::entered > beforeClose; });
    view.Model().MarkSaved(); begin = std::chrono::steady_clock::now(); check(frame.Close(frame.tabs.GetActivePage()));
    check(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(100));
    auto* replacement = frame.NewDocument(); replacement->Load({"safe"}); gate.Release(); until(drained); check(replacement->Text() == "safe");
    frame.ShowSearch(false); auto& dialog = frame.searchDialog; replacement->path = path; replacement->Load({"one two one"});
    dialog.SetPage(2); SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"one"); SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_DIRECTORY, directory.c_str());
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"*.txt");
    ResetEvent(gate.event); work::searchDetail::gate = gate.event; const auto beforeDirectory = work::searchDetail::entered.load();
    dialog.Execute(IDC_SEARCH_FILES); until([&] { return work::searchDetail::entered > beforeDirectory; });
    begin = std::chrono::steady_clock::now(); DestroyWindow(dialog.hwnd); check(!dialog.fileRunning && std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(100));
    gate.Release(); until(drained);
    // Block the ordinary-open-document snapshot worker after directory enumeration.
    frame.ShowSearch(false); dialog.SetPage(2); SetDlgItemTextW(dialog.hwnd, IDC_FINDTEXT, L"one");
    SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_DIRECTORY, directory.c_str()); SetDlgItemTextW(dialog.hwnd, IDC_SEARCH_FILTER, L"*.txt");
    {
        std::vector<std::shared_ptr<work::SearchPermit>> reserved;
        for (int i = 0; i < 7; ++i) { reserved.push_back(work::TrySearchPermit()); check(reserved.back() != nullptr); }
        dialog.Execute(IDC_SEARCH_FILES);
        until([&] { SendMessageW(dialog.hwnd, WM_TIMER, 1, 0); return !dialog.fileRunning; });
        check(dialog.fileHits == 2);
    }
    until(drained);
    dialog.Execute(IDC_SEARCH_FILES);
    until([&] { SendMessageW(dialog.hwnd, WM_TIMER, 1, 0); return dialog.openSearch != nullptr; });
    ResetEvent(gate.event); work::searchDetail::gate = gate.event;
    const auto beforeSnapshot = work::searchDetail::entered.load();
    until([&] { SendMessageW(dialog.hwnd, WM_TIMER, 1, 0); return dialog.openSearch && dialog.openSearch->search.Valid(); });
    until([&] { return work::searchDetail::entered > beforeSnapshot; });
    begin = std::chrono::steady_clock::now(); dialog.Close(); check(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(100));
    frame.ShowSearch(false); check(replacement->Text() == "one two one"); gate.Release(); until(drained); dialog.Close();
    check(frame.Close(frame.tabs.GetActivePage()));
    // Index publication must not retain node/index locks while a block build stalls.
    auto file = std::make_shared<files::MappedFile>(path, nullptr, true); large::Document indexed(file, files::Encoding::Utf8);
    ResetEvent(gate.event); large::testing::indexEntered = false; large::testing::nextIndexGate = gate.event;
    auto indexing = work::Submit([&] { return large::Document::LineCount(indexed.Capture()); });
    until([&] { return large::testing::indexEntered.load(); });
    std::jthread timeout([&] { Sleep(500); SetEvent(gate.event); });
    begin = std::chrono::steady_clock::now(); check(large::Document::LineCount(indexed.Capture()) == 1);
    const auto concurrent = std::chrono::steady_clock::now() - begin; timeout.join(); indexing.Get();
    check(concurrent < std::chrono::milliseconds(100));
    files::testing::bufferedSessions = true;
    files::MappedFile fallback(path, nullptr, true); files::testing::bufferedSessions = false;
    ResetEvent(gate.event); files::testing::fallbackEntered = false; files::testing::nextFallbackGate = gate.event;
    auto stalledRead = work::Submit([&] { auto session = fallback.NewReadSession(); return session->Read(0, 11); });
    until([&] { return files::testing::fallbackEntered.load(); });
    std::jthread readTimeout([&] { Sleep(500); SetEvent(gate.event); });
    begin = std::chrono::steady_clock::now(); const auto bytes = fallback.Read(0, 11);
    const auto independentRead = std::chrono::steady_clock::now() - begin;
    readTimeout.join(); check(stalledRead.Get() == bytes && bytes == "one two one" && independentRead < std::chrono::milliseconds(100));
    if (GetEnvironmentVariableW(L"MULTIPAD4_SMB_TEST_ROOT", nullptr, 0)) {
        auto* closingDocument = frame.NewDocument(); closingDocument->Load({"safe"});
        closingDocument->Send(SCI_SETSAVEPOINT); closingDocument->encodingDirty = false;
        frame.ShowSearch(false);
        auto& closingDialog = frame.searchDialog;
        closingDialog.openSearch = std::make_shared<SearchDialog::OpenSearch>();
        closingDialog.openSearch->identity = closingDocument->searchIdentity;
        check(RunSmbShutdownFixture([&](auto& blocked) {
            closingDialog.openSearch->search = std::move(blocked);
            closingDialog.fileRunning = true;
            closingDialog.fileGeneration = frame.searchResults.generation;
            const HWND closingFrame = frame.m_hWnd;
            const auto closeBegin = std::chrono::steady_clock::now();
            SendMessageW(closingFrame, WM_CLOSE, 0, 0);
            fprintf(stdout, "SMB MainFrame WM_CLOSE callback: %.4f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - closeBegin).count());
            check(!IsWindow(closingFrame) && !closingDialog.openSearch && !closingDialog.fileRunning && work::SearchClosing());
        }));
        check(!frame.IsWindow() && !closingDialog.openSearch && !closingDialog.fileRunning);
        fprintf(stdout, "SMB late result: frame remained destroyed and dialog search remained cleared after acknowledgement\n");
        std::filesystem::remove_all(directory); return;
    }
    // Closing admission cancels jobs, but their resources survive a blocked callback.
    document = frame.NewDocument(); document->path = path; document->OpenFastFile(path, {}, files::Encoding::Utf8);
    child = frame.tabs.Child(frame.tabs.GetActivePage()); child->m_hWndClient = document->ViewWindow(); SetParent(document->ViewWindow(), child->m_hWnd); child->UpdateLayout();
    ResetEvent(gate.event); work::searchDetail::gate = gate.event; const auto beforeShutdown = work::searchDetail::entered.load();
    document->fastView->StartFind(L"one", false, true, false, false); until([&] { return work::searchDetail::entered > beforeShutdown; });
    begin = std::chrono::steady_clock::now(); work::StopSearchAdmission(); check(frame.Close(frame.tabs.GetActivePage()));
    check(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(100) && !work::TrySearchPermit());
    gate.Release(); until(drained); std::filesystem::remove_all(directory);
}
#else
inline void RunSearchRetirementTests(MainFrame&) { throw std::runtime_error("Search retirement tests require BUILD_TESTING."); }
#endif
