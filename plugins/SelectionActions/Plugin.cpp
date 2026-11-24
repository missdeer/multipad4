#include "../EditExpression.h"
#include "../PluginApi.h"
#include "FileIO.h"
#include "PluginInterface.h"
#include "resource.h"
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <regex>
#include <shellapi.h>
#include <shlwapi.h>
#include <stdexcept>

namespace {
HINSTANCE instance = nullptr;
NppData host{};

LRESULT Sci(UINT message, WPARAM w = 0, LPARAM l = 0) {
    return SendMessageW(host._scintillaMainHandle, message, w, l);
}

std::string Selection(bool pathToken = false) {
    auto start = Sci(SCI_GETSELECTIONSTART);
    auto end = Sci(SCI_GETSELECTIONEND);
    if (pathToken && Sci(SCI_GETSELECTIONEMPTY)) {
        const auto length = Sci(SCI_GETLENGTH);
        start = end = Sci(SCI_GETCURRENTPOS);
        constexpr auto delimiters = "\r\n\t \"'<>|";
        while (start > 0 && !std::strchr(delimiters, static_cast<unsigned char>(Sci(SCI_GETCHARAT, start - 1))))
            --start;
        while (end < length && !std::strchr(delimiters, static_cast<unsigned char>(Sci(SCI_GETCHARAT, end))))
            ++end;
    } else if (start == end) {
        const auto caret = Sci(SCI_GETCURRENTPOS);
        start = Sci(SCI_WORDSTARTPOSITION, caret, TRUE);
        end = Sci(SCI_WORDENDPOSITION, caret, TRUE);
    }
    std::string text(end - start + 1, '\0');
    Sci_TextRangeFull range{{start, end}, text.data()};
    Sci(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range));
    text.resize(end - start);
    return text;
}

std::wstring ToolSetting(const wchar_t* key, const wchar_t* fallback) {
    MultiPadProfileString request{L"Tools", key, fallback, nullptr, 0};
    const auto length = SendMessageW(host._nppHandle, MPM_GETPROFILESTRING, 0, reinterpret_cast<LPARAM>(&request));
    if (length <= 0)
        return {};
    std::wstring value(length, L'\0');
    request.text = value.data();
    request.capacity = value.size() + 1;
    if (SendMessageW(host._nppHandle, MPM_GETPROFILESTRING, 0, reinterpret_cast<LPARAM>(&request)) != length)
        return {};
    return value;
}

std::filesystem::path DocumentDirectory() {
    const auto length = SendMessageW(host._nppHandle, NPPM_GETCURRENTDIRECTORY, 0, 0);
    if (length <= 0)
        return {};
    std::wstring directory(length, L'\0');
    if (!SendMessageW(host._nppHandle, NPPM_GETCURRENTDIRECTORY, directory.size() + 1,
                      reinterpret_cast<LPARAM>(directory.data())))
        return {};
    return directory;
}

template<bool ContainingFolder>
void OpenSelection() {
    auto value = files::FromUtf8(Selection(true));
    if (value.empty())
        return;
    while (!value.empty() && (iswspace(value.front()) || value.front() == L'"' || value.front() == L'\'' || value.front() == L'<'))
        value.erase(0, 1);
    while (!value.empty() && (iswspace(value.back()) || value.back() == L'"' || value.back() == L'\'' || value.back() == L'>'))
        value.pop_back();
    if (value.starts_with(L"file://")) {
        wchar_t path[32768];
        DWORD capacity = _countof(path);
        if (SUCCEEDED(PathCreateFromUrlW(value.c_str(), path, &capacity, 0)))
            value = path;
    }
    if (value.find(L"://") == std::wstring::npos) {
        static const std::wregex location(LR"(:(\d+)(?::(\d+))?$)");
        std::wsmatch suffix;
        LRESULT line = 0, column = 1;
        if (std::regex_search(value, suffix, location) && suffix.position() > 1) {
            line = std::wcstoll(suffix[1].str().c_str(), nullptr, 10);
            if (suffix[2].matched)
                column = std::wcstoll(suffix[2].str().c_str(), nullptr, 10);
            value.erase(static_cast<size_t>(suffix.position()));
            while (!value.empty() && (value.back() == L'"' || value.back() == L'\''))
                value.pop_back();
        }
        const DWORD capacity = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (capacity) {
            std::wstring expanded(capacity, L'\0');
            const DWORD length = ExpandEnvironmentStringsW(value.c_str(), expanded.data(), capacity);
            if (length && length <= capacity) {
                expanded.resize(length - 1);
                value = std::move(expanded);
            }
        }
        std::filesystem::path path(value);
        if (path.is_relative())
            path = DocumentDirectory() / path;
        if (ContainingFolder || std::filesystem::is_directory(path)) {
            SendMessageW(host._nppHandle, MPM_OPENFOLDER, ContainingFolder, reinterpret_cast<LPARAM>(path.c_str()));
            return;
        }
        if (std::filesystem::is_regular_file(path)) {
            if (SendMessageW(host._nppHandle, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str())) && line > 0) {
                const auto lines = Sci(SCI_GETLINECOUNT);
                if (lines > 0) {
                    const auto position = Sci(SCI_FINDCOLUMN, (std::min)(line - 1, lines - 1), (std::max)(LRESULT(0), column - 1));
                    Sci(SCI_GOTOPOS, position);
                    Sci(SCI_SCROLLCARET);
                }
            }
            return;
        }
        value = path.wstring();
    }
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(host._nppHandle, L"open", value.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        throw std::runtime_error("Cannot open selected path or link.");
}

