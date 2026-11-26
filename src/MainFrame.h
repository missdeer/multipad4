#pragma once
#include "App.h"
#include "resource.h"
#include "EditorDocument.h"
#include "EditorCommands.h"
#include "SearchDialog.h"
#include "SearchResultsPane.h"
#include "AppSettings.h"
#include <optional>
#include <array>
#include "PluginHost.h"
#include "PluginAdmin.h"
#include "PluginMaintenance.h"
#include "AppUpdate.h"
class MainFrame;
enum class SaveMode { Normal, As, Copy };
class MainFrame : public WinWindow {
public:
    static constexpr auto PortableWindowProperty = L"MultiPad4.Portable";
    HWND m_hWndMDIClient = nullptr, m_hWndToolBar = nullptr, m_hWndStatusBar = nullptr;
    HWND Create(const RECT& position, HINSTANCE instance);
    HWND MDIGetActive(BOOL* maximized = nullptr) const;
    void MDIMaximize(HWND child);
    void MDIRestore(HWND child);
    PluginHost plugins;
    PluginAdmin pluginAdmin;
    std::optional<PluginMaintenance> pendingPluginMaintenance;
    AppUpdater appUpdater;
    DocumentTabStrip tabStrip;
    EditorTabs tabs;
    DocumentTabStrip& GetMDITabCtrl() { return tabStrip; }
    std::vector<std::unique_ptr<EditorDocument>> documents;
    unsigned untitledCount = 0;
    SearchDialog searchDialog;
    SearchResultsPane searchResults;
    AppSettings settings;
    UniqueHandle exitSessionLock;
    bool exitSessionEligible = false;
    DocumentStyleState defaultStyle;
    FrameRuntime runtime;
    HMENU windowMenu = nullptr;
    HIMAGELIST toolbarImages = nullptr;
    HIMAGELIST toolbarDisabledImages = nullptr;
    HACCEL mdiAccelerators = nullptr, editorAccelerators = nullptr;
    bool toolbarVisible = true, statusVisible = true;
    std::vector<std::pair<UINT_PTR, std::wstring>> publishedDocuments;
    UINT_PTR publishedActive = 0;
    unsigned long long publishedStructure = ~0ULL, captionExcerptRevision = ~0ULL, captionIdentity = 0;
    std::wstring publishedCaption, captionExcerpt;
    void UpdateCaption();
    std::map<UINT, std::pair<bool, bool>> toolbarState;
    std::array<std::wstring, 13> statusText;
    int statusWidth = -1;
    UINT statusDpi = 0;
    HFONT statusFont = nullptr;
    BOOL PreTranslateMessage(MSG* msg);
    void UpdateLayout(BOOL resizeBars = TRUE);
    EditorDocument* Active();
    EditorDocument* NewDocument();
    bool Open(const std::filesystem::path& path, std::optional<files::Encoding> forcedEncoding = std::nullopt);
    bool Save(EditorDocument& document, SaveMode mode = SaveMode::Normal, bool keepBackup = false);
    bool SaveTo(EditorDocument& document, const std::filesystem::path& path, bool copy = false, bool keepBackup = false);
    bool Reload(EditorDocument& document);
    bool Close(int index, bool confirm = true);
    EditorDocument* AddDocument(std::unique_ptr<EditorDocument> document);
    void CloseTabs(int anchor, UINT command);
    bool ConfirmClose(EditorDocument& document, bool keepBackup = true);
    bool Relaunch(bool elevated = false, bool empty = false, bool closeCurrent = true);
    void Refresh();
    void FindNext(bool backwards = false);
    bool ConfirmSearchWrap(bool backwards);
    void ShowSearch(bool replace);
    void CreateUpstreamUI();
    void ApplyTabTitleFont();
    int TabWidthPreference() const;
    void ApplyTabWidth();
    void UpdateCommandUI();
    void UpdateStatus();
    CommandState State(UINT id);
    CommandState QueryState(UINT id);
    void Error(const std::exception& error);
    LRESULT Dispatch(UINT message, WPARAM w, LPARAM l, BOOL& handled);
    LRESULT OnArrangeMDI(WORD, WORD command, HWND, BOOL&) {
        if (command == ID_WINDOW_CASCADE) ::SendMessageW(m_hWndMDIClient, WM_MDICASCADE, 0, 0);
        else if (command == ID_WINDOW_ARRANGE) ::SendMessageW(m_hWndMDIClient, WM_MDIICONARRANGE, 0, 0);
        else ::SendMessageW(m_hWndMDIClient, WM_MDITILE, command == ID_WINDOW_TILE_VERT ? MDITILE_VERTICAL : MDITILE_HORIZONTAL, 0);
        return 0;
    }
    LRESULT OnPluginNotify(UINT, WPARAM, LPARAM l, BOOL&) { plugins.ScintillaNotification(*reinterpret_cast<SCNotification*>(l)); return 0; }
    LRESULT OnPluginMessage(UINT msg, WPARAM w, LPARAM l, BOOL&) { return plugins.Message(msg, w, l); }
    LRESULT OnPluginAdmin(WORD, WORD id, HWND, BOOL&) {
        if (id == IDM_SETTING_PLUGINADM) pluginAdmin.Show(*this);
        else ShellExecuteW(m_hWnd, L"open", plugins.home.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    LRESULT OnPluginCommand(WORD, WORD id, HWND, BOOL&);
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDrop(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnChanged(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCloseTab(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTabContextMenu(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTabActivated(UINT, WPARAM, LPARAM child, BOOL&) { tabStrip.SyncSelection(reinterpret_cast<HWND>(child)); return 0; }
    LRESULT OnDockLayout(UINT, WPARAM, LPARAM, BOOL&) { UpdateLayout(); return 0; }
    LRESULT OnMenu(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDpi(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCommand(WORD, WORD, HWND, BOOL&);
    LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnActivateApp(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnFileChange(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnWatchEnable(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPower(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnQueryEndSession(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnEndSession(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTray(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClipboardUpdate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCopyData(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnMenuSelect(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDeferredCommand(UINT, WPARAM, LPARAM, BOOL&);
private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l);
};
