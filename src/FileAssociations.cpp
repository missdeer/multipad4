#include "App.h"
#include "FileAssociations.h"
#include "LanguageConfiguration.h"
#include "resource.h"
#include <algorithm>
#include <cwctype>
#include <set>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
constexpr wchar_t AppName[] = L"MultiPad4";
constexpr wchar_t Classes[] = L"Software\\Classes\\";
constexpr wchar_t Capabilities[] = L"Software\\MultiPad4\\Capabilities";
constexpr wchar_t FileTypes[] = L"Software\\MultiPad4\\Capabilities\\FileAssociations";
constexpr wchar_t RegisteredApps[] = L"Software\\RegisteredApplications";

std::wstring ProgId(const std::wstring& extension) {
    return L"MultiPad4.File" + extension;
}

int IconResourceId(std::wstring_view extension) {
    static constexpr struct {
        std::wstring_view extension;
        int id;
    } icons[] = {
#include "FileIconResources.inc"
    };
    for (const auto& icon : icons) {
        if (icon.extension == extension) return icon.id;
    }
    return IDI_DEFAULT_FILE;
}

std::wstring Quoted(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

std::wstring DefaultIconValue(const std::filesystem::path& executable, const std::wstring& extension) {
    return Quoted(executable) + L"," + std::to_wstring(-IconResourceId(extension));
}

std::wstring DefaultIconKey(const std::wstring& extension) {
    return Classes + ProgId(extension) + L"\\DefaultIcon";
}

bool ValidExtension(const std::wstring& extension) {
    constexpr std::wstring_view alphanumeric = L"abcdefghijklmnopqrstuvwxyz0123456789";
    return extension.size() > 1 && extension.size() <= 80 && extension.front() == L'.' &&
        alphanumeric.find(extension[1]) != std::wstring_view::npos &&
        extension.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789_-", 1) == std::wstring::npos;
}

LSTATUS SetString(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    return RegSetKeyValueW(root, key.c_str(), name, REG_SZ, value.c_str(),
        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

LSTATUS DeleteValue(HKEY root, const std::wstring& key, const wchar_t* name) {
    const LSTATUS status = RegDeleteKeyValueW(root, key.c_str(), name);
    return (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) ? ERROR_SUCCESS : status;
}

bool Registered(const std::wstring& extension) {
    wchar_t value[128]{};
    DWORD bytes = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, FileTypes, extension.c_str(), RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS &&
        value == ProgId(extension);
}

bool IsDefault(const std::wstring& extension) {
    wchar_t value[128]{};
    DWORD length = _countof(value);
    return AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, extension.c_str(), nullptr, value, &length) == S_OK &&
        _wcsicmp(value, ProgId(extension).c_str()) == 0;
}

bool NeedsIconUpdate(const std::filesystem::path& executable, const std::wstring& extension) {
    const auto expected = DefaultIconValue(executable, extension);
    std::wstring value(expected.size() + 1, L'\0');
    DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    return RegGetValueW(HKEY_CURRENT_USER, DefaultIconKey(extension).c_str(), nullptr, RRF_RT_REG_SZ,
        nullptr, value.data(), &bytes) != ERROR_SUCCESS || expected != value.c_str();
}

const wchar_t* Text(const wchar_t* english, const wchar_t* chinese) {
    return PRIMARYLANGID(GetThreadUILanguage()) == LANG_CHINESE ? chinese : english;
}

void Lower(std::wstring& text) {
    for (auto& character : text) character = static_cast<wchar_t>(towlower(character));
}

struct FileType {
    std::wstring extension, languages;
    bool registered = false, checked = false, isDefault = false, needsIconUpdate = false;
    int image = -1;
};

std::vector<FileType> CatalogFileTypes() {
    std::map<std::wstring, std::set<std::wstring>> types;
    for (const auto* entry : LanguageCatalog()) {
        const auto& language = BuiltinLanguage(entry->id);
        auto add = [&](const std::wstring& extensions) {
            std::wistringstream list(extensions);
            std::wstring token;
            while (std::getline(list, token, L';')) {
                const auto first = token.find_first_not_of(L" \t\r\n");
                if (first == std::wstring::npos) continue;
                token = token.substr(first, token.find_last_not_of(L" \t\r\n") - first + 1);
                Lower(token);
                if (token.front() != L'.') token.insert(0, 1, L'.');
                if (ValidExtension(token)) types[token].insert(language.name);
            }
        };
        add(language.extensions);
        for (const auto& variant : language.variants) add(variant.extensions);
    }
    std::vector<FileType> rows;
    rows.reserve(types.size());
    for (const auto& [extension, languages] : types) {
        FileType row{extension, {}};
        for (const auto& language : languages) {
            if (!row.languages.empty()) row.languages += L" / ";
            row.languages += language;
        }
        row.checked = row.registered = Registered(extension);
        rows.push_back(std::move(row));
    }
    return rows;
}

struct Dialog {
    std::filesystem::path executable;
    std::vector<FileType> rows;
    HWND list = nullptr;
    bool updating = false;
};

void UpdateStatus(HWND window, const Dialog& dialog) {
    const auto selected = std::count_if(dialog.rows.begin(), dialog.rows.end(), [](const FileType& row) { return row.checked; });
    const bool pending = std::any_of(dialog.rows.begin(), dialog.rows.end(), [](const FileType& row) {
        return row.checked != row.registered || row.needsIconUpdate;
    });
    const auto status = std::to_wstring(selected) + L" / " + std::to_wstring(dialog.rows.size()) + Text(L" checked", L" \u9879\u5df2\u52fe\u9009");
    SetDlgItemTextW(window, IDC_ASSOCIATION_STATUS, status.c_str());
    EnableWindow(GetDlgItem(window, IDOK), pending);
    EnableWindow(GetDlgItem(window, IDC_ASSOCIATION_DEFAULTS), pending || selected != 0);
}

void LoadImages(Dialog& dialog) {
    const auto size = GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForWindow(dialog.list));
    HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR32, static_cast<int>(dialog.rows.size()), 1);
    if (!images) return;
    for (auto& row : dialog.rows) {
        const int id = IconResourceId(row.extension);
        HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id),
            IMAGE_ICON, size, size, 0));
        row.image = icon ? ImageList_AddIcon(images, icon) : -1;
        if (icon) DestroyIcon(icon);
    }
    if (HIMAGELIST previous = ListView_SetImageList(dialog.list, images, LVSIL_SMALL)) ImageList_Destroy(previous);
}

