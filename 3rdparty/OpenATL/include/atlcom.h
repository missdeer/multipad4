// OpenATL - Clean-room ATL subset for WTL 10.0
// COM implementation classes

#ifndef __ATLCOM_H__
#define __ATLCOM_H__

#pragma once

#include "atlbase.h"

namespace ATL {

///////////////////////////////////////////////////////////////////////////////
// CComObjectRootBase - base class for COM ref counting

class CComObjectRootBase {
public:
    LONG m_dwRef;

    CComObjectRootBase() noexcept : m_dwRef(0) {}
    ~CComObjectRootBase() {}

    HRESULT FinalConstruct() noexcept { return S_OK; }
    void FinalRelease() noexcept {}

    static HRESULT WINAPI InternalQueryInterface(void* pThis,
        const _ATL_INTMAP_ENTRY* pEntries, REFIID iid, void** ppvObject) noexcept
    {
        return AtlInternalQueryInterface(pThis, pEntries, iid, ppvObject);
    }

    ULONG InternalAddRef() noexcept
    {
        return CComGlobalsThreadModel::Increment(&m_dwRef);
    }

    ULONG InternalRelease() noexcept
    {
        return CComGlobalsThreadModel::Decrement(&m_dwRef);
    }

    static HRESULT WINAPI _Break(void*, REFIID, void**, DWORD_PTR) noexcept
    {
        ATLASSERT(FALSE);
        return E_NOINTERFACE;
    }

    static HRESULT WINAPI _NoInterface(void*, REFIID, void**, DWORD_PTR) noexcept
    {
        return E_NOINTERFACE;
    }

    static HRESULT WINAPI _Creator(void*, REFIID, void**, DWORD_PTR) noexcept
    {
        return E_NOINTERFACE;
    }

    static HRESULT WINAPI _Delegate(void*, REFIID, void**, DWORD_PTR) noexcept
    {
        return E_NOINTERFACE;
    }

    static HRESULT WINAPI _Chain(void*, REFIID, void**, DWORD_PTR) noexcept
    {
        return E_NOINTERFACE;
    }

    static HRESULT WINAPI _Cache(void* pv, REFIID iid, void** ppvObject, DWORD_PTR dw) noexcept
    {
        (void)pv; (void)iid; (void)ppvObject; (void)dw;
        return E_NOINTERFACE;
    }

    union {
        LONG m_lRef; // for compatibility
    };
};

///////////////////////////////////////////////////////////////////////////////
// CComObjectRootEx - thread-safe ref counting with Lock/Unlock

template <typename ThreadModel>
class CComObjectRootEx : public CComObjectRootBase {
public:
    typedef ThreadModel _ThreadModel;
    typedef typename ThreadModel::AutoCriticalSection _CritSec;
    typedef typename ThreadModel::AutoDeleteCriticalSection _AutoDelCritSec;
    typedef CComCritSecLock<_CritSec> ObjectLock;

    ~CComObjectRootEx() {}

    ULONG InternalAddRef() noexcept
    {
        ATLASSERT(m_dwRef != -1L);
        return ThreadModel::Increment(&m_dwRef);
    }

    ULONG InternalRelease() noexcept
    {
        return ThreadModel::Decrement(&m_dwRef);
    }

    HRESULT _AtlInitialConstruct() noexcept
    {
        return m_critsec.Init();
    }

    void Lock() noexcept { m_critsec.Lock(); }
    void Unlock() noexcept { m_critsec.Unlock(); }

private:
    _AutoDelCritSec m_critsec;
};

///////////////////////////////////////////////////////////////////////////////
// CComObject - standalone COM object implementation

template <typename Base>
class CComObject : public Base {
public:
    CComObject(void* = NULL) noexcept
    {
        this->m_dwRef = 0;
    }

    virtual ~CComObject()
    {
        this->m_dwRef = -(LONG_MAX / 2);
        this->FinalRelease();
    }

    STDMETHOD_(ULONG, AddRef)() noexcept
    {
        return this->InternalAddRef();
    }

    STDMETHOD_(ULONG, Release)() noexcept
    {
        ULONG l = this->InternalRelease();
        if (l == 0)
            delete this;
        return l;
    }

    STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) noexcept
    {
        return this->_InternalQueryInterface(iid, ppvObject);
    }

