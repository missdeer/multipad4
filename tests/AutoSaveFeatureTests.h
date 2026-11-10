#pragma once
#include "../src/AutoSave.h"
#include "../src/MainFrame.h"
#include "../src/FrameCommands.h"
#include <source_location>
inline void RunAutoSaveFeatureTests(MainFrame& frame) {
    auto require = [](bool ok, std::source_location location = std::source_location::current()) {
        if (!ok) throw std::runtime_error("AutoSave regression at line " + std::to_string(location.line()));
    };
    auto folder = std::filesystem::temp_directory_path() / (L"MultiPad4-autosave-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    const auto originalSettings = frame.settings;
    frame.settings = AppSettings{}; frame.settings.Initialize(true);
    frame.settings.path = folder / L"Settings.ini";
    frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1);
    try {
        auto* document = frame.NewDocument(); require(document != nullptr);
        require(AutoSaveDirectory(frame) == folder / L"AutoSave");
        document->path = folder / L"original.txt";
        document->Load({"initial"}); require(frame.Save(*document));
        auto edit = [&](const char* text = "x") { document->Send(SCI_APPENDTEXT, strlen(text), reinterpret_cast<LPARAM>(text)); };
        auto exists = [](const std::filesystem::path& path) {
            auto metadata = path; metadata += L".ini";
            return std::filesystem::exists(path) && std::filesystem::exists(metadata);
        };
        auto absent = [](const std::filesystem::path& path) {
            auto metadata = path; metadata += L".ini";
            return !std::filesystem::exists(path) && !std::filesystem::exists(metadata);
        };
        require(!SaveDocumentRecovery(frame, *document));
        edit(" 中文"); require(SaveDocumentRecovery(frame, *document));
        auto first = document->runtime.recoveryPath;
        require(first.parent_path() == folder && exists(first));
        require(files::Read(first).encoding == files::Encoding::Utf8 && files::Read(first).utf8 == document->Text());
        require(!SaveDocumentRecovery(frame, *document) && !SaveDocumentRecovery(frame, *document, true));
        require(document->runtime.recoverySnapshots.size() == 1);
        // Metadata-only changes are buffer state, without a text revision.
        document->SetEncoding(files::Encoding::Utf16BE); require(SaveDocumentRecovery(frame, *document));
        document->Send(SCI_SETEOLMODE, SC_EOL_LF); require(SaveDocumentRecovery(frame, *document));
        auto metadata = document->runtime.recoveryPath; metadata += L".ini";
        const auto metadataText = files::Read(metadata).utf8;
        require(metadataText.find("Encoding=" + std::to_string(static_cast<unsigned>(files::Encoding::Utf16BE))) != std::string::npos);
        require(metadataText.find("Eol=" + std::to_string(SC_EOL_LF)) != std::string::npos);
        require(files::Read(first).utf8 == "initial 中文" && files::Read(document->path).utf8 == "initial");
        for (int i = 0; i < 4; ++i) { edit(); require(SaveDocumentRecovery(frame, *document)); }
        require(document->runtime.recoverySnapshots.size() == 6 && absent(first));
        for (int i = 0; i < 2; ++i) { edit(); require(SaveDocumentRecovery(frame, *document, true)); }
        require(document->runtime.recoverySnapshots.size() == 8);
        require(!SaveDocumentRecovery(frame, *document) && document->runtime.recoverySnapshots.size() == 8);
        // Further suspend events cannot overflow the reserved slots.
        auto oldest = document->runtime.recoverySnapshots.front();
        edit(); require(SaveDocumentRecovery(frame, *document, true));
        require(document->runtime.recoverySnapshots.size() == 8 && absent(oldest));
        edit(); require(SaveDocumentRecovery(frame, *document));
        require(document->runtime.recoverySnapshots.size() == 6);
        auto retained = document->runtime.recoverySnapshots;
        RemoveDocumentRecovery(frame, *document, true);
        for (const auto& path : retained) require(exists(path));
        require(document->runtime.recoverySnapshots.empty() && document->runtime.recoveryPath.empty());
        // Stop resets deduplication even when it keeps old files.
        require(SaveDocumentRecovery(frame, *document));
        auto normal = document->runtime.recoveryPath;
        require(frame.Save(*document));
        require(absent(normal) && document->runtime.recoverySnapshots.empty());
        for (const auto& path : retained) require(exists(path));
        edit(); require(SaveDocumentRecovery(frame, *document));
        auto reloaded = document->runtime.recoveryPath;
        document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
        require(frame.Reload(*document));
        require(absent(reloaded) && document->runtime.recoverySnapshots.empty());
        // ManualDelete also leaves versions removed from the rolling list.
        frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1 | 8);
        std::vector<std::filesystem::path> manual;
        for (int i = 0; i < 8; ++i) {
            edit(); require(SaveDocumentRecovery(frame, *document)); manual.push_back(document->runtime.recoveryPath);
        }
        require(document->runtime.recoverySnapshots.size() == 6);
        RemoveDocumentRecovery(frame, *document);
        for (const auto& path : manual) require(exists(path));
        require(document->runtime.recoverySnapshots.empty());
        // Interval and modal guards apply before touching snapshot state.
        frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1);
        frame.settings.Set(IDC_AUTOSAVE_PERIOD + 60000, 1);
        document->runtime.lastAutoSave = GetTickCount64(); BackupDocuments(frame);
        require(document->runtime.recoverySnapshots.empty());
        {
            FrameOperation operation(frame.runtime, frame);
            auto tick = document->runtime.lastAutoSave; BackupDocuments(frame, true);
            require(!SaveDocumentRecovery(frame, *document, true));
            require(document->runtime.recoverySnapshots.empty() && document->runtime.lastAutoSave == tick);
        }
        frame.EnableWindow(FALSE); BackupDocuments(frame, true);
        const bool blocked = document->runtime.recoverySnapshots.empty(); frame.EnableWindow(TRUE);
        require(blocked);
        document->runtime.lastAutoSave = GetTickCount64() - 1100; BackupDocuments(frame);
        require(document->runtime.recoverySnapshots.size() == 1);
        BackupDocuments(frame, true); require(document->runtime.recoverySnapshots.size() == 1);
        RemoveDocumentRecovery(frame, *document);
        // OverwriteCurrent preserves dirty state. Forced work saves a backup.
        document->SetEncoding(files::Encoding::Utf8);
        frame.settings.Set(IDM_SET_FILE_AUTOSAVE, 1 | 16);
        edit(); document->runtime.lastAutoSave = 0; BackupDocuments(frame);
        require(files::Read(document->path).utf8 == document->Text() && document->Modified());
        require(document->runtime.recoverySnapshots.empty());
        edit(" suspended"); const auto onDisk = files::Read(document->path).utf8;
        BackupDocuments(frame, true);
        require(files::Read(document->path).utf8 == onDisk && document->runtime.recoverySnapshots.size() == 1);
        RemoveDocumentRecovery(frame, *document);
        // Failed overwrite falls back to the test's isolated AutoSave folder.
        document->path = folder / L"missing-parent" / L"original.txt";
        edit(); document->runtime.lastAutoSave = 0; BackupDocuments(frame);
        require(document->runtime.recoveryPath.parent_path() == AutoSaveDirectory(frame));
        require(files::Read(document->runtime.recoveryPath).utf8 == "AutoSave for " + files::ToUtf8(document->path.wstring()) + "\n" + document->Text());
        RemoveDocumentRecovery(frame, *document);
        // Both destinations failing must retain the revision for a retry and
        // must not open an error dialog from background work.
        auto directory = AutoSaveDirectory(frame); std::filesystem::remove(directory);
        files::WriteNew(directory, {"block directory creation"});
        const auto revision = document->runtime.lastRecoveryRevision;
        BackupDocuments(frame, true);
        require(document->runtime.recoverySnapshots.empty() && document->runtime.lastRecoveryRevision == revision);
        std::filesystem::remove(directory); BackupDocuments(frame, true);
        require(document->runtime.recoverySnapshots.size() == 1);
        RemoveDocumentRecovery(frame, *document);
        document->path.clear(); document->Load({""});
        document->SetEncoding(files::Encoding::Utf16LE);
        require(document->Modified() && !SaveDocumentRecovery(frame, *document, true));
        edit(); require(SaveDocumentRecovery(frame, *document));
        require(files::Read(document->runtime.recoveryPath).utf8 == document->Text());
        auto closing = document->runtime.recoveryPath;
        document->Send(SCI_SETSAVEPOINT); document->encodingDirty = false;
        require(frame.Close(frame.tabs.GetActivePage()));
        require(exists(closing)); // AutoSave_Stop(TRUE) preserves discarded text.
        frame.settings = originalSettings; std::filesystem::remove_all(folder);
    } catch (...) {
        frame.EnableWindow(TRUE); frame.settings = originalSettings;
        std::error_code error; std::filesystem::remove_all(folder, error); throw;
    }
}
