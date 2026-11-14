#pragma once
#include "../plugins/DockPaneState.h"

inline void RunPluginPaneStateTests(MainFrame& frame, const std::filesystem::path& root) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Plugin pane persistence failed at line " + std::to_string(where.line()));
    };
    const auto path = root / L"插件布局.ini";
    for (bool visible : {true, false}) for (int side : {-1, 0, 1, 2, 3}) {
        dockwins::DFDOCKPOSEX expected{};
        {
            FloatingMoveProbe original; original.owner = frame;
            RECT rect{180, 160, 520, 620};
            require(original.Create(frame, rect, L"Saved pane") != nullptr);
            original.ShowWindow(SW_SHOW);
            if (side >= 0) {
                dockwins::DFDOCKPOS pos{}; pos.hdr.hBar = frame;
                pos.dwDockSide = side; pos.nWidth = 237; pos.nHeight = 319;
                require(original.SetDockingPosition(&pos));
            }
            if (!visible) original.Hide();
            require(original.GetDockingWindowPlacement(&expected));
            require(pluginLayout::Save(path, L"Test", original, visible));
        }
        FloatingMoveProbe restored; restored.owner = frame;
        RECT rect{90, 90, 390, 490};
        require(restored.Create(frame, rect, L"Restored pane") != nullptr);
        // Plugins create their default dock position before applying saved state.
        restored.SetSide(DockSide::Right);
        bool restoredVisible = true;
        require(pluginLayout::Restore(path, L"Test", restored, frame, restoredVisible));
        require(restoredVisible == visible);
        require(((restored.GetStyle() & WS_VISIBLE) != 0) == visible);
        dockwins::DFDOCKPOSEX actual{};
        require(restored.GetDockingWindowPlacement(&actual));
        require(EqualRect(&actual.rect, &expected.rect) != FALSE);
        if (!visible) restored.Show();
        require(restored.IsDocking() == (side >= 0));
        if (side >= 0) {
            dockwins::DFDOCKPOS position{};
            require(restored.GetDockingPosition(&position));
            require(static_cast<int>(position.dwDockSide & 3) == side);
            // The long axis is assigned by the live layout (other panes may
            // share this bar); the user's splitter controls its thickness.
            const bool horizontal = dockwins::CDockingSide(side).IsHorizontal();
            require(horizontal ? position.nHeight == expected.dockPos.nHeight : position.nWidth == expected.dockPos.nWidth);
        }
        VerifyPaneLayoutMembership(frame, restored, side >= 0);
    }
    FloatingMoveProbe probe; RECT rect{180, 160, 520, 620};
    require(probe.Create(frame, rect, L"Invalid layout") != nullptr);
    bool visible = true;
    for (const auto* value : {L"invalid", L"1 0 1 96 100 100 500 500 9 0 0 300 300 0",
        L"1 1 0 96 100 100 -500 500 0 0 0 0 0 0"}) {
        WritePrivateProfileStringW(L"Test", L"DockLayout", value, path.c_str());
        require(!pluginLayout::Restore(path, L"Test", probe, frame, visible));
    }
}