    static HRESULT WINAPI CreateInstance(CComObject<Base>** pp) noexcept
    {
        ATLASSERT(pp != NULL);
        if (pp == NULL)
            return E_POINTER;
        *pp = NULL;

        CComObject<Base>* p = NULL;
        ATLTRY(p = new CComObject<Base>());
        if (p == NULL)
            return E_OUTOFMEMORY;

        p->InternalAddRef();
        HRESULT hr = p->_AtlInitialConstruct();
        if (SUCCEEDED(hr))
            hr = p->FinalConstruct();
        p->InternalRelease();

        if (FAILED(hr)) {
            delete p;
            p = NULL;
        }

        *pp = p;
        return hr;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CComObjectCached - cached COM object

template <typename Base>
class CComObjectCached : public Base {
public:
    CComObjectCached(void* = NULL) noexcept {}

    STDMETHOD_(ULONG, AddRef)() noexcept
    {
        ULONG l = this->InternalAddRef();
        if (l == 2)
            _pAtlModule->Lock();
        return l;
    }

    STDMETHOD_(ULONG, Release)() noexcept
    {
        ULONG l = this->InternalRelease();
        if (l == 0)
            delete this;
        else if (l == 1)
            _pAtlModule->Unlock();
        return l;
    }

    STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) noexcept
    {
        return this->_InternalQueryInterface(iid, ppvObject);
    }
};

///////////////////////////////////////////////////////////////////////////////
// CComCreator / CComCreator2

template <typename T>
class CComCreator {
public:
    static HRESULT WINAPI CreateInstance(void* pv, REFIID riid, LPVOID* ppv) noexcept
    {
        ATLASSERT(ppv != NULL);
        if (ppv == NULL)
            return E_POINTER;
        *ppv = NULL;

        HRESULT hr = E_OUTOFMEMORY;
        T* p = NULL;
        ATLTRY(p = new T(pv));
        if (p != NULL) {
            p->InternalAddRef();
            hr = p->_AtlInitialConstruct();
            if (SUCCEEDED(hr))
                hr = p->FinalConstruct();
            p->InternalRelease();

            if (SUCCEEDED(hr))
                hr = p->QueryInterface(riid, ppv);
            if (FAILED(hr))
                delete p;
        }
        return hr;
    }
};

template <typename T1, typename T2>
class CComCreator2 {
public:
    static HRESULT WINAPI CreateInstance(void* pv, REFIID riid, LPVOID* ppv) noexcept
    {
        if (pv == NULL)
            return T1::CreateInstance(NULL, riid, ppv);
        return T2::CreateInstance(pv, riid, ppv);
    }
};

///////////////////////////////////////////////////////////////////////////////
// CComClassFactory

class CComClassFactory : public IClassFactory {
public:
    typedef HRESULT (WINAPI *CREATORFUNC)(void* pv, REFIID riid, LPVOID* ppv);
    CREATORFUNC m_pfnCreateInstance;

    CComClassFactory() noexcept : m_pfnCreateInstance(NULL) {}

    void SetVoid(void* pv) noexcept
    {
        m_pfnCreateInstance = (CREATORFUNC)pv;
    }

    STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, void** ppvObj)
    {
        ATLASSERT(m_pfnCreateInstance != NULL);
        if (ppvObj == NULL)
            return E_POINTER;
        *ppvObj = NULL;
        if (pUnkOuter != NULL && !InlineIsEqualUnknown(riid))
            return CLASS_E_NOAGGREGATION;
        return m_pfnCreateInstance(pUnkOuter, riid, ppvObj);
    }

    STDMETHOD(LockServer)(BOOL fLock)
    {
        if (fLock)
            _pAtlModule->Lock();
        else
            _pAtlModule->Unlock();
        return S_OK;
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv)
    {
        if (ppv == NULL)
            return E_POINTER;
        *ppv = NULL;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)() { return 2; }
    STDMETHOD_(ULONG, Release)() { return 1; }
};

///////////////////////////////////////////////////////////////////////////////
// IObjectWithSiteImpl

template <typename T>
class ATL_NO_VTABLE IObjectWithSiteImpl : public IObjectWithSite {
public:
    CComPtr<IUnknown> m_spUnkSite;

    STDMETHOD(SetSite)(IUnknown* pUnkSite)
    {
        m_spUnkSite = pUnkSite;
        return S_OK;
    }

