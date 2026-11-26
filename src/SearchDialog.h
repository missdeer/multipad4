#pragma once
#include "EditorDocument.h"
#include "SearchResources.h"
#include "FileSearch.h"
#include "FileReplace.h"
#include "ThreadPool.h"
class MainFrame;
class SearchDialog {
public:
    ~SearchDialog();
    HWND hwnd = nullptr;
    MainFrame* owner = nullptr;
    EditorDocument* document = nullptr;
    bool replacing = false;
    HACCEL accelerators = nullptr;
    RECT savedPosition{};
    bool hasSavedPosition = false;
    int page = 0, layoutWidth = 0, layoutHeight = 0;
    struct ControlLayout { HWND control; RECT rectangle; bool right, stretch; };
    std::vector<ControlLayout> controls;
    BOOL PreTranslateMessage(MSG* message);
    void Open(MainFrame& frame, EditorDocument& target, bool replace);
    void SetPage(int value);
    void Layout();
    void UpdateTransparency();
    void Close();
    void SyncDocument();
    bool ReadFields();
    void Execute(int command);
    void Status(const std::wstring& text);
    bool SearchBusy() const { return ordinaryTask != nullptr; }
    bool DeferOpen(const std::filesystem::path& path, std::optional<files::Encoding> encoding);
    bool CommittingReplacement() const;
    void ServiceReplacement();
    static INT_PTR CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l);
private:
    friend void RunSearchRetirementTests(MainFrame& frame);
    struct SearchTask {
        uintptr_t token = 0;
        int command;
        SearchState state;
        bool selection = false, purge = false, mapped = false, mappedDone = false, yielding = false;
        unsigned long long generation = 0, identity = 0, revision = 0;
        std::vector<unsigned long long> documents;
        size_t next = 0, count = 0, files = 0;
        EditorDocument::MatchCursor cursor;
        std::shared_ptr<OrdinarySearchResult> result;
    };
    std::unique_ptr<SearchTask> ordinaryTask;
    uintptr_t nextSearchToken = 0;
    void ContinueSearch();
    void StartSearch(int command, bool selection);
    void SearchStep();
    void FinishSearch(bool interrupted, bool publish = true);
    struct OpenFileState {
        unsigned long long identity, writeTime, size;
        std::filesystem::path path;
        bool operator==(const OpenFileState&) const = default;
    };
    std::vector<OpenFileState> indexedOpenFiles;
    std::unique_ptr<files::OpenFileIndex> openFileIndex;
    EditorDocument* FindOpenFile(const std::filesystem::path& path, bool live = false);
    unsigned long long pendingIdentity = 0;
    void WatchMappedTask();
    bool fileRunning = false, replaceFiles = false;
    bool expanded = true, active = true, transparentAlways = false;
    int opacity = 180;
    bool Chinese() const;
    void Localize();
    void ResizeHeight();
    size_t fileHits = 0, foundFiles = 0, fileErrors = 0;
    SearchState fileSearch;
    std::wstring fileFilter;
    bool recursive = true, hidden = false;
    std::unique_ptr<files::FileSearch> fileTask;
    std::shared_ptr<files::SearchWalker> replaceWalker;
    struct ReplaceWork {
        enum class Stage { Walk, Prepare, Commit } stage = Stage::Walk;
        std::atomic<bool> cancel{false};
        std::optional<std::filesystem::path> path;
        files::ReplaceVersion version;
        files::ReplaceResult result;
        size_t errors = 0;
        bool committed = false;
        unsigned long long identity = 0, revision = 0;
        std::shared_ptr<std::string> input;
        size_t offset = 0, length = 0;
        work::Task<void> task; // Declared last so destruction drains before payload fields are freed.
    };
    std::shared_ptr<ReplaceWork> replaceWork;
    files::ReplacePattern replacePattern;
    struct DeferredOpen { std::filesystem::path path; std::optional<files::Encoding> encoding; };
    std::vector<DeferredOpen> deferredOpens;
    unsigned long long fileGeneration = 0;
    struct OpenSearch {
        unsigned long long identity = 0, revision = 0;
        unsigned long long viewVersion = 0;
        std::filesystem::path path;
        std::shared_ptr<std::string> text = std::make_shared<std::string>();
        size_t offset = 0, length = 0;
        work::SearchTask<files::FileSearchResult> search;
        bool mapped = false;
    };
    std::shared_ptr<OpenSearch> openSearch;
    void ReplaceFileStep();
    void StartFiles(bool replace);
    void FileStep();
    void FinishFiles(bool cancelled, bool publish = true);
};
