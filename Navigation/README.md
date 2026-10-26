# Nav64, first ground slice (X64NAV01)

RecastNavigation v1.6.0, unmodified Recast and Detour, zlib. Pin and archive
digest: `third_party/recastnavigation.lock.json`. Both modules and the host use
`DT_POLYREF64`, x64, `/MD`. The simulation thread owns each Mesh and Query.
`Geometry`, `Build`, `Query` and the coordinate conversion have no game/Win32
dependency and are the shared entry point for the future mapper/baker.

Game `(x,y,z)` maps to Recast `(x,z,-y)`. Input lengths are metres. Ground is
the original MPR triangle mesh with sector vertex offsets; solid geometry is
the selected, BON/constitution-adjusted, rotated part BV, not object AABBs.
Solid volumes are cut from the compact field before radius erosion, including
head clearance. Their tops never become walkable surfaces. In r6, selected
BASE/IsBridge parts contribute their actual FIG triangles, decoded by the
embedded renderer, interpolated by constitution and transformed with BON and
the object rotation/origin. CROWN and EMPTY retain their pass/ignore semantics.
Surface area is the existing bridge-road material. A floor touching the top
of a solid is supported; a body overlapping that solid is blocked.

There are eight permanent agent profiles (X64NAV01 r8):

| Type | Radius (m) | Clearance tiers (m) | Cell size (m) |
|---|---:|---|---:|
| tiny | 0.05 | 0.2 / 0.4 / 1 / 1.6 | 0.05 |
| small | 0.15 | 0.2 / 0.4 / 1 / 1.6 | 0.05 |
| human | 0.25 | 0.4 / 1 / 1.6 / 2.2 | 0.05 |
| large | 0.5 | 0.4 / 1.6 / 2.2 / 3 | 0.1 |
| giant | 1 | 0.4 / 1.6 / 3 / 6 | 0.2 |
| levitate_tiny | 0.05 | 0.2 / 0.4 / 1 / 2 | 0.05 |
| levitate_small | 0.25 | 0.2 / 0.4 / 1 / 2 | 0.05 |
| levitate | 0.5 | 0.2 / 0.4 / 1 / 2 | 0.1 |

NavigationWorld::Body reads the effective FIG/BON/LNK and first ordinary idle
frame from the existing managers. The model's X axis is left/right. Torso and
hips plus the proximal quarter-length of upper arms define shoulders; distal
arms, weapons, armor alternatives and their descendants do not inflate width.
Radius is half that measured width at the unit's current strength/grace/tall.
Geometry is interpolated without clamping constitution or forcing the human
BV to one metre. Pose evaluation uses the game's left quaternion multiplication
(local * parent), with BON offsets rotated by the parent. Parsed figures,
skeletons, pose tracks and measured constitutions are cached within the scene.
No unit animation, random state, collision distance or attack distance changes.

Choose the largest fixed radius <= the measured radius, never round upwards.
This deliberately follows the owner's passage-preservation rule. Grouped
radii can be smaller than the exact body; exact swept shape/crowd behaviour
remains later work. Missing/unsupported body geometry is an explicit error.
Class-only calls without an individual model retain fixed class defaults.
Fine profiles use 0.05 m cells, so erosion does not round their radius upwards
to a 0.1 m multiple; larger profile radii are exact multiples of their cells.

All profiles share rasterization between four clearance tiers, including
levitation. Solid prisms retain actual ceilings and never become walkable tops.
Measure standing/crawl/lie height from the corresponding model idle poses;
monsters without a pose use their ordinary standing animation. The query picks
the greatest tier <= that height AND <= the original AI class height. The
latter preserves old clearance permissions rather than enlarging the unit
because of a visual envelope. Find, placement, ingress, step and save restoration
use this same bit. Up to 60 temporary Recast areas encode material/clearance;
Detour restores material IDs 1..15 and nested flags 1/3/7/15. Clearance-boundary
contours retain zero simplification error. Profile selection never uses
GetCollisionDistance, body BV correction, ownership or attack distances.

Class 0 retains supported levitation, not free 3D flight: the actual MPR water
layer raises its floor, class-0 material policy admits water, slope is limited
to the existing 60 degrees, body clearance stays 2 m and solids still block.
The game's altitude and high-altitude radius reduction are unchanged. Its
terrain cost/speed is flat (1024), without the ground uphill speed penalty.
Ground types keep the 40 degree slope; all types retain 0.35 m climb and 0.05 m
vertical cells. Full ground water/terrain cost policy still belongs to NAV04.

