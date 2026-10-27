# Nav64 offline data contract (X64NAV01 r11)

RecastNavigation v1.6.0 (zlib), unchanged sources; pin/archive digest:
third_party/recastnavigation.lock.json. Recast and Detour use DT_POLYREF64.
Algorithm 6, container 2, library 0x010600, SaveSchema 4.

The game ONLY reads Maps/Nav64 in the active mod. No navmesh generation on
zone entry, no Cache64/Nav reads/writes, no fallback to the legacy graph.
Missing, stale or incompatible data rejects the zone before publication and
names its MPR/MOB/profile with a request to re-bake in the mapper.
Old Cache64/Nav files are unused; this task does not remove installed files.

## Production baker / mapper interface

EIRebuild64NavBake.exe is a console target in EI_Rebuild.sln. Its build first
builds EI_Rebuild, then links the SAME engine objects with NavBakeMain.cpp.
No second MPR/MOB/BASE extractor, body measurement or profile implementation.
No renderer/audio/window/server startup. MOB scripts are neither compiled nor
executed. ResourceScope resolves the effective mod archives without entering a
mod simulation session; only package lookup tables are loaded.

Example (paths containing spaces require quotes):

    Engine64/EIRebuild64NavBake.exe --game-root "C:/GOG Games/PZ" --mod MODS/Dejavu --all --output-root "D:/nav-release/MODS/Dejavu"

One map:

    Engine64/EIRebuild64NavBake.exe --game-root "C:/GOG Games/PZ" --mod MODS/Dejavu --map zone1 --mob zone1 --output-root "D:/nav-release/MODS/Dejavu"

--game-root, --mod and --output-root are required. --mod may be relative to the
game root. Output goes to <output-root>/Maps/Nav64. --map and --mob may include
extensions; --mob defaults to the --map stem. Use either --all or --map.
--all reads the effective texts.res/map.txt using CGlobalMap::LoadZones, includes
the menu and both zone6/zone6_2 terrain states selected by WorldInterface,
deduplicates MPR/MOB pairs, and continues after a map error.

Exit codes: 0 = all requested outputs written; 2 = invalid arguments;
3 = package/database/registration initialization failure; 4 = one or more map
imports, builds or output writes failed. Errors identify the map and reason.
stdout is flushed after MAP_BEGIN, PROFILE, MAP_DONE and BAKE_DONE records;
stderr contains MAP_ERROR/BAKE_ERROR. PROFILE gives type, radius, tiles, wall
ms, raster/polygon CPU ms, bytes and fingerprint. MAP_DONE gives import and
total wall ms and the sum of eight profile file sizes. BAKE_DONE gives
counts/errors, total wall ms and total bytes. BOUNDS_NORMALIZED records
MPR/MOB/NID/model/part for every reversed finite solid BV, once per map part.
Fields are space-separated key=value; names are resource names without spaces,
reason is the remaining text after reason=. Times are integer milliseconds,
bytes are decimal byte counts. The mapper must check exit code and BAKE_DONE,
not infer completion from a PROFILE record alone.
A failed map is not reported as complete. Each nav64 file is written through
a flushed temporary file and MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH).

Filename: <mpr-stem>--<mob-stem>.<profile-name>.nav64.
Names are lowercase ASCII resource basenames, with .mpr/.mob extensions in
the header. Each file embeds BOTH source names and the profile ID; the mapper
matches that triple against its open MPR/MOB pair and requested profile,
then checks the fingerprint. Do not infer a map solely from a filename/hash.
Renaming a file cannot change its embedded identity. The game checks the same
triple before adding tiles. Container 1/algorithm 5 files must be re-baked.
The container embeds a SHA-256 of normalized geometry, effective MPR header/
sectors/material registry, raw primary MPR/MOB, effective object model/BON
payloads, authoring .navsrc bytes (explicitly empty when absent), profile,
coordinate conversion, algorithm/library/container/ABI settings. Script-only
secondary MOBs do not add geometry. A primary MOB edit requires re-baking.
No file dates, installation paths, transient IDs or EXE hashes enter the key.
.navsrc authoring semantics remain NAV03; this slice fingerprints the sidecar.
Geometry is assembled in the same stable object order in both executables.

## Fixed profiles and body/pose selection

| Type | Radius m | Clearance tiers m | Cell m |
|---|---:|---|---:|
| tiny | 0 | 0.2 / 0.4 / 1 / 1.6 | 0.1 |
| small | 0.1 | 0.2 / 0.4 / 1 / 1.6 | 0.1 |
| human | 0.2 | 0.4 / 1 / 1.6 / 2.2 | 0.1 |
| large | 0.5 | 0.4 / 1.6 / 2.2 / 3 | 0.1 |
| giant | 1 | 0.4 / 1.6 / 3 / 6 | 0.2 |
| levitate_tiny | 0 | 0.2 / 0.4 / 1 / 2 | 0.1 |
| levitate_small | 0.2 | 0.2 / 0.4 / 1 / 2 | 0.1 |
| levitate | 0.5 | 0.2 / 0.4 / 1 / 2 | 0.1 |

