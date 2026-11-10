#pragma once
#include "App.h"
enum class DockSide { Left, Right, Bottom, Floating };
class DockPane : public dockwins::CTitleDockingWindowImpl<DockPane, CWindow, dockwins::COutlookLikeTitleDockingWindowTraits> {
public:
    using Base = dockwins::CTitleDockingWindowImpl<DockPane, CWindow, dockwins::COutlookLikeTitleDockingWindowTraits>;
    DECLARE_WND_CLASS_EX(L"MultiPad4.FrameworkDockPane", CS_DBLCLKS, COLOR_BTNFACE)
    HWND owner = nullptr;
    CListBox list;
    bool floating = false;
    BEGIN_MSG_MAP(DockPane)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_CONTEXTMENU, OnContext)
        COMMAND_CODE_HANDLER(LBN_DBLCLK, OnSelect)
        CHAIN_MSG_MAP(Base)
    END_MSG_MAP()
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnContext(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSelect(WORD, WORD, HWND, BOOL&);
};
// Thin adapter only: docking, floating, dragging and splitters belong to Klimov's library.
class DockHost {
public:
    HWND owner = nullptr;
    DockPane pane;
    DockSide side = DockSide::Left;
    bool visible = true;
    int extent = 220;
    void Create(HWND frame);
    void Layout() { pane.floating = !pane.IsDocking(); }
    void SetSide(DockSide value);
    void Toggle();
};
