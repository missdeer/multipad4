#pragma once
// Application-facing boundary for the two original WTL extension frameworks.
// Includes use generated compatibility headers; upstream snapshots stay intact.
#include <atlcoll.h>
#include <atlgdi.h>
#include <atlgdix.h>
#include <CustomTabCtrl.h>
#include <DotNetTabCtrl.h>
#include <TabbedFrame.h>
#include <TabbedMDI.h>
#include <DockMisc.h>
#include <DockingWindow.h>
#include <ExtDockingWindow.h>
#include <DockingFrame.h>

class EditorTabCtrl : public CDotNetTabCtrlImpl<EditorTabCtrl, CTabViewTabItem> {
    using Base = CDotNetTabCtrlImpl<EditorTabCtrl, CTabViewTabItem>;
    std::wstring titleFace;
    int titlePoints = 0;
    int fixedTabWidth = 0;
public:
    static constexpr int titleMinPoints = 6, titleMaxPoints = 32;
    static constexpr int defaultTabWidth = 100, minTabWidth = 40, maxTabWidth = 1000;
    DECLARE_WND_CLASS_EX(L"MultiPad4.EditorTabs", CS_DBLCLKS, COLOR_WINDOW)
    BEGIN_MSG_MAP(EditorTabCtrl)
        MESSAGE_HANDLER(WM_SETTINGCHANGE, OnTitleSettingsChange)
        MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnTitleSettingsChange)
        MESSAGE_HANDLER(WM_THEMECHANGED, OnTitleSettingsChange)
        CHAIN_MSG_MAP(Base)
    END_MSG_MAP()

    bool HasTitleFont() const { return titlePoints != 0; }
    int TitleFontPoints() const { return titlePoints; }
    UINT TitleDpi() const { return ::GetDpiForWindow(::GetAncestor(m_hWnd, GA_ROOT)); }
    bool HasFixedTabWidth() const { return fixedTabWidth != 0; }
    void SetFixedTabWidth(int width) {
        if (!width && !fixedTabWidth) return;
        fixedTabWidth = width;
        UpdateLayout();
        EnsureVisible(GetCurSel(), false, true);
        Invalidate();
    }
    void UpdateLayout_ScrollToFit(RECT itemArea) {
        if (!fixedTabWidth) return Base::UpdateLayout_ScrollToFit(itemArea);
        RECT item{};
        GetClientRect(&item);
        const int width = MulDiv(fixedTabWidth, TitleDpi(), 96);
        item.left = m_settings.iIndent;
        for (size_t index = 0; index < m_Items.GetCount(); ++index) {
            item.right = item.left + width;
            m_Items[index]->SetRect(item);
            item.left = item.right;
        }
        m_iScrollOffset = (std::max)(m_iScrollOffset, static_cast<int>(itemArea.right - item.left) - m_settings.iIndent);
    }
    void SetTitleFont(const std::wstring& face, long points) {
        if (face.empty() || face.size() >= LF_FACESIZE || face.find(L'\0') != std::wstring::npos ||
            points < titleMinPoints || points > titleMaxPoints) points = 0;
        if (!points && !HasTitleFont()) return;
        titleFace = face;
        titlePoints = static_cast<int>(points);
        SendMessage(WM_SETTINGCHANGE);
    }
    int TitleFontHeight() const {
        WTL::CClientDC dc(m_hWnd);
        auto oldFont = dc.SelectFont(m_font);
        TEXTMETRICW normal{}, selected{};
        dc.GetTextMetrics(&normal);
        dc.SelectFont(m_fontSel);
        dc.GetTextMetrics(&selected);
        dc.SelectFont(oldFont);
        return (std::max)(normal.tmHeight, selected.tmHeight);
    }

    LRESULT OnTitleSettingsChange(UINT message, WPARAM wParam, LPARAM lParam, BOOL& handled) {
        const auto result = Base::OnSettingChange(message, wParam, lParam, handled);
        if (!HasTitleFont()) return result;
        LOGFONTW font{};
        font.lfHeight = -MulDiv(titlePoints, TitleDpi(), 72);
        font.lfWeight = FW_NORMAL;
        font.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(font.lfFaceName, titleFace.c_str());
        WTL::CFont normal, selected;
        if (!normal.CreateFontIndirect(&font)) return result;
        if (GetStyle() & CTCS_BOLDSELECTEDTAB) font.lfWeight = FW_BOLD;
        if (!selected.CreateFontIndirect(&font)) return result;
        m_font.DeleteObject();
        m_fontSel.DeleteObject();
        m_font.Attach(normal.Detach());
        m_fontSel.Attach(selected.Detach());
        m_nFontSizeTextTopOffset = 0;
        UpdateLayout();
        Invalidate();
        return result;
    }
};

class EditorTabOwner : public CMDITabOwnerImpl<EditorTabOwner, EditorTabCtrl> {
    using Base = CMDITabOwnerImpl<EditorTabOwner, EditorTabCtrl>;
public:
    void CalcTabAreaHeight() {
        auto& control = GetTabCtrl();
        if (!control.HasTitleFont()) return Base::CalcTabAreaHeight();
        const auto dpi = control.TitleDpi();
        SetTabAreaHeight((std::max)(MulDiv(24, dpi, 96), control.TitleFontHeight() + MulDiv(10, dpi, 96)));
    }
};
