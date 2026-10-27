# Nav64 X64NAVCORE working implementation

NOT A RELEASE. The zone1 pilot fails in rcBuildHeightfieldLayers. Do not
install these executables or mix their data with r16. Full status and logs:
work/supervision/X64NAVCORE/REPORT.md in the project workspace. This format
is provisional, not the final architecture requested by the BRIEF.

RecastNavigation v1.6.0 is unchanged. Recast, Detour and DetourTileCache
are compiled with DT_POLYREF64. DetourCrowd is also vendored; the current
steering implementation is in the host.

## Command line and authoring

The game reads baked files and the body table. It does not import FIGs,
measure models, hash sources or compare freshness. The authoring/release
workflow is responsible for rebaking. There is no fingerprint mode/field.

    EIRebuild64NavBake --game-root DIR --mod DIR --map zone1 --mob zone1 --output-root DIR
    EIRebuild64NavBake --game-root DIR --mod DIR --all --output-root DIR
    EIRebuild64NavBake --game-root DIR --mod DIR --model samvlrmp01 --output-root DIR

Exactly one of --map, --all and --model is required. --mob is valid only
with --map. --model selects registered map pairs containing the model in
their primary imported MOB; script-spawned objects are dynamic. Every
mode refreshes all available creature bodies. A mapper save should run
--map and check exit status and BAKE_DONE. The mapper itself is unchanged.

Exit codes: 2 invalid command, 3 setup error, 4 import/build/data error,
5 missing input, 0 complete. BAKE_DONE includes completed/missing/errors/
skipped, time and bytes. QUALITY and CSV reports run after mesh build;
the current pilot fails before reaching them. No campaign release yet.

Optional Maps/<mpr>.navsrc is UTF-8 JSON, version 1:

    {"version":1,"dynamic":[10000001],"static":[],"walkable":[{"id":123,"part":"base"}]}

Static/dynamic IDs cannot overlap. Names are canonical lower case. An
explicit static ID overrides automatic script classification. Literal
integer targets of RemoveUnitFromServer/SetCP in the primary MOB become
dynamic. Variable targets need authoring. Objects absent from the baked
static catalogue (including AddMob objects) are dynamic at runtime.
Incremental tiles, authored door openings and off-mesh links remain open.

## Implementation

Non-dynamic trees/stones/buildings are static unless overridden. Other
physical classes (levers, items, traps, stuff, torches) are dynamic. Units
are neighbours, not baked solids. Static geometry uses actual morphed FIG
faces, BON offsets and object rotation; BASE/authored parts are walkable.
Dynamic obstacles use oriented boxes of live model parts.

Terrain applies the legacy material/water and height-gradient rules. It
has no extra Recast slope veto. Eight radius profiles and four clearance
tiers remain; full class permissions at a common radius are unfinished.

Bodies include effective un*.mod/un*.lnk resources, exact prototype shapes
and the 3x3x3 sample grid. Missing/malformed models are explicit. The shared
renderer decoder now reads headerless FIG through its original six-corner
polynomial, represented exactly by duplicated eight-corner samples.

Movement consumes constrained XY without cell-lock/BV vetoes. The existing
world height function supplies server/client Z. Detour stores no sampled
detail vertices, only the minimum polygon fans needed by queries. Neighbour
discs allow overlap recovery; bounded waits freeze the walker clock before
local tangent steering/replanning. Guard rally goals use reachable points.
Party deployment projects placements within 1 m. Other spawn/menu paths,
other random goal producers and specific floor ownership remain open.

## Provisional wire format

Maps/Nav64/<mpr>--<mob>.<profile>.nav64, little endian:

- NAV664 magic; container 4, algorithm 11, library 0x010600, endian and
  64-bit Detour-reference ABI; map/MOB/profile strings and parameters;
- mesh parameters; section 1 ready tiles (byte shuffle + Windows LZMS);
- section 2 existing single surface-domain marker;
- section 3 compressed TileCache layers with height-layer IDs.

Cells are 0.1 x 0.05 m, tiles 128 cells wide. Layer height/area/connection
grids use lossless packet compression. Offline polygons come from the same
layers; runtime reads ready tiles and rebuilds only obstacle updates.

bodies.navbody retains BODY version 1, algorithm 11, with model/shape,
profile IDs, radius and three pose heights. <mpr>--<mob>.navworld is JSON
version 1 containing static IDs. Dynamic obstacles come from the loaded
world. Cache queues and Detour references are never saved. CSV losses and
placements are private quality reports, not runtime resources.

## Blockers

zone1 fails layer build. Detailed Recast messages are disabled in this
candidate, so profile/tile/reason still need investigation. Static layers
lack per-object attribution: a missing baked static ID currently logs
static_removed but does not restore its floor. Doors use general obstacles
rather than flag-only opening areas. Surface ownership and the off-mesh
field are unfinished; the format is not final. Full class/water policy,
all spawn/random-goal paths, passages and the r16 symptom remain open.

Runtime load/path/frame/update limits and gameplay are unconfirmed. The
r16 journal counted calls, not successful steps or committed positions;
it does not establish the exact flat-ground failure. SHA-256 is artifact
identity in SHA256SUMS only, never a navigation/freshness decision.
