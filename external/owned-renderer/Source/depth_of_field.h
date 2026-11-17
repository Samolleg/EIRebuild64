#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include "shader_cache.h"

namespace owned {

// Far-field depth of field for tilted cameras.
//
// Strength comes from the camera that produced the frame: the depression of
// the screen-centre ray below the horizon. The game's gameplay camera ranges
// from near top-down to a low tilt; at 50 degrees and steeper nothing blurs
// (the normal gameplay view stays crisp, and the passes are skipped), at 25
// degrees and flatter the full strength applies.
// Focus is the median view depth of nine depth taps around the screen centre,
// smoothed in log space on the GPU (1x1 ping-pong, no readback).
// Far field only: everything nearer than the focus stays sharp, so the party
// and objects in front of the focus point are never blurred.
//
// Passes, all fullscreen triangles (work resolution: the scene divided by
// 2, or by 3 above 1440 lines, so cost and look do not grow at 4K):
//   focus      1x1   median of 9 depth taps, smoothed toward the last focus
//   prefilter  work  4 depth loads + 4 colour taps; CoC-weighted colour, max CoC
//   gather     work  golden-angle disc (low 16, high 40 taps), scatter-as-gather
//   fill       work  8-tap ring at the disc's tap spacing, closes the gaps
//   composite  full  5 depth loads (nearest = eroded CoC), 4 work loads,
//                    alpha-blended over the resolved world image; sharp
//                    pixels discard and keep the SMAA output untouched.
// No foreground bleed: in-focus texels (CoC < 0.5 work px) carry zero weight in
// the gather, fill and upsample, so a sharp object's colour never spreads into
// the blurred background, and its silhouette keeps its full-resolution edge.
// Sky (depth exactly at the cleared far plane) is at infinite distance and
// stays sharp: it never blurs and never feeds the terrain blur. Only blurred
// far terrain scatters over it, in a band above the silhouette no wider than
// that terrain's own blur radius: the sky texel's gather measures how much of
// the terrain blur reaches it (coverage, carried as alpha -1 - coverage), and
// the composite lays the terrain blur over the sharp sky by that coverage, so
// the horizon edge stays continuous with no cut-out or halo.
//
// Caller saves/restores pipeline state; slots used: PS b0, PS t0-t2, PS s0.

struct DepthOfFieldLens {
    bool active = false;
    float pitch_factor = 0.f;  // 0 top-down .. 1 low tilt
    float aperture = 0.f;      // CoC radius at infinity, work pixels (before zoom)
    float margin = 0.f;        // blur starts at focus * (1 + margin)
};

// 1 at 25 degrees below the horizon and flatter, 0 at 50 degrees and steeper.
inline float depth_of_field_pitch_factor(float depression_degrees) noexcept {
    const float t = std::clamp((50.f - depression_degrees) / 25.f, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

// Lens of a frame from its clip-to-world matrix (row-major, column vectors,
// z up) and the player's strength (0..3, 1 = default). Strength scales the
// blur radius and moves its start nearer the focus.
inline DepthOfFieldLens depth_of_field_lens(const float clip_to_world[16], float strength, float work_height) noexcept {
    DepthOfFieldLens lens{};
    auto unproject = [&](float z, float out[3]) {
        const float* m = clip_to_world;
        const float w = m[14] * z + m[15];
        if (!(std::fabs(w) > 1e-12f)) return false;
        for (int r = 0; r < 3; ++r) out[r] = (m[r * 4 + 2] * z + m[r * 4 + 3]) / w;
        return true;
    };
    float near_point[3], far_point[3];
    if (!(strength > 0.f) || !unproject(0.f, near_point) || !unproject(.5f, far_point)) return lens;
    const float d[3] = {far_point[0] - near_point[0], far_point[1] - near_point[1], far_point[2] - near_point[2]};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(length > 0.f)) return lens;
    const float depression = std::asin(std::clamp(-d[2] / length, -1.f, 1.f)) * 57.2957795f;
    strength = std::min(strength, 3.f);
    lens.pitch_factor = depth_of_field_pitch_factor(depression);
    // Radius at infinity: 1.2% of the work height per unit strength (6.5 work
    // px, 13 screen px at 1080p for the default; 39 screen px at strength 3).
    lens.aperture = .012f * strength * work_height * lens.pitch_factor;
    lens.margin = .6f / (.4f + strength);
    lens.active = lens.aperture >= .25f;
    return lens;
}

// Golden-angle (Vogel) disc taps, emitted as HLSL literals.
inline std::string depth_of_field_shader_source(int quality) {
    const int taps = quality >= 2 ? 40 : 16;
    std::string s = "#define DOF_TAPS " + std::to_string(taps) + "\n";
    char line[96];
    std::snprintf(line, sizeof(line), "#define DOF_FILL_SHARE %.4f\n", 1.8 / std::sqrt(double(taps)));
    s += line;
    s += "static const float3 kDofTaps[DOF_TAPS] = {\n";
    for (int k = 0; k < taps; ++k) {
        const double r = std::sqrt((k + .5) / taps), a = k * 2.39996323;
        std::snprintf(line, sizeof(line), "%s float3(%.6f, %.6f, %.6f)", k ? ",\n" : "", r * std::cos(a), r * std::sin(a), r);
        s += line;
    }
    s += "\n};\n";
    return s + R"(
cbuffer DepthOfField : register(b0) {
    float4 dofDepthRow;  // clip_to_world row 3: view depth = 1/dot(row, float4(ndc.xy, depth, 1))
    float4 dofLens;      // aperture (work px at infinity), focus margin, debug view (1: paths, D3D12; after an upscaler the stable weights), focus blend (1 = snap)
    float4 dofFull;      // width, height, 1/width, 1/height
    float4 dofWork;      // work width, height, divisor, temporal: this frame's share of the blur (0 off, 1 reset; D3D12 after the upscaler)
    float4 dofDepthMap;  // xy: the depth's jitter (depth pixels), z: depth pixels per colour pixel (D3D12 after the upscaler; else 0, 0, 1)
};
Texture2D<float> depthTex : register(t0);
Texture2D source : register(t1);
Texture2D<float> focusTex : register(t2);
// Temporal (D3D12 after the upscaler only): the upscaler's motion (render
// pixels, previous minus current, unjittered), last frame's result, and the
// stable full-size blur weight (dofAlphaPS) for the composite.
Texture2D<float2> motionTex : register(t3);
Texture2D historyTex : register(t4);
Texture2D<float> alphaTex : register(t5);
// The layer's history motion and depth (motionPS), this and last frame's.
Texture2D layerMotionTex : register(t9);
Texture2D layerMotionLast : register(t11);
SamplerState linearClamp : register(s0);
static const float DOF_FOCUS_MIN = 2, DOF_FOCUS_MAX = 600, DOF_ZOOM_REFERENCE = 40, DOF_MAX_RADIUS = 24;
struct V { float4 position : SV_Position; };
V dofVS(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    V o; o.position = float4(uv*float2(2, -2) + float2(-1, 1), 0, 1); return o;
}
float viewDepth(float depth, float2 uv) {
    return 1.0/max(dot(dofDepthRow, float4(uv.x*2 - 1, 1 - uv.y*2, depth, 1)), 1e-6);
}
// Far CoC radius in work pixels of a non-sky depth. A wider focus distance
// (zoomed out) narrows the lens like a real camera focused further away.
float dofAperture(float focus) { return dofLens.x*clamp(DOF_ZOOM_REFERENCE/focus, 0.5, 1.0); }
float dofCoc(float depth, float2 uv, float focus) {
    return dofAperture(focus)*saturate(1.0 - focus*(1.0 + dofLens.y)/viewDepth(depth, uv));
}
// Sky: the cleared far plane, behind everything and never blurred.
bool dofSky(float depth) { return depth >= 1.0; }
// The depth under colour pixel p: after an upscaler the depth is render size
// and jittered, so the pixel centre is scaled and the jitter added back.
float loadDepth(int2 p) {
    int2 q = int2(floor((p + 0.5)*dofDepthMap.z + dofDepthMap.xy));
    return depthTex.Load(int3(clamp(q, 0, int2(dofFull.xy*dofDepthMap.z + 0.5) - 1), 0));
}
float4 loadWork(int2 p) { return source.Load(int3(clamp(p, 0, int2(dofWork.xy) - 1), 0)); }

float focusTap(float2 uv) {
    int2 p = int2(uv*dofFull.xy);
    float depth = loadDepth(p);
    return depth >= 1.0 ? DOF_FOCUS_MAX : clamp(viewDepth(depth, (p + 0.5)*dofFull.zw), DOF_FOCUS_MIN, DOF_FOCUS_MAX);
}
float4 focusPS(V i) : SV_Target {
    float v[9];
    [unroll] for (int k = 0; k < 9; ++k) v[k] = focusTap(0.5 + float2(k%3 - 1, k/3 - 1)*0.05);
    // Median: one tree trunk or unit crossing the centre does not pull focus.
    [unroll] for (int a = 0; a < 8; ++a) {
        [unroll] for (int b = 0; b < 8 - a; ++b) {
            float lo = min(v[b], v[b + 1]), hi = max(v[b], v[b + 1]); v[b] = lo; v[b + 1] = hi;
        }
    }
    float previous = max(focusTex.Load(int3(0, 0, 0)), DOF_FOCUS_MIN);
    return exp(lerp(log(previous), log(v[4]), dofLens.w));
}
float4 prefilterPS(V i) : SV_Target {
    float focus = focusTex.Load(int3(0, 0, 0));
    float2 centre = i.position.xy*dofWork.z;  // work texel centre in screen pixels
    float q = dofWork.z*0.25;
    float3 sum = 0; float weight = 0, coc = 0; bool sky = true;
    [unroll] for (int k = 0; k < 4; ++k) {
        float2 pos = centre + float2((k & 1) ? q : -q, (k & 2) ? q : -q);
        float2 uv = pos*dofFull.zw;
        float depth = loadDepth(int2(pos));
        float c = dofSky(depth) ? 0 : dofCoc(depth, uv, focus);
        sky = sky && dofSky(depth);
        // Clamp rejects negative, infinite and NaN input before it can spread.
        float w = c + 1e-4;
        sum += clamp(source.SampleLevel(linearClamp, uv, 0).rgb, 0, 64)*w; weight += w; coc = max(coc, c);
    }
    // Alpha: CoC in work pixels, or -1 for an all-sky texel.
    return float4(sum/weight, sky ? -1 : coc);
}
// In-focus texels carry no weight; a blurred texel covers this one when its
// own radius reaches the distance (scatter as gather).
float dofWeight(float sampleCoc, float distance) { return saturate(sampleCoc - distance + 1)*saturate(sampleCoc - 0.5); }
float4 gatherPS(V i) : SV_Target {
    int2 p = int2(i.position.xy);
    float4 c = loadWork(p);
    if (c.a < 0) {
        // Sky: how much blurred terrain reaches this texel. Taps span the
        // widest terrain blur; each one counts when its own radius reaches.
        // Twice the covered share: 1 at the silhouette, 0 one radius above.
        float reach = min(dofAperture(focusTex.Load(int3(0, 0, 0))), DOF_MAX_RADIUS);
        if (reach < 0.5) return c;
        float3 sum = 0; float weight = 0;
        [unroll] for (int k = 0; k < DOF_TAPS; ++k) {
            float4 s = loadWork(int2(floor(i.position.xy + kDofTaps[k].xy*reach)));
            float w = dofWeight(s.a, kDofTaps[k].z*reach);
            sum += s.rgb*w; weight += w;
        }
        return weight > 0 ? float4(sum/weight, -1 - saturate(2*weight/DOF_TAPS)) : c;
    }
    float r = min(c.a, DOF_MAX_RADIUS);
    if (r < 0.5) return c;
    float3 sum = c.rgb; float weight = 1;
    [unroll] for (int k = 0; k < DOF_TAPS; ++k) {
        float4 s = loadWork(int2(floor(i.position.xy + kDofTaps[k].xy*r)));
        float w = dofWeight(s.a, kDofTaps[k].z*r);
        sum += s.rgb*w; weight += w;
    }
    return float4(sum/weight, c.a);
}
float4 fillPS(V i) : SV_Target {
    int2 p = int2(i.position.xy);
    float4 c = loadWork(p);
    float r = min(c.a, DOF_MAX_RADIUS);
    if (r < 0.5) return c;
    float f = r*DOF_FILL_SHARE;
    float3 sum = c.rgb; float weight = 1;
    [unroll] for (int k = 0; k < 8; ++k) {
        float2 o = float2(cos(k*0.785398), sin(k*0.785398))*f;
        float4 s = loadWork(int2(floor(i.position.xy + o)));
        float w = dofWeight(s.a, f);
        sum += s.rgb*w; weight += w;
    }
    return float4(sum/weight, c.a);
}
// ---- Temporal (D3D12 after the upscaler). The depth after an upscaler is
// one jittered sample per render pixel: every sky/terrain, sharp/blur and
// CoC decision taken from it flips with the jitter at each silhouette, every
// frame (the DLSS colour itself is stable). The blur is therefore carried as
// a premultiplied layer (colour * weight, weight) whose weight is continuous
// (blurred terrain: its radius ramp, sky: the terrain blur's coverage, sharp:
// 0), and both that work layer and the full-size blend weight are averaged
// over frames along the upscaler's motion (object motion included), the
// history clamped to this frame's neighbourhood so moving units and pans
// leave no trail.
float4 dofLayer(float4 s) {
    float w = s.a < 0 ? saturate(-1 - s.a) : saturate((s.a - 0.25)/1.25);
    return float4(s.rgb*w, w);
}
// The motion at output pixel p, in output pixels (the depth's texel mapping).
// The history is reprojected with the motion of the nearest surface around
// the pixel (dilated motion, as temporal AA takes it): blurred near
// silhouettes spread their layer over the sky and far ground around them, and
// those pixels' own (sky) motion dragged that halo behind distant trees while
// the camera turned or panned (user, cand-1108).
float2 dofMotion(int2 p) {
    int2 q = int2(floor((p + 0.5)*dofDepthMap.z + dofDepthMap.xy));
    return motionTex.Load(int3(clamp(q, 0, int2(dofFull.xy*dofDepthMap.z + 0.5) - 1), 0))/dofDepthMap.z;
}
// The work layer (source: premultiplied) bilinear at output position pos.
float4 dofWorkLayer(float2 pos) {
    float2 w = pos/dofWork.z - 0.5;
    int2 b = int2(floor(w)); float2 f = w - b;
    return lerp(lerp(loadWork(b), loadWork(b + int2(1, 0)), f.x), lerp(loadWork(b + int2(0, 1)), loadWork(b + int2(1, 1)), f.x), f.y);
}
// The layer's history motion and depth (motionPS, work size, x y: motion in
// work pixels, z: view depth), both of the nearest depth of the 3x3 around
// the texel (dilated motion, as temporal AA takes it): blurred silhouettes
// spread their layer one texel over the sky and far ground around them.
float4 dofNearestMotion(int2 b) {
    int2 centre = int2((b + 0.5)*dofWork.z), nearestAt = centre;
    float nearest = 2;
    [unroll] for (int k = 0; k < 9; ++k) {
        int2 q = centre + int2(k % 3 - 1, k / 3 - 1)*int(dofWork.z);
        float d = loadDepth(q);
        if (d < nearest) { nearest = d; nearestAt = q; }
    }
    float depth = dofSky(nearest) ? 65000 : min(viewDepth(nearest, (nearestAt + 0.5)*dofFull.zw), 65000);
    return float4(dofMotion(nearestAt)/dofWork.z, depth, 0);
}
float4 motionPS(V i) : SV_Target { return dofNearestMotion(int2(i.position.xy)); }
// Disocclusion: whether the history at work position previous (last frame's
// layer) held a surface nearer than anything around work texel b now. A far
// tree crown moves slower than the sky behind it whenever the camera
// translates (pans, and turns around the camera's anchor), so the sky it
// uncovers fetches last frame's crown blur along the sky's motion; the 3x3
// colour clamp keeps it, since every neighbourhood of a holey crown spans
// both crown and sky (user, cand-1111: horizon crowns smeared mid-pan;
// D3D12 save1151 mid-yaw, the layer without its clamp trailed whole crowns
// across the sky; debug view 6 marks the texels this rejects). Only while
// the texel moves half a work pixel or more: at rest the jitter moves a
// silhouette's nearest depth by a texel, which the history must smooth.
bool dofDisoccluded(int2 b, float2 previous) {
    int2 last = int2(dofWork.xy) - 1;
    float4 now = layerMotionTex.Load(int3(b, 0));
    if (dot(now.xy, now.xy) < 0.25) return false;
    int2 at = int2(floor(previous - 0.5));
    float before = 65000;
    [unroll] for (int k = 0; k < 4; ++k)
        before = min(before, layerMotionLast.Load(int3(clamp(at + int2(k & 1, k >> 1), 0, last), 0)).z);
    return before < 0.8*now.z;
}
// The fill result as the premultiplied layer, blended with last frame's.
float4 stabilizePS(V i) : SV_Target {
    int2 p = int2(i.position.xy);
    float4 current = dofLayer(loadWork(p));
    if (dofWork.w >= 1) return current;
    float4 lo = current, hi = current;
    [unroll] for (int n = 0; n < 9; ++n) {
        if (n == 4) continue;
        float4 s = dofLayer(loadWork(p + int2(n % 3 - 1, n / 3 - 1)));
        lo = min(lo, s); hi = max(hi, s);
    }
    float2 previous = i.position.xy + layerMotionTex.Load(int3(p, 0)).xy;
    if (any(previous < 0) || any(previous > dofWork.xy)) return current;
    if (dofDisoccluded(p, previous)) return current;
    float4 history = clamp(historyTex.SampleLevel(linearClamp, previous/dofWork.xy, 0), lo, hi);
    return lerp(history, current, dofWork.w);
}
// The full-size blend weight of the blur layer (source: the stable layer).
float dofRawAlpha(float depth, float2 uv, float focus, float cover) {
    return dofSky(depth) ? cover : smoothstep(0.25, 1.5, dofCoc(depth, uv, focus));
}
float4 alphaPS(V i) : SV_Target {
    float focus = focusTex.Load(int3(0, 0, 0));
    int2 p = int2(i.position.xy);
    float2 uv = i.position.xy*dofFull.zw;
    float cover = dofWorkLayer(i.position.xy).a;
    // Nearest of the 3x3 keeps in-focus silhouettes sharp to the pixel; the
    // single pixels bound the history.
    float nearest = 1, lo = 1, hi = 0; int2 nearestAt = p;
    [unroll] for (int n = 0; n < 9; ++n) {
        float d = loadDepth(p + int2(n % 3 - 1, n / 3 - 1));
        if (d < nearest) nearestAt = p + int2(n % 3 - 1, n / 3 - 1);
        nearest = min(nearest, d);
        float a = dofRawAlpha(d, uv, focus, cover);
        lo = min(lo, a); hi = max(hi, a);
    }
    float current = dofRawAlpha(nearest, uv, focus, cover);
    lo = min(lo, current); hi = max(hi, current);
    if (dofWork.w >= 1) return current;
    float2 previous = i.position.xy + dofMotion(nearestAt);
    if (any(previous < 0) || any(previous > dofFull.xy)) return current;
    // The layer's disocclusion test at this pixel's work texel.
    int2 b = clamp(int2(floor(i.position.xy/dofWork.z)), 0, int2(dofWork.xy) - 1);
    if (dofDisoccluded(b, previous/dofWork.z)) return current;
    float history = clamp(historyTex.SampleLevel(linearClamp, previous*dofFull.zw, 0).r, lo, hi);
    return lerp(history, current, dofWork.w);
}
)" R"(
// Debug view 3: a copy of this frame's prefiltered colour (work size).
float4 probePS(V i) : SV_Target { return loadWork(int2(i.position.xy)); }
#ifdef EID3D12
// ---- Debug views 2-5 (depth_of_field_debug_view, D3D12 after the upscaler):
// each marks per pixel whether one stage is right, over the image (alpha
// blended; unmarked pixels keep the picture).
cbuffer DepthOfFieldDebug : register(b1) {
    row_major float4x4 dbgClipToWorld;   // this frame, unjittered (the upscaler motion's camera)
    row_major float4x4 dbgPreviousClip;  // last frame's world to clip (the upscaler motion's)
    float4 dbgFlags;                     // x: the camera moved this frame
};
// The markers of the last frame whose camera moved (debugPS): a harness
// screenshot follows the frame after a camera step, which does not move.
Texture2D latchTex : register(t10);
Texture2D rawTex : register(t6);     // this frame's fill result, before the temporal blend
Texture2D probeNow : register(t7);   // this frame's prefiltered colour (work size)
Texture2D probeLast : register(t8);  // last frame's
// Work texel b's history motion as stabilizePS takes it (work pixels), and
// its own (undilated) motion.
float2 dofHistoryMotion(int2 b, out float2 own) {
    own = dofMotion(int2((b + 0.5)*dofWork.z))/dofWork.z;
    return dofNearestMotion(b).xy;
}
float dofColourError(float3 a, float3 b) { return length(a - b)/(length(a) + length(b) + 0.05); }
float4 dofDebugView(int2 p, float focus) {
    int mode = int(dofLens.z + 0.5);
    float2 renderSize = dofFull.xy*dofDepthMap.z;
    if (mode == 2) {
        // Motion source: the upscaler motion against the camera reprojection
        // of the same depth texel (cameraMotionPS's camera path).
        // magenta: other motion written (object motion), yellow: zero written
        // where the camera moved it, cyan: sky (far plane), faint green: the
        // camera motion, moving.
        int2 q = clamp(int2(floor((p + 0.5)*dofDepthMap.z + dofDepthMap.xy)), 0, int2(renderSize) - 1);
        float d = depthTex.Load(int3(q, 0));
        float2 m = motionTex.Load(int3(q, 0));
        float2 pixel = q + 0.5 - dofDepthMap.xy, uv = pixel/renderSize;
        float4 world = mul(dbgClipToWorld, float4(uv.x*2 - 1, 1 - uv.y*2, d, 1));
        float4 prev = mul(dbgPreviousClip, float4(world.xyz/world.w, 1));
        float2 cam = prev.w > 1e-6 ? float2(prev.x/prev.w*0.5 + 0.5, 0.5 - prev.y/prev.w*0.5)*renderSize - pixel : 0;
        float off = length(m - cam), moved = length(cam);
        if (off > 0.25 && length(m) < 0.01 && moved > 0.25) return float4(1, 1, 0, 0.8);
        if (off > 0.25) return float4(1, 0, 1, saturate(0.4 + off/4));
        if (dofSky(d)) return float4(0, 1, 1, 0.35);
        return moved > 0.25 ? float4(0, 1, 0, 0.2) : 0;
    }
    int2 b = clamp(int2(floor(p/dofWork.z)), 0, int2(dofWork.xy) - 1);
    float2 own, motion = dofHistoryMotion(b, own);
    if (mode == 3) {
        // History alignment: last frame's prefiltered colour fetched along the
        // history motion against this frame's. red: the history motion lands
        // on another colour, orange: only the dilated (nearest-depth) motion
        // misses, the pixel's own one matches, green: moved and matched.
        float3 now = probeNow.Load(int3(b, 0)).rgb;
        float2 at = b + 0.5 + motion, atOwn = b + 0.5 + own;
        float e = dofColourError(now, probeLast.SampleLevel(linearClamp, at/dofWork.xy, 0).rgb);
        float eOwn = dofColourError(now, probeLast.SampleLevel(linearClamp, atOwn/dofWork.xy, 0).rgb);
        float still = dofColourError(now, probeLast.Load(int3(b, 0)).rgb);
        if (any(at < 0) || any(at > dofWork.xy)) return float4(1, 1, 1, 0.5);
        if (e > 0.06 && eOwn <= 0.06) return float4(1, 0.5, 0, 0.8);
        if (e > 0.06) return float4(1, 0, 0, saturate(0.4 + 4*e));
        return still > 0.06 ? float4(0, 1, 0, 0.35) : 0;
    }
    if (mode == 7) {
        // Motion scale: where the prefiltered colour has contrast and the
        // pixel's own motion moves it, the scale s of that motion at which
        // last frame's colour matches best: blue 0 (the image did not move),
        // cyan 0.5, green 1 (the motion is right), yellow 1.5, red 2,
        // magenta -1 (the opposite way).
        int2 last = int2(dofWork.xy) - 1;
        float3 now = probeNow.Load(int3(b, 0)).rgb;
        float contrast = dofColourError(now, probeNow.Load(int3(min(b + int2(1, 0), last), 0)).rgb) +
                         dofColourError(now, probeNow.Load(int3(min(b + int2(0, 1), last), 0)).rgb);
        if (contrast < 0.04 || length(own) < 0.5) return 0;
        static const float scales[6] = {0, 0.5, 1, 1.5, 2, -1};
        static const float3 colours[6] = {float3(0, 0, 1), float3(0, 1, 1), float3(0, 1, 0), float3(1, 1, 0), float3(1, 0, 0), float3(1, 0, 1)};
        float best = 1e9; int pick = 0;
        [unroll] for (int k = 0; k < 6; ++k) {
            float2 at = b + 0.5 + own*scales[k];
            float e = dofColourError(now, probeLast.SampleLevel(linearClamp, at/dofWork.xy, 0).rgb);
            if (e < best) { best = e; pick = k; }
        }
        return float4(colours[pick], 0.8);
    }
    if (mode == 8) {
        // Blend weight lag: the shown (temporal) weight against this
        // frame's own (alphaPS's current). red: blur laid where this frame is
        // sharp (a trail), blue: sharp where this frame blurs (a sharp
        // leading edge), faint yellow: an all-sky 3x3 (the pixel's history
        // took the layer's motion).
        float nearest = 1;
        [unroll] for (int n = 0; n < 9; ++n) nearest = min(nearest, loadDepth(p + int2(n % 3 - 1, n / 3 - 1)));
        float raw = dofRawAlpha(nearest, (p + 0.5)*dofFull.zw, focus, dofWorkLayer(p + 0.5).a);
        float lag = alphaTex.Load(int3(p, 0)) - raw;
        if (lag > 0.25) return float4(1, 0, 0, 0.8);
        if (lag < -0.25) return float4(0, 0.4, 1, 0.8);
        return dofSky(nearest) ? float4(1, 1, 0, 0.15) : 0;
    }
    if (mode == 6) {
        // Disocclusion: magenta where the layer drops its history (last
        // frame's layer held a nearer surface there than anything around
        // the texel now: a moving crown uncovering the sky).
        float2 m = layerMotionTex.Load(int3(b, 0)).xy;
        return length(m) > 0.25 && dofDisoccluded(b, b + 0.5 + m) ? float4(1, 0, 1, 0.8) : 0;
    }
    if (mode == 4) {
        motion = layerMotionTex.Load(int3(b, 0)).xy;
        // History in the shown layer: red = how far the stable layer is from
        // this frame's (the lag), blue: the 3x3 clamp cut the history.
        float4 current = dofLayer(rawTex.Load(int3(b, 0)));
        float4 shown = source.Load(int3(b, 0));
        float4 lo = current, hi = current;
        [unroll] for (int n = 0; n < 9; ++n) {
            float4 s = dofLayer(rawTex.Load(int3(clamp(b + int2(n % 3 - 1, n / 3 - 1), 0, int2(dofWork.xy) - 1), 0)));
            lo = min(lo, s); hi = max(hi, s);
        }
        float2 at = b + 0.5 + motion;
        float4 history = historyTex.SampleLevel(linearClamp, at/dofWork.xy, 0);
        float lag = abs(shown.a - current.a) + dofColourError(shown.rgb, current.rgb);
        float cut = length(history - clamp(history, lo, hi));
        return float4(saturate(lag*4), 0, cut > 0.02 ? 1 : 0, lag > 0.03 || cut > 0.02 ? 0.75 : 0);
    }
    // 5: CoC per pixel (red: radius, 8 work px full), green: sharp, cyan: sky.
    float d = loadDepth(p);
    if (dofSky(d)) return float4(0, 1, 1, 0.35);
    float coc = dofCoc(d, (p + 0.5)*dofFull.zw, focus);
    return coc < 0.25 ? float4(0, 1, 0, 0.3) : float4(1, 0, 0, saturate(0.3 + coc/8));
}
float4 debugPS(V i) : SV_Target {
    int2 p = int2(i.position.xy);
    return dbgFlags.x > 0.5 ? dofDebugView(p, focusTex.Load(int3(0, 0, 0))) : latchTex.Load(int3(p, 0));
}
#else
float4 debugPS(V i) : SV_Target { return 0; }
#endif
float4 compositePS(V i) : SV_Target {
    float focus = focusTex.Load(int3(0, 0, 0));
    int2 p = int2(i.position.xy);
    float2 w = i.position.xy/dofWork.z - 0.5;
    int2 b = int2(floor(w)); float2 f = w - b;
    float3 sum = 0; float weight = 0;
#ifdef EID3D12
    if (dofWork.w > 0) {  // temporal: the stable layer (source) at the stable weight
        if (dofLens.z > 1.5) return latchTex.Load(int3(p, 0));
        float a = alphaTex.Load(int3(p, 0));
        float4 layer = dofWorkLayer(i.position.xy);
        // debug 1: red = blend weight, blue = the work layer's weight
        if (dofLens.z > 0) return float4(a, 0, layer.a, 1);
        if (a < 1.0/255 || layer.a < 1e-4) discard;
        return float4(saturate(layer.rgb/layer.a), a);
    }
#endif
    // The nearest depth of the 3x3 around the pixel picks the path and the
    // radius: one jittered pixel at a silhouette no longer switches between the
    // sky and blur paths (or blur and sharp) frame to frame. Sky only where the
    // whole 3x3 is sky.
    float nearest = 1;
    [unroll] for (int n = 0; n < 9; ++n) nearest = min(nearest, loadDepth(p + int2(n % 3 - 1, n / 3 - 1)));
    if (dofSky(nearest)) {
        // Sharp sky under the terrain blur's coverage: sky texels carry their
        // coverage, blurred terrain texels cover fully, sharp ones not at all.
        [unroll] for (int k = 0; k < 4; ++k) {
            int2 o = int2(k & 1, k >> 1);
            float4 s = loadWork(b + o);
            float cover = s.a < 0 ? -1 - s.a : saturate(s.a - 0.5);
            float sw = (o.x ? f.x : 1 - f.x)*(o.y ? f.y : 1 - f.y)*cover;
            sum += s.rgb*sw; weight += sw;
        }
        if (dofLens.z > 0) return float4(0, 0, 0.25 + 0.75*saturate(weight), 1); // debug: sky path, blue = coverage
        if (weight < 1.0/255) discard;
        return float4(saturate(sum/weight), saturate(weight));
    }
    // Nearest of five: the blur zone stops one pixel short of a silhouette, so
    // the work-resolution upsample never fattens thin sharp features.
    float coc = dofCoc(nearest, i.position.xy*dofFull.zw, focus);
    // debug: red = blur radius (8 work px full), green = sharp (discarded) pixels
    if (dofLens.z > 0) return float4(saturate(coc/8), coc < 0.25 ? 1 : 0, 0, 1);
    if (coc < 0.25) discard;
    [unroll] for (int k = 0; k < 4; ++k) {
        int2 o = int2(k & 1, k >> 1);
        float4 s = loadWork(b + o);
        float bilinear = (o.x ? f.x : 1 - f.x)*(o.y ? f.y : 1 - f.y);
        float sw = bilinear*(saturate(s.a - 0.25) + 1e-3);
        sum += s.rgb*sw; weight += sw;
    }
    return float4(saturate(sum/weight), smoothstep(0.25, 1.5, coc));
}
)";
}

class DepthOfFieldPass {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
public: // shared with the D3D12 backend's depth of field
    static constexpr float kFocusSeconds = .3f;  // focus time constant (real time)
private:
    Ptr<ID3D11Texture2D> work_[2], focus_[2];
    Ptr<ID3D11RenderTargetView> work_rtv_[2], focus_rtv_[2];
    Ptr<ID3D11ShaderResourceView> work_srv_[2], focus_srv_[2];
    Ptr<ID3D11VertexShader> vs_;
    Ptr<ID3D11PixelShader> focus_ps_, prefilter_ps_, gather_ps_, fill_ps_, composite_ps_;
    Ptr<ID3D11Buffer> constants_;
    Ptr<ID3D11SamplerState> linear_;
    Ptr<ID3D11BlendState> over_;
    UINT width_ = 0, height_ = 0, work_width_ = 0, work_height_ = 0, divisor_ = 0;
    int quality_ = 0;
    UINT current_focus_ = 0;
    bool shaders_failed_ = false, history_valid_ = false;
    double last_seconds_ = 0;

