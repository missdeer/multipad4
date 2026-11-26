#ifdef MULTIPAD4_AUTOSAVE_TEST_HOOKS
#include "../tests/AutoSaveCaptureHooks.h"
#endif
// Per-document adaptation of the pinned Notepad4 AutoSave_DoWork/Stop routines.
#include "AutoSave.h"
#include "MainFrame.h"
#include "FrameCommands.h"
#include <atomic>
#include <limits>
#include <chrono>
#include "ThreadPool.h"
#include <shlobj.h>
namespace {
constexpr size_t NormalCount=6, ForcedCount=NormalCount+2;
std::atomic<unsigned long long> sequence=0;
std::filesystem::path Metadata(const std::filesystem::path& path){auto result=path;result+=L".ini";return result;}
void DeleteSnapshot(const std::filesystem::path& path){
    std::error_code bodyError,metadataError;
    std::filesystem::remove(path,bodyError);std::filesystem::remove(Metadata(path),metadataError);
    // Like AutoSave_Stop, forget obsolete entries even if Windows refuses
    // deletion (for example, a backup is open in another application).
}
bool WriteSnapshot(const std::filesystem::path& path,const std::string& text,const std::wstring& metadata, std::string_view prefix = {}){
    bool bodyCreated=false;
    try{files::WriteNewUtf8(path,text,prefix);bodyCreated=true;files::WriteNew(Metadata(path),{files::ToUtf8(metadata),files::Encoding::Utf16LE});return true;}
    catch(const std::exception&){if(bodyCreated)DeleteFileW(path.c_str());return false;}
}
void TrimSnapshots(EditorDocument& document,size_t count,bool keepFiles){
    auto& snapshots=document.runtime.recoverySnapshots;
    while(snapshots.size()>count){if(!keepFiles)DeleteSnapshot(snapshots.front());snapshots.erase(snapshots.begin());}
}
}
DWORD AutoSavePeriodMilliseconds(const MainFrame& frame) {
    const auto text = frame.settings.ReadString(L"AutoSave", L"PeriodMilliseconds");
    if (!text.empty() && text.find_first_not_of(L"0123456789") == std::wstring::npos) {
        // Parse with an explicit limit: even an arbitrarily long corrupt value
        // must not wrap into a short timer interval.
        unsigned long long value = 0;
        for (wchar_t digit : text) {
            value = value * 10 + (digit - L'0');
            if (value > MAXDWORD) break;
        }
        if (value <= MAXDWORD) return static_cast<DWORD>(value);
    }
    // Older MultiPad4 profiles stored whole seconds as Command60101.
    const int seconds = frame.settings.Get(IDC_AUTOSAVE_PERIOD + 60000, 5);
    return seconds >= 0 && static_cast<unsigned>(seconds) <= MAXDWORD / 1000
        ? static_cast<DWORD>(seconds) * 1000 : 5000;
}
std::filesystem::path AutoSaveDirectory(const MainFrame& frame) {
    // Smoke tests and QA sessions must never write into the user's backup tree.
    if (!frame.settings.persistent || frame.settings.portable) return frame.settings.path.parent_path() / L"AutoSave";
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder))) {
        std::filesystem::path path(folder);
        CoTaskMemFree(folder);
        return path / L"MultiPad4" / L"AutoSave";
    }
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, _countof(executable));
    return std::filesystem::path(executable).parent_path() / L"AutoSave";
}
namespace {
struct RecoveryRequest {
    std::filesystem::path path, directory;
    std::wstring title;
    uint64_t revision = 0;
    files::Encoding encoding = files::Encoding::Utf8;
    int eol = SC_EOL_CRLF;
    bool overwrite = false;
};
struct RecoveryOutcome { std::filesystem::path path; bool overwritten = false; };
RecoveryRequest Request(MainFrame& frame, const EditorDocument& d) {
    return {d.path, AutoSaveDirectory(frame), d.untitled, d.style.revision, d.encoding, static_cast<int>(d.Send(SCI_GETEOLMODE)), false};
}
bool NeedsRecovery(const EditorDocument& d) {
    return !d.LargeFile() && d.Modified() && d.Send(SCI_GETLENGTH) &&
        (d.runtime.lastRecoveryRevision != d.style.revision || d.runtime.lastRecoveryEncoding != static_cast<unsigned>(d.encoding) || d.runtime.lastRecoveryEol != d.Send(SCI_GETEOLMODE));
}
RecoveryOutcome WriteRecovery(const RecoveryRequest& request, const std::string& text) {
#ifdef MULTIPAD4_AUTOSAVE_TEST_HOOKS
    if (auto gate = autosavetest::beforeWrite.load()) gate(request.path, request.overwrite && !request.path.empty());
#endif
    if (request.overwrite && !request.path.empty()) {
        try { files::WriteAtomic(request.path, {text, request.encoding}); return {{}, true}; } catch (const std::exception&) {}
    }
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t stamp[96]{};
    swprintf_s(stamp,L" %04u%02u%02u %02u%02u%02u %03u%u-%llu",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId(),++sequence);
    auto filename=request.path.empty()?std::wstring(L"Untitled"):request.path.filename().wstring();
    const auto dot=filename.find(L'.');auto extension=dot==std::wstring::npos?std::wstring(L".bak"):filename.substr(dot);
    auto backupName=filename+stamp+extension;
    std::wstring metadata=L"[Recovery]\r\nPath="+request.path.wstring()+L"\r\nTitle="+request.title+L"\r\nEncoding="+std::to_wstring(static_cast<unsigned>(request.encoding))
        +L"\r\nEol="+std::to_wstring(request.eol)+L"\r\nRevision="+std::to_wstring(request.revision)+L"\r\nProcess="+std::to_wstring(GetCurrentProcessId())+L"\r\n";
    std::filesystem::path destination;
    // Named documents first save alongside the source, just as notepad4 does.
    if(!request.path.empty()){
        auto candidate=request.path.parent_path()/backupName;
        if(WriteSnapshot(candidate,text,metadata))destination=std::move(candidate);
    }
    if(destination.empty()){
        auto directory=request.directory;std::filesystem::create_directories(directory);
        // Long base names may exceed the component limit after adding a stamp.
        if(backupName.size()>240){filename.resize((std::min)(filename.size(),size_t(64)));if(!filename.empty()&&filename.back()>=0xD800&&filename.back()<=0xDBFF)filename.pop_back();backupName=filename+stamp+L".bak";}
        auto candidate=directory/backupName;
        const auto header=request.path.empty()?std::string{}:"AutoSave for "+files::ToUtf8(request.path.wstring())+(request.eol==SC_EOL_LF?"\n":request.eol==SC_EOL_CR?"\r":"\r\n");
        if(!WriteSnapshot(candidate,text,metadata,header))throw std::runtime_error("Cannot create AutoSave backup: "+files::ToUtf8(candidate.wstring()));
        destination=std::move(candidate);
    }
    return {destination, false};
}
void ApplyRecovery(MainFrame& frame, EditorDocument& document, const RecoveryRequest& request, const RecoveryOutcome& outcome, bool force) {
    auto& state = document.runtime;
    if (outcome.overwritten) SnapshotDocument(document);
    else {
        state.recoverySnapshots.push_back(outcome.path); state.recoveryPath = outcome.path;
        TrimSnapshots(document, force ? ForcedCount : NormalCount, (frame.settings.Get(IDM_SET_FILE_AUTOSAVE,6)&8) != 0);
    }
    state.lastRecoveryRevision = request.revision; state.lastRecoveryEncoding = static_cast<unsigned>(request.encoding); state.lastRecoveryEol = request.eol;
}
}
struct AutoSaveTask {
    unsigned long long identity = 0;
    RecoveryRequest request;
    size_t length = 0, offset = 0;
    std::string text;
    bool retired = false, keepBackup = true;
    size_t captureBytes = 256 * 1024;
    work::Task<RecoveryOutcome> write;
    ~AutoSaveTask() {
        if (write.Valid()) try {
            auto outcome = write.Get();
            if (retired && !keepBackup && !outcome.overwritten) DeleteSnapshot(outcome.path);
        } catch (const std::exception&) {}
    }
};
bool SaveDocumentRecovery(MainFrame& frame,EditorDocument& document,bool force) {
    if (!frame.IsWindowEnabled() || frame.runtime.operationDepth || !NeedsRecovery(document)) return false;
    CancelDocumentBackup(frame, document);
    auto request = Request(frame, document);
    auto outcome = WriteRecovery(request, document.Text());
    ApplyRecovery(frame, document, request, outcome, force); return true;
}
void CancelDocumentBackup(MainFrame& frame, const EditorDocument& document, bool keepBackup) {
    if (auto task = frame.runtime.autoSaveTask; task && task->identity == document.searchIdentity)
        frame.runtime.autoSaveTask.reset();
    for (auto it = frame.runtime.autoSaveWrites.begin(); it != frame.runtime.autoSaveWrites.end();) {
        auto& task = **it;
        const bool sameDocument = task.identity == document.searchIdentity;
        const bool samePath = task.request.overwrite && !document.path.empty() && task.request.path == document.path;
        if (!sameDocument && !samePath) { ++it; continue; }
        if (task.request.overwrite && !task.request.path.empty()) {
            // Foreground save/reload/close must serialize against the same target.
            try { auto outcome = task.write.Get(); if (!outcome.overwritten && !keepBackup) DeleteSnapshot(outcome.path); } catch (const std::exception&) {}
            it = frame.runtime.autoSaveWrites.erase(it);
        } else {
            // Worker captures only immutable data; keep ownership until cleanup is ready.
            task.retired = true; task.keepBackup = task.keepBackup && keepBackup; ++it;
        }
    }
}
void QueueDocumentBackups(MainFrame& frame) {
    if (frame.runtime.autoSaveTask || !frame.IsWindowEnabled() || frame.runtime.operationDepth) return;
    auto period = AutoSavePeriodMilliseconds(frame); if (!period) return;
    auto now = GetTickCount64();
    for (size_t visited = 0; visited < frame.documents.size(); ++visited) {
        auto& d = *frame.documents[frame.runtime.autoSaveCursor++ % frame.documents.size()];
        const bool writing = std::any_of(frame.runtime.autoSaveWrites.begin(), frame.runtime.autoSaveWrites.end(), [&](const auto& task) { return task->identity == d.searchIdentity; });
        if (writing || now - d.runtime.lastAutoSave < period || !NeedsRecovery(d)) continue;
        auto task = std::make_shared<AutoSaveTask>(); task->identity = d.searchIdentity;
        task->request = Request(frame, d); task->request.overwrite = (frame.settings.Get(IDM_SET_FILE_AUTOSAVE,6)&16) != 0;
        task->length = static_cast<size_t>(d.Send(SCI_GETLENGTH)); task->text.reserve(task->length);
        d.runtime.lastAutoSave = now; frame.runtime.autoSaveTask = std::move(task);
        if (!frame.runtime.workTimerArmed) { frame.SetTimer(3, 30); frame.runtime.workTimerArmed = true; }
        break;
    }
}
void PumpDocumentBackup(MainFrame& frame) {
    for (auto it = frame.runtime.autoSaveWrites.begin(); it != frame.runtime.autoSaveWrites.end();) {
        auto& task = **it;
        if (!task.write.Ready()) { ++it; continue; }
        try {
            auto outcome = task.write.Get();
            const auto found = frame.tabs.documentsById.find(task.identity);
            if (!task.retired && found != frame.tabs.documentsById.end() && found->second->path == task.request.path)
                ApplyRecovery(frame, *found->second, task.request, outcome, false);
            else if (!outcome.overwritten && (!task.retired || !task.keepBackup)) DeleteSnapshot(outcome.path);
        } catch (const std::exception&) {}
        it = frame.runtime.autoSaveWrites.erase(it);
    }
    if (!frame.runtime.autoSaveTask && (frame.settings.Get(IDM_SET_FILE_AUTOSAVE, 6) & 1)) QueueDocumentBackups(frame);
    auto task = frame.runtime.autoSaveTask; if (!task) return;
    auto found = frame.tabs.documentsById.find(task->identity);
    if (found == frame.tabs.documentsById.end()) { frame.runtime.autoSaveTask.reset(); return; }
    auto& d = *found->second;
    if (!NeedsRecovery(d) || d.path != task->request.path || d.style.revision != task->request.revision || d.encoding != task->request.encoding || d.Send(SCI_GETEOLMODE) != task->request.eol) {
        frame.runtime.autoSaveTask.reset(); d.runtime.lastAutoSave = 0; return;
    }
#ifdef MULTIPAD4_AUTOSAVE_TEST_HOOKS
    if (autosavetest::baselineCapture && task->offset < task->length) {
        auto bytes = d.Range(task->offset, (std::min)(size_t(256 * 1024), task->length - task->offset));
        task->offset += bytes.size(); task->text.append(bytes);
        autosavetest::rangeCopies += bytes.size(); autosavetest::captureCopies += bytes.size(); ++autosavetest::captureSlices;
        return;
    }
#endif
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
    size_t captured = 0;
    while (task->offset < task->length) {
        const auto count = (std::min)(task->captureBytes, task->length - task->offset);
        const auto started = std::chrono::steady_clock::now();
        const auto* bytes = reinterpret_cast<const char*>(d.Send(SCI_GETRANGEPOINTER, task->offset, count));
        if (!bytes) { frame.runtime.autoSaveTask.reset(); d.runtime.lastAutoSave = 0; return; }
        task->text.append(bytes, count); task->offset += count; captured += count;
#ifdef MULTIPAD4_AUTOSAVE_TEST_HOOKS
        autosavetest::captureCopies += count; ++autosavetest::captureSlices;
#endif
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        task->captureBytes = static_cast<size_t>((std::clamp)(count * (0.25 / (std::max)(elapsed, 0.001)), 65536.0, 1048576.0));
        if (std::chrono::steady_clock::now() >= deadline || captured >= 8 * 1024 * 1024) return;
    }
    task->write = work::Submit([request = task->request, text = std::move(task->text)] { return WriteRecovery(request, text); });
    frame.runtime.autoSaveWrites.push_back(task); frame.runtime.autoSaveTask.reset();
}
void RemoveDocumentRecoveries(MainFrame& frame,EditorDocument& document,bool keepBackup){
    auto& state=document.runtime;
    const bool keep=keepBackup||(frame.settings.Get(IDM_SET_FILE_AUTOSAVE,6)&8)!=0;
    CancelDocumentBackup(frame, document, keep);
    if(!keep){
        for(const auto& path:state.recoverySnapshots)DeleteSnapshot(path);
        if(!state.recoveryPath.empty()&&std::find(state.recoverySnapshots.begin(),state.recoverySnapshots.end(),state.recoveryPath)==state.recoverySnapshots.end())DeleteSnapshot(state.recoveryPath);
    }
    state.recoverySnapshots.clear();state.recoveryPath.clear();
    state.lastRecoveryRevision=std::numeric_limits<uint64_t>::max();state.lastRecoveryEncoding=std::numeric_limits<unsigned>::max();state.lastRecoveryEol=-1;
}
