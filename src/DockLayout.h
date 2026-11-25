#pragma once
#include <windows.h>
#include <vector>

enum class PaneSide : int { Floating = -1, Left, Top, Right, Bottom };
struct DockPlacement {
    RECT floating{100, 100, 500, 600};
    PaneSide side = PaneSide::Floating;
    int width = 300;
    int height = 300;
    unsigned bar = 0;
    float position = 0;
    unsigned index = 0;
    bool visible = false;
};

constexpr UINT WM_DOCK_LAYOUT = WM_APP + 0x142;
constexpr auto dockPlacementProperty = L"MultiPad4.DockPlacement";
bool IsHorizontal(PaneSide side);
void LayoutDockPanes(HWND frame, RECT& area);

class PaneInteraction {
public:
    void Attach(HWND pane, HWND frame);
    bool Apply(const DockPlacement& placement);
    DockPlacement Placement() const;
    HWND Window() const { return pane; }
    bool Dispatch(UINT message, WPARAM w, LPARAM l, LRESULT& result);

private:
    void StartDrag(POINT screen, bool resize);
    void MoveDrag(POINT screen);
    void EndDrag(bool cancel);
    void DockAt(POINT screen);
    void PaintCaption(HDC dc);
    enum CaptionHit { Splitter = 0x100, Close = 0x101 };
    HWND pane = nullptr, frame = nullptr;
    DockPlacement placement;
    DockPlacement beforeDrag;
    struct BarMember { HWND window; int width, height; };
    std::vector<BarMember> resizedBar;
    RECT initialRect{};
    POINT grab{}, last{};
    bool moving = false, resizing = false;
    bool closeHovered = false;
};
