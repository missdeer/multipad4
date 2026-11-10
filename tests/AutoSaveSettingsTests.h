#pragma once
#include "../src/AutoSave.h"
#include "RuntimeDialogTests.h"
#include <shlobj.h>

inline void RunAutoSaveSettingsTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("AutoSave settings assertion at line " + std::to_string(where.line()));
    };
    struct RestoreSettings {
        MainFrame& frame;
        AppSettings settings;
        ~RestoreSettings() { frame.settings = settings; }
    } restore{frame, frame.settings};
    frame.settings = AppSettings{};
    frame.settings.Initialize(true);
    frame.settings.path = std::filesystem::temp_directory_path()
        / (L"MultiPad4-autosave-settings-" + std::to_wstring(GetCurrentProcessId())) / L"Settings.ini";
    constexpr UINT controls[] = {IDC_AUTOSAVE_ENABLE, IDC_AUTOSAVE_SUSPEND, IDC_AUTOSAVE_SHUTDOWN,
        IDC_AUTOSAVE_MANUALLYDELETE, IDC_AUTOSAVE_OVERWRITECURRENT};
    auto show = [&](const std::function<void(HWND)>& action) {
        runtimedialogtest::Run(frame, [&] { ExecuteFrameCommand(frame, IDM_SET_FILE_AUTOSAVE); }, action);
    };
    auto checkOptions = [&](HWND dialog, int options) {
        for (size_t i = 0; i < std::size(controls); ++i)
            require(IsDlgButtonChecked(dialog, controls[i]) == static_cast<UINT>((options & (1 << i)) ? BST_CHECKED : BST_UNCHECKED));
    };
    // Exercise the actual modal dialog with no cached period or options.
    show([&](HWND dialog) {
        wchar_t text[32]{}; GetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, text, _countof(text));
        require(wcstod(text, nullptr) == 5.0 && AutoSavePeriodMilliseconds(frame) == 5000);
        checkOptions(dialog, 6); // Suspend and shutdown are enabled by default.
        SetDlgItemInt(dialog, IDC_AUTOSAVE_PERIOD, 31, FALSE);
        PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    require(AutoSavePeriodMilliseconds(frame) == 31000 && frame.settings.Get(IDM_SET_FILE_AUTOSAVE) == 6);
    show([&](HWND dialog) {
        wchar_t text[32]{}; GetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, text, _countof(text));
        require(wcstod(text, nullptr) == 31.0);
        SetDlgItemInt(dialog, IDC_AUTOSAVE_PERIOD, 2, FALSE);
        for (auto control : controls) CheckDlgButton(dialog, control, BST_CHECKED);
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(AutoSavePeriodMilliseconds(frame) == 31000 && frame.settings.Get(IDM_SET_FILE_AUTOSAVE) == 6);
    // Test each bit independently so adjacent resource IDs cannot hide a mapping error.
    for (int options : {1, 2, 4, 8, 16, 31, 0}) {
        show([&](HWND dialog) {
            for (size_t i = 0; i < std::size(controls); ++i)
                CheckDlgButton(dialog, controls[i], (options & (1 << i)) ? BST_CHECKED : BST_UNCHECKED);
            PostMessageW(dialog, WM_COMMAND, IDOK, 0);
        });
        require(frame.settings.Get(IDM_SET_FILE_AUTOSAVE) == options);
        require(frame.State(IDM_SET_FILE_AUTOSAVE).checked == ((options & 1) != 0));
        show([&](HWND dialog) { checkOptions(dialog, options); PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0); });
    }
    // Seconds accept fractional values and zero, while invalid/range-overflow
    // values cannot mutate the profile or close the real modal dialog.
    show([&](HWND dialog) {
        for (const wchar_t* invalid : {L"-1", L"4294968", L"invalid", L"nan", L"inf", L" ", L""}) {
            SetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, invalid);
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            require(IsWindow(dialog) && AutoSavePeriodMilliseconds(frame) == 31000);
        }
        SetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, L"0.5");
        CheckDlgButton(dialog, IDC_AUTOSAVE_ENABLE, BST_CHECKED);
        PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    require(AutoSavePeriodMilliseconds(frame) == 500 && frame.State(IDM_SET_FILE_AUTOSAVE).checked);
    show([&](HWND dialog) {
        wchar_t text[32]{}; GetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, text, _countof(text));
        require(wcstod(text, nullptr) == 0.5);
        SetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, L"0");
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    });
    require(AutoSavePeriodMilliseconds(frame) == 500);
    show([&](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, L"4294967.295");
        PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    require(AutoSavePeriodMilliseconds(frame) == MAXDWORD);
    show([&](HWND dialog) {
        SetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, L"0");
        PostMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    require(AutoSavePeriodMilliseconds(frame) == 0 && !frame.State(IDM_SET_FILE_AUTOSAVE).checked);

    {
        const auto directory = frame.settings.path.parent_path();
        struct RestoreDocuments {
            MainFrame& frame;
            int count;
            std::filesystem::path directory;
            std::vector<std::pair<EditorDocument*, ULONGLONG>> ticks;
            ~RestoreDocuments() {
                for (auto [document, tick] : ticks) document->runtime.lastAutoSave = tick;
                while (frame.tabs.GetPageCount() > count) {
                    auto* document = static_cast<EditorDocument*>(frame.tabs.GetPageData(frame.tabs.GetPageCount() - 1));
                    document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
                    if (!frame.Close(frame.tabs.GetPageCount() - 1)) break;
                }
                std::error_code error; std::filesystem::remove_all(directory, error);
            }
        } documents{frame, frame.tabs.GetPageCount(), directory, {}};
        for (const auto& document : frame.documents) {
            documents.ticks.emplace_back(document.get(), document->runtime.lastAutoSave);
            document->runtime.lastAutoSave = GetTickCount64();
        }
        auto* document = frame.NewDocument(); require(document != nullptr);
        document->Send(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("fractional period"));
        document->runtime.lastAutoSave = GetTickCount64() - 600;
        BackupDocuments(frame);
        require(document->runtime.recoverySnapshots.empty()); // Zero disables the periodic backend.
        show([&](HWND dialog) {
            SetDlgItemTextW(dialog, IDC_AUTOSAVE_PERIOD, L"0.5");
            PostMessageW(dialog, WM_COMMAND, IDOK, 0);
        });
        require(AutoSavePeriodMilliseconds(frame) == 500);
        BackupDocuments(frame);
        require(document->runtime.recoverySnapshots.size() == 1);
        require(document->runtime.recoveryPath.parent_path() == AutoSaveDirectory(frame));
        require(files::Read(document->runtime.recoveryPath).utf8 == document->Text());
    }

    // Open Folder and recovery writes share this resolver. Inspect both modes
    // without invoking Explorer, creating directories, or writing user settings.
    require(AutoSaveDirectory(frame) == frame.settings.path.parent_path() / L"AutoSave");
    frame.settings.persistent = true;
    PWSTR localAppData = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData);
    std::filesystem::path expected;
    if (SUCCEEDED(result)) {
        expected = std::filesystem::path(localAppData) / L"MultiPad4" / L"AutoSave";
        CoTaskMemFree(localAppData);
    } else {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, _countof(executable));
        expected = std::filesystem::path(executable).parent_path() / L"AutoSave";
    }
    require(AutoSaveDirectory(frame) == expected);
    frame.settings = AppSettings{}; frame.settings.Initialize(true);
    frame.settings.Set(IDC_AUTOSAVE_PERIOD + 60000, 31);
    require(AutoSavePeriodMilliseconds(frame) == 31000); // Legacy seconds migrate only when needed.
    frame.settings.WriteString(L"AutoSave", L"PeriodMilliseconds", L"500");
    require(AutoSavePeriodMilliseconds(frame) == 500); // Milliseconds take precedence.
    frame.settings.WriteString(L"AutoSave", L"PeriodMilliseconds", L"999999999999999999999999");
    require(AutoSavePeriodMilliseconds(frame) == 31000); // Corrupt values cannot overflow.
}
