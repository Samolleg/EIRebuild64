#pragma once

// Horizon-based (GTAO-style) ambient occlusion applied to the ambient term only.
//
// Our world pass is forward shaded in the game's submission order, so no
// complete opaque depth exists before lighting. Instead, every native world
// pixel also writes the exact share of its final colour that came from the
// ambient light (after texture, specular clamp, bed tint and fog) into a
// second target that follows the same blend as the colour. After the world
// pass, AO is computed from the finished depth and that share is scaled:
//   final -= ambientShare * (1 - visibility^strength)
// This equals shading with the ambient term multiplied by the occlusion for
// every linear blend, and leaves direct sun, water, sky and UI untouched.

#include <algorithm>
#include <cstdint>

namespace owned {

// ao_quality: 0 off, 1 low (1/4 resolution per axis), 2 high (1/2 per axis).
struct AmbientOcclusionPreset {
    unsigned divisor = 0;  // full-resolution pixels per AO pixel along each axis
    unsigned slices = 0;   // horizon directions per AO pixel
    unsigned steps = 0;    // depth samples per direction side
};

constexpr AmbientOcclusionPreset ambient_occlusion_preset(int quality) noexcept {
    return quality <= 0 ? AmbientOcclusionPreset{}
         : quality == 1 ? AmbientOcclusionPreset{4, 2, 4}
                        : AmbientOcclusionPreset{2, 3, 6};
}

constexpr unsigned ambient_occlusion_extent(unsigned full, unsigned divisor) noexcept {
    return divisor ? (std::max)(1u, (full + divisor - 1) / divisor) : 0u;
}

struct AmbientOcclusionFrame {
    float strength = 1.0f;  // visibility exponent
    float radius = 1.0f;    // world units (metres)
};

// Contact AO must carry more of the depth when the ambient light dominates
// (night, overcast): scale with the ambient share of the frame's shared world
// lighting. The neutral daylight placeholder (ambient 0.2, sun 0.6, share
// 0.25) is unscaled; the full boost applies once the sun falls to a third of
// the ambient (share 0.75). low_light_floor raises the weight from the game's
// own time of day and weather (night, precipitation) when the published
// lighting does not already carry them.
inline AmbientOcclusionFrame ambient_occlusion_frame(float strength, float radius, float low_light_boost,
                                                     const float ambient[3], const float sun[3],
                                                     float low_light_floor = 0.0f) noexcept {
    auto luma = [](const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; };
    const float a = (std::max)(0.0f, luma(ambient)), s = (std::max)(0.0f, luma(sun));
    const float share = a + s > 1e-6f ? a / (a + s) : 1.0f;
    const float t = std::clamp((std::max)((share - 0.25f) / 0.5f, low_light_floor), 0.0f, 1.0f);
    return {strength * (1.0f + ((std::max)(1.0f, low_light_boost) - 1.0f) * t), radius * (1.0f + 0.25f * t)};
}

// Row-major 4x4 inverse (Gauss-Jordan, partial pivoting, double precision).
inline bool ambient_occlusion_invert(const float in[16], float out[16]) noexcept {
    double m[4][8]{};
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) m[r][c] = in[r * 4 + c];
        m[r][4 + r] = 1.0;
    }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if ((m[r][c] < 0 ? -m[r][c] : m[r][c]) > (m[pivot][c] < 0 ? -m[pivot][c] : m[pivot][c])) pivot = r;
        if ((m[pivot][c] < 0 ? -m[pivot][c] : m[pivot][c]) < 1e-12) return false;
        for (int k = 0; k < 8; ++k) std::swap(m[pivot][k], m[c][k]);
        const double d = m[c][c];
        for (int k = 0; k < 8; ++k) m[c][k] /= d;
        for (int r = 0; r < 4; ++r)
            if (r != c) {
                const double f = m[r][c];
                for (int k = 0; k < 8; ++k) m[r][k] -= f * m[c][k];
            }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[r * 4 + c] = static_cast<float>(m[r][4 + c]);
    return true;
}

struct AmbientOcclusionConstants {
    float clip_to_world[16];
    float world_to_clip[16];
    float full_size[4];  // width, height, 1/width, 1/height
    float low_size[4];
    float params[4];     // radius, max radius (full pixels), strength, relative depth tolerance
    std::uint32_t control[4];  // divisor, slices, steps, unused
};
static_assert(sizeof(AmbientOcclusionConstants) == 192, "AO constants must match the HLSL layout");

// Loads only (no samplers): hardware depth is never filtered, and the passes
// fit the composite state guard (t0-t2, b1).
// RGB remains the ambient share. Negative alpha excludes snow from receiving
// and casting AO; existing world writers keep their alpha.
#define EID3D11_AO_SURFACE_SHADER \
 "float4 aoSurfaceShare(float3 color,float3 direct,bool thin,bool snow){return float4(thin||snow?float3(0,0,0):.35*max(color-direct,0),snow?-1:1);}" \
 "bool aoSurfaceCasts(float4 share){return share.a>=0;}"
