// OpenATL - Clean-room ATL subset for WTL 10.0
// Simple collection classes

#ifndef __ATLSIMPCOLL_H__
#define __ATLSIMPCOLL_H__

#pragma once

#include "atldef.h"

#include <new>
#include <cstring>

namespace ATL {

///////////////////////////////////////////////////////////////////////////////
// CSimpleArrayEqualHelper

template <typename T>
class CSimpleArrayEqualHelper {
public:
    static bool IsEqual(const T& a, const T& b)
    {
        return a == b;
    }
};

template <typename T>
class CSimpleArrayEqualHelperFalse {
public:
    static bool IsEqual(const T&, const T&)
    {
        ATLASSERT(FALSE);
        return false;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CSimpleArray

template <typename T, typename TEqual = CSimpleArrayEqualHelper<T>>
class CSimpleArray {
public:
    T* m_aT;
    int m_nSize;
    int m_nAllocSize;

    CSimpleArray() noexcept : m_aT(NULL), m_nSize(0), m_nAllocSize(0)
    {
    }

    CSimpleArray(const CSimpleArray<T, TEqual>& src) : m_aT(NULL), m_nSize(0), m_nAllocSize(0)
    {
        if (src.GetSize()) {
            m_aT = (T*)calloc(src.GetSize(), sizeof(T));
            if (m_aT != NULL) {
                m_nAllocSize = src.GetSize();
                for (int i = 0; i < src.GetSize(); i++)
                    Add(src[i]);
            }
        }
    }

    ~CSimpleArray()
    {
        RemoveAll();
    }

    CSimpleArray<T, TEqual>& operator=(const CSimpleArray<T, TEqual>& src)
    {
        if (this != &src) {
            RemoveAll();
            if (src.GetSize()) {
                m_aT = (T*)calloc(src.GetSize(), sizeof(T));
                if (m_aT != NULL) {
                    m_nAllocSize = src.GetSize();
                    for (int i = 0; i < src.GetSize(); i++)
                        Add(src[i]);
                }
            }
        }
        return *this;
    }

    int GetSize() const noexcept
    {
        return m_nSize;
    }

    BOOL Add(const T& t)
    {
        if (m_nSize == m_nAllocSize) {
            int nNewAllocSize = (m_nAllocSize == 0) ? 1 : (m_nSize * 2);
            T* aT = (T*)realloc(m_aT, nNewAllocSize * sizeof(T));
            if (aT == NULL)
                return FALSE;
            m_nAllocSize = nNewAllocSize;
            m_aT = aT;
        }
        // Use placement new for proper construction
        ::new (&m_aT[m_nSize]) T(t);
        m_nSize++;
        return TRUE;
    }

    BOOL Remove(const T& t)
    {
        int nIndex = Find(t);
        if (nIndex < 0)
            return FALSE;
        return RemoveAt(nIndex);
    }

    BOOL RemoveAt(int nIndex)
    {
        ATLASSERT(nIndex >= 0 && nIndex < m_nSize);
        if (nIndex < 0 || nIndex >= m_nSize)
            return FALSE;

        m_aT[nIndex].~T();
        if (nIndex != (m_nSize - 1))
            memmove(&m_aT[nIndex], &m_aT[nIndex + 1], (m_nSize - nIndex - 1) * sizeof(T));
        m_nSize--;
        return TRUE;
    }

    void RemoveAll()
    {
        if (m_aT != NULL) {
            for (int i = 0; i < m_nSize; i++)
                m_aT[i].~T();
            free(m_aT);
            m_aT = NULL;
        }
        m_nSize = 0;
        m_nAllocSize = 0;
    }

    const T& operator[](int nIndex) const
    {
        ATLASSERT(nIndex >= 0 && nIndex < m_nSize);
        return m_aT[nIndex];
    }

    T& operator[](int nIndex)
    {
        ATLASSERT(nIndex >= 0 && nIndex < m_nSize);
        return m_aT[nIndex];
    }

    T* GetData() const noexcept
    {
        return m_aT;
    }

    int Find(const T& t) const
    {
        for (int i = 0; i < m_nSize; i++) {
            if (TEqual::IsEqual(m_aT[i], t))
                return i;
        }
        return -1;
    }

    BOOL SetAtIndex(int nIndex, const T& t)
    {
        ATLASSERT(nIndex >= 0 && nIndex < m_nSize);
        if (nIndex < 0 || nIndex >= m_nSize)
            return FALSE;
        m_aT[nIndex] = t;
        return TRUE;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CSimpleMap

template <typename TKey, typename TVal, typename TEqual = CSimpleArrayEqualHelper<TKey>>
class CSimpleMap {
public:
    TKey* m_aKey;
    TVal* m_aVal;
    int m_nSize;

    CSimpleMap() noexcept : m_aKey(NULL), m_aVal(NULL), m_nSize(0)
    {
    }

    ~CSimpleMap()
    {
        RemoveAll();
    }

    int GetSize() const noexcept
    {
        return m_nSize;
    }

    BOOL Add(const TKey& key, const TVal& val)
    {
        TKey* pKeyNew = (TKey*)realloc(m_aKey, (m_nSize + 1) * sizeof(TKey));
        if (pKeyNew == NULL)
            return FALSE;
        m_aKey = pKeyNew;

        TVal* pValNew = (TVal*)realloc(m_aVal, (m_nSize + 1) * sizeof(TVal));
        if (pValNew == NULL)
            return FALSE;
        m_aVal = pValNew;

        ::new (&m_aKey[m_nSize]) TKey(key);
        ::new (&m_aVal[m_nSize]) TVal(val);
        m_nSize++;
        return TRUE;
    }

    BOOL Remove(const TKey& key)
    {
        int nIndex = FindKey(key);
        if (nIndex < 0)
            return FALSE;
        return RemoveAt(nIndex);
    }

    BOOL RemoveAt(int nIndex)
    {
        ATLASSERT(nIndex >= 0 && nIndex < m_nSize);
        if (nIndex < 0 || nIndex >= m_nSize)
            return FALSE;

        m_aKey[nIndex].~TKey();
        m_aVal[nIndex].~TVal();

        if (nIndex != (m_nSize - 1)) {
            memmove(&m_aKey[nIndex], &m_aKey[nIndex + 1], (m_nSize - nIndex - 1) * sizeof(TKey));
            memmove(&m_aVal[nIndex], &m_aVal[nIndex + 1], (m_nSize - nIndex - 1) * sizeof(TVal));
        }
        m_nSize--;
        return TRUE;
    }

    void RemoveAll()
    {
        if (m_aKey != NULL) {
            for (int i = 0; i < m_nSize; i++) {
                m_aKey[i].~TKey();
                m_aVal[i].~TVal();
            }
            free(m_aKey);
            m_aKey = NULL;
        }
        if (m_aVal != NULL) {
            free(m_aVal);
            m_aVal = NULL;
        }
        m_nSize = 0;
    }

    BOOL SetAt(const TKey& key, const TVal& val)
    {
        int nIndex = FindKey(key);
        if (nIndex < 0)
            return FALSE;
        m_aVal[nIndex] = val;
        return TRUE;
    }

    TVal Lookup(const TKey& key) const
    {
        int nIndex = FindKey(key);
        if (nIndex >= 0)
            return GetValueAt(nIndex);
        return TVal();
    }

    TKey ReverseLookup(const TVal& val) const
    {
        int nIndex = FindVal(val);
        if (nIndex >= 0)
            return GetKeyAt(nIndex);
        return TKey();
    }

    TKey& GetKeyAt(int nIndex) const
    {
        ATLASSERT(nIndex >= 0 && nIndex < m_nSize);
        return m_aKey[nIndex];
    }

    TVal& GetValueAt(int nIndex) const
    {
        ATLASSERT(nIndex >= 0 && nIndex < m_nSize);
        return m_aVal[nIndex];
    }

    int FindKey(const TKey& key) const
    {
        for (int i = 0; i < m_nSize; i++) {
            if (TEqual::IsEqual(m_aKey[i], key))
                return i;
        }
        return -1;
    }

    int FindVal(const TVal& val) const
    {
        for (int i = 0; i < m_nSize; i++) {
            if (CSimpleArrayEqualHelper<TVal>::IsEqual(m_aVal[i], val))
                return i;
        }
        return -1;
    }

    BOOL SetAtIndex(int nIndex, const TKey& key, const TVal& val)
    {
        ATLASSERT(nIndex >= 0 && nIndex < m_nSize);
        if (nIndex < 0 || nIndex >= m_nSize)
            return FALSE;
        m_aKey[nIndex] = key;
        m_aVal[nIndex] = val;
        return TRUE;
    }
};

///////////////////////////////////////////////////////////////////////////////
// CSimpleValArray - array of simple value types

template <typename T>
class CSimpleValArray : public CSimpleArray<T, CSimpleArrayEqualHelperFalse<T>> {
public:
    BOOL Add(T t)
    {
        return CSimpleArray<T, CSimpleArrayEqualHelperFalse<T>>::Add(t);
    }

    BOOL Remove(T t)
    {
        return CSimpleArray<T, CSimpleArrayEqualHelperFalse<T>>::Remove(t);
    }

    T operator[](int nIndex) const
    {
        return CSimpleArray<T, CSimpleArrayEqualHelperFalse<T>>::operator[](nIndex);
    }
};

} // namespace ATL

#endif // __ATLSIMPCOLL_H__
