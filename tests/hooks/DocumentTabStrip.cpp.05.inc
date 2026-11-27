        if (tabTesting::baseline) {
            const auto result = DefSubclassProc(window, message, w, l);
            if (HDC dc = GetDC(window)) { tabs.PaintTabCloses(dc); ReleaseDC(window, dc); }
            return result;
        }
