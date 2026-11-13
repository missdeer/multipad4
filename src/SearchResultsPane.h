#pragma once
#include "DockHost.h"
#include "EditorDocument.h"
#include "SearchResultLexer.h"
class MainFrame;
class SearchResultsPane : public dockwins::CTitleDockingWindowImpl<SearchResultsPane, CWindow, dockwins::COutlookLikeTitleDockingWindowTraits> {
public:
    using Base = dockwins::CTitleDockingWindowImpl<SearchResultsPane, CWindow, dockwins::COutlookLikeTitleDockingWindowTraits>;
    DECLARE_WND_CLASS_EX(L"MultiPad4.SearchResults", CS_DBLCLKS, COLOR_WINDOW)
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
    void Add(const std::filesystem::path& path, unsigned long long identity, const std::vector<SearchMatch>& matches, const std::wstring& title = {});
    void AddMapped(EditorDocument& document, large::Document::Snapshot snapshot, std::shared_ptr<large::HitArchive> hits);
    bool LoadMore(size_t archive);
    void Finish(size_t hits, size_t files);
    bool Navigate(size_t line, size_t matchIndex = 0);
    BOOL PreTranslateMessage(MSG* message);
    BEGIN_MSG_MAP(SearchResultsPane)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContext)
        CHAIN_MSG_MAP(Base)
    END_MSG_MAP()
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContext(UINT, WPARAM, LPARAM, BOOL&);
private:
    struct MappedPage { unsigned long long identity; std::filesystem::path path; large::Document::Snapshot snapshot; std::shared_ptr<large::HitArchive> hits; size_t next = 0; };
    std::vector<MappedPage> archives;
    size_t header = 0;
    void Append(const std::string& text, SearchResultLexer::Line style, Target target);
};
