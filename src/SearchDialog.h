#pragma once
#include "EditorDocument.h"
#include "SearchResources.h"
#include "FileSearch.h"
#include "ThreadPool.h"
class MainFrame;
class SearchDialog {
public:
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
    static INT_PTR CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l);
private:
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
    std::unique_ptr<files::SearchWalker> replaceWalker;
    unsigned long long fileGeneration = 0;
    struct OpenSearch {
        unsigned long long identity = 0, revision = 0;
        std::filesystem::path path;
        std::shared_ptr<std::string> text = std::make_shared<std::string>();
        size_t offset = 0, length = 0;
        std::atomic<bool> cancel{false};
        work::Task<files::FileSearchResult> search;
        bool mapped = false;
    };
    std::shared_ptr<OpenSearch> openSearch;
    void ReplaceFileStep();
    void StartFiles(bool replace);
    void FileStep();
    void FinishFiles(bool cancelled);
};
