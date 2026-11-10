// Host case-mapping service adapted from notepad4 Edit.cpp; see THIRD_PARTY.md.
#include "EditCaseMapping.h"
#include "../3rdparty/notepad4/src/EditLexer.h"
#include "App.h"
#include "StyleManager.h"
#include "resource.h"
#include <cstdio>
#include <cwctype>
#include <elscore.h>
#include <elssrvc.h>
#include <string_view>
namespace {
thread_local const EditorDocument *mappingDocument = nullptr;

void *Allocate(size_t size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}
template <class T> T DLLFunction(HMODULE module, const char *name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}
const GUID WIN10_ELS_GUID_TRANSLITERATION_HANGUL_DECOMPOSITION = {
    0x4BA2A721, 0xE43D, 0x41b7, {0xB3, 0x30, 0x53, 0x6A, 0xE1, 0xE4, 0x88, 0x63}};
int TransliterateText(const GUID *pGuid, LPCWSTR pszTextW, int cchTextW, LPWSTR &pszMappedW) noexcept {
    using MappingGetServicesSig = HRESULT(WINAPI *)(PMAPPING_ENUM_OPTIONS pOptions, PMAPPING_SERVICE_INFO * prgServices,
                                                    DWORD * pdwServicesCount) noexcept;
    using MappingFreeServicesSig = HRESULT(WINAPI *)(PMAPPING_SERVICE_INFO pServiceInfo) noexcept;
    using MappingRecognizeTextSig =
        HRESULT(WINAPI *)(PMAPPING_SERVICE_INFO pServiceInfo, LPCWSTR pszText, DWORD dwLength, DWORD dwIndex,
                          PMAPPING_OPTIONS pOptions, PMAPPING_PROPERTY_BAG pbag) noexcept;
    using MappingFreePropertyBagSig = HRESULT(WINAPI *)(PMAPPING_PROPERTY_BAG pBag) noexcept;

    static uint8_t triedLoadingELSCore = 0;
    static MappingGetServicesSig pfnMappingGetServices;
    static MappingFreeServicesSig pfnMappingFreeServices;
    static MappingRecognizeTextSig pfnMappingRecognizeText;
    static MappingFreePropertyBagSig pfnMappingFreePropertyBag;

    if (triedLoadingELSCore == 0) {
        triedLoadingELSCore = 1;
        HMODULE hDLL = LoadLibraryExW(L"elscore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (hDLL != nullptr) {
            pfnMappingGetServices = DLLFunction<MappingGetServicesSig>(hDLL, "MappingGetServices");
            pfnMappingFreeServices = DLLFunction<MappingFreeServicesSig>(hDLL, "MappingFreeServices");
            pfnMappingRecognizeText = DLLFunction<MappingRecognizeTextSig>(hDLL, "MappingRecognizeText");
            pfnMappingFreePropertyBag = DLLFunction<MappingFreePropertyBagSig>(hDLL, "MappingFreePropertyBag");
            if (pfnMappingGetServices == nullptr || pfnMappingFreeServices == nullptr ||
                pfnMappingRecognizeText == nullptr || pfnMappingFreePropertyBag == nullptr) {
                FreeLibrary(hDLL);
                return 0;
            }
            (void)hDLL; // Keep system service loaded for cached callbacks.
            triedLoadingELSCore = 2;
        }
    }
    if (triedLoadingELSCore != 2) {
        return 0;
    }

    MAPPING_ENUM_OPTIONS enumOptions;
    PMAPPING_SERVICE_INFO prgServices = nullptr;
    DWORD dwServicesCount = 0;

    memset(&enumOptions, 0, sizeof(MAPPING_ENUM_OPTIONS));
    enumOptions.Size = sizeof(MAPPING_ENUM_OPTIONS);
    enumOptions.pGuid = const_cast<GUID *>(pGuid);

    HRESULT hr = pfnMappingGetServices(&enumOptions, &prgServices, &dwServicesCount);
    const DWORD serviceCount = dwServicesCount;
    dwServicesCount = 0;
    if (SUCCEEDED(hr) && serviceCount && prgServices) {
        MAPPING_PROPERTY_BAG bag;
        memset(&bag, 0, sizeof(MAPPING_PROPERTY_BAG));
        bag.Size = sizeof(MAPPING_PROPERTY_BAG);
        hr = pfnMappingRecognizeText(prgServices, pszTextW, cchTextW, 0, nullptr, &bag);
        if (SUCCEEDED(hr) && bag.dwRangesCount && bag.prgResultRanges) {
            const DWORD dwDataSize = bag.prgResultRanges[0].dwDataSize;
            dwServicesCount = dwDataSize / sizeof(WCHAR);
            pszTextW = static_cast<LPCWSTR>(bag.prgResultRanges[0].pData);
            if (dwServicesCount != 0 && pszTextW[0] != L'\0') {
                LPWSTR pszConvW = static_cast<LPWSTR>(Allocate(dwDataSize + sizeof(WCHAR)));
                if (pszConvW) {
                    memcpy(pszConvW, pszTextW, dwDataSize);
                    pszMappedW = pszConvW;
                } else
                    dwServicesCount = 0;
            }
            pfnMappingFreePropertyBag(&bag);
        }
        pfnMappingFreeServices(prgServices);
    }

    return dwServicesCount;
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
        } else if (menu == IDM_EDIT_XHTML_ESCAPE_CHAR) {
            switch (c) {
            case L'&':
                out += L"&amp;";
                break;
            case L'<':
                out += L"&lt;";
                break;
            case L'>':
                out += L"&gt;";
                break;
            case L'\"':
                out += L"&quot;";
                break;
            case L'\'':
                out += L"&apos;";
                break;
            default:
                out += c;
            }
        } else {
            bool found = false;
            for (const auto &entity : {std::pair{L"&amp;", L'&'},
                                       {L"&lt;", L'<'},
                                       {L"&gt;", L'>'},
                                       {L"&quot;", L'\"'},
                                       {L"&apos;", L'\''}}) {
                size_t length = wcslen(entity.first);
                if (text.compare(i, length, entity.first) == 0) {
                    out += entity.second;
                    i += length - 1;
                    found = true;
                    break;
                }
            }
            if (!found)
                out += c;
        }
    }
    return out;
}
} // namespace
char *EditMapTextCase(int menu, const char *text, size_t &length, UINT codePage) noexcept {
    try {
        int count = MultiByteToWideChar(codePage, 0, text, static_cast<int>(length), nullptr, 0);
        if (!count)
            return nullptr;
        std::wstring wide(count, L'\0');
        MultiByteToWideChar(codePage, 0, text, static_cast<int>(length), wide.data(), count);
        std::wstring mapped = wide;
        DWORD flags = 0;
        const GUID *service = nullptr;
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
        case IDM_EDIT_XHTML_ESCAPE_CHAR:
        case IDM_EDIT_XHTML_UNESCAPE_CHAR:
            mapped =
                Escape(wide, menu, mappingDocument && GetDocumentLexer(*mappingDocument)->rid == NP2LEX_RESOURCESCRIPT);
            break;
        case IDM_EDIT_MAP_FULLWIDTH:
            flags = LCMAP_FULLWIDTH;
            break;
        case IDM_EDIT_MAP_HALFWIDTH:
            flags = LCMAP_HALFWIDTH;
            break;
        case IDM_EDIT_MAP_HIRAGANA:
            flags = LCMAP_HIRAGANA;
            break;
        case IDM_EDIT_MAP_KATAKANA:
            flags = LCMAP_KATAKANA;
            break;
        case IDM_EDIT_MAP_SIMPLIFIED_CHINESE:
            flags = LCMAP_SIMPLIFIED_CHINESE;
            service = &ELS_GUID_TRANSLITERATION_HANT_TO_HANS;
            break;
        case IDM_EDIT_MAP_TRADITIONAL_CHINESE:
            flags = LCMAP_TRADITIONAL_CHINESE;
            service = &ELS_GUID_TRANSLITERATION_HANS_TO_HANT;
            break;
        case IDM_EDIT_MAP_MALAYALAM_LATIN:
            service = &ELS_GUID_TRANSLITERATION_MALAYALAM_TO_LATIN;
            break;
        case IDM_EDIT_MAP_DEVANAGARI_LATIN:
            service = &ELS_GUID_TRANSLITERATION_DEVANAGARI_TO_LATIN;
            break;
        case IDM_EDIT_MAP_CYRILLIC_LATIN:
            service = &ELS_GUID_TRANSLITERATION_CYRILLIC_TO_LATIN;
            break;
        case IDM_EDIT_MAP_BENGALI_LATIN:
            service = &ELS_GUID_TRANSLITERATION_BENGALI_TO_LATIN;
            break;
        case IDM_EDIT_MAP_HANGUL_DECOMPOSITION:
            service = &WIN10_ELS_GUID_TRANSLITERATION_HANGUL_DECOMPOSITION;
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
        bool transliterated = false;
        if (service) {
            LPWSTR result = nullptr;
            int size = TransliterateText(service, wide.data(), static_cast<int>(wide.size()), result);
            if (size && result) {
                mapped.assign(result, size);
                if (!mapped.empty() && mapped.back() == L'\0')
                    mapped.pop_back();
                HeapFree(GetProcessHeap(), 0, result);
                transliterated = true;
            }
        }
        if (flags && !transliterated)
            mapped = Map(wide, flags);
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