    HRESULT create_shaders(ID3D11Device* device, int quality) {
        // The quality's six programs from the embedded table (prepared with
        // the other groups), never FXC on the render thread.
        std::vector<ShaderProgram> programs;
        shader_programs(programs);
        const ShaderProgram* own = programs.data() + (quality >= 2 ? 6 : 0);
        auto compile = [&](const ShaderProgram& program, Ptr<ID3DBlob>& code) {
            Ptr<ID3DBlob> error;
            const HRESULT hr = compile_shader(program, &code, &error);
            if (FAILED(hr) && error) OutputDebugStringA(static_cast<const char*>(error->GetBufferPointer()));
            return hr;
        };
        Ptr<ID3DBlob> code; HRESULT hr;
        if (FAILED(hr = compile(own[0], code))) return hr;
        if (FAILED(hr = create_shader(device,code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs_))) return hr;
        Ptr<ID3D11PixelShader>* shaders[] = {&focus_ps_, &prefilter_ps_, &gather_ps_, &fill_ps_, &composite_ps_};
        for (unsigned n = 0; n < 5; ++n) {
            code.Reset();
            if (FAILED(hr = compile(own[1 + n], code))) return hr;
            if (FAILED(hr = create_shader(device,code->GetBufferPointer(), code->GetBufferSize(), nullptr,
                                                      shaders[n]->ReleaseAndGetAddressOf()))) return hr;
        }
        quality_ = quality;
        if (constants_) return S_OK;
        D3D11_BUFFER_DESC b{}; b.ByteWidth = 80; b.Usage = D3D11_USAGE_DYNAMIC;
        b.BindFlags = D3D11_BIND_CONSTANT_BUFFER; b.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(hr = device->CreateBuffer(&b, nullptr, &constants_))) return hr;
        D3D11_SAMPLER_DESC s{}; s.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; s.MaxLOD = 0;
        if (FAILED(hr = device->CreateSamplerState(&s, &linear_))) return hr;
        // lerp(sharp, blur, alpha): the resolved world image is never read.
        D3D11_BLEND_DESC blend{}; auto& target = blend.RenderTarget[0];
        target.BlendEnable = TRUE; target.SrcBlend = D3D11_BLEND_SRC_ALPHA; target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        target.BlendOp = D3D11_BLEND_OP_ADD; target.SrcBlendAlpha = D3D11_BLEND_ZERO;
        target.DestBlendAlpha = D3D11_BLEND_ONE; target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED|D3D11_COLOR_WRITE_ENABLE_GREEN|D3D11_COLOR_WRITE_ENABLE_BLUE;
        return device->CreateBlendState(&blend, &over_);
    }

