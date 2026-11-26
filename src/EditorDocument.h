#pragma once
#include "App.h"
#include "FileIO.h"
#include "FileSearch.h"
#include "StyleState.h"
#include "AppRuntime.h"
#include "FileVariables.h"
#include "FastFileView.h"
#include "DocumentTabStrip.h"
#include <functional>
#include <unordered_map>
#include "StatusStatistics.h"
#include "SimdText.h"
namespace files { struct ReplacePattern; }
struct SearchState {
    std::wstring findText, replaceText;
    int flags = 0;
    bool wrap = true;
    bool regex = false, wildcard = false, transformBackslashes = false, dotMatchesNewline = false;
    bool useBoostRegex = false;
    bool closeAfterAction = false, bookmarkMatches = false, useMonospaced = false, transparent = false;
    bool closeAfterFind = false, closeAfterReplace = false;
    std::vector<std::wstring> findHistory, replaceHistory;
    LRESULT lastMatchStart = -1, lastMatchEnd = -1;
    size_t matches = 0;
};
struct SearchMatch {
    LRESULT start = 0, end = 0, line = 0, lineStart = 0;
    std::shared_ptr<const std::string> source;
    std::shared_ptr<const large::Document::Snapshot> mappedSource;
    large::Position nativeStart = 0, nativeEnd = 0, nativeLineStart = 0;
    std::string preview;
    std::shared_ptr<const large::Document::Snapshot> diskSource;
    unsigned long long sourceRevision = 0, sourceSize = 0, sourceWriteTime = 0;
    LRESULT sourceBase = 0;
    std::optional<files::ReplacementTarget> replacementTarget;
};
struct OrdinarySearchResult {
    struct Line { LRESULT number, start; std::shared_ptr<const std::string> text; };
    std::shared_ptr<large::HitArchive> hits;
    std::vector<Line> lines;
};
struct EditorDocument {
    inline static unsigned long long nextSearchIdentity = 0;
    const unsigned long long searchIdentity = ++nextSearchIdentity;
    ~EditorDocument() { fastView.reset(); if (hwnd && ::IsWindow(hwnd)) ::DestroyWindow(hwnd); }
    struct LoadMetrics { files::ReadStats fileRead; double allocation = 0, readDecode = 0, lineScan = 0, insertion = 0, configure = 0;
        simd::LineEndings endings; unsigned long long revision = 0; LRESULT document = 0;
    } loadMetrics;
    std::unique_ptr<FastFileView> fastView;
    bool FastView() const { return fastView && fastView->Active(); }
    HWND ViewWindow() const { return fastView ? fastView->Window() : hwnd; }
    void OpenFastFile(const std::filesystem::path& source, const files::ReadOptions& options, std::optional<files::Encoding> forced = {});
    HWND hwnd = nullptr;
    SciFnDirect directFunction = nullptr;
    sptr_t directPointer = 0;
    DWORD editorThread = 0;
    std::filesystem::path path;
    std::wstring untitled;
    bool titleDirty = true, publishedModified = false, publishedEncodingDirty = false;
    unsigned long long excerptRevision = 0;
    std::filesystem::path publishedPath;
    std::wstring publishedUntitled, publishedTitle;
    size_t publishedTabIndex = 0;
    files::Encoding encoding = files::Encoding::Utf8;
    std::wstring language = L"Text";
    SearchState search;
    DocumentStyleState style;
    DocumentRuntime runtime;
    StatusStatistics statusStatistics;
    FileVariableState fileVariables;
    bool encodingDirty = false;
    int edgeMode = EDGE_LINE;
    LRESULT Send(UINT msg, WPARAM w = 0, LPARAM l = 0) const {
        // This fork accepts an optional line hint for GETCOLUMN. Its default
        // must be -1; zero means "use line 0" and corrupts later-line columns.
        if (FastView()) {
            if (msg == SCI_SETWRAPMODE) w = SC_WRAP_NONE;
            if (msg == SCI_SETMARGINWIDTHN && !w) l = 0;
            // Painting/preferences queries never become whole-file operations.
            // Document-wide APIs are explicit on FastFileView/large::Document.
        }
        if (msg == SCI_GETCOLUMN && l == 0) l = -1;
        if (directFunction && GetCurrentThreadId() == editorThread)
            return directFunction(directPointer, msg, w, l);
        return ::SendMessageW(hwnd, msg, w, l);
    }
    bool Modified() const { return encodingDirty || (FastView() ? fastView->Modified() : Send(SCI_GETMODIFY) != 0); }
    std::wstring Title() const;
    bool Create(HWND parent);
    void Load(const files::Text& text, int additionalDocumentOptions = 0);
    void LoadFile(const std::filesystem::path& source, const files::ReadOptions& options = {}, std::optional<files::Encoding> forced = {});
    void LoadStream(size_t reserve, int options, const std::function<files::Encoding(const files::ChunkConsumer&)>& produce);
    bool LargeFile() const { return (Send(SCI_GETDOCUMENTOPTIONS) & SC_DOCUMENTOPTION_TEXT_LARGE) != 0; }
    void SaveStreaming(const std::filesystem::path& destination) const;
    std::string Range(size_t start, size_t length) const;
    std::string Text() const;
    void ConfigureLexer();
    bool FindNext(bool backwards = false, const std::function<bool(bool)>& confirmWrap = {});
    large::SearchOptions MappedSearchOptions() const;
    std::string SearchReplacement();
    files::ReplacePattern SearchReplacePattern();
    void ValidateSearch(bool replacing = false);
    std::vector<SearchMatch> MappedMatches(const large::Document::Snapshot& snapshot, const std::vector<large::SearchHit>& hits);
    bool ReplaceOne();
    size_t ReplaceAll(bool selectionOnly = false);
    std::vector<SearchMatch> CollectMatches(bool selectionOnly = false);
    struct MatchCursor {
        std::string needle;
        LRESULT next = 0, limit = 0;
        int flags = 0;
        bool done = false;
    };
    MatchCursor BeginMatches(bool selectionOnly = false);
    bool MatchStep(MatchCursor& cursor, const std::function<void(LRESULT, LRESULT)>& consume);
    size_t FindAll(bool selectAll = false, bool bookmark = false);
    void SetEncoding(files::Encoding value);
};
void HandleStyleNotification(EditorDocument& document, const SCNotification& notification);
class EditorChild {
public:
    HWND owner = nullptr;
    HWND m_hWnd = nullptr, m_hWndClient = nullptr;
    EditorDocument* document = nullptr;
    bool Create(HWND mdi, const wchar_t* title);
    void UpdateLayout();
    void UpdateFileIcon();
    HICON GetIcon(bool large = true) const {
        return reinterpret_cast<HICON>(SendMessageW(m_hWnd, WM_GETICON, large ? ICON_BIG : ICON_SMALL, 0));
    }
    ~EditorChild() { if (fileIcon) DestroyIcon(fileIcon); }
private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l);
    void Notify(const NMHDR& notification);
    HICON fileIcon = nullptr;
    int fileIconResource = 0;
    UINT fileIconDpi = 0;
};
// Document lookup follows the tab control order, including drag reordering.
class EditorTabs {
public:
    HWND owner = nullptr, m_hWnd = nullptr;
    DocumentTabStrip* control = nullptr;
    std::vector<std::unique_ptr<EditorChild>> children;
    std::unordered_map<HWND, EditorChild*> childrenByWindow;
    std::unordered_map<UINT_PTR, EditorDocument*> documentsById;
    operator HWND() const { return m_hWnd; }
    int GetPageCount() const { return control ? control->GetItemCount() : 0; }
    HWND GetPageHWND(int i) const { return control ? control->ChildAt(i) : nullptr; }
    EditorChild* Child(int i) const {
        HWND window = GetPageHWND(i);
        auto found = childrenByWindow.find(window);
        return found == childrenByWindow.end() ? nullptr : found->second;
    }
    void* GetPageData(int i) const { auto* child = Child(i); return child ? child->document : nullptr; }
    int GetActivePage() const { return control ? control->GetCurSel() : -1; }
    void SetActivePage(int i) {
        if (control) control->Activate(i);
        ::PostMessageW(owner, WM_DOCUMENT_CHANGED, 0, 0);
    }
    std::wstring GetPageTitle(int i) const {
        const HWND child = GetPageHWND(i);
        const int length = GetWindowTextLengthW(child);
        std::wstring title(static_cast<size_t>(length) + 1, L'\0');
        title.resize(GetWindowTextW(child, title.data(), length + 1));
        return title;
    }
    void SetPageTitle(int i, const wchar_t* title) {
        if (auto* child = Child(i)) {
            SetWindowTextW(child->m_hWnd, title);
            control->SetTitle(i, title);
            child->UpdateFileIcon();
            control->SetIcon(child->m_hWnd, child->GetIcon(false));
        }
    }
    bool AddPage(HWND editor, const wchar_t* title, int, void* data) {
        auto child = std::make_unique<EditorChild>(); child->owner = owner;
        child->document = static_cast<EditorDocument*>(data);
        if (!child->Create(m_hWnd, title)) return false;
        child->UpdateFileIcon();
        if (!control->Insert(child->m_hWnd, title)) {
            ::SendMessageW(m_hWnd, WM_MDIDESTROY, reinterpret_cast<WPARAM>(child->m_hWnd), 0);
            control->SyncSelection();
            return false;
        }
        ::SetParent(editor, child->m_hWnd); child->m_hWndClient = editor;
        ::ShowWindow(editor, SW_SHOW); child->UpdateLayout();
        childrenByWindow.emplace(child->m_hWnd, child.get());
        documentsById.emplace(child->document->searchIdentity, child->document);
        children.push_back(std::move(child));
        const HWND childWindow = children.back()->m_hWnd;
        control->SetIcon(childWindow, children.back()->GetIcon(false));
        // MDI can focus the child while it is still being created, before its editor is attached.
        if (::GetFocus() == childWindow) ::SetFocus(editor);
        return true;
    }
    void RemovePage(int i) {
        auto* child = Child(i); if (!child) return;
        childrenByWindow.erase(child->m_hWnd);
        documentsById.erase(child->document->searchIdentity);
        ::SendMessageW(m_hWnd, WM_MDIDESTROY, reinterpret_cast<WPARAM>(child->m_hWnd), 0);
        control->Remove(i);
        std::erase_if(children, [child](const auto& p) { return p.get() == child; });
    }
};