void SizeColumns(HWND list) {
    RECT rect{};
    GetClientRect(list, &rect);
    const int width = rect.right - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(list));
    ListView_SetColumnWidth(list, 0, width * 27 / 100);
    ListView_SetColumnWidth(list, 1, width * 47 / 100);
    ListView_SetColumnWidth(list, 2, width - width * 27 / 100 - width * 47 / 100);
}

void Populate(HWND window, Dialog& dialog) {
    wchar_t input[128]{};
    GetDlgItemTextW(window, IDC_ASSOCIATION_FILTER, input, _countof(input));
    std::wstring filter = input;
    Lower(filter);
    dialog.updating = true;
    SendMessageW(dialog.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(dialog.list);
    for (size_t index = 0; index != dialog.rows.size(); ++index) {
        auto& row = dialog.rows[index];
        auto languages = row.languages;
        Lower(languages);
        if (row.extension.find(filter) == std::wstring::npos && languages.find(filter) == std::wstring::npos) continue;
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        item.iItem = ListView_GetItemCount(dialog.list);
        item.pszText = row.extension.data();
        item.iImage = row.image;
        item.lParam = static_cast<LPARAM>(index);
        const int inserted = ListView_InsertItem(dialog.list, &item);
        if (inserted < 0) continue;
        ListView_SetItemText(dialog.list, inserted, 1, row.languages.data());
        ListView_SetItemText(dialog.list, inserted, 2, const_cast<wchar_t*>(row.isDefault ? AppName : L""));
        ListView_SetCheckState(dialog.list, inserted, row.checked);
    }
    SendMessageW(dialog.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(dialog.list, nullptr, TRUE);
    dialog.updating = false;
    UpdateStatus(window, dialog);
}

void RefreshDefaults(Dialog& dialog) {
    for (auto& row : dialog.rows) {
        row.isDefault = IsDefault(row.extension);
        row.needsIconUpdate = (row.registered || row.isDefault) && NeedsIconUpdate(dialog.executable, row.extension);
    }
    for (int index = 0; index < ListView_GetItemCount(dialog.list); ++index) {
        LVITEMW item{};
        item.mask = LVIF_PARAM;
        item.iItem = index;
        if (!ListView_GetItem(dialog.list, &item)) continue;
        const auto& row = dialog.rows[static_cast<size_t>(item.lParam)];
        ListView_SetItemText(dialog.list, index, 2, const_cast<wchar_t*>(row.isDefault ? AppName : L""));
    }
}

INT_PTR CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto& dialog = *reinterpret_cast<Dialog*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        dialog.list = GetDlgItem(window, IDC_ASSOCIATION_LIST);
        SetWindowTextW(window, Text(L"File Associations", L"\u6587\u4ef6\u5173\u8054"));
        SetDlgItemTextW(window, IDC_ASSOCIATION_FILTER_LABEL, Text(L"&Filter:", L"\u7b5b\u9009(&F)\uff1a"));
        SetDlgItemTextW(window, IDC_ASSOCIATION_SELECT, Text(L"Select all", L"\u5168\u9009"));
        SetDlgItemTextW(window, IDC_ASSOCIATION_CLEAR, Text(L"Select none", L"\u5168\u4e0d\u9009"));
        SetDlgItemTextW(window, IDC_ASSOCIATION_INVERT, Text(L"Invert selection", L"\u53cd\u9009"));
        SetDlgItemTextW(window, IDC_ASSOCIATION_DEFAULTS, Text(L"Save and open Default Apps...", L"\u4fdd\u5b58\u5e76\u6253\u5f00\u9ed8\u8ba4\u5e94\u7528"));
        SetDlgItemTextW(window, IDC_ASSOCIATION_DEFAULTS_HELP,
            Text(L"To make MultiPad4 open a file type by default, choose it for that extension in Windows Settings.",
                L"\u8981\u8ba9 MultiPad4 \u9ed8\u8ba4\u6253\u5f00\u67d0\u7c7b\u6587\u4ef6\uff0c\u8bf7\u5728 Windows \u8bbe\u7f6e\u4e2d\u4e3a\u8be5\u6269\u5c55\u540d\u9009\u62e9 MultiPad4\u3002"));
        SetDlgItemTextW(window, IDOK, Text(L"Save registration", L"\u4fdd\u5b58\u6ce8\u518c"));
        SetDlgItemTextW(window, IDCANCEL, Text(L"Close", L"\u5173\u95ed"));
        SendDlgItemMessageW(window, IDC_ASSOCIATION_FILTER, EM_SETLIMITTEXT, 127, 0);
        ListView_SetExtendedListViewStyle(dialog.list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        const wchar_t* columns[] = {Text(L"Extension", L"\u6269\u5c55\u540d"), Text(L"Language", L"\u8bed\u8a00"), Text(L"Windows default", L"Windows \u9ed8\u8ba4\u5e94\u7528")};
        for (int index = 0; index < _countof(columns); ++index) {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT;
            column.pszText = const_cast<wchar_t*>(columns[index]);
            ListView_InsertColumn(dialog.list, index, &column);
        }
        SizeColumns(dialog.list);
        LoadImages(dialog);
        Populate(window, dialog);
        CenterWindow(window, GetParent(window));
        return TRUE;
    }
    auto* dialog = reinterpret_cast<Dialog*>(GetWindowLongPtrW(window, DWLP_USER));
    if (!dialog) return FALSE;
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE) {
        RefreshDefaults(*dialog);
        UpdateStatus(window, *dialog);
    }
    if (message == WM_DPICHANGED) {
        LoadImages(*dialog);
        SizeColumns(dialog->list);
        Populate(window, *dialog);
    }
    if (message == WM_NOTIFY && wParam == IDC_ASSOCIATION_LIST && !dialog->updating) {
        const auto& change = *reinterpret_cast<const NMLISTVIEW*>(lParam);
        if (change.hdr.code == LVN_ITEMCHANGED && change.iItem >= 0 && (change.uChanged & LVIF_STATE) &&
            ((change.uOldState ^ change.uNewState) & LVIS_STATEIMAGEMASK)) {
            dialog->rows[static_cast<size_t>(change.lParam)].checked = (change.uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2);
            UpdateStatus(window, *dialog);
        }
        return FALSE;
    }
    if (message != WM_COMMAND) return FALSE;
    switch (LOWORD(wParam)) {
    case IDC_ASSOCIATION_FILTER:
        if (HIWORD(wParam) == EN_CHANGE) Populate(window, *dialog);
        return TRUE;
    case IDC_ASSOCIATION_SELECT: case IDC_ASSOCIATION_CLEAR: case IDC_ASSOCIATION_INVERT: {
        const auto command = LOWORD(wParam);
        for (auto& row : dialog->rows)
            row.checked = command == IDC_ASSOCIATION_INVERT ? !row.checked : command == IDC_ASSOCIATION_SELECT;
        dialog->updating = true;
        for (int index = 0; index < ListView_GetItemCount(dialog->list); ++index) {
            LVITEMW item{};
            item.mask = LVIF_PARAM;
            item.iItem = index;
            if (ListView_GetItem(dialog->list, &item))
                ListView_SetCheckState(dialog->list, index, dialog->rows[static_cast<size_t>(item.lParam)].checked);
        }
        dialog->updating = false;
        UpdateStatus(window, *dialog);
        return TRUE;
    }
    case IDOK: case IDC_ASSOCIATION_DEFAULTS: {
        LSTATUS status = ERROR_SUCCESS;
        std::wstring failed;
        for (auto& row : dialog->rows) {
            if (row.checked != row.registered || (row.checked && row.needsIconUpdate)) {
                status = SetFileAssociation(HKEY_CURRENT_USER, row.extension, dialog->executable, row.checked);
                if (status == ERROR_SUCCESS) row.registered = row.checked;
            }
            if (status == ERROR_SUCCESS && !row.checked && row.isDefault && row.needsIconUpdate) {
                status = SetString(HKEY_CURRENT_USER, DefaultIconKey(row.extension), nullptr,
                    DefaultIconValue(dialog->executable, row.extension));
            }
            if (status != ERROR_SUCCESS) {
                failed = row.extension;
                break;
            }
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        RefreshDefaults(*dialog);
        UpdateStatus(window, *dialog);
        if (status != ERROR_SUCCESS) {
            const auto error = std::wstring(Text(L"Could not update association for ", L"\u65e0\u6cd5\u66f4\u65b0\u6587\u4ef6\u5173\u8054\uff1a")) +
                failed + L" (" + std::to_wstring(status) + L").";
            MessageBoxW(window, error.c_str(), AppName, MB_OK | MB_ICONERROR);
        } else if (LOWORD(wParam) == IDC_ASSOCIATION_DEFAULTS) {
            const bool registered = std::any_of(dialog->rows.begin(), dialog->rows.end(), [](const FileType& row) { return row.registered; });
            const auto uri = registered ? L"ms-settings:defaultapps?registeredAppUser=MultiPad4" : L"ms-settings:defaultapps";
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                MessageBoxW(window, Text(L"Registration was saved, but Windows Default Apps could not be opened.",
                    L"\u6ce8\u518c\u5df2\u4fdd\u5b58\uff0c\u4f46\u65e0\u6cd5\u6253\u5f00 Windows \u9ed8\u8ba4\u5e94\u7528\u3002"), AppName, MB_OK | MB_ICONERROR);
        }
        return TRUE;
    }
    default: return FALSE;
    }
}
}

