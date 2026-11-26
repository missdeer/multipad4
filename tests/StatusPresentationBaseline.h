#pragma once
// Development-only copy of f9d684f UpdateStatus; counters do not change its rendering or publication decisions.
#include "../src/MainFrame.h"
#include "../src/UpstreamUI.h"
inline void BaselineStatusPresentation(MainFrame& frame, const wchar_t* formatOverride = nullptr) {
    auto m_hWndStatusBar = frame.m_hWndStatusBar; auto m_hWnd = frame.m_hWnd;
    auto statusVisible = frame.statusVisible;
    auto& statusText = frame.statusText; auto& statusWidth = frame.statusWidth;
    auto& statusDpi = frame.statusDpi; auto& statusFont = frame.statusFont;
    auto Active = [&] { return frame.Active(); };
    auto KillTimer = [&](UINT_PTR id) { return frame.KillTimer(id); };
    auto SetTimer = [&](UINT_PTR id, UINT delay) { return frame.SetTimer(id, delay); };
    auto GetClientRect = [&](RECT* rectangle) { return frame.GetClientRect(rectangle); };
    auto LoadStringW = [&](HINSTANCE module, UINT id, wchar_t* target, int capacity) {
        ++statusMeasurementCounters.formatLoads;
        if (!formatOverride) return ::LoadStringW(module, id, target, capacity);
        wcsncpy_s(target, static_cast<size_t>(capacity), formatOverride, _TRUNCATE);
        return static_cast<int>(wcslen(target));
    };
    auto StatusNumber = [](LRESULT value) {
        auto text = std::to_wstring(value); if (value < 1000) return text;
        wchar_t separator[4]{}; ++statusMeasurementCounters.localeQueries;
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator, _countof(separator));
        for (size_t position = text.size(); position > 3; position -= 3) text.insert(position - 3, 1, separator[0] ? separator[0] : L',');
        return text;
    };
    if (!m_hWndStatusBar || !statusVisible) { KillTimer(2); return; }
    // Stable upstream StatusItem order, including the flexible empty part.
    std::array<std::wstring, 13> text{};
    auto* d = Active();
    if (!d) KillTimer(2);
    if (d) {
        if (d->Send(SCI_GETMARGINWIDTHN, 0)) {
            ++statusMeasurementCounters.margins;
            const auto number = "__" + std::to_string(d->Send(SCI_GETLINECOUNT));
            d->Send(SCI_SETMARGINWIDTHN, 0, d->Send(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<LPARAM>(number.c_str())));
        }
        if (!d->FastView()) {
            auto pos = d->Send(SCI_GETCURRENTPOS), line = d->Send(SCI_LINEFROMPOSITION, pos);
            auto& stats = d->statusStatistics;
            bool pending = stats.Update(*d);
            if (pending) SetTimer(2, 30); else KillTimer(2);
            auto column = stats.Ready(0) ? StatusNumber(stats.Column(0) + 1) : L"...";
            auto columns = stats.Ready(1) ? StatusNumber(stats.Column(1)) : L"...";
            auto character = stats.Ready(0) ? StatusNumber(stats.Characters(0) + 1) : L"...";
            auto characters = stats.Ready(1) ? StatusNumber(stats.Characters(1)) : L"...";
            text[0] = L"Ln " + std::to_wstring(line + 1) + L" / " + std::to_wstring(d->Send(SCI_GETLINECOUNT));
            text[1] = L"Col " + column + L" / " + columns;
            text[2] = L"Ch " + character + L" / " + characters;
            const bool rectangular = d->Send(SCI_SELECTIONISRECTANGLE) != 0;
            const std::wstring selectionCharacters = rectangular ? L"--" : !stats.SelectionReady() ? L"..." : StatusNumber(stats.SelectedCharacters());
            const std::wstring selectionBytes = rectangular ? L"--" : !stats.SelectionReady() ? L"..." : StatusNumber(stats.selectedBytes);
            text[3] = L"Sel " + selectionCharacters + L" / " + selectionBytes;
            text[4] = L"SelLn " + (stats.SelectionReady() ? std::to_wstring(stats.selectedLines) : L"...");
            text[5] = L"Fnd " + std::to_wstring(d->style.markOccurrences ? d->style.occurrences : d->search.matches)
                + (d->style.markOccurrences && d->style.occurrencePending ? L" ..." : L"");
            // Reuse the upstream localized six-field format instead of hard-coding
            // English prefixes when the rest of the interface uses another locale.
            wchar_t format[512]{}, formatted[1024]{};
            std::array<std::wstring, 10> values{
                StatusNumber(line + 1), StatusNumber(d->Send(SCI_GETLINECOUNT)),
                column, columns, character, characters,
                selectionCharacters, selectionBytes, stats.SelectionReady() ? StatusNumber(stats.selectedLines) : L"...",
                StatusNumber(d->style.markOccurrences ? d->style.occurrences : d->search.matches) + (d->style.markOccurrences && d->style.occurrencePending ? L" ..." : L"")};
            if (LoadStringW(GetModuleHandleW(nullptr), IDS_STATUSITEM_FORMAT, format, _countof(format)) &&
                _snwprintf_s(formatted, _countof(formatted), _TRUNCATE, format, values[0].c_str(), values[1].c_str(), values[2].c_str(), values[3].c_str(),
                    values[4].c_str(), values[5].c_str(), values[6].c_str(), values[7].c_str(), values[8].c_str(), values[9].c_str()) >= 0) {
                const wchar_t* start = formatted;
                for (size_t field = 0; field < 6; ++field) {
                    auto end = wcschr(start, L'\n');
                    text[field] = end ? std::wstring(start, end) : std::wstring(start);
                    if (!end) break; start = end + 1;
                }
            }
        } else KillTimer(2);
        text[7] = d->LargeFile() ? L"Large file (AutoSave off)" : d->language; text[8] = files::EncodingName(d->encoding);
        auto eol = d->Send(SCI_GETEOLMODE); text[9] = eol == SC_EOL_CRLF ? L"CR+LF" : eol == SC_EOL_LF ? L"LF" : L"CR";
        text[10] = d->Send(SCI_GETOVERTYPE) ? L"OVR" : L"INS";
        text[11] = std::to_wstring(d->Send(SCI_GETZOOM)) + L"%";
        wchar_t size[64]{};
        StrFormatByteSizeW(d->Send(SCI_GETLENGTH), size, _countof(size));
        text[12] = size;
        if (d->FastView()) {
            auto& view = *d->fastView;
            for (size_t i = 0; i < 6; ++i) text[i].clear();
            text[0] = L"Bytes " + std::to_wstring(view.Begin()) + L"–" + std::to_wstring(view.End());
            text[7] = L"Mapped document"; text[10] = d->fastView->ReadOnly() ? L"Read only" : L"Editable";
            text[12] = std::to_wstring(view.Size() / (1024 * 1024)) + L" MiB";
        }
    }
    RECT client{}; GetClientRect(&client);
    auto currentDpi = GetDpiForWindow(m_hWnd);
    auto currentFont = reinterpret_cast<HFONT>(::SendMessageW(m_hWndStatusBar, WM_GETFONT, 0, 0));
    if (text == statusText && client.right == statusWidth && currentDpi == statusDpi && currentFont == statusFont) return;
    statusText = text; statusWidth = client.right; statusDpi = currentDpi; statusFont = currentFont;
    std::array<int, 13> widths{}, edges{};
    HDC dc = ::GetDC(m_hWndStatusBar);
    HFONT font = reinterpret_cast<HFONT>(::SendMessageW(m_hWndStatusBar, WM_GETFONT, 0, 0));
    auto oldFont = font ? SelectObject(dc, font) : nullptr;
    int total = 0, padding = MulDiv(16, GetDpiForWindow(m_hWnd), 96);
    for (size_t i = 0; i < text.size(); ++i) {
        ++statusMeasurementCounters.extents; SIZE size{}; GetTextExtentPoint32W(dc, text[i].c_str(), static_cast<int>(text[i].size()), &size);
        widths[i] = size.cx + padding; if (i != 6) total += widths[i];
    }
    if (oldFont) SelectObject(dc, oldFont); ::ReleaseDC(m_hWndStatusBar, dc);
    widths[6] = max(0L, client.right - total - padding);
    for (size_t i = 0; i < edges.size(); ++i) edges[i] = widths[i] + (i ? edges[i - 1] : 0);
    edges.back() = -1;
    ::SendMessageW(m_hWndStatusBar, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
    for (size_t i = 0; i < text.size(); ++i)
        ::SendMessageW(m_hWndStatusBar, SB_SETTEXTW, i, reinterpret_cast<LPARAM>(text[i].c_str()));
}
