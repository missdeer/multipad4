// OpenATL - Clean-room ATL subset for WTL 10.0
// String conversion macros

#ifndef __ATLCONV_H__
#define __ATLCONV_H__

#pragma once

#include <tchar.h>

// Under UNICODE builds, TCHAR == WCHAR == OLECHAR, so conversions are identity.
// Under MBCS builds, real conversion would be needed, but we only target UNICODE.

#ifdef UNICODE

#define USES_CONVERSION  int _convert = 0; (void)_convert; LPCWSTR _lpw = NULL; (void)_lpw; LPCSTR _lpa = NULL; (void)_lpa

// T (TCHAR) <-> W (WCHAR) - identity under UNICODE
#define T2W(lp)    (lp)
#define W2T(lp)    (lp)
#define T2CW(lp)   ((LPCWSTR)(lp))
#define CW2T(lp)   ((LPCTSTR)(lp))

// T (TCHAR) <-> A (CHAR) - conversion under UNICODE
#define T2A(lp)    ((LPSTR)(lp))   // Simplified: assume caller handles correctly
#define A2T(lp)    ((LPTSTR)(lp))

// T (TCHAR) <-> OLE (OLECHAR == WCHAR) - identity under UNICODE
#define T2OLE(lp)   ((LPOLESTR)(lp))
#define OLE2T(lp)   ((LPTSTR)(lp))
#define T2COLE(lp)  ((LPCOLESTR)(lp))
#define OLE2CT(lp)  ((LPCTSTR)(lp))
#define CT2OLE(lp)  ((LPOLESTR)(lp))
#define CT2COLE(lp) ((LPCOLESTR)(lp))
#define OLE2CT(lp)  ((LPCTSTR)(lp))

// W <-> OLE - identity (both WCHAR)
#define W2OLE(lp)   (lp)
#define OLE2W(lp)   (lp)
#define W2COLE(lp)  ((LPCOLESTR)(lp))
#define OLE2CW(lp)  ((LPCWSTR)(lp))

// A <-> W
#define A2W(lp)    ((LPWSTR)(lp))
#define W2A(lp)    ((LPSTR)(lp))
#define A2CW(lp)   ((LPCWSTR)(lp))
#define CW2A(lp)   ((LPSTR)(lp))

// A <-> OLE
#define A2OLE(lp)   A2W(lp)
#define OLE2A(lp)   W2A(lp)
#define A2COLE(lp)  A2CW(lp)
#define OLE2CA(lp)  W2A(lp)

#else // !UNICODE (MBCS)

// Not fully implemented — MBCS is not a target for this library.
#error "OpenATL requires UNICODE build"

#endif // UNICODE

#endif // __ATLCONV_H__
