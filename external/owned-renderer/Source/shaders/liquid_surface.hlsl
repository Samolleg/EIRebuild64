// Liquid surface shading (liquid_surface_shader.h).
// Embedded into the game by Source/liquid_surface_shader.h at build time; Prototypes/shader-lab
// loads this file directly. The __cplusplus guard lines make the file a C++
// raw string where that header includes it; to HLSL they are a skipped
// group of comments and one closed string.
// Keep each piece between guard lines under 15000 bytes (MSVC literal limit).
#ifdef __cplusplus
R"hlsl( //"
#endif
// Liquid art controls. Distances are terrain units; time is the game's precise
// segment time. All liquid families use their authored colour/opacity/emission.
// Shipped positive depth medians are ~.7-2.4, with deep lakes over 10.
// Reach authored opacity at 1.75; colour absorption continues into deeper beds.
static const float liquidAbsorption = 1.7; // @param 0 5
static const float liquidOpaqueDepth = 1.75; // @param 0.2 5
static const float liquidColorAbsorption = .5; // @param 0 2
static const float liquidOpaqueMaterialStart = .9; // Smooth approach to authored opaque materials.
static const float liquidShallowOpacity = .45; // Fraction of authored opacity. @param 0 1
static const float liquidShallowLighten = .20; // @param 0 1
static const float liquidDeepBrightness = .45; // @param 0 1
static const float3 liquidDetailScale = float3(.65,1.7,4.3);
static const float liquidCalmMotion = .06;
static const float liquidEmissiveFlow = .15;
static const float liquidF0 = .045;
static const float liquidSpecularPower = 128; // @param 8 512
static const float liquidSpecularStrength = .6; // @param 0 2
static const float liquidSkyZenith = .65; // @param 0 1.5
// Shore width is vertical layer depth, not horizontal distance to dry land.
// Shipped shallow P10 depths are ~.14-.30. Any authored emission disables shore.
static const float liquidShoreDepth = .35; // @param 0 1.5
static const float liquidShoreNoiseScale = 1.4;
static const float2 liquidShoreBreakup = float2(.2,.75);
static const float liquidShoreCoverage = .35; // Visible base band between foam patches.
static const float liquidShoreFlowSpeed = .035;
static const float liquidShoreBrightness = .65; // @param 0 2
static const float liquidShoreOpacity = .12; // @param 0 1
static const float liquidShoreWhiten = .75; // @param 0 1
static const float2 liquidShoreColorRange = float2(.06,.45);
static const float liquidMuddyShoreMinimum = .2;
// Filter from four to two pixels per noise cell, then retain only its mean.
static const float2 liquidFilterRange = float2(.25,.5);
static const float liquidMudOpacity = .85;
static const float liquidMudBrightness = .62;
static const float2 liquidLavaTile = float2(10,4.5); // world units per detail-map repeat
static const float liquidAlgaeTile = 14; // world units per swamp algae repeat
static const float liquidSwampTile = 2; // world units per layer-art repeat
static const float liquidSwampDrift = .03; // world units per game second
// Swamp/water edge (liquidSwampCoverage): noise repeat in world units and its
// fixed detail-map level (512 px base: level 3 is ~.37 units per texel),
// share moved per unit of noise (algae mask, turbulence), how fast that
// reach fades in from share 0 and 1, and the half-width of the soft edge.
static const float liquidSwampEdgeTile = 24;
static const float liquidSwampEdgeLevel = 3;
static const float2 liquidSwampEdgeNoise = float2(1.5,.5);
static const float liquidSwampEdgeTaper = 3;
static const float liquidSwampEdgeSoftness = .12;
static const float liquidAlgaeBrightness = .55;
static const float liquidLavaDrift = .12; // world units per second
// Seamless detail has no texture/UV/frame/screen input. Octaves are rotated
// relative to each other; derivatives below are analytic world-space slopes.
static const float2 liquidDetailRotation = float2(.8,.6);
static const float3 liquidHeightWeights = float3(.57,.29,.14);
static const float2 liquidColorDetail = float2(.92,1.08);
// Procedural replacement for tile-gated bubbles. Sparse highlights favour dark
// or green/brown colours through blue-channel separation, never a type check.
static const float liquidBubbleScale = .7;
static const float liquidBubbleFineScale = 9;
static const float2 liquidBubblePatch = float2(.82,.94);
static const float2 liquidBubbleSpeckle = float2(.7,.9);
static const float liquidBubbleSpeed = .07;
static const float liquidBubbleBrightness = .16; // @param 0 1
static const float liquidBubbleWhiten = .4;

#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Directional wave field (normal-only; the game keeps geometric displacement).
// Wavelengths in world units; speeds follow deep-water dispersion. Time is the
// game's segment clock; one segment is ~55 ms (tunable visual scale).
static const float liquidSecondsPerTime = .055; // @param 0.01 0.2
static const float liquidGravity = 9.81;
static const float liquidWaveMinimum = .35; // calm water still moves @param 0 1
static const float liquidDetailDrift = .25; // world units per second @param 0 2
static const float liquidShoreSurge = .30; // fraction of shore depth, in and out
static const float liquidShoreSurgeRate = .7; // radians per second
// Sea surf (liquidSurfSwellAt, liquidSurfChop, the surf foam; sea-mask water
// only, scaled by the field's exposure). The look is set live from F10 (the
// b8 surf parameters below the cbuffer: wave height, wavelength, speed,
// choppiness, octaves, crest segments, foam, tint); these are the fixed rest.
// Approach after the stylized water write-up (gameidea.org 2026, "creating a
// stylized 3D water shader": noise-warped choppy octaves stacked at rising
// frequency, finite-difference normals with a distance LOD, foam from depth
// and steep crests through animated fBm-Voronoi clumps, bubbles and a darker
// offset edge, Beer-Lambert depth tint) and a procedural shoreline study
// (lacy scalloped swash edge, thin lace, turquoise shallows, wet sand band).
// Independently written.
static const float liquidSurfBand = 25;        // m; the field's distance scale (liquid_surf.h liquidSurfBand)
static const float liquidSurfLandReach = 8;    // m; the field's land distance scale (liquid_surf.h liquidSurfLandReach)
static const float liquidSurfTessSpacing = .7; // m between new vertices on surf water (terrain tess stages) @param 0.3 2
static const float liquidSurfTessFlag = 2;     // added to a tess corner's limit on surf water
static const float liquidSurfRunupInland = 4;  // m from the field's waterline beyond which the shore ground takes nothing
static const float liquidSurfChopLength = 1.3; // chop base wavelength, share of the swell wavelength @param 0.3 3
static const float liquidSurfChopSpeed = .45;  // chop drift, share of the swell speed @param 0 2
static const float liquidSurfSurge = 4.5;      // m the swell drifts ahead and back (speed variation)
static const float liquidSurfSpacing = .3;     // spacing variation between swell crests, in wavelengths
static const float liquidSurfSetLength = 7;    // crests per set cycle (3-5 large, the rest a lull)
static const float liquidSurfLull = .5;        // height of the crests between sets
static const float liquidSurfRunupRise = .22;  // share of a crest period its swash runs up the beach (the rest it drains)
static const float liquidSurfRunup = .45;      // m the swash climbs above the sea surface per m of crest height @param 0 1.5
static const float liquidSurfWetDarken = .3;   // darkening of wet sand and rock @param 0 0.6
static const float liquidSurfWallTrim = .45;   // m from a steep shore's waterline where the wave height is trimmed (no glass rim)
static const float3 liquidSurfShallowColor = float3(.20,.62,.58); // painterly turquoise of clear shallows
static const float3 liquidSurfDeepColor = float3(.04,.20,.30);    // the deeper blue the absorbed light turns to
static const float3 liquidSurfAbsorb = float3(.9,.22,.14);        // per-channel absorption per m (x the absorption setting)
static const float liquidSurfGlow = .35;                            // light through thin wave tops @param 0 1
static const float3 liquidSurfFoamShadow = float3(.72,.8,.84);   // the darker foam edge, share of the water colour
static const float liquidSurfFoamCut = 1.05;         // pattern + density where foam starts @param 0.8 1.6
static const float liquidSurfFoamEdgeWidth = .06; // the darker edge: how far below the foam cut it reaches @param 0 .2
static const float liquidSurfFoamBubbles = .5;       // how dark the gaps between bubbles inside foam get @param 0 1
static const float3 liquidSurfFoamWhite = float3(.92,.95,.94);    // foam thrown onto sand and rock
// SSR quality/range, in world units. Fixed work budget is a quality setting.
// March steps come from water_reflection_quality (liquidReflectionQuality.x:
// Low 12, Medium 20, High 40) within this range.
static const uint2 liquidSsrSteps = uint2(8,64);
static const uint liquidSsrRefine = 4;
static const float liquidSsrDistance = 32;
static const float liquidSsrStart = .12;
static const float liquidSsrThickness = .2;
static const float liquidSsrEdgeFade = .06;
static const float liquidSsrScreenMiss = 1; // confidence of a miss read from the screen at its end point @param 0 1
static const float liquidSsrDepthThickness = .02; // thickness grows by this share per world unit of view depth @param 0 0.1
// Reflection softness: the reflected cone widens by this much per world unit
// of ray (a smooth surface) plus the ripple slope spread that normal-map
// filtering removed, and the hit colour is read from the matching level of
// the mipped scene copy (liquidReflectionQuality.y levels). Unseen sides read
// a few levels softer.
static const float liquidSsrSpread = .015; // @param 0 0.2
static const float liquidSsrRippleSpread = .6; // per unit of ripple slope deviation @param 0 3
static const float liquidSsrUnseenBlur = 2; // extra mip levels on an unseen side @param 0 5
// Rays pointing back toward the camera mostly meet back faces: fade them.
static const float2 liquidSsrTowardCamera = float2(.7,.95); // view.ray range of the fade @param 0 1
// A mirror ray only meets what stands ahead of its water point along its own
// horizontal travel. A screen sample whose visible surface lies behind that
// AND more than liquidSsrForegroundDepth in front of the ray point (a tree
// or bank between the camera and the water) is transparent to the ray: no
// hit, no crossing, no hidden distance (#138). A visible surface behind the
// water point but close in front of the ray (a deck, plank or bank top that
// overhangs the water) belongs to an object over the ray and stays solid;
// treating it as foreground cut reflections of bridges and banks off along
// a straight line (candidate-40, frame_000089). Margin and depth in world
// units; the depth grows with view depth like the surface thickness.
static const float liquidSsrForegroundMargin = .05;
static const float liquidSsrForegroundDepth = 1.5; // @param 0 10
// Particles (#140): a ray point at most this far behind the previous frame's
// nearest visible particle on its pixel (plus the step length, growing with
// view depth like liquidSsrThickness) is inside the particle's volume.
static const float liquidSsrParticleThickness = 1.5; // world units @param 0 6
static const float liquidSsrParticleConfidence = 1; // @param 0 1
// A crossing behind the visible face of an occluder ahead of the water point
// reflects that occluder's unseen side: its colour at the crossing, shaded,
// with this share of a surface hit's confidence.
static const float liquidSsrUnseenShade = .8; // @param 0 1
static const float liquidSsrUnseenConfidence = .7; // @param 0 1
// A missed ray that ran behind such an occluder without a crossing (it began
// behind it) has nothing to reflect from the screen. It reflects less, fading
// in over this hidden run (world units), so the water's own colour shows
// instead of open sky; a ray that only grazes a silhouette keeps most of it.
static const float liquidSsrHiddenReach = 4; // @param 0.5 20
static const float liquidSsrHiddenKeep = .3; // share of the reflection kept on fully hidden misses @param 0 1

