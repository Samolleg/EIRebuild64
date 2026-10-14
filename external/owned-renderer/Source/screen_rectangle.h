#pragma once

#include <cstdint>

namespace owned {

// CVisualizatorD3D::DrawFrameSelection (game 005041F0) as a compact owned
// overlay request: rubber selection, cinematic bars and screen fades. Game
// screen pixels; the game keeps selection, input and fade state.
struct ScreenRectangleRequest {
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    std::uint8_t alpha = 0;  // black fill alpha
    bool frame = false;      // opaque white border, fill inset by one pixel
};

// Stock contract: an inverted rectangle draws nothing and returns FALSE.
inline bool make_screen_rectangle_request(int x1, int y1, int x2, int y2,
                                          char alpha, bool frame,
                                          ScreenRectangleRequest& out) noexcept {
    if (x2 < x1 || y2 < y1) return false;
    out = {x1, y1, x2, y2, static_cast<std::uint8_t>(alpha), frame};
    return true;
}

// Half-open pixel span [left,right) x [top,bottom) and ARGB colour.
struct ScreenRectangleQuad {
    int left = 0, top = 0, right = 0, bottom = 0;
    std::uint32_t color = 0;
};

// Pixel coverage of the stock draw. Its fill quad spans (x1+f,y1+f) to
// (x2-f,y2-f), f = 1 when framed, and covers the pixels whose centres lie in
// that half-open span. The framed line strip x1,y1 -> x1,y2 -> x2,y2 -> x2,y1
// -> x1,y1 covers the inclusive perimeter of [x1,x2] x [y1,y2]; the column
// x2-1 and row y2-1 inside it stay unfilled exactly as in the stock draw.
inline unsigned screen_rectangle_quads(const ScreenRectangleRequest& r,
                                       ScreenRectangleQuad (&out)[5]) noexcept {
    unsigned count = 0;
    const int inset = r.frame ? 1 : 0;
    const int left = r.x1 + inset, top = r.y1 + inset;
    const int right = r.x2 - inset, bottom = r.y2 - inset;
    if (r.alpha && right > left && bottom > top)
        out[count++] = {left, top, right, bottom, std::uint32_t(r.alpha) << 24};
    if (!r.frame) return count;
    const std::uint32_t white = 0xffffffffu;
    out[count++] = {r.x1, r.y1, r.x2 + 1, r.y1 + 1, white};
    if (r.y2 > r.y1) out[count++] = {r.x1, r.y2, r.x2 + 1, r.y2 + 1, white};
    if (r.y2 - r.y1 > 1) {
        out[count++] = {r.x1, r.y1 + 1, r.x1 + 1, r.y2, white};
        if (r.x2 > r.x1) out[count++] = {r.x2, r.y1 + 1, r.x2 + 1, r.y2, white};
    }
    return count;
}

}  // namespace owned
