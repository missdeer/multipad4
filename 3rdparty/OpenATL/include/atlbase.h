// OpenATL - Clean-room ATL subset for WTL 10.0
// Main ATL module header

#ifndef __ATLBASE_H__
#define __ATLBASE_H__

#pragma once

#ifndef __cplusplus
#error ATL requires C++ compilation
#endif

// Minimum Windows 7
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0800
#endif

#include <windows.h>
#include <ole2.h>
#include <tchar.h>
#include <limits.h>

// Include sub-headers
#include "atldef.h"
#include "atltrace.h"
#include "atlcore.h"
#include "atlsimpcoll.h"
#include "atlalloc.h"
#include "atlcomcli.h"
#include "atlconv.h"

#include <commctrl.h>
#include <shlwapi.h>

namespace ATL {

///////////////////////////////////////////////////////////////////////////////
// ATL::Checked namespace - secure CRT wrappers

namespace Checked {

inline void strcpy_s(char* dest, size_t size, const char* src) noexcept
{
    ::strcpy_s(dest, size, src);
}

inline void wcscpy_s(wchar_t* dest, size_t size, const wchar_t* src) noexcept
{
    ::wcscpy_s(dest, size, src);
}

inline errno_t strncpy_s(char* dest, size_t size, const char* src, size_t count) noexcept
{
    return ::strncpy_s(dest, size, src, count);
}

inline errno_t wcsncpy_s(wchar_t* dest, size_t size, const wchar_t* src, size_t count) noexcept
{
    return ::wcsncpy_s(dest, size, src, count);
}

inline void strcat_s(char* dest, size_t size, const char* src) noexcept
{
    ::strcat_s(dest, size, src);
}

inline void wcscat_s(wchar_t* dest, size_t size, const wchar_t* src) noexcept
{
    ::wcscat_s(dest, size, src);
}

inline void memcpy_s(void* dest, size_t destSize, const void* src, size_t count) noexcept
{
    ::memcpy_s(dest, destSize, src, count);
}

inline void memmove_s(void* dest, size_t destSize, const void* src, size_t count) noexcept
{
    ::memmove_s(dest, destSize, src, count);
}

// TCHAR variants
#ifdef _UNICODE
inline void tcscpy_s(wchar_t* dest, size_t size, const wchar_t* src) noexcept
{
    ::wcscpy_s(dest, size, src);
}

inline errno_t tcsncpy_s(wchar_t* dest, size_t size, const wchar_t* src, size_t count) noexcept
{
    return ::wcsncpy_s(dest, size, src, count);
}

inline void tcscat_s(wchar_t* dest, size_t size, const wchar_t* src) noexcept
{
    ::wcscat_s(dest, size, src);
}
#else
inline void tcscpy_s(char* dest, size_t size, const char* src) noexcept
{
    ::strcpy_s(dest, size, src);
}

inline errno_t tcsncpy_s(char* dest, size_t size, const char* src, size_t count) noexcept
{
    return ::strncpy_s(dest, size, src, count);
}

inline void tcscat_s(char* dest, size_t size, const char* src) noexcept
{
    ::strcat_s(dest, size, src);
}
#endif

} // namespace Checked

///////////////////////////////////////////////////////////////////////////////
// Threading models

class CComMultiThreadModel {
public:
    typedef CComAutoCriticalSection AutoCriticalSection;
    typedef CComAutoDeleteCriticalSection AutoDeleteCriticalSection;
    typedef CComCriticalSection CriticalSection;

    static ULONG WINAPI Increment(LONG volatile* p) noexcept
    {
        return ::InterlockedIncrement(p);
    }

    static ULONG WINAPI Decrement(LONG volatile* p) noexcept
    {
        return ::InterlockedDecrement(p);
    }
};

class CComSingleThreadModel {
public:
    typedef CComFakeCriticalSection AutoCriticalSection;
    typedef CComFakeCriticalSection AutoDeleteCriticalSection;
    typedef CComFakeCriticalSection CriticalSection;

    static ULONG WINAPI Increment(LONG volatile* p) noexcept
    {
        return (ULONG)++(*p);
    }

