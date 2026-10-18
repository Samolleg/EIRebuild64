#pragma once

// World fog. The game keeps authority over the hourly fog colour and the
// map-border fog. The stock linear distance fog (start 90 day / 50 night, end
// at the 100 m far clip) existed to hide the original draw distance; it is a
// reference for the "faithful" preset only. The default look is a modern
// atmosphere with natural distances driven by time of day, weather and
// terrain (see the presets below).
//
// Stock distance fog (E-150; supported 3DfpFPU.dll, SHA-256 a7b11bf5...fc82):
// the TL pipeline stores SetFog(start,end) at +0x38/+0x3C and rebuilds planar
// view depth from screen z with the projection terms _33/_34/_43 (+0x6C/+0x70/
// +0x7C) before lowering the specular-alpha fog byte linearly. We read those
// values; planar depth reconstruction is the same formula. The f298f790 build
// moves these fields back four bytes; tl_module_contract.h selects its layout.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace owned {

struct StockDistanceFog {
    float start = 0.0f, end = 0.0f;
    float scale = 0.0f;  // the pipeline's own 255/(end-start)
    float projection_33 = 0.0f, projection_34 = 0.0f, projection_43 = 0.0f;
};

// Planar view depth exactly as the TL pass rebuilds it from screen z.
inline float stock_fog_view_depth(float screen_z, const StockDistanceFog& fog) noexcept {
    return fog.projection_43 / (fog.projection_34 * screen_z - fog.projection_33);
}

// ---------------------------------------------------------------------------
// Presets (all keep the map-border fog and the game's fog colour):
//   0 faithful   : the stock distances with a smooth S-curve instead of the
//                  hard linear fade;
//   1 atmospheric: natural aerial perspective (configurable visibility,
//                  modulated by hour and weather), height fog over the map's
//                  low ground and a soft glow around the sun;
//   2 full       : preset 1 as a froxel volume, adding swamp mist, sun
//                  scattering through the owned cascade maps, shadowed lamp
//                  scattering and temporal reprojection.
// Every native world pixel (opaque, water, ordered transparents) applies the
// same function: colour*T + bounded fog lighting*(1-T). Our world is forward
// shaded in game order, so there is no "after opaque, before transparents"
// point for a fullscreen pass; forward application gives transparents the
// identical result.

// Game environment of the current world frame (CWorldClient 0x7AF5B8,
// E-150). Density inputs for the atmosphere.
struct WorldEnvironment {
    bool valid = false;
    double game_hours = 0.0;  // ticks*scale + base, unwrapped
    float hour = 0.0f;        // fmod(game_hours, 24)
    bool cave = false;        // CWorldClient+0x6E0 == 1 (label from the addon: zone sky type)
    int weather_current = 0, weather_next = 0;  // 1 rain; 2 snow per the addon enum
    float weather_progress = 1.0f;  // (ticks - start) / length of the pending change
    float rain_particles = 0.0f; // immediate rain particle density, before renderer smoothing
    float snow_particles = 0.0f; // the same for snow (DrawSnow, 004E4C80)
    float weather_start = 0.0f;  // CWeather +0xC0: start tick of the latest change (seeds a shower)
    float precipitation_strength = 1.0f; // visual strength of the rain/snow (precipitation.h), 0.2..1
    double world_time = 0.0;  // tick + fraction: world animation time, pauses with the game
    int region = 0;           // fog_region(): from the region name that selects Lights<region>.ini
};

// Supported game: 004E40B0 updates the 64-step particle power; 004E5680
// selects the current kind, and 004E449A tests that power for rain emission.
// Evaluate from transition state because the particle draw updates its stored
// power later than the world snapshot. Progress > 1 also represents no transition.
// kind: 1 rain (DrawRain), 2 snow (DrawSnow); both gate on the same power.
inline float weather_particle_power(int current, int next, float progress, int kind) noexcept {
    if (current == 0) return 0.0f;
    if (progress > 1.0f) return next == kind ? 1.0f : 0.0f;
    if (current != kind) return 0.0f;
    const double wave = std::cos(static_cast<double>(progress) * 3.14159);
    const float density = static_cast<float>(0.5 * (1.0 + (next == 0 ? wave : -wave)));
    return std::nearbyint(density * 64.0f) / 64.0f;
}
inline float weather_particle_rain(int current, int next, float progress) noexcept {
    return weather_particle_power(current, next, progress, 1);
}