    HRESULT create_targets(ID3D11Device* device, UINT width, UINT height) {
        for (UINT n = 0; n < 2; ++n) {
            work_[n].Reset(); work_rtv_[n].Reset(); work_srv_[n].Reset();
            focus_[n].Reset(); focus_rtv_[n].Reset(); focus_srv_[n].Reset();
        }
        history_valid_ = false;
        // A failed size is not retried until it changes.
        width_ = width; height_ = height;
        divisor_ = height > 1440 ? 3u : 2u;
        work_width_ = std::max(1u, (width + divisor_ - 1)/divisor_);
        work_height_ = std::max(1u, (height + divisor_ - 1)/divisor_);
        auto target = [&](UINT w, UINT h, DXGI_FORMAT format, Ptr<ID3D11Texture2D>& texture,
                          Ptr<ID3D11RenderTargetView>& rtv, Ptr<ID3D11ShaderResourceView>& srv) {
            D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h;
            d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1; d.Format = format;
            d.BindFlags = D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = device->CreateTexture2D(&d, nullptr, &texture);
            if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(texture.Get(), nullptr, &rtv);
            if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(texture.Get(), nullptr, &srv);
            return hr;
        };
        for (UINT n = 0; n < 2; ++n) {
            HRESULT hr = target(work_width_, work_height_, DXGI_FORMAT_R16G16B16A16_FLOAT, work_[n], work_rtv_[n], work_srv_[n]);
            if (SUCCEEDED(hr)) hr = target(1, 1, DXGI_FORMAT_R32_FLOAT, focus_[n], focus_rtv_[n], focus_srv_[n]);
            if (FAILED(hr)) {
                OutputDebugStringA("EID3D11 depth of field targets unavailable; depth of field omitted\n");
                work_[0].Reset(); work_srv_[0].Reset(); work_rtv_[0].Reset();
                return hr;
            }
        }
        return S_OK;
    }

public:
    std::uint64_t storage_bytes() const {
        return work_[0] ? std::uint64_t(work_width_)*work_height_*16u + 8u : 0;
    }
    UINT work_width() const { return work_width_; }
    UINT work_height() const { return work_height_; }
    void reset() { *this = DepthOfFieldPass{}; }