Prepare warms all eight types during NewZone and staged save loading, before
publishing the world; party warm-up reuses them. A new pose or ordinary summon
cannot introduce a new type/mesh. Parallel tile workers retain their own Recast
contexts; only the simulation thread inserts Detour blobs in fixed z/x order.
NAV ready records type, radius, height, cell size, built/cache, wall ms, phase,
fingerprint, generation, workers and raster/polygon CPU ms. NAV prepared gives
total time/profile count. Timing on zone1 still requires the owner's game log.
Material areas 1..15 use existing costs, while movement uses existing speed
tables. Directional search costs are deferred to NAV04. No crowd controller,
door topology or old search fallback is present. Independent multi-floor
surface identity and complete bridge semantics remain later packages.

Tiles are binned spatially before building. Triangles are converted/classified
once per profile, solid hulls are computed once, and each tile visits only its
overlapping inputs (including the full erosion border). Up to eight workers,
one per two hardware threads, each own their Recast context and temporaries.
Completed blobs enter Detour on the simulation thread in fixed z/x order.
Cells depend on the permanent type; radius and clearance tiers round down to the body.

## Container v1 (little endian)

No raw host structs/pointers are written. The only native layout is the tile
payload returned by the pinned library's `dtCreateNavMeshData`.

* u32 magic `0x3656414e`, `0x00003436` (NAV664), container=1, algorithm=4,
  library=`0x010600`, endian=`0x12345678`, polyrefBytes=8.
* u32 text length + UTF-8/ASCII SHA-256 input fingerprint, maximum 4096 bytes.
* Profile: six f32 (radius,height,slope,climb,cellSize,cellHeight), u32 tileCells.
* Detour parameters: origin[3], tileWidth, tileHeight (f32), maxTiles, maxPolys
  (u32). Limits: 4096 tiles, 32768 polygons/tile, 16 MiB/tile, 512 MiB/file.
* u32 sectionCount=3; each section has u32 type and byteLength.
  Type 1: u32 tileCount followed by (u32 size, Detour tile bytes), no stored
  refs. Type 2: u32 surfaceCount=1, u32 surfaceId=1 (imported triangle surfaces).
  Type 3: u32 buildLayerCount=0, reserved for NAV05's TileCache data.
  Unknown versions/sections, stale fingerprints and ABI mismatches regenerate
  through the same builder. Exact lengths/counts are validated before addTile.

`Maps/Nav64/<fingerprint>.nav64` and `Cache64/Nav/<fingerprint>.nav64` use
identical bytes. Mapper NAV03 must use this API/profile/input normalization;
new metadata/layers require an explicit container/algorithm revision.
Fingerprint inputs are scene MPR + primary MOB + authoring variant, effective
MPR header/sectors, material registry, selected transformed BV geometry, actual
object/ground triangles, profile and ABI/build settings. Raw MOB script/unit
payloads, party/equipment, transient IDs and generation counters do not enter
the key. A script-only AddMob (including AutoRunSpellAddon123) does not invalidate
geometry. Initial static objects enter the snapshot before Prepare. Remove/add
pairs are compared by their final geometric contribution at the next query;
identical updates and objects without a contribution do not advance generation.
Real changes log NAV geometry_changed and still rebuild synchronously until NAV05.
No file dates or EXE hashes participate. Runtime object deltas change
the fingerprint and do not overwrite authored files. Cache writes flush a
temporary file and atomically replace the target.

Queries reject partial paths and exhausted buffers/nodes. Sliced Detour work
has a 65536-iteration bound, an 8192-polygon corridor and 8192 straight points.
The segment executor carries exact points, uses piecewise linear motion and
limits the complete proposed step by a Detour raycast; it never splines or
rounds through a wall. Physical neighbours cause waiting, never pushing.
Point queries use the current bridge/deck height as their initial hint instead
of discarding it for original MPR height. Route supportZ already present in the
packet drives both client walking/turning and path-preview particles; visual
support is retained at a stopped endpoint and cleared on authoritative placement.
The complete crowd/queue policy and dynamic tile budgets belong to NAV05/06.

SaveSchema 4 replaces version 3 without migration. Unit address is stable
surface + world XYZ + profile radius/height. Polygon refs/generation and query
state are transient. Staged load builds the mesh from restored objects,
resolves addresses and rebuilds moving routes before publishing the world.
Legacy embedded MOB graph/GRP data is neither read nor modified.

Source recovery (X64NAV01 r5): existing authored positions may be just outside
an eroded polygon. Find, step and save restoration share a bounded ingress
(radius + two cells) over continuous non-null imported triangles. The actual position
and ingress are part of the route/preview; movement must reduce the distance
to the mesh, preserve body clearance and cross no raw solid or ground gap.
This does not change mesh radius/slope or relax placement/target projection.
Fully empty FIG nodes are ignored explicitly; invalid nonempty BV data still
rejects the zone with NID/model/part and input values, or an MPR sector name.
