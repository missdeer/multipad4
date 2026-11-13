// OpenATL - Clean-room ATL subset for WTL 10.0
// Memory allocators and smart pointers

#ifndef __ATLALLOC_H__
#define __ATLALLOC_H__

#pragma once

#include "atldef.h"

#include <cstdlib>
#include <cstring>
#include <malloc.h>

namespace ATL {

///////////////////////////////////////////////////////////////////////////////
// Allocator classes

class CCRTAllocator {
public:
    static void* Allocate(size_t nBytes) noexcept
    {
        return malloc(nBytes);
    }

    static void* Reallocate(void* p, size_t nBytes) noexcept
    {
        return realloc(p, nBytes);
    }

    static void Free(void* p) noexcept
    {
        free(p);
    }
};

class CLocalAllocator {
public:
    static void* Allocate(size_t nBytes) noexcept
    {
        return ::LocalAlloc(LMEM_FIXED, nBytes);
    }

    static void* Reallocate(void* p, size_t nBytes) noexcept
    {
        return ::LocalReAlloc(p, nBytes, 0);
    }

    static void Free(void* p) noexcept
    {
        ::LocalFree(p);
    }
};

class CGlobalAllocator {
public:
    static void* Allocate(size_t nBytes) noexcept
    {
        return ::GlobalAlloc(GMEM_FIXED, nBytes);
    }

    static void* Reallocate(void* p, size_t nBytes) noexcept
    {
        return ::GlobalReAlloc(p, nBytes, 0);
    }

    static void Free(void* p) noexcept
    {
        ::GlobalFree(p);
    }
};

///////////////////////////////////////////////////////////////////////////////
// CHeapPtr

template <typename T, typename Allocator = CCRTAllocator>
class CHeapPtr {
public:
    T* m_pData;

    CHeapPtr() noexcept : m_pData(NULL) {}

    explicit CHeapPtr(T* p) noexcept : m_pData(p) {}

    CHeapPtr(CHeapPtr<T, Allocator>&& src) noexcept : m_pData(src.m_pData)
    {
        src.m_pData = NULL;
    }

    ~CHeapPtr()
    {
        Free();
    }

    CHeapPtr<T, Allocator>& operator=(CHeapPtr<T, Allocator>&& src) noexcept
    {
        if (this != &src) {
            Free();
            m_pData = src.m_pData;
            src.m_pData = NULL;
        }
        return *this;
    }

    operator T*() const noexcept
    {
        return m_pData;
    }

    T* operator->() const noexcept
    {
        ATLASSERT(m_pData != NULL);
        return m_pData;
    }

    T** operator&() noexcept
    {
        return &m_pData;
    }

    bool Allocate(size_t nElements = 1) noexcept
    {
        ATLASSERT(m_pData == NULL);
        m_pData = (T*)Allocator::Allocate(nElements * sizeof(T));
        return m_pData != NULL;
    }

    bool AllocateBytes(size_t nBytes) noexcept
    {
        ATLASSERT(m_pData == NULL);
        m_pData = (T*)Allocator::Allocate(nBytes);
        return m_pData != NULL;
    }

    bool Reallocate(size_t nElements) noexcept
    {
        T* pNew = (T*)Allocator::Reallocate(m_pData, nElements * sizeof(T));
        if (pNew == NULL)
            return false;
        m_pData = pNew;
        return true;
    }

    bool ReallocateBytes(size_t nBytes) noexcept
    {
        T* pNew = (T*)Allocator::Reallocate(m_pData, nBytes);
        if (pNew == NULL)
            return false;
        m_pData = pNew;
        return true;
    }

    void Free() noexcept
    {
        if (m_pData != NULL) {
            Allocator::Free(m_pData);
            m_pData = NULL;
        }
    }

    T* Detach() noexcept
    {
        T* p = m_pData;
        m_pData = NULL;
        return p;
    }

    void Attach(T* p) noexcept
    {
        Free();
        m_pData = p;
    }

private:
    CHeapPtr(const CHeapPtr&) = delete;
    CHeapPtr& operator=(const CHeapPtr&) = delete;
};

///////////////////////////////////////////////////////////////////////////////
// CTempBuffer - stack/heap temporary buffer
// Uses alloca for small allocations, heap for larger ones

template <typename T, int t_nFixedBytes = 128>
class CTempBuffer {
public:
    CTempBuffer() noexcept : m_p(NULL), m_nSize(0), m_bOnHeap(false)
    {
    }

    explicit CTempBuffer(size_t nElements)
        : m_p(NULL), m_nSize(0), m_bOnHeap(false)
    {
        Allocate(nElements);
    }

    ~CTempBuffer()
    {
        FreeHeap();
    }

    operator T*() const noexcept
    {
        return m_p;
    }

    T* operator->() const noexcept
    {
        ATLASSERT(m_p != NULL);
        return m_p;
    }

    T* Allocate(size_t nElements)
    {
        return AllocateBytes(nElements * sizeof(T));
    }

    T* AllocateBytes(size_t nBytes)
    {
        FreeHeap();
        if (nBytes <= t_nFixedBytes) {
            m_p = reinterpret_cast<T*>(m_abFixedBuffer);
            m_bOnHeap = false;
        } else {
            m_p = static_cast<T*>(malloc(nBytes));
            m_bOnHeap = true;
        }
        m_nSize = nBytes;
        return m_p;
    }

private:
    void FreeHeap() noexcept
    {
        if (m_bOnHeap && m_p != NULL) {
            free(m_p);
        }
        m_p = NULL;
        m_nSize = 0;
        m_bOnHeap = false;
    }

    T* m_p;
    size_t m_nSize;
    bool m_bOnHeap;
    BYTE m_abFixedBuffer[t_nFixedBytes];

    CTempBuffer(const CTempBuffer&) = delete;
    CTempBuffer& operator=(const CTempBuffer&) = delete;
};

} // namespace ATL

#endif // __ATLALLOC_H__
