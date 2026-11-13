// OpenATL - Clean-room ATL subset for WTL 10.0
// COM client utilities

#ifndef __ATLCOMCLI_H__
#define __ATLCOMCLI_H__

#pragma once

#include "atldef.h"

#include <ole2.h>
#include <memory>

namespace ATL {

///////////////////////////////////////////////////////////////////////////////
// _NoAddRefReleaseOnCComPtr - prevents calling AddRef/Release directly on CComPtr

template <typename T>
class _NoAddRefReleaseOnCComPtr : public T {
private:
    STDMETHOD_(ULONG, AddRef)() = 0;
    STDMETHOD_(ULONG, Release)() = 0;
};

///////////////////////////////////////////////////////////////////////////////
// CComPtr - COM smart pointer

template <typename T>
class CComPtr {
public:
    T* p;

    CComPtr() noexcept : p(NULL) {}

    CComPtr(T* lp) noexcept : p(lp)
    {
        if (p != NULL)
            p->AddRef();
    }

    CComPtr(const CComPtr<T>& lp) noexcept : p(lp.p)
    {
        if (p != NULL)
            p->AddRef();
    }

    CComPtr(CComPtr<T>&& lp) noexcept : p(lp.p)
    {
        lp.p = NULL;
    }

    ~CComPtr()
    {
        if (p != NULL)
            p->Release();
    }

    T* operator=(T* lp) noexcept
    {
        if (p != lp) {
            T* pOld = p;
            p = lp;
            if (p != NULL)
                p->AddRef();
            if (pOld != NULL)
                pOld->Release();
        }
        return *this;
    }

    T* operator=(const CComPtr<T>& lp) noexcept
    {
        return operator=(lp.p);
    }

    CComPtr<T>& operator=(CComPtr<T>&& lp) noexcept
    {
        if (this != &lp) {
            if (p != NULL)
                p->Release();
            p = lp.p;
            lp.p = NULL;
        }
        return *this;
    }

    operator T*() const noexcept
    {
        return p;
    }

    T& operator*() const
    {
        ATLASSERT(p != NULL);
        return *p;
    }

    _NoAddRefReleaseOnCComPtr<T>* operator->() const noexcept
    {
        ATLASSERT(p != NULL);
        return (_NoAddRefReleaseOnCComPtr<T>*)p;
    }

    T** operator&() noexcept
    {
        ATLASSERT(p == NULL);
        return &p;
    }

    bool operator!() const noexcept
    {
        return (p == NULL);
    }

    bool operator<(T* pT) const noexcept
    {
        return p < pT;
    }

    bool operator!=(T* pT) const noexcept
    {
        return !operator==(pT);
    }

    bool operator==(T* pT) const noexcept
    {
        return p == pT;
    }

    T* Detach() noexcept
    {
        T* pt = p;
        p = NULL;
        return pt;
    }

    void Attach(T* p2) noexcept
    {
        if (p != NULL)
            p->Release();
        p = p2;
    }

    void Release() noexcept
    {
        T* pTemp = p;
        if (pTemp != NULL) {
            p = NULL;
            pTemp->Release();
        }
    }

    HRESULT CopyTo(T** ppT) noexcept
    {
        ATLASSERT(ppT != NULL);
        if (ppT == NULL)
            return E_POINTER;
        *ppT = p;
        if (p != NULL)
            p->AddRef();
        return S_OK;
    }

    template <typename Q>
    HRESULT QueryInterface(Q** pp) const noexcept
    {
        ATLASSERT(pp != NULL);
        return p->QueryInterface(__uuidof(Q), (void**)pp);
    }

    HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter = NULL,
        DWORD dwClsContext = CLSCTX_ALL) noexcept
    {
        ATLASSERT(p == NULL);
        return ::CoCreateInstance(rclsid, pUnkOuter, dwClsContext, __uuidof(T), (void**)&p);
    }