    STDMETHOD(GetSite)(REFIID riid, void** ppvSite)
    {
        if (ppvSite == NULL)
            return E_POINTER;
        *ppvSite = NULL;

        if (m_spUnkSite)
            return m_spUnkSite->QueryInterface(riid, ppvSite);
        return E_FAIL;
    }
};

} // namespace ATL

///////////////////////////////////////////////////////////////////////////////
// COM Map Macros

#define BEGIN_COM_MAP(x) \
public: \
    typedef x _ComMapClass; \
    HRESULT _InternalQueryInterface(REFIID iid, void** ppvObject) noexcept \
    { \
        return ATL::AtlInternalQueryInterface(this, _GetEntries(), iid, ppvObject); \
    } \
    static const ATL::_ATL_INTMAP_ENTRY* _GetEntries() noexcept \
    { \
        static const ATL::_ATL_INTMAP_ENTRY _entries[] = {

#define COM_INTERFACE_ENTRY(x) \
            { &__uuidof(x), (DWORD_PTR)(static_cast<x*>((_ComMapClass*)_ATL_PACKING)) - _ATL_PACKING, \
              (ATL::_ATL_CREATORDATA*)(DWORD_PTR)ATL::_ATL_SIMPLEMAPENTRY ? \
              (HRESULT (WINAPI*)(void*, REFIID, LPVOID*, DWORD_PTR))1 : NULL },

#define COM_INTERFACE_ENTRY_IID(iid, x) \
            { &iid, (DWORD_PTR)(static_cast<x*>((_ComMapClass*)_ATL_PACKING)) - _ATL_PACKING, \
              (HRESULT (WINAPI*)(void*, REFIID, LPVOID*, DWORD_PTR))1 },

#define COM_INTERFACE_ENTRY2(x, x2) \
            { &__uuidof(x), (DWORD_PTR)(static_cast<x*>(static_cast<x2*>((_ComMapClass*)_ATL_PACKING))) - _ATL_PACKING, \
              (HRESULT (WINAPI*)(void*, REFIID, LPVOID*, DWORD_PTR))1 },

#define COM_INTERFACE_ENTRY2_IID(iid, x, x2) \
            { &iid, (DWORD_PTR)(static_cast<x*>(static_cast<x2*>((_ComMapClass*)_ATL_PACKING))) - _ATL_PACKING, \
              (HRESULT (WINAPI*)(void*, REFIID, LPVOID*, DWORD_PTR))1 },

#define END_COM_MAP() \
            { NULL, 0, NULL } \
        }; \
        return _entries; \
    } \
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0; \
    virtual ULONG STDMETHODCALLTYPE Release() = 0; \
    STDMETHOD(QueryInterface)(REFIID, void**) = 0;

///////////////////////////////////////////////////////////////////////////////
// Aggregation Macros

#define DECLARE_NOT_AGGREGATABLE(x) \
public: \
    typedef ATL::CComCreator< ATL::CComObject<x> > _CreatorClass;

#define DECLARE_AGGREGATABLE(x) \
public: \
    typedef ATL::CComCreator2< ATL::CComCreator< ATL::CComObject<x> >, \
        ATL::CComCreator< ATL::CComObject<x> > > _CreatorClass;

#define DECLARE_ONLY_AGGREGATABLE(x) \
public: \
    typedef ATL::CComCreator< ATL::CComObject<x> > _CreatorClass;

#define DECLARE_PROTECT_FINAL_CONSTRUCT() \
    void InternalFinalConstructAddRef() { this->InternalAddRef(); } \
    void InternalFinalConstructRelease() { this->InternalRelease(); }

#define DECLARE_CLASSFACTORY() \
    typedef ATL::CComClassFactory _ClassFactoryCreatorClass;

#define DECLARE_CLASSFACTORY_EX(cf) \
    typedef cf _ClassFactoryCreatorClass;

///////////////////////////////////////////////////////////////////////////////
// OBJECT_ENTRY_AUTO - auto-registration macro (simplified stub)

#define OBJECT_ENTRY_AUTO(clsid, x) \
    __declspec(selectany) ATL::_ATL_OBJMAP_ENTRY __objMap_##x = \
    { &clsid, NULL, x::_CreatorClass::CreateInstance, x::_CreatorClass::CreateInstance, NULL, 0, NULL };

#endif // __ATLCOM_H__
