#pragma once
// Host services for the vendored matepath directory API. Icons are resolved on
// the owning STA, avoiding upstream's cross-apartment Shell/UI worker access.
#include <cstring>
#include <cwchar>
#define COUNTOF(array) _countof(array)
#define NP2HeapAlloc(size) HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size))
#define NP2HeapFree(memory) HeapFree(GetProcessHeap(), 0, (memory))
inline bool StrIsEmpty(LPCWSTR value) noexcept { return !value || !*value; }
template<size_t N> bool StrEqualEx(LPCWSTR a, const wchar_t (&b)[N]) noexcept { return wcscmp(a, b) == 0; }
inline bool PathEqual(LPCWSTR a, LPCWSTR b) noexcept { return _wcsicmp(a, b) == 0; }
template<class T, class U> T AsPointer(U value) noexcept { return reinterpret_cast<T>(value); }
template<class T, class U> T AsInteger(U value) noexcept { return reinterpret_cast<T>(value); }
template<class T> void** AsPPVArgs(T** value) noexcept { return reinterpret_cast<void**>(value); }
struct BackgroundWorker {
    HWND hwnd;
    void Init(HWND owner) noexcept { hwnd = owner; }
    void Cancel() noexcept {}
    void Destroy() noexcept {}
    bool Continue() const noexcept { return true; }
};