    static ULONG WINAPI Decrement(LONG volatile* p) noexcept
    {
        return (ULONG)--(*p);
    }
};

class CComMultiThreadModelNoCS {
public:
    typedef CComFakeCriticalSection AutoCriticalSection;
    typedef CComFakeCriticalSection AutoDeleteCriticalSection;
    typedef CComFakeCriticalSection CriticalSection;

    static ULONG WINAPI Increment(LONG volatile* p) noexcept
    {
        return ::InterlockedIncrement(p);
    }

    static ULONG WINAPI Decrement(LONG volatile* p) noexcept
    {
        return ::InterlockedDecrement(p);
    }
};

// Default thread model typedefs
#if defined(_ATL_SINGLE_THREADED)
typedef CComSingleThreadModel CComObjectThreadModel;
typedef CComSingleThreadModel CComGlobalsThreadModel;
#elif defined(_ATL_APARTMENT_THREADED)
typedef CComSingleThreadModel CComObjectThreadModel;
typedef CComMultiThreadModel CComGlobalsThreadModel;
#else // _ATL_FREE_THREADED
typedef CComMultiThreadModel CComObjectThreadModel;
typedef CComMultiThreadModel CComGlobalsThreadModel;
#endif

///////////////////////////////////////////////////////////////////////////////
// CComCritSecLock - RAII lock

template <typename TLock>
class CComCritSecLock {
public:
    CComCritSecLock(TLock& cs, bool bInitialLock = true)
        : m_cs(cs), m_bLocked(false)
    {
        if (bInitialLock) {
            HRESULT hr = Lock();
            if (FAILED(hr))
                AtlThrow(hr);
        }
    }

    ~CComCritSecLock()
    {
        if (m_bLocked)
            Unlock();
    }

    HRESULT Lock() noexcept
    {
        ATLASSERT(!m_bLocked);
        HRESULT hr = m_cs.Lock();
        if (SUCCEEDED(hr))
            m_bLocked = true;
        return hr;
    }

    void Unlock() noexcept
    {
        ATLASSERT(m_bLocked);
        m_cs.Unlock();
        m_bLocked = false;
    }

private:
    TLock& m_cs;
    bool m_bLocked;

