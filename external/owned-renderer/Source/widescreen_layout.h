#pragma once
// Owned widescreen: the pure screen mapping, shared by the game-site hooks
// (widescreen.cpp) and renderer composition.
//
// The engine lays out its interface in a virtual 800x600 space and builds the
// world with a fixed 4:3 projection whose horizontal clip scale lives in
// CDXGeometryProcessor::CreateClipScaleMatrix. Every CPU stack that draws,
// culls (CheckVisibility, clip flags, Calc2DClipRect) and picks
// (GetTerrainXYZFromScreenXY) multiplies that clip scale in, so one value
// keeps drawing, culling and picking consistent (hor+ on wide screens).
//
// Interface coordinates (see RENDERER_ARCHITECTURE.md, "Owned widescreen"):
//  - layout: the constants the game passes (a spell slot at 760, a dialog
//    rect), in the stock 800x600 space, relative to an anchor;
//  - placed: the 4:3 space centred on the screen, one unit = `unit` pixels.
//    The mouse (ScreenToVirtual) and every hit test work here. Centred content
//    (menus, camp, dialogs) is placed as laid out; an anchored HUD widget is
//    placed around its anchor point (the HUD safe area's edge, or for the chat
//    the 4:3 content's corner) at the UI scale.
//  - pixels: placed * unit around the screen centre. A HUD widget's 2D
//    surfaces are converted at full size around its anchor and composed by the
//    renderer at the UI scale around the same pivot, so text keeps its stock
//    size inside the surface and the whole widget shrinks together.
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <limits>

