#include "PluginInterface.h"
#include "Docking.h"
#include "../PluginApi.h"
#include "ProjectLibrary.h"
#include "Resources.h"
#include "FileIO.h"
#include <shobjidl.h>
#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using namespace project;
HINSTANCE instance;
NppData host{};
bool runningCommand = false;
HWND pane = nullptr;
void RefreshPane();

bool PaneShown() { return pane && (GetWindowLongPtrW(pane, GWL_STYLE) & WS_VISIBLE); }

LRESULT Host(UINT message, WPARAM w = 0, LPARAM l = 0) {
    return SendMessageW(host._nppHandle, message, w, l);
}

void Report(HWND owner, const std::exception& error) {
    MessageBoxW(owner, files::FromUtf8(error.what()).c_str(), L"Project", MB_OK | MB_ICONERROR);
}

template<void (*Action)()>
void RunCommand() {
    if (runningCommand) return;
    runningCommand = true;
    try { Action(); }
    catch (const std::exception& error) { Report(host._nppHandle, error); }
    if (PaneShown()) RefreshPane();
    runningCommand = false;
}

fs::path ConfigDir() {
    const auto length = Host(NPPM_GETPLUGINSCONFIGDIR);
    if (length <= 0) throw std::runtime_error("The MultiPad4 plugin configuration folder is unavailable.");
    std::wstring path(length + 1, L'\0');
    if (!Host(NPPM_GETPLUGINSCONFIGDIR, path.size(), reinterpret_cast<LPARAM>(path.data())))
        throw std::runtime_error("Cannot read the plugin configuration folder.");
    path.resize(length);
    fs::create_directories(path);
    return path;
}

struct CurrentLibrary {
    fs::path path;
    Library library;
};

CurrentLibrary LoadCurrent() {
    const auto config = ConfigDir();
    const auto choice = config / LibrarySelection;
    if (!fs::exists(choice)) {
        auto path = config / DefaultLibrary;
        return {path, ReadLibrary(path, true)};
    }
    const auto stored = files::FromUtf8(files::ReadWithEncoding(choice, files::Encoding::Utf8).utf8);
    const fs::path path(stored);
    if (stored.find(L'\0') != std::wstring::npos || !path.is_absolute())
        throw std::runtime_error("Invalid project library location. Choose Open Project Library or Use Default Library.");
    return {path, ReadLibrary(path, false)};
}

template<typename T>
struct ReleaseCom { void operator()(T* object) const { object->Release(); } };
template<typename T> using ComPtr = std::unique_ptr<T, ReleaseCom<T>>;

void Check(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("The Windows file picker could not complete the request.");
}

fs::path ItemPath(IShellItem* item) {
    PWSTR text = nullptr;
    Check(item->GetDisplayName(SIGDN_FILESYSPATH, &text));
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(text, CoTaskMemFree);
    return fs::weakly_canonical(owned.get()).make_preferred();
}

enum class Pick { Files, Folder, Library, NewLibrary };
std::vector<fs::path> PickPaths(HWND owner, Pick kind, const fs::path& initial = {}) {
    const bool save = kind == Pick::NewLibrary;
    IFileDialog* raw = nullptr;
    Check(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_IFileDialog, reinterpret_cast<void**>(&raw)));
    const ComPtr<IFileDialog> picker(raw);
    FILEOPENDIALOGOPTIONS flags = 0;
    Check(picker->GetOptions(&flags));
    flags |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
    if (save) flags &= ~FOS_OVERWRITEPROMPT;
    if (kind == Pick::Folder) flags |= FOS_PICKFOLDERS;
    if (kind == Pick::Files) flags |= FOS_ALLOWMULTISELECT;
    Check(picker->SetOptions(flags));
    if (save || kind == Pick::Library) {
        constexpr COMDLG_FILTERSPEC filter{L"MultiPad4 project libraries (*.mp4projects)", L"*.mp4projects"};
        Check(picker->SetFileTypes(1, &filter));
        Check(picker->SetDefaultExtension(L"mp4projects"));
        Check(picker->SetTitle(save ? L"New Project Library" : L"Open Project Library"));
    } else {
        Check(picker->SetTitle(kind == Pick::Folder ? L"Select Folder" : L"Add Files to Project"));
    }
    if (!initial.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_IShellItem,
            reinterpret_cast<void**>(&folder)))) picker->SetFolder(ComPtr<IShellItem>(folder).get());
    }
    const auto shown = picker->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    Check(shown);

    if (kind != Pick::Files) {
        IShellItem* item = nullptr;
        Check(picker->GetResult(&item));
        return {ItemPath(ComPtr<IShellItem>(item).get())};
    }
    IFileOpenDialog* open = nullptr;
    Check(picker->QueryInterface(IID_IFileOpenDialog, reinterpret_cast<void**>(&open)));
    IShellItemArray* items = nullptr;
    Check(ComPtr<IFileOpenDialog>(open)->GetResults(&items));
    const ComPtr<IShellItemArray> selected(items);
    DWORD count = 0;
    Check(selected->GetCount(&count));
    std::vector<fs::path> paths;
    paths.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        IShellItem* item = nullptr;
        Check(selected->GetItemAt(i, &item));
        paths.push_back(ItemPath(ComPtr<IShellItem>(item).get()));
    }
    return paths;
}