    CComCritSecLock(const CComCritSecLock&) = delete;
    CComCritSecLock& operator=(const CComCritSecLock&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
// CHandle - HANDLE wrapper

class CHandle {
public:
    HANDLE m_h;

    CHandle() noexcept : m_h(NULL) {}
    CHandle(CHandle&& h) noexcept : m_h(h.m_h) { h.m_h = NULL; }
    explicit CHandle(HANDLE h) noexcept : m_h(h) {}

    ~CHandle()
    {
        if (m_h != NULL)
            Close();
    }

    CHandle& operator=(CHandle&& h) noexcept
    {
        if (this != &h) {
            if (m_h != NULL)
                Close();
            m_h = h.m_h;
            h.m_h = NULL;
        }
        return *this;
    }

    operator HANDLE() const noexcept { return m_h; }

    void Attach(HANDLE h) noexcept
    {
        ATLASSERT(m_h == NULL);
        m_h = h;
    }

    HANDLE Detach() noexcept
    {
        HANDLE h = m_h;
        m_h = NULL;
        return h;
    }

    void Close() noexcept
    {
        if (m_h != NULL) {
            ::CloseHandle(m_h);
            m_h = NULL;
        }
    }

private:
    CHandle(const CHandle&) = delete;
    CHandle& operator=(const CHandle&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
// CRegKey - Registry key wrapper

class CRegKey {
public:
    HKEY m_hKey;

    CRegKey() noexcept : m_hKey(NULL) {}

    CRegKey(CRegKey&& key) noexcept : m_hKey(key.m_hKey) { key.m_hKey = NULL; }

    explicit CRegKey(HKEY hKey) noexcept : m_hKey(hKey) {}

    ~CRegKey()
    {
        Close();
    }

    CRegKey& operator=(CRegKey&& key) noexcept
    {
        if (this != &key) {
            Close();
            m_hKey = key.m_hKey;
            key.m_hKey = NULL;
        }
        return *this;
    }

    operator HKEY() const noexcept { return m_hKey; }

    void Attach(HKEY hKey) noexcept
    {
        ATLASSERT(m_hKey == NULL);
        m_hKey = hKey;
    }

    HKEY Detach() noexcept
    {
        HKEY hKey = m_hKey;
        m_hKey = NULL;
        return hKey;
    }

    LSTATUS Close() noexcept
    {
        LSTATUS lRes = ERROR_SUCCESS;
        if (m_hKey != NULL) {
            lRes = ::RegCloseKey(m_hKey);
            m_hKey = NULL;
        }
        return lRes;
    }

    LSTATUS Create(HKEY hKeyParent, LPCTSTR lpszKeyName,
        LPTSTR lpszClass = REG_NONE, DWORD dwOptions = REG_OPTION_NON_VOLATILE,
        REGSAM samDesired = KEY_READ | KEY_WRITE,
        LPSECURITY_ATTRIBUTES lpSecAttr = NULL,
        LPDWORD lpdwDisposition = NULL) noexcept
    {
        ATLASSERT(hKeyParent != NULL);
        DWORD dw;
        HKEY hKey = NULL;
        LSTATUS lRes = ::RegCreateKeyEx(hKeyParent, lpszKeyName, 0,
            lpszClass, dwOptions, samDesired, lpSecAttr, &hKey, &dw);
        if (lpdwDisposition != NULL)
            *lpdwDisposition = dw;
        if (lRes == ERROR_SUCCESS) {
            Close();
            m_hKey = hKey;
        }
        return lRes;
    }

    LSTATUS Open(HKEY hKeyParent, LPCTSTR lpszKeyName,
        REGSAM samDesired = KEY_READ | KEY_WRITE) noexcept
    {
        ATLASSERT(hKeyParent != NULL);
        HKEY hKey = NULL;
        LSTATUS lRes = ::RegOpenKeyEx(hKeyParent, lpszKeyName, 0, samDesired, &hKey);
        if (lRes == ERROR_SUCCESS) {
            Close();
            m_hKey = hKey;
        }
        return lRes;
    }

    LSTATUS DeleteSubKey(LPCTSTR lpszSubKey) noexcept
    {
        ATLASSERT(m_hKey != NULL);
        return ::RegDeleteKey(m_hKey, lpszSubKey);
    }

    LSTATUS DeleteValue(LPCTSTR lpszValue) noexcept
    {
        ATLASSERT(m_hKey != NULL);
        return ::RegDeleteValue(m_hKey, lpszValue);
    }

    LSTATUS QueryValue(LPCTSTR pszValueName, DWORD* pdwType, void* pData, ULONG* pnBytes) noexcept
    {
        ATLASSERT(m_hKey != NULL);
        return ::RegQueryValueEx(m_hKey, pszValueName, NULL, pdwType, (LPBYTE)pData, pnBytes);
    }

    LSTATUS QueryDWORDValue(LPCTSTR pszValueName, DWORD& dwValue) noexcept
    {
        ULONG nBytes = sizeof(DWORD);
        DWORD dwType = 0;
        LSTATUS lRes = QueryValue(pszValueName, &dwType, &dwValue, &nBytes);
        if (lRes == ERROR_SUCCESS && dwType != REG_DWORD)
            lRes = ERROR_INVALID_DATA;
        return lRes;
    }

    LSTATUS QueryStringValue(LPCTSTR pszValueName, LPTSTR pszValue, ULONG* pnChars) noexcept
    {
        ULONG nBytes = (*pnChars) * sizeof(TCHAR);
        DWORD dwType = 0;
        LSTATUS lRes = QueryValue(pszValueName, &dwType, pszValue, &nBytes);
        if (lRes == ERROR_SUCCESS && dwType != REG_SZ && dwType != REG_EXPAND_SZ)
            lRes = ERROR_INVALID_DATA;
        if (lRes == ERROR_SUCCESS)
            *pnChars = nBytes / sizeof(TCHAR);
        return lRes;
    }

    LSTATUS SetValue(LPCTSTR pszValueName, DWORD dwType, const void* pValue, ULONG nBytes) noexcept
    {
        ATLASSERT(m_hKey != NULL);
        return ::RegSetValueEx(m_hKey, pszValueName, 0, dwType, (const BYTE*)pValue, nBytes);
    }

    LSTATUS SetDWORDValue(LPCTSTR pszValueName, DWORD dwValue) noexcept
    {
        return SetValue(pszValueName, REG_DWORD, &dwValue, sizeof(DWORD));
    }

    LSTATUS SetStringValue(LPCTSTR pszValueName, LPCTSTR pszValue, DWORD dwType = REG_SZ) noexcept
    {
        ATLASSERT(pszValue != NULL);
        return SetValue(pszValueName, dwType, pszValue,
            (ULONG)(_tcslen(pszValue) + 1) * sizeof(TCHAR));
    }

    LSTATUS QueryBinaryValue(LPCTSTR pszValueName, void* pValue, ULONG* pnBytes) noexcept
    {
        ATLASSERT(pnBytes != NULL);
        DWORD dwType = 0;
        LSTATUS lRes = ::RegQueryValueEx(m_hKey, pszValueName, NULL, &dwType, (LPBYTE)pValue, pnBytes);
        if (lRes != ERROR_SUCCESS) return lRes;
        if (dwType != REG_BINARY) return ERROR_INVALID_DATA;
        return ERROR_SUCCESS;
    }

    LSTATUS SetBinaryValue(LPCTSTR pszValueName, const void* pValue, ULONG nBytes) noexcept
    {
        return SetValue(pszValueName, REG_BINARY, pValue, nBytes);
    }

    LSTATUS RecurseDeleteKey(LPCTSTR lpszKey) noexcept
    {
        CRegKey key;
        LSTATUS lRes = key.Open(m_hKey, lpszKey, KEY_READ | KEY_WRITE);
        if (lRes != ERROR_SUCCESS)
            return lRes;

        FILETIME time;
        DWORD dwSize = 256;
        TCHAR szBuffer[256];
        while (::RegEnumKeyEx(key.m_hKey, 0, szBuffer, &dwSize, NULL, NULL, NULL, &time) == ERROR_SUCCESS) {
            lRes = key.RecurseDeleteKey(szBuffer);
            if (lRes != ERROR_SUCCESS)
                return lRes;
            dwSize = 256;
        }
        key.Close();
        return DeleteSubKey(lpszKey);
    }

private:
    CRegKey(const CRegKey&) = delete;
    CRegKey& operator=(const CRegKey&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
// CComAllocator

class CComAllocator {
public:
    static void* Allocate(size_t nBytes) noexcept
    {
        return ::CoTaskMemAlloc(nBytes);
    }

    static void* Reallocate(void* p, size_t nBytes) noexcept
    {
        return ::CoTaskMemRealloc(p, nBytes);
    }

    static void Free(void* p) noexcept
    {
        ::CoTaskMemFree(p);
    }
};

///////////////////////////////////////////////////////////////////////////////
// InlineIsEqualUnknown

inline BOOL InlineIsEqualUnknown(REFGUID rguid) noexcept
{
    return (
        ((DWORD*)&rguid)[0] == 0 &&
        ((DWORD*)&rguid)[1] == 0 &&
        ((DWORD*)&rguid)[2] == 0x000000C0 &&
        ((DWORD*)&rguid)[3] == 0x46000000);
}

///////////////////////////////////////////////////////////////////////////////
// AtlHresultFromLastError

inline HRESULT AtlHresultFromLastError() noexcept
{
    DWORD dwErr = ::GetLastError();
    return HRESULT_FROM_WIN32(dwErr);
}

///////////////////////////////////////////////////////////////////////////////
// _ATL_RT_DLGINIT resource type

#ifndef _ATL_RT_DLGINIT
#define _ATL_RT_DLGINIT MAKEINTRESOURCE(240)
#endif

///////////////////////////////////////////////////////////////////////////////
// _DialogSplitHelper - dialog template helpers

namespace _DialogSplitHelper {

#pragma pack(push, 1)
struct DLGTEMPLATEEX
{
    WORD dlgVer;
    WORD signature;
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    WORD cDlgItems;
    short x;
    short y;
    short cx;
    short cy;
};

struct DLGITEMTEMPLATEEX
{
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    short x;
    short y;
    short cx;
    short cy;
    DWORD id;
};
#pragma pack(pop)

inline BOOL IsDialogEx(const DLGTEMPLATE* pTemplate) noexcept
{
    return ((DLGTEMPLATEEX*)pTemplate)->signature == 0xFFFF;
}

inline WORD DlgTemplateItemCount(const DLGTEMPLATE* pTemplate) noexcept
{
    if (IsDialogEx(pTemplate))
        return ((DLGTEMPLATEEX*)pTemplate)->cDlgItems;
    else
        return pTemplate->cdit;
}

inline BOOL IsActiveXControl(DLGITEMTEMPLATE* /*pItem*/, BOOL /*bDialogEx*/) noexcept
{
    return FALSE;
}

inline DLGITEMTEMPLATE* FindFirstDlgItem(const DLGTEMPLATE* pTemplate) noexcept
{
    BOOL bDialogEx = IsDialogEx(pTemplate);
    WORD* pw;
    if (bDialogEx) {
        pw = (WORD*)((DLGTEMPLATEEX*)pTemplate + 1);
    } else {
        pw = (WORD*)(pTemplate + 1);
    }
    // Skip menu
    if (*pw == 0xFFFF) pw += 2; else while (*pw++) {}
    // Skip class
    if (*pw == 0xFFFF) pw += 2; else while (*pw++) {}
    // Skip title
    while (*pw++) {}
    // Skip font if DS_SETFONT
    DWORD dwStyle = bDialogEx ? ((DLGTEMPLATEEX*)pTemplate)->style : pTemplate->style;
    if (dwStyle & DS_SETFONT) {
        pw++; // size
        if (bDialogEx) { pw++; pw++; } // weight, italic
        while (*pw++) {} // typeface
    }
    // Align to DWORD
    pw = (WORD*)(((ULONG_PTR)pw + 3) & ~3);
    return (DLGITEMTEMPLATE*)pw;
}

inline DLGITEMTEMPLATE* FindNextDlgItem(DLGITEMTEMPLATE* pItem, BOOL bDialogEx) noexcept
{
    WORD* pw;
    if (bDialogEx)
        pw = (WORD*)((DLGITEMTEMPLATEEX*)pItem + 1);
    else
        pw = (WORD*)(pItem + 1);
    // Skip class
    if (*pw == 0xFFFF) pw += 2; else while (*pw++) {}
    // Skip title
    if (*pw == 0xFFFF) pw += 2; else while (*pw++) {}
    // Skip creation data
    WORD cbExtra = *pw++;
    pw = (WORD*)((BYTE*)pw + cbExtra);
    // Align to DWORD
    pw = (WORD*)(((ULONG_PTR)pw + 3) & ~3);
    return (DLGITEMTEMPLATE*)pw;
}

inline LPCDLGTEMPLATE SplitDialogTemplate(DLGTEMPLATE* pTemplate, BYTE*& pInitData) noexcept
{
    pInitData = NULL;
    return pTemplate;
}

inline DWORD FindCreateData(DWORD /*wID*/, BYTE* /*pInitData*/, BYTE** ppData) noexcept
{
    if (ppData) *ppData = NULL;
    return 0;
}

inline HRESULT ParseInitData(IStream* /*pStream*/, BSTR* /*pbstrLicKey*/) noexcept
{
    return E_NOTIMPL;
}

} // namespace _DialogSplitHelper

///////////////////////////////////////////////////////////////////////////////
// Forward declarations

struct _AtlCreateWndData;
class CAtlWinModule;
class CAtlModule;
class CComModule;

// Global module pointers (set by constructors of CAtlModule / CComModule)
__declspec(selectany) CAtlModule* _pAtlModule = NULL;
__declspec(selectany) CComModule* _pModule = NULL;

///////////////////////////////////////////////////////////////////////////////
// _AtlCreateWndData

struct _AtlCreateWndData {
    void* m_pThis;
    DWORD m_dwThreadID;
    _AtlCreateWndData* m_pNext;
};

///////////////////////////////////////////////////////////////////////////////
// _ATL_WIN_MODULE

struct _ATL_WIN_MODULE70 {
    UINT cbSize;
    CComCriticalSection m_csWindowCreate;
    _AtlCreateWndData* m_pCreateWndList;
    CSimpleArray<ATOM> m_rgWindowClassAtoms;
};

typedef _ATL_WIN_MODULE70 _ATL_WIN_MODULE;

///////////////////////////////////////////////////////////////////////////////
// _ATL_COM_MODULE

struct _ATL_COM_MODULE70 {
    UINT cbSize;
    HINSTANCE m_hInstTypeLib;
    CComCriticalSection m_csObjMap;
};

typedef _ATL_COM_MODULE70 _ATL_COM_MODULE;

///////////////////////////////////////////////////////////////////////////////
// _ATL_MODULE

struct _ATL_MODULE70 {
    UINT cbSize;
    LONG m_nLockCnt;
    CComCriticalSection m_csStaticDataInitAndTypeInfo;
};

typedef _ATL_MODULE70 _ATL_MODULE;

///////////////////////////////////////////////////////////////////////////////
// _ATL_OBJMAP_ENTRY / _ATL_INTMAP_ENTRY

struct _ATL_OBJMAP_ENTRY30 {
    const CLSID* pclsid;
    HRESULT (WINAPI *pfnUpdateRegistry)(BOOL bRegister);
    HRESULT (WINAPI *pfnGetClassObject)(void* pv, REFIID riid, LPVOID* ppv);
    HRESULT (WINAPI *pfnCreateInstance)(void* pv, REFIID riid, LPVOID* ppv);
    IUnknown* pCF;
    DWORD dwRegister;
    void* pfnObjectMain;
};

typedef _ATL_OBJMAP_ENTRY30 _ATL_OBJMAP_ENTRY;

#define _ATL_SIMPLEMAPENTRY ((ATL::_ATL_CREATORDATA*)1)

struct _ATL_CREATORDATA {
    HRESULT (WINAPI *pfnCreateInstance)(void*, REFIID, LPVOID*);
};

struct _ATL_INTMAP_ENTRY {
    const IID* piid;
    DWORD_PTR dw;
    HRESULT (WINAPI *pFunc)(void* pv, REFIID riid, LPVOID* ppv, DWORD_PTR dw);
};

struct _ATL_REGMAP_ENTRY {
    LPCOLESTR szKey;
    LPCOLESTR szData;
};

///////////////////////////////////////////////////////////////////////////////
// AtlInternalQueryInterface

inline HRESULT WINAPI AtlInternalQueryInterface(
    void* pThis,
    const _ATL_INTMAP_ENTRY* pEntries,
    REFIID iid,
    void** ppvObject) noexcept
{
    ATLASSERT(pThis != NULL);
    ATLASSERT(pEntries != NULL);
    ATLASSERT(ppvObject != NULL);

    if (ppvObject == NULL)
        return E_POINTER;

    *ppvObject = NULL;

    if (InlineIsEqualUnknown(iid)) {
        // First non-NULL entry is the IUnknown
        for (; pEntries->pFunc != NULL; pEntries++) {
            if (pEntries->piid != NULL) {
                IUnknown* pUnk = (IUnknown*)((LONG_PTR)pThis + pEntries->dw);
                *ppvObject = pUnk;
                pUnk->AddRef();
                return S_OK;
            }
        }
        return E_NOINTERFACE;
    }

    for (; pEntries->pFunc != NULL; pEntries++) {
        if (pEntries->piid == NULL)
            continue;

        if (pEntries->pFunc == (HRESULT(WINAPI*)(void*, REFIID, LPVOID*, DWORD_PTR))1) {
            // _ATL_SIMPLEMAPENTRY - offset only
            if (IsEqualGUID(iid, *pEntries->piid)) {
                IUnknown* pUnk = (IUnknown*)((LONG_PTR)pThis + pEntries->dw);
                *ppvObject = pUnk;
                pUnk->AddRef();
                return S_OK;
            }
        } else {
            // Custom function
            HRESULT hr = pEntries->pFunc(pThis, iid, ppvObject, pEntries->dw);
            if (hr == S_OK || (FAILED(hr) && hr != E_NOINTERFACE))
                return hr;
        }
    }

    return E_NOINTERFACE;
}

///////////////////////////////////////////////////////////////////////////////
// CAtlModule - base module class

class CAtlModule : public _ATL_MODULE {
public:
    CAtlModule() noexcept
    {
        cbSize = sizeof(_ATL_MODULE);
        m_nLockCnt = 0;
        m_csStaticDataInitAndTypeInfo.Init();
        _pAtlModule = this;
    }

    virtual ~CAtlModule()
    {
        m_csStaticDataInitAndTypeInfo.Term();
    }

    virtual LONG Lock() noexcept
    {
        return CComGlobalsThreadModel::Increment(&m_nLockCnt);
    }

    virtual LONG Unlock() noexcept
    {
        return CComGlobalsThreadModel::Decrement(&m_nLockCnt);
    }

    virtual LONG GetLockCount() noexcept
    {
        return m_nLockCnt;
    }

    HRESULT AddCommonRGSReplacements(void* /*pRegistrar*/) noexcept
    {
        // Stub - no registry support
        return S_OK;
    }

    virtual HRESULT AddCommonRGSReplacements(IUnknown* /*pRegistrar*/) noexcept
    {
        return S_OK;
    }
};

template <typename T>
class CAtlModuleT : public CAtlModule {
public:
    HRESULT RegisterServer(BOOL /*bRegTypeLib*/ = FALSE, const CLSID* /*pCLSID*/ = NULL) noexcept
    {
        return S_OK;
    }

    HRESULT UnregisterServer(BOOL /*bUnRegTypeLib*/, const CLSID* /*pCLSID*/ = NULL) noexcept
    {
        return S_OK;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CAtlComModule

class CAtlComModule : public _ATL_COM_MODULE {
public:
    CAtlComModule() noexcept
    {
        cbSize = sizeof(_ATL_COM_MODULE);
        m_hInstTypeLib = NULL;
        m_csObjMap.Init();
    }

    ~CAtlComModule()
    {
        m_csObjMap.Term();
    }
};

__declspec(selectany) CAtlComModule _AtlComModule;

///////////////////////////////////////////////////////////////////////////////
// CAtlWinModule

class CAtlWinModule : public _ATL_WIN_MODULE {
public:
    CAtlWinModule() noexcept
    {
        cbSize = sizeof(_ATL_WIN_MODULE);
        m_pCreateWndList = NULL;
        m_csWindowCreate.Init();
    }

    ~CAtlWinModule()
    {
        m_csWindowCreate.Term();
    }

    void AddCreateWndData(_AtlCreateWndData* pData, void* pObject) noexcept
    {
        ATLASSERT(pData != NULL && pObject != NULL);
        pData->m_pThis = pObject;
        pData->m_dwThreadID = ::GetCurrentThreadId();

        CComCritSecLock<CComCriticalSection> lock(m_csWindowCreate, false);
        if (SUCCEEDED(lock.Lock())) {
            pData->m_pNext = m_pCreateWndList;
            m_pCreateWndList = pData;
        }
    }

    void* ExtractCreateWndData() noexcept
    {
        CComCritSecLock<CComCriticalSection> lock(m_csWindowCreate, false);
        if (FAILED(lock.Lock()))
            return NULL;

        void* pv = NULL;
        DWORD dwThreadID = ::GetCurrentThreadId();
        _AtlCreateWndData* pEntry = m_pCreateWndList;
        _AtlCreateWndData* pPrev = NULL;

        while (pEntry != NULL) {
            if (pEntry->m_dwThreadID == dwThreadID) {
                pv = pEntry->m_pThis;
                if (pPrev == NULL)
                    m_pCreateWndList = pEntry->m_pNext;
                else
                    pPrev->m_pNext = pEntry->m_pNext;
                break;
            }
            pPrev = pEntry;
            pEntry = pEntry->m_pNext;
        }
        return pv;
    }
};

__declspec(selectany) CAtlWinModule _AtlWinModule;

///////////////////////////////////////////////////////////////////////////////
// Free-standing functions (legacy compatibility)

inline void AtlWinModuleAddCreateWndData(_ATL_WIN_MODULE* pWinModule, _AtlCreateWndData* pData, void* pObject) noexcept
{
    (void)pWinModule;
    _AtlWinModule.AddCreateWndData(pData, pObject);
}

inline void* AtlWinModuleExtractCreateWndData(_ATL_WIN_MODULE* pWinModule) noexcept
{
    (void)pWinModule;
    return _AtlWinModule.ExtractCreateWndData();
}

///////////////////////////////////////////////////////////////////////////////
// CComModule - legacy module class

class CComModule : public CAtlModuleT<CComModule> {
public:
    HINSTANCE m_hInst;
    HINSTANCE m_hInstResource;
    HINSTANCE m_hInstTypeLib;
    _ATL_OBJMAP_ENTRY* m_pObjMap;

    CComModule() noexcept
        : m_hInst(NULL), m_hInstResource(NULL), m_hInstTypeLib(NULL), m_pObjMap(NULL)
    {
        _pModule = this;
    }

    HRESULT Init(_ATL_OBJMAP_ENTRY* p, HINSTANCE h, const GUID* /*plibid*/ = NULL) noexcept
    {
        m_pObjMap = p;
        m_hInst = h;
        m_hInstResource = h;
        m_hInstTypeLib = h;

        _AtlBaseModule.m_hInst = h;
        _AtlBaseModule.m_hInstResource = h;

        if (p != NULL) {
            while (p->pclsid != NULL) {
                p->pCF = NULL;
                p++;
            }
        }

        return S_OK;
    }

    void Term() noexcept
    {
        if (m_pObjMap != NULL) {
            _ATL_OBJMAP_ENTRY* p = m_pObjMap;
            while (p->pclsid != NULL) {
                if (p->pCF != NULL) {
                    p->pCF->Release();
                    p->pCF = NULL;
                }
                p++;
            }
        }
    }

    HRESULT GetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) noexcept
    {
        if (ppv == NULL)
            return E_POINTER;
        *ppv = NULL;

        if (m_pObjMap == NULL)
            return CLASS_E_CLASSNOTAVAILABLE;

        _ATL_OBJMAP_ENTRY* p = m_pObjMap;
        while (p->pclsid != NULL) {
            if (IsEqualCLSID(*p->pclsid, rclsid)) {
                if (p->pfnGetClassObject != NULL)
                    return p->pfnGetClassObject(p->pCF ? (void*)p->pCF : NULL, riid, ppv);
                return CLASS_E_CLASSNOTAVAILABLE;
            }
            p++;
        }
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    HINSTANCE GetModuleInstance() const noexcept { return m_hInst; }
    HINSTANCE GetResourceInstance() const noexcept { return m_hInstResource; }
    HINSTANCE GetTypeLibInstance() const noexcept { return m_hInstTypeLib; }
};

///////////////////////////////////////////////////////////////////////////////
// AtlGetCommCtrlVersion - forward declaration
// Full implementation is provided by WTL's atlapp.h for _ATL_VER >= 0x0B00

HRESULT AtlGetCommCtrlVersion(LPDWORD pdwMajor, LPDWORD pdwMinor);

///////////////////////////////////////////////////////////////////////////////
// AtlAxWinInit - stub for ActiveX hosting (not supported)

inline BOOL AtlAxWinInit() noexcept
{
    return TRUE;
}

} // namespace ATL

// For headers that use the ATL:: prefix
using ATL::_AtlBaseModule;
using ATL::_AtlWinModule;
using ATL::_AtlComModule;
using ATL::_pAtlModule;
using ATL::_pModule;

#endif // __ATLBASE_H__
