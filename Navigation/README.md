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
head clearance. Their tops never become walkable surfaces. BASE, CROWN and
EMPTY do not acquire new walkable geometry in this slice.

The initial policy is standing-human ground, 0.1 m voxels, 0.05 m height,
256-voxel tiles, 40 degree slope and 0.35 m climb. Individual collision radius
and current class height get a conservatively quantized mesh on demand;
this is not acceptance of the full eight-class/water/flight policy (NAV04).
Material areas 1..15 use existing costs, while movement uses existing speed
tables. Directional search costs are deferred to NAV04. No crowd controller,
door topology, bridge decks or old search fallback is present.

## Container v1 (little endian)

No raw host structs/pointers are written. The only native layout is the tile
payload returned by the pinned library's `dtCreateNavMeshData`.

* u32 magic `0x3656414e`, `0x00003436` (NAV664), container=1, algorithm=1,
  library=`0x010600`, endian=`0x12345678`, polyrefBytes=8.
* u32 text length + UTF-8/ASCII SHA-256 input fingerprint, maximum 4096 bytes.
* Profile: six f32 (radius,height,slope,climb,cellSize,cellHeight), u32 tileCells.
* Detour parameters: origin[3], tileWidth, tileHeight (f32), maxTiles, maxPolys
  (u32). Limits: 4096 tiles, 32768 polygons/tile, 16 MiB/tile, 512 MiB/file.
* u32 sectionCount=3; each section has u32 type and byteLength.
  Type 1: u32 tileCount followed by (u32 size, Detour tile bytes), no stored
  refs. Type 2: u32 surfaceCount=1, u32 surfaceId=1 (MPR ground).
  Type 3: u32 buildLayerCount=0, reserved for NAV05's TileCache data.
  Unknown versions/sections, stale fingerprints and ABI mismatches regenerate
  through the same builder. Exact lengths/counts are validated before addTile.

`Maps/Nav64/<fingerprint>.nav64` and `Cache64/Nav/<fingerprint>.nav64` use
identical bytes. Mapper NAV03 must use this API/profile/input normalization;
new metadata/layers require an explicit container/algorithm revision.
Fingerprint inputs are scene MPR + primary MOB + authoring variant, effective
MPR header/sectors, material registry, primary/secondary MOB payloads, selected
transformed BV geometry, all imported ground triangles, profile and ABI/build
settings. No file dates or EXE hashes participate. Runtime object deltas change
the fingerprint and do not overwrite authored files. Cache writes flush a
temporary file and atomically replace the target.

Queries reject partial paths and exhausted buffers/nodes. Sliced Detour work
has a 65536-iteration bound, an 8192-polygon corridor and 8192 straight points.
The segment executor carries exact points, uses piecewise linear motion and
limits the complete proposed step by a Detour raycast; it never splines or
rounds through a wall. Physical neighbours cause waiting, never pushing.
The complete crowd/queue policy and dynamic tile budgets belong to NAV05/06.

SaveSchema 4 replaces version 3 without migration. Unit address is stable
surface + world XYZ + profile radius/height. Polygon refs/generation and query
state are transient. Staged load builds the mesh from restored objects,
resolves addresses and rebuilds moving routes before publishing the world.
Legacy embedded MOB graph/GRP data is neither read nor modified.
