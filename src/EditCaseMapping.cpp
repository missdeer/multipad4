// Host case-mapping service adapted from notepad4 Edit.cpp; see THIRD_PARTY.md.
#include "EditCaseMapping.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "App.h"
#include "StyleManager.h"
#include "resource.h"
#include <cstdio>
#include <cwctype>
#include <string_view>
namespace {
thread_local const EditorDocument *mappingDocument = nullptr;
thread_local MultiPadSelectionMapper selectionMapper = nullptr;
constexpr int pluginCaseMapping = 4; // Reserved custom mapping value below the editor command IDs.

void *Allocate(size_t size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}
std::wstring Map(const std::wstring &text, DWORD flags) {
    int length = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, text.data(), static_cast<int>(text.size()), nullptr, 0,
                               nullptr, nullptr, 0);
    if (!length)
        return text;
    std::wstring out(length, L'\0');
    LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, text.data(), static_cast<int>(text.size()), out.data(), length,
                  nullptr, nullptr, 0);
    return out;
}
int Digit(wchar_t c) {
    return c >= L'0' && c <= L'9'   ? c - L'0'
           : c >= L'a' && c <= L'f' ? c - L'a' + 10
           : c >= L'A' && c <= L'F' ? c - L'A' + 10
                                    : -1;
}
std::wstring HexToCharacters(const std::wstring &text) {
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (i + 1 < text.size() &&
            ((c == L'\\' && (text[i + 1] == L'x' || text[i + 1] == L'u' || text[i + 1] == L'U')) ||
             (c == L'U' && text[i + 1] == L'+'))) {
            const auto mode = text[i + 1];
            size_t limit = mode == L'x' ? 4 : mode == L'u' ? 4 : mode == L'U' ? 8 : 6;
            size_t minimum = mode == L'x' ? 2 : mode == L'U' ? 8 : 4;
            unsigned value = 0;
            size_t count = 0;
            for (; count < limit && i + 2 + count < text.size(); ++count) {
                int h = Digit(text[i + 2 + count]);
                if (h < 0)
                    break;
                value = (value << 4) | h;
            }
            if (count >= minimum && value <= 0x10FFFF) {
                i += count + 1;
                if (value > 0xFFFF) {
                    value -= 0x10000;
                    out += static_cast<wchar_t>(0xD800 + (value >> 10));
                    out += static_cast<wchar_t>(0xDC00 + (value & 1023));
                } else
                    out += static_cast<wchar_t>(value);
                continue;
            }
        }
        out += c;
    }
    return out;
}
std::wstring Escape(const std::wstring &text, int menu, bool resourceScript) {
    std::wstring out;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (menu == IDM_EDIT_ESCAPECCHARS) {
            if (resourceScript && c == L'"') {
                out += L"\"\"";
                continue;
            }
            if (resourceScript && c == L'\'') {
                out += c;
                continue;
            }
            auto position = std::wstring_view(L"\a\b\t\n\v\f\r\\\"'").find(c);
            if (position != std::wstring_view::npos) {
                out += L'\\';
                out += L"abtnvfr\\\"'"[position];
            } else
                out += c;
        } else if (menu == IDM_EDIT_UNESCAPECCHARS) {
            if (resourceScript && c == L'"' && i + 1 < text.size() && text[i + 1] == L'"') {
                out += c;
                ++i;
                continue;
            }
            if (resourceScript && c == L'\\' && i + 1 < text.size() && (text[i + 1] == L'"' || text[i + 1] == L'\'')) {
                out += c;
                continue;
            }
            if (c == L'\\' && i + 1 < text.size()) {
                auto position = std::wstring_view(L"abtnvfr\\\"'").find(text[i + 1]);
                if (position != std::wstring_view::npos) {
                    out += L"\a\b\t\n\v\f\r\\\"'"[position];
                    ++i;
                    continue;
                }
            }
            out += c;
        }
    }
    return out;
}
} // namespace
char *EditMapTextCase(int menu, const char *text, size_t &length, UINT codePage) noexcept {
    if (menu == pluginCaseMapping)
        return selectionMapper ? selectionMapper(text, length, codePage) : nullptr;
    try {
        int count = MultiByteToWideChar(codePage, 0, text, static_cast<int>(length), nullptr, 0);
        if (!count)
            return nullptr;
        std::wstring wide(count, L'\0');
        MultiByteToWideChar(codePage, 0, text, static_cast<int>(length), wide.data(), count);
        std::wstring mapped = wide;
        switch (menu) {
        case IDM_EDIT_CHAR2HEX: {
            mapped.clear();
            wchar_t buffer[32];
            for (wchar_t c : wide) {
                swprintf_s(buffer, c <= 255 ? L"\\x%02X" : L"\\u%04X", static_cast<unsigned>(c));
                mapped += buffer;
            }
            if (wide.size() == 2 && wide[0] >= 0xD800 && wide[0] <= 0xDBFF && wide[1] >= 0xDC00 && wide[1] <= 0xDFFF) {
                unsigned value = 0x10000 + ((wide[0] - 0xD800) << 10) + (wide[1] - 0xDC00);
                swprintf_s(buffer, L" U+%06X", value);
                mapped += buffer;
            }
            break;
        }
        case IDM_EDIT_HEX2CHAR:
            mapped = HexToCharacters(wide);
            break;
        case IDM_EDIT_ESCAPECCHARS:
        case IDM_EDIT_UNESCAPECCHARS:
            mapped =
                Escape(wide, menu, mappingDocument && GetDocumentLexer(*mappingDocument)->rid == NP2LEX_RESOURCESCRIPT);
            break;
        case IDM_EDIT_TITLECASE:
            mapped =
                Map(Map(wide, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING), LCMAP_TITLECASE | LCMAP_LINGUISTIC_CASING);
            break;
        case IDM_EDIT_SENTENCECASE: {
            mapped = Map(wide, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING);
            bool start = true;
            for (auto &c : mapped) {
                if (std::wstring_view(L"\r\n.;!?。．！？；\u0085\u2028\u2029").find(c) != std::wstring_view::npos)
                    start = true;
                else if (start && IsCharAlphaNumericW(c)) {
                    c = static_cast<wchar_t>(
                        reinterpret_cast<ULONG_PTR>(CharUpperW(reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(c)))));
                    start = false;
                }
            }
            break;
        }
        default:
            for (auto &c : mapped)
                c = IsCharUpperW(c)   ? static_cast<wchar_t>(reinterpret_cast<ULONG_PTR>(
                                            CharLowerW(reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(c)))))
                    : IsCharLowerW(c) ? static_cast<wchar_t>(reinterpret_cast<ULONG_PTR>(
                                            CharUpperW(reinterpret_cast<LPWSTR>(static_cast<ULONG_PTR>(c)))))
                                      : c;
            break;
        }
        if (mapped == wide)
            return nullptr;
        int size = WideCharToMultiByte(codePage, 0, mapped.data(), static_cast<int>(mapped.size()), nullptr, 0, nullptr,
                                       nullptr);
        auto *result = static_cast<char *>(Allocate(static_cast<size_t>(size) + 1));
        if (!result)
            return nullptr;
        WideCharToMultiByte(codePage, 0, mapped.data(), static_cast<int>(mapped.size()), result, size, nullptr,
                            nullptr);
        length = size;
        return result;
    } catch (...) {
        return nullptr;
    }
}

void ApplyDocumentCaseMapping(EditorDocument &document, UINT command) {
    const auto *previous = mappingDocument;
    struct Restore {
        const EditorDocument *previous;
        ~Restore() {
            mappingDocument = previous;
        }
    } restore{previous};
    mappingDocument = &document;
    document.Send(SCI_CUSTOMCASEMAPPING, command);
}

void ApplyDocumentCaseMapping(EditorDocument &document, MultiPadSelectionMapper mapper) {
    struct Restore {
        MultiPadSelectionMapper previous;
        ~Restore() { selectionMapper = previous; }
    } restore{selectionMapper};
    selectionMapper = mapper;
    document.Send(SCI_CUSTOMCASEMAPPING, pluginCaseMapping);
}