constexpr char kAmbientOcclusionSurfaceShader[] = EID3D11_AO_SURFACE_SHADER;
constexpr char kAmbientOcclusionShader[] = EID3D11_AO_SURFACE_SHADER R"(
cbuffer AmbientOcclusionConstants:register(b1){row_major float4x4 aoClipToWorld;row_major float4x4 aoWorldToClip;
 float4 aoFullSize;float4 aoLowSize;float4 aoParams;uint4 aoControl;};
Texture2D<float> aoDepth:register(t0);
Texture2D<float2> aoLow:register(t1);
// Horizon pass only: per AO pixel, the hardware depth of its representative
// full-resolution texel, or -1 where that texel casts no AO.
Texture2D<float> aoCompact:register(t1);
Texture2D<float4> aoAmbient:register(t2);
static const float AO_PI=3.14159265;
static const float AO_SKY_DEPTH=65000;
struct AoVertex{float4 position:SV_Position;};
AoVertex aoFullscreenVS(uint id:SV_VertexID){AoVertex o;float2 uv=float2((id<<1)&2,id&2);o.position=float4(uv.x*2-1,1-uv.y*2,0,1);return o;}
float aoAcos(float x){float a=abs(x);float r=(-0.156583*a+AO_PI*.5)*sqrt(saturate(1-a));return x>=0?r:AO_PI-r;}
float3 aoUnproject(float2 uv,float depth){float4 p=mul(aoClipToWorld,float4(uv.x*2-1,1-uv.y*2,depth,1));return p.xyz/p.w;}
float aoLinearDepth(float3 world){return dot(aoWorldToClip[3],float4(world,1));}
bool aoInside(int2 texel){return all(texel>=0)&&all(texel<int2(aoFullSize.xy));}
float aoDepthAt(int2 texel){return aoDepth.Load(int3(clamp(texel,int2(0,0),int2(aoFullSize.xy)-1),0));}
float3 aoPositionAt(int2 texel){return aoUnproject((texel+.5)*aoFullSize.zw,aoDepthAt(texel));}
float3 aoSmallerDelta(float3 forward,float3 backward){return dot(forward,forward)<dot(backward,backward)?forward:backward;}
bool aoOccluder(int2 texel){return aoInside(texel)&&aoSurfaceCasts(aoAmbient.Load(int3(texel,0)));}
float3 aoSurfaceDelta(int2 texel,int2 step,float3 P){
 bool forward=aoOccluder(texel+step),backward=aoOccluder(texel-step);
 if(forward&&backward)return aoSmallerDelta(aoPositionAt(texel+step)-P,P-aoPositionAt(texel-step));
 if(forward)return aoPositionAt(texel+step)-P;
 if(backward)return P-aoPositionAt(texel-step);
 return 0;
}

int2 aoCenterTexel(int2 low){int divisor=(int)aoControl.x;return min(low*divisor+divisor/2,int2(aoFullSize.xy)-1);}
float aoCompactAt(int2 low){return all(low>=0)&&all(low<int2(aoLowSize.xy))?aoCompact.Load(int3(low,0)):-1;}
float3 aoCompactPosition(int2 low,float depth){return aoUnproject((aoCenterTexel(low)+.5)*aoFullSize.zw,depth);}

// One compact depth per AO pixel, so horizon samples read a small low
// resolution texture instead of two scattered full-resolution ones.
float aoPreparePS(AoVertex input):SV_Target{
 int2 texel=aoCenterTexel(int2(input.position.xy));
 return aoOccluder(texel)?aoDepthAt(texel):-1;
}

