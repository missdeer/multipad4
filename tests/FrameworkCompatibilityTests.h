#pragma once
#include "../src/Frameworks.h"
#include <stdexcept>
#include "../src/DockHost.h"
#include "QAArtifacts.h"

// Exercise the actual framework tracker against a popup HWND. Screen-to-client
// conversion on every move models captured mouse messages as its origin moves.
class FloatingMoveProbe : public DockPane {
public:
    ~FloatingMoveProbe() { if (IsWindow()) { if (IsDocking()) Undock(); DestroyWindow(); } }
    void SetSide(DockSide side) {
        if (side == DockSide::Floating) {
            RECT rect{180, 160, 520, 620};
            Float(&rect);
        } else {
            dockwins::DFDOCKPOS pos{};
            pos.hdr.hWnd = m_hWnd; pos.hdr.hBar = owner;
            pos.dwDockSide = dockwins::CDockingSide::sRight;
            pos.nWidth = 220; pos.nHeight = 400;
            SetDockingPosition(&pos);
        }
    }
    void Verify(HWND frame) {
        owner = frame;
        RECT position{140, 140, 460, 540};
        if (!Create(frame, position, L"Floating drag regression"))
            throw std::runtime_error("Cannot create floating drag probe");
        if (GetStyle() & WS_CHILD) throw std::runtime_error("Drag probe must float");
        // Isolate free movement from screen-dependent docking targets. Normal
        // frame integration separately verifies all four docking transitions.
        m_docker = dockwins::CDocker(nullptr);
        RECT original{}; GetWindowRect(&original);
        auto track = [&](bool cancel) {
            dockwins::DFDOCKRECT header{};
            header.hdr.hWnd = m_hWnd; header.rect = original;
            POINT grab{20, -10}, screenGrab = grab; ClientToScreen(&screenGrab);
            CGhostMoveTracker tracker(m_docker, grab, header);
            tracker.BeginDrag();
            for (auto delta : {POINT{37, 23}, POINT{83, 51}, POINT{-41, -27}}) {
                POINT client{screenGrab.x + delta.x, screenGrab.y + delta.y};
                ScreenToClient(&client);
                tracker.OnMove(client.x, client.y);
                RECT actual{}; GetWindowRect(&actual);
                RECT expected{original.left + delta.x, original.top + delta.y,
                    original.right + delta.x, original.bottom + delta.y};
                if (!EqualRect(&actual, &expected))
                    throw std::runtime_error("Floating pane did not follow the drag before release");
            }
            CaptureQAWindow(m_hWnd, cancel ? L"Dock-drag-before-cancel" : L"Dock-drag-before-release");
            tracker.EndDrag(cancel);
        };
        track(true);
        RECT restored{}; GetWindowRect(&restored);
        if (!EqualRect(&restored, &original)) throw std::runtime_error("Canceled floating drag did not restore position");
        track(false);
        GetWindowRect(&restored);
        if (restored.left != original.left - 41 || restored.top != original.top - 27)
            throw std::runtime_error("Released floating drag lost its position");
    }
};

inline void RunFloatingMoveTests(HWND frame) {
    FloatingMoveProbe probe;
    probe.Verify(frame);
}

// Query the framework's layout records independently of the HWND hierarchy.
// A floating pane must not be present in any package, even if an old package
// still has visible pixels or is waiting for its deferred close.
inline void VerifyPaneLayoutMembership(MainFrame& frame, HWND pane, bool docked) {
    if (((::GetWindowLongPtrW(pane, GWL_STYLE) & WS_CHILD) != 0) != docked)
        throw std::runtime_error("Pane docking ownership and native WS_CHILD disagree");
    int memberships = 0;
    for (HWND bar = ::GetWindow(frame, GW_CHILD); bar; bar = ::GetWindow(bar, GW_HWNDNEXT)) {
        wchar_t name[96]{}; GetClassNameW(bar, name, _countof(name));
        if (wcscmp(name, L"CPackageWindowFrame::CPackageWindow") != 0) continue;
        dockwins::DFDOCKPOS query{};
        query.hdr.hWnd = pane; query.hdr.hBar = bar; query.hdr.code = DC_GETDOCKPOSITION;
        if (::SendMessageW(bar, WMDF_DOCK, 0, reinterpret_cast<LPARAM>(&query))) {
            ++memberships;
            if (!docked || ::GetParent(pane) != bar)
                throw std::runtime_error("Dock layout still owns a floating or reparented HWND");
        }
    }
    if (memberships != (docked ? 1 : 0)) throw std::runtime_error("Pane HWND has inconsistent dock layout membership");
}

