#pragma once
#include "DockHost.h"
#include <filesystem>

constexpr UINT WM_MATEPATH_ACTION = WM_APP + 0x110;
constexpr UINT WM_MATEPATH_OPEN = WM_APP + 0x111;
class MatepathPane : public dockwins::CTitleDockingWindowImpl<MatepathPane, CWindow, dockwins::COutlookLikeTitleDockingWindowTraits> {
public:
    using Base = dockwins::CTitleDockingWindowImpl<MatepathPane, CWindow, dockwins::COutlookLikeTitleDockingWindowTraits>;
    DECLARE_WND_CLASS_EX(L"MultiPad4.MatepathPane", CS_DBLCLKS, COLOR_BTNFACE)
    enum { Back = 100, Forward, Up, Reload, Go, Filter, Hidden, Properties, SortName, SortSize, SortType, SortDate, Reverse, Follow, Address, Pattern };
    HWND owner = nullptr;
    CListViewCtrl list;
    CEdit address, pattern;
    CButton back, forward, up, reload, go, apply;
    std::filesystem::path directory;
    std::wstring fileSpec = L"*.*";
    bool visible = true, showHidden = false, reverse = false;
    int sort = 0;
    DockSide side = DockSide::Right;
    DockSide CurrentSide() const;
    std::vector<std::filesystem::path> history;
    size_t historyIndex = 0;
    void CreatePane(HWND frame);
    void DestroyPane();
    void SetSide(DockSide value);
    void Toggle();
    void Layout();
    bool Navigate(const std::filesystem::path& path, bool remember = true);
    void RefreshItems();
    void ActivateSelection();
    void Reveal(const std::filesystem::path& path);
    BOOL PreTranslateMessage(MSG* msg);
    BEGIN_MSG_MAP(MatepathPane)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContext)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpi)
        MESSAGE_HANDLER(WM_DPICHANGED_AFTERPARENT, OnDpi)
        COMMAND_RANGE_HANDLER(Back, Follow, OnCommand)
        CHAIN_MSG_MAP(Base)
    END_MSG_MAP()
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNotify(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContext(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDpi(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnCommand(WORD, WORD, HWND, BOOL&);
};
