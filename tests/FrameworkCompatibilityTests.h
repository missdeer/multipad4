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

// Drag/drop uses DC_ACCEPT + DC_DOCK, whereas menu docking uses
// DC_SETDOCKPOSITION. Inserting a new bar between existing bars exercises a
// different owning-container path, and must not empty another frame's pointer.
inline void RunDragDockTests(MainFrame& frame) {
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
    for (int round = 0; round < 6; ++round) {
        frame.matepath.SetSide(DockSide::Floating);
        frame.UpdateLayout();
        RECT client{}; frame.GetClientRect(&client);
        std::vector<POINT> targets;
        dockwins::CDocker docker(frame);
        for (int y = 30; y < client.bottom; y += 20) {
            for (int x = 1; x < client.right; x += 10) {
                POINT cursor{x, y}; frame.ClientToScreen(&cursor);
                dockwins::DFDOCKRECT candidate{};
                candidate.hdr.hWnd = frame.matepath;
                candidate.rect = {cursor.x, cursor.y, cursor.x + 300, cursor.y + 400};
                docker.AdjustDragRect(&candidate);
                if (docker.AcceptDock(&candidate) && candidate.hdr.hBar == frame.m_hWnd)
                    targets.push_back(cursor);
            }
        }
        if (targets.empty()) throw std::runtime_error("No drag/drop target found for a new frame bar");
        auto target = targets[(targets.size() - 1) * round / 5];
        frame.matepath.ScreenToClient(&target);
        // Drive BeginMoving's real capture/message loop through release. The
        // coordinates are relative to the popup before its first live move;
        // button-up commits the last candidate rather than moving it again.
        frame.matepath.PostMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(target.x, target.y));
        frame.matepath.PostMessage(WM_LBUTTONUP, 0, MAKELPARAM(target.x, target.y));
        POINT grab{20, -10};
        if (!frame.matepath.BeginMoving(grab) || !frame.matepath.IsDocking())
            throw std::runtime_error("Drag/drop could not dock a new frame bar");
        frame.UpdateLayout();
    }
    frame.matepath.SetSide(DockSide::Right);
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