fs::path BufferPath(WPARAM buffer) {
    const auto length = Host(MPM_GETBUFFERFILEPATH, buffer);
    if (length <= 0) return {};
    std::wstring text(length + 1, L'\0');
    if (Host(MPM_GETBUFFERFILEPATH, buffer, reinterpret_cast<LPARAM>(text.data())) != length) return {};
    text.resize(length);
    return fs::path(text).make_preferred();
}

std::vector<WPARAM> OpenBuffers() {
    std::vector<WPARAM> buffers;
    const auto count = Host(NPPM_GETNBOPENFILES, 0, ALL_OPEN_FILES);
    for (LRESULT i = 0; i < count; ++i) buffers.push_back(Host(NPPM_GETBUFFERIDFROMPOS, i));
    return buffers;
}

struct PathOrder {
    bool operator()(const fs::path& left, const fs::path& right) const {
        return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    }
};

void OpenMembers(const std::vector<Member>& members) {
    std::set<fs::path, PathOrder> seen;
    size_t unavailable = 0;
    auto open = [&](const fs::path& path) {
        if (!seen.insert(path).second) return;
        std::error_code error;
        if (!fs::is_regular_file(path, error)) ++unavailable;
        else Host(NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.c_str()));
    };
    for (const auto& member : members) {
        if (!member.folder) { open(member.path); continue; }
        std::error_code error;
        std::set<fs::path, PathOrder> entries;
        const fs::directory_iterator end;
        for (fs::directory_iterator it(member.path, error); !error && it != end; it.increment(error)) {
            std::error_code status;
            if (it->is_regular_file(status)) entries.insert(it->path());
            else if (status) ++unavailable;
        }
        if (error) ++unavailable;
        for (const auto& path : entries) open(path);
    }
    if (unavailable) {
        const auto message = std::to_wstring(unavailable) + L" project file or folder entries were unavailable.";
        MessageBoxW(host._nppHandle, message.c_str(), L"Project", MB_OK | MB_ICONWARNING);
    }
}

INT_PTR ShowDialog(HWND owner, UINT resource, DLGPROC callback, LPARAM context) {
    const auto result = DialogBoxParamW(instance, MAKEINTRESOURCEW(resource), owner, callback, context);
    if (result == -1) throw std::runtime_error("Cannot open the project dialog.");
    return result;
}

void FillList(HWND list, const std::vector<std::wstring>& labels, int index) {
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    const HDC dc = GetDC(list);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
    const auto old = font ? SelectObject(dc, font) : nullptr;
    LONG width = 0;
    for (const auto& label : labels) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SIZE extent{};
        GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &extent);
        width = (std::max)(width, extent.cx);
    }
    if (old) SelectObject(dc, old);
    ReleaseDC(list, dc);
    SendMessageW(list, LB_SETHORIZONTALEXTENT, width + 6, 0);
    SendMessageW(list, LB_SETCURSEL, index, 0);
}