float2 aoHorizonPS(AoVertex input):SV_Target{
 int2 low=int2(input.position.xy);
 int2 texel=aoCenterTexel(low);
 float depth=aoCompact.Load(int3(low,0));
 if(depth<0||depth>=.999999)return float2(1,AO_SKY_DEPTH);
 float2 uv=(texel+.5)*aoFullSize.zw;
 float3 P=aoUnproject(uv,depth);
 float3 V=normalize(aoUnproject(uv,0)-P);
 float3 dx=aoSurfaceDelta(texel,int2(1,0),P);
 float3 dy=aoSurfaceDelta(texel,int2(0,1),P);
 float3 N=cross(dx,dy);N=dot(N,N)>1e-20?normalize(N):V;if(dot(N,V)<0)N=-N;
 // A 4x4 interleaved rotation/jitter pattern; the denoise window covers it.
 const uint pattern[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
 uint index=pattern[(low.x&3)+4*(low.y&3)];
 float rotation=index/16.0,jitter=frac(index*.618034+.5);
 float radius=aoParams.x,falloff=1/(.6*radius);
 uint slices=aoControl.y,steps=aoControl.z;
 float visibility=0;
 [loop]for(uint slice=0;slice<slices;++slice){
  float phi=(slice+rotation)*AO_PI/slices;
  float2 omega=float2(cos(phi),sin(phi));
  float3 along=aoUnproject(uv+omega*aoFullSize.zw,depth)-P;
  float3 ortho=along-dot(along,V)*V;
  float orthoLength=length(ortho);
  if(orthoLength<1e-8){visibility+=1;continue;}
  ortho/=orthoLength;
  float3 axis=cross(ortho,V);
  float3 projected=N-axis*dot(N,axis);
  float projectedLength=length(projected);
  float cosN=saturate(dot(projected,V)/max(projectedLength,1e-6));
  float n=(dot(ortho,projected)>=0?1:-1)*aoAcos(cosN);
  float low0=cos(n+AO_PI*.5),low1=cos(n-AO_PI*.5);
  float horizon0=low0,horizon1=low1;
  float4 end=mul(aoWorldToClip,float4(P+ortho*radius,1));
  float radiusPixels=end.w>1e-6?length((float2(end.x,-end.y)/end.w*.5+.5-uv)*aoFullSize.xy):0;
  radiusPixels=min(radiusPixels,aoParams.y);
  if(radiusPixels>=1){
   // Steps advance in AO pixels (at least one); the world-distance falloff
   // rejects samples beyond the radius.
   float divisor=aoControl.x;
   [loop]for(uint k=0;k<steps;++k){
    float t=(k+jitter)/steps;
    int2 offset=int2(round(omega*max((1+t*t*(radiusPixels-1))/divisor,1)));
    int2 s0=low+offset,s1=low-offset;
    float depth0=aoCompactAt(s0),depth1=aoCompactAt(s1);
    if(depth0>=0){
     float3 d=aoCompactPosition(s0,depth0)-P;float l=length(d);
     horizon0=max(horizon0,lerp(low0,dot(d,V)/max(l,1e-6),saturate((radius-l)*falloff)));
    }
    if(depth1>=0){
     float3 d=aoCompactPosition(s1,depth1)-P;float l=length(d);
     horizon1=max(horizon1,lerp(low1,dot(d,V)/max(l,1e-6),saturate((radius-l)*falloff)));
    }
   }
  }
  float h0=-aoAcos(horizon1),h1=aoAcos(horizon0);
  h0=n+clamp(h0-n,-AO_PI*.5,AO_PI*.5);h1=n+clamp(h1-n,-AO_PI*.5,AO_PI*.5);
  float sinN=sin(n);
  visibility+=projectedLength*((cosN+2*h0*sinN-cos(2*h0-n))+(cosN+2*h1*sinN-cos(2*h1-n)))*.25;
 }
 return float2(saturate(visibility/slices),aoLinearDepth(P));
}

// Depth-aware 5x5 window, half-weight rim: every rotation of the 4x4 pattern
// receives equal weight.
float2 aoDenoisePS(AoVertex input):SV_Target{
 int2 p=int2(input.position.xy);
 float2 center=aoLow.Load(int3(p,0));
 if(center.y>=AO_SKY_DEPTH)return center;
 float sum=0,weight=0;
 [unroll]for(int y=-2;y<=2;++y)[unroll]for(int x=-2;x<=2;++x){
  float2 s=aoLow.Load(int3(clamp(p+int2(x,y),int2(0,0),int2(aoLowSize.xy)-1),0));
  float w=(abs(x)==2?.5:1)*(abs(y)==2?.5:1)*saturate(1-abs(s.y-center.y)/(center.y*aoParams.w));
  sum+=s.x*w;weight+=w;
 }
 return float2(sum/max(weight,1e-4),center.y);
}

// Full resolution: depth-aware bilinear upsample, then remove the occluded
// part of this pixel's ambient share (reverse-subtract blend).
float4 aoResolvePS(AoVertex input):SV_Target{
 int2 p=int2(input.position.xy);
 float3 ambient=aoAmbient.Load(int3(p,0)).rgb;
 if(all(ambient<=0))return 0;
 float depth=aoDepth.Load(int3(p,0));
 if(depth>=.999999)return 0;
 float w=aoLinearDepth(aoUnproject((p+.5)*aoFullSize.zw,depth));
 float2 q=(p+.5)/aoControl.x-.5;
 int2 base=int2(floor(q));float2 f=q-base;
 float sum=0,weight=0,nearest=1,nearestDelta=1e30;
 [unroll]for(int j=0;j<2;++j)[unroll]for(int i=0;i<2;++i){
  float2 s=aoLow.Load(int3(clamp(base+int2(i,j),int2(0,0),int2(aoLowSize.xy)-1),0));
  float delta=abs(s.y-w);
  float tap=(i?f.x:1-f.x)*(j?f.y:1-f.y)*saturate(1-delta/(w*aoParams.w));
  sum+=s.x*tap;weight+=tap;
  if(delta<nearestDelta){nearestDelta=delta;nearest=s.x;}
 }
 float visibility=weight>1e-3?sum/weight:nearest;
 return float4(ambient*(1-pow(saturate(visibility),aoParams.z)),0);
})";

#undef EID3D11_AO_SURFACE_SHADER

}  // namespace owned
