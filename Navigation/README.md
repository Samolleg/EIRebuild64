# Nav64 X64NAVCORE candidate

The offline baker and game use the same Recast/Detour v1.6.0 source.
No fingerprint, content hash, stale-input check, legacy-graph fallback or
runtime configuration toggle participates in navigation. Refresh on mapper
save, release with --all and model replacement with --model NAME.

    EIRebuild64NavBake --game-root DIR --mod DIR --map zone1 --mob zone1 --output-root DIR --report-root PRIVATE_DIR
    EIRebuild64NavBake --game-root DIR --mod DIR --all --output-root DIR --report-root PRIVATE_DIR
    EIRebuild64NavBake --game-root DIR --mod DIR --model NAME --output-root DIR --report-root PRIVATE_DIR

Modes are exclusive; --mob requires --map. Output goes to Maps/Nav64 below
output-root. Report-root is mandatory, outside that directory: all CSVs go
there, never into runtime data or the handoff. The optional offline-only
--tile-cells 32|64|96|128 compares bake layouts; normal release and the game
use 96 cells (9.6 m). Other layouts require a matching profile build and are
rejected by the game loader. Exit codes: 0 complete, 2 arguments, 3 setup,
4 map/build/data failure, 5 missing input. --all continues after map errors.
Consume BODIES, QUALITY, HERO_START, REBUILD, PROFILE, SOURCES, MAP_DONE,
MAP_ERROR/MAP_MISSING and BAKE_DONE. Compilation alone is not a passed pilot.

## Geometry and world changes

Static trees/stones/buildings use actual solid FIG triangles; BASE and
authored parts are walkable. Items, traps, stuff, torches and ordinary levers
are dynamic. Literal RemoveUnitFromServer/SetCP script NIDs are dynamic;
variable references can be marked in navsrc. Newly spawned objects not in
the baked catalogue are dynamic. Their real solid parts become oriented
boxes rasterized as non-walkable prisms; they never supply walkable BV tops.

Recast uses 0.1 m cells, 0.05 m vertical cells, climb 0.35 m, slope 40 degrees,
flight slope 60 degrees. Eight profiles retain four clearance tiers each.
Material/water policy is independent of slope. All profiles bake the decoded
centimetre source geometry, exactly as runtime tile rebuilds do. Ready files
are decoded by the runtime loader before baker quality queries.

No TileCache layers or DetourTileCache dependency remain. Regular Recast
compact fields, monotone regions, contours and polygon meshes produce ready
Detour tiles including multiple elevations. No detail-mesh samples are
stored: Detour triangulates the polygon fan, and gameplay supplies support Z.

The game reads ready tiles and one shared navgeom, indexes source triangles
by tile and static object, and queues affected old/new footprints. A single
worker rebuilds only those tile/profile pairs from retained triangles,
excluding removed static NIDs and rasterizing current oriented dynamic boxes.
Snapshot jobs whose tile was dirtied again are discarded. Worker Recast
state, temporary mesh/query and allocations are destroyed on the worker;
the simulation thread only swaps a preallocated tile, refreshes local door
flags and invalidates references by generation. At most one completed tile
is applied per frame. Actual worst-case frame timing needs gameplay evidence.
On zone load, current dynamic obstacles and absent static objects are applied
before placement. Mesh state and process-local references are not saved.

Door material 15 changes flags without rebuilding. Door updates visit only
aperture tiles. Replacements restore current door states and clearance flags.
The baker logs actual worker/apply timing at the zone1 start tile; this does
not establish the worst case for every campaign tile or 100 creatures.

## Authoring

Optional Maps/<mpr>.navsrc is UTF-8 JSON version 1:

    {"version":1,"dynamic":[10000001],"static":[],"walkable":[{"id":123,"part":"base"}],"doors":[{"id":456,"min":[10,20,0],"max":[11,21,3],"open_state":1}]}

Static/dynamic IDs cannot overlap. Part names are lower case. Explicit
static policy overrides literal script classification. Door apertures are
world-space boxes, open_state is the actual lever state. Authored apertures
override automatic MOB isDoor detection. Doors lacking a solid panel in
their authored state need an explicit aperture. Mapper UI integration is
separate from this baker contract.

## Wire format

- <mpr>--<mob>.<profile>.nav64: container 7, algorithm 15, Recast 0x010600,
  DT_POLYREF64, names/profile, ready shuffled/LZMS tiles and surface schema.
  No layer section exists. Loader validates exact profile, bounds and ABI.
- <mpr>--<mob>.navworld: JSON version 2, baked static NIDs.
- <mpr>--<mob>.navgeom: shared LZMS-compressed NSRC version 2, original bounds,
  indexed common signed centimetre vertices; per-triangle indices, centimetre
  water depths, byte material/class metadata, static object and surface IDs;
  door apertures and reserved off-mesh endpoint/surface/radius/flag records.
  One copy serves all profiles. No source import occurs during gameplay.
- Surface 1 is terrain. BASE IDs encode (unsigned NID << 32) | (part + 2).
  Server placement/step/restore resolve exact BASE triangles. Complete client
  floor selection remains open. Off-mesh records remain reserved, not linked.
- bodies.navbody: BODY version 1, algorithm 15, resource-derived models and
  full morph grids, including non-un* models actually used by MOB creatures.
  Runtime never measures bodies. Save field sizes and movement speeds remain.

## Acceptance

HERO_START must advance by 0.02 m in shared Project/Find/Constrain code; the
baker fails if it does not. QUALITY preserves total gained_water_m2 and
separately reports gained_forbidden_water_m2 under the original depth rule.
CSV reports include losses, water reasons, BASE connections and placements.
Runtime performance, save/load, queues/doors, full random-goal coverage,
common-radius class policy and complete client floor selection remain
separate acceptance items. See the private REPORT for measured release data.