// Repeated docking messages must not leave two layout records for one HWND.
// A later float must remove the pane from the container completely.
inline void RunRepeatedDockRegistrationTests(MainFrame& frame) {
    FloatingMoveProbe probe;
    probe.owner = frame;
    RECT floating{180, 160, 520, 620};
    if (!probe.Create(frame, floating, L"Repeated dock registration"))
        throw std::runtime_error("Cannot create repeated docking probe");
    dockwins::DFDOCKPOS pos{};
    pos.hdr.hWnd = probe; pos.hdr.hBar = frame;
    pos.dwDockSide = dockwins::CDockingSide::sRight | dockwins::CDockingSide::sSingle;
    pos.nWidth = 220; pos.nHeight = 400;
    if (!probe.SetDockingPosition(&pos)) throw std::runtime_error("Cannot dock repeated registration probe");
    HWND bar = ::GetParent(probe);
    for (int repeat = 0; repeat < 3; ++repeat) {
        dockwins::DFDOCKRECT header{};
        header.hdr.hWnd = probe; header.hdr.hBar = bar;
        probe.GetWindowRect(&header.rect);
        if (!probe.DockMe(&header)) throw std::runtime_error("Repeated docking request failed");
    }
    if (!probe.Float(&floating)) throw std::runtime_error("Cannot float repeated registration probe");
    VerifyPaneLayoutMembership(frame, probe, false);
    if (::IsWindowVisible(bar))
        throw std::runtime_error("Floating HWND remains in the old dock layout after repeated registration");
    MSG message{};
    while (PeekMessageW(&message, bar, WM_CLOSE, WM_CLOSE, PM_REMOVE)) DispatchMessageW(&message);
    if (::IsWindow(bar)) throw std::runtime_error("Repeated registration left an orphan docking container");
}

