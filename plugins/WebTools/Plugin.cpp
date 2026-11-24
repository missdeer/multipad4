#include "EditCodeFormat.h"
#include "EditExpression.h"
#include "EditSelection.h"
#include "PluginInterface.h"
#include <algorithm>
#include <cstring>
#include <shlwapi.h>
#include <stdexcept>
#include <string_view>

namespace {
HINSTANCE instance = nullptr;
NppData host{};

LRESULT Sci(UINT message, WPARAM w = 0, LPARAM l = 0) {
    return SendMessageW(host._scintillaMainHandle, message, w, l);
}

bool GetFormatStyles(MultiPadCodeFormatStyles& styles) {
    return SendMessageW(host._nppHandle, MPM_GETCODEFORMATSTYLES, 0, reinterpret_cast<LPARAM>(&styles)) != 0;
}

template<bool Pretty>
void Format() {
    MultiPadCodeFormatStyles styles{};
    if (GetFormatStyles(styles))
        FormatDocumentCode(host._scintillaMainHandle, styles, Pretty);
}

void Replace(LRESULT start, LRESULT end, const std::string& output) {
    Sci(SCI_BEGINUNDOACTION);
    Sci(SCI_SETTARGETRANGE, start, end);
    Sci(SCI_REPLACETARGET, output.size(), reinterpret_cast<LPARAM>(output.data()));
    Sci(SCI_SETSEL, start, start + output.size());
    Sci(SCI_ENDUNDOACTION);
}

std::string ReadSelection(LRESULT start, LRESULT end) {
    std::string text(end - start + 1, '\0');
    Sci_TextRangeFull range{{start, end}, text.data()};
    Sci(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range));
    text.resize(end - start);
    return text;
}

template<UINT Command>
void ConvertUrl() {
    if (RejectRectangularEdit(host._scintillaMainHandle))
        return;
    const auto start = Sci(SCI_GETSELECTIONSTART), end = Sci(SCI_GETSELECTIONEND);
    auto wide = files::FromUtf8(ReadSelection(start, end));
    if constexpr (Command == IDM_EDIT_URLDECODE) {
        DWORD capacity = static_cast<DWORD>(wide.size() + 1);
        std::wstring unescaped(capacity, L'\0');
        if (FAILED(UrlUnescapeW(wide.data(), unescaped.data(), &capacity, URL_UNESCAPE_AS_UTF8)))
            throw std::runtime_error("Cannot URL-decode the selected text.");
        unescaped.resize(capacity);
        Replace(start, end, files::ToUtf8(unescaped));
    } else {
        constexpr auto whitespace = L" \a\b\f\n\r\t\v";
        const auto first = wide.find_first_not_of(whitespace);
        if (first == std::wstring::npos)
            return;
        const auto last = wide.find_last_not_of(whitespace);
        wide = wide.substr(first, last - first + 1);
        DWORD capacity = static_cast<DWORD>(std::min<size_t>(MAXDWORD, wide.size() * 12 + 16));
        std::wstring escaped(capacity, L'\0');
        constexpr DWORD flags = URL_ESCAPE_AS_UTF8 |
            (Command == IDM_EDIT_URLCOMPONENTENCODE ? URL_ESCAPE_ASCII_URI_COMPONENT | URL_ESCAPE_SEGMENT_ONLY : 0);
        if (FAILED(UrlEscapeW(wide.c_str(), escaped.data(), &capacity, flags)))
            throw std::runtime_error("Cannot URL-encode the selected text.");
        escaped.resize(capacity);
        Replace(start, end, files::ToUtf8(escaped));
    }
}

