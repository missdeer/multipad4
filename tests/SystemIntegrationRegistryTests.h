#pragma once
#include "../src/SystemIntegration.h"
#include "../src/FileAssociations.h"
#include "../src/resource.h"
#include <shellapi.h>
#include <cstring>
#include <stdexcept>
#include <source_location>

inline void RunFileAssociationRegistryTests() {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("File association assertion failed at line " + std::to_string(where.line()));
    };
    struct Fixture {
        std::wstring path = L"Software\\MultiPad4-FileAssociationTests-" + std::to_wstring(GetCurrentProcessId());
        HKEY key = nullptr;
        ~Fixture() {
            if (key) RegCloseKey(key);
            RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        }
    } fixture;
    require(RegCreateKeyExW(HKEY_CURRENT_USER, fixture.path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
        nullptr, &fixture.key, nullptr) == ERROR_SUCCESS);
    auto read = [&](const wchar_t* key, const wchar_t* name, const wchar_t* expected) {
        wchar_t value[1024]{};
        DWORD bytes = sizeof(value);
        const auto status = RegGetValueW(fixture.key, key, name, RRF_RT_REG_SZ, nullptr, value, &bytes);
        if (expected) require(status == ERROR_SUCCESS && wcscmp(value, expected) == 0);
        else require(status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND);
    };
    const wchar_t* extension = L"Software\\Classes\\.cpp";
    const wchar_t* openWith = L"Software\\Classes\\.cpp\\OpenWithProgids";
    const wchar_t* choices = L"Software\\MultiPad4\\Capabilities\\FileAssociations";
    const wchar_t* apps = L"Software\\RegisteredApplications";
    const wchar_t* progId = L"MultiPad4.File.cpp";
    const wchar_t other[] = L"Other.Editor";
    require(RegSetKeyValueW(fixture.key, extension, nullptr, REG_SZ, other, sizeof(other)) == ERROR_SUCCESS);
    require(RegSetKeyValueW(fixture.key, openWith, other, REG_SZ, L"", sizeof(wchar_t)) == ERROR_SUCCESS);
    wchar_t filename[32768]{};
    require(GetModuleFileNameW(nullptr, filename, _countof(filename)) != 0);
    const std::filesystem::path executable = filename;
    const auto module = GetModuleHandleW(nullptr);
    const auto group = FindResourceW(module, MAKEINTRESOURCEW(IDR_MAINFRAME), RT_GROUP_ICON);
    require(group != nullptr);
    const auto directory = LockResource(LoadResource(module, group));
    require(directory != nullptr);
    for (const int size : {16, 32}) {
        const int id = LookupIconIdFromDirectoryEx(static_cast<PBYTE>(directory), TRUE, size, size, LR_DEFAULTCOLOR);
        require(id != 0);
        const auto frame = FindResourceW(module, MAKEINTRESOURCEW(id), RT_ICON);
        const auto english = FindResourceExW(module, RT_ICON, MAKEINTRESOURCEW(id), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US));
        require(frame && english);
        const auto bytes = SizeofResource(module, english);
        require(bytes && SizeofResource(module, frame) == bytes);
        require(std::memcmp(LockResource(LoadResource(module, frame)), LockResource(LoadResource(module, english)), bytes) == 0);
    }
    HICON icon = nullptr;
    require(ExtractIconExW(filename, -46061, nullptr, &icon, 1) == 1 && icon != nullptr);
    DestroyIcon(icon);
    require(SetFileAssociation(fixture.key, L".cpp\\bad", executable, true) == ERROR_INVALID_PARAMETER);
    require(SetFileAssociation(fixture.key, L".cpp", executable, true) == ERROR_SUCCESS);
    require(SetFileAssociation(fixture.key, L".py", executable, true) == ERROR_SUCCESS);
    read(choices, L".cpp", progId);
    read(openWith, progId, L"");
    read(apps, L"MultiPad4", L"Software\\MultiPad4\\Capabilities");
    const wchar_t* iconKey = L"Software\\Classes\\MultiPad4.File.cpp\\DefaultIcon";
    const auto embeddedIcon = L"\"" + executable.wstring() + L"\",-46061";
    read(iconKey, nullptr, embeddedIcon.c_str());
    constexpr wchar_t looseIcon[] = L"\"C:\\MultiPad4\\res\\icons\\file-cpp.ico\",0";
    require(RegSetKeyValueW(fixture.key, iconKey, nullptr, REG_SZ, looseIcon, sizeof(looseIcon)) == ERROR_SUCCESS);
    require(SetFileAssociation(fixture.key, L".cpp", executable, true) == ERROR_SUCCESS);
    read(iconKey, nullptr, embeddedIcon.c_str());
    read(L"Software\\Classes\\MultiPad4.File.cpp\\shell\\open\\command", nullptr, (L"\"" + executable.wstring() + L"\" \"%1\"").c_str());
    require(SetFileAssociation(fixture.key, L".cpp", executable, false) == ERROR_SUCCESS);
    read(choices, L".cpp", nullptr);
    read(openWith, progId, nullptr);
    read(openWith, other, L"");
    read(extension, nullptr, other);
    read(L"Software\\Classes\\MultiPad4.File.cpp\\shell\\open\\command", nullptr, (L"\"" + executable.wstring() + L"\" \"%1\"").c_str());
    read(apps, L"MultiPad4", L"Software\\MultiPad4\\Capabilities");

    const wchar_t* defaultIconKey = L"Software\\Classes\\MultiPad4.File.multipad4-test\\DefaultIcon";
    const auto executableIcon = L"\"" + executable.wstring() + L"\",";
    const auto oldDefaultIcon = executableIcon + L"0";
    require(RegSetKeyValueW(fixture.key, defaultIconKey, nullptr, REG_SZ, oldDefaultIcon.c_str(),
        static_cast<DWORD>((oldDefaultIcon.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    require(SetFileAssociation(fixture.key, L".multipad4-test", executable, true) == ERROR_SUCCESS);
    read(defaultIconKey, nullptr, (executableIcon + std::to_wstring(-IDI_DEFAULT_FILE)).c_str());
    icon = nullptr;
    require(ExtractIconExW(filename, -IDI_DEFAULT_FILE, nullptr, &icon, 1) == 1 && icon != nullptr);
    DestroyIcon(icon);
    require(SetFileAssociation(fixture.key, L".multipad4-test", executable, false) == ERROR_SUCCESS);

    require(SetFileAssociation(fixture.key, L".py", executable, false) == ERROR_SUCCESS);
    require(SetFileAssociation(fixture.key, L".py", executable, false) == ERROR_SUCCESS);
    read(apps, L"MultiPad4", nullptr);
}

// Uses only a disposable HKCU branch. Never invokes the dialog or touches IFEO.
inline void RunSystemIntegrationRegistryTests() {
    auto require = [](bool ok, std::source_location where = std::source_location::current()) {
        if (!ok) throw std::runtime_error("Notepad registry assertion failed at line " + std::to_string(where.line()));
    };
    struct Fixture {
        std::wstring root = L"Software\\MultiPad4-Tests-" + std::to_wstring(GetCurrentProcessId());
        std::wstring path = root + L"\\NotepadReplacement";
        HKEY key = nullptr;
        ~Fixture() {
            if (key) RegCloseKey(key);
            RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
            RegDeleteKeyW(HKEY_CURRENT_USER, root.c_str()); // succeeds only if empty
        }
    } fixture;
    require(RegCreateKeyExW(HKEY_CURRENT_USER, fixture.path.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
        nullptr, &fixture.key, nullptr) == ERROR_SUCCESS);
    // Read the helper's embedded manifest without launching an elevated process.
    wchar_t module[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, module, _countof(module));
    require(length && length < _countof(module));
    const auto helper = std::filesystem::path(module).replace_filename(NotepadReplacementHelper);
    ACTCTXW activation{};
    activation.cbSize = sizeof(activation);
    activation.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
    activation.lpSource = helper.c_str();
    activation.lpResourceName = MAKEINTRESOURCEW(1);
    const HANDLE context = CreateActCtxW(&activation);
    require(context != INVALID_HANDLE_VALUE);
    ACTIVATION_CONTEXT_RUN_LEVEL_INFORMATION runLevel{};
    const BOOL queried = QueryActCtxW(0, context, nullptr, RunlevelInformationInActivationContext,
        &runLevel, sizeof(runLevel), nullptr);
    ReleaseActCtx(context);
    require(queried && runLevel.RunLevel == ACTCTX_RUN_LEVEL_REQUIRE_ADMIN && !runLevel.UiAccess);

    const std::wstring executable = L"C:\\MultiPad4 Registry Test\\multipad4.exe";
    const std::wstring command = L"\"" + executable + L"\" /z";
    auto setDword = [&](const wchar_t* name, DWORD value) {
        require(RegSetValueExW(fixture.key, name, 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS);
    };
    auto setString = [&](const wchar_t* name, const std::wstring& value) {
        require(RegSetValueExW(fixture.key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    };
    auto dword = [&](const wchar_t* name) {
        DWORD value = 0, type = 0, bytes = sizeof(value);
        require(RegQueryValueExW(fixture.key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &bytes) == ERROR_SUCCESS);
        require(type == REG_DWORD && bytes == sizeof(value)); return value;
    };
    auto string = [&](const wchar_t* name) {
        wchar_t value[512]{}; DWORD type = 0, bytes = sizeof(value);
        require(RegQueryValueExW(fixture.key, name, nullptr, &type, reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS);
        require(type == REG_SZ); return std::wstring(value);
    };
    auto absent = [&](const wchar_t* name) {
        require(RegQueryValueExW(fixture.key, name, nullptr, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND);
    };
    auto apply = [&](bool enable) {
        return ApplyNotepadReplacement(HKEY_CURRENT_USER, fixture.path.c_str(), executable, enable);
    };

    setDword(L"UseFilter", 1);
    setDword(L"AppExecutionAliasRedirect", 1);
    setString(L"AppExecutionAliasRedirectPackages", L"*");
    setString(nullptr, L"Unrelated default value");
    HKEY child = nullptr;
    require(RegCreateKeyExW(fixture.key, L"0", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &child, nullptr) == ERROR_SUCCESS);
    const DWORD marker = 42;
    const LSTATUS childStatus = RegSetValueExW(child, L"Unrelated", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&marker), sizeof(marker));
    RegCloseKey(child); require(childStatus == ERROR_SUCCESS);

    require(apply(true) == ERROR_SUCCESS);
    require(string(L"Debugger") == command && dword(L"UseFilter") == 0);
    absent(L"AppExecutionAliasRedirect"); absent(L"AppExecutionAliasRedirectPackages");
    require(apply(true) == ERROR_SUCCESS); // repeated enable is safe
    require(apply(false) == ERROR_SUCCESS);
    absent(L"Debugger");
    require(dword(L"UseFilter") == 1 && dword(L"AppExecutionAliasRedirect") == 1);
    require(string(L"AppExecutionAliasRedirectPackages") == L"*");
    require(string(nullptr) == L"Unrelated default value");
    DWORD actualMarker = 0, markerBytes = sizeof(actualMarker);
    require(RegGetValueW(fixture.key, L"0", L"Unrelated", RRF_RT_REG_DWORD, nullptr, &actualMarker, &markerBytes) == ERROR_SUCCESS);
    require(actualMarker == marker);

    const std::wstring previous = L"\"C:\\Old MultiPad4\\MultiPad4.EXE\" /Z";
    setString(L"Debugger", previous);
    require(apply(false) == ERROR_SUCCESS && string(L"Debugger") == previous && dword(L"UseFilter") == 1);
    require(apply(true) == ERROR_SUCCESS && string(L"Debugger") == command && dword(L"UseFilter") == 0);
    require(apply(false) == ERROR_SUCCESS);
    absent(L"Debugger");

    for (const auto* invalid : {L"\"multipad4.exe\" /z", L"\"C:\\Old MultiPad4\\multipad4.exe\" /z extra"}) {
        setString(L"Debugger", invalid);
        require(apply(true) == ERROR_ALREADY_EXISTS && string(L"Debugger") == invalid);
    }

    // Substring ownership used to mistake wrapper commands for our replacement.
    const std::wstring foreign = L"\"C:\\Other Editor\\wrapper.exe\" " + command;
    setString(L"Debugger", foreign);
    setDword(L"UseFilter", 7);
    setDword(L"AppExecutionAliasRedirect", 8);
    setString(L"AppExecutionAliasRedirectPackages", L"other-package");
    require(apply(false) == ERROR_SUCCESS);
    require(apply(true) == ERROR_ALREADY_EXISTS);
    require(string(L"Debugger") == foreign && dword(L"UseFilter") == 7);
    require(dword(L"AppExecutionAliasRedirect") == 8 && string(L"AppExecutionAliasRedirectPackages") == L"other-package");
    setDword(L"Debugger", 99); // non-string ownership also remains untouched
    require(apply(false) == ERROR_SUCCESS && apply(true) == ERROR_ALREADY_EXISTS);
    require(dword(L"Debugger") == 99);

    // RegSetValueEx checks the granted access of this real read-only HKCU handle.
    setString(L"Debugger", command);
    HKEY readOnly = nullptr;
    require(RegOpenKeyExW(HKEY_CURRENT_USER, fixture.path.c_str(), 0, KEY_QUERY_VALUE, &readOnly) == ERROR_SUCCESS);
    const LSTATUS enableStatus = ApplyNotepadReplacementToKey(readOnly, executable, true);
    const LSTATUS disableStatus = ApplyNotepadReplacementToKey(readOnly, executable, false);
    RegCloseKey(readOnly);
    require(enableStatus == ERROR_ACCESS_DENIED && disableStatus == ERROR_ACCESS_DENIED);
    require(string(L"Debugger") == command && dword(L"UseFilter") == 7);
    require(apply(false) == ERROR_SUCCESS);
    setDword(L"UseFilter", 5);
    require(apply(false) == ERROR_SUCCESS && dword(L"UseFilter") == 5); // no owned replacement, no writes
}
