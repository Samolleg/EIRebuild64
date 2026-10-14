#pragma once
// Interactive water waves behind units: a damped wave-equation height field
// in a window that follows the units in water (renderer_liquid.cpp), stepped
// at a fixed 30 Hz of game time, so paused time holds it and fast speed runs
// it faster. Shared with Prototypes/shader-lab so wakes can be tuned there.
//
// A unit is a moving pressure source making local rings and churn around
// its legs (short-lived: the field damps fast); the trailing Kelvin V is the
// liquid shader's (liquidUnitWake). Strength follows speed and body size.
// The field's third channel is the wading trail: every step each unit
// stamps its footprint into it and the trail fades over a few game seconds,
// so a wading unit leaves one continuous band of stirred water behind it
// (like a snow trail), not a train of separate rings. The river carries the
// trail downstream: each step moves it semi-Lagrangian by the current near
// the units (their feet's river speed, weighted by distance), so in running
// water the band drifts off and in still water it stays and fades.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace owned {

inline constexpr int kLiquidWaveSize = 512;
inline constexpr float kLiquidWaveCell = .125f;   // world units per texel: a 64 x 64 window
inline constexpr double kLiquidWaveStep = 1.0 / 30.0; // game seconds per step
// Courant number c^2 dt^2 / dx^2: wave speed sqrt(.07) * .125 m * 30 Hz ~ 1 m/s,
// below walking speed (~1.5 m/s) so walking already leaves a V wake.
inline constexpr float kLiquidWaveCourant = .07f;
// Per step: half-life ~0.5 game seconds, so the field's rings stay local to
// the legs (the trailing V is drawn analytically by the liquid shader).
inline constexpr float kLiquidWaveDamping = .955f;
// Per step: the trail's half-life is ~0.5 game seconds.
inline constexpr float kLiquidTrailDecay = .9548f;
// River speed from the liquid surface gradient, as the liquid shader runs its
// ripples (liquid_surface.hlsl liquidRiverOnset, liquidRiverSpeed): still below
// the onset slope, then .5 m/s plus 2.5 per unit slope, at most 4 m/s.
inline void liquid_river_velocity(float gx, float gy, float out[2]) noexcept {
    const float slope = std::sqrt(gx * gx + gy * gy);
    float t = std::clamp((slope - .015f) / (.08f - .015f), 0.f, 1.f);
    t = t * t * (3 - 2 * t);
    const float speed = std::min(.5f + 2.5f * slope, 4.f) * t / std::max(slope, 1e-5f);
    out[0] = gx * speed; out[1] = gy * speed;
}
// Game route speed is world units per game tick (~55 ms).
inline constexpr float kLiquidWaveSecondsPerTick = .055f;

constexpr char kLiquidWaveShader[] = R"(
cbuffer LiquidWaveStep:register(b0){float2 waveOrigin;float waveCell;float waveCourant;
 int2 waveShift;float waveDamping;uint waveUnits;float4 waveUnit[16];float4 waveFlow[16];};
Texture2D<float4> waveIn:register(t0);RWTexture2D<float4> waveOut:register(u0);
static const int WAVE_SIZE=512;
static const float WAVE_TRAIL_DECAY=.9548; // kLiquidTrailDecay
static const float WAVE_STEP=1.0/30; // kLiquidWaveStep
static const float WAVE_FLOW_REACH=6; // m: a unit's river current carries the trail around it
float4 waveLoad(int2 p){return all(p>=0&&p<WAVE_SIZE)?waveIn.Load(int3(p,0)):0;}
// The trail channel bilinear at a texel position (texel centres at integers).
float waveTrailAt(float2 q){
 int2 i=int2(floor(q));float2 f=q-i;
 return lerp(lerp(waveLoad(i).z,waveLoad(i+int2(1,0)).z,f.x),lerp(waveLoad(i+int2(0,1)).z,waveLoad(i+1).z,f.x),f.y);
}
[numthreads(8,8,1)]void liquidWaveCS(uint3 id:SV_DispatchThreadID){
 int2 p=int2(id.xy),s=p+waveShift;
 float4 c=waveLoad(s); // x current height, y previous height, z trail
 // A window shift with no game step copies both heights without simulation.
 if(waveCourant==0){waveOut[p]=c;return;}
 float laplacian=waveLoad(s+int2(1,0)).x+waveLoad(s-int2(1,0)).x+
                 waveLoad(s+int2(0,1)).x+waveLoad(s-int2(0,1)).x-4*c.x;
 float h=(2*c.x-c.y+waveCourant*laplacian)*waveDamping;
 float2 world=waveOrigin+(float2(p)+.5)*waveCell;
 // River current here: the units' feet currents, weighted by distance.
 float2 current=0;float weight=1e-4;
 [loop]for(uint f=0;f<waveUnits;++f){
  float2 d=world-waveUnit[f].xy;float w=exp(-dot(d,d)/(WAVE_FLOW_REACH*WAVE_FLOW_REACH));
  current+=w*waveFlow[f].xy;weight+=w;
 }
 current/=weight;
 float trail=waveTrailAt(float2(s)-current*(WAVE_STEP/waveCell))*WAVE_TRAIL_DECAY;
 // Each unit is a pressure source (xy position, z footprint radius, w push
 // per step): it presses the surface down under a smooth footprint; moving,
 // it outruns the waves it makes and they trail behind it as a V wake.
 [loop]for(uint u=0;u<waveUnits;++u){
  float d=length(world-waveUnit[u].xy)/waveUnit[u].z;
  if(d<1){float k=1-d*d;h-=waveUnit[u].w*k*k;}
  // The trail: the legs' footprint, a little narrower than the body.
  float t=d/.75;if(t<1)trail=max(trail,1-t*t);
 }
 int edge=min(min(p.x,p.y),min(WAVE_SIZE-1-p.x,WAVE_SIZE-1-p.y));
 float fade=saturate(edge/12.0);
 waveOut[p]=float4(h*fade,c.x,trail*fade,0);
})";

struct LiquidWaveStepConstants {
    float origin[2]; float cell, courant; int shift[2]; float damping; std::uint32_t units; float unit[16][4];
    float flow[16][4]; // river velocity xy (m/s) at each unit (liquid_river_velocity)
};
static_assert(sizeof(LiquidWaveStepConstants) == 32 + 2 * 16 * 16, "LiquidWaveStep cbuffer layout");

// One unit's source: position, route speed (world units per game tick),
// body radius (world units), phase, game seconds -> {x, y, radius, push}.
// Walking (~1.5 m/s) gives a clear wake; running roughly doubles it; size
// scales it by the footprint area. A standing unit only bobs gently.
inline void liquid_wave_unit(float x, float y, float tick_speed, float radius, float phase, double seconds, float out[4]) noexcept {
    const float metres_per_second = std::max(tick_speed, 0.f) / kLiquidWaveSecondsPerTick;
    // radius: the body's half extent. The pressed footprint is the body;
    // the push grows with its area, so a rat makes tiny rings, a troll big ones.
    const float r = std::clamp(radius, .1f, 2.f);
    const float size = std::clamp(radius / .35f, .15f, 5.f);
    const float moving = std::min(metres_per_second, 5.f);
    const float bob = .0006f * float(std::sin(seconds * 5 + phase));
    // A steady push (no stride pulse): the wake is one continuous V, not a
    // train of separate ripples.
    out[0] = x; out[1] = y; out[2] = r;
    out[3] = (.0007f * moving + bob) * size;
}

} // namespace owned
