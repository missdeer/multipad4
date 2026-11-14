#pragma once
#include "App.h"
#include "resource.h"
#include "EditorDocument.h"
#include "EditorCommands.h"
#include "SearchDialog.h"
#include "SearchResultsPane.h"
#include "AppSettings.h"
#include <optional>
#include "PluginHost.h"
#include "PluginAdmin.h"
class MainFrame;
enum class SaveMode { Normal, As, Copy };
using MainFrameBase = dockwins::CDockingFrameImplBase<MainFrame, CTabbedMDIFrameWindowImpl<MainFrame>>;
class MainFrame : public MainFrameBase, public CMessageFilter {
public:
    DECLARE_FRAME_WND_CLASS(L"MultiPad4.MainFrame", IDR_MAINFRAME)
    PluginHost plugins;
    PluginAdmin pluginAdmin;
    EditorTabs tabs;
    std::vector<std::unique_ptr<EditorDocument>> documents;
    unsigned untitledCount = 0;
    SearchDialog searchDialog;
    SearchResultsPane searchResults;
    AppSettings settings;
    DocumentStyleState defaultStyle;
    FrameRuntime runtime;
    HMENU windowMenu = nullptr;
    HIMAGELIST toolbarImages = nullptr;
    HIMAGELIST toolbarDisabledImages = nullptr;
    HACCEL mdiAccelerators = nullptr;
    bool toolbarVisible = true, statusVisible = true;
    BOOL PreTranslateMessage(MSG* msg) override;
    EditorDocument* Active();
    EditorDocument* NewDocument();
    bool Open(const std::filesystem::path& path, std::optional<files::Encoding> forcedEncoding = std::nullopt);
    bool Save(EditorDocument& document, SaveMode mode = SaveMode::Normal, bool keepBackup = false);
    bool SaveTo(EditorDocument& document, const std::filesystem::path& path, bool copy = false, bool keepBackup = false);
    bool Reload(EditorDocument& document);
    bool Close(int index);
    bool ConfirmClose(EditorDocument& document, bool keepBackup = true);
    bool Relaunch(bool elevated = false, bool empty = false, bool closeCurrent = true);
    void Refresh();
    void FindNext(bool backwards = false);
    bool ConfirmSearchWrap(bool backwards);
    void ShowSearch(bool replace);
    void CreateUpstreamUI();
    void UpdateCommandUI();
    void UpdateStatus();
    CommandState State(UINT id);
    CommandState QueryState(UINT id);
    void Error(const std::exception& error);
    BEGIN_MSG_MAP(MainFrame)
        MESSAGE_RANGE_HANDLER(NPPMSG, NPPMSG + 999, OnPluginMessage)
        MESSAGE_RANGE_HANDLER(RUNCOMMAND_USER, RUNCOMMAND_USER + 100, OnPluginMessage)
        COMMAND_ID_HANDLER(IDM_SETTING_PLUGINADM, OnPluginAdmin)
        COMMAND_ID_HANDLER(IDM_SETTING_OPENPLUGINSDIR, OnPluginAdmin)
        COMMAND_RANGE_HANDLER(46000, 55999, OnPluginCommand)
        MESSAGE_HANDLER(WM_APP + 0x120, OnPluginNotify)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_DROPFILES, OnDrop)
        MESSAGE_HANDLER(WM_DOCUMENT_CHANGED, OnChanged)
        MESSAGE_HANDLER(WM_CLOSE_TAB, OnCloseTab)
        MESSAGE_HANDLER(WM_INITMENUPOPUP, OnMenu)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpi)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_TIMER, OnTimer)
        MESSAGE_HANDLER(WM_ACTIVATEAPP, OnActivateApp)
        MESSAGE_HANDLER(WM_POWERBROADCAST, OnPower)
        MESSAGE_HANDLER(WM_QUERYENDSESSION, OnQueryEndSession)
        MESSAGE_HANDLER(WM_ENDSESSION, OnEndSession)
        MESSAGE_HANDLER(WM_APP + 0x103, OnTray)
        MESSAGE_HANDLER(WM_APP + 0x104, OnDeferredCommand)
        MESSAGE_HANDLER(WM_CLIPBOARDUPDATE, OnClipboardUpdate)
        MESSAGE_HANDLER(WM_COPYDATA, OnCopyData)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
        MESSAGE_HANDLER(WM_MENUSELECT, OnMenuSelect)
        COMMAND_ID_HANDLER(ID_WINDOW_CASCADE, OnArrangeMDI)
        COMMAND_ID_HANDLER(ID_WINDOW_TILE_HORZ, OnArrangeMDI)
        COMMAND_ID_HANDLER(ID_WINDOW_TILE_VERT, OnArrangeMDI)
        COMMAND_ID_HANDLER(ID_WINDOW_ARRANGE, OnArrangeMDI)
        COMMAND_RANGE_HANDLER(40000, 45999, OnCommand)
        CHAIN_MSG_MAP(MainFrameBase)
    END_MSG_MAP()
    LRESULT OnArrangeMDI(WORD, WORD command, HWND, BOOL&) {
        if (command == ID_WINDOW_CASCADE) MDICascade();
        else if (command == ID_WINDOW_ARRANGE) MDIIconArrange();
        else MDITile(command == ID_WINDOW_TILE_VERT ? MDITILE_VERTICAL : MDITILE_HORIZONTAL);
        return 0;
    }
    LRESULT OnPluginNotify(UINT, WPARAM, LPARAM l, BOOL&) { plugins.ScintillaNotification(*reinterpret_cast<SCNotification*>(l)); return 0; }
    LRESULT OnPluginMessage(UINT msg, WPARAM w, LPARAM l, BOOL&) { return plugins.Message(msg, w, l); }
    LRESULT OnPluginAdmin(WORD, WORD id, HWND, BOOL&) {
        if (id == IDM_SETTING_PLUGINADM) pluginAdmin.Show(*this);
        else ShellExecuteW(m_hWnd, L"open", plugins.home.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }
    LRESULT OnPluginCommand(WORD, WORD id, HWND, BOOL&) { plugins.Execute(id); return 0; }
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDrop(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnChanged(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCloseTab(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnMenu(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDpi(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCommand(WORD, WORD, HWND, BOOL&);
    LRESULT OnTimer(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnActivateApp(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPower(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnQueryEndSession(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnEndSession(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTray(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClipboardUpdate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCopyData(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContextMenu(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnMenuSelect(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDeferredCommand(UINT, WPARAM, LPARAM, BOOL&);
};