    HRESULT CoCreateInstance(LPCOLESTR szProgID, LPUNKNOWN pUnkOuter = NULL,
        DWORD dwClsContext = CLSCTX_ALL) noexcept
    {
        CLSID clsid;
        HRESULT hr = ::CLSIDFromProgID(szProgID, &clsid);
        ATLASSERT(p == NULL);
        if (SUCCEEDED(hr))
            hr = ::CoCreateInstance(clsid, pUnkOuter, dwClsContext, __uuidof(T), (void**)&p);
        return hr;
    }

    bool IsEqualObject(IUnknown* pOther) noexcept
    {
        if (p == NULL && pOther == NULL)
            return true;
        if (p == NULL || pOther == NULL)
            return false;

        CComPtr<IUnknown> punk1;
        CComPtr<IUnknown> punk2;
        p->QueryInterface(__uuidof(IUnknown), (void**)&punk1);
        pOther->QueryInterface(__uuidof(IUnknown), (void**)&punk2);
        return punk1 == punk2;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CComQIPtr - COM query interface smart pointer

template <typename T, const IID* piid = &__uuidof(T)>
class CComQIPtr : public CComPtr<T> {
public:
    CComQIPtr() noexcept {}

    CComQIPtr(T* lp) noexcept : CComPtr<T>(lp) {}

    CComQIPtr(const CComQIPtr<T, piid>& lp) noexcept : CComPtr<T>(lp.p) {}

    CComQIPtr(IUnknown* lp) noexcept
    {
        if (lp != NULL)
            lp->QueryInterface(*piid, (void**)&this->p);
    }

    T* operator=(T* lp) noexcept
    {
        return CComPtr<T>::operator=(lp);
    }

    T* operator=(const CComQIPtr<T, piid>& lp) noexcept
    {
        return CComPtr<T>::operator=(lp.p);
    }

    T* operator=(IUnknown* lp) noexcept
    {
        T* pOld = this->p;
        this->p = NULL;
        if (lp != NULL)
            lp->QueryInterface(*piid, (void**)&this->p);
        if (pOld != NULL)
            pOld->Release();
        return this->p;
    }
};

// Specialization for IUnknown
template <>
class CComQIPtr<IUnknown, &IID_IUnknown> : public CComPtr<IUnknown> {
public:
    CComQIPtr() noexcept {}
    CComQIPtr(IUnknown* lp) noexcept : CComPtr<IUnknown>(lp) {}
    CComQIPtr(const CComQIPtr<IUnknown, &IID_IUnknown>& lp) noexcept : CComPtr<IUnknown>(lp.p) {}

    IUnknown* operator=(IUnknown* lp) noexcept
    {
        return CComPtr<IUnknown>::operator=(lp);
    }

    IUnknown* operator=(const CComQIPtr<IUnknown, &IID_IUnknown>& lp) noexcept
    {
        return CComPtr<IUnknown>::operator=(lp.p);
    }
};

///////////////////////////////////////////////////////////////////////////////
// CComBSTR - BSTR wrapper

class CComBSTR {
public:
    BSTR m_str;

    CComBSTR() noexcept : m_str(NULL) {}

    CComBSTR(int nSize, LPCOLESTR sz = NULL)
        : m_str(::SysAllocStringLen(sz, nSize))
    {
    }

    CComBSTR(LPCOLESTR pSrc)
        : m_str(::SysAllocString(pSrc))
    {
    }

    CComBSTR(const CComBSTR& src) : m_str(NULL)
    {
        if (src.m_str != NULL)
            m_str = ::SysAllocStringLen(src.m_str, ::SysStringLen(src.m_str));
    }

    CComBSTR(CComBSTR&& src) noexcept : m_str(src.m_str)
    {
        src.m_str = NULL;
    }

    CComBSTR(LPCSTR pSrc) : m_str(NULL)
    {
        if (pSrc != NULL) {
            int nLen = ::MultiByteToWideChar(CP_ACP, 0, pSrc, -1, NULL, 0);
            if (nLen > 0) {
                m_str = ::SysAllocStringLen(NULL, nLen - 1);
                if (m_str != NULL)
                    ::MultiByteToWideChar(CP_ACP, 0, pSrc, -1, m_str, nLen);
            }
        }
    }

    ~CComBSTR()
    {
        ::SysFreeString(m_str);
    }

    CComBSTR& operator=(const CComBSTR& src)
    {
        if (this != &src) {
            ::SysFreeString(m_str);
            m_str = ::SysAllocStringLen(src.m_str, ::SysStringLen(src.m_str));
        }
        return *this;
    }

    CComBSTR& operator=(CComBSTR&& src) noexcept
    {
        if (this != std::addressof(src)) {
            ::SysFreeString(m_str);
            m_str = src.m_str;
            src.m_str = NULL;
        }
        return *this;
    }

    CComBSTR& operator=(LPCOLESTR pSrc)
    {
        ::SysFreeString(m_str);
        m_str = ::SysAllocString(pSrc);
        return *this;
    }

    operator BSTR() const noexcept { return m_str; }
    BSTR* operator&() noexcept { return &m_str; }

    unsigned int Length() const noexcept
    {
        return ::SysStringLen(m_str);
    }

    unsigned int ByteLength() const noexcept
    {
        return ::SysStringByteLen(m_str);
    }

    BSTR Copy() const
    {
        if (m_str == NULL)
            return NULL;
        return ::SysAllocStringLen(m_str, ::SysStringLen(m_str));
    }

    HRESULT CopyTo(BSTR* pbstr) const
    {
        ATLASSERT(pbstr != NULL);
        if (pbstr == NULL)
            return E_POINTER;
        *pbstr = Copy();
        if (m_str != NULL && *pbstr == NULL)
            return E_OUTOFMEMORY;
        return S_OK;
    }

    void Attach(BSTR src) noexcept
    {
        if (m_str != src) {
            ::SysFreeString(m_str);
            m_str = src;
        }
    }

    BSTR Detach() noexcept
    {
        BSTR s = m_str;
        m_str = NULL;
        return s;
    }

    void Empty() noexcept
    {
        ::SysFreeString(m_str);
        m_str = NULL;
    }

    bool operator!() const noexcept
    {
        return m_str == NULL;
    }

    HRESULT Append(LPCOLESTR lpsz, int nLen) noexcept
    {
        if (lpsz == NULL || nLen == 0)
            return S_OK;
        int nOldLen = Length();
        BSTR bstrNew = ::SysAllocStringLen(NULL, nOldLen + nLen);
        if (bstrNew == NULL)
            return E_OUTOFMEMORY;
        if (m_str != NULL)
            memcpy(bstrNew, m_str, nOldLen * sizeof(OLECHAR));
        memcpy(bstrNew + nOldLen, lpsz, nLen * sizeof(OLECHAR));
        bstrNew[nOldLen + nLen] = L'\0';
        ::SysFreeString(m_str);
        m_str = bstrNew;
        return S_OK;
    }

    HRESULT Append(LPCOLESTR lpsz) noexcept
    {
        if (lpsz == NULL)
            return S_OK;
        return Append(lpsz, (int)wcslen(lpsz));
    }

    HRESULT Append(const CComBSTR& bstrSrc) noexcept
    {
        return Append(bstrSrc.m_str, bstrSrc.Length());
    }

    CComBSTR& operator+=(const CComBSTR& bstrSrc)
    {
        Append(bstrSrc);
        return *this;
    }

    bool operator==(const CComBSTR& bstrSrc) const noexcept
    {
        if (m_str == NULL && bstrSrc.m_str == NULL)
            return true;
        if (m_str == NULL || bstrSrc.m_str == NULL)
            return false;
        return wcscmp(m_str, bstrSrc.m_str) == 0;
    }

    bool operator!=(const CComBSTR& bstrSrc) const noexcept
    {
        return !operator==(bstrSrc);
    }

    bool operator<(const CComBSTR& bstrSrc) const noexcept
    {
        if (m_str == NULL)
            return bstrSrc.m_str != NULL;
        if (bstrSrc.m_str == NULL)
            return false;
        return wcscmp(m_str, bstrSrc.m_str) < 0;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CComVariant - VARIANT wrapper

class CComVariant : public tagVARIANT {
public:
    CComVariant() noexcept
    {
        ::VariantInit(this);
    }

    ~CComVariant()
    {
        Clear();
    }

    CComVariant(const VARIANT& varSrc)
    {
        ::VariantInit(this);
        ::VariantCopy(this, const_cast<VARIANT*>(&varSrc));
    }

    CComVariant(const CComVariant& varSrc)
    {
        ::VariantInit(this);
        ::VariantCopy(this, const_cast<VARIANT*>(static_cast<const VARIANT*>(&varSrc)));
    }

    CComVariant(LPCOLESTR lpszSrc)
    {
        ::VariantInit(this);
        vt = VT_BSTR;
        bstrVal = ::SysAllocString(lpszSrc);
    }

    CComVariant(int nSrc) noexcept
    {
        ::VariantInit(this);
        vt = VT_I4;
        lVal = nSrc;
    }

    CComVariant(long nSrc) noexcept
    {
        ::VariantInit(this);
        vt = VT_I4;
        lVal = nSrc;
    }

    CComVariant(bool bSrc) noexcept
    {
        ::VariantInit(this);
        vt = VT_BOOL;
        boolVal = bSrc ? VARIANT_TRUE : VARIANT_FALSE;
    }

    CComVariant& operator=(const CComVariant& varSrc)
    {
        ::VariantCopy(this, const_cast<VARIANT*>(static_cast<const VARIANT*>(&varSrc)));
        return *this;
    }

    CComVariant& operator=(const VARIANT& varSrc)
    {
        ::VariantCopy(this, const_cast<VARIANT*>(&varSrc));
        return *this;
    }

    CComVariant& operator=(LPCOLESTR lpszSrc)
    {
        Clear();
        vt = VT_BSTR;
        bstrVal = ::SysAllocString(lpszSrc);
        return *this;
    }

    CComVariant& operator=(int nSrc) noexcept
    {
        if (vt != VT_I4) {
            Clear();
            vt = VT_I4;
        }
        lVal = nSrc;
        return *this;
    }

    bool operator==(const VARIANT& varSrc) const noexcept
    {
        if (vt != varSrc.vt)
            return false;
        switch (vt) {
        case VT_EMPTY:
        case VT_NULL:
            return true;
        case VT_I4:
            return lVal == varSrc.lVal;
        case VT_BOOL:
            return boolVal == varSrc.boolVal;
        case VT_BSTR:
            return (::SysStringLen(bstrVal) == ::SysStringLen(varSrc.bstrVal)) &&
                (bstrVal == NULL || varSrc.bstrVal == NULL ?
                    bstrVal == varSrc.bstrVal :
                    wcscmp(bstrVal, varSrc.bstrVal) == 0);
        default:
            break;
        }
        return false;
    }

    bool operator!=(const VARIANT& varSrc) const noexcept
    {
        return !operator==(varSrc);
    }

    HRESULT Clear() noexcept
    {
        return ::VariantClear(this);
    }

    HRESULT Copy(const VARIANT* pSrc) noexcept
    {
        return ::VariantCopy(this, const_cast<VARIANT*>(pSrc));
    }

    HRESULT ChangeType(VARTYPE vtNew, const VARIANT* pSrc = NULL) noexcept
    {
        VARIANT* pVar = const_cast<VARIANT*>(pSrc);
        if (pVar == NULL)
            pVar = this;
        return ::VariantChangeType(this, pVar, 0, vtNew);
    }

    HRESULT Detach(VARIANT* pDest) noexcept
    {
        ATLASSERT(pDest != NULL);
        HRESULT hr = ::VariantClear(pDest);
        if (SUCCEEDED(hr)) {
            memcpy(pDest, this, sizeof(VARIANT));
            vt = VT_EMPTY;
        }
        return hr;
    }
};

} // namespace ATL

#endif // __ATLCOMCLI_H__
