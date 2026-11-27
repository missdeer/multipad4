    if (tabTesting::baseline) {
        const int count = TabCtrl_GetItemCount(control); const RECT viewport = TabViewport();
        for (int index = 0; index < count; ++index) {
            const RECT button = TabCloseRect(index, viewport);
            if (IsRectEmpty(&button) || !RectVisible(dc, &button)) continue;
            TCITEMW item{}; item.mask = TCIF_PARAM; TabCtrl_GetItem(control, index, &item);
            const HWND child = reinterpret_cast<HWND>(item.lParam);
            const bool hot = hotClose == child && dragIndex < 0 && (!pressedClose || pressedClose == child);
            if (tabTesting::measure) ++tabTesting::draws;
            PaintTabButton(control, dc, button, 0, hot, true, true);
        }
        return;
    }
