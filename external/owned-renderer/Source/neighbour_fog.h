#pragma once
#include "neighbour_layout.h"
#include <algorithm>
#include <cmath>

namespace owned {

// Border fog across rendered neighbour maps (render only). On each side of the
// active map that an accepted neighbour touches, wholly or partly
// (neighbour_fog_contacts), fog starts `inside` metres within the active map
// (density 0), reaches `seam_density` at the seam and eases (smoothstep) to full
// density `outside` metres into the neighbour, so distant shapes linger as
// silhouettes in the haze; content is culled only kNeighbourFogCullMargin
// times that far, beyond full fog, so nothing pops. Along the touched run the game's
// band of both adjoining pieces gives way to this curve; the rest of the side
// and sides without a neighbour keep the band. F10 neighbour_fog_range scales
// both distances.
constexpr double kNeighbourFogInside = 20.0, kNeighbourFogOutside = 80.0, kNeighbourFogCullMargin = 1.2;
constexpr double kNeighbourFogSeamDensity = 0.2, kNoNeighbourSeam = 1e30;
constexpr double kNeighbourFogRangeMin = 0.5, kNeighbourFogRangeMax = 4.0;
// Rendered pieces (active map first) the GPU constants carry. Layouts hold at
// most 4 neighbours per map; a piece beyond this is treated as void, so its
// adjoining band stays visible.
constexpr unsigned kNeighbourFogPieces = 8;

struct NeighbourFogField {
    // Seam per side; a side with no rendered neighbour sits at +-kNoNeighbourSeam
    // so its curve never reaches the map.
    NeighbourBounds seam{-kNoNeighbourSeam, -kNoNeighbourSeam, kNoNeighbourSeam, kNoNeighbourSeam};
    double inside = kNeighbourFogInside, outside = kNeighbourFogOutside;
    double seam_density = kNeighbourFogSeamDensity;
    // Touched run per side, where the band gives way (same side order).
    NeighbourContacts span;
    // Every rendered piece's raw rectangle, active map first. A neighbour
    // piece's own band gives way wherever one of these adjoins it.
    std::array<NeighbourBounds, kNeighbourFogPieces> pieces{};
    unsigned piece_count = 0;
};

inline double neighbour_fog_range(double scale) noexcept {
    return std::isfinite(scale) ? std::clamp(scale, kNeighbourFogRangeMin, kNeighbourFogRangeMax) : 1.0;
}

// The player's draw distance (metres) caps how deep neighbour content reaches:
// the range is lowered until the cull distance beyond the seam fits within it,
// so full fog still comes before the cut. A range already inside is unchanged.
inline double neighbour_fog_reach(double scale, double draw_distance) noexcept {
    const double range = neighbour_fog_range(scale);
    if (!(draw_distance > 0)) return range;
    return std::clamp(draw_distance / (kNeighbourFogOutside * kNeighbourFogCullMargin), kNeighbourFogRangeMin, range);
}

// active: the active map rectangle; contacts: neighbour_fog_contacts(active, accepted).
inline NeighbourFogField neighbour_fog_field(const NeighbourBounds& active,
    const NeighbourContacts& contacts, double scale) noexcept {
    NeighbourFogField f;
    const double range = neighbour_fog_range(scale);
    f.inside = kNeighbourFogInside * range;f.outside = kNeighbourFogOutside * range;
    f.span = contacts;
    if (contacts.any(0)) f.seam.x0 = active.x0;
    if (contacts.any(1)) f.seam.y0 = active.y0;
    if (contacts.any(2)) f.seam.x1 = active.x1;
    if (contacts.any(3)) f.seam.y1 = active.y1;
    return f;
}

// Moves the covered seams by (dx,dy), e.g. active-map to world coordinates.
inline NeighbourFogField neighbour_fog_translated(NeighbourFogField f, double dx, double dy) noexcept {
    if (f.seam.x0 > -kNoNeighbourSeam) f.seam.x0 += dx;
    if (f.seam.x1 < kNoNeighbourSeam) f.seam.x1 += dx;
    if (f.seam.y0 > -kNoNeighbourSeam) f.seam.y0 += dy;
    if (f.seam.y1 < kNoNeighbourSeam) f.seam.y1 += dy;
    for (int side = 0; side < 4; ++side) {
        const double d = side == 0 || side == 2 ? dy : dx;
        f.span.lo[side] += d;f.span.hi[side] += d;
    }
    for (unsigned k = 0; k < f.piece_count; ++k) {
        auto& r = f.pieces[k];r.x0 += dx;r.x1 += dx;r.y0 += dy;r.y1 += dy;
    }
    return f;
}

// Adds a rendered piece; false (piece left out) once the GPU list is full.
inline bool neighbour_fog_add_piece(NeighbourFogField& f, const NeighbourBounds& piece) noexcept {
    if (f.piece_count >= kNeighbourFogPieces) return false;
    f.pieces[f.piece_count++] = piece;return true;
}

// Per-side curve at signed distance s outward from the seam.
inline double neighbour_seam_side(double s, const NeighbourFogField& f) noexcept {
    const double u = std::clamp(s / f.outside, 0.0, 1.0);
    return f.seam_density * std::clamp(s / f.inside + 1.0, 0.0, 1.0) +
           (1.0 - f.seam_density) * u * u * (3.0 - 2.0 * u);
}
// Distance beyond a seam where content is culled (past full fog).
inline double neighbour_fog_cull_distance(const NeighbourFogField& f) noexcept {
    return f.outside * kNeighbourFogCullMargin;
}

// Squared seam fog; sides combine like the game band's corners.
inline double neighbour_seam_fog(double x, double y, const NeighbourFogField& f) noexcept {
    const double a = neighbour_seam_side(f.seam.x0 - x, f), b = neighbour_seam_side(f.seam.y0 - y, f);
    const double c = neighbour_seam_side(x - f.seam.x1, f), d = neighbour_seam_side(y - f.seam.y1, f);
    return a * a + b * b + c * c + d * d;
}

// Full border fog density 0..1 (1 hides). band: the game's clear rectangle
// (already inset by the band distance) fading out over `distance`. Within the
// band's reach of a seam, along its touched run (eased in over `distance` from
// each end), the band terms across that seam fade out on both adjoining pieces.
inline double border_fog_density(double x, double y, const NeighbourBounds& band,
    double distance, const NeighbourFogField& f) noexcept {
    const double seam[4] = {f.seam.x0, f.seam.y0, f.seam.x1, f.seam.y1};
    double cover[4];
    for (int side = 0; side < 4; ++side) {
        const double along = side & 1 ? x : y, across = side & 1 ? y : x;
        cover[side] = std::fabs(across - seam[side]) <= distance + 2 ?
            std::clamp((std::min)(along - f.span.lo[side], f.span.hi[side] - along) / distance, 0.0, 1.0) : 0.0;
    }
    const double keep_x = 1 - (std::max)(cover[0], cover[2]), keep_y = 1 - (std::max)(cover[1], cover[3]);
    const double a = (std::min)(x - band.x0, 0.0) / distance * keep_x, b = (std::min)(band.x1 - x, 0.0) / distance * keep_x;
    const double c = (std::min)(y - band.y0, 0.0) / distance * keep_y, d = (std::min)(band.y1 - y, 0.0) / distance * keep_y;
    return (std::min)(std::sqrt(a * a + b * b + c * c + d * d + neighbour_seam_fog(x, y, f)), 1.0);
}

// Share of a rendered piece side's band that gives way at `along`: the pieces
// just across the side's line (0.5 m out) cover that point, eased over
// `distance` toward the nearest uncovered point along the line (adjoining
// pieces merge into one run). line/outward: the side's coordinate and its
// outward sign; along_y: the side runs along y (west and east sides).
inline double neighbour_piece_side_cover(double line, double outward, double along, bool along_y,
    double distance, const NeighbourFogField& f) noexcept {
    const double across = line + outward * 0.5;
    double lo = along, hi = along;bool found = false;
    const auto runs = [&](const NeighbourBounds& r, double& s0, double& s1) {
        s0 = along_y ? r.y0 : r.x0;s1 = along_y ? r.y1 : r.x1;
        const double a0 = along_y ? r.x0 : r.y0, a1 = along_y ? r.x1 : r.y1;
        return across >= a0 && across <= a1;
    };
    for (unsigned k = 0; k < f.piece_count; ++k) {
        double s0, s1;
        if (runs(f.pieces[k], s0, s1) && along >= s0 && along <= s1) {found = true;lo = (std::min)(lo, s0);hi = (std::max)(hi, s1);}
    }
    if (!found) return 0;
    for (unsigned pass = 0; pass < f.piece_count; ++pass) for (unsigned k = 0; k < f.piece_count; ++k) {
        double s0, s1;
        if (!runs(f.pieces[k], s0, s1)) continue;
        if (s0 <= hi && s1 > hi) hi = s1;
        if (s1 >= lo && s0 < lo) lo = s0;
    }
    return std::clamp((std::min)(along - lo, hi - along) / distance, 0.0, 1.0);
}

// Border fog density 0..1 of a point on a rendered neighbour piece (raw
// edges). Its own band (inset distance+1, as the game's) gives way wherever
// another rendered piece adjoins it, and stays only toward void; the active
// map's seam curve adds as everywhere. Neighbour terrain, decorations and
// ground cover all use this, so they fog alike.
inline double neighbour_piece_fog_density(double x, double y, const NeighbourBounds& piece,
    double distance, const NeighbourFogField& f) noexcept {
    const double inset = distance + 1;
    const double t[4] = {(std::min)(x - (piece.x0 + inset), 0.0) / distance, (std::min)(y - (piece.y0 + inset), 0.0) / distance,
        (std::min)(piece.x1 - inset - x, 0.0) / distance, (std::min)(piece.y1 - inset - y, 0.0) / distance};
    const double line[4] = {piece.x0, piece.y0, piece.x1, piece.y1};
    double sum = 0;
    for (int side = 0; side < 4; ++side) {
        if (t[side] >= 0) continue;
        const bool along_y = side == 0 || side == 2;
        const double keep = 1 - neighbour_piece_side_cover(line[side], side < 2 ? -1.0 : 1.0, along_y ? y : x, along_y, distance, f);
        sum += t[side] * keep * t[side] * keep;
    }
    return (std::min)(std::sqrt(sum + neighbour_seam_fog(x, y, f)), 1.0);
}

// True when the box lies wholly beyond the cull distance (in full neighbour
// fog with margin): draw nothing there. Neighbour terrain, decorations and
// ground cover placement use this.
inline bool neighbour_fog_hides(const NeighbourBounds& box, const NeighbourFogField& f) noexcept {
    const double d = neighbour_fog_cull_distance(f);
    return box.x1 <= f.seam.x0 - d || box.x0 >= f.seam.x1 + d ||
           box.y1 <= f.seam.y0 - d || box.y0 >= f.seam.y1 + d;
}

// GPU form: seam (x0,y0,x1,y1), curve (inside, outside, seam density, 0) and
// the touched runs' starts and ends per side.
inline void neighbour_fog_constants(const NeighbourFogField& f, float seam[4], float curve[4],
    float span_lo[4], float span_hi[4]) noexcept {
    seam[0] = float(f.seam.x0);seam[1] = float(f.seam.y0);seam[2] = float(f.seam.x1);seam[3] = float(f.seam.y1);
    curve[0] = float(f.inside);curve[1] = float(f.outside);curve[2] = float(f.seam_density);curve[3] = 0.f;
    for (int side = 0; side < 4; ++side) {span_lo[side] = float(f.span.lo[side]);span_hi[side] = float(f.span.hi[side]);}
}
// GPU form of the rendered pieces: info.x the count, then (x0,y0,x1,y1) each.
inline void neighbour_fog_piece_constants(const NeighbourFogField& f, float info[4],
    float pieces[kNeighbourFogPieces][4]) noexcept {
    info[0] = float(f.piece_count);info[1] = info[2] = info[3] = 0.f;
    for (unsigned k = 0; k < kNeighbourFogPieces; ++k) {
        const auto& r = f.pieces[k];
        pieces[k][0] = float(r.x0);pieces[k][1] = float(r.y0);pieces[k][2] = float(r.x1);pieces[k][3] = float(r.y1);
    }
}

// Same math as border_fog_density for every world shader.
constexpr char kNeighbourFogHlsl[] = R"(
float neighbourSeamFog(float2 p,float4 seam,float4 curve){
 float4 s=float4(seam.xy-p,p-seam.zw);
 float4 u=saturate(s/curve.y);
 float4 c=curve.z*saturate(s/curve.x+1)+(1-curve.z)*u*u*(3-2*u);
 return dot(c,c);
}
float borderFogDensity(float2 p,float4 band,float bandDistance,float4 seam,float4 curve,float4 spanLo,float4 spanHi){
 float4 along=p.yxyx;
 float4 cover=saturate(min(along-spanLo,spanHi-along)/bandDistance)*step(abs(p.xyxy-seam),bandDistance+2);
 float2 keep=1-max(cover.xy,cover.zw);
 float4 outside=min(float4(p-band.xy,band.zw-p),0)/bandDistance*keep.xyxy;
 return saturate(sqrt(dot(outside,outside)+neighbourSeamFog(p,seam,curve)));
}
static const uint NEIGHBOUR_FOG_PIECES=8;
float neighbourPieceSideCover(float edge,float outward,float along,bool alongY,float d,float4 pieces[NEIGHBOUR_FOG_PIECES],uint count){
 float across=edge+outward*.5,lo=along,hi=along;bool found=false;
 [loop]for(uint k=0;k<count;++k){float4 r=pieces[k];float2 s=alongY?r.yw:r.xz,a=alongY?r.xz:r.yw;
  if(across>=a.x&&across<=a.y&&along>=s.x&&along<=s.y){found=true;lo=min(lo,s.x);hi=max(hi,s.y);}}
 if(!found)return 0;
 [loop]for(uint sweep=0;sweep<count;++sweep)[loop]for(uint m=0;m<count;++m){float4 r=pieces[m];float2 s=alongY?r.yw:r.xz,a=alongY?r.xz:r.yw;
  if(across>=a.x&&across<=a.y){if(s.x<=hi&&s.y>hi)hi=s.y;if(s.y>=lo&&s.x<lo)lo=s.x;}}
 return saturate(min(along-lo,hi-along)/d);
}
float neighbourPieceFogDensity(float2 p,float4 piece,float d,float4 seam,float4 curve,float4 pieces[NEIGHBOUR_FOG_PIECES],uint count){
 float4 t=min(float4(p-piece.xy-(d+1),piece.zw-(d+1)-p),0)/d;
 float sum=0;
 [unroll]for(uint side=0;side<4;++side)[branch]if(t[side]<0){
  bool alongY=(side&1)==0;
  float keep=1-neighbourPieceSideCover(piece[side],side<2?-1:1,alongY?p.y:p.x,alongY,d,pieces,count);
  sum+=t[side]*keep*t[side]*keep;
 }
 return saturate(sqrt(sum+neighbourSeamFog(p,seam,curve)));
}
)";

} // namespace owned