int Selected(HWND dialog, int list) {
    return static_cast<int>(SendDlgItemMessageW(dialog, list, LB_GETCURSEL, 0, 0));
}

std::wstring MemberLabel(const Member& member) {
    std::wstring label = member.folder ? L"[Folder] " : L"";
    if (!member.path.has_filename()) return label + member.path.native();
    return label + member.path.filename().native() + L"  (" + member.path.parent_path().native() + L")";
}

struct ListChoice {
    const wchar_t* title;
    const wchar_t* label;
    std::vector<std::wstring> items;
    int index;
};

INT_PTR CALLBACK ListProc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INITDIALOG) {
        auto& choice = *reinterpret_cast<ListChoice*>(l);
        SetWindowLongPtrW(dialog, DWLP_USER, l);
        SetWindowTextW(dialog, choice.title);
        SetDlgItemTextW(dialog, PROJECT_LABEL, choice.label);
        FillList(GetDlgItem(dialog, PROJECT_LIST), choice.items, choice.index);
        EnableWindow(GetDlgItem(dialog, IDOK), !choice.items.empty());
        return TRUE;
    }
    if (message != WM_COMMAND) return FALSE;
    if (LOWORD(w) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (LOWORD(w) == IDOK || (LOWORD(w) == PROJECT_LIST && HIWORD(w) == LBN_DBLCLK)) {
        const int index = Selected(dialog, PROJECT_LIST);
        if (index >= 0) {
            reinterpret_cast<ListChoice*>(GetWindowLongPtrW(dialog, DWLP_USER))->index = index;
            EndDialog(dialog, IDOK);
        }
        return TRUE;
    }
    return FALSE;
}