struct Tool {
    const wchar_t* key;
    const wchar_t* fallback;
};
constexpr Tool tools[]{
    {L"GoogleSearchUrl", L"https://www.google.com/search?q=%s"},
    {L"BingSearchUrl", L"https://www.bing.com/search?q=%s"},
    {L"WikiSearchUrl", L"https://en.wikipedia.org/wiki/Special:Search?search=%s"},
    {L"CustomAction1", L""}, {L"CustomAction2", L""},
};

template<size_t Index>
void RunTool() {
    const auto selection = Selection();
    auto command = ToolSetting(tools[Index].key, tools[Index].fallback);
    if (selection.empty() || command.empty())
        return;
    std::wstring escaped;
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (unsigned char c : selection) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped += c;
        } else {
            escaped += {L'%', hex[c >> 4], hex[c & 15]};
        }
    }
    size_t position = 0;
    while ((position = command.find(L"%s", position)) != std::wstring::npos) {
        command.replace(position, 2, escaped);
        position += escaped.size();
    }
    SendMessageW(host._nppHandle, MPM_RUNCOMMAND, 0, reinterpret_cast<LPARAM>(command.c_str()));
}

void CalculateExpression() {
    EvaluateSelectedExpression(host._scintillaMainHandle, true);
}

ShortcutKey customAction1{true, false, true, '1'}, customAction2{true, false, true, '2'};
FuncItem commands[]{
    {{}, OpenSelection<false>},
    {{}, OpenSelection<true>},
    {},
    {{}, CalculateExpression},
    {},
    {{}, RunTool<0>},
    {{}, RunTool<1>},
    {{}, RunTool<2>},
    {},
    {{}, RunTool<3>, 0, false, &customAction1},
    {{}, RunTool<4>, 0, false, &customAction2},
};
constexpr UINT labels[]{
    CMD_OPEN_PATH_OR_LINK, CMD_OPEN_CONTAINING_FOLDER, 0, CMD_CALCULATE_EXPR, 0,
    CMD_ONLINE_SEARCH_GOOGLE, CMD_ONLINE_SEARCH_BING, CMD_ONLINE_SEARCH_WIKI, 0,
    CMD_CUSTOM_ACTION1, CMD_CUSTOM_ACTION2,
};
static_assert(_countof(labels) == _countof(commands));
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH)
        instance = module;
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
    for (size_t index = 0; index < _countof(commands); ++index) {
        if (labels[index])
            LoadStringW(instance, labels[index], commands[index]._itemName, menuItemSize);
    }
    *count = _countof(commands);
    return commands;
}
extern "C" __declspec(dllexport) BOOL multipadCommandEnabled(UINT id) {
    if (!SendMessageW(host._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0))
        return FALSE;
    if (id == static_cast<UINT>(commands[0]._cmdID) || id == static_cast<UINT>(commands[1]._cmdID))
        return Sci(SCI_GETLENGTH) != 0;
    if (Sci(SCI_GETSELECTIONEMPTY))
        return FALSE;
    if (id == static_cast<UINT>(commands[3]._cmdID))
        return !Sci(SCI_GETREADONLY);
    if (id == static_cast<UINT>(commands[9]._cmdID))
        return !ToolSetting(tools[3].key, tools[3].fallback).empty();
    if (id == static_cast<UINT>(commands[10]._cmdID))
        return !ToolSetting(tools[4].key, tools[4].fallback).empty();
    return TRUE;
}
extern "C" __declspec(dllexport) void multipadPopulateEditorContextMenu(HMENU menu) {
    wchar_t label[menuItemSize]{};
    if (LoadStringW(instance, 2, label, _countof(label))) {
        const UINT command = commands[0]._cmdID;
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (multipadCommandEnabled(command) ? MF_ENABLED : MF_GRAYED), command, label);
    }
}
extern "C" __declspec(dllexport) void beNotified(SCNotification*) {}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
