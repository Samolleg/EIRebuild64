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

// CI3DFigure::SetRelativeToScreen for a placed point: the camera-space centre
// that projects through the clip-scaled projection to the point's pixel, at
// depth / size_scale so a 3D figure's own size follows the UI scale.
struct CameraPoint { float x, y, z; };
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
// Active only inside the verified floating combat-text producer.
inline thread_local float combat_text_scale = 1.f;

} // namespace owned::widescreen