struct TextInput { const wchar_t* title; const wchar_t* label; std::wstring value; };
INT_PTR CALLBACK TextProc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INITDIALOG) {
        auto& input = *reinterpret_cast<TextInput*>(l);
        SetWindowLongPtrW(dialog, DWLP_USER, l);
        SetWindowTextW(dialog, input.title);
        SetDlgItemTextW(dialog, PROJECT_LABEL, input.label);
        SendDlgItemMessageW(dialog, PROJECT_INPUT, EM_SETLIMITTEXT, 255, 0);
        SetDlgItemTextW(dialog, PROJECT_INPUT, input.value.c_str());
        return TRUE;
    }
    if (message != WM_COMMAND) return FALSE;
    if (LOWORD(w) == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (LOWORD(w) == IDOK) {
        auto& input = *reinterpret_cast<TextInput*>(GetWindowLongPtrW(dialog, DWLP_USER));
        wchar_t value[256]{};
        GetDlgItemTextW(dialog, PROJECT_INPUT, value, _countof(value));
        try { input.value = value; }
        catch (const std::exception& error) { Report(dialog, error); return TRUE; }
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    return FALSE;
}

struct Manager {
    CurrentLibrary current;
    size_t selected;
    bool changed = false;

    Project& Entry() { return current.library.projects[selected]; }

    void UpdateButtons(HWND dialog) {
        const int member = Selected(dialog, PROJECT_MEMBERS);
        const auto count = Entry().members.size();
        EnableWindow(GetDlgItem(dialog, PROJECT_RENAME), selected > 0);
        EnableWindow(GetDlgItem(dialog, PROJECT_REMOVE), selected > 0);
        EnableWindow(GetDlgItem(dialog, PROJECT_UP), selected > 1);
        EnableWindow(GetDlgItem(dialog, PROJECT_DOWN), selected > 0 && selected + 1 < current.library.projects.size());
        EnableWindow(GetDlgItem(dialog, PROJECT_REMOVE_MEMBER), member >= 0);
        EnableWindow(GetDlgItem(dialog, PROJECT_MEMBER_UP), member > 0);
        EnableWindow(GetDlgItem(dialog, PROJECT_MEMBER_DOWN), member >= 0 && static_cast<size_t>(member + 1) < count);
        EnableWindow(GetDlgItem(dialog, PROJECT_SORT), count > 1);
        EnableWindow(GetDlgItem(dialog, PROJECT_CLEAR_BASE), !Entry().base.empty());
    }

    void ShowEntry(HWND dialog, int member = -1) {
        std::vector<std::wstring> members;
        for (const auto& entry : Entry().members) members.push_back(MemberLabel(entry));
        FillList(GetDlgItem(dialog, PROJECT_MEMBERS), members, member);
        SetDlgItemTextW(dialog, PROJECT_BASE, Entry().base.c_str());
        CheckDlgButton(dialog, PROJECT_AUTO_LOAD, Entry().autoLoad ? BST_CHECKED : BST_UNCHECKED);
        UpdateButtons(dialog);
    }

    void ShowProjects(HWND dialog) {
        std::vector<std::wstring> names;
        const auto& library = current.library;
        for (size_t index = 0; index < library.projects.size(); ++index) {
            auto name = library.projects[index].name;
            if (index == library.active) name += L" (active)";
            names.push_back(std::move(name));
        }
        FillList(GetDlgItem(dialog, PROJECT_LIST), names, static_cast<int>(selected));
        ShowEntry(dialog);
    }

    void NameProject(HWND dialog, bool add) {
        auto& entries = current.library.projects;
        TextInput input{add ? L"New Project" : L"Rename Project", L"&Project name", add ? L"" : Entry().name};
        if (ShowDialog(dialog, PROJECT_TEXT, TextProc, reinterpret_cast<LPARAM>(&input)) != IDOK) return;
        const auto first = input.value.find_first_not_of(L" \t");
        if (first == std::wstring::npos) throw std::runtime_error("Enter a project name.");
        auto name = input.value.substr(first, input.value.find_last_not_of(L" \t") - first + 1);
        if (std::ranges::any_of(entries, [&](const Project& entry) {
            return (add || &entry != &Entry()) && SameText(entry.name, name);
        })) throw std::runtime_error("A project with this name already exists.");
        if (add) {
            entries.push_back({std::move(name), {}, {}, false});
            selected = entries.size() - 1;
        } else Entry().name = std::move(name);
        changed = true;
        ShowProjects(dialog);
    }

    void ProjectCommand(HWND dialog, int command) {
        auto& library = current.library;
        auto& entries = library.projects;
        if (command == PROJECT_NEW || command == PROJECT_RENAME) {
            if (command == PROJECT_NEW || selected) NameProject(dialog, command == PROJECT_NEW);
            return;
        }
        if (!selected) return;
        if (command == PROJECT_REMOVE) {
            entries.erase(entries.begin() + selected);
            if (library.active == selected) library.active = 0;
            else if (library.active > selected) --library.active;
            selected = (std::min)(selected, entries.size() - 1);
        } else {
            const auto target = command == PROJECT_UP ? selected - 1 : selected + 1;
            if (target == 0 || target >= entries.size()) return;
            std::swap(entries[selected], entries[target]);
            if (library.active == selected) library.active = target;
            else if (library.active == target) library.active = selected;
            selected = target;
        }
        changed = true;
        ShowProjects(dialog);
    }

    void MemberCommand(HWND dialog, int command) {
        auto& entry = Entry();
        auto& members = entry.members;
        int member = Selected(dialog, PROJECT_MEMBERS);
        switch (command) {
        case PROJECT_ADD_FILES: case PROJECT_ADD_FOLDER: case PROJECT_SET_BASE: {
            const bool folder = command != PROJECT_ADD_FILES;
            auto paths = PickPaths(dialog, folder ? Pick::Folder : Pick::Files, entry.base);
            if (paths.empty()) return;
            if (command == PROJECT_SET_BASE) entry.base = std::move(paths.front());
            else for (auto& path : paths) if (AddMember(entry, {std::move(path), folder}))
                member = static_cast<int>(members.size() - 1);
            break;
        }
        case PROJECT_REMOVE_MEMBER:
            if (member < 0) return;
            members.erase(members.begin() + member);
            member = (std::min)(member, static_cast<int>(members.size()) - 1);
            break;
        case PROJECT_MEMBER_UP: case PROJECT_MEMBER_DOWN: {
            const int target = member + (command == PROJECT_MEMBER_UP ? -1 : 1);
            if (member < 0 || target < 0 || static_cast<size_t>(target) >= members.size()) return;
            std::swap(members[member], members[target]);
            member = target;
            break;
        }
        case PROJECT_SORT:
            std::ranges::stable_sort(members, [](const Member& left, const Member& right) {
                return PathOrder{}(left.path.filename(), right.path.filename());
            });
            member = -1;
            break;
        case PROJECT_CLEAR_BASE: entry.base.clear(); break;
        case PROJECT_AUTO_LOAD: entry.autoLoad = IsDlgButtonChecked(dialog, PROJECT_AUTO_LOAD) == BST_CHECKED; break;
        default: return;
        }
        changed = true;
        ShowEntry(dialog, member);
    }
};

INT_PTR CALLBACK ManagerProc(HWND dialog, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INITDIALOG) SetWindowLongPtrW(dialog, DWLP_USER, l);
    auto* manager = reinterpret_cast<Manager*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!manager) return FALSE;
    try {
        if (message == WM_INITDIALOG) { manager->ShowProjects(dialog); return TRUE; }
        if (message != WM_COMMAND) return FALSE;
        const int command = LOWORD(w);
        if (command == IDCANCEL) { EndDialog(dialog, IDCANCEL); return TRUE; }
        if (command == IDOK) {
            if (manager->changed) SaveLibrary(manager->current.path, manager->current.library);
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (command == PROJECT_LIST) {
            const auto selected = Selected(dialog, PROJECT_LIST);
            if (HIWORD(w) == LBN_SELCHANGE && selected >= 0) {
                manager->selected = selected;
                manager->ShowEntry(dialog);
            }
        } else if (command == PROJECT_MEMBERS) manager->UpdateButtons(dialog);
        else if (command >= PROJECT_NEW && command <= PROJECT_DOWN) manager->ProjectCommand(dialog, command);
        else manager->MemberCommand(dialog, command);
        return TRUE;
    } catch (const std::exception& error) { Report(dialog, error); return TRUE; }
}

