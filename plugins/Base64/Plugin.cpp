#include "../PluginApi.h"
#include "PluginInterface.h"
#include "FileIO.h"
#include "../../src/resource.h"
#include <cctype>
#include <stdexcept>

namespace {
HINSTANCE instance = nullptr;
NppData host{};

LRESULT Sci(UINT message, WPARAM w = 0, LPARAM l = 0) {
    return SendMessageW(host._scintillaMainHandle, message, w, l);
}

std::string Encode(const std::string& input, bool urlSafe) {
    const char* alphabet = urlSafe ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                                   : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    unsigned accumulator = 0;
    int bits = 0;
    for (unsigned char byte : input) {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            output += alphabet[(accumulator >> bits) & 63];
        }
    }
    if (bits) output += alphabet[(accumulator << (6 - bits)) & 63];
    while (output.size() % 4) output += '=';
    return output;
}

std::string Decode(const std::string& input) {
    std::string output;
    unsigned accumulator = 0;
    int bits = 0;
    bool padded = false;
    for (unsigned char byte : input) {
        if (std::isspace(byte)) continue;
        if (byte == '=') {
            padded = true;
            continue;
        }
        if (padded) throw std::runtime_error("Invalid Base64 padding.");
        int value = byte >= 'A' && byte <= 'Z' ? byte - 'A'
                  : byte >= 'a' && byte <= 'z' ? byte - 'a' + 26
                  : byte >= '0' && byte <= '9' ? byte - '0' + 52
                  : byte == '+' || byte == '-' ? 62
                  : byte == '/' || byte == '_' ? 63
                  : -1;
        if (value < 0) throw std::runtime_error("Invalid Base64 text.");
        accumulator = (accumulator << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output += static_cast<char>(accumulator >> bits);
        }
    }
    if (bits == 6) throw std::runtime_error("Invalid Base64 length.");
    return output;
}

std::string Hex(std::string_view bytes, LRESULT eolMode) {
    const char* eol = eolMode == SC_EOL_CRLF ? "\r\n" : eolMode == SC_EOL_LF ? "\n" : "\r";
    constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i) output += i % 16 ? " " : eol;
        auto byte = static_cast<unsigned char>(bytes[i]);
        output += digits[byte >> 4];
        output += digits[byte & 15];
    }
    return output;
}

template<UINT Command>
void ConvertSelection() {
    const auto buffer = SendMessageW(host._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0);
    const auto encodingId = SendMessageW(host._nppHandle, MPM_GETBUFFERFILEENCODING, buffer, 0);
    if (encodingId < 0) return;
    if (Sci(SCI_GETREADONLY) || Sci(SCI_GETSELECTIONEMPTY)) return;
    if (Sci(SCI_SELECTIONISRECTANGLE)) {
        wchar_t message[512]{};
        if (!LoadStringW(GetModuleHandleW(nullptr), IDS_SELRECT, message, _countof(message)))
            wcscpy_s(message, L"This command does not support rectangular selections.");
        auto text = files::ToUtf8(message);
        Sci(SCI_SHOWNOTIFICATION, SC_NOTIFICATIONPOSITION_DEFAULT, reinterpret_cast<LPARAM>(text.c_str()));
        return;
    }

    const auto start = Sci(SCI_GETSELECTIONSTART), end = Sci(SCI_GETSELECTIONEND);
    std::string selected(end - start + 1, '\0');
    Sci_TextRangeFull range{{start, end}, selected.data()};
    Sci(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<LPARAM>(&range));
    selected.resize(end - start);
    const auto encoding = static_cast<files::Encoding>(encodingId);
    const bool utf8 = files::CodePage(encoding) == CP_UTF8;
    std::string output;
    if constexpr (Command == IDM_EDIT_BASE64_DECODE_AS_HEX) {
        output = Hex(Decode(selected), Sci(SCI_GETEOLMODE));
    } else if constexpr (Command == IDM_EDIT_BASE64_DECODE) {
        output = Decode(selected);
        if (!utf8) output = files::Decode(output, encoding).utf8;
    } else {
        output = Encode(utf8 ? selected : files::Encode({selected, encoding}), Command == IDM_EDIT_BASE64_SAFE_ENCODE);
        if constexpr (Command == IDM_EDIT_BASE64_HTML_EMBEDDED_IMAGE) {
            const auto length = SendMessageW(host._nppHandle, NPPM_GETEXTPART, 0, 0);
            std::wstring extension(length + 1, L'\0');
            SendMessageW(host._nppHandle, NPPM_GETEXTPART, extension.size(), reinterpret_cast<LPARAM>(extension.data()));
            extension.resize(length);
            if (!extension.empty()) extension.erase(0, 1);
            output = "<img src=\"data:image/" + files::ToUtf8(extension) + ";base64," + output + "\" />";
        }
    }

    Sci(SCI_BEGINUNDOACTION);
    Sci(SCI_SETTARGETRANGE, start, end);
    Sci(SCI_REPLACETARGET, output.size(), reinterpret_cast<LPARAM>(output.data()));
    Sci(SCI_SETSEL, start, start + output.size());
    Sci(SCI_ENDUNDOACTION);
}

FuncItem commands[]{
    {{}, ConvertSelection<IDM_EDIT_BASE64_ENCODE>},
    {{}, ConvertSelection<IDM_EDIT_BASE64_SAFE_ENCODE>},
    {{}, ConvertSelection<IDM_EDIT_BASE64_HTML_EMBEDDED_IMAGE>},
    {{}, ConvertSelection<IDM_EDIT_BASE64_DECODE>},
    {{}, ConvertSelection<IDM_EDIT_BASE64_DECODE_AS_HEX>},
};
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) instance = module;
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) const wchar_t* getName() { return L"Base64"; }
extern "C" __declspec(dllexport) void setInfo(NppData data) { host = data; }
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) {
    constexpr UINT labels[]{IDM_EDIT_BASE64_ENCODE, IDM_EDIT_BASE64_SAFE_ENCODE,
        IDM_EDIT_BASE64_HTML_EMBEDDED_IMAGE, IDM_EDIT_BASE64_DECODE, IDM_EDIT_BASE64_DECODE_AS_HEX};
    static_assert(_countof(labels) == _countof(commands));
    for (size_t index = 0; index < _countof(commands); ++index) {
        auto& label = commands[index]._itemName;
        LoadStringW(instance, labels[index], label, _countof(label));
    }
    *count = _countof(commands);
    return commands;
}
extern "C" __declspec(dllexport) BOOL multipadCommandEnabled(UINT) {
    return SendMessageW(host._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0) &&
        !Sci(SCI_GETREADONLY) && !Sci(SCI_GETSELECTIONEMPTY);
}
extern "C" __declspec(dllexport) void beNotified(SCNotification*) {}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