int DocumentIconResourceId(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    if (extension.empty()) {
        auto filename = path.filename().wstring();
        if (filename.starts_with(L'.')) extension = std::move(filename);
    }
    Lower(extension);
    return IconResourceId(extension);
}

LSTATUS SetFileAssociation(HKEY root, const std::wstring& extension, const std::filesystem::path& executable, bool enable) {
    if (!ValidExtension(extension) || !executable.is_absolute()) return ERROR_INVALID_PARAMETER;
    const auto progId = ProgId(extension);
    const auto openWith = Classes + extension + L"\\OpenWithProgids";
    if (!enable) {
        if (const auto status = DeleteValue(root, openWith, progId.c_str()); status != ERROR_SUCCESS) return status;
        if (const auto status = DeleteValue(root, FileTypes, extension.c_str()); status != ERROR_SUCCESS) return status;
        DWORD valueCount = 0;
        HKEY key = nullptr;
        const auto opened = RegOpenKeyExW(root, FileTypes, 0, KEY_QUERY_VALUE, &key);
        if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) return ERROR_SUCCESS;
        if (opened != ERROR_SUCCESS) return opened;
        const auto status = RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &valueCount, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(key);
        return status == ERROR_SUCCESS && !valueCount ? DeleteValue(root, RegisteredApps, AppName) : status;
    }

    const auto classKey = Classes + progId;
    if (const auto status = SetString(root, classKey, nullptr, std::wstring(AppName) + L" " + extension + L" file"); status != ERROR_SUCCESS) return status;
    if (const auto status = SetString(root, classKey + L"\\shell\\open\\command", nullptr, Quoted(executable) + L" \"%1\""); status != ERROR_SUCCESS) return status;
    if (const auto status = SetString(root, DefaultIconKey(extension), nullptr, DefaultIconValue(executable, extension)); status != ERROR_SUCCESS) return status;
    if (const auto status = SetString(root, Capabilities, L"ApplicationName", AppName); status != ERROR_SUCCESS) return status;
    if (const auto status = SetString(root, Capabilities, L"ApplicationDescription", L"MultiPad4 text and source code editor"); status != ERROR_SUCCESS) return status;
    if (const auto status = SetString(root, openWith, progId.c_str(), L""); status != ERROR_SUCCESS) return status;
    if (const auto status = SetString(root, RegisteredApps, AppName, Capabilities); status != ERROR_SUCCESS) return status;
    return SetString(root, FileTypes, extension.c_str(), progId);
}

void ShowFileAssociations(HWND owner) {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, _countof(executable));
    if (!length || length == _countof(executable)) throw std::runtime_error("Cannot locate the MultiPad4 executable.");
    Dialog dialog{executable, CatalogFileTypes()};
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_FILE_ASSOCIATIONS), owner,
        Procedure, reinterpret_cast<LPARAM>(&dialog)) == -1) throw std::runtime_error("Cannot open File Associations.");
}
