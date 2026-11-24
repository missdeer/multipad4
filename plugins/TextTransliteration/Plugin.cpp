// Transliteration adapted from notepad4 Edit.cpp. See THIRD_PARTY.md.
#include "../PluginApi.h"
#include "PluginInterface.h"
#include "resource.h"
#include <climits>
#include <elscore.h>
#include <elssrvc.h>
#include <memory>
#include <string>

namespace {
HINSTANCE instance = nullptr;
NppData host{};

const GUID hangulDecomposition{
    0x4BA2A721, 0xE43D, 0x41B7, {0xB3, 0x30, 0x53, 0x6A, 0xE1, 0xE4, 0x88, 0x63}};

struct ElsApi {
    HMODULE module = LoadLibraryExW(L"elscore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    decltype(&MappingGetServices) getServices = Resolve<decltype(getServices)>("MappingGetServices");
    decltype(&MappingFreeServices) freeServices = Resolve<decltype(freeServices)>("MappingFreeServices");
    decltype(&MappingRecognizeText) recognizeText = Resolve<decltype(recognizeText)>("MappingRecognizeText");
    decltype(&MappingFreePropertyBag) freeBag = Resolve<decltype(freeBag)>("MappingFreePropertyBag");

    ElsApi() {
        // Keep the system DLL loaded for the cached callbacks across plugin reloads.
        HMODULE pinned = nullptr;
        if (module && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                        reinterpret_cast<LPCWSTR>(module), &pinned))
            FreeLibrary(module);
    }

    template<class Function>
    Function Resolve(const char* name) const {
        return module ? reinterpret_cast<Function>(GetProcAddress(module, name)) : nullptr;
    }
};

std::wstring Transliterate(const std::wstring& text, const GUID& service) {
    static const ElsApi els;
    if (!els.getServices || !els.freeServices || !els.recognizeText || !els.freeBag)
        return {};

    MAPPING_ENUM_OPTIONS options{};
    options.Size = sizeof(options);
    options.pGuid = const_cast<GUID*>(&service);
    PMAPPING_SERVICE_INFO services = nullptr;
    DWORD count = 0;
    if (FAILED(els.getServices(&options, &services, &count)) || !services)
        return {};
    const std::unique_ptr<MAPPING_SERVICE_INFO, decltype(els.freeServices)> releaseServices(services, els.freeServices);
    if (!count)
        return {};

    MAPPING_PROPERTY_BAG bag{};
    bag.Size = sizeof(bag);
    if (FAILED(els.recognizeText(services, text.data(), static_cast<DWORD>(text.size()), 0, nullptr, &bag)))
        return {};
    const std::unique_ptr<MAPPING_PROPERTY_BAG, decltype(els.freeBag)> releaseBag(&bag, els.freeBag);
    if (!bag.dwRangesCount || !bag.prgResultRanges)
        return {};
    const auto& range = bag.prgResultRanges[0];
    const auto* mapped = static_cast<const wchar_t*>(range.pData);
    size_t length = range.dwDataSize / sizeof(wchar_t);
    if (!mapped || !length || !mapped[0])
        return {};
    if (mapped[length - 1] == L'\0')
        --length;
    return std::wstring(mapped, length);
}

template<DWORD Flags, const GUID* Service>
char* MapSelection(const char* input, size_t& length, UINT codePage) noexcept {
    try {
        if (length > INT_MAX)
            return nullptr;
        const int inputLength = static_cast<int>(length);
        const int count = MultiByteToWideChar(codePage, 0, input, inputLength, nullptr, 0);
        if (!count)
            return nullptr;
        std::wstring text(count, L'\0');
        if (!MultiByteToWideChar(codePage, 0, input, inputLength, text.data(), count))
            return nullptr;

        std::wstring mapped;
        if constexpr (Service != nullptr)
            mapped = Transliterate(text, *Service);
        if constexpr (Flags != 0) {
            if (mapped.empty()) {
                const int size = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, Flags, text.data(), count, nullptr, 0,
                                               nullptr, nullptr, 0);
                if (!size)
                    return nullptr;
                mapped.resize(size);
                if (!LCMapStringEx(LOCALE_NAME_USER_DEFAULT, Flags, text.data(), count, mapped.data(), size,
                                   nullptr, nullptr, 0))
                    return nullptr;
            }
        }
        if (mapped.empty() || mapped == text || mapped.size() > INT_MAX)
            return nullptr;
        const int mappedLength = static_cast<int>(mapped.size());
        const int bytes = WideCharToMultiByte(codePage, 0, mapped.data(), mappedLength, nullptr, 0, nullptr, nullptr);
        if (!bytes)
            return nullptr;
        auto* result = static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, bytes));
        if (!result)
            return nullptr;
        if (!WideCharToMultiByte(codePage, 0, mapped.data(), mappedLength, result, bytes, nullptr, nullptr)) {
            HeapFree(GetProcessHeap(), 0, result);
            return nullptr;
        }
        length = bytes;
        return result;
    } catch (...) {
        return nullptr;
    }
}