void EditProjects() {
    auto current = LoadCurrent();
    const auto selected = current.library.active;
    Manager manager{std::move(current), selected};
    ShowDialog(host._nppHandle, PROJECT_MANAGER, ManagerProc, reinterpret_cast<LPARAM>(&manager));
}

void Activate(CurrentLibrary& current, size_t index) {
    auto& library = current.library;
    if (library.active != index) {
        library.active = index;
        SaveLibrary(current.path, library);
    }
    if (library.projects[index].autoLoad) OpenMembers(library.projects[index].members);
}

void SelectProject() {
    auto current = LoadCurrent();
    const auto& library = current.library;
    ListChoice choice{L"Select Project", library.description.empty() ? L"Projects" : library.description.c_str(), {},
        static_cast<int>(library.active)};
    for (const auto& entry : library.projects) choice.items.push_back(entry.name);
    if (ShowDialog(host._nppHandle, PROJECT_PICK, ListProc, reinterpret_cast<LPARAM>(&choice)) == IDOK)
        Activate(current, choice.index);
}

template<bool Next>
void CycleProject() {
    auto current = LoadCurrent();
    const auto count = current.library.projects.size();
    if (count > 1) Activate(current, (current.library.active + (Next ? 1 : count - 1)) % count);
}

void ProjectFiles() {
    const auto current = LoadCurrent();
    const auto& entry = current.library.projects[current.library.active];
    ListChoice choice{L"Open Project File or Folder", entry.name.c_str(), {}, 0};
    for (const auto& member : entry.members) choice.items.push_back(MemberLabel(member));
    if (ShowDialog(host._nppHandle, PROJECT_PICK, ListProc, reinterpret_cast<LPARAM>(&choice)) == IDOK)
        OpenMembers({entry.members[choice.index]});
}

