// OpenATL - Clean-room ATL subset for WTL 10.0
// Core threading primitives and base module

#ifndef __ATLCORE_H__
#define __ATLCORE_H__

#pragma once

#include "atldef.h"
#include "atltrace.h"

namespace ATL {

///////////////////////////////////////////////////////////////////////////////
// Critical Section wrappers

class CComCriticalSection {
public:
    CRITICAL_SECTION m_sec;

    CComCriticalSection() noexcept
    {
        memset(&m_sec, 0, sizeof(CRITICAL_SECTION));
    }

    ~CComCriticalSection() {}

    HRESULT Lock() noexcept
    {
        ::EnterCriticalSection(&m_sec);
        return S_OK;
    }

    HRESULT Unlock() noexcept
    {
        ::LeaveCriticalSection(&m_sec);
        return S_OK;
    }

    HRESULT Init() noexcept
    {
        ::InitializeCriticalSection(&m_sec);
        return S_OK;
    }

    HRESULT Term() noexcept
    {
        ::DeleteCriticalSection(&m_sec);
        return S_OK;
    }
};

class CComFakeCriticalSection {
public:
    HRESULT Lock() noexcept { return S_OK; }
    HRESULT Unlock() noexcept { return S_OK; }
    HRESULT Init() noexcept { return S_OK; }
    HRESULT Term() noexcept { return S_OK; }
};

class CComAutoCriticalSection : public CComCriticalSection {
public:
    CComAutoCriticalSection()
    {
        CComCriticalSection::Init();
    }
    ~CComAutoCriticalSection()
    {
        CComCriticalSection::Term();
    }
private:
    CComAutoCriticalSection(const CComAutoCriticalSection&) = delete;
    CComAutoCriticalSection& operator=(const CComAutoCriticalSection&) = delete;
};

class CComSafeDeleteCriticalSection : public CComCriticalSection {
public:
    bool m_bInitialized;

    CComSafeDeleteCriticalSection() noexcept : m_bInitialized(false) {}

    ~CComSafeDeleteCriticalSection()
    {
        if (m_bInitialized) {
            m_bInitialized = false;
            CComCriticalSection::Term();
        }
    }

    HRESULT Init() noexcept
    {
        if (!m_bInitialized) {
            HRESULT hr = CComCriticalSection::Init();
            if (SUCCEEDED(hr))
                m_bInitialized = true;
            return hr;
        }
        return S_OK;
    }

    HRESULT Term() noexcept
    {
        if (m_bInitialized) {
            m_bInitialized = false;
            return CComCriticalSection::Term();
        }
        return S_OK;
    }

    HRESULT Lock() noexcept
    {
        ATLASSERT(m_bInitialized);
        return CComCriticalSection::Lock();
    }
};

class CComAutoDeleteCriticalSection : public CComSafeDeleteCriticalSection {
public:
    ~CComAutoDeleteCriticalSection()
    {
        // parent destructor handles cleanup
    }
private:
    HRESULT Term() noexcept; // intentionally not implemented
};

///////////////////////////////////////////////////////////////////////////////
// String resource image support

#pragma pack(push, 2)
struct ATLSTRINGRESOURCEIMAGE {
    WORD nLength;
    WCHAR achString[];
};
#pragma pack(pop)

inline const ATLSTRINGRESOURCEIMAGE* AtlGetStringResourceImage(
    HINSTANCE hInstance, UINT id) noexcept
{
    HRSRC hResource = ::FindResourceW(hInstance, MAKEINTRESOURCEW(((id >> 4) + 1)), (LPWSTR)RT_STRING);
    if (hResource == NULL)
        return NULL;

    HGLOBAL hGlobal = ::LoadResource(hInstance, hResource);
    if (hGlobal == NULL)
        return NULL;

    const ATLSTRINGRESOURCEIMAGE* pImage = (const ATLSTRINGRESOURCEIMAGE*)::LockResource(hGlobal);
    if (pImage == NULL)
        return NULL;

    UINT iIndex = id & 0x000F;
    for (UINT x = 0; x < iIndex; x++) {
        pImage = (const ATLSTRINGRESOURCEIMAGE*)(((const BYTE*)pImage) + (sizeof(ATLSTRINGRESOURCEIMAGE) + (pImage->nLength * sizeof(WCHAR))));
    }

    return pImage;
}

inline const ATLSTRINGRESOURCEIMAGE* AtlGetStringResourceImage(
    HINSTANCE hInstance, UINT id, WORD wLanguage) noexcept
{
    HRSRC hResource = ::FindResourceExW(hInstance, (LPWSTR)RT_STRING, MAKEINTRESOURCEW(((id >> 4) + 1)), wLanguage);
    if (hResource == NULL)
        return NULL;

    HGLOBAL hGlobal = ::LoadResource(hInstance, hResource);
    if (hGlobal == NULL)
        return NULL;

    const ATLSTRINGRESOURCEIMAGE* pImage = (const ATLSTRINGRESOURCEIMAGE*)::LockResource(hGlobal);
    if (pImage == NULL)
        return NULL;

    UINT iIndex = id & 0x000F;
    for (UINT x = 0; x < iIndex; x++) {
        pImage = (const ATLSTRINGRESOURCEIMAGE*)(((const BYTE*)pImage) + (sizeof(ATLSTRINGRESOURCEIMAGE) + (pImage->nLength * sizeof(WCHAR))));
    }

    return pImage;
}

///////////////////////////////////////////////////////////////////////////////
// _ATL_BASE_MODULE / CAtlBaseModule

struct _ATL_BASE_MODULE70 {
    UINT cbSize;
    HINSTANCE m_hInst;
    HINSTANCE m_hInstResource;
    DWORD dwAtlBuildVer;
};

typedef _ATL_BASE_MODULE70 _ATL_BASE_MODULE;

class CAtlBaseModule : public _ATL_BASE_MODULE {
public:
    CAtlBaseModule() noexcept
    {
        cbSize = sizeof(_ATL_BASE_MODULE);
        m_hInst = NULL;
        m_hInstResource = NULL;
        dwAtlBuildVer = _ATL_VER;

        // Get the module handle for the current executable
        m_hInst = ::GetModuleHandleW(NULL);
        m_hInstResource = m_hInst;
    }

    HINSTANCE GetModuleInstance() const noexcept
    {
        return m_hInst;
    }

    HINSTANCE GetResourceInstance() const noexcept
    {
        return m_hInstResource;
    }

    HINSTANCE SetResourceInstance(HINSTANCE hInst) noexcept
    {
        HINSTANCE hOld = m_hInstResource;
        m_hInstResource = hInst;
        return hOld;
    }
};

__declspec(selectany) CAtlBaseModule _AtlBaseModule;

inline HINSTANCE AtlFindStringResourceInstance(UINT nID, WORD wLanguage = 0) noexcept
{
    if (wLanguage != 0) {
        if (AtlGetStringResourceImage(_AtlBaseModule.GetResourceInstance(), nID, wLanguage) != NULL)
            return _AtlBaseModule.GetResourceInstance();
    } else {
        if (AtlGetStringResourceImage(_AtlBaseModule.GetResourceInstance(), nID) != NULL)
            return _AtlBaseModule.GetResourceInstance();
    }
    return _AtlBaseModule.GetModuleInstance();
}

} // namespace ATL

#endif // __ATLCORE_H__
