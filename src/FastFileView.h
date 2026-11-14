#pragma once
#include "App.h"
#include "FileIO.h"
#include "LazyDocument.h"
#include "LazySearch.h"
#include <atomic>
#include <mutex>
#include "ThreadPool.h"
struct EditorDocument;
// The Scintilla window is a bounded rendering/editing viewport. Source text,
// global positions, undo and the lazy line index belong to the piece tree.
class FastFileView {
    EditorDocument& document;
    files::ReadOptions options;
    std::optional<files::Encoding> forced;
    std::unique_ptr<large::Document> model;
    HWND window = nullptr, label = nullptr, button = nullptr;
    large::Position begin = 0, end = 0;
    files::Encoding encoding = files::Encoding::Utf8;
    std::string viewText;
    std::optional<large::Selection> extendedSelection;
    bool loading = false, readOnly = false;
    unsigned editDepth = 0;
    work::Task<void> worker;
    std::atomic<bool> cancel{false};
    struct TaskResult { large::Document::Snapshot snapshot; std::optional<large::Document::Match> match; std::string error; bool completed = false; std::function<void()> apply; };
    std::mutex resultMutex;
    TaskResult result;
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditorProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void Layout();
    void UpdateBar();
    void CancelTask();
    large::Document::Snapshot matchSnapshot;
    std::optional<large::SearchHit> lastHit;
    large::SearchOptions lastOptions;
    bool repeatZero = false;
    struct Scope { large::Document::Snapshot snapshot; large::Position first, last; large::SearchOptions options; };
    std::optional<Scope> searchScope;
    std::string resultStatus;
    struct MarkSet { large::Document::Snapshot snapshot; std::shared_ptr<large::HitArchive> hits; bool bookmarks; };
    std::vector<MarkSet> marks;
    void PaintMarks();
    void ApplyReplacement(const large::Document::Snapshot& expected, const std::shared_ptr<large::Document>& branch,
        large::Selection before, large::Selection after, size_t count);
    void Launch(const std::function<std::optional<large::Document::Match>(const large::Document::Snapshot&, const std::atomic<bool>&)>& action);
    void CompleteTask();
    std::string Encode(std::string_view utf8) const;
    std::string Decode(std::string_view raw) const;
    large::Position NativePrefix(std::string_view utf8, size_t bytes) const;
    large::Selection Selection() const;
    void RestoreSelection(large::Selection selection);
    bool HandleMessage(UINT message, WPARAM w, LPARAM l, LRESULT& result);
public:
    explicit FastFileView(EditorDocument& document, const std::filesystem::path& path,
        const files::ReadOptions& options, std::optional<files::Encoding> forced);
    ~FastFileView();
    void Attach();
    void Seek(large::Position offset, bool bottom = false);
    void ShowSelection(large::Position start, large::Position length);
    // Explicit compatibility operation; ordinary typing never calls this.
    void LoadForEditing();
    void SaveCopy(const std::filesystem::path& path);
    void Save(const std::filesystem::path& path, bool copy);
    void OnModified(const SCNotification& notification);
    void BeginEdit();
    void EndEdit();
    void Undo();
    void Redo();
    void SetReadOnly(bool value);
    void StartFind(const std::wstring& needle, bool backwards, bool matchCase, bool wholeWord, bool wrap);
    void StartSearch(large::SearchOptions options, bool backwards, bool wrap, bool selectionOnly = false);
    bool ReplaceMatch(const large::SearchOptions& options, std::string_view replacement, bool captures);
    size_t ReplaceEvery(const large::SearchOptions& options, std::string_view replacement, bool captures, bool selectionOnly);
    void StartReplaceEvery(large::SearchOptions options, std::string replacement, bool captures, bool selectionOnly);
    std::shared_ptr<large::HitArchive> ArchiveMatches(const large::SearchOptions& options, bool selectionOnly);
    std::vector<large::SearchHit> Matches(const large::SearchOptions& options, bool selectionOnly);
    void StartEnumerate(large::SearchOptions options, bool selectionOnly,
        const std::function<void(const large::Document::Snapshot&, const std::shared_ptr<large::HitArchive>&)>& completed, bool retainOffsets = true, bool indexLines = false);
    void SetMarks(large::Document::Snapshot snapshot, std::vector<large::SearchHit> matches, bool bookmarks);
    void SetMarks(large::Document::Snapshot snapshot, std::shared_ptr<large::HitArchive> matches, bool bookmarks, bool purge = true);
    large::Selection GlobalSelection() const { return Selection(); }
    files::Encoding Encoding() const { return encoding; }
    std::string ReadUtf8(large::Position offset, size_t length) const { return Decode(model->Read(offset, length)); }
    void StopWork() { CancelTask(); }
    const std::string& Status() const { return resultStatus; }
    void StartGoToLine(large::Position line);
    void StartExternalMatch(large::Document::Snapshot utf8, large::Position line, large::Position lineStart, large::Position start, large::Position end);
    bool Active() const { return model != nullptr; }
    bool Modified() const { return model && model->Modified(); }
    bool CanUndo() const { return model && model->CanUndo(); }
    bool CanRedo() const { return model && model->CanRedo(); }
    bool ReadOnly() const { return readOnly; }
    bool Loading() const { return loading; }
    bool Busy() const { return worker.Valid(); }
    HWND Window() const { return window; }
    large::Position Size() const { return model ? model->Size() : 0; }
    large::Position Begin() const { return begin; }
    large::Position End() const { return end; }
    large::Position BytesRead() const { return model ? model->Statistics().sourceBytesRead : 0; }
    large::Position Caret() const { return Selection().caret; }
    large::Document& Model() { return *model; }
};
