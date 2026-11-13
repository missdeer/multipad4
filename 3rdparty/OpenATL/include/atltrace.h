// OpenATL - Clean-room ATL subset for WTL 10.0
// Debug tracing support

#ifndef __ATLTRACE_H__
#define __ATLTRACE_H__

#pragma once

#include <tchar.h>

namespace ATL {

// Trace levels
enum atlTraceFlags {
    atlTraceGeneral    = 0x0001,
    atlTraceCOM        = 0x0002,
    atlTraceQI         = 0x0004,
    atlTraceRegistrar  = 0x0008,
    atlTraceRefcount   = 0x0010,
    atlTraceWindowing  = 0x0020,
    atlTraceControls   = 0x0040,
    atlTraceHosting    = 0x0080,
    atlTraceDBClient   = 0x0100,
    atlTraceDBProvider = 0x0200,
    atlTraceSnapin     = 0x0400,
    atlTraceNotImpl    = 0x0800,
    atlTraceAllocation = 0x1000,
    atlTraceException  = 0x2000,
    atlTraceTime       = 0x4000,
    atlTraceCache      = 0x8000,
    atlTraceStencil    = 0x10000,
    atlTraceString     = 0x20000,
    atlTraceMap        = 0x40000,
    atlTraceUtil       = 0x80000,
    atlTraceSecurity   = 0x100000,
    atlTraceSync       = 0x200000,
    atlTraceISAPI      = 0x400000,
    atlTraceUser       = 0x80000000,
};

template <unsigned int traceCategory = 0, unsigned int traceLevel = 0>
class CTraceCategoryEx {
public:
    enum { m_category = traceCategory };
    enum { m_level = traceLevel };

    CTraceCategoryEx(LPCTSTR /*lpszCategoryName*/ = NULL) noexcept {}

    operator unsigned int() const noexcept { return m_category; }
};

typedef CTraceCategoryEx<> CTraceCategory;

} // namespace ATL

// DECLARE_TRACE_CATEGORY - declares an extern trace category
#ifdef _DEBUG
#define DECLARE_TRACE_CATEGORY(name) extern ATL::CTraceCategory name;
#else
#define DECLARE_TRACE_CATEGORY(name)
#endif

// Standard trace categories
#ifdef _DEBUG
namespace ATL {
__declspec(selectany) CTraceCategory atlTraceGeneral(_T("atlTraceGeneral"));
__declspec(selectany) CTraceCategory atlTraceCOM(_T("atlTraceCOM"));
__declspec(selectany) CTraceCategory atlTraceQI(_T("atlTraceQI"));
__declspec(selectany) CTraceCategory atlTraceRegistrar(_T("atlTraceRegistrar"));
__declspec(selectany) CTraceCategory atlTraceRefcount(_T("atlTraceRefcount"));
__declspec(selectany) CTraceCategory atlTraceWindowing(_T("atlTraceWindowing"));
__declspec(selectany) CTraceCategory atlTraceControls(_T("atlTraceControls"));
__declspec(selectany) CTraceCategory atlTraceHosting(_T("atlTraceHosting"));
__declspec(selectany) CTraceCategory atlTraceDBClient(_T("atlTraceDBClient"));
__declspec(selectany) CTraceCategory atlTraceDBProvider(_T("atlTraceDBProvider"));
__declspec(selectany) CTraceCategory atlTraceSnapin(_T("atlTraceSnapin"));
__declspec(selectany) CTraceCategory atlTraceNotImpl(_T("atlTraceNotImpl"));
__declspec(selectany) CTraceCategory atlTraceAllocation(_T("atlTraceAllocation"));
__declspec(selectany) CTraceCategory atlTraceException(_T("atlTraceException"));
__declspec(selectany) CTraceCategory atlTraceTime(_T("atlTraceTime"));
__declspec(selectany) CTraceCategory atlTraceCache(_T("atlTraceCache"));
__declspec(selectany) CTraceCategory atlTraceStencil(_T("atlTraceStencil"));
__declspec(selectany) CTraceCategory atlTraceString(_T("atlTraceString"));
__declspec(selectany) CTraceCategory atlTraceMap(_T("atlTraceMap"));
__declspec(selectany) CTraceCategory atlTraceUtil(_T("atlTraceUtil"));
__declspec(selectany) CTraceCategory atlTraceSecurity(_T("atlTraceSecurity"));
__declspec(selectany) CTraceCategory atlTraceSync(_T("atlTraceSync"));
__declspec(selectany) CTraceCategory atlTraceISAPI(_T("atlTraceISAPI"));
__declspec(selectany) CTraceCategory atlTraceUser(_T("atlTraceUser"));
} // namespace ATL
#endif

// ATLTRACE / ATLTRACE2 macros
#ifdef _DEBUG

#include <cstdio>
#include <cstdarg>

namespace ATL {

inline void __cdecl AtlTraceV(LPCTSTR lpszFormat, va_list args) noexcept
{
    TCHAR szBuffer[1024];
    _vsntprintf_s(szBuffer, _countof(szBuffer), _TRUNCATE, lpszFormat, args);
    ::OutputDebugString(szBuffer);
}

inline void __cdecl AtlTrace(LPCTSTR lpszFormat, ...) noexcept
{
    va_list args;
    va_start(args, lpszFormat);
    AtlTraceV(lpszFormat, args);
    va_end(args);
}

inline void __cdecl AtlTrace2(DWORD /*category*/, UINT /*level*/, LPCTSTR lpszFormat, ...) noexcept
{
    va_list args;
    va_start(args, lpszFormat);
    AtlTraceV(lpszFormat, args);
    va_end(args);
}

} // namespace ATL

#define ATLTRACE ATL::AtlTrace
#define ATLTRACE2 ATL::AtlTrace2

#else // !_DEBUG

#ifndef ATLTRACE
#define ATLTRACE(...) ((void)0)
#endif

#ifndef ATLTRACE2
#define ATLTRACE2(...) ((void)0)
#endif

#endif // _DEBUG

// ATLTRACENOTIMPL
#ifdef _DEBUG
#define ATLTRACENOTIMPL(funcname) \
    ATLTRACE2(ATL::atlTraceNotImpl, 0, _T("NOT IMPLEMENTED: %s\n"), funcname); \
    return E_NOTIMPL
#else
#define ATLTRACENOTIMPL(funcname) return E_NOTIMPL
#endif

#endif // __ATLTRACE_H__
