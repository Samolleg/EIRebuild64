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
levitation slope 60 degrees. Six profiles retain four clearance tiers each.
MPR material/water class areas only restrict traversal. The 40/60 degree
slope filter applies to ground and levitation, not flying. All profiles bake the losslessly decoded
imported source geometry, exactly as runtime support and tile rebuilds do. Ready files
are decoded by the runtime loader before baker quality queries.

No TileCache layers or DetourTileCache dependency remain. Regular Recast
compact fields, monotone regions, contours and polygon meshes produce ready
Detour tiles including multiple elevations. Ground/levitation detail samples
are not stored: Detour triangulates the polygon fan, but their floor queries
use original height. Before contouring, compact spans are attributed to original
walkable triangles and regions are split by terrain/BASE identity. Polygons
retain that identity, with only directly connected source neighbours admitted
at voxel boundaries. Height selects the closest original floor after rejecting
lower skins with insufficient requested body clearance beneath an admitted
upper support. The same rule applies across touching polygon references;
query selection uses the requested pose height, not the profile minimum.
Original triangle interpolation supplies Z; intact terrain hides buried BASE.
Catalogue storage is shared by all profiles. Solid geometry never supplies a
floor ID. Raster area merging cannot promote a nearby non-walkable top.
Movement uses Detour's connected surface traversal (512 visited-polygon
bound), accepts at most 1 mm of endpoint roundoff, then checks original Z
against climb + tan(slope) * horizontal distance. A clipped move is rejected.
Layered tiles keep exact contours: simplifying terrain and BASE boundaries
independently can create overlapping polygons and a funnel shortcut outside
the mesh. Before source serialization, facing boundary edges within one
walkable BASE part close gaps up to one 0.1 m cell. Only upward <=40 degree
source faces participate. Edges must face outwards, be nearly antiparallel,
overlap along their lengths and differ by at most 0.35 m at the seam.
Each half of the gap retains its own exact source plane; a step remains a
step. No convex hull, solid top, terrain, other object or steep riser becomes
a walkable floor. Seam triangles retain the owning part/object for removal.
The declared BASE may interpolate its walkable source plane over at most
0.25 cell (2.5 cm); its source catalogue includes that same margin. Terrain
retains its strict 1 mm footprint. Release support, delta-Z, terrain and
client checks keep their original thresholds and also cover the seam planes.
The local client resolves interpolated XY against the same server-owned mesh;
it never replaces the selected floor with the highest expanded bridge BV.
Contour error is selected from the compact region topology before contouring:
0.25 cell on tiles with holes (Euler characteristic), otherwise one cell.
A one-cell error was observed collapsing a hole and misclassifying its outline.
If this produces multiple outlines for a region, the tile selects monotone
partitioning followed by connected monotone row strips and exact contours
(zero simplification error). Bad/collapsed outlines select the same exact
construction. Surface border vertices are retained to preserve provenance. Recast merges interior regions even with zero
merge area; separate row runs cannot form holes and preserve walkable spans. MONOTONE_TILE lists each selected tile
and profile in the baker log. The same selection runs during worker rebuilds.
Any remaining multiple outline or other Recast error rejects the tile.
No library sources are modified.

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
Loaded positions use the same placement resolver: at most 1 m horizontally,
nearest saved world height, with surface identity used only to break floor ties.
Re-resolve polygon references and actual terrain/BASE height; do not compare
the exact game height with the approximate Detour height as a save-load gate.
Unavailable placement stops the derived move and is journaled, without rejecting
the saved world. The existing serialized address fields remain advisory.

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

- <mpr>--<mob>.<profile>.nav64: container 10, algorithm 26, Recast 0x010600,
  DT_POLYREF64, names/profile, ready shuffled/LZMS tiles and surface schema.
  In the expanded tile stream, each tile blob is followed by a u32 polygon
  support count (must equal polyCount). Each polygon has u32 source-count and
  that many u64 source IDs (low/high u32), primary first, connected neighbours
  afterwards. These records belong to that tile's polygon order and move with
  tile replacement. Algorithm 26 requires rebaking all six profiles, sources
  and body tables together; 24/25 data cannot be reused with this runtime.
  No layer section exists. Loader validates exact profile, bounds and ABI.