// CWorldClient+0x184 holds the region name the game uses to build
// "Lights[Cave]<region>.ini" (00568045-005680E6). Shipped data: Gipat, Ingos
// and Suslanger tables; Suslanger's file is byte-identical to Gipat's.
enum FogRegion : int { fog_region_unknown = 0, fog_region_gipat, fog_region_ingos, fog_region_suslanger };
inline int fog_region(const char* name) noexcept {
    auto same = [](const char* a, const char* b) {
        for (; *a && *b; ++a, ++b) {
            char x = *a >= 'A' && *a <= 'Z' ? char(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? char(*b + 32) : *b;
            if (x != y) return false;
        }
        return *a == 0 && *b == 0;
    };
    if (!name) return fog_region_unknown;
    if (same(name, "gipat")) return fog_region_gipat;
    if (same(name, "ingos")) return fog_region_ingos;
    if (same(name, "suslanger")) return fog_region_suslanger;
    return fog_region_unknown;
}

// Region haze character (renderer look, luminance-neutral multipliers):
// Gipat humid neutral-green, Ingos cold clear blue-white, Suslanger warm dusty
// ochre; caves a neutral grey.
inline void fog_region_tint(int region, bool cave, float out[3]) noexcept {
    float r = 1.f, g = 1.f, b = 1.f;
    if (cave) { r = g = b = 1.f; }
    else if (region == fog_region_gipat) { r = .96f; g = 1.03f; b = .93f; }
    else if (region == fog_region_ingos) { r = .92f; g = .99f; b = 1.12f; }
    else if (region == fog_region_suslanger) { r = 1.16f; g = 1.f; b = .74f; }
    const float luma = .2126f * r + .7152f * g + .0722f * b;
    out[0] = r / luma; out[1] = g / luma; out[2] = b / luma;
}

// The authored fog supplies the hue; the current world light also supplies
// energy, bounded by the non-emissive lit-white surface under that light.
// Shared with the CPU sky horizon; fogHazeColor below is the shader equivalent.
inline void fog_lit_haze_color(const float game[3], const float tint[4], const float light[4],
                               float out[3]) noexcept {
    const float luma = .2126f * game[0] + .7152f * game[1] + .0722f * game[2];
    const float light_luma = .2126f * light[0] + .7152f * light[1] + .0722f * light[2];
    const float brightness = (std::max)(luma, light_luma);
    for (int k = 0; k < 3; ++k) {
        const float hue = luma > 1e-4f ? game[k] / luma : 1.f;
        const float light_hue = light_luma > 1e-4f ? light[k] / light_luma : 1.f;
        const float c = 1.f + (hue - 1.f) * tint[3];
        out[k] = std::clamp((c + (light_hue - c) * light[3]) * tint[k] * brightness,
                            0.f, std::clamp(light[k], 0.f, 1.f));
    }
}

inline float fog_smoothstep(float a, float b, float x) noexcept {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Precipitation weight of the current weather blend. Rain counts fully, snow
// half; the weather change is blended by its own transition progress.
inline float fog_precipitation(const WorldEnvironment& e) noexcept {
    auto amount = [](int type) { return type == 1 ? 1.0f : type == 2 ? 0.5f : 0.0f; };
    const float p = std::clamp(e.weather_progress, 0.0f, 1.0f);
    // Light showers haze less than heavy ones (visual strength only).
    return (amount(e.weather_current) + (amount(e.weather_next) - amount(e.weather_current)) * p) *
           (0.4f + 0.6f * e.precipitation_strength);
}

// Night weight with the stock fog-start hours: day 6-18, night 22-2.
inline float fog_night(float hour) noexcept {
    if (hour >= 6.0f && hour <= 18.0f) return 0.0f;
    if (hour > 18.0f && hour < 22.0f) return (hour - 18.0f) / 4.0f;
    if (hour > 2.0f && hour < 6.0f) return 1.0f - (hour - 2.0f) / 4.0f;
    return 1.0f;
}

// Morning ground fog: a deterministic per-game-day draw with the configured
// chance, rising after 3:00 and burning off by 10:00.
inline float fog_morning(double game_hours, float chance) noexcept {
    if (!(game_hours >= 0.0) || chance <= 0.0f) return 0.0f;
    const auto day = static_cast<std::uint32_t>(static_cast<std::int64_t>(game_hours / 24.0));
    std::uint32_t h = day * 0x9e3779b1u;
    h ^= h >> 15; h *= 0x85ebca77u; h ^= h >> 13; h *= 0xc2b2ae3du; h ^= h >> 16;
    if (static_cast<float>(h >> 8) / 16777216.0f >= chance) return 0.0f;
    const float hour = static_cast<float>(game_hours - 24.0 * std::floor(game_hours / 24.0));
    return fog_smoothstep(3.0f, 5.0f, hour) * (1.0f - fog_smoothstep(7.5f, 10.0f, hour));
}

// Ground wetness follows precipitation up within half an hour and lingers
// after it stops. Time jumps (loads, map changes) do not advance it.
// Rain fog builds up over a quarter of a game hour after the weather turns and
// eases off the same way. Game time only: a paused game does not advance it,
// fast speed advances it faster, and load/map jumps snap to the target.
inline float fog_ramp_precipitation(float level, float target, double elapsed_hours,
                                    float ramp_hours = 0.25f) noexcept {
    if (!(elapsed_hours >= 0.0) || elapsed_hours > 6.0) return target;
    const float step = static_cast<float>(elapsed_hours) / ramp_hours;
    return level + std::clamp(target - level, -step, step);
}

// Ground wetness follows the ramped rain up and decays exponentially after it
// (about 5% left after linger_hours).
inline float fog_update_wetness(float wetness, float rain_level, double elapsed_hours,
                                float linger_hours) noexcept {
    if (!(elapsed_hours >= 0.0) || elapsed_hours > 6.0) return (std::max)(wetness, rain_level);
    if (rain_level >= wetness) return rain_level;
    const float decay = std::exp(-3.0f * static_cast<float>(elapsed_hours) / (std::max)(0.25f, linger_hours));
    return rain_level + (wetness - rain_level) * decay;
}

struct FogDensity {
    float height = 0.0f;  // extinction per metre at the map's low ground
    float mist = 0.0f;    // swamp mist extinction per metre at the water surface
};

inline FogDensity fog_density(float base, float morning, float wetness, float precipitation,
                              float night) noexcept {
    FogDensity d;
    d.height = base * (1.0f + 3.0f * morning + 1.5f * wetness + precipitation);
    d.mist = 0.15f * (0.4f + 0.6f * night + morning + wetness);
    return d;
}

// Region climate policy, not a per-tile ice flag: Ingos lakes do not supply
// the open-water evaporation layer. Air/ground fog and swamp coverage remain.
inline float fog_open_water_density(int region, float morning, float night, float wetness) noexcept {
    return region == fog_region_ingos ? 0.f : .05f * (morning + .5f * night + .5f * wetness);
}

// Match the owned surface's non-emissive lighting, which takes max, not sum.
inline void fog_lit_white(const float ambient[4], const float sun[4], float out[3]) noexcept {
    for (int k = 0; k < 3; ++k) out[k] = std::clamp((std::max)(ambient[k], sun[k]), 0.f, 1.f);
}

// Aerial-perspective extinction per metre: 3/visibility at a clear noon
// (5% contrast), hazier towards dusk and night, after rain and in overcast
// or precipitation, and with morning fog.
inline float fog_visibility_extinction(float visibility) noexcept {
    return 3.0f / (std::max)(visibility, 10.0f);
}

inline float fog_air_density(float visibility, float hour, float night, float morning, float wetness,
                             float precipitation) noexcept {
    const float haze = 1.0f + 0.6f * night + 0.3f * fog_smoothstep(0.0f, 6.0f, std::fabs(hour - 13.0f)) +
                       2.0f * morning + 0.7f * wetness + 1.5f * precipitation;
    return fog_visibility_extinction(visibility) * haze;
}

// Draw distance (draw_distance, metres): world pixels fade into the border fog
// colour (the sky with the owned sky) over the last tenth of the eye distance.
// The camera-pass cull removes boxes whose least planar depth exceeds it, so a
// culled box is always wholly faded.
constexpr float kDrawDistanceFadeStart = 0.9f;

// Where the fog alone hides everything, or 0. Presets 1-2: the air haze has
// reached optical depth 6.5 (under 0.2% of the surface left; full cover at
// 1/255 is 5.5) along any ray, since height fog, mist and the volume only add
// to it (wisps keep at least 30% of the height fog). Ground-fog layers are not
// counted: a ray from the raised camera leaves them. Preset 0: 5% past the
// stock fog end, where the faithful S-curve is complete on planar depth.
inline float fog_full_cover_distance(int preset, bool stock_valid, float stock_end, float air,
                                     float haze_free) noexcept {
    if (preset <= 0) return stock_valid && stock_end > 0.0f ? stock_end * 1.05f : 0.0f;
    return air > 0.0f ? haze_free + 6.5f / air : 0.0f;
}

// Fade distance (fogStock.w): the camera-pass cull takes the nearer of the draw
// distance and the fog cover distance, so the fade must end at that same
// distance. Fully fogged surfaces are haze, not the sky behind the cull.
inline float fog_draw_fade_distance(float draw_distance, float fog_cover) noexcept {
    return fog_cover > 0.0f && fog_cover < draw_distance ? fog_cover : draw_distance;
}

// Froxel grid for preset 2: 0 low, 1 high.
struct FogVolumeGrid { unsigned width = 0, height = 0, slices = 0; };
constexpr FogVolumeGrid fog_volume_grid(int quality) noexcept {
    return quality <= 0 ? FogVolumeGrid{80, 45, 48} : FogVolumeGrid{160, 90, 96};
}

// Map-space swamp mist source. Swamp is the liquid tile's own type in the
// map's tile-type table (terrain_swamp_tile), the liquid shader's class too.
struct WorldMistGrid {
    std::uint64_t generation = 0;
    std::uint32_t width = 0, height = 0;
    float origin[2]{};
    float cell = 4.0f;          // world metres per texel
    float terrain_low_z = 0.0f; // 10th percentile of sector heights: base of height fog
    std::vector<float> texels;  // swamp coverage, open-water coverage, surface z, 0
};

struct FogFrameGpu {
    float clip_to_world[16]{};
    float previous_world_to_clip[16]{};
    float eye[4]{};         // camera position, preset
    float height[4]{};      // density, base z, falloff metres, mist density
    float volume[4]{};      // near, far eye distance, 1/log(far/near), volume enabled
    float screen[4]{};      // 1/width, 1/height, slices, mist falloff metres
    float projection[4]{};  // _33, _34, _43 of the stock projection, valid
    float sun[4]{};         // direction toward the sun, phase g
    float sun_color[4]{};   // sun in-scatter colour, point in-scatter scale
    float mist_grid[4]{};   // origin x, origin y, 1/(width*cell), 1/(height*cell); 0 = none
    float cloud[4]{};       // cloud shadow map (sky_clouds.h): enabled, origin x, origin y, 1/extent
    float temporal[4]{};    // slice jitter, history weight, owned sky probe valid, shaft jitter frame
    float stock[4]{};       // stock start, end, air extinction per metre, draw distance (0: none)
    float previous_eye[4]{};  // previous frame camera position
    float air[4]{};         // haze-free near distance, sun glow, debug test state, debug component
    float tint[4]{};        // region haze tint (luminance 1), saturation kept from the game colour
    float light[4]{};       // non-emissive lit white, blend toward its hue
    float wisps[4]{};       // world time, wisp strength, open-water mist density, unused
    float volume_cell[4]{}; // half a froxel in uv (x, y), wisp drift offset in world units (z, w)
    float shaft[4]{};       // sun shafts (sun_shafts.h): shadow darkening share, forward boost, headroom, unused
    float shaft_haze[4]{};  // frame haze colour the shaft term is measured in, anisotropic blend
};
static_assert(sizeof(FogFrameGpu) == 432, "fog frame constants must match HLSL");

// Shared HLSL: frame constants and the forward application used by every
// native world pixel (native shader) and the froxel passes.
constexpr char kAtmosphereShader[] = R"(
cbuffer FogFrame:register(b10){row_major float4x4 fogClipToWorld;row_major float4x4 fogPreviousWorldToClip;
 float4 fogEye;float4 fogHeight;float4 fogVolumeRange;float4 fogScreen;float4 fogProjection;float4 fogSun;
 float4 fogSunColor;float4 fogMistGrid;float4 fogCloud;float4 fogTemporal;float4 fogStock;float4 fogPreviousEye;float4 fogAir;float4 fogTint;float4 fogLight;float4 fogWisps;float4 fogVolumeCell;float4 fogShaft;float4 fogShaftHaze;};
Texture3D<float4> fogIntegrated:register(t11);
SamplerState fogLinearClamp:register(s7);
// Owned sky probe (sky_clouds.h skyProbeCS): equirectangular display colour,
// valid while fogTemporal.z is set.
Texture2D<float4> skyProbe:register(t12);
Texture2D<float> skyCloudShadow:register(t10);
// Cloud shadow (sky_clouds.h): the world-space map holds the ground
// transmittance along the game's light; a receiver is projected onto it along
// the same light. The map is pre-blurred (sky_clouds.h skyShadowBlurCS: five
// taps 6 m apart keep the edges soft), so one read serves. fogCloud.x (> 0
// when enabled) is the visibility beyond the map: the deck's mean, so a closed
// deck does not leave the terrain past the map lit (runtime: a lit region that
// darkened as the camera approached it in rain).
float skyCloudVisibility(float3 p){
 [branch]if(fogCloud.x<=0)return 1;
 float2 uv=(p.xy-fogSun.xy/max(fogSun.z,.15)*p.z-fogCloud.yz)*fogCloud.w;
 float edge=max(abs(uv.x-.5),abs(uv.y-.5));
 [branch]if(edge>=.5)return fogCloud.x;
 float v=skyCloudShadow.SampleLevel(fogLinearClamp,uv,0);
 return lerp(v,fogCloud.x,smoothstep(.4,.5,edge));
}
float fogViewDepth(float screenZ){return fogProjection.z/(fogProjection.y*screenZ-fogProjection.x);}
float3 fogUnproject(float2 uv,float screenZ){float4 p=mul(fogClipToWorld,float4(uv.x*2-1,1-uv.y*2,screenZ,1));return p.xyz/p.w;}
// Froxel slices are spaced exponentially in eye distance.
float fogSliceCoordinate(float distance){return log(max(distance,fogVolumeRange.x)/fogVolumeRange.x)*fogVolumeRange.z;}
float fogAirOpticalDepth(float distance){return fogStock.z*max(distance-fogAir.x,0);}
// Optical depth of exponential height fog along eye->p.
float fogHeightOpticalDepth(float3 p){
 float h=max(fogHeight.z,.1);
 float a=clamp((fogEye.z-fogHeight.y)/h,-40,80),b=clamp((p.z-fogHeight.y)/h,-40,80);
 float mean=abs(b-a)>1e-3?(exp(-a)-exp(-b))/(b-a):exp(-a);
 return fogHeight.x*length(p-fogEye.xyz)*mean;
}
float fogPhase(float c,float g){float g2=g*g;return (1-g2)/(12.566371*pow(max(1+g2-2*g*c,1e-4),1.5));}
// Peak-normalised forward lobe: grazing the lake increases optical depth,
// not the available sunlight. The angular response stays smooth and <= 1.
float fogSunResponse(float c){
 float strength=max(fogAir.y,0);
 return (strength/(1+strength))*fogPhase(clamp(c,-1,1),fogSun.w)/fogPhase(1,fogSun.w);
}
// Match fog_lit_haze_color: retain the authored hue and world-light energy.
float3 fogHazeColor(float3 gameFog){
 float luma=dot(gameFog,float3(.2126,.7152,.0722));
 float lightLuma=dot(fogLight.rgb,float3(.2126,.7152,.0722));
 float3 hue=luma>1e-4?gameFog/luma:float3(1,1,1);
 float3 lightHue=lightLuma>1e-4?fogLight.rgb/lightLuma:float3(1,1,1);
 float3 c=lerp(float3(1,1,1),hue,fogTint.w);
 c=lerp(c,lightHue,fogLight.w);
 return min(max(c*fogTint.rgb*max(luma,lightLuma),0),saturate(fogLight.rgb));
}
// Scatter spends the remaining fog-light budget instead of stacking on haze.
// Both analytic and integrated-volume paths use this composition. Its bound
// is the scene's lit white times extinction, raised by `headroom` (a share of
// that budget) for volumetric sun shafts only. Negative integrated scatter is
// haze that sat in sun shadow: it darkens the haze before the budget applies.
float3 fogLighting(float3 haze,float transmittance,float3 scatter,float headroom){
 float3 room=saturate(fogLight.rgb)*(1-transmittance)*(1+max(headroom,0));
 float3 base=max(haze*(1-transmittance)+min(scatter,0),0);
 float3 lit=max(scatter,0);
 return base+max(room-base,0)*lit/max(room+lit,1e-6);
}
// Volumetric sun shafts (sun_shafts.h), per unit extinction in haze-colour
// units. Fully lit air in a direction without forward boost adds nothing, so
// the accepted haze stays unchanged; air in sun shadow loses the sun's share
// of the haze and lit air toward the sun gains a Henyey-Greenstein forward
// lobe (fogSun.w) blended with the isotropic haze, mean-normalised to 1.
float fogShaftWeight(float c,float visibility){
 float forward=max(fogPhase(clamp(c,-1,1),fogSun.w)*12.566371-1,0)*fogShaftHaze.w;
 return fogShaft.y*visibility*forward-fogShaft.x*(1-saturate(visibility));
}
// Shaft tap k of 6 for a froxel segment (eye distances near..far, width in
// metres), relative to the eye: stratified along the ray, spread across it.
// jitter (0..1 each) moves the taps within their strata and rotates the
// spread: a new jitter every frame, accumulated by the froxel history,
// integrates the segment instead of point-sampling shadow texels (the
// temporal acne of fixed taps while the camera moves).
float3 fogShaftTap(int k,float3 ray,float near,float far,float width,float2 jitter){
 float3 side=normalize(cross(ray,abs(ray.z)<.99?float3(0,0,1):float3(1,0,0)));
 float3 up=cross(side,ray);
 float angle=jitter.y*6.2831853,s=sin(angle),c=cos(angle);
 float2 o=float2(k&1?.35:-.35,(k%3-1)*.35);
 o=float2(o.x*c-o.y*s,o.x*s+o.y*c)*width;
 return ray*lerp(near,far,(k+jitter.x)/6)+side*o.x+up*o.y;
}
// World-domain fog factor for the game's fog-colour blend: the border fog
// byte, and in the faithful preset the stock distances as a smooth S-curve.
// Draw distance (fogStock.w, eye distance): 1 inside, easing to 0 over its last
// tenth (.9 = kDrawDistanceFadeStart). Fogged draws blend to the border-fog
// colour with it after the atmosphere (nativeAtmosphere), so a wholly faded
// pixel is exactly the sky behind the cull; draws with the game's fog off
// dissolve by a 4x4 ordered dither instead (blend-mode independent), so
// neither pops at the cull.
// The *At forms take the pixel's world point p (world geometry interpolates
// it): the same point the plain forms unproject from the pixel position.
float nativeDrawFadeDistance(float distance){return 1-smoothstep(fogStock.w*.9,fogStock.w,distance);}
float nativeDrawFade(float4 position){
 [branch]if(fogStock.w<=0)return 1;
 return nativeDrawFadeDistance(length(fogUnproject(position.xy*fogScreen.xy,position.z)-fogEye.xyz));
}
float nativeDrawFadeAt(float3 p){
 [branch]if(fogStock.w<=0)return 1;
 return nativeDrawFadeDistance(length(p-fogEye.xyz));
}
bool nativeDrawDissolvedFade(float4 position,float fade){
 if(fade>=1)return false;
 const uint bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
 uint2 p=uint2(position.xy)&3;
 return fade<(bayer[p.x+4*p.y]+.5)/16.0;
}
bool nativeDrawDissolved(float4 position){return nativeDrawDissolvedFade(position,nativeDrawFade(position));}
bool nativeDrawDissolvedAt(float4 position,float3 p){return nativeDrawDissolvedFade(position,nativeDrawFadeAt(p));}
float nativeFogFactor(float border,float4 position){
 if(fogProjection.w<.5||fogEye.w>.5)return border;
 return min(border,1-smoothstep(fogStock.x,fogStock.y,fogViewDepth(position.z)));
}
// Colour the border fog blends to. With the owned sky (presets 1 and 2) it is
// the sky the pixel would show without terrain, so fully fogged land and the
// empty space beyond the last map merge into the sky's horizon haze; the
// faithful preset and the stock sky keep the game's fog colour.
float3 borderFogColorAt(float3 p,float3 gameFog){
 float3 c=gameFog;
 [branch]if(fogTemporal.z>.5&&fogEye.w>.5){
  float3 r=normalize(p-fogEye.xyz);
  float az=atan2(r.y,r.x)*(.5/3.14159265);az+=az<0?1:0;
  c=skyProbe.SampleLevel(fogLinearClamp,float2(az,.5-asin(clamp(r.z,-1,1))/3.14159265),0).rgb;
 }
 return c;
}
float3 borderFogColor(float4 position,float3 gameFog){
 [branch]if(fogTemporal.z>.5&&fogEye.w>.5)return borderFogColorAt(fogUnproject(position.xy*fogScreen.xy,position.z),gameFog);
 return gameFog;
}
)" R"(
// Fog quality permutations: a program compiled with NATIVE_FOG_VOLUME_QUALITY
// fixes the atmosphere state the run-time constants otherwise choose, so the
// unused path compiles out: 0 no atmosphere (fog_preset 0), 1 the analytic
// atmosphere without the fog volume (preset 1), 2 the fog volume (preset 2).
// Undefined: chosen per pixel from fogEye.w and fogVolumeRange.w. A program
// with the define is bound only while the renderer's state matches it.
#ifdef NATIVE_FOG_VOLUME_QUALITY
#define NATIVE_FOG_ATMOSPHERE_ON (NATIVE_FOG_VOLUME_QUALITY>0)
#define NATIVE_FOG_VOLUME_ON (NATIVE_FOG_VOLUME_QUALITY>1)
#else
#define NATIVE_FOG_ATMOSPHERE_ON (fogEye.w>.5)
#define NATIVE_FOG_VOLUME_ON (fogVolumeRange.w>.5)
#endif
// Atmosphere for presets 1 and 2 on top of the border-fogged colour. Returns
// the transmittance applied to the surface (AO scales its ambient share).
float nativeAtmosphereCoreAt(inout float3 color,float4 position,float3 fogColor,float3 p){
 if(!NATIVE_FOG_ATMOSPHERE_ON||position.z>=1)return 1;
 float2 uv=position.xy*fogScreen.xy;
 float3 view=p-fogEye.xyz;float distance=length(view);view/=max(distance,1e-4);
 float3 inscatter=0;float transmittance;
 [branch]if(NATIVE_FOG_VOLUME_ON){
  // Smooth across angular cells only. Each surface samples its own depth:
  // filtering along z pulled the next slice's bright scatter onto nearer
  // geometry, especially along horizontal rays through the dense low fog.
  float3 c=float3(uv,fogSliceCoordinate(distance)-.5/fogScreen.z);
  float2 cell=fogVolumeCell.xy;
  float4 v=.25*(fogIntegrated.SampleLevel(fogLinearClamp,c+float3(-cell.x,-cell.y,0),0)+
               fogIntegrated.SampleLevel(fogLinearClamp,c+float3(cell.x,-cell.y,0),0)+
               fogIntegrated.SampleLevel(fogLinearClamp,c+float3(-cell.x,cell.y,0),0)+
               fogIntegrated.SampleLevel(fogLinearClamp,c+float3(cell.x,cell.y,0),0));
  // Texture slice zero stores its far boundary, not the empty near plane.
  // Clamp filtering cannot supply that missing boundary: interpolate it
  // explicitly so close surfaces never inherit the entire first slice.
  float firstFar=fogVolumeRange.x*exp(1/(fogVolumeRange.z*fogScreen.z));
  float first=saturate((distance-fogVolumeRange.x)/(firstFar-fogVolumeRange.x));
  v=lerp(float4(0,0,0,1),v,first);
  // Beyond the volume the aerial term continues analytically.
  transmittance=v.a*exp(-fogStock.z*max(distance-fogVolumeRange.y,0));inscatter=v.rgb;
 }else{
  transmittance=exp(-fogAirOpticalDepth(distance)-fogHeightOpticalDepth(p));
  inscatter=fogSunColor.rgb*fogSunResponse(dot(view,fogSun.xyz))*(1-transmittance);
 }
 // F10 component isolation: shafts (4) and lamps (5) show only their light.
 if(fogAir.w>3.5){color+=abs(inscatter);return 1;}
 float headroom=NATIVE_FOG_VOLUME_ON?fogShaft.z:0;
 color=color*transmittance+fogLighting(fogHazeColor(fogColor),transmittance,inscatter,headroom);
 return transmittance;
}
float nativeAtmosphereCore(inout float3 color,float4 position,float3 fogColor){
 if(!NATIVE_FOG_ATMOSPHERE_ON||position.z>=1)return 1;
 return nativeAtmosphereCoreAt(color,position,fogColor,fogUnproject(position.xy*fogScreen.xy,position.z));
}
// Draw-distance fade to the border-fog colour; returns the surface share kept.
float nativeDrawFadeTo(inout float3 color,float4 position,float3 gameFog){
 float fade=nativeDrawFade(position);
 [branch]if(fade<1)color=lerp(borderFogColor(position,gameFog),color,fade);
 return fade;
}
float nativeDrawFadeToAt(inout float3 color,float3 p,float3 gameFog){
 float fade=nativeDrawFadeAt(p);
 [branch]if(fade<1)color=lerp(borderFogColorAt(p,gameFog),color,fade);
 return fade;
}
// The atmosphere, then the draw-distance fade: haze added after the fade
// left far surfaces lighter than the sky behind them (silhouettes past the
// fade start, and a visible step where the cull removes them).
float nativeAtmosphere(inout float3 color,float4 position,float3 fogColor){
 float transmittance=nativeAtmosphereCore(color,position,fogColor);
 return transmittance*nativeDrawFadeTo(color,position,fogColor);
}
float nativeAtmosphereAt(inout float3 color,float4 position,float3 fogColor,float3 p){
 float transmittance=nativeAtmosphereCoreAt(color,position,fogColor,p);
 return transmittance*nativeDrawFadeToAt(color,p,fogColor);
}
)";

// Froxel passes (preset 2). Compiled with the native shader source so the
// in-scattering uses the exact sun-cascade selection and point-light records
// and shadow maps of the world pass.
constexpr char kFogVolumeShader[] = R"(
RWTexture3D<float4> fogVolumeOut:register(u0);
Texture3D<float4> fogVolumeHistory:register(t14);
Texture2D<float4> fogMist:register(t15);
Texture3D<float4> fogVolumeScatter:register(t12);
float fogSliceDistance(float w){return fogVolumeRange.x*exp(w/fogVolumeRange.z);}
float3 fogRay(float2 uv){return normalize(fogUnproject(uv,1)-fogEye.xyz);}
// 2x2 comparison taps: the froxel centres move against the shadow texels as
// the camera moves, so a single tap would crawl.
float fogSliceVisibility(uint slice,float3 p,out float edge){
 float4 l=mul(shadowLight[slice],float4(p,1));float3 c=l.xyz/l.w;
 edge=max(abs(c.x),abs(c.y));
 if(edge>1||c.z<0||c.z>1)return 1;
 return shadowDepth.SampleCmpLevelZero(shadowSampler,float3(c.x*.5+.5,.5-c.y*.5,slice),saturate(c.z-.002));
}
// Cross-fades into the next coarser map near a map's edge and over the
// close maps' view-distance overlap, as surface shadows do
// (nativeShadowVisibility). A hard switch moved with the camera-fitted
// maps, so haze near a border jumped between resolutions while panning.
float fogSunVisibility(float3 p){
 uint slice=nativeShadowSlice(p);
 if(slice==5||(shadowControl.x&(1u<<slice))==0)return 1;
 float edge;float visibility=fogSliceVisibility(slice,p,edge);
 float blend=smoothstep(.75,1-7.0/max(1.0,(float)shadowControl.z),edge);
 if(slice<3&&shadowBias[slice].w>shadowBias[slice].z)
  blend=max(blend,smoothstep(shadowBias[slice].z,shadowBias[slice].w,mul(shadowCamera[slice],float4(p,1)).w));
 if(blend<=0)return visibility;
 if(slice==4)return lerp(visibility,1,blend);
 [unroll]for(uint coarser=0;coarser<5;coarser++){
  if(coarser>slice&&(shadowControl.x&(1u<<coarser))!=0&&nativeShadowContains(coarser,p)){
   float unused;return lerp(visibility,fogSliceVisibility(coarser,p,unused),blend);}
 }
 return lerp(visibility,1,blend);
}
// Light shafts: sun visibility averaged over the froxel's whole depth
// extent (and across its width), so the result is the shadowed fraction of
// the segment rather than a point sample that crawls as the grid moves.
// The taps (fogShaftTap) are the same every frame: per-frame rotation
// shimmered while the camera moved, because the history is rejected there.
float fogHalton(uint i,uint b){float f=1,r=0;[loop]while(i>0){f/=b;r+=f*(i%b);i/=b;}return r;}
float fogShaftVisibility(float3 ray,float near,float far,float width,float2 jitter){
 float sum=0;
 [unroll]for(int k=0;k<6;k++)sum+=fogSunVisibility(fogEye.xyz+fogShaftTap(k,ray,near,far,width,jitter));
 return sum/6;
}
float3 fogPointScatter(float3 p){
 float3 sum=0;
 uint2 lightRange=nativePointLightRange(p);
 [loop]for(uint i=lightRange.x;i<lightRange.y;++i){
  NativePointLight light=nativePointLights[nativePointLightGrid[i]];
  float3 d=light.centerInverseRadiusSquared.xyz-p;
  float reach=1-dot(d,d)*light.centerInverseRadiusSquared.w;
  if(reach<=0)continue;
  float visibility=1;
  [branch]if(light.shadowSlots.x>0){
   float radius=light.shadowCenter.w,nearPlane=radius*.02,depthScale=1-nearPlane/radius;
   uint face;float2 cube;float major;
   nativePointCubeCoordinate(p-light.shadowCenter.xyz,face,cube,major);
   float compareDepth=(1-nearPlane/max(major-radius*.01,nearPlane))/depthScale;
   visibility=nativePointFetchCompare((uint)light.shadowSlots.x-1,cube,face,compareDepth);
   if(light.shadowSlots.y>0)visibility=min(visibility,lerp(1,nativePointFetchCompare((uint)light.shadowSlots.y-1,cube,face,compareDepth),light.shadowSlots.w));
   visibility=lerp(1,visibility,light.shadowSlots.z);
  }
  sum+=light.color.rgb*(reach*reach*visibility);
 }
 return sum*(1/12.566371);
}
// Smooth value noise for drifting wisps and rolling low mist. Animated by the
// world time and the integrated renderer wind, so it freezes with the game and
// never changes per frame on its own.
float fogHash(float3 p){p=frac(p*.3183099+.1);p*=17;return frac(p.x*p.y*p.z*(p.x+p.y+p.z));}
float fogValueNoise(float3 x){
 float3 i=floor(x),f=frac(x);f=f*f*(3-2*f);
 return lerp(lerp(lerp(fogHash(i),fogHash(i+float3(1,0,0)),f.x),lerp(fogHash(i+float3(0,1,0)),fogHash(i+float3(1,1,0)),f.x),f.y),
             lerp(lerp(fogHash(i+float3(0,0,1)),fogHash(i+float3(1,0,1)),f.x),lerp(fogHash(i+float3(0,1,1)),fogHash(i+float3(1,1,1)),f.x),f.y),f.z);
}
// footprint: the froxel's size in metres. Noise finer than the cells would
// alias against the camera-aligned grid, so its contrast fades out as the
// cells grow (a pre-filter, not a temporal blend); the fine octave goes first.
float fogWisp(float3 p,float footprint){
 // The drift offset follows the renderer wind (fogVolumeCell.zw, world units).
 float t=fogWisps.x;
 float3 q=float3((p.xy+fogVolumeCell.zw)*.09,p.z*.14+t*.0008);
 float coarse=saturate(5/max(footprint,1e-3)-.5),fine=saturate(2/max(footprint,1e-3)-.5);
 float n=.5+coarse*(.65*fogValueNoise(q)-.325)+fine*(.35*fogValueNoise(q*2.3+7.1)-.175);
 return lerp(1,saturate(n*1.8-.35)*1.7,fogWisps.y);
}
// Swamp and open-water mist: an exponential layer resting on the liquid
// surface, integrated analytically over a ray segment (eye distances near..
// far) with the pre-blurred map coverage at the segment's middle. A thin
// layer point-sampled at froxel centres popped as the cells moved.
float fogMistOpticalDepth(float3 ray,float near,float far,float footprint){
 if(fogMistGrid.z<=0)return 0;
 float2 middle=fogEye.xy+ray.xy*(.5*(near+far));
 float4 m=fogMist.SampleLevel(fogLinearClamp,(middle-fogMistGrid.xy)*fogMistGrid.zw,0);
 if(m.w<=1e-3)return 0;
 float surface=m.z/m.w,h=max(fogScreen.w,.1);
 float a=max(fogEye.z+ray.z*near-surface,0)/h,b=max(fogEye.z+ray.z*far-surface,0)/h;
 float mean=abs(b-a)>1e-3?(exp(-a)-exp(-b))/(b-a):exp(-a);
 return (fogHeight.w*m.x+fogWisps.z*m.y)*fogWisp(float3(middle,surface+h),footprint)*mean*(far-near);
}
float fogHeightDensity(float3 p){return fogHeight.x*exp(-clamp((p.z-fogHeight.y)/max(fogHeight.z,.1),-40,80));}
// Per froxel centre (no jitter): in-scattered light per unit extinction, and
// the local density added to the analytic air and height fog (mist and the
// wisp modulation of the ground fog).
[numthreads(4,4,4)]void fogInjectCS(uint3 id:SV_DispatchThreadID){
 uint3 size;fogVolumeOut.GetDimensions(size.x,size.y,size.z);
 if(any(id>=size))return;
 float2 uv=(id.xy+.5)/size.xy;
 float distance=fogSliceDistance((id.z+.5)/size.z);
 float near=fogSliceDistance(id.z/(float)size.z),far=fogSliceDistance((id.z+1.0)/size.z);
 float3 ray=fogRay(uv),p=fogEye.xyz+ray*distance;
 float width=distance*length(fogRay(uv+float2(1.0/size.x,0))-ray);
 float wisp=fogWisp(p,max(far-near,width));
 float local=fogHeightDensity(p)*(wisp-1)*.7;
 // Same sun-glow strength as the analytic atmosphere; the full preset
 // additionally resolves the segment's shadow and cloud visibility, and the
 // shaft term carves the haze itself with that visibility.
 float c=dot(ray,fogSun.xyz);
 // Per-frame Halton jitter (fogTemporal.w = frame index), decorrelated per
 // froxel by interleaved gradient noise.
 uint frameIndex=(uint)fogTemporal.w;
 float2 halton=float2(fogHalton(frameIndex+1,2),fogHalton(frameIndex+1,3));
 float noise=frac(52.9829189*frac(dot(float2(id.xy)+float2(id.z*7,id.z*13),float2(.06711056,.00583715))));
 float visibility=fogShaftVisibility(ray,near,far,width,frac(halton+noise))*skyCloudVisibility(p);
 float3 sun=fogSunColor.rgb*fogSunResponse(c)*visibility+fogShaftHaze.rgb*fogShaftWeight(c,visibility);
 float4 current=float4(sun+fogPointScatter(p)*fogSunColor.w,local);
 [branch]if(fogTemporal.y>0){
  float4 previous=mul(fogPreviousWorldToClip,float4(p,1));
  if(previous.w>1e-4){
   float3 ndc=previous.xyz/previous.w;
   float2 historyUv=float2(ndc.x*.5+.5,.5-ndc.y*.5);
   float historyW=fogSliceCoordinate(length(p-fogPreviousEye.xyz));
   // The history is reprojected in world space, so it stays valid while the
   // camera moves. Rejecting it on motion left every froxel its raw value:
   // the shadowed shaft term sampled at froxel centres that slide over the
   // world, i.e. shimmer whenever the camera moved. Keep it under motion and
   // clamp it to a band around the current value instead (no stale smear).
   float moved=length(float3((historyUv-uv)*size.xy,historyW*size.z-(id.z+.5)));
   float weight=fogTemporal.y*saturate(1-moved*.04);
   if(all(historyUv>0)&&all(historyUv<1)&&historyW>0&&historyW<1&&weight>0){
    float4 history=fogVolumeHistory.SampleLevel(fogLinearClamp,float3(historyUv,historyW),0);
    // Wide band: jittered samples differ frame to frame by design; a tight
    // clamp passed that noise straight through instead of averaging it.
    // The shaft term can be negative (shadow carves the haze): order the
    // bounds, or an inverted clamp let the history run away to black.
    float4 boundA=current*.25,boundB=current*4;
    history=clamp(history,min(boundA,boundB)-.01,max(boundA,boundB)+.01);
    current=lerp(current,history,weight);
   }
  }
 }
 fogVolumeOut[id]=current;
}
// Front-to-back energy-conserving integration with the air and height fog
// integrated analytically over each slice; texel z holds the result at the
// far boundary of slice z.
[numthreads(8,8,1)]void fogIntegrateCS(uint3 id:SV_DispatchThreadID){
 uint3 size;fogVolumeOut.GetDimensions(size.x,size.y,size.z);
 if(any(id.xy>=size.xy))return;
 float2 uv=(id.xy+.5)/size.xy;
 float3 ray=fogRay(uv);
 float angular=length(fogRay(uv+float2(1.0/size.x,0))-ray);
 float h=max(fogHeight.z,.1);
 float last=fogVolumeRange.x;
 float3 scatter=0;float transmittance=1;
 [loop]for(uint z=0;z<size.z;++z){
  float distance=fogSliceDistance((z+1.0)/size.z);
  float len=distance-last;
  float a=clamp((fogEye.z+ray.z*last-fogHeight.y)/h,-40,80),b=clamp((fogEye.z+ray.z*distance-fogHeight.y)/h,-40,80);
  float heightMean=abs(b-a)>1e-3?(exp(-a)-exp(-b))/(b-a):exp(-a);
  float4 v=fogVolumeScatter.Load(int4(id.xy,z,0));
  float opticalDepth=max(fogAirOpticalDepth(distance)-fogAirOpticalDepth(last)+fogHeight.x*heightMean*len+v.a*len+
                         fogMistOpticalDepth(ray,last,distance,max(len,distance*angular)),0);
  float t=exp(-opticalDepth);
  scatter+=transmittance*v.rgb*(1-t);
  transmittance*=t;
  last=distance;
  fogVolumeOut[uint3(id.xy,z)]=float4(scatter,transmittance);
 }
}
)";

}  // namespace owned