template<bool All, bool Remove = false>
void UpdateMembers() {
    auto current = LoadCurrent();
    auto& entry = current.library.projects[current.library.active];
    const auto buffers = All ? OpenBuffers() : std::vector<WPARAM>{static_cast<WPARAM>(Host(NPPM_GETCURRENTBUFFERID))};
    bool changed = false;
    for (const auto buffer : buffers) {
        auto path = BufferPath(buffer);
        if (path.empty()) continue;
        if constexpr (Remove) {
            changed |= std::erase_if(entry.members, [&](const Member& member) {
                return !member.folder && SameText(member.path.native(), path.native());
            }) != 0;
        } else changed |= AddMember(entry, {std::move(path), false});
    }
    if (changed) SaveLibrary(current.path, current.library);
}

void LoadProject() {
    const auto current = LoadCurrent();
    OpenMembers(current.library.projects[current.library.active].members);
}

void CloseProject() {
    const auto current = LoadCurrent();
    const auto& entry = current.library.projects[current.library.active];
    const auto buffers = OpenBuffers();
    for (auto it = buffers.rbegin(); it != buffers.rend(); ++it) {
        if (Contains(entry, BufferPath(*it)) && !Host(MPM_CLOSEBUFFER, *it)) break;
    }
}

template<bool New>
void SwitchLibrary() {
    const auto config = ConfigDir();
    const auto paths = PickPaths(host._nppHandle, New ? Pick::NewLibrary : Pick::Library, config);
    if (paths.empty()) return;
    if constexpr (New) {
        if (fs::exists(paths.front())) throw std::runtime_error("Choose an unused file name for the new project library.");
        SaveLibrary(paths.front(), Library{});
    } else ReadLibrary(paths.front(), false);
    files::WriteAtomic(config / LibrarySelection, {files::ToUtf8(paths.front().native())});
}

void UseDefault() {
    const auto config = ConfigDir();
    ReadLibrary(config / DefaultLibrary, true);
    fs::remove(config / LibrarySelection);
}

void DescribeLibrary() {
    auto current = LoadCurrent();
    TextInput input{L"Project Library Description", L"&Description", current.library.description};
    if (ShowDialog(host._nppHandle, PROJECT_TEXT, TextProc, reinterpret_cast<LPARAM>(&input)) != IDOK ||
        input.value == current.library.description) return;
    current.library.description = std::move(input.value);
    SaveLibrary(current.path, current.library);
}

std::optional<CurrentLibrary> paneLibrary;

void UpdatePaneButtons() {
    EnableWindow(GetDlgItem(pane, PROJECT_PANE_OPEN), Selected(pane, PROJECT_PANE_FILES) >= 0);
}

void RefreshPane() {
    const HWND selector = GetDlgItem(pane, PROJECT_PANE_SELECTOR);
    const HWND list = GetDlgItem(pane, PROJECT_PANE_FILES);
    const HWND status = GetDlgItem(pane, PROJECT_PANE_STATUS);
    try {
        auto current = LoadCurrent();
        const bool sameLibrary = paneLibrary && paneLibrary->path == current.path;
        if (sameLibrary && paneLibrary->library.original == current.library.original) {
            SendMessageW(selector, CB_SETCURSEL, current.library.active, 0);
            return;
        }
        const auto& entry = current.library.projects[current.library.active];
        const int selected = Selected(pane, PROJECT_PANE_FILES);
        int index = -1;
        std::vector<std::wstring> labels;
        for (const auto& member : entry.members) labels.push_back(MemberLabel(member));
        if (sameLibrary && selected >= 0) {
            const auto& old = paneLibrary->library.projects[paneLibrary->library.active];
            if (SameText(old.name, entry.name) && static_cast<size_t>(selected) < old.members.size()) {
                const auto found = std::ranges::find(labels, MemberLabel(old.members[selected]));
                if (found != labels.end()) index = static_cast<int>(found - labels.begin());
            }
        }
        SendMessageW(selector, CB_RESETCONTENT, 0, 0);
        for (const auto& project : current.library.projects)
            SendMessageW(selector, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(project.name.c_str()));
        SendMessageW(selector, CB_SETCURSEL, current.library.active, 0);
        EnableWindow(selector, TRUE);
        FillList(list, labels, index);
        SetWindowTextW(status, L"No project members");
        ShowWindow(list, labels.empty() ? SW_HIDE : SW_SHOWNA);
        ShowWindow(status, labels.empty() ? SW_SHOWNA : SW_HIDE);
        paneLibrary = std::move(current);
    } catch (const std::exception&) {
        paneLibrary.reset();
        SendMessageW(selector, CB_RESETCONTENT, 0, 0);
        SendMessageW(list, LB_RESETCONTENT, 0, 0);
        EnableWindow(selector, FALSE);
        ShowWindow(list, SW_HIDE);
        SetWindowTextW(status, L"Project library unavailable");
        ShowWindow(status, SW_SHOWNA);
    }
    UpdatePaneButtons();
}