template<bool Escape>
char* MapEntities(const char* input, size_t& length, UINT codePage) noexcept {
    try {
        const int count = MultiByteToWideChar(codePage, 0, input, static_cast<int>(length), nullptr, 0);
        if (!count)
            return nullptr;
        std::wstring text(count, L'\0');
        MultiByteToWideChar(codePage, 0, input, static_cast<int>(length), text.data(), count);
        constexpr std::pair<wchar_t, std::wstring_view> entities[]{
            {L'&', L"&amp;"}, {L'<', L"&lt;"}, {L'>', L"&gt;"}, {L'"', L"&quot;"}, {L'\'', L"&apos;"},
        };
        std::wstring output;
        for (size_t index = 0; index < text.size();) {
            const auto match = std::find_if(std::begin(entities), std::end(entities), [&](const auto& entity) {
                if constexpr (Escape)
                    return text[index] == entity.first;
                return text.compare(index, entity.second.size(), entity.second) == 0;
            });
            if (match == std::end(entities)) {
                output += text[index++];
            } else if constexpr (Escape) {
                output += match->second;
                ++index;
            } else {
                output += match->first;
                index += match->second.size();
            }
        }
        if (output == text)
            return nullptr;
        const int size = WideCharToMultiByte(codePage, 0, output.data(), static_cast<int>(output.size()),
                                             nullptr, 0, nullptr, nullptr);
        if (!size)
            return nullptr;
        auto* result = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, size));
        if (!result)
            return nullptr;
        length = WideCharToMultiByte(codePage, 0, output.data(), static_cast<int>(output.size()),
                                    result, size, nullptr, nullptr);
        return result;
    } catch (...) {
        return nullptr;
    }
}

template<bool Escape>
void ConvertEntities() {
    SendMessageW(host._nppHandle, MPM_MAPSELECTION, 0, reinterpret_cast<LPARAM>(&MapEntities<Escape>));
}

void EvaluateJavaScript() {
    if (RejectRectangularEdit(host._scintillaMainHandle))
        return;
    const auto start = Sci(SCI_GETSELECTIONSTART), end = Sci(SCI_GETSELECTIONEND);
    try {
        const auto output = EvaluateExpression(ReadSelection(start, end), false);
        if (!output.empty())
            Replace(end, end, ' ' + output);
    } catch (const std::exception& error) {
        Sci(SCI_SHOWNOTIFICATION, start << 2, reinterpret_cast<LPARAM>(error.what()));
    }
}

ShortcutKey escapeHtml{false, true, true, 'X'}, unescapeHtml{false, true, true, 'H'};
ShortcutKey encodeUrl{true, false, true, 'E'}, decodeUrl{true, false, true, 'R'};
FuncItem commands[]{
    {{}, Format<false>},
    {{}, Format<true>},
    {},
    {{}, EvaluateJavaScript},
    {{}, ConvertEntities<true>, 0, false, &escapeHtml},
    {{}, ConvertEntities<false>, 0, false, &unescapeHtml},
    {},
    {{}, ConvertUrl<IDM_EDIT_URLENCODE>, 0, false, &encodeUrl},
    {{}, ConvertUrl<IDM_EDIT_URLCOMPONENTENCODE>},
    {{}, ConvertUrl<IDM_EDIT_URLDECODE>, 0, false, &decodeUrl},
};
constexpr UINT labels[]{
    IDM_EDIT_CODE_COMPRESS, IDM_EDIT_CODE_PRETTY, 0, CMD_EVALUATE_JS_EXPR,
    IDM_EDIT_XHTML_ESCAPE_CHAR, IDM_EDIT_XHTML_UNESCAPE_CHAR, 0,
    IDM_EDIT_URLENCODE, IDM_EDIT_URLCOMPONENTENCODE, IDM_EDIT_URLDECODE,
};
static_assert(std::size(labels) == std::size(commands));
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) instance = module;
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) const wchar_t* getName() {
    static wchar_t name[menuItemSize]{};
    LoadStringW(instance, 1, name, _countof(name));
    return name;
}
extern "C" __declspec(dllexport) void setInfo(NppData data) { host = data; }
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    for (size_t index = 0; index < std::size(commands); ++index) {
        if (labels[index])
            LoadStringW(instance, labels[index], commands[index]._itemName, menuItemSize);
    }
    *count = _countof(commands);
    return commands;
}
extern "C" __declspec(dllexport) BOOL multipadCommandEnabled(UINT id) {
    if (!SendMessageW(host._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0) || Sci(SCI_GETREADONLY))
        return FALSE;
    if (id == static_cast<UINT>(commands[0]._cmdID) || id == static_cast<UINT>(commands[1]._cmdID)) {
        MultiPadCodeFormatStyles styles{};
        return GetFormatStyles(styles);
    }
    return !Sci(SCI_GETSELECTIONEMPTY);
}
extern "C" __declspec(dllexport) void beNotified(SCNotification*) {}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