- <mpr>--<mob>.navworld: JSON version 4, baked static NIDs, ground dynamic boxes and flight_dynamic visual boxes.
- <mpr>--<mob>.navgeom: shared NSRC version 8 (four parallel LZMS blocks), inner section 7,
  original bounds and indexed common float32 XYZ vertices, losslessly stored;
  per-triangle indices, float32 water depths, byte material/class metadata,
  static object and surface IDs. Centimetre rounding is not used: it changes
  thin BASE planes and slope classification. Byte columns encode vertices
  and triangle records; canopy envelopes retain exact uint16 samples;
  door apertures and reserved off-mesh endpoint/surface/radius/flag records.
  Includes precomputed triangle/surface/object tile indices. One copy serves
  all profiles. No source import or spatial binning occurs during gameplay.
- Surface 1 is terrain. BASE IDs encode (unsigned NID << 32) | (part + 2).
  Placement/step/restore and the local client resolve original terrain/BASE
  triangles through polygon provenance. Gameplay acceptance remains separate.
  Off-mesh records remain reserved, not linked.
- bodies.navbody: BODY version 1, algorithm 26, resource-derived models and
  full morph grids, including non-un* models actually used by MOB creatures.
  Runtime never measures bodies. Save field sizes and movement speeds remain.

## Acceptance

HERO_START must advance by 0.02 m in shared Project/Find/Constrain code; the
baker fails if it does not. QUALITY preserves total gained_water_m2 and
separately reports gained_forbidden_water_m2 under the original depth rule.
CSV reports include losses, water reasons, BASE connections, placements and
private object/vegetation identity. Zone1 release validation walks complete
paths in 5 cm moves and 17 cm ticks across corners, using the runtime surface
traversal. The original 56 profile/map pairs plus journal-derived ruin-wall,
mountain and NPC-height routes check each accepted step/tick against the
climb/slope height budget, unrounded source support, terrain and client floor.
The six previously rejected support samples and both ramp seam samples are
also checked at their exact XY with the 0.35 m placement height budget.
HEIGHT_CONTINUITY reports counts/errors; a discontinuity prints coordinates
and the independently resolved client floor.
A failed step or discontinuity on a complete route rejects the map. Unreachable
goals remain explicitly reported as no_path. Shared loader time is measured
including reconciliation with the imported live objects, without a game tick.
Runtime performance, save/load, queues/doors, class-policy and random-goal
gameplay acceptance, and in-game client floor acceptance remain
separate acceptance items. See the private REPORT for measured release data.

## Shared-radius classes and AI goals (algorithm 25)

Each ground profile retains the union of terrain admitted by classes 1..7.
The profile's material class still selects its representative cost area;
body radii, pose clearances and movement speeds are unchanged. Bits 0..3
are nested pose permissions; bits 4..11 forbid the corresponding AI class.
Terrain exclusions are eroded at the full profile radius independently of
physical clearance. Contours split at class-mask boundaries without consuming
additional Recast area IDs or admitting the terrain policy onto a BASE above
it. Worker rebuilds use the same masks; doors retain them on reopen.
Contour construction temporarily colors adjacent regions so Recast treats
same-material class/floor boundaries as area borders; zero simplification
error alone does not protect those borders. Real materials are restored
before polygon serialization. Recast library sources remain unchanged.
Projection uses its requested horizontal distance as the complete admission
bound, without an extra millimetre. Strict support projection and movement
therefore share the same 1 mm numerical boundary limit.
Find, placement/restore, support, physical steps and random goals use the
unit's current AI-class exclusion, independently of its shared radius profile.
Generic queries without an explicit class use the profile's material class;
the baker's existing quality queries therefore enforce that class as well.

Guard/investigation goals use a circle; both fear/retreat branches use their
original square search region. Candidates come from the source's reachable
mesh region, use its live support address and require a complete exact route.
The requested region is seeded by a proven route from the unit, so a small
region far from its current position is not lost to source-circle sampling.
Reachable polygons are clipped to the requested square before area-weighted
sampling; circles additionally reject points outside their radius. Whole
Detour polygons are never sampled when they exceed the requested region.
Attempts remain bounded to eight; a zero-radius goal resolves only the given
point with the existing 1 mm numerical boundary tolerance. An unavailable region produces no move. Scripted patrol coordinates,
look directions and timing are unchanged.

