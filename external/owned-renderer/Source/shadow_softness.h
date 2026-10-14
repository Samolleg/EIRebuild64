#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace owned {
// Percentage-closer soft shadows (PCSS): a blocker search estimates the
// caster-to-receiver gap and the filter radius follows the penumbra of an area
// light, so contact shadows stay crisp and distant ones soften. The shader
// mirrors these helpers; they also build its constants.
struct ShadowSoftnessPolicy {
    // Largest caster-receiver gap, in world units, the sun blocker search is
    // sized for. Taller gaps clamp at the search radius (visual tuning).
    static constexpr float sun_search_gap_world=32.f;
    static constexpr float sun_search_min_texels=2.f, sun_search_max_texels=24.f;
    // Point cubes: wide sparse filters draw several offset copies of thin
    // casters, so the penumbra stays within a few cube texels.
    static constexpr float point_min_texels=1.5f;
    static constexpr float point_max_texels_low=3.f, point_max_texels_high=4.f;
};

// Constants for shadowBias[5] of the directional constant buffer:
// x = tan(half sun angle), y = sun search radius (world), z = quality
// (0 keeps the tapered PCF, 1 low, 2 high), w = point source-size scale.
inline std::array<float,4> shadow_softness_constants(int quality,int sun_angle_tenths_degree,
    int point_size_percent) noexcept {
    quality=std::clamp(quality,0,2);
    if(!quality)return {};
    const float half_angle=float(std::clamp(sun_angle_tenths_degree,1,60))*.05f*3.14159265f/180.f;
    const float tangent=std::tan(half_angle);
    return {tangent,tangent*ShadowSoftnessPolicy::sun_search_gap_world,float(quality),
        float(std::clamp(point_size_percent,10,400))*.01f};
}

// Bilinear percentage-closer lookup of an orthographic sun map against a planar
// receiver, shared by the production world/grass shaders and the WARP
// regression. `at` and `origin` are in texel coordinates, `receiver` is the
// biased receiver depth at `origin`, `depthStep` its depth change per texel.
// Hardware comparison filtering tests all four texels against the one depth
// at `at`; on a receiver steeper than about half a texel of depth per texel
// (any face turned more than ~30 degrees from the sun) the texels on the sun
// side then shadow their own surface, and the rotated soft-shadow taps turn
// that into noise that changes every frame. Here each texel is compared with
// the receiver plane at its own centre. Gather only reads raw depth, so any
// clamp sampler serves; texels outside the map and references beyond the far
// plane compare as lit, as the border comparison sampler did.
constexpr char kShadowPlaneCompareShader[] = R"(
float shadowPlaneCompare(Texture2DArray<float> map,SamplerState clampSampler,uint slice,float side,
                         float2 at,float2 origin,float receiver,float2 depthStep){
 float2 base=floor(at-.5),f=at-.5-base;
 // Gather order: x=(0,1) y=(1,1) z=(1,0) w=(0,0) texels from base.
 float4 stored=map.GatherRed(clampSampler,float3((base+1)/side,slice));
 float4 tx=base.x+float4(0,1,1,0),ty=base.y+float4(1,1,0,0);
 stored=min(min(tx,ty),side-1-max(tx,ty))<0?1:stored;
 float2 corner=base+.5-origin;
 float4 reference=saturate(receiver+float4(dot(depthStep,corner+float2(0,1)),dot(depthStep,corner+1),
                                            dot(depthStep,corner+float2(1,0)),dot(depthStep,corner)));
 float4 lit=reference<=stored?1:0;
 return lerp(lerp(lit.w,lit.z,f.x),lerp(lit.x,lit.y,f.x),f.y);
}
)";

// Sun: map depths are linear in world depth (orthographic), so the gap is the
// depth difference times the map's world depth span. Returns the filter radius
// in this map's texels; the world radius is the same in every cascade until a
// clamp applies, which keeps cascade blend bands at one softness.
inline float pcss_sun_filter_texels(float receiver_depth,float blocker_depth,float depth_span_world,
    float tan_half_angle,float texel_world,float min_texels,float search_texels) noexcept {
    const float gap=std::max(receiver_depth-blocker_depth,0.f)*depth_span_world;
    return std::clamp(gap*tan_half_angle/std::max(texel_world,1e-6f),min_texels,
        std::max(min_texels,search_texels));
}

// Point light of source radius `source`: similar triangles give the penumbra
// width at the receiver, (receiver - blocker) / blocker * source. Distances are
// along the cube face axis. Returns a world radius at the receiver.
inline float pcss_point_filter_world(float receiver_distance,float blocker_distance,float source,
    float min_world,float max_world) noexcept {
    const float penumbra=source*std::max(receiver_distance-blocker_distance,0.f)/
        std::max(blocker_distance,1e-4f);
    return std::clamp(penumbra,min_world,std::max(min_world,max_world));
}
} // namespace owned
