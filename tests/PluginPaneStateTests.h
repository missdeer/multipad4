#pragma once
#include "../plugins/DockPaneState.h"

inline void RunPluginPaneStateTests(MainFrame& frame, const std::filesystem::path& root) {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Plugin pane persistence failed at line " + std::to_string(where.line()));
    };
    const auto path = root / L"插件布局.ini";
    for (bool visible : {true, false}) for (int side : {-1, 0, 1, 2, 3}) {
        DockPlacement expected{{180, 160, 520, 620}, static_cast<PaneSide>(side), 237, 319, 0, 0, 0, visible};
        {
            FloatingMoveProbe original;
            require(original.Create(frame, expected.floating, L"Saved pane") != nullptr);
            require(original.Place(expected));
            expected = original.Placement();
            require(pluginLayout::Save(path, L"Test", original, visible));
        }
        FloatingMoveProbe restored;
        RECT rect{90, 90, 390, 490};
        require(restored.Create(frame, rect, L"Restored pane") != nullptr);
        require(restored.Place(DockPlacement{rect, PaneSide::Right}));
        bool restoredVisible = true;
        require(pluginLayout::Restore(path, L"Test", restored, frame, restoredVisible));
        require(restoredVisible == visible);
        require(((restored.GetStyle() & WS_VISIBLE) != 0) == visible);
        const auto actual = restored.Placement();
        require(EqualRect(&actual.floating, &expected.floating) != FALSE);
        require(actual.side == expected.side);
        if (!visible) restored.Show();
        require(restored.IsDocking() == (side >= 0));
        require(actual.width == expected.width && actual.height == expected.height);
        VerifyPaneLayoutMembership(frame, restored, side >= 0);
    }
    FloatingMoveProbe probe; RECT rect{180, 160, 520, 620};
    require(probe.Create(frame, rect, L"Invalid layout") != nullptr);
    bool visible = true;
    WritePrivateProfileStringW(L"Test", L"DockLayout", L"1 0 1 96 100 100 500 500 3 0 0 300 300 0", path.c_str());
    require(pluginLayout::Restore(path, L"Test", probe, frame, visible) && !visible && probe.Placement().side == PaneSide::Left);
    for (const auto* value : {L"invalid", L"1 0 1 96 100 100 500 500 9 0 0 300 300 0",
        L"1 1 0 96 100 100 -500 500 0 0 0 0 0 0"}) {
        WritePrivateProfileStringW(L"Test", L"DockLayout", value, path.c_str());
        require(!pluginLayout::Restore(path, L"Test", probe, frame, visible));
    }
}