template<DWORD Flags, const GUID* Service = nullptr>
void ConvertSelection() {
    SendMessageW(host._nppHandle, MPM_MAPSELECTION, 0, reinterpret_cast<LPARAM>(&MapSelection<Flags, Service>));
}

void ConvertHanja() {
    SendMessageW(host._scintillaMainHandle, WM_IME_KEYDOWN, VK_HANJA, 0);
}

FuncItem commands[]{
    {{}, ConvertSelection<LCMAP_FULLWIDTH>},
    {{}, ConvertSelection<LCMAP_HALFWIDTH>},
    {},
    {{}, ConvertSelection<LCMAP_SIMPLIFIED_CHINESE, &ELS_GUID_TRANSLITERATION_HANT_TO_HANS>},
    {{}, ConvertSelection<LCMAP_TRADITIONAL_CHINESE, &ELS_GUID_TRANSLITERATION_HANS_TO_HANT>},
    {},
    {{}, ConvertSelection<LCMAP_HIRAGANA>},
    {{}, ConvertSelection<LCMAP_KATAKANA>},
    {},
    {{}, ConvertHanja},
    {{}, ConvertSelection<0, &hangulDecomposition>},
    {},
    {{}, ConvertSelection<0, &ELS_GUID_TRANSLITERATION_BENGALI_TO_LATIN>},
    {{}, ConvertSelection<0, &ELS_GUID_TRANSLITERATION_CYRILLIC_TO_LATIN>},
    {{}, ConvertSelection<0, &ELS_GUID_TRANSLITERATION_DEVANAGARI_TO_LATIN>},
    {{}, ConvertSelection<0, &ELS_GUID_TRANSLITERATION_MALAYALAM_TO_LATIN>},
};
constexpr UINT labels[]{
    IDM_EDIT_MAP_FULLWIDTH, IDM_EDIT_MAP_HALFWIDTH, 0,
    IDM_EDIT_MAP_SIMPLIFIED_CHINESE, IDM_EDIT_MAP_TRADITIONAL_CHINESE, 0,
    IDM_EDIT_MAP_HIRAGANA, IDM_EDIT_MAP_KATAKANA, 0,
    IDM_EDIT_MAP_HANJA_HANGUL, IDM_EDIT_MAP_HANGUL_DECOMPOSITION, 0,
    IDM_EDIT_MAP_BENGALI_LATIN, IDM_EDIT_MAP_CYRILLIC_LATIN, IDM_EDIT_MAP_DEVANAGARI_LATIN, IDM_EDIT_MAP_MALAYALAM_LATIN,
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
extern "C" __declspec(dllexport) BOOL multipadCommandEnabled(UINT) {
    return SendMessageW(host._nppHandle, NPPM_GETCURRENTBUFFERID, 0, 0) &&
        !SendMessageW(host._scintillaMainHandle, SCI_GETREADONLY, 0, 0) &&
        !SendMessageW(host._scintillaMainHandle, SCI_GETSELECTIONEMPTY, 0, 0);
}
extern "C" __declspec(dllexport) void beNotified(SCNotification*) {}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