Radius uses half the FIG torso/shoulder width in the current constitution,
not GetCollisionDistance or database dimensions. Distal arms, weapons and
armor do not enlarge width. Models without standard torso names use remaining
body vertices. Missing idle/pose clips use that model's bind transforms.
Choose the largest fixed radius <= the body. A zero-radius minimum avoids
enlarging creatures narrower than 0.1 m. A body lower than the lowest clearance
receives that tier; no exception. Other tiers round down, capped by the old AI
class permission. Missing model/FIG/BON remains an explicit resource error;
corrupt/nonfinite resources are not treated as valid geometry.
Body measurement never modifies live animation, collision separation or attacks.

All eight profiles are baked, including future summons/poses. They build
concurrently. Raw rasterization is shared per (cell size, ground/levitation)
tile using the largest group border; water/slope policies require separate
ground and levitation rasters. A bounded 32-tile domain store limits memory.
Each profile copies raw spans, applies its original ledge/height filters and
builds its own compact field, radius erosion, clearance contours and detail.
All radii are exact cell multiples; erosion never rounds a radius above the
measured half-width. Contour simplification error is one cell instead of
zero; region adjacency and area/pose boundaries still constrain contours.
Detail height samples are RETAINED: Detour projection/step/restore use them,
including BASE over terrain. Removing them would flatten interior hills and
change floor selection. In inspected r10 tiles detail was only 13-16% of bytes;
polygon/link/BV/vertex counts dominated. The fix reduces voxel stairs and
compresses the section instead of discarding height information.
Detour tiles enter each mesh in fixed z/x order. A map exceeding 8 MiB across
profiles fails before output replacement; --all also enforces 400 MiB total.

Game (x,y,z) maps to Recast (x,z,-y). Units are metres. Ground is the actual
MPR triangles with sector offsets/water. BASE/IsBridge parts use effective FIG
triangles, BON/constitution, orientation and origin; CROWN/EMPTY keep their
semantics. Other solid BVs become prisms, never walkable tops. Climb is 0.35 m;
ground slope 40 degrees, supported levitation slope 60 degrees. Levitation is
not free 3D flight. Four clearance permissions share each profile mesh.
The decoder is the embedded owned renderer's production decoder. Finite BV
endpoints are normalized on each local axis before transforming all eight
corners. CFigure::RecalcBoundingBoxWithRotation (Figure.cpp) likewise computes
Min/Max over all eight endpoints; the old aiMap object path also enumerates
both endpoints rather than requiring their order. Negative constitution is
not a corrupt volume. NaN/infinite geometry still reports its source.

## Container 2

Little endian fields, no raw host pointers/structs except pinned Detour tile
payloads: NAV664 magic (u32 0x3656414e, 0x00003436), container=2, algorithm=6,
library=0x010600, endian=0x12345678, polyrefBytes=8; four length-prefixed UTF-8
strings: fingerprint, MPR basename, MOB basename, profile ID. String lengths
are u32 byte counts without NUL. Then six f32 profile fields (radius, height,
slope, climb, cellSize, cellHeight), u32 tileCells; three f32 origin values,
two f32 tile dimensions, u32 maxTiles/maxPolys; u32 sectionCount=3.
Each section is u32 type, u32 payloadBytes, payload:

- 1: u32 codec=5 (Windows Compression API LZMS, buffer mode, no RAW),
  u32 expandedBytes, u32 compressedBytes, compressed buffer. The expanded
  buffer is u32 tileCount followed by (u32 tileBytes, Detour tile bytes).
  Expansion is bounded to 512 MiB and must have exactly the declared length.
- 2: u32 domainCount=1, u32 domainId=1 (imported terrain/BASE).
- 3: u32 layerCount=0 (reserved until NAV05).

Compression is lossless and uses the OS cabinet.dll, linked via cabinet.lib;
no dependency DLL is added to the package. API contract:
[Compress](https://learn.microsoft.com/en-us/windows/win32/api/compressapi/nf-compressapi-compress),
[Decompress](https://learn.microsoft.com/en-us/windows/win32/api/compressapi/nf-compressapi-decompress).
The production reader validates
versions, SHA, profile, exact lengths, counts, indices and tile ABI before
addTile. Limits: 4096 tiles, 32768 polygons/tile, 16 MiB/tile, 512 MiB/file.
Zero walkable tiles is valid. Stale data is rejected, never regenerated.

## Runtime / remaining packages

Prepare reads every fixed profile before publishing NewZone/staged load and
resolves restored unit addresses. Query entry points still route exclusively
through NavigationWorld. Preview, exact linear corridor, constrained Detour
step, bounded source ingress, pose masks and SaveSchema 4 remain as in r8.
Old x64 saves are rejected without migration; transient polygon refs are not
serialized. Partial paths and bounded-search exhaustion are failures.
Moving units wait rather than push. Full crowd/queue/terrain costs and robust
independent floors remain later NAV packages.

Real runtime geometry changes invalidate the snapshot. No runtime builder or
old graph is used; until NAV05 dynamic tiles are implemented, an unpublished
changed snapshot is unavailable. Exact campaign gameplay and the zone1
passages (1.3 m opening/ramp, 1.5 m courtyards, 0.8 m door, 6 m giant gates)
require owner acceptance. samvlrmp01 measured width remains 1.30 m.
