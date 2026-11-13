#pragma once
#define __ATLCOLL_H__

// The tabbing framework uses only the indexed CAtlArray interface.
// Keep its objects alive through insertion/removal using standard C++ ownership.
#include <atlbase.h>
#include <vector>
#include <algorithm>

namespace ATL {
template<class E, class ETraits = void>
class CAtlArray {
public:
    size_t GetCount() const noexcept { return elements.size(); }
    bool IsEmpty() const noexcept { return elements.empty(); }
    E& operator[](size_t index) { return elements.at(index); }
    const E& operator[](size_t index) const { return elements.at(index); }
    E& GetAt(size_t index) { return elements.at(index); }
    const E& GetAt(size_t index) const { return elements.at(index); }
    void SetAt(size_t index, const E& element) { elements.at(index) = element; }
    size_t Add(const E& element) {
        elements.push_back(element);
        return elements.size() - 1;
    }
    bool SetCount(size_t count, int = -1) {
        elements.resize(count);
        return true;
    }
    void InsertAt(size_t index, const E& element, size_t count = 1) {
        if (count == 0) return;
        if (index > elements.max_size() || count > elements.max_size() - index)
            AtlThrow(E_OUTOFMEMORY);
        E saved(element);
        if (index >= elements.size()) {
            elements.resize(index + count);
            std::fill(elements.begin() + index, elements.end(), saved);
        } else {
            elements.insert(elements.begin() + index, count, saved);
        }
    }
    void RemoveAt(size_t index, size_t count = 1) {
        if (index > elements.size() || count > elements.size() - index)
            AtlThrow(E_INVALIDARG);
        elements.erase(elements.begin() + index, elements.begin() + index + count);
    }
    void RemoveAll() noexcept { elements.clear(); }
    E* GetData() noexcept { return elements.data(); }
    const E* GetData() const noexcept { return elements.data(); }

private:
    std::vector<E> elements;
};
}