bool liquidSurfaceEnabled(uint terrainFlags) { return (terrainFlags & 8u) != 0; }
float liquidBedTransmission(float depth) {
 // Vertex-only bed coverage cannot locate the dry bank within a triangle.
 // Leave base terrain alone; liquid pixels already own depth absorption.
 return 1;
}
float liquidNoiseHash(float2 p) {
 return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);
}
float liquidNoise(float2 p) {
 float2 cell=floor(p),f=frac(p);f=f*f*(3-2*f);
 return lerp(lerp(liquidNoiseHash(cell),liquidNoiseHash(cell+float2(1,0)),f.x),
             lerp(liquidNoiseHash(cell+float2(0,1)),liquidNoiseHash(cell+1),f.x),f.y);
}
float liquidBandWeight(float cyclesPerPixel) {
 return 1-smoothstep(liquidFilterRange.x,liquidFilterRange.y,cyclesPerPixel);
}
float liquidFilteredNoise(float2 p,float footprint) {
 return lerp(.5,liquidNoise(p),liquidBandWeight(footprint));
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// Current opaque scene only; no history or raster-position jitter.
cbuffer LiquidReflectionFrame:register(b7) {
 row_major float4x4 liquidWorldClip;
 row_major float4x4 liquidInverseWorldClip;
 float4 liquidReflectionTarget; // width, height, enabled, physical
 float4 liquidReflectionQuality; // march steps (water_reflection_quality), highest history mip, history valid, particle depth valid
 row_major float4x4 liquidHistoryClip; // world-to-clip of the previous frame's colour
 row_major float4x4 liquidHistoryInverse; // its inverse
 float4 liquidSsrMarch; // x: largest coarse march step in target pixels (water_reflection_step_pixels; 0: fixed steps)
};
// Slice 0: this frame's world colour at the first liquid batch (refraction).
// Slice 1: the previous frame's finished world colour with mips (reflections;
// it holds the particles and transparent passes drawn after the water, #140).
Texture2DArray<float4> liquidSceneColor:register(t3);
#ifdef EID3D12
// D3D12: one t table serves every stage; the world vertex programs read t19
// (nativeTerrainLayerShifts), so the DXIL reads the scene depth at t112.
Texture2D<float> liquidSceneDepth:register(t112);
#else
Texture2D<float> liquidSceneDepth:register(t19);
#endif
// NATIVE_LIQUID_SWIMMERS: the variants bound while the depth snapshot carries
// swimmer marks; every other variant holds none of this code.
#ifdef NATIVE_LIQUID_SWIMMERS
// The depth snapshot's stencil plane (g): the frame's swimmers (fish) are
// marked in it just before the snapshot (Renderer::mark_swimmers; bit 0 on
// D3D11, any value on D3D12, whose receiver marks are cleared by then).
Texture2D<uint2> liquidSceneStencil:register(t113);
#ifdef EID3D12
#define LIQUID_SWIMMER(mark) ((mark)!=0u)
#else
#define LIQUID_SWIMMER(mark) (((mark)&1u)!=0u)
#endif
#endif
// The previous frame's nearest visible game-particle screen depth (1: none),
// drawn with liquidHistoryClip; particles never write the scene depth.
Texture2D<float> liquidParticleDepth:register(t28);
SamplerState liquidSceneSampler:register(s5);
// Half-resolution reflection (Renderer::trace_water_reflections). The liquid
// surfaces first write, at half resolution, each pixel's ray (t32: direction,
// camera facing) and water point (t31: xyz; w the cone spread per pixel
// footprint, 0 where the liquid takes no screen reflection); nativeLiquidSsrPS
// marches those rays into t29 (colour premultiplied by confidence; a the
// confidence) and t30 (ray, hidden share; w < 0: no usable sample), which the
// water body resolves at full resolution (liquidResolvedReflection).
Texture2D<float4> liquidSsrColor:register(t29);
Texture2D<float4> liquidSsrKey:register(t30);
Texture2D<float4> liquidSsrPlace:register(t31);
Texture2D<float4> liquidSsrRay:register(t32);
static float4 liquidSsrRayOut=0,liquidSsrPlaceOut=0; // the input pass's targets (NATIVE_LIQUID_SSR_INPUT)
// Generated seamless normal map (renderer-owned, full mip chain).
Texture2D<float4> liquidNormalMap:register(t22);
SamplerState liquidNormalSampler:register(s6);
Texture2D<float4> liquidDetailMap:register(t23);
static const float4 liquidNormalTile = float4(11,7,3.4,2.1); // world units per texture repeat @param 0.5 30
static const float4 liquidNormalSpeed = float4(.35,.27,.18,.13); // world units per second @param 0 2
static const float4 liquidNormalWeight = float4(.50,.38,.30,.22); // @param 0 1
static const float liquidNormalAmplitude = .55; // @param 0 2
static const float liquidCrestFoam = .35; // whitecap foam on the steepest wave slopes @param 0 1
static const float liquidReflectMinimum = .22; // steep top-down camera still shows reflections @param 0 1
static const float liquidFlowPeriod = 90; // game time units per flow phase (~5 s) @param 10 300
static const float liquidFlowPhaseTile = 40; // world units; desynchronises flow phases @param 5 120
static const float liquidSsrFlatten = .4; // share of the wave normal used for SSR rays @param 0 1
// Water repeat breakup (#139): warp tile and its largest shift (world units);
// world units per pixel where the distant terms start and are full (close
// top-down play stays below the start); patch and swell tiles (no multiples
// of the normal tiles); ripple strength range near and far; swell weight.
// Both broad fields read a blurred detail-map level, so only its base octave
// (3 periods per tile: ~40 m waves at the warp tile) remains and the warp's
// slope stays near 0.2.
static const float liquidBroadLevel = 4.5; // detail map mip (512 px base) @param 0 9
static const float liquidWarpTile = 120; // @param 20 400
static const float liquidWarpAmount = 3; // @param 0 8
static const float2 liquidFarBreakup = float2(.08,.3); // @param 0 1
static const float liquidFarPatchTile = 170; // @param 20 400
static const float liquidFarSwellTile = 29; // @param 5 100
static const float2 liquidNearPatchRange = float2(.75,1.2); // @param 0 2
static const float2 liquidFarPatchRange = float2(.45,1.35); // @param 0 2
static const float liquidFarSwellWeight = .45; // @param 0 1
// Running water follows the liquid surface's own downhill slope (level lakes
// have none). Speeds are metres per game second. River phases restart every
// liquidRiverPeriod everywhere, so the two-phase flow never shears. At 0.9 s
// a slow river moved its ripples ~0.3 m between crossfades of two unrelated
// patterns, so the surface morphed in place ("pulsating jelly"): frames 0.1 s
// apart correlated at 0.1-0.4 with no coherent shift. A phase now lasts long
// enough for the pattern to visibly travel; liquidRiverTravel caps the
// travel per phase on fast water, where a longer advection would stretch
// the texture across speed changes and per-area phase offsets.
static const float2 liquidRiverOnset = float2(.015,.08); // surface slope where water starts to run @param 0 0.5
static const float3 liquidRiverSpeed = float3(.5,2.5,4); // at slope 0, added per unit slope, maximum @param 0 8
static const float liquidRiverPeriod = 2.4; // game seconds per river flow phase @param 0.2 6
static const float liquidRiverTravel = 3.5; // longest ripple travel in one phase (world units) @param 0.5 10
static const float liquidRiverStreak = .15; // world units of streak per m/s @param 0 0.5
static const float liquidRiverNormal = 1.3; // ripple amplitude of fast running water (rapids; slow rivers keep 1) @param 0 3
static const float2 liquidRiverFoam = float2(.1,.8); // foam amount of gentle and steep running water (rapids) @param 0 1.5
// Running water breaks white where it runs shallow: along its banks and over
// rocks and bars near the surface (water depth over the bed, world units).
static const float2 liquidRiverShallows = float2(.06,.4); // depth of full foam and of none @param 0 1.5
static const float liquidRiverShallowFoam = .45; // foam amount of fully running shallows @param 0 1.5
// The decoded slope is smoothed over 5x5 vertices: zone11 cascades peak ~0.8-1.
static const float2 liquidWaterfallSlope = float2(.55,1.1); // surface slope ramping to a full waterfall @param 0 2
static const float liquidCascadeFoam = .95; // foam amount where a cascade source is 1 (impact pools) @param 0 1.5
static const float liquidBoilPeriod = 1.4; // game seconds per churning-foam cycle on impact pools @param 0.2 5
static const float liquidFoamTile = 3.1; // world units per coarse foam patch repeat @param 0.3 10
static const float liquidFoamFineTile = 3.2; // world units per fine foam breakup repeat @param 0.1 4
static const float liquidFoamBrightness = .9; // @param 0 2
static const float liquidFoamOpacity = .9; // @param 0 1
// Footprint stretched by `streak` world units along the flow: anisotropic
// filtering averages the pattern into streaks without rotating coordinates.
float4 liquidStreakGradient(float4 g,float2 dir,float streak) {
 float a=dot(g.xy,dir),b=dot(g.zw,dir),s=streak/max(abs(a)+abs(b),1e-6);
 return float4(g.xy+dir*a*s,g.zw+dir*b*s);
}
// Explicit world-space gradients (dx.xy, dy.xy) keep anisotropic filtering at
// grazing angles and stay valid outside pixel shaders.
float2 liquidRotate(float2 p,float2 r) {
 return float2(r.x*p.x-r.y*p.y,r.y*p.x+r.x*p.y);
}
// xy: slope; z: length of the mip-averaged unit normal (1 when unfiltered).
float3 liquidNormalAt(float2 uv,float tile,float4 gradient,float2 rotation=float2(1,0)) {
 float4 n=liquidNormalMap.SampleGrad(liquidNormalSampler,uv,liquidRotate(gradient.xy,rotation)/tile,
                                     liquidRotate(gradient.zw,rotation)/tile);
 return float3(n.xy*2-1,n.a);
}
// Toksvig: a filtered normal of length a hides slope variance ~(1-a)/a.
float liquidNormalVariance(float a) { return (1-a)/max(a,.05); }
// Detail map: R lava veins, G lava heat, B fine turbulence, A swamp algae mask.
float4 liquidDetailAt(float2 uv,float tile,float4 gradient,float2 rotation=float2(1,0)) {
 return liquidDetailMap.SampleGrad(liquidNormalSampler,uv,liquidRotate(gradient.xy,rotation)/tile,
                                 liquidRotate(gradient.zw,rotation)/tile);
}
// Swamp coverage from the smoothed per-vertex swamp share: a soft threshold
// at one half, moved by world-space detail noise (algae mask, fine
// turbulence) so the edge between algae and clear water is irregular. One
// fixed mip level: the edge is the same at every distance. The noise fades
// out toward share 0 and 1, so open water and swamp interiors stay exact.
float liquidSwampCoverage(float2 position,float share) {
 [branch]if(share<=0||share>=1)return share;
 float4 n=liquidDetailMap.SampleLevel(liquidNormalSampler,
                                      liquidRotate(position,float2(.8,-.6))/liquidSwampEdgeTile+.37,liquidSwampEdgeLevel);
 float reach=saturate(min(share,1-share)*liquidSwampEdgeTaper);
 float edge=share+((n.a-.5)*liquidSwampEdgeNoise.x+(n.b-.5)*liquidSwampEdgeNoise.y)*reach;
 return smoothstep(.5-liquidSwampEdgeSoftness,.5+liquidSwampEdgeSoftness,edge);
}
// Four drifting layers of the normal map, weighted into one surface slope
// (xy) plus the slope variance their filtering removed (z).
float3 liquidWaveSlope(float2 base,float2 along,float seconds,float4 gradient) {
 float2 dirB=float2(along.x*.6-along.y*.8,along.x*.8+along.y*.6);
 float2 dirC=float2(along.x*.6+along.y*.8,-along.x*.8+along.y*.6);
 float3 n1=liquidNormalAt((base+along*seconds*liquidNormalSpeed.x)/liquidNormalTile.x,liquidNormalTile.x,gradient);
 float3 n2=liquidNormalAt((liquidRotate(base,float2(.8,.6))+dirB*seconds*liquidNormalSpeed.y)/liquidNormalTile.y+.37,liquidNormalTile.y,gradient,float2(.8,.6));
 float3 n3=liquidNormalAt((base+dirC*seconds*liquidNormalSpeed.z)/liquidNormalTile.z+.61,liquidNormalTile.z,gradient);
 float3 n4=liquidNormalAt((liquidRotate(base,float2(.6,-.8))-along*seconds*liquidNormalSpeed.w)/liquidNormalTile.w+.13,liquidNormalTile.w,gradient,float2(.6,-.8));
 float4 w=liquidNormalWeight;
 return float3(n1.xy*w.x+n2.xy*w.y+n3.xy*w.z+n4.xy*w.w,
  dot(w*w,float4(liquidNormalVariance(n1.z),liquidNormalVariance(n2.z),liquidNormalVariance(n3.z),liquidNormalVariance(n4.z))));
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// Running water's ripples carry no time drift of their own: the river moves
// them only by the bounded two-phase advection. Drifting them along the
// per-vertex flow by unbounded game time turned every bend into angular
// stripes that grew over a session.
float3 liquidRiverSlope(float2 base,float4 gradient) {
 return liquidWaveSlope(base,float2(1,0),0,gradient);
}
// Ripple travel in one river phase at a speed (world units).
float liquidRiverShiftLength(float speed) { return min(speed*liquidRiverPeriod,liquidRiverTravel); }
// Two crossfaded phases keep their contrast: a plain lerp of two unrelated
// patterns loses up to a third of it mid-fade, which reads as breathing.
// Scale a blend's deviation by this (w: weight of the second phase).
float liquidPhaseContrast(float w) { return rsqrt((1-w)*(1-w)+w*w); }
// River flow phases (a, b, blend of b) at game seconds; restart every period.
float3 liquidRiverPhases(float seconds,float phaseOffset) {
 float cycle=seconds/liquidRiverPeriod+phaseOffset,a=frac(cycle);
 return float3(a,frac(cycle+.5),abs(1-2*a));
}
// The river's two advected ripple phases at one point; periodic in game time
// with period liquidRiverPeriod (no unbounded term).
float3 liquidRiverWaves(float2 position,float2 shift,float riverA,float riverB,float riverBlend,
                        float4 gradientA,float4 gradientB) {
 float3 waves=lerp(liquidRiverSlope(position-shift*(riverA-.5),gradientA),
                   liquidRiverSlope(position-shift*(riverB-.5)+float2(3.7,1.9),gradientB),riverBlend);
 return float3(waves.xy*liquidPhaseContrast(riverBlend),waves.z);
}
// Liquid type programs (renderer.cpp, per batch from its meshes' liquid
// vertices): NATIVE_LIQUID_WATER (no swamp or lava vertex: self-illumination
// and swamp share are 0 on every pixel) and NATIVE_LIQUID_LAVA (every liquid
// vertex self-illuminated: no water-like terms; opaque). LIQUID_WATERLIKE is
// the per-pixel test those programs fold to a constant.
#if defined(NATIVE_LIQUID_LAVA)
#define LIQUID_WATERLIKE(s) false
#elif defined(NATIVE_LIQUID_WATER)
#define LIQUID_WATERLIKE(s) true
#else
#define LIQUID_WATERLIKE(s) ((s)<=0)
#endif
bool liquidSsrAllowed(float selfIllumination) { return LIQUID_WATERLIKE(selfIllumination); }
bool liquidProject(float3 p,out float2 uv) {
 float4 clip=mul(liquidWorldClip,float4(p,1));
 uv=clip.xy/max(clip.w,1e-6)*float2(.5,-.5)+.5;
 return clip.w>0&&all(uv>0)&&all(uv<1)&&clip.z>=0&&clip.z<=clip.w;
}
bool liquidRayGap(float3 p,float3 forward,out float gap,out float3 scenePoint,out float2 uv,out bool sky) {
 gap=0;scenePoint=0;sky=false;
 if(!liquidProject(p,uv))return false;
 int2 pixel=int2(uv*liquidReflectionTarget.xy);
 float z=liquidSceneDepth.Load(int3(pixel,0));
 sky=z>=1;
 if(sky)return false;
 // Unproject the sampled pixel centre, not a bilinear silhouette depth.
 float2 ndc=(float2(pixel)+.5)/liquidReflectionTarget.xy*float2(2,-2)+float2(-1,1);
 float4 scene=mul(liquidInverseWorldClip,float4(ndc,z,1));
 if(abs(scene.w)<1e-6)return false;
 scenePoint=scene.xyz/scene.w;
 gap=dot(p-scenePoint,forward);
 return true;
}
// Colour of the surface shown at screen uv, read from the previous frame's
// finished world colour where that surface was on screen then (reprojected
// with the previous camera), else this frame's copy at full detail.
float3 liquidReflectedColor(float2 uv,float level) {
 [branch]if(liquidReflectionQuality.z>.5) {
  int2 pixel=int2(uv*liquidReflectionTarget.xy);
  float z=liquidSceneDepth.Load(int3(pixel,0));
  float2 ndc=(float2(pixel)+.5)/liquidReflectionTarget.xy*float2(2,-2)+float2(-1,1);
  float4 scene=mul(liquidInverseWorldClip,float4(ndc,z,1));
  if(z<1&&abs(scene.w)>1e-6) {
   float4 clip=mul(liquidHistoryClip,float4(scene.xyz/scene.w,1));
   float2 previous=clip.xy/max(clip.w,1e-6)*float2(.5,-.5)+.5;
   if(clip.w>0&&all(previous>0)&&all(previous<1))
    return liquidSceneColor.SampleLevel(liquidSceneSampler,float3(previous,1),level).rgb;
  }
 }
 return liquidSceneColor.SampleLevel(liquidSceneSampler,float3(uv,0),0).rgb;
}
// Colour at a world point seen on screen at uv: the previous frame's finished
// image where that point appeared then, else this frame's copy.
float3 liquidPointColor(float3 world,float2 uv,float level) {
 [branch]if(liquidReflectionQuality.z>.5) {
  float4 clip=mul(liquidHistoryClip,float4(world,1));
  float2 previous=clip.xy/max(clip.w,1e-6)*float2(.5,-.5)+.5;
  if(clip.w>0&&all(previous>0)&&all(previous<1))
   return liquidSceneColor.SampleLevel(liquidSceneSampler,float3(previous,1),level).rgb;
 }
 return liquidSceneColor.SampleLevel(liquidSceneSampler,float3(uv,0),0).rgb;
}
// The Low pixel path (NATIVE_LOW, native_low_pixel_path) runs with water
// reflections off, so the reflection and physical-water code is compiled out.
// The same program draws water batches without the reflection frame (unbound:
// reflections and physical water read as off).
#ifdef NATIVE_LOW
static const bool liquidLowPath=true;
#else
static const bool liquidLowPath=false;
#endif
// rgb, confidence of a screen hit; hidden: 0..1 how long a missed ray ran
// behind screen geometry ahead of its water point, where the screen holds no
// data for what it would reflect.
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Interleaved gradient noise: a per-pixel dither with little low-frequency
// energy, so a jittered march start turns step banding into fine grain.
float liquidPixelDither(float2 pixel) {
 return frac(52.9829189*frac(dot(pixel,float2(.06711056,.00583715))));
}
// pixel: SV_Position.xy (march dither); spread: cone widening per world unit
// of ray (liquidSsrSpread plus ripple spread); towardCamera: dot(view, ray).
float4 liquidSceneReflection(float3 position,float3 direction,float selfIllumination,float footprint,
 float2 pixel,float spread,float towardCamera,out float hidden) {
 hidden=0;
 [branch]if(liquidLowPath||!liquidSsrAllowed(selfIllumination)||liquidReflectionTarget.z<.5)return 0;
 float3 forward=liquidWorldClip[3].xyz;
 if(dot(forward,forward)<1e-8)forward=liquidWorldClip[2].xyz;
 forward=normalize(forward);
 // A full step of per-pixel dither: neighbouring pixels sample the ray at
 // different depths, so no step boundary lines up into a ladder.
 float jitter=liquidPixelDither(pixel);
 float previous=liquidSsrStart,previousGap=-liquidSsrThickness;
 bool previousValid=false;
 // The ray's own horizontal travel; a ray straight up has none and keeps
 // every sample.
 float2 travel=direction.xy*rsqrt(max(dot(direction.xy,direction.xy),1e-12));
 bool upright=dot(direction.xy,direction.xy)<1e-8;
 float hiddenFrom=-1,hiddenLength=0;
 float depthScale=rsqrt(max(dot(liquidWorldClip[3].xyz,liquidWorldClip[3].xyz),1e-12));
 bool afterForeground=false;
 // Particles: the ray in the previous frame's clip space (linear in the ray
 // distance), tested against that frame's particle depth at each step. A
 // ray entering a particle's volume from in front of it (or from a pixel
 // without particles) reflects the finished image there: the smoke, flame
 // or spell as drawn over what lay behind it on screen.
 bool particles=liquidReflectionQuality.w>.5;
 float4 historyStart=mul(liquidHistoryClip,float4(position,1)),historyRay=mul(liquidHistoryClip,float4(direction,0));
 float3 historyForward=liquidHistoryClip[3].xyz;
 historyForward*=rsqrt(max(dot(historyForward,historyForward),1e-12));
 float previousParticleGap=-1;
 bool screenSeen=false,historySeen=false;
 // The march length and its quadratic schedule stay fixed; more steps only
 // shorten each step, so the crossing refinement works alike at every
 // quality.
 uint steps=clamp((uint)liquidReflectionQuality.x,liquidSsrSteps.x,liquidSsrSteps.y);
 // Screen-bounded step count (liquidSsrMarch.x > 0): a ray whose whole march
 // projects short on screen (distant water) takes only as many steps as keep
 // every coarse step within liquidSsrMarch.x target pixels; a ray that
 // already needs the quality's count (near water) keeps it unchanged. The
 // ray is measured in four quarters of the fraction schedule (each holds a
 // quarter of the steps); a quarter's largest step is its length times
 // f_end / f_mid of that quarter (2, 4/3, 6/5, 8/7). Any point behind or at
 // the camera plane keeps the full count.
 [branch]if(liquidSsrMarch.x>0) {
  float2 screen[5];bool projected=true;
  [unroll]for(uint q=0;q<5;++q) {
   float f=q*.25;
   float4 c=mul(liquidWorldClip,float4(position+direction*(liquidSsrStart+liquidSsrDistance*f*f),1));
   projected=projected&&c.w>1e-3;
   screen[q]=c.xy/max(c.w,1e-3)*.5*liquidReflectionTarget.xy;
  }
  [branch]if(projected) {
   float largest=max(max(2*length(screen[1]-screen[0]),4./3*length(screen[2]-screen[1])),
                     max(1.2*length(screen[3]-screen[2]),8./7*length(screen[4]-screen[3])));
   float needed=ceil(4*largest/liquidSsrMarch.x);
   steps=(uint)clamp(needed,(float)min(liquidSsrSteps.x,steps),(float)steps);
  }
 }
 [loop]for(uint step=1;step<=steps;++step) {
  float fraction=(step-jitter)/steps;
  float distance=liquidSsrStart+liquidSsrDistance*fraction*fraction;
  float3 p=position+direction*distance;
  // Past the screen for good: the ray's projection (and with particles its
  // previous-frame projection) lies inside the screen over one interval of
  // the ray, since liquidProject tests only linear clip-space half-spaces.
  // A sample outside an interval the ray was already inside is past it, and
  // a step outside both changes nothing (no hit, no hidden run, no particle),
  // so the march ends there with the same result.
  float2 screenUv;bool onScreen=liquidProject(p,screenUv);
  screenSeen=screenSeen||onScreen;
  bool historyPast=true;
  [branch]if(particles) {
   float4 historyAt=historyStart+historyRay*distance;
   float2 historyAtUv=historyAt.xy/max(historyAt.w,1e-6)*float2(.5,-.5)+.5;
   bool historyOn=historyAt.w>0&&all(historyAtUv>0)&&all(historyAtUv<1);
   historySeen=historySeen||historyOn;historyPast=historySeen&&!historyOn;
  }
  if(screenSeen&&!onScreen&&historyPast)break;
  [branch]if(particles) {
   float4 historyPoint=historyStart+historyRay*distance;
   float2 historyUv=historyPoint.xy/max(historyPoint.w,1e-6)*float2(.5,-.5)+.5;
   float particleGap=-1;
   if(historyPoint.w>0&&all(historyUv>0)&&all(historyUv<1)) {
    int2 particlePixel=int2(historyUv*liquidReflectionTarget.xy);
    float particleZ=liquidParticleDepth.Load(int3(particlePixel,0));
    if(particleZ<1) {
     float2 ndc=(float2(particlePixel)+.5)/liquidReflectionTarget.xy*float2(2,-2)+float2(-1,1);
     float4 particle=mul(liquidHistoryInverse,float4(ndc,particleZ,1));
     if(abs(particle.w)>1e-6)particleGap=dot(p-particle.xyz/particle.w,historyForward);
    }
   }
   float particleLimit=liquidSsrParticleThickness*(1+max(historyPoint.w,0)*rsqrt(max(dot(liquidHistoryClip[3].xyz,liquidHistoryClip[3].xyz),1e-12))*
                       liquidSsrDepthThickness)+(distance-previous);
   [branch]if(particleGap>=0&&particleGap<=particleLimit&&previousParticleGap<0) {
    float edge=min(min(historyUv.x,historyUv.y),min(1-historyUv.x,1-historyUv.y));
    float confidence=saturate(edge/liquidSsrEdgeFade)*saturate(1-distance/liquidSsrDistance)*liquidSsrParticleConfidence*
        (1-smoothstep(liquidSsrTowardCamera.x,liquidSsrTowardCamera.y,towardCamera));
    float level=min(log2(max(distance*spread/max(footprint,1e-4),1)),liquidReflectionQuality.y);
    return float4(liquidSceneColor.SampleLevel(liquidSceneSampler,float3(historyUv,1),level).rgb,confidence);
   }
   previousParticleGap=particleGap;
  }
  float gap;float3 scenePoint;float2 uv;bool sky;
  bool valid=liquidRayGap(p,forward,gap,scenePoint,uv,sky);
  // Foreground (behind the water point along the ray's travel and well in
  // front of the ray): transparent.
  float rayDepth=dot(liquidWorldClip[3],float4(p,1))*depthScale;
  if(valid&&!upright&&dot(scenePoint.xy-position.xy,travel)<liquidSsrForegroundMargin&&
     gap>liquidSsrForegroundDepth*(1+max(rayDepth,0)*liquidSsrDepthThickness)) {
   hiddenFrom=-1;previous=distance;previousValid=false;afterForeground=true;
   continue;
  }
  // Every front-to-behind crossing is refined and judged by its refined gap.
  // No gate on the coarse sample's scene depth: under the steep game camera a
  // camera-facing surface lies nearer than the previous ray point at the
  // coarse sample (by ~2*sin^2(pitch) per unit past the hit), so such a gate
  // dropped every hit early in a step and repeated each object's waterline as
  // one band per step down the water.
  // A ray that comes out from behind a transparent foreground object already
  // behind the next surface ahead met that surface while hidden: the
  // reflection continues there instead of leaving a gap the shape of the
  // object (candidate-40: trees cleared reflections behind them).
  bool resumed=valid&&!previousValid&&afterForeground&&gap>=0;
  if(valid)afterForeground=false;
  if(valid&&((previousValid&&previousGap<0)||resumed)&&gap>=0) {
   float lo=previous,hi=distance,loGap=resumed?-liquidSsrThickness:previousGap;
   [unroll]for(uint refine=0;refine<liquidSsrRefine;++refine) {
    float mid=(lo+hi)*.5,midGap;float3 midScene;float2 midUv;bool midSky;
    if(!liquidRayGap(position+direction*mid,forward,midGap,midScene,midUv,midSky))break;
    if(!upright&&dot(midScene.xy-position.xy,travel)<liquidSsrForegroundMargin&&
       midGap>liquidSsrForegroundDepth*(1+max(dot(liquidWorldClip[3],float4(position+direction*mid,1))*depthScale,0)*liquidSsrDepthThickness))break;
    if(midGap>=0){hi=mid;gap=midGap;uv=midUv;}else{lo=mid;loGap=midGap;}
   }
   // A surface leaves a residual of about the final interval at most. A
   // larger one means the ray went behind the visible face of an occluder
   // ahead of the water point (foreground samples are transparent above):
   // under a bridge deck, into a post, a board or a bank whose facing side the
   // steep camera does not show. The ray meets that occluder's unseen side, so
   // it takes the occluder's colour at the crossing, shaded (#121 fragments,
   // structures over the water reflecting only in broken pieces).
   // Surface thickness grows with view depth (the screen's depth precision and
   // a pixel's world size both do), as in the INSIDE SSR.
   float hiDepth=dot(liquidWorldClip[3],float4(position+direction*hi,1))*depthScale;
   float limit=liquidSsrThickness*(1+max(hiDepth,0)*liquidSsrDepthThickness)+(hi-lo);
   float surface=saturate((limit-gap)/(.25*limit)); // 1: visible surface hit, 0: unseen side
   float hit=hi;
   [branch]if(surface>0) {
    // Secant point inside the final interval, so the hit and its colour do
    // not snap to the bisection grid (2^refine sawtooth stripes per step).
    hit=lerp(lo,hi,saturate(-loGap/max(gap-loGap,1e-6)));
    float2 hitUv;if(liquidProject(position+direction*hit,hitUv))uv=hitUv;
   }
   float edge=min(min(uv.x,uv.y),min(1-uv.x,1-uv.y));
   float confidence=saturate(edge/liquidSsrEdgeFade)*saturate(1-hit/liquidSsrDistance)*
       lerp(liquidSsrUnseenConfidence,1,surface)*
       (1-smoothstep(liquidSsrTowardCamera.x,liquidSsrTowardCamera.y,towardCamera));
   // Cone footprint at the hit in screen pixels picks the scene-copy level:
   // near hits stay sharp, far hits and rippled water read soft.
   float level=log2(max(hit*spread/max(footprint,1e-4),1))+liquidSsrUnseenBlur*(1-surface);
   level=min(level,liquidReflectionQuality.y);
   return float4(liquidReflectedColor(uv,level)*lerp(liquidSsrUnseenShade,1,surface),confidence);
  }
  // Longest run of samples behind geometry ahead of the water point: what
  // the ray would reflect there is hidden from the screen (a bank top, a
  // bridge deck or arch over the water).
  if(valid&&gap>liquidSsrThickness) {
   if(hiddenFrom<0)hiddenFrom=distance;
   hiddenLength=max(hiddenLength,distance-hiddenFrom);
  } else if(valid||sky)hiddenFrom=-1;
  previous=distance;previousGap=gap;previousValid=valid;
 }
 hidden=saturate(hiddenLength/liquidSsrHiddenReach);
 // A miss whose end point appears on open sky (no depth) reads the finished
 // image there: the sky as drawn plus the smoke, flames and other particles
 // over it, which never write depth and so are never hit (#140). Only sky:
 // land or water at that pixel lies along a different line of sight than
 // the ray, and reading it darkened open water around real reflections
 // (abcad51, save1133). Behind geometry or off screen the probe stays.
 [branch]if(hiddenLength<=0) {
  float3 end=position+direction*liquidSsrDistance;
  float2 endUv;
  if(liquidProject(end,endUv)) {
   // Sky coverage around the end point, filtered at the colour's footprint
   // (bilinear over four depth texels that far apart). Neighbouring rays end
   // within a fraction of a pixel of each other, so one nearest depth texel
   // magnified the sky edge's pixel steps into large bright blocks on the
   // water (save1144 shallows).
   float level=min(log2(max(liquidSsrDistance*spread/max(footprint,1e-4),1)),liquidReflectionQuality.y);
   float stride=exp2(level);
   float2 h=endUv*liquidReflectionTarget.xy/stride-.5,f=frac(h);
   int2 maxPixel=int2(liquidReflectionTarget.xy)-1;
   float skyShare=0;
   [unroll]for(uint k=0;k<4;++k) {
    float2 offset=float2(k&1,k>>1);
    int2 endPixel=clamp(int2((floor(h)+offset+.5)*stride),int2(0,0),maxPixel);
    skyShare+=(liquidSceneDepth.Load(int3(endPixel,0))>=1?1:0)*(offset.x?f.x:1-f.x)*(offset.y?f.y:1-f.y);
   }
   [branch]if(skyShare>0) {
    float edge=min(min(endUv.x,endUv.y),min(1-endUv.x,1-endUv.y));
    float confidence=saturate(edge/liquidSsrEdgeFade)*liquidSsrScreenMiss*skyShare*
        (1-smoothstep(liquidSsrTowardCamera.x,liquidSsrTowardCamera.y,towardCamera));
    return float4(liquidPointColor(end,endUv,level),confidence);
   }
  }
 }
 return 0;
}

#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// The half-resolution reflection at this full-resolution pixel: the four
// nearest half-resolution samples, bilinear weights times how alike each
// sample's water point and ray are to this pixel's (a sample across a depth
// edge, on other water or with a different ray counts little), normalised.
// Without any alike sample, the most alike one. Same gate as the march.
static const float liquidSsrResolveReach = 3; // water-point distance scale, in pixel footprints @param 1 8
static const float liquidSsrResolveRayPower = 2; // ray agreement sharpness (soft: the half-resolution rays carry point-sampled wave normals, and a sharp weight flickered between neighbours) @param 1 32
float4 liquidResolvedReflection(float2 pixel,float3 position,float3 direction,float selfIllumination,float footprint,out float hidden) {
 hidden=0;
 [branch]if(liquidLowPath||!liquidSsrAllowed(selfIllumination)||liquidReflectionTarget.z<.5)return 0;
 uint width,height;liquidSsrColor.GetDimensions(width,height);
 float2 h=pixel*.5-.5;int2 base=int2(floor(h));float2 f=h-float2(base);
 float reach=liquidSsrResolveReach*max(footprint,1e-4);
 float4 sum=0,best=0;float hiddenSum=0,total=0,bestHidden=0,bestAlike=0;
 [unroll]for(uint k=0;k<4;++k) {
  int2 offset=int2(k&1,k>>1);
  int3 texel=int3(clamp(base+offset,int2(0,0),int2(width,height)-1),0);
  float4 place=liquidSsrPlace.Load(texel),key=liquidSsrKey.Load(texel);
  if(place.w<=0||key.w<0)continue;
  float3 d=place.xyz-position;
  float alike=exp(-dot(d,d)/(reach*reach))*pow(saturate(dot(key.xyz,direction)),liquidSsrResolveRayPower);
  float w=(offset.x?f.x:1-f.x)*(offset.y?f.y:1-f.y)*alike;
  float4 c=liquidSsrColor.Load(texel);
  sum+=c*w;hiddenSum+=key.w*w;total+=w;
  if(alike>bestAlike){bestAlike=alike;best=c;bestHidden=key.w;}
 }
 [branch]if(total>1e-6){sum/=total;hidden=hiddenSum/total;}
 else{sum=best;hidden=bestHidden;}
 return float4(sum.a>1e-6?sum.rgb/sum.a:float3(0,0,0),sum.a);
}

float liquidShoreEnabled(float selfIllumination) {
 return LIQUID_WATERLIKE(selfIllumination)?1:0;
}
float liquidReflectionWeight(float selfIllumination) {
 return liquidShoreEnabled(selfIllumination);
}
float liquidFresnel(float facing,float selfIllumination) {
 return (liquidF0+(1-liquidF0)*pow(1-saturate(facing),5))*liquidReflectionWeight(selfIllumination);
}
float3 liquidNoiseGradient(float2 p) {
 float2 cell=floor(p),f=frac(p),blend=f*f*(3-2*f),gradient=6*f*(1-f);
 float a=liquidNoiseHash(cell),b=liquidNoiseHash(cell+float2(1,0));
 float c=liquidNoiseHash(cell+float2(0,1)),d=liquidNoiseHash(cell+1);
 return float3(lerp(lerp(a,b,blend.x),lerp(c,d,blend.x),blend.y),
               lerp(b-a,d-c,blend.y)*gradient.x,
               lerp(c-a,d-b,blend.x)*gradient.y);
}
float3 liquidFilteredGradient(float2 p,float footprint) {
 return lerp(float3(.5,0,0),liquidNoiseGradient(p),liquidBandWeight(footprint));
}
float3 liquidMoltenColor(float3 lit,float3 emissive,float heat) {
 // Temperature ramp: dark crust -> deep red (authored hue family) -> orange ->
 // yellow-hot. The authored emissive picks the hue; brightness comes from heat.
 float3 hue=emissive/max(max(emissive.r,max(emissive.g,emissive.b)),1e-3);
 float3 crust=lerp(float3(.07,.01,.004),hue*.07,.2)+lit*.04;
 float3 red=lerp(float3(.62,.06,.02),hue*.62,.2);
 float3 orange=float3(.95,.30,.04),yellow=float3(1,.82,.30);
 return heat<.30?lerp(crust,red,heat/.30):
        (heat<.78?lerp(red,orange,(heat-.30)/.48):lerp(orange,yellow,saturate((heat-.78)/.17)));
}
cbuffer LiquidRippleFrame:register(b8) {
 float4 liquidRippleMeta; // count, precise frame time, game seconds (caustics), smoothed particle rain
 float4 liquidRipplePositionSpeed[16]; // world XYZ, speed
 float4 liquidRipplePhase[16];
 float4 liquidWaveWindow; // origin xy, 1/extent, valid
 float4 liquidRippleFlow[16]; // per unit: river surface gradient at its feet (the wave field's trail current)
 float4 liquidSurfWindow; // sea surf texture: origin xy (world m), 1/extent xy
 float4 liquidSurfMode; // valid, debug view + 2 * water_surf_quality, half texel uv xy
 // Sea surf look, live from F10 (Renderer::prepare_liquid_ripples).
 float4 liquidSurfWaveParams;  // swell crest-to-trough height (m), wavelength (m), speed (m per game second), choppiness
 float4 liquidSurfShapeParams; // chop octaves (High), crest segment length (m), segment randomness 0..1, chop height (m)
 float4 liquidSurfFoamParams;  // shore foam amount, shore foam width (m of water), crest foam threshold (normal z), foam cell size (m)
 float4 liquidSurfLookParams;  // shallow tint, absorption, cliff burst, sea-edge fade (m; applied in the composite on the CPU)
 float4 liquidSurfSwellDir;    // regional swell direction xy (unit, open sea toward the shores), z agreement 0..1, w wind whitecaps 0..1 (renderer_liquid.cpp)
};
// Sea surf field (liquid_surf.h, renderer-owned, one world-space texture over
// the maps with surf; 2 m texels): r exposure to open water (0 on rivers,
// ponds and sheltered lakes; faded out where the sea runs off the covered
// maps), gb the incoming wave direction (from open water toward the shore;
// none out at sea, beyond the coastal band), a the signed distance to the
// waterline (water +, land -).
Texture2D<float4> liquidSurf:register(t62);
// Decoded: r, gb the incoming direction with its length the share of the
// coastal swell (1 in the band, 0 out at sea), a metres.
float4 liquidSurfDecode(float4 s) {
 float2 incoming=s.gb*2-1;float l=length(incoming);
 float d=s.a<.25?(s.a-.25)*4*liquidSurfLandReach:(s.a-.25)/.75*liquidSurfBand;
 return float4(s.r,l>.05?incoming/l*saturate((l-.05)/.25):float2(0,0),d);
}
// Pixel stage: one filtered read.
float4 liquidSurfAt(float2 p) {
 float2 uv=(p-liquidSurfWindow.xy)*liquidSurfWindow.zw;
 if(liquidSurfMode.x<.5||any(uv<0)||any(uv>1))return float4(0,0,0,liquidSurfBand);
 return liquidSurfDecode(liquidSurf.SampleLevel(liquidNormalSampler,clamp(uv,liquidSurfMode.zw,1-liquidSurfMode.zw),0));
}
// Vertex and domain stages (no sampler bound there): the same bilinear
// filter from four texels.
float4 liquidSurfLoad(float2 p) {
 float2 uv=(p-liquidSurfWindow.xy)*liquidSurfWindow.zw;
 if(liquidSurfMode.x<.5||any(uv<0)||any(uv>1))return float4(0,0,0,liquidSurfBand);
 uint width,height;liquidSurf.GetDimensions(width,height);
 float2 t=uv*float2(width,height)-.5;int2 c=int2(floor(t));float2 f=t-float2(c);
 int2 last=int2(width,height)-1;
 float4 a=liquidSurf.Load(int3(clamp(c,0,last),0)),b=liquidSurf.Load(int3(clamp(c+int2(1,0),0,last),0));
 float4 d=liquidSurf.Load(int3(clamp(c+int2(0,1),0,last),0)),e=liquidSurf.Load(int3(clamp(c+1,0,last),0));
 return liquidSurfDecode(lerp(lerp(a,b,f.x),lerp(d,e,f.x),f.y));
}
// The sea surface height (x), shore steepness (y, impact 0 beach .. 1 cliff
// or quay), smooth swell distance (z, m: water a heavily blurred distance to
// land, land minus its distance to water) and smooth water depth (w, m) at p
// (liquid_surf.h level, impact, swell_distance, swell_depth): t63, beside
// t62. Crest geometry follows z, shoaling and breaking w, so crests never
// bend over bumps of the bed. Outside the maps with surf: x far below, y 0,
// z 60, w 0. Pixel stage: one filtered read; vertex and domain stages (no
// sampler there): the same filter from four texels.
Texture2D<float4> liquidSurfLevel:register(t63);
float4 liquidSurfLevelAt(float2 p) {
 float2 uv=(p-liquidSurfWindow.xy)*liquidSurfWindow.zw;
 if(liquidSurfMode.x<.5||any(uv<0)||any(uv>1))return float4(-1e4,0,60,0);
 return liquidSurfLevel.SampleLevel(liquidNormalSampler,clamp(uv,liquidSurfMode.zw,1-liquidSurfMode.zw),0);
}
float4 liquidSurfLevelLoad(float2 p) {
 float2 uv=(p-liquidSurfWindow.xy)*liquidSurfWindow.zw;
 if(liquidSurfMode.x<.5||any(uv<0)||any(uv>1))return float4(-1e4,0,60,0);
 uint width,height;liquidSurfLevel.GetDimensions(width,height);
 float2 t=uv*float2(width,height)-.5;int2 c=int2(floor(t));float2 f=t-float2(c);
 int2 last=int2(width,height)-1;
 float4 a=liquidSurfLevel.Load(int3(clamp(c,0,last),0)),b=liquidSurfLevel.Load(int3(clamp(c+int2(1,0),0,last),0));
 float4 d=liquidSurfLevel.Load(int3(clamp(c+int2(0,1),0,last),0)),e=liquidSurfLevel.Load(int3(clamp(c+1,0,last),0));
 return lerp(lerp(a,b,f.x),lerp(d,e,f.x),f.y);
}
// Integer lattice hash (exact at any world coordinate, unlike a sine hash).
uint liquidSurfHashU(int2 c) {
 uint h=uint(c.x)*0x8da6b343u^uint(c.y)*0xd8163841u;
 h^=h>>15;h*=0x2c1b3c6du;h^=h>>12;h*=0x297a2d39u;h^=h>>15;
 return h;
}
float liquidSurfHash(int2 c) { return float(liquidSurfHashU(c)>>8)*(1.0/16777216.0); }
float2 liquidSurfHash2(int2 c) { uint h=liquidSurfHashU(c); return float2(h&0xffffu,h>>16)*(1.0/65536.0); }
// Smooth value noise -1..1 (x) and its gradient (yz).
float3 liquidSurfNoiseGrad(float2 p) {
 float2 i=floor(p),f=p-i,u=f*f*(3-2*f),du=6*f*(1-f);int2 c=int2(i);
 float a=liquidSurfHash(c),b=liquidSurfHash(c+int2(1,0)),d=liquidSurfHash(c+int2(0,1)),e=liquidSurfHash(c+int2(1,1));
 return float3(lerp(lerp(a,b,u.x),lerp(d,e,u.x),u.y)*2-1,
               2*du.x*lerp(b-a,e-d,u.y),2*du.y*lerp(d-a,e-b,u.x));
}
float liquidSurfNoise(float2 p) {
 float2 i=floor(p),f=p-i;f=f*f*(3-2*f);int2 c=int2(i);
 return lerp(lerp(liquidSurfHash(c),liquidSurfHash(c+int2(1,0)),f.x),
             lerp(liquidSurfHash(c+int2(0,1)),liquidSurfHash(c+1),f.x),f.y)*2-1;
}
// 0..1 value noise over the plane and time (foam patches pulsing).
float liquidSurfNoise3(float3 p) {
 float3 i=floor(p),f=p-i;f=f*f*(3-2*f);int3 c=int3(i);
 int2 a=c.xy+int2(c.z*7919,-c.z*104729),b=a+int2(7919,-104729);
 float lower=lerp(lerp(liquidSurfHash(a),liquidSurfHash(a+int2(1,0)),f.x),lerp(liquidSurfHash(a+int2(0,1)),liquidSurfHash(a+1),f.x),f.y);
 float upper=lerp(lerp(liquidSurfHash(b),liquidSurfHash(b+int2(1,0)),f.x),lerp(liquidSurfHash(b+int2(0,1)),liquidSurfHash(b+1),f.x),f.y);
 return lerp(lower,upper,f.z);
}
// Nearest (x) and second nearest (y) distance to a cell grid's feature
// points, each circling slowly on t, so foam pulses and reshapes instead of
// sliding as a sheet. Isotropic: never stretched along an axis.
float2 liquidSurfVoronoi(float2 uv,float t) {
 float2 i=floor(uv),f=uv-i;int2 c=int2(i);float first=8,second=8;
 [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x) {
  float2 h=liquidSurfHash2(c+int2(x,y));
  float2 r=float2(x,y)+.5+.4*sin(t+6.2831853*h)-f;
  float d=dot(r,r);
  if(d<first){second=first;first=d;}else second=min(second,d);
 }
 return sqrt(float2(first,second));
}
// Foam of a given density 0..1 at foam-cell coordinates fp, after the
// stylized-water write-up's structure (foam level added to a drifting
// pattern, cut sharply; emergence noise; an offset darker edge) with the
// shoreline study's texture: the pattern is four octaves of domain-warped,
// drifting value noise (irregular holes and wisps at every scale, never a
// grid of round dots), with a weak share of circling Voronoi centres for
// some clumping. Thin density leaves scattered lacy patches, full density a
// sheet with irregular holes. Thick foam gets a few small, irregular bubble
// holes. Far away (detail 0) its mean.
// x foam 0..1, y the darker edge just outside it, z thickness 0..1.
float liquidSurfFoamClumps(float2 uv,float t,float fine) {
 float v=0,a=.5;float2 p=uv;
 [unroll]for(int k=0;k<4;++k) {
  v+=a*(liquidSurfNoise(p+t*float2(.031,-.023)*(k+1))*.5+.5);
  p=float2(p.x*.8-p.y*.6,p.x*.6+p.y*.8)*2.07+float2(17.3,-9.1);
  a*=.5;
 }
 // Close up: two finer octaves (a few cm) so the edge and holes stay crisp
 // instead of a blurry enlarged pattern; their mean stands in far away.
 [branch]if(fine>0) {
  float f=liquidSurfNoise(p)*.5+.5;p=float2(p.x*.8-p.y*.6,p.x*.6+p.y*.8)*2.07+float2(4.7,13.9);
  f+=.5*(liquidSurfNoise(p)*.5+.5);
  v+=a*lerp(.75,f,fine);
 } else v+=a*.75;
 float c=saturate(1-liquidSurfVoronoi(uv*.9,t).x*1.1);
 return v*.8+.25*c*c; // about 0 .. 1, mean ~.4
}
float3 liquidSurfFoamPattern(float2 fp,float density,float t,float detail,uint quality,float cells) {
 [branch]if(density<=.01)return 0;
 float level=saturate(density);
 float3 mean=float3(smoothstep(.25,1,level)*.8,0,level);
 [branch]if(detail<=.01)return mean;
 float2 warp=float2(liquidSurfNoise(fp*.21+t*.04),liquidSurfNoise(fp*.21+float2(5.2,1.3)-t*.035))*.9;
 // cells: foam cells per pixel (footprint / cell size). Crisp edges and
 // fine octaves close up, soft far away.
 float fine=1-smoothstep(.02,.08,cells);
 float soft=clamp(cells*2.5,.025,.12);
 float emerge=smoothstep(0,1,liquidSurfNoise3(float3(fp*.25,t*.2))*.5+.5);
 float present=lerp(emerge,1,level);
 float main=liquidSurfFoamClumps(fp+warp,t,fine)*present+level;
 float foam=smoothstep(liquidSurfFoamCut,liquidSurfFoamCut+soft,main);
 // The darker edge: a rim just outside the foam, from the same pattern at a
 // lower cut (a second offset evaluation of the whole pattern cost about as
 // much as the foam itself on open sea).
 float edge=saturate(smoothstep(liquidSurfFoamCut-liquidSurfFoamEdgeWidth,liquidSurfFoamCut-liquidSurfFoamEdgeWidth+soft,main)-foam);
 float thick=saturate((main-liquidSurfFoamCut)*2);
 float bubbles=1;
 [branch]if(quality>0) {
  float b=liquidSurfNoise((fp+warp)*5.3+float2(t*.1,0))*.5+.5;
  bubbles=1-liquidSurfFoamBubbles*smoothstep(.64,.8,b)*smoothstep(.3,1,thick);
 }
 return lerp(mean,float3(foam*bubbles,edge,foam*thick),detail);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Octave counts from the chop octave setting and water_surf_quality (High:
// the setting, Medium one fewer, Low two fewer, at least one); the domain
// stage displaces at most 1/2/3 of them, the pixel stage takes all for its
// normals.
uint liquidSurfOctaves(bool geometry) {
 int quality=int(min(floor(liquidSurfMode.y*.25),2)); // mode.y = debug + 4 * quality
 int count=max(int(clamp(round(liquidSurfShapeParams.x),1,6))-(2-quality),1);
 return uint(geometry?min(count,quality+1):count);
}
// One chop octave: two rounded cusp-crested waves 60 degrees apart, each
// running at its own phase (phase.xy), over noise-warped coordinates whose
// warp evolves in place; raised to the choppiness: peaked crests over broad
// troughs. 0..1.
float liquidSurfOctave(float2 uv,float choppy,float2 phase) {
 uv+=liquidSurfNoise(uv*.6+phase*.05)*1.1;
 float sa=sin(uv.x+phase.x),sb=sin(dot(uv,float2(.5,.8660254))+phase.y);
 // A rounded |sin|: no crease along the crest (creases drew hair lines).
 float a=(1-sqrt(sa*sa+.02))/.8586,b=(1-sqrt(sb*sb+.02))/.8586;
 return pow(max((a+b)*.5,1e-4),choppy);
}
// The sea chop height (m, mean zero) at p: octaves of liquidSurfOctave, each
// rotated, about twice the frequency and a quarter of the height of the
// last, a little rounder. Each octave is two copies running opposite ways
// at its own (deep-water) phase speed, so the chop churns in place instead
// of one pattern sliding toward the shore. Each octave fades out as its
// waves near the pixel footprint (the distance LOD: no shimmer far away) and
// the loop stops once one is gone. The domain stage passes footprint 0.
float liquidSurfChop(float2 p,float seconds,uint count,float footprint) {
 float freq0=3.14159265/max(liquidSurfWaveParams.y*liquidSurfChopLength,.5),freq=freq0;
 float amp=liquidSurfShapeParams.w*.5,choppy=max(liquidSurfWaveParams.w,1);
 float omega=seconds*liquidSurfWaveParams.z*liquidSurfChopSpeed*freq0;
 float2 uv=p*float2(.8,1);float h=0;
 [loop]for(uint k=0;k<count;++k) {
  float fade=liquidBandWeight(footprint*freq*.31830989);
  [branch]if(fade<=0)break;
  float mean=1/(1.26+.825*choppy+.665*choppy*choppy); // the octave's mean over the plane
  float w=omega*sqrt(freq/freq0)*(1+.13*k);
  float o=liquidSurfOctave(uv*freq,choppy,float2(w,-.8*w))+liquidSurfOctave(uv*freq+float2(1.7,4.1),choppy,float2(-.9*w,w));
  h+=(o-2*mean)*amp*fade;
  uv=float2(uv.x*.8-uv.y*.6,uv.x*.6+uv.y*.8);
  freq*=2.1;amp*=.27;choppy=lerp(choppy,1,.25);
 }
 return h;
}
// liquidSurfOctave (x) with its gradient over uv (yz), through the warp, the
// two waves, the rounding and the power.
float3 liquidSurfOctaveGrad(float2 uv,float choppy,float2 phase) {
 float3 n=liquidSurfNoiseGrad(uv*.6+phase*.05);
 float2 dn=n.yz*.6*1.1; // d(warp)/d(uv), the same for both warped components
 float2 w=uv+n.x*1.1;
 const float2 dir=float2(.5,.8660254);
 float sa=sin(w.x+phase.x),sb=sin(dot(w,dir)+phase.y);
 float ra=sqrt(sa*sa+.02),rb=sqrt(sb*sb+.02);
 float a=(1-ra)/.8586,b=(1-rb)/.8586,m=(a+b)*.5;
 float o=pow(max(m,1e-4),choppy);
 float dm=m>1e-4?choppy*o/m:0;
 float2 dw=dm*.5*(-(sa/ra)*cos(w.x+phase.x)*float2(1,0)-(sb/rb)*cos(dot(w,dir)+phase.y)*dir)/.8586;
 return float3(o,dw+dn*(dw.x+dw.y));
}
// Pixel stage: the chop height (x) and its slope (yz) in one pass, the
// octaves differentiated analytically (each octave's coordinates are a
// rotated and scaled copy of p). Forward differences evaluated the whole
// chop three times per pixel (the largest share of the sea shading cost).
float3 liquidSurfChopGrad(float2 p,float seconds,uint count,float footprint) {
 float freq0=3.14159265/max(liquidSurfWaveParams.y*liquidSurfChopLength,.5),freq=freq0;
 float amp=liquidSurfShapeParams.w*.5,choppy=max(liquidSurfWaveParams.w,1);
 float omega=seconds*liquidSurfWaveParams.z*liquidSurfChopSpeed*freq0;
 float2 uv=p*float2(.8,1);float2x2 m=float2x2(.8,0,0,1); // uv = m p
 float3 h=0;
 [loop]for(uint k=0;k<count;++k) {
  float fade=liquidBandWeight(footprint*freq*.31830989);
  [branch]if(fade<=0)break;
  float mean=1/(1.26+.825*choppy+.665*choppy*choppy);
  float w=omega*sqrt(freq/freq0)*(1+.13*k);
  float3 o=liquidSurfOctaveGrad(uv*freq,choppy,float2(w,-.8*w))+liquidSurfOctaveGrad(uv*freq+float2(1.7,4.1),choppy,float2(-.9*w,w));
  float s=amp*fade;
  h.x+=(o.x-2*mean)*s;
  h.yz+=mul(o.yz,m)*freq*s; // d/dp = (d/dq) * freq * m
  uv=float2(uv.x*.8-uv.y*.6,uv.x*.6+uv.y*.8);
  m=mul(float2x2(.8,-.6,.6,.8),m);
  freq*=2.1;amp*=.27;choppy=lerp(choppy,1,.25);
 }
 return h;
}
float2 liquidSurfChopSlope(float2 p,float seconds,uint count,float footprint) {
 return liquidSurfChopGrad(p,seconds,count,footprint).yz;
}
// Share of the chop: full on open water, calmer in the breaking shallows
// (the swell carries the shore there) and none at the waterline; at a steep
// shore it keeps its height up to the wall's foot.
float liquidSurfChopWeight(float4 field,float4 level,float depth) {
 float wall=smoothstep(.1,.4,level.y),deep=level.w;
 return field.r*lerp(smoothstep(.04,.6,depth)*lerp(.45,1,smoothstep(.6,2.5,deep)),
                     smoothstep(liquidSurfWallTrim*.6,liquidSurfWallTrim*1.6,max(field.a,0)),wall);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// The swell: crests rolling in toward the shore along the smooth swell
// distance (t63 z). Each crest's arrival is shifted along the coast by
// smooth noise over the shore point it heads for (liquidSurfOffset), drifting
// slowly in time, and within its own cycle by a per-crest seeded warp
// (liquidSurfCrestWarp), so every crest lands, breaks and spaces itself
// differently, at full height, never one front running along the coast nor
// the same shape every time. Each crest has its own size (sets, lulls, a
// segment envelope reseeded per crest), grows and steepens as the smooth
// depth shoals, breaks where the water is about as deep as it is high and
// runs out as a low bore; against a steep shore it rises on the rock and is
// trimmed only at the wall's foot.
struct LiquidSurfSwell {
 float height;float2 slope; // m along world up, and its gradient
 float crest;    // 0..1 the crest shape here (1 on a top)
 float front;    // 0..1 on the shoreward face of a crest
 float behind;   // 0 at a top .. 1 at the next trough, behind it
 float breaking; // 0..1 how far the crests here have broken
 float wave;     // 0..1 this crest's size
 float seed;     // 0..1 this crest's own seed (its foam layout)
};
float liquidSurfTravel(float seconds) {
 // Irregular arrivals: the swell surges ahead and slackens (two slow noises),
 // so crests never land on one steady beat.
 return seconds*liquidSurfWaveParams.z+liquidSurfSurge*(liquidSurfNoise(float2(seconds*.045,7.3))+.6*liquidSurfNoise(float2(seconds*.11,2.9)));
}
// x the along-coast arrival offset (wavelengths), yz its gradient (per m).
float3 liquidSurfOffset(float2 shore,float seconds) {
 float segment=max(liquidSurfShapeParams.y,2);
 float3 a=liquidSurfNoiseGrad(shore/segment+float2(seconds*.012,-seconds*.009));
 float3 b=liquidSurfNoiseGrad(shore/(segment*.43)+float2(5.2,1.3)+float2(-seconds*.017,seconds*.013));
 // Up to ~1.6 wavelengths, held to about half a wavelength per segment so the
 // crests stay mostly parallel to the coast (oblique, never side-on).
 float k=liquidSurfShapeParams.z*min(1.6,.55*segment/max(liquidSurfWaveParams.y,1));
 return float3(a.x+.3*b.x,a.yz/segment+.3*b.yz/(segment*.43))*k;
}
// Per-crest warp of its own cycle (wave n at a shore point): how far its top
// is moved ahead or back within the cycle (|x| < 1/pi keeps the cycle
// monotone), from noise reseeded for every crest; yz its gradient (per m).
float3 liquidSurfCrestWarp(float n,float2 shore,float seconds) {
 float segment=max(liquidSurfShapeParams.y,2)*.8;
 float3 g=liquidSurfNoiseGrad(shore/segment+liquidSurfHash2(int2(int(n),433))*97+seconds*.01);
 float k=.28*liquidSurfShapeParams.z;
 return float3(g.x,g.yz/segment)*k;
}
// Size of crest n at a shore point: 3-5 large crests per set and a lull,
// per-crest randomness, and its own segment envelope (crests about a
// segment long with low gaps, a fresh layout for every crest, drifting).
float liquidSurfCrestSize(float n,float2 shore,float seconds) {
 float set=floor(n/liquidSurfSetLength);
 float big=(n-set*liquidSurfSetLength)<lerp(3,5.99,liquidSurfHash(int2(int(set),57)))?1:liquidSurfLull;
 float segment=max(liquidSurfShapeParams.y,2)*lerp(.6,1.4,liquidSurfHash(int2(int(n),2903)));
 float2 jitter=liquidSurfHash2(int2(int(n),911))*61;
 float envelope=smoothstep(-.35,.35,liquidSurfNoise(shore/segment+jitter+seconds*float2(.008,-.006)));
 // A quarter of the crests barely form, so arrivals come in uneven groups.
 float h=liquidSurfHash(int2(int(n),1713));
 float form=h<.25?.12:lerp(.55,1.25,(h-.25)/.75);
 return big*form*lerp(1,lerp(.1,1,envelope),liquidSurfShapeParams.z);
}
// The regular phase along the smooth swell distance d plus travel, its
// spacing warp and the along-coast offset (x), and its gradient (yz, per m).
// Crests follow the heavily smoothed shore distance (liquid_surf.h
// swell_distance), so they always come in toward each shore and only gently
// curve in bays (a plane swell along one regional direction ran along coasts
// not facing it).
// gradient: that of d (per m); shore: where the along-coast noise is read.
float3 liquidSurfShorePhase(float d,float2 gradient,float2 shore,float seconds) {
 float lambda=max(liquidSurfWaveParams.y,1);
 float z=(d+liquidSurfTravel(seconds))/lambda;
 float3 warp=liquidSurfNoiseGrad(float2(z*.37,3.1)); // spacing variation; monotone: .37*2*1.5*spacing < 1
 float3 offset=liquidSurfOffset(shore,seconds);
 return float3(z+liquidSurfSpacing*warp.x+offset.x,
               gradient*(1+liquidSurfSpacing*.37*warp.y)/lambda+offset.yz);
}
// gradient: that of the smooth swell distance level.z at p (per m).
LiquidSurfSwell liquidSurfSwellAt(float2 p,float4 field,float4 level,float2 gradient,float seconds,float depth) {
 LiquidSurfSwell s=(LiquidSurfSwell)0;
 float band=length(field.gb),d=max(level.z,0);
 // Shore-distance crests stay in the band.
 float reach=band*(1-smoothstep(liquidSurfBand*.6,liquidSurfBand,max(field.a,0)));
 [branch]if(reach<=0)return s;
 // The along-coast noise (offset, crest warp and size) is read at p, not at
 // the shore point p+incoming*d: out at sea d is tens of metres, so where
 // the stored direction is weak or noisy (where shore directions disagree)
 // a small error in it swung that point far and squeezed the crests into
 // tight bunched stripes (save1146, Gipat south-west). For the same reason
 // the slope takes the distance field's own gradient, not -incoming.
 float2 shore=p;
 float3 phase=liquidSurfShorePhase(d,gradient,shore,seconds);
 float n=floor(phase.x),y0=phase.x-n;
 // This crest's own warp of its cycle (ends fixed, so neighbours join).
 float3 cw=liquidSurfCrestWarp(n,shore,seconds);
 float y=y0+cw.x*sin(3.14159265*y0);
 float2 gradY=(1+cw.x*3.14159265*cos(3.14159265*y0))*phase.yz+sin(3.14159265*y0)*cw.yz;
 float size=liquidSurfCrestSize(n,shore,seconds);
 float height=liquidSurfWaveParams.x*field.r*size;
 float wall=smoothstep(.1,.4,level.y),deep=max(level.w,0);
 // Shoaling, then breaking at a smooth depth about the crest's height
 // (each crest at its own depth: its height differs).
 float shoal=lerp(1,1.45,1-smoothstep(.5,3,deep));
 float breakDepth=height*shoal*1.25;
 s.breaking=(1-smoothstep(breakDepth*.55,breakDepth*1.1+.05,deep))*(1-wall);
 float amplitude=height*shoal*lerp(1,.4,s.breaking*s.breaking)*reach;
 // A steep shore: higher against the rock in the last metres, trimmed only
 // at the wall's foot. A beach: flat only at the drawn waterline (the swash).
 float fromWall=max(field.a,0);
 // Steep shore: the coarse water mesh cannot climb a wall, so its lifted
 // vertices stood as spiky triangles against the rock; the wave fades out
 // over the last ~2.5 m and the impact is carried by foam on the water.
 amplitude*=lerp(smoothstep(.02,.3,depth),smoothstep(.6,2.5,fromWall),wall);
 // Shape: a rising front (shoreward, steeper as it breaks), a rounded top
 // and a long smooth back; peaked a little (broad troughs).
 float front=lerp(.36,.1,s.breaking)*lerp(1,.7,wall);
 float h,dh;
 if(y<front){float t=y/front;h=t*t*(3-2*t);dh=6*t*(1-t)/front;}
 else{float t=(y-front)/(1-front);h=.5+.5*cos(3.14159265*t);dh=-1.5707963*sin(3.14159265*t)/(1-front);}
 float hp=pow(max(h,1e-4),1.4),dhp=1.4*hp/max(h,1e-4)*dh;
 s.height=amplitude*(hp-.33);
 s.slope=amplitude*dhp*gradY;
 s.crest=hp;
 s.front=y<front?smoothstep(0,1,y/front):1-smoothstep(0,.08,(y-front)/(1-front));
 s.behind=y<front?0:(y-front)/(1-front);
 s.wave=saturate(size)*reach;
 s.breaking*=reach;
 s.seed=liquidSurfHash(int2(int(n),5521));
 return s;
}
// The crests arriving at the waterline at a shore point (the swell's own
// phase at distance 0 with each crest's warp, so the swash and the bursts
// land where and when its crests do): x time since the last arrival (crest
// periods 0..1), y that crest's size 0..1 (x exposure), z its seed.
float3 liquidSurfArrival(float2 shore,float2 incoming,float exposure,float seconds) {
 float phase=liquidSurfShorePhase(0,-incoming,shore,seconds).x;
 float n=floor(phase),y0=phase-n;
 float y=y0+liquidSurfCrestWarp(n,shore,seconds).x*sin(3.14159265*y0);
 float arrived=y>=.1?n:n-1; // broken crests: the top passes about .1 into the cycle
 return float3(frac(y-.1),saturate(liquidSurfCrestSize(arrived,shore,seconds))*exposure,liquidSurfHash(int2(int(arrived),5521)));
}
// The sea surf surface height (m along world up) at p for the domain stage:
// the swell and the geometry octaves of the chop.
float liquidSurfHeight(float2 p,float4 field,float4 level,float seconds,float depth) {
 return liquidSurfSwellAt(p,field,level,float2(0,0),seconds,depth).height+
        liquidSurfChop(p,seconds,liquidSurfOctaves(true),0)*liquidSurfChopWeight(field,level,depth);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// The shore ground above the waterline (terrain pixel stage), elevation the
// ground's height above the sea surface (m), normal the ground's, footprint
// its metres per pixel: x the thin swash sheet, y its lacy foam edge and the
// lace it leaves on the sand, z wet sand or rock, w white water and spray
// thrown up a steep face. Each arriving crest (liquidSurfArrival) pushes the
// swash up the beach, where it slows, stops and drains, each stretch of shore
// on its own; on a steep shore it bursts up the face and falls back.
float4 liquidSurfShoreAt(float2 p,float4 field,float4 level,float seconds,float elevation,float3 normal,float footprint) {
 float band=length(field.gb);
 float2 incoming=band>1e-3?field.gb/band:float2(0,0);
 float2 shore=p+incoming*min(level.z,0); // < 0 on land: back to the waterline
 float3 arrival=liquidSurfArrival(shore,incoming,field.r,seconds);
 float since=arrival.x,height=liquidSurfWaveParams.x*arrival.y;
 float wall=smoothstep(.1,.4,level.y),cell=max(liquidSurfFoamParams.w,.2);
 float detail=liquidBandWeight(footprint/cell);
 uint quality=uint(min(floor(liquidSurfMode.y*.25),2));
 float2 fp=p/cell+arrival.z*41; // a fresh foam layout per crest
 float scallop=lerp(.5,liquidSurfVoronoi(p/cell*.5,seconds*.2).x,detail); // scallops along the swash edge
 float4 o=0;
 // Beach: the sheet runs up fast, slows, stops and drains; its foam edge
 // rides its top, lace and wet sand stay behind it.
 float flat=smoothstep(.55,.85,normal.z)*(1-wall);
 float rise=since<liquidSurfRunupRise?sin(1.5707963*since/liquidSurfRunupRise):1-smoothstep(liquidSurfRunupRise,liquidSurfRunupRise+.6,since);
 float top=liquidSurfRunup*height*(.7+.6*scallop);
 float under=top*rise-elevation; // > 0 under the sheet
 float fresh=1-smoothstep(.1,.9,since);
 o.x=smoothstep(0,.015,under)*(.35+.65*fresh)*flat;
 [branch]if(flat>0&&top>1e-3) {
  float edge=(1-smoothstep(0,.03,abs(under-.01)))*smoothstep(.02,.15,rise*arrival.y);
  float left=smoothstep(-.01,.01,top-elevation)*step(0,-under)*fresh*(1-smoothstep(liquidSurfRunupRise,1,since))*.55;
  o.y=liquidSurfFoamPattern(fp,max(edge,left),seconds*.5,detail,quality,footprint/cell).x*flat*saturate(liquidSurfFoamParams.x);
 }
 float wetTop=liquidSurfRunup*liquidSurfWaveParams.x*field.r*(.75+.5*scallop);
 o.z=max(smoothstep(-.02,.02,wetTop-elevation)*lerp(.6,1,fresh),1-smoothstep(0,.05,elevation))*flat;
 float keep=(field.r>1e-3?1:0)*(1-smoothstep(liquidSurfRunupInland-1,liquidSurfRunupInland,-field.a));
 return o*keep;
}
// water_surf_debug 1: band water unlit by exposure (blue sheltered, yellow,
// red fully exposed), darker stripes across the incoming direction.
// water_surf_debug 2: shore impact, dark green beach through yellow to magenta cliff or quay.
float3 liquidSurfImpactDebug(float impact) {
 return impact<.5?lerp(float3(.05,.35,.1),float3(1,.9,.1),impact*2):lerp(float3(1,.9,.1),float3(.95,.1,.85),impact*2-1);
}
float3 liquidSurfDebug(float4 field,float2 p) {
 float3 ramp=field.r<.5?lerp(float3(.1,.25,.9),float3(1,.9,.1),field.r*2):lerp(float3(1,.9,.1),float3(1,.1,.05),field.r*2-1);
 return ramp*(frac(dot(p,field.gb)*.25)<.2?.55:1);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Character ripples: local rings and churn from the renderer's wave
// simulation (liquid_waves.h), plus a short Kelvin-angle V trailing each
// moving unit, evaluated here from the unit's position, heading, speed and
// body size (ripple cbuffer). Kelvin wakes keep a ~19.5 degree half angle at
// any speed; the V is a few body lengths long, so a person reads as wading,
// not as a boat. Game time only through the field; no camera input.
Texture2D<float4> liquidWaves:register(t24); // height, previous height, wading trail
static const float liquidWaveNormal = 3; // ring slope gain (wave field heights are a few mm) @param 0 6
static const float liquidWaveFoam = .7; // @param 0 2
static const float liquidWaveFoamOpacity = .7; // wake foam over clear shallow water @param 0 1
// renderer_liquid.cpp liquid_unit_reach mirrors the unit terms' reach: keep it in step.
static const float liquidWakeTan = .354; // tan(19.47): Kelvin half angle @param 0.1 0.7
static const float liquidWakeLength = 2.5; // body sizes of trailing V at walking pace, +1.5 per m/s @param 1 10
static const float liquidWakeAmplitude = .14; // crest height per metre of body size @param 0 0.3
static const float liquidSecondsPerTick = .055; // game route speed is per tick
static const float liquidWakeSheen = .35; // brightening of wake crests @param 0 1
// Height of unit u's V wake at p (world units).
float liquidUnitWake(float2 p,uint u) {
 float4 ps=liquidRipplePositionSpeed[u],ph=liquidRipplePhase[u];
 float v=ps.w/liquidSecondsPerTick,r=max(ph.y,.05);
 if(v<.2||dot(ph.zw,ph.zw)<.25)return 0;
 float2 q=p-ps.xy;
 float behind=-dot(q,ph.zw),lat=abs(dot(q,float2(-ph.w,ph.z)));
 float len=r*(liquidWakeLength+1.5*min(v,4));
 if(behind<-r||behind>len||lat>len*liquidWakeTan+2*r)return 0; // rejected before any trigonometry
 float x=behind+r*.6; // the V opens just ahead of the body centre
 float arm=lat-x*liquidWakeTan,width=r*.35+x*.12;
 float wavelength=r*.9+v*.08;
 float edge=len*liquidWakeTan+2*r;
 float fade=exp(-max(behind,0)/(len*.4))*saturate(1+behind/r)*saturate(v/1.2)*
            saturate((len-behind)/(len*.3))*saturate((edge-lat)/r);
 // Divergent crests along the arms and faint transverse crests inside the V.
 float divergent=exp(-arm*arm/(width*width))*sin(6.2831853*(x*.94+lat*.33)/wavelength);
 float inside=saturate(-arm/width)*.12*sin(6.2831853*x/(wavelength*1.6))*exp(-lat/(x*liquidWakeTan+r));
 // Eased in and out with the unit's motion (renderer_liquid.cpp ease_liquid_units).
 return liquidWakeAmplitude*r*fade*(divergent+inside)*liquidRippleFlow[u].z;
}
float liquidUnitWakes(float2 p) {
 float h=0;uint count=min(uint(liquidRippleMeta.x),16u);
 [loop]for(uint u=0;u<count;++u)h+=liquidUnitWake(p,u);
 return h;
}
// Foam only at the contact of fast movement, not along the V.
float liquidUnitContactFoam(float2 p) {
 float foam=0;uint count=min(uint(liquidRippleMeta.x),16u);
 [loop]for(uint u=0;u<count;++u){
  float4 ps=liquidRipplePositionSpeed[u];float r=max(liquidRipplePhase[u].y,.05);
  float2 q=p-ps.xy;
  foam=max(foam,saturate((ps.w/liquidSecondsPerTick-2.2)/1.8)*exp(-dot(q,q)/(r*r*1.3))*liquidRippleFlow[u].z);
 }
 return foam;
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Water parting around a wading or standing unit, from the river's own
// speed: potential flow around the body as a cylinder. The surface rises
// where the flow stagnates against the upstream side, dips along the sides
// where it speeds up (Bernoulli), and stays low in the separated wake behind
// (downstream stagnation suppressed). Still water does not part. Plus the
// contact line where the body meets the water (thicker where water piles up
// upstream or in front of a moving body) and a foam tail carried downstream.
static const float liquidContactRadius = .55; // waterline radius as a share of the body half extent @param 0.2 1.2
static const float liquidContactWidth = .035; // contact line half width (world units) @param 0.01 0.15
static const float liquidContactFoam = .35; // foam of the contact line @param 0 1.5
static const float liquidContactRim = .003; // height of the water piled against the body (world units) @param 0 0.02
static const float liquidContactTail = 3; // downstream foam tail in running water, body radii @param 0 8
static const float liquidPartingHeight = .08; // share of the flow's velocity head raised and lowered around the body @param 0 0.3
float liquidPartingHeightAt(float2 q,float r,float2 flow,float head) {
 float d2=max(dot(q,q),r*r),r2=r*r/d2;
 float c=dot(q,flow)*rsqrt(d2); // cos(angle from downstream)
 float cos2=2*c*c-1;
 float h=2*r2*cos2-r2*r2;
 // Downstream of the body the flow separates: no stagnation rise there.
 return head*(c>0&&h>0?h*.25:h)*saturate(sqrt(d2)/r-.6);
}
// xy: slope; z: foam.
float3 liquidUnitContact(float2 p,float2 current,float seconds,float footprint) {
 float3 result=0;uint count=min(uint(liquidRippleMeta.x),16u);
 float runSlope=length(current);
 float run=smoothstep(liquidRiverOnset.x,liquidRiverOnset.y,runSlope);
 float2 flow=runSlope>1e-5?current/runSlope:float2(1,0);
 float speed=min(liquidRiverSpeed.x+liquidRiverSpeed.y*runSlope,liquidRiverSpeed.z)*run;
 float head=liquidPartingHeight*speed*speed/(2*liquidGravity);
 [loop]for(uint u=0;u<count;++u){
  float4 ps=liquidRipplePositionSpeed[u],ph=liquidRipplePhase[u];
  float r=max(ph.y,.05)*liquidContactRadius;
  float2 q=p-ps.xy;
  float tailLength=r*liquidContactTail*run,reach=max(r*4,r+tailLength)+.3;
  if(abs(q.x)>reach||abs(q.y)>reach)continue; // rejected before any noise
  float d=max(length(q),1e-4);float2 radial=q/d;
  [branch]if(head>1e-5&&d<r*4) {
   const float e=.03;
   float h0=liquidPartingHeightAt(q,r,flow,head);
   result.xy+=float2(liquidPartingHeightAt(q+float2(e,0),r,flow,head)-h0,
                     liquidPartingHeightAt(q+float2(0,e),r,flow,head)-h0)/e*liquidRippleFlow[u].w;
  }
  float w=liquidContactWidth*(1+r);
  float x=(d-r)/w,outline=exp(-x*x);
  float moving=saturate(ps.w/liquidSecondsPerTick/3)*liquidRippleFlow[u].z;
  float presence=liquidRippleFlow[u].w; // eased entry into and exit from the water
  float front=run*saturate(-dot(radial,flow))+moving*saturate(dot(radial,ph.zw));
  // Broken, slowly churning outline (bounded game time: no drift); faint in
  // still water around a standing body.
  float breakup=liquidFilteredNoise(p*11+float2(sin(seconds*.9+ph.x),cos(seconds*.7+ph.x))*.6,footprint*11);
  float foam=outline*smoothstep(.25,.75,breakup)*(1+.8*front)*lerp(.3,1,max(run,moving));
  result.xy+=radial*(-2*x/w)*exp(-x*x)*liquidContactRim*(1+front)*presence;
  [branch]if(tailLength>0){
   // Downstream: streaks carried off the body by the river's two-phase
   // advection (liquidRiverPhases), so they flow without unbounded time.
   float along=dot(q,flow),lat=dot(q,float2(-flow.y,flow.x));
   if(along>0){
    float3 phase=liquidRiverPhases(seconds,ph.x);
    float shift=liquidRiverShiftLength(min(liquidRiverSpeed.x+liquidRiverSpeed.y*runSlope,liquidRiverSpeed.z));
    float sA=liquidFilteredNoise(float2((along-phase.x*shift)*2.5,lat*9)+ph.x,footprint*9);
    float sB=liquidFilteredNoise(float2((along-phase.y*shift)*2.5,lat*9)+ph.x+7.3,footprint*9);
    float streaks=smoothstep(.45,.8,.5+(lerp(sA,sB,phase.z)-.5)*liquidPhaseContrast(phase.z));
    float spread=r*(.7+.5*along/max(tailLength,1e-3));
    foam=max(foam,streaks*exp(-lat*lat/(spread*spread))*smoothstep(r*.5,r*1.1,along)*
                  saturate(1-along/(r+tailLength))*run*.8);
   }
  }
  result.z=max(result.z,foam*presence);
 }
 result.z=saturate(result.z*liquidContactFoam);
 return result;
}
// The wading trail (liquid_waves.h, field channel z): a continuous band of
// stirred water along each unit's path, carried downstream by the river and
// fading over a few seconds. Its churn never stands still: two layers, each
// restarting at a fresh random place when its weight is zero, crossfade on a
// short cycle (they boil), and both drift with the river's own speed by the
// bounded two-phase advection (no time-growing coordinate).
static const float liquidTrailChurn = .35; // ripple slope of the stirred band @param 0 1.5
static const float liquidTrailFoam = .3; // foam lace in the stirred band @param 0 1
static const float liquidTrailCycle = .7; // game seconds per churn layer life @param 0.2 3
float3 liquidWaveRipples(float2 position,float2 current,float seconds,float footprint) {
 float3 result=0;
 [branch]if(liquidWaveWindow.w>0) {
  float2 uv=(position-liquidWaveWindow.xy)*liquidWaveWindow.z;
  if(all(uv>0)&&all(uv<1)) {
   float t=1.0/512,cell=1/(liquidWaveWindow.z*512);
   float4 here=liquidWaves.SampleLevel(liquidSceneSampler,uv,0);
   float2 slope=float2(liquidWaves.SampleLevel(liquidSceneSampler,uv+float2(t,0),0).x-
                       liquidWaves.SampleLevel(liquidSceneSampler,uv-float2(t,0),0).x,
                       liquidWaves.SampleLevel(liquidSceneSampler,uv+float2(0,t),0).x-
                       liquidWaves.SampleLevel(liquidSceneSampler,uv-float2(0,t),0).x)/(2*cell);
   result=float3(slope*liquidWaveNormal,smoothstep(.006,.02,here.x)*.5);
   float run=smoothstep(liquidRiverOnset.x,liquidRiverOnset.y,length(current));
   float trail=saturate(here.z)*(1-.4*run);
   [branch]if(trail>.01) {
    float runSlope=length(current);
    float2 velocity=runSlope>1e-5?current/runSlope*min(liquidRiverSpeed.x+liquidRiverSpeed.y*runSlope,liquidRiverSpeed.z)*run:float2(0,0);
    float cycle=seconds/liquidTrailCycle,a=frac(cycle),b=frac(cycle+.5),blend=abs(1-2*a);
    float ia=floor(cycle),ib=floor(cycle+.5)+.5;
    float2 pa=position-velocity*liquidTrailCycle*(a-.5)+float2(liquidNoiseHash(float2(ia,1.3)),liquidNoiseHash(float2(ia,4.1)))*17;
    float2 pb=position-velocity*liquidTrailCycle*(b-.5)+float2(liquidNoiseHash(float2(ib,2.7)),liquidNoiseHash(float2(ib,6.9)))*17;
    float3 churn=lerp(liquidFilteredGradient(pa*7,footprint*7),liquidFilteredGradient(pb*7+float2(5.3,1.7),footprint*7),blend);
    result.xy+=churn.yz*trail*liquidTrailChurn;
    float lace=lerp(liquidFilteredNoise(pa*4.3+float2(3.1,8.2),footprint*4.3),liquidFilteredNoise(pb*4.3,footprint*4.3),blend);
    result.z=max(result.z,smoothstep(.55,.85,lace)*smoothstep(.1,.6,trail)*liquidTrailFoam);
   }
  }
 }
 [branch]if(liquidRippleMeta.x>0) {
  const float e=.04;
  float h0=liquidUnitWakes(position);
  result.xy+=float2(liquidUnitWakes(position+float2(e,0))-h0,liquidUnitWakes(position+float2(0,e))-h0)/e;
  // Crests catch a little light (aerated tops), so the short V reads even
  // over clear shallow water seen from above.
  result.z=max(max(result.z,liquidUnitContactFoam(position)),smoothstep(.002,.012,h0)*liquidWakeSheen);
  float3 contact=liquidUnitContact(position,current,seconds,footprint);
  result.xy+=contact.xy;result.z=max(result.z,contact.z);
 }
 result.z=saturate(result.z)*liquidWaveFoam;
 return result;
}
// Raindrop rings (physical water, during rain). Each layer is a world grid of
// cells; every cell restarts on its own phase each drop lifetime and, with a
// probability that follows the rain, places one drop at a hashed point. The
// drop is an impact dimple, then a damped ring wave expanding from it. All
// inputs are world position and game time: stable on a still camera, frozen
// when paused. Rings finer than the pixel footprint leave only their slope
// variance, which widens the sun highlight of distant rainy water.
static const uint liquidRainLayers = 4;
static const float liquidRainCell = 1.25; // world units per drop cell
static const float liquidRainLife = .9; // seconds from impact to fade-out
static const float2 liquidRainRadius = float2(.04,.5); // ring radius at impact, growth over its life
static const float liquidRainWavelength = .11; // world units between ring crests
static const float liquidRainRingSlope = .55;
static const float liquidRainDimpleSize = .07; // world units
static const float liquidRainDimpleSlope = .5;
static const float liquidRainDensity = .9; // drops per cell and lifetime at full rain
static const float liquidRainVariance = .05; // slope variance of unresolved rings at full rain
uint liquidRainHash(uint x) {
 x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;
}
float liquidRainUnit(uint h) { return (h>>8)*(1.0/16777216.0); }
// xy: height gradient of the rings; z: slope variance of rings below the pixel footprint.
float3 liquidRainRipples(float2 position,float seconds,float rain,float footprint) {
 float detail=liquidBandWeight(footprint/liquidRainWavelength);
 float3 result=float3(0,0,rain*liquidRainVariance*(1-detail));
 [branch]if(detail>0) {
 [unroll]for(uint layer=0;layer<liquidRainLayers;++layer) {
  // Rotated, offset grids so the cells never line up.
  float angle=layer*2.39996323;
  float2 axis=float2(cos(angle),sin(angle));
  float2 q=liquidRotate(position,axis)/liquidRainCell+float2(.37,.71)*layer;
  float2 cell=floor(q),local=q-cell;
  uint key=liquidRainHash(asuint(int(cell.x))*0x9e3779b1u^liquidRainHash(asuint(int(cell.y))+layer*0x632be5abu));
  float cycle=seconds/liquidRainLife+liquidRainUnit(key);
  uint drop=liquidRainHash(key^asuint(int(floor(cycle)))*0x85ebca77u);
  if(liquidRainUnit(drop)>=rain*liquidRainDensity)continue;
  float age=frac(cycle);
  uint place=liquidRainHash(drop);
  float2 center=.3+.4*float2(liquidRainUnit(place),liquidRainUnit(liquidRainHash(place)));
  float2 d=(local-center)*liquidRainCell;
  float r=length(d);
  // Fade before the cell border so rings never cut off at a seam.
  float2 edge=min(center,1-center)*liquidRainCell;
  float fit=saturate((min(edge.x,edge.y)-r)/liquidRainWavelength);
  float x=(r-liquidRainRadius.x-liquidRainRadius.y*age)/liquidRainWavelength;
  float u=r/liquidRainDimpleSize;
  // Off the ring's envelope and past the dimple (both about 1% of their
  // peak), or at the cell border: no visible slope, skip the wave terms.
  // Most pixels of a cell lie there (rain-shelter cost runs: the rings on
  // every wet flat face cost about 0.4 ms of 4K terrain and figures).
  if(fit<=0||(abs(x)>2.6&&u>2.5))continue;
  float envelope=exp(-x*x*.7);
  float decay=smoothstep(0,.05,age)*pow(1-age,1.5);
  float ring=liquidRainRingSlope*decay*envelope*(-sin(6.2831853*x)-cos(6.2831853*x)*x*.22);
  float dimple=liquidRainDimpleSlope*2.33*u*exp(-u*u)*(1-smoothstep(0,.2,age));
  // d is in this layer's rotated grid. Gradients transform by its
  // transpose, so return to the world basis before adding layers.
  result.xy+=liquidRotate(d/max(r,1e-4),float2(axis.x,-axis.y))*(ring+dimple)*fit*detail;
 }
 }
 return result;
}
float3 liquidSafeNormal(float3 v) { return v*rsqrt(max(dot(v,v),1e-12)); }
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Physical water terms (water_physical; they need the scene snapshot, so the
// reflection frame carries the switch in liquidReflectionTarget.w).
static const float liquidRoughness = .12; // GGX alpha of the former Blinn-Phong power 128 @param 0.01 0.6
static const float liquidPhysicalF0 = .02; // water at normal incidence
static const float liquidRefractionIndex = 1.33;
static const float liquidRefractionDepth = 3; // world units of bed parallax at most @param 0 10
static const float liquidRefractionShift = .08; // largest refraction shift on screen (fraction of its height) @param 0 0.15
static const float liquidRefractionEdgeSlope = .2; // largest share of the room to the screen edge a shift keeps; the edge magnifies the bed at most 1/(1-slope) @param 0.05 0.5
// Per-channel absorption: the authored tint's strongest channel travels
// furthest; weaker channels are absorbed up to 1.4x liquidAbsorption.
static const float2 liquidChannelAbsorption = float2(.6,.8); // base, extra for weak channels
static const float liquidShoreSigma = .5; // tiles; Gaussian over the liquid-tile mask @param 0.1 1.5
// NATIVE_LIQUID_NO_PHYSICAL: bound while water_physical is off (the frame's w 0);
// lava never takes the physical terms (its reflection weight is 0).
#if defined(NATIVE_LIQUID_NO_PHYSICAL)||defined(NATIVE_LIQUID_LAVA)
bool liquidPhysical() { return false; }
#else
bool liquidPhysical() { return !liquidLowPath&&liquidReflectionTarget.w>.5; }
#endif
float liquidSunGgx(float3 n,float3 v,float3 l,float a2) {
 float3 h=liquidSafeNormal(v+l);
 float nl=saturate(dot(n,l)),nv=max(dot(n,v),1e-3),nh=saturate(dot(n,h)),vh=saturate(dot(v,h));
 a2=clamp(a2,1e-4,1);
 float d=nh*nh*(a2-1)+1;
 float visibility=.5/(nl*sqrt(nv*nv*(1-a2)+a2)+nv*sqrt(nl*nl*(1-a2)+a2)+1e-5);
 return a2/(3.14159265*d*d)*visibility*(liquidPhysicalF0+(1-liquidPhysicalF0)*pow(1-vh,5))*nl;
}
// Beer-Lambert absorption of each channel along a path through the water,
// normalised like the scalar authored depth response (1 = fully absorbed).
float3 liquidChannelAbsorbed(float3 tint,float path) {
 float3 dominance=tint/max(max(tint.r,tint.g),max(tint.b,1e-3));
 float3 sigma=liquidAbsorption*(liquidChannelAbsorption.x+liquidChannelAbsorption.y*(1-dominance));
 return saturate((1-exp(-sigma*max(path,0)))/(1-exp(-liquidAbsorption*liquidOpaqueDepth)));
}
float liquidTileCdf(float x) { return 1/(1+exp(-1.702*x)); }
// The liquid-tile mask of a tile's 3x3 neighbourhood convolved with a
// Gaussian at the pixel's place in its tile: ~1 inside, .5 on a straight tile
// border, ~.25 at an outer corner.
float liquidTileField(uint flags,float2 local) {
 if((flags&(1u<<15))==0)return 1;
 float3 ix,iy;
 [unroll]for(int d=-1;d<=1;++d) {
  ix[d+1]=liquidTileCdf((d+1-local.x)/liquidShoreSigma)-liquidTileCdf((d-local.x)/liquidShoreSigma);
  iy[d+1]=liquidTileCdf((d+1-local.y)/liquidShoreSigma)-liquidTileCdf((d-local.y)/liquidShoreSigma);
 }
 float field=0;
 [unroll]for(uint k=0;k<9;++k)field+=((flags>>(16+k))&1)*ix[k%3]*iy[k/3];
 return field;
}
float3 liquidScenePoint(int2 pixel,float z) {
 float2 ndc=(float2(pixel)+.5)/liquidReflectionTarget.xy*float2(2,-2)+float2(-1,1);
 float4 p=mul(liquidInverseWorldClip,float4(ndc,z,1));
 return p.xyz/(abs(p.w)>1e-6?p.w:1e-6);
}
// Water depth over the bed averaged over about 1.5 m (this pixel and eight
// snapshot reads on a ring, rotated per pixel), a dry beach counting as 0;
// without the snapshot (Low path, the half-resolution pass) the vertex
// depth. Shipped beds step from flat shallows straight down a tile edge,
// and the shallow look taken from the exact depth drew those square steps
// as hard white-edged blocks (save1144 sandbar).
static const float liquidBedSmoothReach = 1.5; // m @param 0.3 4
// Snapshot points standing more than this above the surface are not the bed
// beside the water but something in front of it (a character, tree or cliff):
// counted as dry ground they lit a pale shallow halo around every figure
// standing in water. They are skipped; a beach just above the line still
// counts as 0.
static const float liquidBedSmoothDry = .5; // m @param 0.1 2
float liquidSmoothBedDepth(float2 pixel,float3 position,float vertexDepth,float footprint) {
 [branch]if(liquidLowPath||liquidReflectionTarget.z<.5)return max(vertexDepth,0);
 int2 last=int2(liquidReflectionTarget.xy)-1;
 float reach=clamp(liquidBedSmoothReach/max(footprint,1e-4),1,64);
 float turn=6.2831853*frac(52.9829189*frac(dot(pixel,float2(.06711056,.00583715))));
 float sum=0,count=0;
 [unroll]for(uint k=0;k<9;++k) {
  float a=turn+k*.7853982,r=k==0?0:reach*(k&1?1:.55);
  int2 q=clamp(int2(pixel+float2(cos(a),sin(a))*r),int2(0,0),last);
  float z=liquidSceneDepth.Load(int3(q,0));
  if(z>=1)continue;
  float d=position.z-liquidScenePoint(q,z).z;
  if(d>=-liquidBedSmoothDry){sum+=max(d,0);++count;}
 }
 return count>0?sum/count:max(vertexDepth,0);
}
static float liquidBedSmooth=0; // shadeLiquidSurface: liquidSmoothBedDepth of this pixel
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// Foam layer (nativeLiquidLayer pass): whitewater is a separate surface drawn
// over the water body, lifted along the liquid normal. The vertex stage keeps
// a triangle when any corner can carry foam: running water or a cascade
// source (never lava).
static const float liquidFoamLayerLift = .06; // world units the foam surface stands off the water @param 0 0.3
float liquidFoamLayerWeight(float cascade,float2 current,float selfIllumination) {
 if(selfIllumination>0)return 0;
 return max(saturate(cascade),smoothstep(liquidRiverOnset.x,liquidRiverOnset.y,length(current)));
}
// Fall tessellation (terrain_tessellation on): falling and running water is
// subdivided and displaced along its horizontal flow direction (away from the
// rock). Everything here reads only values interpolated from the patch
// corners, so patches sharing an edge displace it identically (no cracks);
// edge factors depend only on the edge's two end points.
static const float liquidTessSpacing = .35; // target spacing of new vertices on falling/running water (m) @param 0.15 1
static const float liquidTessLip = .35; // how far the sheet stands out from the rock below the lip (m) @param 0 1
static const float liquidTessLipCurve = .8; // drop (m) over which the lip curls over to full stand-off @param 0.1 3
static const float liquidTessColumns = .22; // thicker and thinner columns of falling water (m) @param 0 0.6
static const float liquidTessSides = .45; // the sides pull back into the rock by up to this (m) @param 0 1.5
static const float liquidTessSideDepth = .6; // water depth over the rock below which the sides thin out (m) @param 0.1 2
static const float2 liquidTessRipple = float2(.04,0); // ripple amplitude on falls (x, m); y unused: running water no longer ripples its mesh @param 0 0.2
static const float liquidTessRippleSpeed = 5; // radians per game second @param 0 20
// Corner weight: falling or running water (and the water a fall lands in, so
// edges shared with the pool subdivide alike); lakes and lava stay whole.
float liquidTessWeight(float cascade,float2 current,float selfIllumination) {
 return liquidFoamLayerWeight(cascade,current,selfIllumination);
}
float liquidTessEdgeFactor(float3 a,float3 b,float weightA,float weightB,float maxFactor) {
 if(maxFactor<=1||max(weightA,weightB)<=0)return 1;
 return clamp(ceil(length(a-b)/liquidTessSpacing),1,maxFactor);
}
// Sea surf on tessellated water: a corner in the coastal band with surf
// carries liquidSurfTessFlag on top of its fall weight (nativeTessVS); its
// edges split to liquidSurfTessSpacing for the wave height (nativeTerrainDomain).
// Water without surf keeps exactly its former factors and displacement.
bool liquidTessSurf(float limit) { return limit>=liquidSurfTessFlag; }
float liquidTessFall(float limit) { return liquidTessSurf(limit)?limit-liquidSurfTessFlag:limit; }
float liquidSurfTessEdgeFactor(float3 a,float3 b,float maxFactor) {
 return maxFactor<=1?1:clamp(ceil(length(a-b)/liquidSurfTessSpacing),1,maxFactor);
}
// Displacement (m, along the horizontal flow direction) at a point of the
// liquid surface: rest position q, interpolated current, drop below the lip,
// water depth over the bed, game seconds.
float liquidTessDisplacement(float3 q,float2 current,float drop,float depth,float seconds) {
 float runSlope=length(current);
 float fall=smoothstep(liquidWaterfallSlope.x,liquidWaterfallSlope.y,runSlope);
 float run=smoothstep(liquidRiverOnset.x,liquidRiverOnset.y,runSlope);
 if(run<=0)return 0;
 float2 h=current/runSlope;
 float across=dot(q.xy,float2(-h.y,h.x));
 // Columns: long vertical bands of thicker water, plus finer ones.
 float columns=liquidNoise(float2(across*.9,q.z*.12))*.7+liquidNoise(float2(across*2.3+7.1,q.z*.3))*.3;
 float lip=liquidTessLip*(1-exp(-max(drop,0)/liquidTessLipCurve));
 // Sides: where the water over the rock thins, the sheet pulls back into it,
 // at an irregular line set by the columns.
 float side=1-smoothstep(0,liquidTessSideDepth,depth+(columns-.5)*.5);
 float outward=(lip+liquidTessColumns*(columns-.5)*smoothstep(0,1,drop))*(1-side)-liquidTessSides*side;
 // Falls only: on running water this sideways wobble (and its phase across
 // world coordinates, noisy where the flow turns) made the sheet heave.
 float ripple=sin(drop*3.1+across*1.7-seconds*liquidTessRippleSpeed)*liquidTessRipple.x;
 return fall*(outward+ripple);
}
struct LiquidSurfaceInput {
 float3 position, viewDirection, tint, emissive;
 float depth, opacity, multiplier, time, windForce, selfIllumination;
 float2 windDirection,current; // current: downhill liquid surface gradient (tangent)
 float bend,cascade; // rate of change of the surface gradient; cascade foam source 0..1
 float4 gradient; // world XY derivatives: ddx.xy, ddy.xy
 float2 heightGradient; // world Z derivatives (ddx.z, ddy.z), including steep falls
 float footprint,depthFootprint,mud; // mud: swamp share from the liquid tiles' own type (never the colour)
 float2 pixel; // SV_Position.xy, for the scene depth/colour snapshot
 float coverage; // rounded liquid-tile outline (physical terms only)
 float4 art; // animated swamp algae colour; a=1 when sampled
 float3 artMean; // mean colour of the tile's current art frame (the swamp colour the game draws); valid when art.a>0
 float drop; // distance below the fall lip (world units; OwnedLiquidFlow::drop)
 float fill; // pit depth the vertex stage lifted this surface by (OwnedLiquidFlow::fill)
 float edge; // liquid-tile field: ~1 inside, .5 on the liquid's outline (liquidTileField)
 uint layer; // 0: water body pass, 1: foam layer pass
};
// Depth below the water surface z of the snapshot point a refracted ray point
// p projects onto (uv); negative on sky, above the surface or off screen.
// clear: the share of eight neighbours (spread px and 0.43 of it away, spread
// at least 7) not standing above the surface. A sample near a figure blends
// toward this pixel's own bed by it: a figure's soft edge (hair, cloth)
// blends its colour into pixels whose
// depth is still the bed, and the hard switch to the own bed at the figure
// drew a thin outline above its head. Underwater neighbours (weed) do not
// count, or every blade would fail the depth test against the ray.
float liquidRefractionHit(float3 p,float surface,float spread,out float2 uv,out float clear) {
 clear=0;
 if(!liquidProject(p,uv))return -1;
 int2 q=int2(uv*liquidReflectionTarget.xy),last=int2(liquidReflectionTarget.xy)-1;
 float z=liquidSceneDepth.Load(int3(q,0));
 float hit=z<1?surface-liquidScenePoint(q,z).z:-1;
 static const float2 around[8]={float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),float2(.43,0),float2(-.43,0),float2(0,.43),float2(0,-.43)};
 [unroll]for(uint k=0;k<8;++k) {
  int2 r=clamp(q+int2(around[k]*max(spread,7)),int2(0,0),last);
  float zr=liquidSceneDepth.Load(int3(r,0));
  clear+=zr<1&&liquidScenePoint(r,zr).z>surface?0:.125;
 }
 return hit;
}
// Scene behind the surface along the Snell-refracted ray (rgb) and the view
// path through the water measured against the opaque depth snapshot (a).
#ifdef NATIVE_LIQUID_SWIMMERS
// A swimmer (a fish, marked in the snapshot's stencil) is looked up from the
// near side: the bed's refraction reach lands far beyond a small body near
// the surface, so a pixel that looks straight at a swimmer shows it, shifted
// along the refracted ray by the swimmer's own depth where that still lands
// on one, and absorbed along the path to that depth, not to the bed far
// below. liquidSwimmer: this pixel shows a swimmer.
static float liquidSwimmer=0;
#endif
float4 liquidRefraction(LiquidSurfaceInput i,float3 n,float3 v) {
 int2 pixel=int2(i.pixel);
 float z=liquidSceneDepth.Load(int3(pixel,0));
 float vertical=max(i.depth,0),path=vertical/max(v.z,.2); // sky behind: authored depth
 if(z<1) {
  // Absorption, opacity and the refraction reach take the smoothed bed depth
  // (liquidBedSmooth): from this pixel's own depth the shift jumped wherever
  // that depth did (a weed blade over the bed), breaking the bed into
  // shards with see-through outlines (user, 8% shift cap).
  path=liquidBedSmooth/max(v.z,.2);
  vertical=liquidBedSmooth;
 }
 float2 uv=(float2(pixel)+.5)/liquidReflectionTarget.xy,refracted;
 float3 t=refract(-v,n,1/liquidRefractionIndex);
#ifdef NATIVE_LIQUID_SWIMMERS
 [branch]if(z<1) {
  [branch]if(LIQUID_SWIMMER(liquidSceneStencil.Load(int3(pixel,0)).g)) {
   const float below=max(i.position.z-liquidScenePoint(pixel,z).z,0);
   float2 at=uv,moved;
   [branch]if(t.z<0&&liquidProject(i.position+t*(below/max(-t.z,.25)),moved)) {
    if(LIQUID_SWIMMER(liquidSceneStencil.Load(int3(int2(moved*liquidReflectionTarget.xy),0)).g))at=moved;
   }
   liquidSwimmer=1;
   return float4(liquidSceneColor.SampleLevel(liquidSceneSampler,float3(at,0),0).rgb,below/max(v.z,.2));
  }
 }
#endif
 float reach=min(vertical,liquidRefractionDepth)/max(-t.z,.25);
 // A sample counts only where what it shows lies below the surface: above-
 // water geometry (a figure's head and torso) must not leak in. The bed
 // shifts by the smoothed reach; anything shallower by its own depth (below).
 // A failed sample
 // keeps this pixel's own bed, and one beside a figure blends toward it
 // (liquidRefractionHit clear), so no figure-shaped seam or ghost shows
 // (user F11s, save1150).
 // Near the screen edge the shift shrinks smoothly so the sample stays on
 // screen (the bed it needs past the edge was never drawn): rays leaving the
 // screen fell back to this pixel's own bed (or stretched the last rows) in
 // hard bands along the bottom of the screen. A hard "at most halfway to the
 // edge" limit squeezed the bottom 16% (twice the shift cap) into half its
 // height: the bed there showed magnified 2x below a crease (#284).
 [branch]if(t.z<0) {
  float2 full;liquidProject(i.position+t*reach,full);
  float2 shift=full-uv;
  // The shift on screen stays under liquidRefractionShift of the screen
  // height: the bed behind a figure is hidden from the camera, so the patch a
  // full bed parallax (130-170 px at 4K in deep water) needed there showed as
  // a smeared figure-shaped ghost whatever filled it.
  float shiftHeight=length(shift*float2(liquidReflectionTarget.x/liquidReflectionTarget.y,1));
  float scale=min(1,liquidRefractionShift/max(shiftHeight,1e-6));
  // Per axis, the shift toward an edge keeps at most liquidRefractionEdgeSlope
  // of the room left to it, through a smooth minimum (s/(1+(s/limit)^4)^.25):
  // the shift is untouched well away from the edge, has no crease where the
  // limit starts, and changes by at most that slope per unit of screen, so
  // the bed near the edge is magnified by at most 1/(1-slope), smoothly.
  float2 m=abs(shift)*scale/max(liquidRefractionEdgeSlope*(shift>0?1-uv:uv),1e-6),m2=m*m;
  float2 keep=rsqrt(sqrt(1+m2*m2));
  scale*=keep.x*keep.y;reach*=scale;
  // The blend reaches about a third of the shift, so the seam to the own bed
  // stays soft at any shift length.
  float spread=shiftHeight*scale*liquidReflectionTarget.y*.35;
  float clear;float hit=liquidRefractionHit(i.position+t*reach,i.position.z,spread,refracted,clear);
  // Something shallower than the ray point (a figure's legs, a weed blade)
  // shifts only as far as its own depth: the bed's full reach drew a
  // figure's submerged legs as a larger copy behind it (user, save1150).
  float rayDepth=reach*-t.z;
  [branch]if(hit>0&&hit<rayDepth*.9)
   hit=liquidRefractionHit(i.position+t*(hit/max(-t.z,.25)),i.position.z,spread,refracted,clear);
  float3 own=liquidSceneColor.SampleLevel(liquidSceneSampler,float3(uv,0),0).rgb;
  // A swimmer at the bed's reach is a ghost at the wrong parallax (it shows
  // where a pixel looks straight at it): this pixel keeps its own bed.
#ifdef NATIVE_LIQUID_SWIMMERS
  [branch]if(hit>0&&clear>0) {
   if(LIQUID_SWIMMER(liquidSceneStencil.Load(int3(int2(refracted*liquidReflectionTarget.xy),0)).g))clear=0;
  }
#endif
  [branch]if(hit>0&&clear>0)
   return float4(lerp(own,liquidSceneColor.SampleLevel(liquidSceneSampler,float3(refracted,0),0).rgb,clear),path);
  return float4(own,path);
 }
 return float4(liquidSceneColor.SampleLevel(liquidSceneSampler,float3(uv,0),0).rgb,path);
}
float liquidShoreAmount(LiquidSurfaceInput i) {
 float2 wind=i.windDirection/max(length(i.windDirection),1e-6);
 float motion=liquidCalmMotion+abs(i.multiplier)*saturate(i.windForce);
 float breakup=liquidFilteredNoise(i.position.xy*liquidShoreNoiseScale-wind*i.time*liquidShoreFlowSpeed*motion,
                                  i.footprint*liquidShoreNoiseScale);
 float surge=1+liquidShoreSurge*sin(i.time*liquidSecondsPerTime*liquidShoreSurgeRate+breakup*6.2831853);
 float edge=1-smoothstep(0,liquidShoreDepth*surge,liquidBedSmooth);
 float luminance=dot(saturate(i.tint),float3(.2126,.7152,.0722));
 float clarity=lerp(liquidMuddyShoreMinimum,1,
                    smoothstep(liquidShoreColorRange.x,liquidShoreColorRange.y,luminance));
 return edge*lerp(liquidShoreCoverage,1,smoothstep(liquidShoreBreakup.x,liquidShoreBreakup.y,breakup))*
        clarity*liquidShoreEnabled(i.selfIllumination);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// Caustics on terrain under water: two drifting layers of the thin vein
// network, strongest just below the surface and fading with depth.
static const float liquidCausticTile = 3.2; // @param 0.5 10
static const float liquidCausticStrength = .9; // @param 0 3
float liquidBedCaustics(float2 p,float depth,float4 gradient) {
 if(depth<=0)return 0;
 float2 d=float2(.23,.14)*liquidRippleMeta.z;
 float c1=liquidDetailAt((p+d)/liquidCausticTile,liquidCausticTile,gradient).r;
 float c2=liquidDetailAt(liquidRotate(p-d*.8,float2(.8,.6))/(liquidCausticTile*.73)+.5,
                         liquidCausticTile*.73,gradient,float2(.8,.6)).r;
 return saturate(c1+c2)*smoothstep(0,.2,depth)*exp(-depth*.5)*liquidCausticStrength;
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// Whitewater controls. Pool foam gathers at a fall's impact (cascade source
// raised to liquidCascadeFalloff keeps it near the fall) plus drifting lines;
// rapids foam follows speed and slope.
static const float liquidCascadeFalloff = 1.5; // higher: pool foam stays closer to the impact @param 1 8
static const float liquidFoamLace = .45; // thin drifting foam lines around the foam masses @param 0 1
static const float liquidFoamMass = .6; // share of the foam amount that becomes solid masses @param 0 1
static const float liquidFoamBoil = .5; // solid white boil right under a fall @param 0 1
static const float liquidCascadeMilk = .35; // bubbly milky water body around a fall's impact @param 0 1
static const float liquidWaterfallAeration = 3; // drop (world units) by which the fall is fully white @param 0.5 10
static const float liquidWaterfallSheetOpacity = .8; // opacity of the water body under the fall foam @param 0 1
// The fall's sides and its contact with the pool fade out over this depth of
// water above the bed/rock (world units), so the edges dissolve.
static const float liquidFallEdgeDepth = .5; // @param 0 2
// Whitewater colour: near-neutral cool white lit by a desaturated sun and
// ambient, so warm scene light does not turn foam into cream paint.
static const float3 liquidFoamWhite = float3(.92,.96,1);
static const float liquidFoamLightSaturation = .35; // share of the light's own hue kept on foam @param 0 1
float3 liquidFoamLight(float3 c) {
 return lerp(dot(c,float3(.2126,.7152,.0722)).xxx,c,liquidFoamLightSaturation);
}
float liquidFallEdge(LiquidSurfaceInput i) {
 return smoothstep(0,liquidFallEdgeDepth,i.depth)*smoothstep(.5,.9,i.edge);
}
// Water body opacity on a fall: thin and see-through at the lip.
float liquidFallSheetAlpha(LiquidSurfaceInput i) {
 return liquidWaterfallSheetOpacity*lerp(.5,1,smoothstep(0,1.5,i.drop));
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// Fall faces: the map's own liquid overlay keeps every fall's shape and its
// steep faces below the lip show the clear, darker body of falling water. The
// fall's foam is its own geometry, the waterfall shell (procedural_falls.h
// build_fall_shell, drawn by renderer_ambient_particles.cpp), so nothing here
// samples a pattern in fall height: such patterns degenerated on the
// overlay's level treads and where the fall meets the pool (horizontal and
// stretched streaks).
static const float liquidCurtainOpacity = .62; // the body's opacity (translucent) @param 0 1
// Fall faces: smoothed surface slope (5x5 vertices: fall faces ~.6-1, rapids
// below ~.4) and drop below the lip over which the fall body fully takes over.
static const float2 liquidCurtainSlope = float2(.4,.7); // @param 0 2
static const float2 liquidCurtainDrop = float2(.3,1.2); // m @param 0 4
float liquidCurtainWeight(LiquidSurfaceInput i,float running) {
 return smoothstep(liquidCurtainSlope.x,liquidCurtainSlope.y,length(i.current))*running*
        smoothstep(liquidCurtainDrop.x,liquidCurtainDrop.y,i.drop);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// The last metres of running water above a fall lip: near a fall (cascade
// source, which fades over kLiquidCascadeReach) but not below its lip (drop
// ~0) and not the falling face. There the water speeds up, chops and
// streaks white and its surface leans toward the lip.
static const float2 liquidLipDrop = float2(.15,.6); // drop below the lip (m) over which the approach ends @param 0 2
static const float liquidLipSpeed = .8; // share by which the flow speeds up at the lip @param 0 3
static const float liquidLipChop = .9; // extra ripple amplitude at the lip @param 0 3
static const float liquidLipFoam = .5; // foam amount of light streaks at the lip @param 0 1
static const float liquidLipTilt = .12; // extra surface slope toward the lip @param 0 0.5
float liquidLipApproach(LiquidSurfaceInput i,float running,float waterfall) {
 return saturate(i.cascade)*(1-smoothstep(liquidLipDrop.x,liquidLipDrop.y,i.drop))*running*(1-waterfall);
}
// Shallow water foam follows the depth over the bed, which is linear over
// each 1 m triangle of the bed grid: thresholded directly it drew the grid's
// straight edges as white squares. A fine noise moves the threshold.
static const float liquidShallowBreakup = .22; // depth (m) the noise moves the threshold by @param 0 1
static const float liquidShallowTile = 1.7; // world units per breakup noise repeat @param 0.3 6
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
// The foam layer: rapids and pool whitewater as colour and coverage over the
// water body drawn before it.
float4 liquidFoamLayer(LiquidSurfaceInput i,float3 ambient,float3 sunColor,float3 l,float3 tint,float mud,
 float seconds,float phaseOffset,float running,float steep,float waterfall,float2 flowDir,
 float2 riverShift,float riverA,float riverB,float riverBlend,float4 riverGradientA,float4 riverGradientB) {
 float3 foamBase=lerp(tint,liquidFoamWhite,.9);
 float3 ambientN=liquidFoamLight(ambient),sunN=liquidFoamLight(sunColor);
 float cascade=saturate(i.cascade);
 // Impact: water near a fall (cascade source) that lies well below its lip.
 float impact=pow(cascade,liquidCascadeFalloff)*smoothstep(.1*liquidWaterfallAeration,1.5*liquidWaterfallAeration,i.drop);
 float shallows=0;
 [branch]if(running>0&&i.depth<liquidRiverShallows.y+liquidShallowBreakup) {
  float breakup=liquidDetailAt(i.position.xy/liquidShallowTile+.23,liquidShallowTile,i.gradient).b;
  shallows=1-smoothstep(liquidRiverShallows.x,liquidRiverShallows.y,i.depth+(breakup-.5)*2*liquidShallowBreakup);
 }
 float lip=liquidLipApproach(i,running,waterfall);
 float amount=max(running*max(lerp(liquidRiverFoam.x,liquidRiverFoam.y,smoothstep(.1,.8,steep)),shallows*liquidRiverShallowFoam),
                  max(impact*liquidCascadeFoam,lip*liquidLipFoam))*(1-.6*mud);
 float cover=0;float3 color=foamBase;
 [branch]if(amount>0&&waterfall<1) {
  // Churning pool foam: two layers, each jumping to a fresh random place and
  // drift at the moment its weight is zero, crossfaded on a short cycle. It
  // boils in place without any time-growing coordinate.
  float boilCycle=seconds/liquidBoilPeriod+phaseOffset;
  float boilA=frac(boilCycle),boilB=frac(boilCycle+.5),boilBlend=abs(1-2*boilA);
  float cycleA=floor(boilCycle),cycleB=floor(boilCycle+.5)+.5;
  float2 jitterA=float2(liquidNoiseHash(float2(cycleA,1.7)),liquidNoiseHash(float2(cycleA,5.3)))*9;
  float2 jitterB=float2(liquidNoiseHash(float2(cycleB,2.9)),liquidNoiseHash(float2(cycleB,7.1)))*9;
  float angleA=liquidNoiseHash(float2(cycleA,9.7))*6.2831853,angleB=liquidNoiseHash(float2(cycleB,3.3))*6.2831853;
  float2 boilShiftA=jitterA+float2(cos(angleA),sin(angleA))*boilA*.7;
  float2 boilShiftB=jitterB+float2(cos(angleB),sin(angleB))*boilB*.7;
  float4 pool=lerp(liquidDetailAt((i.position.xy+boilShiftA)/liquidFoamTile,liquidFoamTile,i.gradient),
                   liquidDetailAt((i.position.xy+boilShiftB)/liquidFoamTile+.41,liquidFoamTile,i.gradient),boilBlend);
  float4 poolFine=lerp(liquidDetailAt((i.position.xy-boilShiftA*.7)/liquidFoamFineTile+.53,liquidFoamFineTile,i.gradient),
                       liquidDetailAt((i.position.xy-boilShiftB*.7)/liquidFoamFineTile+.17,liquidFoamFineTile,i.gradient),boilBlend);
  float coarse=pool.b*.5+pool.g*.5,fine=poolFine.b;
  float2 foamSlope=(poolFine.gb-.5)*2.2+(pool.gb-.5)*.8;
  [branch]if(running>0) {
   float2 shiftA=riverShift*(riverA-.5),shiftB=riverShift*(riverB-.5);
   float4 fa=liquidDetailAt((i.position.xy-shiftA)/liquidFoamTile+.31,liquidFoamTile,riverGradientA);
   float4 fb=liquidDetailAt((i.position.xy-shiftB)/liquidFoamTile+.77,liquidFoamTile,riverGradientB);
   float4 ga=liquidDetailAt((i.position.xy-shiftA)/liquidFoamFineTile+.19,liquidFoamFineTile,riverGradientA);
   float4 gb=liquidDetailAt((i.position.xy-shiftB)/liquidFoamFineTile+.61,liquidFoamFineTile,riverGradientB);
   // Constant-contrast blends: the thresholded foam masses would otherwise
   // shrink at every crossfade and the foam would pulse.
   float contrast=liquidPhaseContrast(riverBlend);
   coarse=lerp(coarse,.5+(lerp(fa.b*.5+fa.g*.5,fb.b*.5+fb.g*.5,riverBlend)-.5)*contrast,running);
   fine=lerp(fine,.5+(lerp(ga.b,gb.b,riverBlend)-.5)*contrast,running);
   foamSlope=lerp(foamSlope,(lerp(ga.gb,gb.gb,riverBlend)-.5)*2.2*contrast,running);
  }
  // Soft rounded masses where the foam is thick, a lace network of thin
  // foam lines (ridges of the fine churn) on the coarse patches around them,
  // and a solid boil right under a fall.
  float pattern=coarse*.72+fine*.28;
  float soft=lerp(.3,.18,amount);
  // The boil's outline follows the coarse churn, so it frays out irregularly.
  float boil=smoothstep(.35,.95,impact*lerp(.55,1.35,coarse));
  float thickness=min(amount*liquidFoamMass+boil*liquidFoamBoil,.75);
  float masses=smoothstep(1-thickness-soft,1-thickness+soft,pattern);
  float lace=smoothstep(.7,.9,1-abs(fine-.5)*2)*smoothstep(.4,.7,coarse)*saturate(amount*2.5);
  cover=max(masses,lace*liquidFoamLace)*smoothstep(.04,.4,amount*lerp(.4,1.5,coarse));
  // Lumpy relief: lit unevenly, never flat; the boil lies flat on the pool.
  float3 foamNormal=liquidSafeNormal(float3(i.current*(1-boil)+foamSlope,1));
  float3 light=ambientN*1.15+sunN*(.3+.7*saturate(dot(foamNormal,l)))*.75;
  color=foamBase*light*liquidFoamBrightness*lerp(.75,1.08,smoothstep(.2,.8,fine))*lerp(.85,1.05,smoothstep(.3,.7,coarse));
 }
 // Falling water carries no foam of its own: the waterfall shell does.
 cover*=(1-waterfall)*(1-liquidCurtainWeight(i,running));
 return float4(color,cover*liquidFoamOpacity);
}
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

// Physical water's refracted scene (rgb, as composited) and its share (a): the
// scene copy already holds the bed's border fog and atmosphere, so the pixel
// entry fogs only the rest (fogging it again lightened and flattened water
// toward every map edge, where border and seam fog rise).
static float4 liquidTransmitted=0;
// Physical water's per-channel share of the scene behind it (`through`
// below), for the guide twins (NATIVE_LIQUID_GUIDES); -1: not set (the other
// liquid paths cover what is behind them by their alpha).
static float3 liquidThrough=-1;
float4 shadeLiquidSurface(LiquidSurfaceInput i,float3 ambient,
 float3 sunDirection,float3 sunColor,float3 skyColor) {
 liquidTransmitted=0;liquidThrough=-1;
 liquidBedSmooth=liquidSmoothBedDepth(i.pixel,i.position,i.depth,i.footprint);
 // A lifted pit (impact funnel) lies flat: its authored slope no longer runs.
 float pit=smoothstep(0,.6,i.fill);
 i.current*=1-pit;
#ifdef NATIVE_LIQUID_SSR_INPUT
 // Reflection quality below high (under 40 march steps): the half-resolution
 // rays take the flat flow normal, without the ripple, chop and surf shading
 // that otherwise ran here only to bend them (about 0.7 ms of the 1.1 ms
 // reflection pass on the gipat1 sea at 4K). Reflections ripple less there.
 [branch]if(liquidReflectionQuality.x<40) {
  float3 view=liquidSafeNormal(i.viewDirection),flat=reflect(-view,liquidSafeNormal(float3(i.current,1)));
  liquidSsrRayOut=float4(flat,dot(view,flat));
  liquidSsrPlaceOut=float4(i.position,liquidSsrAllowed(i.selfIllumination)?liquidSsrSpread/max(i.footprint,1e-4):0);
  return 0;
 }
#endif
 float windLength=length(i.windDirection);
 float2 along=windLength>1e-6?i.windDirection/windLength:float2(0,1);
 // On the sea the small waves drift with the swell toward the shore, not
 // along the game's wind (offshore wind made the sea seem to stream away
 // from the beach).
 float4 surfField=liquidSurfAt(i.position.xy);
 // One direction for all water of the map (the regional swell direction
 // where it has one), never per pixel: the drift is direction times game
 // time, so any spatial change of direction shears the ripples without
 // bound. Blending it by the per-pixel sea weight smeared them into streaks
 // and arcs that grew with time wherever that weight changes: along the sea
 // field's end at the map edge (a V at the corners), toward rivers and near
 // shores (save1146, user F11s).
 if(liquidSurfSwellDir.z>=.3)along=liquidSurfSwellDir.xy;
 float seconds=i.time*liquidSecondsPerTime;
 float calm=saturate(i.selfIllumination);
 float windiness=lerp(liquidWaveMinimum,1,saturate(abs(i.multiplier)*saturate(i.windForce)*1.5));
 // Two phases restart every period and crossfade (flow-map technique). A
 // smooth per-area phase offset keeps the crossfade from pulsing in unison.
 float phaseOffset=liquidDetailAt(i.position.xy/liquidFlowPhaseTile,liquidFlowPhaseTile,i.gradient).g*2;
 float flowCycle=i.time/liquidFlowPeriod+phaseOffset;
 float flowA=frac(flowCycle),flowBlend=abs(1-2*flowA);
 // Running water: downhill along the liquid surface, faster and foamier with
 // its slope; near-vertical surfaces become waterfalls. The flow is a vertex
 // value, so flow*time would shear without bound: the river phases advect
 // the ripples by at most half a period's travel and restart.
 float runSlope=length(i.current);
 float2 flowDir=runSlope>1e-5?i.current/runSlope:along;
 float steep=runSlope;
 float running=smoothstep(liquidRiverOnset.x,liquidRiverOnset.y,runSlope)*liquidShoreEnabled(i.selfIllumination);
 float waterfall=smoothstep(liquidWaterfallSlope.x,liquidWaterfallSlope.y,steep)*running;
 float lip=liquidLipApproach(i,running,waterfall);
 float riverSpeed=min(liquidRiverSpeed.x+liquidRiverSpeed.y*runSlope,liquidRiverSpeed.z)*(1+lip*liquidLipSpeed);
 float3 riverPhase=liquidRiverPhases(seconds,phaseOffset);
 float riverA=riverPhase.x,riverB=riverPhase.y,riverBlend=riverPhase.z;
 // Velocity is the current scaled (to zero below the onset), so it fades out
 // smoothly instead of normalising tiny, direction-unstable currents.
 float flowGain=liquidRiverShiftLength(riverSpeed)/liquidRiverPeriod*saturate(runSlope/liquidRiverOnset.y)/max(runSlope,1e-4);
 float2 riverShift=i.current*flowGain*liquidRiverPeriod;
 float4 riverGradient=liquidStreakGradient(i.gradient,flowDir,running*riverSpeed*liquidRiverStreak);
 // The advected coordinate also changes with the flow across the surface:
 // where the flow bends the texture compresses, so its footprint widens by
 // the decoded bend (a smooth vertex value). Per-triangle screen derivatives
 // of the flow made the mip jump at triangle edges (lines below falls).
 float widen=i.bend*flowGain*liquidRiverPeriod;
 // Where the flow bends hard (pools, eddies) the advected warp would fold the
 // texture onto itself; limit it to at most half a texture stretch.
 float fold=min(1,1/max(widen,1e-4));
 riverShift*=fold;widen*=fold;
 float4 riverGradientA=riverGradient*(1+widen*abs(riverA-.5));
 float4 riverGradientB=riverGradient*(1+widen*abs(riverB-.5));
 [branch]if(i.layer!=0) {
  float3 layerTint=saturate(i.tint);
  float layerMud=i.mud*liquidShoreEnabled(i.selfIllumination);
  if(i.selfIllumination>0)return 0;
  return liquidFoamLayer(i,ambient,sunColor,liquidSafeNormal(sunDirection),layerTint,layerMud,seconds,phaseOffset,
                         running,steep,waterfall,flowDir,riverShift,riverA,riverB,riverBlend,riverGradientA,riverGradientB);
 }
 // Sea surf exposure here (0 off exposed coasts: there the terms below add
 // nothing and the water is unchanged). Never on swamp, lava or running water.
 float surfGate=liquidShoreEnabled(i.selfIllumination)*(1-saturate(i.mud))*(1-running);
 float surf=surfField.r*surfGate;
 float surfDebug=fmod(liquidSurfMode.y,4); // water_surf_debug: 1 exposure, 2 shore impact
 [branch]if(surfDebug>.5&&surfDebug<2.5&&LIQUID_WATERLIKE(i.selfIllumination)&&surfField.a<liquidSurfBand-.01)
  return float4(surfDebug<1.5?liquidSurfDebug(surfField,i.position.xy):liquidSurfImpactDebug(liquidSurfLevelAt(i.position.xy).y),1);
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif
 // Two detail layers drifting in different directions (no single sliding sheet).
 float2 across=float2(-along.y,along.x);
 float speed=liquidDetailDrift*lerp(1,liquidEmissiveFlow,calm)*seconds;
 float2 p=i.position.xy-(along*.8+across*.6)*speed;
 float2 q=i.position.xy-(along*.7-across*.7)*speed*1.3;
 float2 r=liquidDetailRotation,other=float2(r.y,-r.x);
 float3 detail=liquidDetailScale;
 float3 a=liquidFilteredGradient(p*detail.x,i.footprint*detail.x);
 float3 b=liquidFilteredGradient(liquidRotate(q,r)*detail.y+float2(19,37),i.footprint*detail.y);
 float3 c=liquidFilteredGradient(liquidRotate(p,other)*detail.z+float2(43,11),i.footprint*detail.z);
 // Surface normals from the generated tiling normal map: two scales, each with two
 // layers drifting in different directions. Mip filtering keeps every distance
 // consistent (no flat/ripply patches, no aliasing). Amplitude is uniform; wind
 // and authored waves only nudge it.
 // Repeat breakup (#139). The normal layers tile every 2-11 m and the
 // repeat showed at every distance. A slow world-space warp (one detail read
 // at a tile that is no multiple of the normal tiles) shifts the ripple
 // pattern smoothly by up to liquidWarpAmount, so neighbouring repeats never
 // line up; its slope stays small, so ripples are not visibly stretched.
 // Ripple strength also varies in broad calmer and rougher patches.
 float4 warpSample=liquidDetailMap.SampleLevel(liquidNormalSampler,
                                               liquidRotate(i.position.xy,float2(.96,.28))/liquidWarpTile+.43,liquidBroadLevel);
 float2 wavePosition=i.position.xy+(warpSample.ga-.5)*4*liquidWarpAmount;
 float3 waveField=0;
 [branch]if(running<1)
  waveField=lerp(liquidWaveSlope(wavePosition,along,seconds,i.gradient),
                 liquidWaveSlope(wavePosition+float2(3.7,1.9),along,seconds,i.gradient),flowBlend);
 [branch]if(running>0) {
  float3 river=liquidRiverWaves(wavePosition,riverShift,riverA,riverB,riverBlend,riverGradientA,riverGradientB);
  waveField=lerp(waveField,river,running);
 }
 // Distant pixels widen the strength range and add one broad rotated swell
 // layer, where many periods share the screen.
 float far=smoothstep(liquidFarBreakup.x,liquidFarBreakup.y,i.footprint);
 float farPatch=liquidDetailMap.SampleLevel(liquidNormalSampler,
                                            liquidRotate(i.position.xy,float2(.28,.96))/liquidFarPatchTile+.29,liquidBroadLevel).g;
 float2 strengthRange=lerp(liquidNearPatchRange,liquidFarPatchRange,far);
 float strength=lerp(strengthRange.x,strengthRange.y,smoothstep(.25,.75,farPatch));
 waveField=float3(waveField.xy*strength,waveField.z*strength*strength);
 [branch]if(far>0) {
  float3 swell=liquidNormalAt((liquidRotate(i.position.xy,float2(-.53,.85))+along*seconds*liquidNormalSpeed.w)/liquidFarSwellTile+.71,
                              liquidFarSwellTile,i.gradient,float2(-.53,.85));
  waveField.xy+=swell.xy*liquidFarSwellWeight*far;
 }
 float2 wave=waveField.xy;
 float amplitude=liquidNormalAmplitude*lerp(.8,1,windiness)*lerp(1,.25,calm)*lerp(1,liquidRiverNormal,running*saturate((riverSpeed-1)/2))*(1+lip*liquidLipChop);
 float slopeVariance=waveField.z*amplitude*amplitude;
 float2 slope=-wave*amplitude;
 float2 shoreSlope=0;
 float3 ripples=0;
 [branch]if(LIQUID_WATERLIKE(i.selfIllumination))ripples=liquidWaveRipples(i.position.xy,i.current,seconds,i.footprint);
 shoreSlope+=ripples.xy;
 // Sea surf waves: the slope of the same height field the domain stage
 // displaces (the swell analytically, the chop by finite differences with
 // all its octaves, the finest fading with the footprint), so normals match
 // the geometry; untessellated water (neighbour maps, tessellation off)
 // takes the slope alone.
 LiquidSurfSwell surfSwell=(LiquidSurfSwell)0;float surfImpact=0;float4 surfLevel=0;float2 surfChop=0;
 [branch]if(surf>1e-3) {
  surfLevel=liquidSurfLevelAt(i.position.xy);surfImpact=surfLevel.y;
  // Forward differences of the smooth swell distance over one metre.
  float2 swellGradient=float2(liquidSurfLevelAt(i.position.xy+float2(1,0)).z,liquidSurfLevelAt(i.position.xy+float2(0,1)).z)-surfLevel.z;
  surfSwell=liquidSurfSwellAt(i.position.xy,surfField,surfLevel,swellGradient,seconds,i.depth);
  surfChop=liquidSurfChopSlope(i.position.xy,seconds,liquidSurfOctaves(false),i.footprint)*
           liquidSurfChopWeight(surfField,surfLevel,i.depth);
  // Far away (swell crests under a pixel or two) its slope eases off instead of sparkling.
  shoreSlope+=(surfSwell.slope*lerp(.5,1,liquidBandWeight(i.footprint/(max(liquidSurfWaveParams.y,1)*.5)))+surfChop)*surfGate;
 }
 // Rain rings on water and swamp (not lava). The swamp reflects little,
 // so its rings also lighten the murky body along their crests (run 7: no
 // rain ripples were visible in the swamp).
 float rain=liquidPhysical()&&LIQUID_WATERLIKE(i.selfIllumination)?saturate(liquidRippleMeta.w):0;
 float rainCrest=0;
 [branch]if(rain>0) {
  float3 drops=liquidRainRipples(i.position.xy,seconds,rain,i.footprint);
  shoreSlope+=drops.xy;slopeVariance+=drops.z;
  rainCrest=saturate(length(drops.xy)*.8);
 }
 // The sloped liquid surface tilts the geometric normal (i.current = -grad h).
 float3 n=liquidSafeNormal(float3(i.current+flowDir*lip*liquidLipTilt-slope-shoreSlope,1));
 float3 v=liquidSafeNormal(i.viewDirection),l=liquidSafeNormal(sunDirection);
 float depth=max(i.depth,0);
 float absorption=saturate((1-exp(-liquidAbsorption*depth))/
                           (1-exp(-liquidAbsorption*liquidOpaqueDepth)));
 float3 tint=saturate(i.tint);
 float mud=i.mud*liquidShoreEnabled(i.selfIllumination);
 // Water takes its object's colour. The game draws a liquid as its tile art
 // and uses the object colour only for the wave glint, so the swamp body
 // takes the colour of its own art frame (dark green in the shipped swamp
 // tiles, whatever object they are painted on); only without art does it
 // fall back to the object colour, darkened as murk.
 // mud is already the soft swamp coverage (liquidSwampCoverage): the tint
 // follows it directly. A second threshold here lagged the opacity and the
 // reflection terms, leaving lighter, more opaque clear water along the edge.
 bool swampArt=i.art.a>0;
 float swampTintWeight=mud;
 float3 swampTint=swampArt?saturate(i.artMean):tint;
 float3 bodyTint=lerp(tint,swampTint,swampTintWeight);
 float3 albedo=lerp(lerp(bodyTint,lerp(float3(1,1,1),swampTint,swampTintWeight),liquidShallowLighten),
                    bodyTint*liquidDeepBrightness,1-exp(-liquidColorAbsorption*depth));
 albedo*=lerp(1,liquidMudBrightness,swampArt?0:mud);
 albedo*=1+mud*rainCrest*.45;
 float facing=saturate(dot(n,v));
 float fresnel=max(liquidFresnel(facing,i.selfIllumination),liquidReflectMinimum*liquidReflectionWeight(i.selfIllumination));
 fresnel*=lerp(1,.55,mud); // murky swamp pools reflect less
 float3 reflected=reflect(-v,n);
#ifdef LIQUID_OWNED_SKY
 float3 sky=liquidOwnedSky(reflected,skyColor,liquidSkyZenith); // sky_clouds.h reflection probe
#else
 float3 sky=skyColor*lerp(1,liquidSkyZenith,saturate(reflected.z));
#endif
 // Coherent SSR rays: trace along a flattened normal so nearby geometry reflects
 // instead of scattering every ray off-screen.
 float3 mirror=reflect(-v,liquidSafeNormal(lerp(liquidSafeNormal(float3(i.current,1)),n,liquidSsrFlatten)));
 float hidden;
 float spread=liquidSsrSpread+liquidSsrRippleSpread*sqrt(max(slopeVariance,0));
#ifdef NATIVE_LIQUID_SSR_INPUT
 // The half-resolution reflection pass's inputs (liquidSsrRay/liquidSsrPlace):
 // the march reads the cone spread only per pixel footprint.
 liquidSsrRayOut=float4(mirror,dot(v,mirror));
 liquidSsrPlaceOut=float4(i.position,liquidSsrAllowed(i.selfIllumination)?spread/max(i.footprint,1e-4):0);
 return 0;
#endif
 float4 scene=liquidResolvedReflection(i.pixel,i.position,mirror,i.selfIllumination,i.footprint,hidden);
 sky=lerp(sky,scene.rgb,scene.a);
 // A miss that ran behind geometry ahead of it reflects less, so the water's
 // own colour shows there instead of bright sky (liquidSsrHiddenKeep).
 fresnel*=lerp(1,liquidSsrHiddenKeep,hidden);
 float3 lit=albedo*max(ambient,sunColor*saturate(dot(n,l)));
 float highlight=pow(saturate(dot(n,liquidSafeNormal(v+l))),liquidSpecularPower)*
                 saturate(dot(n,l))*liquidSpecularStrength*liquidReflectionWeight(i.selfIllumination);
 float3 specular=sunColor*tint*highlight;
 bool physical=liquidPhysical();
 // Physical sun highlight: GGX with roughness widened by the ripple variance
 // that normal-map filtering removed, so distant water keeps its glitter
 // energy as a broader lobe instead of sparkling or vanishing.
 [branch]if(physical)
  specular=sunColor*liquidSunGgx(n,v,l,liquidRoughness*liquidRoughness+2*slopeVariance)*
           liquidReflectionWeight(i.selfIllumination)*lerp(1,.55,mud);
 float3 color=lerp(lit,sky,fresnel);
 if(!physical)color+=specular;
 // Swamp: matte algae mats over darker reflective pools (mats take no
 // reflection or highlight and are opaque).
 float algae=0;
 [branch]if(mud>0) {
  // Two scales (one rotated) break the repeat; fine turbulence frays the edges.
  float4 m=liquidDetailAt(i.position.xy/liquidAlgaeTile+.23,liquidAlgaeTile,i.gradient);
  float4 m2=liquidDetailAt(liquidRotate(i.position.xy,float2(.6,.8))/(liquidAlgaeTile*1.63)+.71,
                           liquidAlgaeTile*1.63,i.gradient,float2(.6,.8));
  algae=smoothstep(.40,.50,m.a*.6+m2.a*.4+(m.b-.5)*.12)*mud;
  float3 mat=i.art.a>0?i.art.rgb:saturate(i.tint)*liquidAlgaeBrightness*lerp(.55,1.35,m.b);
  color=lerp(color,mat*max(ambient,sunColor*saturate(l.z)),algae);
 }
 float variation=dot(float3(a.x,b.x,c.x),liquidHeightWeights);
 float brightness=lerp(liquidColorDetail.x,liquidColorDetail.y,variation);
 color*=brightness;
 [branch]if(calm>0) {
  // Lava: warped turbulent heat with hot veins (generated map, two scales
  // drifting slowly apart) and a slow heat pulse.
  // Mixing flow: a slow coarse field drifts one way and warps the main layer,
  // which drifts another way, so the surface churns instead of sliding.
  float2 lavaDrift=along*seconds*liquidLavaDrift;
  float4 warp=liquidDetailAt((liquidRotate(i.position.xy,float2(.6,-.8))+lavaDrift*.7)/(liquidLavaTile.x*2.3)+.17,
                             liquidLavaTile.x*2.3,i.gradient,float2(.6,-.8));
  float2 churn=(warp.gb-.5)*1.6+float2(sin(seconds*.21),cos(seconds*.17))*.35;
  float4 l1=liquidDetailAt((i.position.xy-lavaDrift*.5+churn)/liquidLavaTile.x,liquidLavaTile.x,i.gradient);
  float4 l2=liquidDetailAt((liquidRotate(i.position.xy,float2(.8,.6))+lavaDrift*.6-churn*.6)/liquidLavaTile.y+.41,
                         liquidLavaTile.y,i.gradient,float2(.8,.6));
  float pulse=.5+.5*sin(seconds*.6+l1.g*6.2831853);
  float veins=max(l1.r,l2.r*.7);
  float heat=saturate((l1.g*.75+l2.g*.25-.5)*2.1+.40+veins*.15*(.75+.25*pulse));
  float3 molten=liquidMoltenColor(color,i.emissive,heat)*lerp(.72,1.22,l2.b);
  color=lerp(color,molten,calm);
 }
 float muddy=mud;
 float patch=lerp(.02,smoothstep(liquidBubblePatch.x,liquidBubblePatch.y,liquidNoise(p*liquidBubbleScale)),
                  liquidBandWeight(i.footprint*liquidBubbleScale));
 float speckle=lerp(.2,smoothstep(liquidBubbleSpeckle.x,liquidBubbleSpeckle.y,
                         liquidNoise(q*liquidBubbleFineScale+float2(i.time*liquidBubbleSpeed,0))),
                    liquidBandWeight(i.footprint*liquidBubbleFineScale));
 float bubbles=patch*speckle;
 color+=lerp(tint,float3(1,1,1),liquidBubbleWhiten)*bubbles*muddy*
        liquidBubbleBrightness*liquidShoreEnabled(i.selfIllumination);
 float crest=0;
 float4 foamDetail=.5;
 [branch]if(LIQUID_WATERLIKE(i.selfIllumination)) {
  foamDetail=liquidDetailAt(i.position.xy/3+float2(seconds*.05,0),3,i.gradient);
  crest=smoothstep(.45,.85,length(wave))*smoothstep(.35,.75,foamDetail.b)*liquidCrestFoam*(1-algae);
 }
 // Swamp water has no surf: the shore band (water shallower than about
 // liquidShoreDepth) fades out with the swamp share. Over a flat, shallow,
 // bumpy swamp bed every depth contour near that threshold drew a band, so
 // the bank showed nested polygonal stripes (save1133).
 float shore=liquidShoreAmount(i)*(1-mud)+ripples.z+crest;
 float3 foam=lerp(tint,float3(1,1,1),liquidShoreWhiten);
 color+=foam*max(ambient,sunColor*saturate(dot(n,l)))*shore*liquidShoreBrightness;
#ifdef __cplusplus
//")hlsl" R"hlsl( //"
#endif

 // Sea surf body, tint and foam. Body: light through the thin top of a
 // swell crest, darker troughs (volume). Tint: clear turquoise shallows
 // turning deeper blue with depth, per-channel Beer-Lambert (physical water
 // tints the refracted bed in its composite below; otherwise the body colour
 // leans that way). Foam: one density from the shore (a scalloped edge band
 // riding each arriving crest's swash, lace behind it), breaking swell crests
 // (each crest with its own foam layout), steep crests and bursts off steep
 // shores, drawn by liquidSurfFoamPattern (lace thickening into foam with
 // holes, bubbles, a darker rim) and lit by the smooth swell surface, not the
 // fine chop (its ridges combed the foam).
 float surfFoam=0,surfShade=0,surfTint=0,seaScatter=0;float3 seaFoamLit=0;
 [branch]if(surf>1e-3) {
  float depthHere=max(i.depth,0);
  float wall=smoothstep(.1,.4,surfImpact);
  float cell=max(liquidSurfFoamParams.w,.2);
  uint quality=uint(min(floor(liquidSurfMode.y*.25),2));
  float3 smoothN=liquidSafeNormal(float3(i.current-surfSwell.slope*.6,1));
  float3 lightN=ambient*1.1+sunColor*.7*saturate(dot(smoothN,l));
  // Body.
  float backlit=.55+.45*saturate(dot(-v.xy,l.xy)*2);
  float thin=surfSwell.front*smoothstep(.55,.95,surfSwell.crest)*surfSwell.wave;
  color=lerp(color,lerp(liquidSurfShallowColor,saturate(tint),.4)*(ambient*1.1+sunColor*.6*backlit),thin*.22*surfGate);
  color*=1-.16*surfSwell.wave*(1-smoothstep(.1,.5,surfSwell.crest))*surfGate;
  // Tint.
  surfTint=surf*liquidSurfLookParams.x*(1-.6*wall);
  if(!physical&&surfTint>0) {
   float3 through=exp(-liquidSurfAbsorb*liquidSurfLookParams.y*depthHere);
   color=lerp(color,lerp(liquidSurfDeepColor,liquidSurfShallowColor,through.g)*lightN,saturate(surfTint)*.45*(1-smoothstep(0,3,depthHere)));
  }
  // Foam density, the write-up's foam level: shallow water (the shore band,
  // breathing with each crest's swash) plus steep crests, plus breaking
  // swell crests and bursts off steep shores; summed and clamped.
  float band=length(surfField.gb);
  float2 incoming=band>1e-3?surfField.gb/band:liquidSurfSwellDir.xy;
  float3 arrival=liquidSurfArrival(i.position.xy+incoming*max(surfLevel.z,0),incoming,surfField.r,seconds);
  float swash=arrival.x<liquidSurfRunupRise?sin(1.5707963*arrival.x/liquidSurfRunupRise):1-smoothstep(liquidSurfRunupRise,liquidSurfRunupRise+.55,arrival.x);
  float width=max(liquidSurfFoamParams.y,.01);
  float surfDetail=liquidBandWeight(i.footprint/cell);
  float t=seconds*.6;
  float lift=width*.45*swash*arrival.y; // the band reaches farther out as a crest washes in
  // Beach: by water depth. A steep shore (depth jumps at the rock, so a depth
  // band collapsed into a hard white outline): by distance from the rock,
  // swelling with each arriving crest and draining back between them, so it
  // reads as water slapping the wall, never a painted line.
  // m from the drawn waterline: the nearer of the depth estimate and the
  // field distance (a blend switched to the coarse field where the water is
  // deep right at the rock and broke the foam line there).
  float fromWall=min(depthHere/.35,max(surfField.a,0));
  float slap=1-smoothstep(.15,.85,arrival.x);
  float wallFoam=(1-smoothstep(0,.5+2*slap*arrival.y,fromWall))*lerp(.15,.75,slap);
  float shoreFoam=lerp(smoothstep(width,0,depthHere-lift),wallFoam,wall)*saturate(liquidSurfFoamParams.x);
  // Crests: steep faces of the swell and chop surface (without the fine
  // ripple normals), and breaking swell crests trailing foam behind them.
  // Whitecaps from the chop (and a share of the swell): the swell's narrow
  // breaking front alone drew one long thin white line along each crest.
  float3 crestN=liquidSafeNormal(float3(-(surfSwell.slope*.6+surfChop*1.25),1));
  // Whitecaps: steep wave tops, broken into patches (never a ruled line).
  float crestFoam=smoothstep(liquidSurfFoamParams.z+.01,liquidSurfFoamParams.z-.05,crestN.z)*2*surf;
  // Breaking crests: white water on the top and spilling behind it, a few
  // metres wide, broken along the crest by slow noise (never a ruled line).
  float breaking=surfSwell.breaking*surfSwell.wave*(surfSwell.front*smoothstep(.6,.95,surfSwell.crest)*.6+
                 (surfSwell.behind>0?(1-smoothstep(0,.6,surfSwell.behind)):0));
  [branch]if(breaking>0)breaking*=smoothstep(.15,.65,liquidSurfNoise3(float3(i.position.xy/max(liquidSurfShapeParams.y*.35,2),t*.05+surfSwell.seed*7))+.15);
  // Steep shores: each arriving crest bursts white off the rock and falls
  // back into churned foam drifting off it.
  float burst=0;
  [branch]if(wall>0) {
   float spread=liquidSurfLookParams.z*(1+5*arrival.y)*smoothstep(0,.3,arrival.x);
   burst=max((1-smoothstep(spread*.6,spread+.6,fromWall))*(1-smoothstep(.25,.9,arrival.x)),
             (1-smoothstep(.3,1.4,fromWall))*.25)*wall*min(liquidSurfLookParams.z,1.5)*.8;
  }
  // Forming crests out at sea: white on the tops of the larger swell crests
  // before they break, in broken patches, more with wind. From the swell
  // shape (not the chop, whose fine slope filters away with distance), so
  // they read from the game camera's distance.
  float forming=surfSwell.wave*smoothstep(.7,.96,surfSwell.crest)*(1-surfSwell.breaking)*(.35+.65*liquidSurfSwellDir.w);
  [branch]if(forming>0)forming*=smoothstep(.45,.75,liquidSurfNoise3(float3(i.position.xy/max(liquidSurfShapeParams.y*.5,3),t*.04+surfSwell.seed*5)));
  float density=saturate(shoreFoam+crestFoam+forming*.8+breaking*1.3+burst)*surf;
  float2 fp=i.position.xy/cell; // no per-crest reseed here: it would pop the shore foam as each crest arrives
  float3 pattern=liquidSurfFoamPattern(fp,density,t,surfDetail,quality,i.footprint/cell);
  [branch]if(abs(fmod(liquidSurfMode.y,4)-3)<.5)return float4(pattern.xxx,1); // water_surf_debug 3: the surf foam alone
  // Body: thin foam lets the water through (translucent lace), thick foam is
  // opaque; soft brightness variation inside it so a sheet never reads as
  // flat paper close up.
  float body=lerp(.5,1,smoothstep(0,.6,pattern.z));
  float grain=liquidSurfNoise(fp*3.1+float2(t*.07,-t*.05))*.5+liquidSurfNoise(fp*7.7-float2(t*.04,0))*.25;
  surfFoam=pattern.x*body*(1-algae)*surfGate;
  surfShade=pattern.y*(1-algae)*surfGate;
  color=lerp(color,color*liquidSurfFoamShadow,surfShade);
  seaFoamLit=lerp(tint,float3(1,1,1),.93)*lightN*lerp(.86,1.04,pattern.z)*(1+.07*grain); // near-white: a tinted mix read grey and dirty
  color=lerp(color,seaFoamLit,surfFoam);
  // Light through the thin top of a wave (the ocean's subsurface glow),
  // strongest looking toward the sun; used by the physical sea model below.
  seaScatter=saturate(surfSwell.crest)*surfSwell.wave*(.25+.75*saturate(dot(-v.xy,l.xy)*2))*surfGate;
 }

 // Whitewater is the foam layer (liquidFoamLayer, its own pass over this
 // one). The water body keeps what lies under the foam: milky, bubbly water
 // around a fall's impact, and on a fall the darker flowing sheet.
 float foamCover=0;
 [branch]if(LIQUID_WATERLIKE(i.selfIllumination)&&i.cascade>0) {
  float milky=pow(saturate(i.cascade),4)*smoothstep(.1*liquidWaterfallAeration,1.5*liquidWaterfallAeration,i.drop)*
              liquidCascadeMilk*(1-algae)*(1-waterfall);
  float3 milkLight=ambient*1.1+sunColor*.5*saturate(l.z);
  color=lerp(color,lerp(bodyTint,lerp(tint,float3(1,1,1),liquidShoreWhiten),.5)*milkLight*.8,milky);
 }
 // Falling water (the clear body under the waterfall shell's foam): darker,
 // translucent water in its own tint, keeping a share of the running water's
 // ripples and light; diffuse, so it takes little sky sheen or sun highlight.
 float curtain=LIQUID_WATERLIKE(i.selfIllumination)?liquidCurtainWeight(i,running):0;
 float fallBody=max(waterfall,curtain)*(LIQUID_WATERLIKE(i.selfIllumination)?1:0);
 float fallOpacity=lerp(liquidFallSheetAlpha(i),liquidCurtainOpacity,curtain);
 [branch]if(fallBody>0) {
  float3 body=bodyTint*.75*(ambient*1.1+sunColor*.5*saturate(l.z));
  color=lerp(color,lerp(body,color,.4),fallBody);
  fresnel*=1-.85*fallBody;
  specular*=1-fallBody;
 }
 waterfall=fallBody;
 float froth=foamCover;
 float authored=saturate(i.opacity);
 float alpha=authored*lerp(lerp(liquidShallowOpacity,liquidMudOpacity,mud),1,absorption);
 alpha=min(authored,alpha+shore*liquidShoreOpacity);
 alpha=max(alpha,ripples.z*liquidWaveFoamOpacity); // wake foam stays visible on clear water
 alpha=max(alpha,max(surfFoam*.92,surfShade*.35)); // surf foam and its darker edge too
 alpha=lerp(alpha,authored,smoothstep(liquidOpaqueMaterialStart,1,authored));
 alpha=lerp(alpha,max(authored,.95),algae);
 alpha=lerp(alpha,max(alpha,liquidFoamOpacity),froth);
 alpha=lerp(alpha,fallOpacity*authored,waterfall);
 [branch]if(physical) {
  specular*=(1-algae)*(1-foamCover)*(1-surfFoam);
  [branch]if(LIQUID_WATERLIKE(i.selfIllumination)) {
   // Beer-Lambert per channel along the refracted view path; the authored
   // opacity range is kept, only depth, path and colour become physical.
   float4 behind=liquidRefraction(i,n,v);
   // Sea surf: the bed seen through clear sea water turns turquoise, then
   // deeper blue, per channel along the view path (red goes first).
   float3 absorbed=liquidChannelAbsorbed(bodyTint,behind.a);
   // Over a swimmer the surface veil (the shallow share of the authored
   // opacity) thins to a fifth: the fish is what the pixel shows, absorbed by
   // its own short path, as in the approved preview.
#ifdef NATIVE_LIQUID_SWIMMERS
   float3 opacity=authored*lerp(lerp(liquidShallowOpacity,liquidMudOpacity,mud)*(1-.8*liquidSwimmer),1,absorbed);
#else
   float3 opacity=authored*lerp(lerp(liquidShallowOpacity,liquidMudOpacity,mud),1,absorbed);
#endif
   opacity=min(authored,opacity+shore*liquidShoreOpacity);
   opacity=max(opacity,ripples.z*liquidWaveFoamOpacity);
   opacity=max(opacity,max(surfFoam*.92,surfShade*.35));
   opacity=lerp(opacity,authored,smoothstep(liquidOpaqueMaterialStart,1,authored));
   opacity=lerp(opacity,max(authored,.95),algae);
   opacity=lerp(opacity,max(opacity,liquidFoamOpacity),froth);
   opacity=lerp(opacity,fallOpacity*authored,waterfall);
   // Schlick reflection does not depend on the body's opacity; what is not
   // reflected is transmitted from the refracted scene.
   float3 composite=opacity*color+(1-opacity)*lerp(behind.rgb,sky,fresnel*(1-algae));
   float3 through=(1-opacity)*(1-fresnel*(1-algae));
   // Sea (by the surf weight, fading over the spill into other water): an
   // ocean water model instead of the authored body colour and opacity.
   // The bed along the refracted view path is absorbed per channel (red
   // first: sand turns turquoise, then deep teal) and replaced by light
   // scattered in the water; the thin tops of waves glow; the sky reflects
   // by Fresnel; foam lies on top. No [branch]: the compiler may fold this
   // branch (lava, non-physical variants).
   float seaW=saturate(surf/.35)*saturate(liquidSurfLookParams.x*1.43); // sea weight: full on any sea water (liquid_surf.h liquidSurfSeaFloor), fading over the spill; tint setting 0.7 = full
   if(seaW>0) {
    // Path through the water: the scene depth near the shore, the smoothed
    // depth (liquid_surf.h swell_depth) in deeper water. The terrain heights
    // under the sea are coarsely quantized, so the raw path stepped and the
    // absorbed colour banded in rings following the bed.
    float smoothPath=max(surfLevel.w,0)/max(v.z,.25);
#ifdef NATIVE_LIQUID_SWIMMERS
    float seaPath=lerp(behind.a,smoothPath,smoothstep(1.2,3.5,surfLevel.w)*(1-liquidSwimmer)); // a swimmer keeps its own path
#else
    float seaPath=lerp(behind.a,smoothPath,smoothstep(1.2,3.5,surfLevel.w));
#endif
    float3 T=exp(-liquidSurfAbsorb*liquidSurfLookParams.y*seaPath);
    float3 inscatter=liquidSurfDeepColor*(ambient*.9+sunColor*.55*saturate(l.z));
    float3 water=behind.rgb*T+inscatter*(1-T)+liquidSurfShallowColor*sunColor*seaScatter*liquidSurfGlow;
    float3 sea=lerp(water,sky,fresnel);
    sea=lerp(sea,seaFoamLit,surfFoam);
    composite=lerp(composite,sea,seaW);
    // Dither: the smooth deep teal gradient banded in 8 bits.
    float ign=frac(52.9829189*frac(dot(i.pixel,float2(.06711056,.00583715))));
    composite+=(ign-.5)*(1.5/255)*seaW;
    through=lerp(through,(1-surfFoam)*(1-fresnel)*T,seaW);
   }
   liquidTransmitted=float4(through*behind.rgb,dot(through,1.0/3));
   liquidThrough=through;
   return float4(saturate(composite+specular),i.coverage*lerp(1,liquidFallEdge(i),waterfall));
  }
  color+=specular;alpha*=i.coverage;
  alpha*=lerp(1,liquidFallEdge(i),waterfall);
 }
 // Lava is opaque (user-approved correction: it took the water's depth and
 // authored transparency); only the liquid tile's edge coverage remains.
#ifdef NATIVE_LIQUID_LAVA
 return float4(color,i.coverage);
#else
 return float4(LIQUID_WATERLIKE(i.selfIllumination)?saturate(color):color,alpha);
#endif
}
#ifdef __cplusplus
//")hlsl"
#endif