namespace owned::widescreen {

constexpr float kStockAspect = 4.0f / 3.0f;
// CI3DFigure VIEWING_ANGLE_TAN: (4/3) * tan(vertical half angle).
constexpr float kViewingAngleTan = 0.4815746188f;
// Horizontal half-angle tangent per unit aspect: 0.75 * VIEWING_ANGLE_TAN.
constexpr float kHalfAngleTanPerAspect = 0.75f * kViewingAngleTan;

// HUD width cap on wide screens (player option `hud_width`).
enum class HudWidth : int { full = 0, ultrawide_21_9 = 1, wide_16_9 = 2 };

// x: start/end = the HUD safe area's left/right edge; content_start = the 4:3
// content's left edge (chat); centre = unanchored. y: start/end = top/bottom.
enum class Anchor : std::int8_t { start = -1, centre = 0, end = 1, content_start = 2 };

struct Anchors {
    Anchor x = Anchor::centre;
    Anchor y = Anchor::centre;
};
inline bool anchored(const Anchors& a) noexcept { return a.x != Anchor::centre || a.y != Anchor::centre; }

struct Layout {
    float width = 800.0f;        // surface pixels
    float height = 600.0f;
    float scale = 1.0f;          // HUD UI scale (anchored widgets only)
    float unit = 1.0f;           // pixels per placed unit (the stock fit)
    float safe_left = 0.0f;      // HUD safe area in pixels
    float safe_right = 800.0f;
    float clip_x = 1.0f;         // CreateClipScaleMatrix _11 (hor+ on wide screens)
    float clip_y = 1.0f;         // magnitude of _22 (vert+ on narrow screens)
};

enum class ProjectionBasis : std::uint8_t { stock_aspect, actual_rect };
// Both Create's fixed 4:3 projection and the live world's actual-rect
// projection must preserve the same complete clip transform, including W.
inline void make_clip_scale_matrix(float* matrix, const Layout& layout,
    ProjectionBasis basis = ProjectionBasis::stock_aspect) noexcept {
    for (unsigned i = 0; i < 16; ++i) matrix[i] = 0.f;
    matrix[10] = matrix[15] = 1.f;
    if (basis == ProjectionBasis::actual_rect) {
        const float ratio = kStockAspect / (layout.width / layout.height);
        matrix[0] = ratio;
        matrix[5] = -ratio;
    } else {
        matrix[0] = layout.clip_x;
        matrix[5] = -layout.clip_y;
    }
}

inline float interface_font_scale(const Layout& layout) noexcept {
    return 0.01f * layout.width / (800.0f * layout.unit);
}
// Keep retail TextSize's integer multiply/divide and rounding divisors.
inline std::int32_t text_width_divisor(const Layout& layout) noexcept {
    return static_cast<std::int32_t>(800.0f * layout.unit + 0.5f);
}
inline std::int32_t text_height_divisor(const Layout& layout) noexcept {
    return static_cast<std::int32_t>(600.0f * layout.unit + 0.5f);
}
inline std::int32_t text_width(const Layout& layout, std::int32_t pixels) noexcept {
    return pixels * 800 / (std::max)(1, text_width_divisor(layout));
}
inline std::int32_t text_height(const Layout& layout, std::int32_t pixels) noexcept {
    return pixels * 600 / (std::max)(1, text_height_divisor(layout));
}

inline float hud_width_aspect(HudWidth width) noexcept {
    switch (width) {
    case HudWidth::ultrawide_21_9: return 64.0f / 27.0f;
    case HudWidth::wide_16_9: return 16.0f / 9.0f;
    default: return 0.0f;  // full width
    }
}

constexpr float kMinimumUiScale = 0.5f;

inline Layout make_layout(float width, float height, HudWidth hud = HudWidth::full,
                          float scale = 1.0f) noexcept {
    Layout layout;
    layout.width = width > 1.0f ? width : 1.0f;
    layout.height = height > 1.0f ? height : 1.0f;
    layout.scale = (std::min)(1.2f, (std::max)(kMinimumUiScale, scale));
    const float aspect = layout.width / layout.height;
    layout.unit = (std::min)(layout.width / 800.0f, layout.height / 600.0f);
    layout.clip_x = (std::min)(1.0f, kStockAspect / aspect);
    layout.clip_y = (std::min)(1.0f, aspect / kStockAspect);
    // The safe area never cuts into the 4:3 content.
    const float content = 800.0f * layout.unit;
    const float cap = hud_width_aspect(hud);
    float safe = cap > 0.0f ? (std::min)(layout.width, cap * layout.height) : layout.width;
    safe = (std::max)(safe, (std::min)(content, layout.width));
    layout.safe_left = 0.5f * (layout.width - safe);
    layout.safe_right = 0.5f * (layout.width + safe);
    return layout;
}

// Placed <-> pixels.
inline float placed_to_screen_x(const Layout& l, float x) noexcept { return 0.5f * l.width + (x - 400.0f) * l.unit; }
inline float placed_to_screen_y(const Layout& l, float y) noexcept { return 0.5f * l.height + (y - 300.0f) * l.unit; }
inline float screen_to_placed_x(const Layout& l, float x) noexcept { return 400.0f + (x - 0.5f * l.width) / l.unit; }
inline float screen_to_placed_y(const Layout& l, float y) noexcept { return 300.0f + (y - 0.5f * l.height) / l.unit; }

// An anchor's pivot: its layout reference and its placed position.
inline float anchor_reference_x(Anchor a) noexcept { return a == Anchor::end ? 800.0f : a == Anchor::centre ? 400.0f : 0.0f; }
inline float anchor_reference_y(Anchor a) noexcept { return a == Anchor::end ? 600.0f : a == Anchor::centre ? 300.0f : 0.0f; }
inline float anchor_pivot_x(const Layout& l, Anchor a) noexcept {
    switch (a) {
    case Anchor::start: return screen_to_placed_x(l, l.safe_left);
    case Anchor::end: return screen_to_placed_x(l, l.safe_right);
    case Anchor::content_start: return 0.0f;
    default: return 400.0f;
    }
}
inline float anchor_pivot_y(const Layout& l, Anchor a) noexcept {
    return a == Anchor::start ? screen_to_placed_y(l, 0.0f) : a == Anchor::end ? screen_to_placed_y(l, l.height) : 300.0f;
}
// Layout -> placed. Unanchored content is placed as laid out; an anchored
// widget at the UI scale around its pivot (scale 1 = the JMV layout).
inline float layout_to_placed_x(const Layout& l, const Anchors& a, float x, bool scaled = true) noexcept {
    if (!anchored(a)) return x;
    return anchor_pivot_x(l, a.x) + (scaled ? l.scale : 1.0f) * (x - anchor_reference_x(a.x));
}
inline float layout_to_placed_y(const Layout& l, const Anchors& a, float y, bool scaled = true) noexcept {
    if (!anchored(a)) return y;
    return anchor_pivot_y(l, a.y) + (scaled ? l.scale : 1.0f) * (y - anchor_reference_y(a.y));
}
// An anchored widget's output: native geometry and surface pixels remain
// full size until the renderer composes them around this pivot.
struct HudComposition {
    float scale = 1.0f, pivot_x = 0.0f, pivot_y = 0.0f;
    float scale_y = 1.0f, offset_x = 0.0f, offset_y = 0.0f;
    // Preserve the native screen aperture when relocating the sliding panel.
    bool clip_native_screen = false;
    // A final screen-space viewport; negative right selects the native aperture.
    float clip_left=0.f, clip_top=0.f, clip_right=-1.f, clip_bottom=-1.f;
    int rotation_quarters=0;
    float rotation_x=0.f,rotation_y=0.f;
    float opacity=1.f;
};
inline bool same_composition(const HudComposition& a, const HudComposition& b) noexcept {
    return a.scale == b.scale && a.scale_y == b.scale_y && a.pivot_x == b.pivot_x &&
           a.pivot_y == b.pivot_y && a.offset_x == b.offset_x && a.offset_y == b.offset_y &&
           a.clip_native_screen == b.clip_native_screen && a.clip_left==b.clip_left &&
           a.clip_top==b.clip_top && a.clip_right==b.clip_right && a.clip_bottom==b.clip_bottom && a.rotation_quarters==b.rotation_quarters &&
           a.rotation_x==b.rotation_x && a.rotation_y==b.rotation_y && a.opacity==b.opacity;
}
inline bool composition_scaled(const HudComposition& h) noexcept { return h.scale != 1.f || h.scale_y != 1.f; }
inline bool composition_changed(const HudComposition& h) noexcept {
    return composition_scaled(h) || h.offset_x != 0.f || h.offset_y != 0.f || h.rotation_quarters!=0;
}
inline HudComposition hud_composition(const Layout& l, const Anchors& a) noexcept {
    if (!anchored(a) || l.scale == 1.0f) return {};
    return {l.scale, placed_to_screen_x(l, anchor_pivot_x(l, a.x)), placed_to_screen_y(l, anchor_pivot_y(l, a.y)), l.scale};
}
inline float compose(float pixel, float pivot, float scale) noexcept { return pivot + scale * (pixel - pivot); }
inline float compose_x(float pixel, const HudComposition& h) noexcept { return compose(pixel,h.pivot_x,h.scale)+h.offset_x; }
inline float compose_y(float pixel, const HudComposition& h) noexcept { return compose(pixel,h.pivot_y,h.scale_y)+h.offset_y; }

inline void compose_point(float& x,float& y,const HudComposition& h) noexcept {
    x=compose_x(x,h);y=compose_y(y,h);
    const float dx=x-h.rotation_x,dy=y-h.rotation_y;
    switch((h.rotation_quarters%4+4)%4) {
    case 1:x=h.rotation_x-dy;y=h.rotation_y+dx;break;
    case 2:x=h.rotation_x-dx;y=h.rotation_y-dy;break;
    case 3:x=h.rotation_x+dy;y=h.rotation_y-dx;break;
    }
}

// The native shader multiplies column vectors; the compatibility shader
// multiplies row vectors. Compose only X/Y, retaining depth and homogeneous W.
inline void compose_hud_clip(float* matrix,const widescreen::HudComposition& hud,
                      float width,float height,bool row_vectors=false) noexcept {
 if(!composition_changed(hud))return;
 // Derive the physical-pixel affine transform, then map its two rows to
 // clip coordinates. Z and W remain exactly the native transform's values.
 float ox=(1.f-hud.scale)*hud.pivot_x+hud.offset_x;
 float oy=(1.f-hud.scale_y)*hud.pivot_y+hud.offset_y;
 float a=hud.scale,b=0.f,c=0.f,d=hud.scale_y;
 const float x0=ox,y0=oy;
 switch((hud.rotation_quarters%4+4)%4) {
 case 1:a=d=0.f;b=-hud.scale_y;c=hud.scale;
        ox=hud.rotation_x+hud.rotation_y-y0;oy=hud.rotation_y-hud.rotation_x+x0;break;
 case 2:a=-hud.scale;d=-hud.scale_y;
        ox=2.f*hud.rotation_x-x0;oy=2.f*hud.rotation_y-y0;break;
 case 3:a=d=0.f;b=hud.scale_y;c=-hud.scale;
        ox=hud.rotation_x-hud.rotation_y+y0;oy=hud.rotation_y+hud.rotation_x-x0;break;
 }
 const float tx=a+b*height/width+2.f*ox/width-1.f;
 const float ty=1.f-c*width/height-d-2.f*oy/height;
 for(unsigned index=0;index<4;++index){
  const unsigned xi=row_vectors?index*4:index,yi=row_vectors?index*4+1:index+4;
  const unsigned wi=row_vectors?index*4+3:index+12;
  const float x=matrix[xi],y=matrix[yi],w=matrix[wi];
  matrix[xi]=a*x-b*height/width*y+tx*w;
  matrix[yi]=-c*width/height*x+d*y+ty*w;
 }
}

// Surface-local lengths (inside one surface, carets, text extents).
inline float local_to_pixels(const Layout& l, float v) noexcept { return v * l.unit; }
inline float pixels_to_local(const Layout& l, float v) noexcept { return v / l.unit; }

// Integer ingress used by both the retail hooks and the source frontend.
// Endpoints round independently; real cursor positions floor for membership.
inline std::int32_t pixel_x(const Layout& l, float x) noexcept { return std::lround(placed_to_screen_x(l,x)); }
inline std::int32_t pixel_y(const Layout& l, float y) noexcept { return std::lround(placed_to_screen_y(l,y)); }
inline std::int32_t local_pixels(const Layout& l, std::int32_t v) noexcept { return std::lround(local_to_pixels(l,float(v))); }
inline std::int32_t local_units(const Layout& l, std::int32_t v) noexcept { return std::lround(pixels_to_local(l,float(v))); }
inline std::int32_t cursor_x(const Layout& l, std::int32_t v) noexcept { return std::int32_t(std::floor(screen_to_placed_x(l,float(v)))); }
inline std::int32_t cursor_y(const Layout& l, std::int32_t v) noexcept { return std::int32_t(std::floor(screen_to_placed_y(l,float(v)))); }
inline std::int32_t tooltip_right(const Layout& l) noexcept { return std::int32_t(std::floor(screen_to_placed_x(l,l.width))); }
inline std::int32_t tooltip_bottom(const Layout& l) noexcept { return std::int32_t(std::floor(screen_to_placed_y(l,l.height))); }
inline std::int32_t layout_int_x(const Layout& l,const Anchors& anchor,int x) noexcept { return std::lround(layout_to_placed_x(l,anchor,float(x),false)); }
inline std::int32_t layout_int_y(const Layout& l,const Anchors& anchor,int y) noexcept { return std::lround(layout_to_placed_y(l,anchor,float(y),false)); }
inline std::int32_t edge_shift(const Layout& l, int side) noexcept {
    if(!side)return 0;
    const Anchors anchor{side<0?Anchor::start:Anchor::end,Anchor::centre};
    return std::lround(layout_to_placed_x(l,anchor,0.f,false));
}

// Native ingress is unscaled: composition owns live HUD scaling. Builders
// place their entire input once, then suppress nested figure/area ingress.
constexpr Anchors kTopLeft{Anchor::start,Anchor::start};
constexpr Anchors kTopRight{Anchor::end,Anchor::start};
constexpr Anchors kBottomLeft{Anchor::start,Anchor::end};
constexpr Anchors kBottomRight{Anchor::end,Anchor::end};
constexpr Anchors kBottomCentre{Anchor::centre,Anchor::end};
inline float native_x(const Layout& l,const Anchors& a,float x,int raw=0) noexcept {
    return raw?x:layout_to_placed_x(l,a,x,false);
}
inline float native_y(const Layout& l,const Anchors& a,float y,int raw=0) noexcept {
    return raw?y:layout_to_placed_y(l,a,y,false);
}
inline void native_bar_corners(const Layout& layout,const Anchors& anchor,int raw,
                               float& l,float& t,float& r,float& b) noexcept {
    if(raw)return;
    if(anchor.x==Anchor::centre && l<r) {
        if(r<=0.f)l=(std::min)(l,screen_to_placed_x(layout,0.f));
        if(l>=800.f)r=(std::max)(r,screen_to_placed_x(layout,layout.width));
    }
    l=native_x(layout,anchor,l);r=native_x(layout,anchor,r);
    t=native_y(layout,anchor,t);b=native_y(layout,anchor,b);
}
template<class Rect> inline void native_rect(const Layout& l,const Anchors& a,Rect& r) noexcept {
    r.left=layout_int_x(l,a,r.left);r.right=layout_int_x(l,a,r.right);
    r.top=layout_int_y(l,a,r.top);r.bottom=layout_int_y(l,a,r.bottom);
}
// Surface endpoints stay floating point until final pixel rounding, as in
// VirtualToScreen's retail hook (not the integer native hit-rectangle store).
template<class Rect> inline void native_surface_rect(const Layout& l,const Anchors& a,Rect& r) noexcept {
    r.left=pixel_x(l,native_x(l,a,float(r.left)));r.right=pixel_x(l,native_x(l,a,float(r.right)));
    r.top=pixel_y(l,native_y(l,a,float(r.top)));r.bottom=pixel_y(l,native_y(l,a,float(r.bottom)));
}
template<class Rect> inline void placed_rect_to_pixels(const Layout& l, Rect& r) noexcept {
    r.left=pixel_x(l,float(r.left)); r.top=pixel_y(l,float(r.top));
    r.right=pixel_x(l,float(r.right)); r.bottom=pixel_y(l,float(r.bottom));
}
template<class Rect> inline void pixels_rect_to_placed(const Layout& l, Rect& r) noexcept {
    r.left=cursor_x(l,r.left); r.top=cursor_y(l,r.top);
    r.right=cursor_x(l,r.right); r.bottom=cursor_y(l,r.bottom);
}
template<class Rect> inline void local_rect_to_pixels(const Layout& l, Rect& r) noexcept {
    r.left=local_pixels(l,r.left); r.top=local_pixels(l,r.top);
    r.right=local_pixels(l,r.right); r.bottom=local_pixels(l,r.bottom);
}
template<class Rect> inline void pixels_rect_to_local(const Layout& l, Rect& r) noexcept {
    r.left=local_units(l,r.left); r.top=local_units(l,r.top);
    r.right=local_units(l,r.right); r.bottom=local_units(l,r.bottom);
}
// The stock tooltip body with the c74 right/bottom operands. Keep its native
// zero fallback for oversized tips, and its cursor-relative vertical flip.
template<class Rect> inline void clamp_tooltip(const Layout& l, Rect& r, std::int32_t cursor_y) noexcept {
    if(r.right>tooltip_right(l)) {
        auto dx=r.right-tooltip_right(l); r.left-=dx; r.right-=dx;
        if(r.left<0){r.right-=r.left;r.left=0;}
    }
    if(r.bottom>tooltip_bottom(l)) {
        auto dy=r.bottom-cursor_y; r.top-=dy; r.bottom-=dy;
        if(r.top<0){r.bottom-=r.top;r.top=0;}
    }
}

// A hit surface can cover either the whole output (source readback) or the
// centred content at half size (retail). Its declared origin is authoritative.
inline std::int32_t hit_surface_pixel(float supplied, float content_origin, float divisor) noexcept {
    return std::lround(supplied-content_origin/divisor);
}
inline bool campaign_minute(double hours, int& minute) noexcept {
    const double total=hours*60.0;
    if(!std::isfinite(total) || total<0 || total>(std::numeric_limits<int>::max)())return false;
    minute=int(total);return true;
}
inline void format_clock_time(char* text, std::size_t size, int minute) noexcept {
    // Native GDI is CP1251 in both frontends.
    std::snprintf(text,size,"\xC2\xF0\xE5\xEC\xFF \xF1\xF3\xF2\xEE\xEA: \xC4\xE5\xED\xFC %d, %02d:%02d",
        minute/1440+1,(minute%1440)/60,minute%60);
}
struct ClockTextLayout { int top,height; };
inline ClockTextLayout clock_text_layout(int native_height,int count,int line_height) noexcept {
    const int top=4+count*line_height;
    return {top,(std::max)(native_height+line_height,top+line_height+4)};
}
inline int clock_text_width(const Layout& l,int native_width,int measured_pixels) noexcept {
    return (std::max)(native_width,int(std::ceil(float(measured_pixels)/l.unit))+12);
}

// CI3DFigure::SetRelativeToScreen for a placed point: the camera-space centre
// that projects through the clip-scaled projection to the point's pixel, at
// depth / size_scale so a 3D figure's own size follows the UI scale.
struct CameraPoint { float x, y, z; };
inline CameraPoint native_camera_point(float x,float y,float depth) noexcept {
    return {(x/400.f-1.f)*kViewingAngleTan*depth,
            (y/400.f-.75f)*kViewingAngleTan*depth,depth};
}
inline CameraPoint camera_to_native(float x,float y,float z) noexcept {
    return {x/z/kViewingAngleTan*400.f+400.f,y/z/kViewingAngleTan*400.f+300.f,z};
}
inline std::int32_t portrait_native_x(const Layout& l,int slide=0) noexcept {
    return layout_int_x(l,kTopLeft,100)-slide;
}
struct MapCentre { std::int32_t dx,dy,inverse_scale; };
inline MapCentre map_centre(const Layout& l,const HudComposition& h) noexcept {
    const float x=placed_to_screen_x(l,layout_to_placed_x(l,kTopRight,700.f,false));
    const float y=placed_to_screen_y(l,layout_to_placed_y(l,kTopRight,80.f,false));
    return {-std::int32_t(std::lround(screen_to_placed_x(l,compose_x(x,h)))),
            -std::int32_t(std::lround(screen_to_placed_y(l,compose_y(y,h)))),
            std::int32_t(std::lround(65536.f/h.scale))};
}
// Match x86 IMUL low 32 bits and SAR 16, including negative coordinates.
inline std::int32_t map_offset(std::int32_t point,std::int32_t delta,std::int32_t scale) noexcept {
    const std::uint32_t bits=(std::uint32_t(point)+std::uint32_t(delta))*std::uint32_t(scale);
    const std::uint32_t high=bits>>16;
    return bits&0x80000000u ? std::int32_t(high)-65536 : std::int32_t(high);
}
inline CameraPoint placed_to_camera(const Layout& l, float x, float y, float depth, float size_scale = 1.0f) noexcept {
    const float z = depth / size_scale;
    const float clip_x = 2.0f * placed_to_screen_x(l, x) / l.width - 1.0f;
    const float clip_y = 2.0f * placed_to_screen_y(l, y) / l.height - 1.0f;
    return {kViewingAngleTan * z * clip_x / l.clip_x,
            0.75f * kViewingAngleTan * z * clip_y / l.clip_y, z};
}

// The engine bounds terrain sectors (CTerrain::SetClip) and drawn objects by a
// square of half side 100 around the camera, sized for the 4:3 frustum: at
// depth 100 the 4:3 view is 48 units wide each side. Past an aspect of about
// 2.77 (32:9 is 3.56) the frustum's sides leave that square before the depth
// limit, so the square grows by the horizontal half-angle tangent; at 21:9 and
// below it is unchanged.
inline float draw_square_factor(float width, float height) noexcept {
    const float aspect = width / (height > 1.0f ? height : 1.0f);
    return (std::max)(1.0f, kHalfAngleTanPerAspect * aspect);
}

// A full-screen picture (loading screen, movie frame) at its own aspect:
// contained and centred (pillarbox or letterbox) when its aspect differs from
// the output by more than 1%; cover_* is the centred crop of the picture with
// the output's aspect, for the blurred fill behind it.
struct PresentationFit {
    bool boxed = false;
    long left = 0, top = 0, right = 0, bottom = 0;
    float cover_u0 = 0.0f, cover_v0 = 0.0f, cover_u1 = 1.0f, cover_v1 = 1.0f;
};
inline PresentationFit presentation_fit(float source_width, float source_height,
                                        float output_width, float output_height) noexcept {
    PresentationFit fit;
    fit.right = static_cast<long>(output_width);
    fit.bottom = static_cast<long>(output_height);
    if (source_width <= 0.0f || source_height <= 0.0f || output_width <= 0.0f || output_height <= 0.0f) return fit;
    const float source = source_width / source_height, output = output_width / output_height;
    const float ratio = source / output;
    if (ratio > 0.99f && ratio < 1.01f) return fit;
    fit.boxed = true;
    if (ratio < 1.0f) {  // narrower: pillarbox
        const float width = output_height * source;
        fit.left = static_cast<long>(0.5f * (output_width - width) + 0.5f);
        fit.right = fit.left + static_cast<long>(width + 0.5f);
        fit.cover_v0 = 0.5f * (1.0f - ratio);
        fit.cover_v1 = 1.0f - fit.cover_v0;
    } else {             // wider: letterbox
        const float height = output_width / source;
        fit.top = static_cast<long>(0.5f * (output_height - height) + 0.5f);
        fit.bottom = fit.top + static_cast<long>(height + 0.5f);
        fit.cover_u0 = 0.5f * (1.0f - 1.0f / ratio);
        fit.cover_u1 = 1.0f - fit.cover_u0;
    }
    return fit;
}

// A short player-visible line while the widescreen contract is broken (set by
// widescreen.cpp, drawn by the renderer's status notice), else null.
inline const char* notice = nullptr;
// The interface sites are ours (set at game start by widescreen.cpp).
inline bool interface_owned = false;
// The anchored widget on this thread composes its visual output with this
// (identity outside anchored widgets or at 100%): the renderer reads it when
// an interface blit, text or geometry submission is queued.
inline thread_local HudComposition hud_blit{};

} // namespace owned::widescreen