    // Blends the far-field blur of `scene` (the unclamped world colour) over
    // `output` (the resolved world image, width x height). `depth` is the
    // world's hardware depth, `world_to_clip` the camera that produced it.
    // `seconds` is a monotonic real-time clock for focus smoothing. Returns
    // Quality 1 (16 taps), then quality 2 (40 taps): dofVS, then the five
    // pixel stages in create_shaders order. Quality 0 never draws.
    static void shader_programs(std::vector<ShaderProgram>& out) {
        static const std::string sources[2] = {depth_of_field_shader_source(1), depth_of_field_shader_source(2)};
        for (const std::string& source : sources) {
            out.push_back({source, "DepthOfField", nullptr, "dofVS", "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
            for (const char* entry : {"focusPS", "prefilterPS", "gatherPS", "fillPS", "compositePS"})
                out.push_back({source, "DepthOfField", nullptr, entry, "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
        }
        // [12], [13]: the temporal layer and weight (D3D12 after the upscaler; no taps).
        // [14]: debug view 3's copy of the prefiltered colour.
        // [15]: debug views 2-8 into the latch (D3D12); [16] the layer's motion and depth.
        for (const char* entry : {"stabilizePS", "alphaPS", "probePS", "debugPS", "motionPS"})
            out.push_back({sources[1], "DepthOfField", nullptr, entry, "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3});
    }
    // S_FALSE without drawing when the lens is inactive (top-down camera,
    // strength 0); a failure is logged once where it occurs and leaves
    // `output` untouched.
    HRESULT render(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* scene,
                   ID3D11ShaderResourceView* depth, ID3D11RenderTargetView* output, UINT width, UINT height,
                   int quality, float strength, const float clip_to_world[16], double seconds, UINT* draws = nullptr) {
        if (draws) *draws = 0;
        quality = std::clamp(quality, 0, 2);
        const float work_height = float(height > 1440 ? (height + 2)/3 : (height + 1)/2);
        const DepthOfFieldLens lens = quality > 0 ? depth_of_field_lens(clip_to_world, strength, work_height) : DepthOfFieldLens{};
        // Inactive frames keep no history: the focus snaps when blur returns,
        // where the blur itself is still ramping in from zero.
        if (!lens.active) { history_valid_ = false; return S_FALSE; }
        if (shaders_failed_) return E_FAIL;
        HRESULT hr;
        if (quality != quality_ && FAILED(hr = create_shaders(device, quality))) {
            OutputDebugStringA("EID3D11 depth of field shaders unavailable; depth of field omitted\n");
            shaders_failed_ = true; return hr;
        }
        if ((width != width_ || height != height_) && FAILED(hr = create_targets(device, width, height))) return hr;
        if (!work_[0]) return E_FAIL;

        const double elapsed = seconds - last_seconds_;
        const float blend = history_valid_ && elapsed >= 0 ?
            float(1.0 - std::exp(-std::min(elapsed, 10.0)/kFocusSeconds)) : 1.f;
        last_seconds_ = seconds;
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(hr = context->Map(constants_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) return hr;
        const float values[20] = {clip_to_world[12], clip_to_world[13], clip_to_world[14], clip_to_world[15],
                                  lens.aperture, lens.margin, 0.f, blend,
                                  float(width), float(height), 1.f/width, 1.f/height,
                                  float(work_width_), float(work_height_), float(divisor_), 0.f,
                                  0.f, 0.f, 1.f, 0.f};
        std::copy(values, values + 20, static_cast<float*>(map.pData));
        context->Unmap(constants_.Get(), 0);

        ID3D11ShaderResourceView* none[3]{};
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs_.Get(), nullptr, 0); context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0); context->DSSetShader(nullptr, nullptr, 0);
        context->RSSetState(nullptr); context->OMSetDepthStencilState(nullptr, 0);
        context->PSSetConstantBuffers(0, 1, constants_.GetAddressOf());
        context->PSSetSamplers(0, 1, linear_.GetAddressOf());
        UINT issued = 0;
        auto pass = [&](ID3D11RenderTargetView* target, UINT w, UINT h, ID3D11PixelShader* shader,
                        ID3D11ShaderResourceView* input, ID3D11ShaderResourceView* focus, bool over) {
            context->PSSetShaderResources(0, 3, none);
            context->OMSetRenderTargets(1, &target, nullptr);
            const D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1};
            context->RSSetViewports(1, &viewport);
            context->OMSetBlendState(over ? over_.Get() : nullptr, nullptr, ~0u);
            ID3D11ShaderResourceView* views[3] = {depth, input, focus};
            context->PSSetShaderResources(0, 3, views);
            context->PSSetShader(shader, nullptr, 0); context->Draw(3, 0); ++issued;
        };
        const UINT next = current_focus_ ^ 1u;
        pass(focus_rtv_[next].Get(), 1, 1, focus_ps_.Get(), nullptr, focus_srv_[current_focus_].Get(), false);
        current_focus_ = next;
        ID3D11ShaderResourceView* focus = focus_srv_[current_focus_].Get();
        pass(work_rtv_[0].Get(), work_width_, work_height_, prefilter_ps_.Get(), scene, focus, false);
        pass(work_rtv_[1].Get(), work_width_, work_height_, gather_ps_.Get(), work_srv_[0].Get(), focus, false);
        pass(work_rtv_[0].Get(), work_width_, work_height_, fill_ps_.Get(), work_srv_[1].Get(), focus, false);
        pass(output, width, height, composite_ps_.Get(), work_srv_[0].Get(), focus, true);
        context->PSSetShaderResources(0, 3, none);
        context->OMSetBlendState(nullptr, nullptr, ~0u);
        history_valid_ = true;
        if (draws) *draws = issued;
        return S_OK;
    }
};

} // namespace owned