void SelectPaneProject() {
    if (!paneLibrary) return;
    const auto index = SendDlgItemMessageW(pane, PROJECT_PANE_SELECTOR, CB_GETCURSEL, 0, 0);
    if (index < 0 || static_cast<size_t>(index) >= paneLibrary->library.projects.size()) return;
    auto current = *paneLibrary;
    if (static_cast<size_t>(index) != current.library.active) Activate(current, static_cast<size_t>(index));
}

void OpenPaneMember() {
    const int selected = Selected(pane, PROJECT_PANE_FILES);
    if (!paneLibrary || selected < 0) return;
    const auto& members = paneLibrary->library.projects[paneLibrary->library.active].members;
    if (static_cast<size_t>(selected) < members.size()) OpenMembers({members[selected]});
}

INT_PTR CALLBACK PaneProc(HWND dialog, UINT message, WPARAM w, LPARAM) {
    switch (message) {
    case WM_SIZE: {
        RECT bounds{}, units{6, 6, 0, 0}, files{0, 50, 0, 0};
        GetClientRect(dialog, &bounds);
        MapDialogRect(dialog, &units);
        MapDialogRect(dialog, &files);
        const int width = (std::max)(0L, bounds.right - 2 * units.left);
        RECT combo{};
        const HWND selector = GetDlgItem(dialog, PROJECT_PANE_SELECTOR);
        SendMessageW(selector, CB_GETDROPPEDCONTROLRECT, 0, reinterpret_cast<LPARAM>(&combo));
        SetWindowPos(selector, nullptr, 0, 0, width, combo.bottom - combo.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT footer{};
        GetWindowRect(GetDlgItem(dialog, PROJECT_PANE_OPEN), &footer);
        const int buttonHeight = footer.bottom - footer.top;
        const auto bottom = (std::max)(files.top, bounds.bottom - buttonHeight - units.top);
        MoveWindow(GetDlgItem(dialog, PROJECT_PANE_FILES), units.left, files.top, width,
            (std::max)(0L, bottom - files.top - units.top), TRUE);
        MoveWindow(GetDlgItem(dialog, PROJECT_PANE_STATUS), units.left, files.top, width, buttonHeight + units.top, TRUE);
        for (int id : {PROJECT_PANE_OPEN, PROJECT_PANE_EDIT}) {
            RECT button{};
            GetWindowRect(GetDlgItem(dialog, id), &button);
            MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&button), 2);
            SetWindowPos(GetDlgItem(dialog, id), nullptr, button.left, bottom, 0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case PROJECT_PANE_SELECTOR:
            if (HIWORD(w) == CBN_SELENDOK || (HIWORD(w) == CBN_SELCHANGE &&
                !SendDlgItemMessageW(dialog, PROJECT_PANE_SELECTOR, CB_GETDROPPEDSTATE, 0, 0)))
                RunCommand<SelectPaneProject>();
            return TRUE;
        case PROJECT_PANE_FILES:
            if (HIWORD(w) == LBN_DBLCLK) RunCommand<OpenPaneMember>();
            else if (HIWORD(w) == LBN_SELCHANGE) UpdatePaneButtons();
            return TRUE;
        case PROJECT_PANE_OPEN: RunCommand<OpenPaneMember>(); return TRUE;
        case PROJECT_PANE_EDIT: RunCommand<EditProjects>(); return TRUE;
        }
        break;
    case WM_NCDESTROY:
        paneLibrary.reset();
        pane = nullptr;
        break;
    }
    return FALSE;
}

void TogglePane() {
    if (PaneShown()) {
        Host(NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(pane));
        return;
    }
    if (!pane) {
        pane = CreateDialogParamW(instance, MAKEINTRESOURCEW(PROJECT_PANE), host._nppHandle, PaneProc, 0);
        if (!pane) throw std::runtime_error("Cannot create the Project pane.");
        tTbData data{};
        data.hClient = pane;
        data.pszName = L"Project";
        data.pszModuleName = L"Project.dll";
        data.dlgID = PROJECT_PANE;
        data.uMask = DWS_DF_CONT_LEFT;
        if (!Host(NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&data))) {
            DestroyWindow(pane);
            throw std::runtime_error("Cannot dock the Project pane.");
        }
    }
    RefreshPane();
    Host(NPPM_DMMSHOW, 0, reinterpret_cast<LPARAM>(pane));
    const HWND selector = GetDlgItem(pane, PROJECT_PANE_SELECTOR);
    SetFocus(IsWindowEnabled(selector) ? selector : GetDlgItem(pane, PROJECT_PANE_EDIT));
}

