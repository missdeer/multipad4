#include "ComObject.h"
#include "ShellContextMenu.h"
#include <shlobj.h>
#include "resource.h"

namespace {
constexpr UINT firstShellCommand = 1;
constexpr UINT lastShellCommand = 0x3FFF;
static_assert(lastShellCommand < ID_CLOSE && lastShellCommand < ID_CLOSEALL &&
    lastShellCommand < ID_CLOSE_LEFT_TABS && lastShellCommand < ID_CLOSE_RIGHT_TABS &&
    lastShellCommand < ID_CLOSE_OTHER_TABS);

struct ShellMenuMessages {
    ComObject<IContextMenu3> menu3;
    ComObject<IContextMenu2> menu2;
};

LRESULT CALLBACK ShellMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto& shell = *reinterpret_cast<ShellMenuMessages*>(data);
    if (message == WM_INITMENUPOPUP || message == WM_MENUCHAR ||
        ((message == WM_DRAWITEM || message == WM_MEASUREITEM) && wParam == 0)) {
        LRESULT result = 0;
        if (shell.menu3 && SUCCEEDED(shell.menu3->HandleMenuMsg2(message, wParam, lParam, &result))) return result;
        if (shell.menu2 && message != WM_MENUCHAR &&
            SUCCEEDED(shell.menu2->HandleMenuMsg(message, wParam, lParam))) return 0;
        // Keep the frame's main-menu updater away from the tab popup.
        if (message == WM_INITMENUPOPUP) return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}
}

UINT TrackTabContextMenu(HWND owner, HMENU menu, POINT point, const std::filesystem::path& path) {
    const UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON;
    const auto trackTabCommands = [&] {
        return TrackPopupMenuEx(menu, flags | TPM_NONOTIFY, point.x, point.y, owner, nullptr);
    };
    if (path.empty()) return trackTabCommands();
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return trackTabCommands();

    ComObject<IShellItem> item;
    ComObject<IContextMenu> context;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(item.Receive()))) ||
        FAILED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(context.Receive())))) return trackTabCommands();

    const int tabItemCount = GetMenuItemCount(menu);
    const HRESULT queried = context->QueryContextMenu(menu, tabItemCount, firstShellCommand, lastShellCommand, CMF_NORMAL);
    ShellMenuMessages messages;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(messages.menu3.Receive()))))
        context->QueryInterface(IID_PPV_ARGS(messages.menu2.Receive()));
    const UINT_PTR subclass = reinterpret_cast<UINT_PTR>(&messages);
    if (FAILED(queried) || HRESULT_CODE(queried) == 0 ||
        !SetWindowSubclass(owner, ShellMenuProc, subclass, reinterpret_cast<DWORD_PTR>(&messages))) {
        while (GetMenuItemCount(menu) > tabItemCount && DeleteMenu(menu, tabItemCount, MF_BYPOSITION)) {}
        return trackTabCommands();
    }

    InsertMenuW(menu, tabItemCount, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    const UINT command = TrackPopupMenuEx(menu, flags, point.x, point.y, owner, nullptr);
    RemoveWindowSubclass(owner, ShellMenuProc, subclass);

    if (command < firstShellCommand || command > lastShellCommand) return command;
    const UINT offset = command - firstShellCommand;
    if (offset < static_cast<UINT>(HRESULT_CODE(queried))) {
        CMINVOKECOMMANDINFOEX invoke{};
        invoke.cbSize = sizeof(invoke);
        invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        invoke.hwnd = owner;
        invoke.lpVerb = MAKEINTRESOURCEA(offset);
        invoke.lpVerbW = MAKEINTRESOURCEW(offset);
        invoke.nShow = SW_SHOWNORMAL;
        invoke.ptInvoke = point;
        context->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke));
    }
    return 0;
}
