#pragma once
#include <objbase.h>
#include <oleauto.h>

template<class Interface>
class ComObject {
public:
    ComObject() = default;
    ComObject(const ComObject&) = delete;
    ComObject& operator=(const ComObject&) = delete;
    ~ComObject() { if (object) object->Release(); }
    Interface* operator->() const { return object; }
    explicit operator bool() const { return object != nullptr; }
    Interface** Receive() { return &object; }
private:
    Interface* object = nullptr;
};

struct ComValue : VARIANT {
    ComValue() { VariantInit(this); }
    ComValue(const ComValue&) = delete;
    ComValue& operator=(const ComValue&) = delete;
    ~ComValue() { VariantClear(this); }
};
