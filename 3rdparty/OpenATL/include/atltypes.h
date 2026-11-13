// OpenATL - Clean-room ATL subset for WTL 10.0
// Type helpers (CPoint, CSize, CRect)

#ifndef __ATLTYPES_H__
#define __ATLTYPES_H__

#pragma once

#include <windows.h>

namespace ATL {
// These are minimal wrappers - WTL provides its own geometry types via atlmisc.h
// but some code may reference these ATL types.
} // namespace ATL

// CPoint
class CPoint : public POINT {
public:
    CPoint() noexcept { x = 0; y = 0; }
    CPoint(int initX, int initY) noexcept { x = initX; y = initY; }
    CPoint(POINT initPt) noexcept { x = initPt.x; y = initPt.y; }
    CPoint(SIZE initSize) noexcept { x = initSize.cx; y = initSize.cy; }
    CPoint(LPARAM dwPoint) noexcept { x = (short)LOWORD(dwPoint); y = (short)HIWORD(dwPoint); }

    void Offset(int xOffset, int yOffset) noexcept { x += xOffset; y += yOffset; }
    void Offset(POINT point) noexcept { x += point.x; y += point.y; }
    void Offset(SIZE size) noexcept { x += size.cx; y += size.cy; }
    void SetPoint(int X, int Y) noexcept { x = X; y = Y; }

    BOOL operator==(POINT point) const noexcept { return (x == point.x && y == point.y); }
    BOOL operator!=(POINT point) const noexcept { return (x != point.x || y != point.y); }

    CPoint operator+(SIZE size) const noexcept { return CPoint(x + size.cx, y + size.cy); }
    CPoint operator-(SIZE size) const noexcept { return CPoint(x - size.cx, y - size.cy); }
    CPoint operator-() const noexcept { return CPoint(-x, -y); }
    CPoint operator+(POINT point) const noexcept { return CPoint(x + point.x, y + point.y); }
};

// CSize
class CSize : public SIZE {
public:
    CSize() noexcept { cx = 0; cy = 0; }
    CSize(int initCX, int initCY) noexcept { cx = initCX; cy = initCY; }
    CSize(SIZE initSize) noexcept { cx = initSize.cx; cy = initSize.cy; }
    CSize(POINT initPt) noexcept { cx = initPt.x; cy = initPt.y; }

    BOOL operator==(SIZE size) const noexcept { return (cx == size.cx && cy == size.cy); }
    BOOL operator!=(SIZE size) const noexcept { return (cx != size.cx || cy != size.cy); }

    CSize operator+(SIZE size) const noexcept { return CSize(cx + size.cx, cy + size.cy); }
    CSize operator-(SIZE size) const noexcept { return CSize(cx - size.cx, cy - size.cy); }
    CSize operator-() const noexcept { return CSize(-cx, -cy); }
};

// CRect
class CRect : public RECT {
public:
    CRect() noexcept { left = 0; top = 0; right = 0; bottom = 0; }
    CRect(int l, int t, int r, int b) noexcept { left = l; top = t; right = r; bottom = b; }
    CRect(const RECT& srcRect) noexcept { ::CopyRect(this, &srcRect); }
    CRect(LPCRECT lpSrcRect) noexcept { ::CopyRect(this, lpSrcRect); }
    CRect(POINT point, SIZE size) noexcept { left = point.x; top = point.y; right = left + size.cx; bottom = top + size.cy; }
    CRect(POINT topLeft, POINT bottomRight) noexcept { left = topLeft.x; top = topLeft.y; right = bottomRight.x; bottom = bottomRight.y; }

    int Width() const noexcept { return right - left; }
    int Height() const noexcept { return bottom - top; }
    CSize Size() const noexcept { return CSize(right - left, bottom - top); }

    CPoint& TopLeft() noexcept { return *((CPoint*)this); }
    CPoint& BottomRight() noexcept { return *((CPoint*)this + 1); }
    const CPoint& TopLeft() const noexcept { return *((const CPoint*)this); }
    const CPoint& BottomRight() const noexcept { return *((const CPoint*)this + 1); }
    CPoint CenterPoint() const noexcept { return CPoint((left + right) / 2, (top + bottom) / 2); }

    BOOL IsRectEmpty() const noexcept { return ::IsRectEmpty(this); }
    BOOL IsRectNull() const noexcept { return (left == 0 && right == 0 && top == 0 && bottom == 0); }
    BOOL PtInRect(POINT point) const noexcept { return ::PtInRect(this, point); }

    void SetRect(int x1, int y1, int x2, int y2) noexcept { ::SetRect(this, x1, y1, x2, y2); }
    void SetRectEmpty() noexcept { ::SetRectEmpty(this); }
    void CopyRect(LPCRECT lpSrcRect) noexcept { ::CopyRect(this, lpSrcRect); }
    BOOL EqualRect(LPCRECT lpRect) const noexcept { return ::EqualRect(this, lpRect); }

    void InflateRect(int x, int y) noexcept { ::InflateRect(this, x, y); }
    void InflateRect(SIZE size) noexcept { ::InflateRect(this, size.cx, size.cy); }
    void InflateRect(LPCRECT lpRect) noexcept { left -= lpRect->left; top -= lpRect->top; right += lpRect->right; bottom += lpRect->bottom; }
    void InflateRect(int l, int t, int r, int b) noexcept { left -= l; top -= t; right += r; bottom += b; }

    void DeflateRect(int x, int y) noexcept { ::InflateRect(this, -x, -y); }
    void DeflateRect(SIZE size) noexcept { ::InflateRect(this, -size.cx, -size.cy); }
    void DeflateRect(LPCRECT lpRect) noexcept { left += lpRect->left; top += lpRect->top; right -= lpRect->right; bottom -= lpRect->bottom; }
    void DeflateRect(int l, int t, int r, int b) noexcept { left += l; top += t; right -= r; bottom -= b; }

    void OffsetRect(int x, int y) noexcept { ::OffsetRect(this, x, y); }
    void OffsetRect(SIZE size) noexcept { ::OffsetRect(this, size.cx, size.cy); }
    void OffsetRect(POINT point) noexcept { ::OffsetRect(this, point.x, point.y); }

    void NormalizeRect() noexcept
    {
        if (left > right) { int temp = left; left = right; right = temp; }
        if (top > bottom) { int temp = top; top = bottom; bottom = temp; }
    }

    void MoveToX(int x) noexcept { right = Width() + x; left = x; }
    void MoveToY(int y) noexcept { bottom = Height() + y; top = y; }
    void MoveToXY(int x, int y) noexcept { MoveToX(x); MoveToY(y); }
    void MoveToXY(POINT point) noexcept { MoveToX(point.x); MoveToY(point.y); }

    BOOL IntersectRect(LPCRECT lpRect1, LPCRECT lpRect2) noexcept { return ::IntersectRect(this, lpRect1, lpRect2); }
    BOOL UnionRect(LPCRECT lpRect1, LPCRECT lpRect2) noexcept { return ::UnionRect(this, lpRect1, lpRect2); }
    BOOL SubtractRect(LPCRECT lpRectSrc, LPCRECT lpRectOther) noexcept { return ::SubtractRect(this, lpRectSrc, lpRectOther); }

    BOOL operator==(const RECT& rect) const noexcept { return ::EqualRect(this, &rect); }
    BOOL operator!=(const RECT& rect) const noexcept { return !::EqualRect(this, &rect); }

    void operator=(const RECT& srcRect) noexcept { ::CopyRect(this, &srcRect); }
};

#endif // __ATLTYPES_H__
