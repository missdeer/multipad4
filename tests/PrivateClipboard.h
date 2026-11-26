#pragma once
#include <windows.h>
#include <ole2.h>
#include "../src/EditClipboard.h"
#include <stdexcept>
#include <string>
#include <memory>
#include <vector>
// Each window station owns its clipboard; create this before COM or any HWND.
class PrivateClipboard {
    HWINSTA originalStation = GetProcessWindowStation(), station = nullptr;
    HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId()), desktop = nullptr;
    HWND owner = nullptr;
    struct SavedFormat {
        UINT format = 0;
        STGMEDIUM medium{};
        ~SavedFormat() { ReleaseStgMedium(&medium); }
        HANDLE Handle() const {
            switch (medium.tymed) {
            case TYMED_GDI: return medium.hBitmap;
            case TYMED_ENHMF: return medium.hEnhMetaFile;
            case TYMED_MFPICT: return medium.hMetaFilePict;
            default: return medium.hGlobal;
            }
        }
    };
    std::vector<std::unique_ptr<SavedFormat>> saved;
    bool captured = false, restoring = false;
    void Reset() noexcept {
        if (captured && !RestoreClipboard()) OutputDebugStringW(L"MultiPad4: private test clipboard restoration failed.\n");
        if (owner) { DestroyWindow(owner); owner = nullptr; }
        if (station) SetProcessWindowStation(originalStation);
        if (desktop) { SetThreadDesktop(originalDesktop); CloseDesktop(desktop); desktop = nullptr; }
        if (station) { CloseWindowStation(station); station = nullptr; }
    }
public:
    PrivateClipboard() {
        // Windows restricts explicitly named stations to administrators. Its logon-scoped
        // noninteractive station works without elevation; preserve its existing formats too.
        station = CreateWindowStationW(nullptr, 0, WINSTA_ALL_ACCESS, nullptr);
        if (!station || !SetProcessWindowStation(station)) {
            const auto error = GetLastError(); Reset(); throw std::runtime_error("Cannot isolate test clipboard station: " + std::to_string(error));
        }
        desktop = CreateDesktopW(L"RichTextTest", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        if (!desktop || !SetThreadDesktop(desktop)) {
            const auto error = GetLastError(); Reset(); throw std::runtime_error("Cannot isolate test clipboard desktop: " + std::to_string(error));
        }
        try {
            wchar_t name[256]{}; DWORD needed = 0;
            if (!GetUserObjectInformationW(station, UOI_NAME, name, sizeof(name), &needed) || _wcsicmp(name, L"WinSta0") == 0)
                throw std::runtime_error("Test clipboard is not isolated.");
            owner = CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!owner || !OpenEditClipboard(owner)) throw std::runtime_error("Cannot preserve private clipboard.");
            struct Close { ~Close() { CloseClipboard(); } } close;
            for (UINT format = 0;;) {
                SetLastError(ERROR_SUCCESS); format = EnumClipboardFormats(format);
                if (!format) { if (GetLastError() != ERROR_SUCCESS) throw std::runtime_error("Cannot enumerate private clipboard before testing."); break; }
                const auto data = GetClipboardData(format);
                auto entry = std::make_unique<SavedFormat>(); entry->format = format;
                HANDLE copy = nullptr;
                if (format == CF_ENHMETAFILE || format == CF_DSPENHMETAFILE) {
                    copy = CopyEnhMetaFileW(static_cast<HENHMETAFILE>(data), nullptr);
                    entry->medium.tymed = TYMED_ENHMF; entry->medium.hEnhMetaFile = static_cast<HENHMETAFILE>(copy);
                } else {
                    const auto source = format == CF_DSPBITMAP ? CF_BITMAP : format == CF_DSPMETAFILEPICT ? CF_METAFILEPICT : format;
                    copy = data ? OleDuplicateData(data, static_cast<CLIPFORMAT>(source), 0) : nullptr;
                    if (source == CF_BITMAP || source == CF_PALETTE) { entry->medium.tymed = TYMED_GDI; entry->medium.hBitmap = static_cast<HBITMAP>(copy); }
                    else if (source == CF_METAFILEPICT) { entry->medium.tymed = TYMED_MFPICT; entry->medium.hMetaFilePict = static_cast<HMETAFILEPICT>(copy); }
                    else { entry->medium.tymed = TYMED_HGLOBAL; entry->medium.hGlobal = static_cast<HGLOBAL>(copy); }
                }
                if (!copy) throw std::runtime_error("Cannot duplicate private clipboard format before testing.");
                saved.push_back(std::move(entry));
            }
            captured = true;
        } catch (...) { Reset(); throw; }
    }
    bool RestoreClipboard() noexcept {
        if (!captured) return true;
        if (!OpenEditClipboard(owner)) return false;
        bool ok = true;
        if (!restoring) { if (!EmptyClipboard()) { CloseClipboard(); return false; } restoring = true; }
        for (auto& entry : saved) if (entry->medium.tymed != TYMED_NULL) {
            if (SetClipboardData(entry->format, entry->Handle())) entry->medium.tymed = TYMED_NULL;
            else ok = false;
        }
        CloseClipboard(); if (ok) captured = false; return ok;
    }
    ~PrivateClipboard() { Reset(); }
    PrivateClipboard(const PrivateClipboard&) = delete;
    PrivateClipboard& operator=(const PrivateClipboard&) = delete;
};
