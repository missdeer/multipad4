// Per-document adaptation of the pinned Notepad4 AutoSave_DoWork/Stop routines.
#include "AutoSave.h"
#include "MainFrame.h"
#include "FrameCommands.h"
#include <atomic>
#include <limits>
#include <future>
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
bool WriteSnapshot(const std::filesystem::path& path,const std::string& text,const std::wstring& metadata){
    bool bodyCreated=false;
    try{files::WriteNew(path,{text,files::Encoding::Utf8});bodyCreated=true;files::WriteNew(Metadata(path),{files::ToUtf8(metadata),files::Encoding::Utf16LE});return true;}
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
    if (!frame.settings.persistent) return frame.settings.path.parent_path() / L"AutoSave";
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
        auto candidate=directory/backupName;std::string content=text;
        if(!request.path.empty())content="AutoSave for "+files::ToUtf8(request.path.wstring())+(request.eol==SC_EOL_LF?"\n":request.eol==SC_EOL_CR?"\r":"\r\n")+content;
        if(!WriteSnapshot(candidate,content,metadata))throw std::runtime_error("Cannot create AutoSave backup: "+files::ToUtf8(candidate.wstring()));
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
    std::future<RecoveryOutcome> future;
};
bool SaveDocumentRecovery(MainFrame& frame,EditorDocument& document,bool force) {
    if (!frame.IsWindowEnabled() || frame.runtime.operationDepth || !NeedsRecovery(document)) return false;
    CancelDocumentBackup(frame, document);
    auto request = Request(frame, document);
    auto outcome = WriteRecovery(request, document.Text());
    ApplyRecovery(frame, document, request, outcome, force); return true;
}
void CancelDocumentBackup(MainFrame& frame, const EditorDocument& document, bool keepBackup) {
    auto task = frame.runtime.autoSaveTask;
    if (!task || task->identity != document.searchIdentity) return;
    if (task->future.valid()) {
        try { auto outcome = task->future.get(); if (!outcome.overwritten && !keepBackup) DeleteSnapshot(outcome.path); } catch (const std::exception&) {}
    }
    frame.runtime.autoSaveTask.reset();
}
void QueueDocumentBackups(MainFrame& frame) {
    if (frame.runtime.autoSaveTask || !frame.IsWindowEnabled() || frame.runtime.operationDepth) return;
    auto period = AutoSavePeriodMilliseconds(frame); if (!period) return;
    auto now = GetTickCount64();
    for (const auto& document : frame.documents) {
        auto& d = *document;
        if (now - d.runtime.lastAutoSave < period || !NeedsRecovery(d)) continue;
        auto task = std::make_shared<AutoSaveTask>(); task->identity = d.searchIdentity;
        task->request = Request(frame, d); task->request.overwrite = (frame.settings.Get(IDM_SET_FILE_AUTOSAVE,6)&16) != 0;
        task->length = static_cast<size_t>(d.Send(SCI_GETLENGTH)); task->text.reserve(task->length);
        d.runtime.lastAutoSave = now; frame.runtime.autoSaveTask = std::move(task);
        if (!frame.runtime.workTimerArmed) { frame.SetTimer(3, 30); frame.runtime.workTimerArmed = true; }
        break;
    }
}
void PumpDocumentBackup(MainFrame& frame) {
    auto task = frame.runtime.autoSaveTask; if (!task) return;
    auto found = frame.tabs.documentsById.find(task->identity);
    if (found == frame.tabs.documentsById.end()) { frame.runtime.autoSaveTask.reset(); return; }
    auto& d = *found->second;
    if (task->future.valid()) {
        if (task->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        try {
            auto outcome = task->future.get();
            if (d.path == task->request.path) ApplyRecovery(frame, d, task->request, outcome, false);
            else if (!outcome.overwritten) DeleteSnapshot(outcome.path);
        } catch (const std::exception&) {}
        frame.runtime.autoSaveTask.reset(); return;
    }
    if (!NeedsRecovery(d) || d.path != task->request.path || d.style.revision != task->request.revision || d.encoding != task->request.encoding || d.Send(SCI_GETEOLMODE) != task->request.eol) {
        frame.runtime.autoSaveTask.reset(); d.runtime.lastAutoSave = 0; return;
    }
    if (task->offset < task->length) {
        auto bytes = d.Range(task->offset, (std::min)(size_t(256 * 1024), task->length - task->offset));
        task->offset += bytes.size(); task->text.append(bytes); return;
    }
    task->future = std::async(std::launch::async, [request = task->request, text = std::move(task->text)] { return WriteRecovery(request, text); });
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