FuncItem commands[]{
    {L"Project &Pane", RunCommand<TogglePane>},
    {},
    {L"&Select Project...", RunCommand<SelectProject>},
    {L"&Project Files...", RunCommand<ProjectFiles>},
    {L"&Edit Projects...", RunCommand<EditProjects>},
    {},
    {L"&Add Current File", RunCommand<UpdateMembers<false>>},
    {L"&Remove Current File", RunCommand<UpdateMembers<false, true>>},
    {L"Add A&ll Open Files", RunCommand<UpdateMembers<true>>},
    {},
    {L"&Load Project", RunCommand<LoadProject>},
    {L"&Close Project", RunCommand<CloseProject>},
    {L"&Next Project", RunCommand<CycleProject<true>>},
    {L"Pre&vious Project", RunCommand<CycleProject<false>>},
    {},
    {L"New Project Librar&y...", RunCommand<SwitchLibrary<true>>},
    {L"&Open Project Library...", RunCommand<SwitchLibrary<false>>},
    {L"&Use Default Library", RunCommand<UseDefault>},
    {L"Library &Description...", RunCommand<DescribeLibrary>},
};
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) instance = module;
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
extern "C" __declspec(dllexport) const wchar_t* getName() { return L"Project"; }
extern "C" __declspec(dllexport) const wchar_t* multipadGetMainMenuLabel() { return L"P&roject"; }
extern "C" __declspec(dllexport) void setInfo(NppData data) { host = data; }
extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* count) { *count = _countof(commands); return commands; }
extern "C" __declspec(dllexport) void beNotified(SCNotification*) {}
extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return TRUE; }
extern "C" __declspec(dllexport) BOOL multipadSupportsFastView() { return TRUE; }
extern "C" __declspec(dllexport) BOOL multipadPreTranslateMessage(MSG* message) {
    if (!PaneShown() || !IsChild(pane, message->hwnd)) return FALSE;
    if (message->hwnd == GetDlgItem(pane, PROJECT_PANE_FILES) && message->message == WM_KEYDOWN && message->wParam == VK_RETURN) {
        RunCommand<OpenPaneMember>();
        return TRUE;
    }
    return IsDialogMessageW(pane, message);
}
extern "C" __declspec(dllexport) BOOL multipadCommandEnabled(UINT id) {
    if (runningCommand) return FALSE;
    for (const auto& command : commands) {
        if (command._cmdID != static_cast<int>(id)) continue;
        if (command._pFunc == RunCommand<TogglePane>) Host(NPPM_SETMENUITEMCHECK, id, PaneShown());
        if (command._pFunc == RunCommand<UpdateMembers<false>> || command._pFunc == RunCommand<UpdateMembers<false, true>>)
            return Host(MPM_GETBUFFERFILEPATH, Host(NPPM_GETCURRENTBUFFERID)) > 0;
        if (command._pFunc == RunCommand<UpdateMembers<true>>) return Host(NPPM_GETNBOPENFILES, 0, ALL_OPEN_FILES) > 0;
        break;
    }
    return TRUE;
}
