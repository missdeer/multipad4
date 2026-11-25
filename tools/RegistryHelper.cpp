#include "SystemIntegration.h"
#include <string_view>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int) {
    const std::wstring_view action(commandLine);
    if (action != L"--enable" && action != L"--disable") return ERROR_INVALID_PARAMETER;

    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, _countof(module));
    if (!length) return static_cast<int>(GetLastError());
    if (length >= _countof(module)) return ERROR_INSUFFICIENT_BUFFER;

    // Always register the adjacent editor. The caller supplies no path or registry data.
    std::wstring executable(module, length);
    executable.replace(executable.find_last_of(L'\\') + 1, std::wstring::npos, MULTIPAD4_EDITOR_FILENAME);
    const bool enable = action == L"--enable";
    if (enable) {
        const DWORD attributes = GetFileAttributesW(executable.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return static_cast<int>(GetLastError());
        if (attributes & FILE_ATTRIBUTE_DIRECTORY) return ERROR_FILE_NOT_FOUND;
    }
    return ApplyNotepadReplacement(HKEY_LOCAL_MACHINE, NotepadKey, executable, enable, KEY_WOW64_64KEY);
}
