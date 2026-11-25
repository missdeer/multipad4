#pragma once
#include "App.h"
#include "EditorDocument.h"
#include "SearchResultLexer.h"
#include "FileSearch.h"
class MainFrame;
class SearchResultsPane : public PaneWindow<SearchResultsPane> {
public:
    static constexpr auto WindowClass = L"MultiPad4.SearchResults";
    MainFrame* owner = nullptr;
    HWND editor = nullptr;
    struct Target { unsigned long long identity = 0; std::filesystem::path path; SearchMatch match; std::vector<SearchMatch> matches; size_t archive = static_cast<size_t>(-1); };
    std::vector<Target> targets;
    std::vector<SearchResultLexer::Line> lines;
    bool visible = false;
    unsigned long long generation = 0;
    void CreatePane(MainFrame& frame);
    void Reveal();
    void Clear();
    void Begin(const std::wstring& query);
    void Add(EditorDocument& document, const std::vector<SearchMatch>& matches);
    void AddOrdinary(EditorDocument& document, std::shared_ptr<OrdinarySearchResult> result);
    void Add(const std::filesystem::path& path, unsigned long long identity, const std::vector<SearchMatch>& matches, const std::wstring& title = {});
    void AddMapped(EditorDocument& document, large::Document::Snapshot snapshot, std::shared_ptr<large::HitArchive> hits);
    void AddFile(files::FileSearchResult result, unsigned long long identity = 0, unsigned long long revision = 0);
    bool LoadMore(size_t archive);
    void Finish(size_t hits, size_t files);
    void Skipped(const std::filesystem::path& path, const std::string& reason);
    bool Navigate(size_t line, size_t matchIndex = 0);
    BOOL PreTranslateMessage(MSG* message);
    LRESULT Dispatch(UINT message, WPARAM w, LPARAM l, BOOL& handled) {
        switch (message) {
        case WM_CREATE: return OnCreate(message, w, l, handled);
        case WM_SIZE: return OnSize(message, w, l, handled);
        case WM_CLOSE: return OnClose(message, w, l, handled);
        case WM_NOTIFY: return OnNotify(message, w, l, handled);
        case WM_CONTEXTMENU: return OnContext(message, w, l, handled);
        }
        handled = FALSE;
        return 0;
    }
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContext(UINT, WPARAM, LPARAM, BOOL&);
private:
    struct MappedPage { unsigned long long identity; std::filesystem::path path; large::Document::Snapshot snapshot; std::shared_ptr<large::HitArchive> hits; size_t next = 0; files::FileSearchResult disk; unsigned long long revision = 0; bool file = false; std::shared_ptr<OrdinarySearchResult> ordinary; std::wstring title; };
    std::vector<MappedPage> archives;
    struct Row { std::string text; SearchResultLexer::Line style; Target target; };
    std::vector<Row> pendingRows;
    unsigned batchDepth = 0;
    bool changingText = false;
    size_t pendingStyle = SIZE_MAX;
    struct Batch {
        SearchResultsPane& pane;
        int exceptions;
        explicit Batch(SearchResultsPane& pane);
        ~Batch() noexcept(false);
    };
    void CommitRows();
    void StyleRows(size_t first, size_t last = SIZE_MAX);
    size_t header = 0;
    void Append(const std::string& text, SearchResultLexer::Line style, Target target);
};
