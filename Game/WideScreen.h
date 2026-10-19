#pragma once
#include "GeneralSettings.h"
#include "hud_layout_context.h"

// Native producers share c74 XY anchors; composition alone owns live HUD scale.
namespace WideScreen
{
inline owned::widescreen::Layout Layout(int w,int h) { return owned::embedded_hud_layout().native_layout(unsigned(w),unsigned(h)); }
inline owned::widescreen::Layout Layout() { return Layout(theSettings.dwWidth,theSettings.dwHeight); }
inline int Effective(int w,int h) { return owned::widescreen::local_pixels(Layout(w,h),800); }
inline int Effective() { return Effective(theSettings.dwWidth, theSettings.dwHeight); }
inline int ScreenShift() { return owned::widescreen::pixel_x(Layout(),0.f); }
inline int VirtualShift() { return owned::widescreen::edge_shift(Layout(),1); }
inline thread_local owned::widescreen::Anchors currentAnchor{};
inline thread_local int rawDepth=0;
inline float PlaceX(float x) { return owned::widescreen::native_x(Layout(),currentAnchor,x,rawDepth); }
inline float PlaceY(float y) { return owned::widescreen::native_y(Layout(),currentAnchor,y,rawDepth); }
inline void PlaceRect(CRect& rect) { owned::widescreen::native_rect(Layout(),currentAnchor,rect); }
inline CRect PlacedRect(int left,int top,int right,int bottom) {
    CRect r(left,top,right,bottom);PlaceRect(r);return r;
}

struct RawScope { RawScope(){++rawDepth;} ~RawScope(){--rawDepth;} };
class Scope
{
    owned::widescreen::Anchors previous;
public:
    explicit Scope(owned::widescreen::Anchors anchor) : previous(currentAnchor) { currentAnchor=anchor; }
    ~Scope() { currentAnchor=previous; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
}
