// OpenATL - Clean-room ATL subset for WTL 10.0
// Compatible with llvm-mingw cross-compiler
// C++17, minimum Windows 7

#ifndef __ATLDEF_H__
#define __ATLDEF_H__

#pragma once

// Ensure Windows 7 minimum
#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0800
#endif

// _MSC_VER compatibility shim for MinGW
// WTL checks _MSC_VER >= 1400; define as VS2015 (1900) if not set
#ifndef _MSC_VER
#define _MSC_VER 1900
#define _OPENATL_DEFINED_MSC_VER
// Prevent clang's __stddef_wchar_t.h from redefining wchar_t.
// The guard checks: defined(_MSC_VER) && !_NATIVE_WCHAR_T_DEFINED,
// and also !defined(_WCHAR_T). We must define both.
#ifndef _NATIVE_WCHAR_T_DEFINED
#define _NATIVE_WCHAR_T_DEFINED 1
#endif
#ifndef _WCHAR_T
#define _WCHAR_T
#endif
#ifndef _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
#endif
#endif

// ATL version - 14.0, satisfies WTL's >= 0x0800 check
#define _ATL_VER 0x0E00

// Packing
#ifndef _ATL_PACKING
#define _ATL_PACKING 8
#endif

// __declspec(novtable) is not supported by MinGW/clang
#ifndef ATL_NO_VTABLE
#define ATL_NO_VTABLE
#endif

// Deprecation
#ifndef ATL_DEPRECATED
#ifdef __GNUC__
#define ATL_DEPRECATED __attribute__((deprecated))
#else
#define ATL_DEPRECATED
#endif
#endif

// Nothrow
#ifndef ATL_NOTHROW
#define ATL_NOTHROW
#endif

// Forceinline
#ifndef ATLAPI
#define ATLAPI HRESULT __stdcall
#endif

#ifndef ATLAPI_
#define ATLAPI_(x) x __stdcall
#endif

#ifndef ATL_NOINLINE
#ifdef __GNUC__
#define ATL_NOINLINE __attribute__((noinline))
#else
#define ATL_NOINLINE
#endif
#endif

#ifndef ATLASSUME
#define ATLASSUME(expr) do { (void)(expr); } while(0)
#endif

// Threading model defaults
#if !defined(_ATL_SINGLE_THREADED) && !defined(_ATL_APARTMENT_THREADED) && !defined(_ATL_FREE_THREADED)
#define _ATL_FREE_THREADED
#endif

// Debug assertions
#include <crtdbg.h>

#ifndef ATLASSERT
#define ATLASSERT(expr) _ASSERTE(expr)
#endif

#ifndef ATLVERIFY
#ifdef _DEBUG
#define ATLVERIFY(expr) ATLASSERT(expr)
#else
#define ATLVERIFY(expr) (void)(expr)
#endif
#endif

// AtlThrow / ATLENSURE / ATLTRY
#ifndef _ATL_NO_EXCEPTIONS

#include <exception>

namespace ATL {

class CAtlException {
public:
    HRESULT m_hr;
    CAtlException(HRESULT hr = E_FAIL) noexcept : m_hr(hr) {}
    operator HRESULT() const noexcept { return m_hr; }
};

#ifndef AtlThrow
[[noreturn]] inline void AtlThrow(HRESULT hr)
{
    throw CAtlException(hr);
}
#endif

} // namespace ATL

#ifndef ATLTRY
#define ATLTRY(x) try { x; } catch(...) {}
#endif

#ifndef ATLENSURE_THROW
#define ATLENSURE_THROW(expr, hr) \
    do { if(!(expr)) ATL::AtlThrow(hr); } while(0)
#endif

#ifndef ATLENSURE
#define ATLENSURE(expr) ATLENSURE_THROW(expr, E_FAIL)
#endif

#ifndef ATLENSURE_RETURN
#define ATLENSURE_RETURN(expr) \
    do { if(!(expr)) { ATLASSERT(FALSE); return E_FAIL; } } while(0)
#endif

#ifndef ATLENSURE_RETURN_VAL
#define ATLENSURE_RETURN_VAL(expr, val) \
    do { if(!(expr)) { ATLASSERT(FALSE); return val; } } while(0)
#endif

#else // _ATL_NO_EXCEPTIONS

namespace ATL {

#ifndef AtlThrow
inline void AtlThrow(HRESULT hr)
{
    ATLASSERT(FALSE);
    (void)hr;
}
#endif

} // namespace ATL

#ifndef ATLTRY
#define ATLTRY(x) x
#endif

#ifndef ATLENSURE_THROW
#define ATLENSURE_THROW(expr, hr) \
    do { if(!(expr)) { ATLASSERT(FALSE); } } while(0)
#endif

#ifndef ATLENSURE
#define ATLENSURE(expr) ATLENSURE_THROW(expr, E_FAIL)
#endif

#ifndef ATLENSURE_RETURN
#define ATLENSURE_RETURN(expr) \
    do { if(!(expr)) { ATLASSERT(FALSE); return E_FAIL; } } while(0)
#endif

#ifndef ATLENSURE_RETURN_VAL
#define ATLENSURE_RETURN_VAL(expr, val) \
    do { if(!(expr)) { ATLASSERT(FALSE); return val; } } while(0)
#endif

#endif // _ATL_NO_EXCEPTIONS

// offsetofclass - computes byte offset of base class within derived
#ifndef offsetofclass
#define offsetofclass(base, derived) \
    ((DWORD_PTR)(static_cast<base*>((derived*)_ATL_PACKING)) - _ATL_PACKING)
#endif

// FAILED_UNEXPECTEDLY
#ifndef FAILED_UNEXPECTEDLY
#define FAILED_UNEXPECTEDLY(hr) FAILED(hr)
#endif

// ATLUNUSED
#ifndef ATLUNUSED
#define ATLUNUSED(x) (void)(x)
#endif

// MSVC __if_exists / __if_not_exists compatibility
// __if_exists(x) { ... } - always include the body (becomes unconditional block)
// __if_not_exists(x) { ... } - never include the body
#ifdef _OPENATL_DEFINED_MSC_VER
#ifndef __if_exists
#define __if_exists(x)
#endif
#ifndef __if_not_exists
#define __if_not_exists(x) if constexpr (false)
#endif
#endif

#endif // __ATLDEF_H__
