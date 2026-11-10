#pragma once
#include "EditorDocument.h"
class MainFrame;
class SearchDialog {
  public:
    HWND hwnd = nullptr;
    MainFrame *owner = nullptr;
    EditorDocument *document = nullptr;
    bool replacing = false;
    HFONT monoFont = nullptr;
    HACCEL accelerators = nullptr;
    BOOL PreTranslateMessage(MSG *message);
    RECT savedPosition{};
    bool hasSavedPosition = false;
    void ApplyFont();
    int layoutWidth = 0, layoutHeight = 0;
    struct ControlLayout {
        HWND control;
        RECT rectangle;
        bool right;
        bool stretch;
    };
    std::vector<ControlLayout> controls;
    void Open(MainFrame &frame, EditorDocument &target, bool replace);
    void Close();
    bool ReadFields();
    static INT_PTR CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l);
};