The baker accepts --replay-input PRIVATE_JSON for journal-derived release inputs.
The same input accepts required `random_goal` records (radius, circle/square
shape) and `class_support` records (class, expected admission). They query the
ready mesh through the production goal/projection API, with no game tick.
Records are an array: scene [mpr,mob], kind (step_rejected/query_failed/walk),
unit, source/goal [x,y,z], optional profile/height/max_length. Required walk
records additionally set name, required=true and optionally neighbour=true.
The private report records Place/Find/Step outcomes and source-floor evidence.
Walk records run CAIMap::NewPath/CalcSpeeds/CreatePathSegment, CRouteWalker and
NavigationWorld::Move shared with L1ExecMove, then the client support resolver.
One iteration is one game tick. Neighbour release records exercise waiting and
bounded avoidance; a route that never encounters the neighbour is a failure.
No scripts, animations, renderer or game session are started.

All emitted polygons must be clockwise and convex in Detour XZ. A tile whose
simplified contour yields an inverted face is partitioned into exact compact
row runs before publication. Invalid output is rejected. Step traversal clips
the actual segment against linked convex polygons and exact cross-tile portal
edges; it retains the 1 mm numerical allowance and original height limits.
The server publishes frozen route clocks during queue waiting so the client
does not keep advancing the old route while the server stands still.

A stationary blocking unit triggers a bypass on the first blocked step.
Moving neighbours retain the bounded 27-tick queue. Circumscribed rings include
connected neighbouring footprints, including bodies outside the original route;
a bypass around one body must also be able to route around its whole group.
Each visibility edge is a normal Find corridor checked against every supplied
body. Material costs, max length, class/height filters and physical radii remain
unchanged. At most twelve rings (146 vertices) bound the secondary query. A*
uses an admissible distance/material-cost bound to avoid unnecessary corridors.
An unavailable/limited bypass fails the movement command and returns control
to its owner; queue_detour records the actual query status and endpoints.

A departing zone snapshot stores the camera view without its removed party
carrier or carrier-dependent FPS flag. Full-slot saves retain the strict map
reference. Both use the same nullable reference field and save schema v4.
Release walk records may set neighbour_distance, release_after_wait (0 keeps
the neighbour standing) and require_detour. Their results include detour_max_ms.


## Six profiles and automatic flight (algorithm 26)

Profiles are small, human, large, giant, levitate and flying. Tiny bodies use
small; levitating bodies share the remaining 0.5 m levitate profile. Profile
ordinals change, so nav64/navgeom/navbody must be released together. The wire
profile now stores Movement (ground=0, levitate=1, flying=2) after materialClass.
The body table uses ground indices 0..3 and levitate index 4.

Database locomotion 1 remains ground, 2 remains levitation, and 3 selects flying
with AI class 0. No species names, database assignments or altitude controls are
introduced. Existing database rows are unchanged. Bindings are an owner step.

Flight bakes a continuous upper envelope of terrain/water, every static visual
part (including crowns and ignored collision parts), doors and full dynamic-part bounds (including passable/ignored crowns).
It covers their footprints with the shared 1 m flight radius and a lattice
margin, then propagates height maxima to form approach/departure ramps. Source
slope, climb, material and walkable-roof flags cannot block this profile. A
0.5 m vertical margin accounts for voxel/detail quantization. This is automatic
flight above obstacles, not free movement in caves or manual altitude selection.
The flight mesh alone retains a sampled detail surface; its height supplies
query/step/render support. Ground and levitation keep original triangle support.
Flight routes sample the detail height every 0.25 m; serialized per-node speeds
account for 3D length while the existing XY walker and commands remain in use.
Crowd and melee admission use actual flight height. Ordinary units keep their
previous rules. World changes rebuild the flying mesh once on the existing
worker and publish it atomically; ground changes remain scoped to affected tiles.

NavBake validates the released flight detail against original terrain/water and
visual upper bounds, and traces flight over the real gate, forest and mountain.
The profile is deliberately conservative: it goes over a visual part's full XY
bounds, including empty crown gaps. Larger body-clearance policy and in-game
acceptance must be reviewed with the future database bindings.

Dynamic flight geometry retains all transformed part BVs, independently of
ground blocking flags, in navworld flight_dynamic. The live-object diff
compares those bounds too; removing, moving or turning a passable visual part
queues one worker sky rebuild. Static upper bounds remain measured from FIG.
Ground collision BVs and their tile rebuild policy are unchanged.
