#pragma once
#include "App.h"
#include "FileIO.h"
#include "StyleState.h"
#include "AppRuntime.h"
#include "FileVariables.h"
#include "FastFileView.h"
#include <functional>
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
struct SearchMatch { LRESULT start = 0, end = 0, line = 0, lineStart = 0; std::shared_ptr<const std::string> source; };
struct EditorDocument {
    inline static unsigned long long nextSearchIdentity = 0;
    const unsigned long long searchIdentity = ++nextSearchIdentity;
    ~EditorDocument() { fastView.reset(); if (hwnd && ::IsWindow(hwnd)) ::DestroyWindow(hwnd); }
    struct LoadMetrics { files::ReadStats fileRead; double allocation = 0, readDecode = 0, lineScan = 0, insertion = 0, configure = 0; } loadMetrics;
    std::unique_ptr<FastFileView> fastView;
    bool FastView() const { return fastView && fastView->Active(); }
    HWND ViewWindow() const { return fastView ? fastView->Window() : hwnd; }
    void OpenFastFile(const std::filesystem::path& source, const files::ReadOptions& options, std::optional<files::Encoding> forced = {});
    HWND hwnd = nullptr;
    std::filesystem::path path;
    std::wstring untitled;
    files::Encoding encoding = files::Encoding::Utf8;
    std::wstring language = L"Text";
    SearchState search;
    DocumentStyleState style;
    DocumentRuntime runtime;
    FileVariableState fileVariables;
    bool encodingDirty = false;
    int edgeMode = EDGE_LINE;
    LRESULT Send(UINT msg, WPARAM w = 0, LPARAM l = 0) const {
        // This fork accepts an optional line hint for GETCOLUMN. Its default
        // must be -1; zero means "use line 0" and corrupts later-line columns.
        if (FastView()) {
            if (msg == SCI_SETREADONLY && !w) return 0;
            if (msg == SCI_SETWRAPMODE) w = SC_WRAP_NONE;
            if (msg == SCI_SETMARGINWIDTHN && !w) l = 0;
        }
        if (msg == SCI_GETCOLUMN && l == 0) l = -1;
        return ::SendMessageW(hwnd, msg, w, l);
    }
    bool Modified() const { return encodingDirty || Send(SCI_GETMODIFY) != 0; }
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
    bool ReplaceOne();
    size_t ReplaceAll(bool selectionOnly = false);
    std::vector<SearchMatch> CollectMatches(bool selectionOnly = false);
    size_t FindAll(bool selectAll = false, bool bookmark = false);
    void SetEncoding(files::Encoding value);
};
void HandleStyleNotification(EditorDocument& document, const SCNotification& notification);
class EditorChild : public CTabbedMDIChildWindowImpl<EditorChild> {
public:
    DECLARE_FRAME_WND_CLASS(L"MultiPad4.MdiDocument", 0)
    HWND owner = nullptr;
    EditorDocument* document = nullptr;
    BEGIN_MSG_MAP(EditorChild)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_MDIACTIVATE, OnActivate)
        CHAIN_MSG_MAP(CTabbedMDIChildWindowImpl<EditorChild>)
    END_MSG_MAP()
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&) {
        ::PostMessageW(owner, WM_CLOSE_TAB, reinterpret_cast<WPARAM>(m_hWnd), 0); return 0;
    }
    LRESULT OnNotify(UINT, WPARAM, LPARAM l, BOOL& handled) {
        auto* n = reinterpret_cast<NMHDR*>(l);
        if (document && n->hwndFrom == document->hwnd) ::SendMessageW(owner, WM_APP + 0x120, 0, l);
        if (document && n->hwndFrom == document->hwnd) HandleStyleNotification(*document, *reinterpret_cast<SCNotification*>(l));
        if (n->code == SCN_SAVEPOINTLEFT || n->code == SCN_SAVEPOINTREACHED || n->code == SCN_UPDATEUI || n->code == SCN_ZOOM)
            ::PostMessageW(owner, WM_DOCUMENT_CHANGED, 0, 0);
        handled = FALSE; return 0;
    }
    LRESULT OnActivate(UINT, WPARAM, LPARAM, BOOL& handled) {
        ::PostMessageW(owner, WM_DOCUMENT_CHANGED, 0, 0); handled = FALSE; return 0;
    }
};
// Document lookup follows the framework tab order, including drag reordering.
class EditorTabs {
public:
    HWND owner = nullptr, m_hWnd = nullptr;
    CDotNetTabCtrl<CTabViewTabItem>* control = nullptr;
    std::vector<std::unique_ptr<EditorChild>> children;
    operator HWND() const { return m_hWnd; }
    int GetPageCount() const { return control ? control->GetItemCount() : 0; }
    HWND GetPageHWND(int i) const { return control->GetItem(i)->GetTabView(); }
    EditorChild* Child(int i) const {
        HWND window = GetPageHWND(i);
        for (const auto& child : children) if (child->m_hWnd == window) return child.get();
        return nullptr;
    }
    void* GetPageData(int i) const { auto* child = Child(i); return child ? child->document : nullptr; }
    int GetActivePage() const { return control ? control->GetCurSel() : -1; }
    void SetActivePage(int i) {
        ::SendMessageW(m_hWnd, WM_MDIACTIVATE, reinterpret_cast<WPARAM>(GetPageHWND(i)), 0);
        ::PostMessageW(owner, WM_DOCUMENT_CHANGED, 0, 0);
    }
    std::wstring GetPageTitle(int i) const { return control->GetItem(i)->GetText().GetString(); }
    void SetPageTitle(int i, const wchar_t* title) { if (auto* child = Child(i)) child->SetTitle(title); }
    bool AddPage(HWND editor, const wchar_t* title, int, void* data) {
        auto child = std::make_unique<EditorChild>(); child->owner = owner;
        child->document = static_cast<EditorDocument*>(data);
        // A nonzero style replaces (rather than augments) WTL's defaults.
        // Keep the caption and sizing frame so Win32 can maximize, restore,
        // cascade and tile this real MDI child.
        if (!child->Create(m_hWnd, CWindow::rcDefault, title, EditorChild::GetWndStyle(0) | WS_MAXIMIZE)) return false;
        ::SetParent(editor, child->m_hWnd); child->m_hWndClient = editor;
        ::ShowWindow(editor, SW_SHOW); child->UpdateLayout(); child->SetTitle(title);
        children.push_back(std::move(child)); return true;
    }
    void RemovePage(int i) {
        auto* child = Child(i); if (!child) return;
        ::SendMessageW(m_hWnd, WM_MDIDESTROY, reinterpret_cast<WPARAM>(child->m_hWnd), 0);
        std::erase_if(children, [child](const auto& p) { return p.get() == child; });
    }
};

