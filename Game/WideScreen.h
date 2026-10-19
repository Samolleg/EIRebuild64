#pragma once
#include "GeneralSettings.h"
#include "widescreen_layout.h"

// Source scopes retain their lifetimes; all dimensions and edge shifts come from c74.
namespace WideScreen
{
inline owned::widescreen::Layout Layout(int w,int h) { return owned::widescreen::make_layout(float(w),float(h)); }
inline owned::widescreen::Layout Layout() { return Layout(theSettings.dwWidth,theSettings.dwHeight); }
inline int Effective(int w,int h) { return owned::widescreen::local_pixels(Layout(w,h),800); }
inline int Effective() { return Effective(theSettings.dwWidth, theSettings.dwHeight); }
inline int ScreenShift() { return owned::widescreen::pixel_x(Layout(),0.f); }
inline int VirtualShift() { return owned::widescreen::edge_shift(Layout(),1); }
inline int currentShift = 0;
class Scope
{
    int previous;
public:
    explicit Scope(int side) : previous(currentShift) { currentShift = owned::widescreen::edge_shift(Layout(),side); }
    ~Scope() { currentShift = previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
}
