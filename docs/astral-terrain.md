# Astral terrain

ASTRALTERRAIN01 implements the approved dark-abyss concept: slow violet cloud
layers, dim intermittent filaments and a broken haze near land. This is an
opaque terrain material with apparent depth, not a transparent hole or a
volumetric fog emitter. It does not move terrain, change collision or light
nearby objects. There is no new setting or resource archive.

The loader reads the map's existing `ei_atlas_profile_<namespace>_<hash>.json`
using the same namespace/hash contract as terrain families. Only assigned,
unambiguous slots with explicitly named `Astral` corners receive a four-bit
mask. Names are ASCII case insensitive. Black pixels and TERRTYPE are never
classification evidence. The original `ataman_astral000` page also identifies
its authored first cell. Missing/unmatched profiles leave repacked art unchanged.

The mask crosses asset publication (protocol 36) and is rotated into world
corners through the existing terrain UV rule. Base tiles only are eligible;
animated and overlay tiles are excluded. Mixed cells keep normal land shading
and smoothly composite the astral share from bilinear corner weights. Pure
cells bypass terrestrial material shading. Existing native terrain vertex
storage carries the mask, local coordinates and shore distance without changing
the GPU structure layout. Distance is measured against nearby non-pure base
cells, across sector boundaries; absent tiles and outer map boundaries do not
create haze. Haze lookup does not inspect beyond a map into an adjoining map.

Cloud layers use continuous world coordinates and the native logical clock
(`.055` seconds per time unit). Identical clock values reproduce identical
pixels. A bounded camera offset gives the layers apparent depth. Pure and mixed
pixels retain map visibility and the normal atmospheric/distance fog path.
The shared loader/decode/shader path applies to active and neighbour terrain.
Astral model veils and existing liquid shaders are unchanged.

## Verification

`tools/check-astral-terrain.ps1 -OutputDirectory <task>/check` compiles the
production material function on D3D11 WARP and checks classification, exclusions,
rotated corners, sector-edge distance, missing shoreline, finite bounded output,
clock determinism, motion, shore locality and mixed-material coverage. It writes
GPU readback PPM previews at times 0 and 30 seconds; these are material-only
views with a synthetic straight shore, not screenshots of the game.

`-ProfileDirectory <directory>` instead checks supplied real editor JSON
profiles with the production parser, including rejection of wrong namespace and
slot count. Do not commit extracted resource data or diagnostic output.

The ordinary task build compiles native shader variants and the x64 programs.
After an explicitly authorized installation, remaining visual acceptance is:
the painted allod edge and mixed transitions, pause/resume, camera movement and
zoom, fog/day/night, map transition and visible neighbour maps. GPU unit previews
and compilation do not establish in-game appearance or frame-time impact.