// Drag/drop uses DC_ACCEPT + DC_DOCK, whereas menu docking uses
// DC_SETDOCKPOSITION. Inserting a new bar between existing bars exercises a
// different owning-container path, and must not empty another frame's pointer.
inline void RunDragDockTests(MainFrame& frame) {
    RunRepeatedDockRegistrationTests(frame);
    // Keep bars on both sides so new drop targets include middle insertions.
    FloatingMoveProbe extra;
    extra.owner = frame;
    RECT initial{200, 200, 420, 600};
    if (!extra.Create(frame, initial, L"Additional docking bar"))
        throw std::runtime_error("Cannot create additional docking bar");
    dockwins::DFDOCKPOS position{};
    position.hdr.hWnd = extra; position.hdr.hBar = frame;
    position.dwDockSide = dockwins::CDockingSide::sRight;
    position.nWidth = 160; position.nHeight = 200;
    if (!extra.SetDockingPosition(&position)) throw std::runtime_error("Cannot dock additional bar");
    FloatingMoveProbe browser;
    browser.owner = frame;
    if (!browser.Create(frame, initial, L"Drag docking probe")) throw std::runtime_error("Cannot create docking probe");
    browser.SetSide(DockSide::Right);
    for (int round = 0; round < 6; ++round) {
        browser.SetSide(DockSide::Floating);
        VerifyPaneLayoutMembership(frame, browser, false);
        frame.UpdateLayout();
        RECT client{}; frame.GetClientRect(&client);
        std::vector<POINT> targets;
        dockwins::CDocker docker(frame);
        for (int y = 30; y < client.bottom; y += 20) {
            for (int x = 1; x < client.right; x += 10) {
                POINT cursor{x, y}; frame.ClientToScreen(&cursor);
                dockwins::DFDOCKRECT candidate{};
                candidate.hdr.hWnd = browser;
                candidate.rect = {cursor.x, cursor.y, cursor.x + 300, cursor.y + 400};
                docker.AdjustDragRect(&candidate);
                if (docker.AcceptDock(&candidate) && candidate.hdr.hBar == frame.m_hWnd)
                    targets.push_back(cursor);
            }
        }
        if (targets.empty()) throw std::runtime_error("No drag/drop target found for a new frame bar");
        auto target = targets[(targets.size() - 1) * round / 5];
        browser.ScreenToClient(&target);
        // Drive BeginMoving's real capture/message loop through release. The
        // coordinates are relative to the popup before its first live move;
        // button-up commits the last candidate rather than moving it again.
        browser.PostMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(target.x, target.y));
        browser.PostMessage(WM_LBUTTONUP, 0, MAKELPARAM(target.x, target.y));
        POINT grab{20, -10};
        if (!browser.BeginMoving(grab) || !browser.IsDocking())
            throw std::runtime_error("Drag/drop could not dock a new frame bar");
        VerifyPaneLayoutMembership(frame, browser, true);
        frame.UpdateLayout();
    }
    browser.SetSide(DockSide::Right);
}

// The WTL API returns a borrowed wrapper around a newly allocated brush.
// Repeated splitter previews must release it after restoring the DC's brush.
inline void RunFrameworkCompatibilityTests() {
    struct OwnedFrame : dockwins::IFrame {
        int* destroyed;
        explicit OwnedFrame(int& count) : destroyed(&count) {}
        ~OwnedFrame() override { ++*destroyed; }
        HWND hwnd() const override { return nullptr; }
        bool AcceptDock(dockwins::DFDOCKRECT*) const override { return false; }
        distance MinDistance() const override { return 1; }
        void GetMinMaxInfo(LPMINMAXINFO) const override {}
    };
    int destroyed = 0;
    {
        using Frame = dockwins::CPtrFrame<dockwins::IFrame>;
        std::deque<Frame> frames;
        for (int i = 0; i < 6; ++i) frames.push_back(Frame(i * 100, new OwnedFrame(destroyed)));
        for (int index : {1, 4, 2, 6}) {
            Frame inserted(50, new OwnedFrame(destroyed));
            frames.insert(frames.begin() + index, inserted);
            for (const auto& frame : frames)
                if (!frame.operator->()) throw std::runtime_error("Docking deque insertion emptied a frame pointer");
        }
        if (destroyed) throw std::runtime_error("Docking deque insertion destroyed a live frame");
    }
    if (destroyed != 10) throw std::runtime_error("Docking frames were not released exactly once");
    WTL::CDC dc;
    if (!dc.CreateCompatibleDC(nullptr)) throw std::runtime_error("Cannot create framework test DC");
    WTL::CBitmap bitmap;
    if (!bitmap.CreateBitmap(16, 16, 1, 1, nullptr)) throw std::runtime_error("Cannot create framework test bitmap");
    const auto previous = dc.SelectBitmap(bitmap);
    dockwins::CSimpleSplitterBar<> splitter(true);
    splitter.SetRect(0, 0, 16, 16);
    splitter.DrawGhostBar(dc); splitter.CleanGhostBar(dc);
    const auto before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 64; ++i) {
        splitter.DrawGhostBar(dc); splitter.CleanGhostBar(dc);
    }
    const auto after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    dc.SelectBitmap(previous);
    if (after > before + 2) throw std::runtime_error("Splitter preview leaked GDI objects");
}
