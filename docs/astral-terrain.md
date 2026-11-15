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

## Active-map publication fix (ASTRALTERRAINFIX02)

The original loader hook did not cover `E1PublishTerrainSource`: the active
engine reconstructs its own terrain from map-member callbacks, independently
of the resource loader. Consequently, a loader diagnostic could report Astral
while the actually drawn active map had no mask. The active publisher now calls
the same classifier through the engine's selected texture-resource callback,
which can read the atlas JSON as well as MMP members. No shader or art change
is involved. `astral_terrain_active` reports the active source's slot and placed
base-tile counts once per publication.

Build the x64 integration check with `tools/check-astral-publication.ps1
-OutputDirectory <task>/check`, then run `check-astral-publication.exe <map.mpr>
<texture-archive.res> <expected-Astral-slots>`. It uses the actual E1 publication,
resource parsers, profile classifier and GPU stream decoder; only unrelated
world-frame/lighting hooks are supplied by the harness. Unexpected calls to
unrelated render hooks terminate the test. It also removes the profile and
asserts that no effect is invented. The build reuses the standard build cache's
Pak/Zstd objects and requires that x64 cache to have been prepared.
`-PublicationSource <old-E1Terrain.cpp>` reproduces the regression on old code.

The installed main-menu data exercised 6 slots, 2137 tiles and 19233 marked
GPU vertices. The current Zone1 has no authored Astral and yields zero, as
required. These checks establish publication and decode; a real in-game image
is still the visual check.


## Readability from the main-menu camera (ASTRALREAD03)

The black floor remains RGB (.006, .005, .012). Local violet cloud layers,
filaments and broken shore haze carry more contrast; there is no global
exposure or fog-setting change. World height now contributes to the continuous
noise coordinates so steep painted terrain has variation in the menu's low
camera. Drift speed, material masks, geometry and model veils are unchanged.

On the repeatable GPU plane without shore haze, median linear luminance rises
from .0179 to .0549; the darkest decile remains .0113 and the maximum .1517.
The GPU check bounds median/highlight brightness and checks nonconstant flow
on a vertical surface, in addition to the existing time, shore and mask tests.
These figures are guardrails, not artistic acceptance: the ordinary main-menu
camera after installation is the required visual comparison. Isolated previews
use an sRGB display conversion and do not reproduce the complete game frame.
