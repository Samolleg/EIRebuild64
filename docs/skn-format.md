# SKN v1

Little endian; IEEE-754 binary32/binary16; fixed-width integers; valid UTF-8 NUL-terminated strings. Offsets and sizes are bytes. No pointer, address, STL layout or ABI is serialized. Coordinates are game metres, Z up, in the existing game model basis. Matrices are column-major and multiply column vectors, Gbind=Gparent×local TRS. Disk quaternions are x,y,z,w. ANM's w,x,y,z representation and legacy composition remain internal to the existing pose evaluator. World is applied once.

## Container and required sections

The 32-byte header is eight u32: magic `0x004e4b53` (`SKN\0`), version=1, exact file size, header size=32, section count, directory offset=32, reserved=0, reserved=0. A directory record is six u32: type, absolute offset, byte size, count, stride, section version=1. Directory records are 24 bytes. Sections start on 16-byte boundaries, do not overlap each other/header/directory, and fit the file. byte size=count×stride without overflow. Types must be unique; unknown types or versions fail. Alignment padding is not payload.

| Type | Payload | Stride |
|---|---|---:|
| 1 | String bytes | 1 |
| 2 | Joints | 112 |
| 3 | Vertices, all LODs | 48 |
| 4 | Global vertex indices, triangle list | 4 |
| 5 | Submeshes | 16 |
| 6 | LODs | 56 |
| 7 | Materials | 76 |
| 12 | Optional semantic joint per submesh | 4 |

Sections 1–7 are required. A string reference is u32 relative to section 1. Offset 0 conventionally contains the empty string. Names are unique stable LNK names. DDS IDs are relative, use `/`, contain no absolute prefix, drive, backslash or `..`, and end in `.dds`.

## Skeleton

Joint byte offsets: 0 name string reference; 4 i32 parent (-1 root, otherwise preceding joint); 8 f32 translation[3]; 20 f32 quaternion[4]; 36 f32 scale[3]; 48 f32 inverse global bind[16]. Scale is positive; quaternion norm squared must be within 1e-4 of 1. Gbind×inverse bind must equal identity within 1e-4 per element. There are 1–65535 joints. The loader validates all floats and all references, without repairing the asset.

Legacy bind comes from BON, selected with strength/grace/tall. Native ANM/ADB tracks bind to LNK names; preserve the existing clock, root translation, height scale, quaternion magnitude/composition and hierarchy. Pose matrices come from `prepare_native_poses`/`nativePoseCS`. Skin palette = Gpose×inverse(Gbind). Native pose matrices already include world; SKN must not apply it twice. Only immutable vertices/influences/bind data are uploaded; no CPU-deformed vertex stream is sent each frame.

## Mesh and LOD

Vertex offsets: 0 f32 position[3]; 12 SNORM16 normal[4]; 20 SNORM16 tangent[4], w=handedness; 28 f16 UV0[2]; 32 u16 joint[4]; 40 UNORM16 weight[4]. Weights sum exactly to 65535; all four joint indices must exist, including zero-weight lanes. Indices are u32 absolute vertex references, grouped in threes. Authoring uses MikkTSpace and splits UV/normal/region seams. The rigid reference tool has no normal map and writes a perpendicular reference tangent, not an authoring-quality MikkTSpace bake.

Submesh offsets: 0 u32 first index; 4 u32 index count; 8 u32 material; 12 u16 region; 14 u16 layer. Ranges contain whole triangles and valid material/vertex references.

LOD offsets: 0/4 u32 first vertex/count; 8/12 u32 first index/count; 16/20 u32 first submesh/count; 24 f32 screen error; 28 f32 hysteresis; 32 f32 min[3]; 44 f32 max[3]. There are 1–4 levels, with strictly increasing nonnegative screen error, hysteresis in [0,1]. Submeshes partition each LOD's index range without gaps/overlap. Indices address only that LOD's vertices. Bind bounds contain every vertex; authored render bounds must also cover deformation. All LODs share skeleton/material IDs; production art targets 3–4 levels, a rigid reference may use one.

Section 12 has one u32 semantic joint index per submesh. It maps visual sections to unchanged legacy part admission/visibility; it does not replace FIG/BV/navbody. The unit selector requires it. Vertices may still weight any four joints. Existing gameplay events, collision, attachment proxies and navigation continue to use the original resources.

## Materials and DDS

Material offsets: 0 f32 baseColor RGBA; 16 f32 emission RGB; 28 f32 roughness; 32 f32 metallic; 36 f32 AO; 40 f32 alpha cutoff; 44 u32 alpha mode (0 opaque, 1 mask, 2 blend); 48 u32 twoSided (0/1); 52 u32 DDS string references[4] in base/normal/ORM/emission order; 68/72 reserved u32=0. Base factors, roughness, metallic, AO and cutoff are [0,1]; emission is nonnegative. Empty IDs select material factors/default samples: white base and ORM, flat normal, black emission. Factors multiply map samples. ORM is R=AO/G=roughness/B=metallic.

DDS requires DX10, one 2D texture, no cube/volume/array, dimensions 1–16384, exact block payload and complete authored mip chain through 1×1. Base/emission use BC7 sRGB (DXGI 99); normal XY uses BC5 UNORM linear (83), positive Z reconstructed; ORM uses BC7 UNORM linear (98). A mip has ceil(width/4)×ceil(height/4)×16 bytes. Missing declared maps, wrong color space/format, missing mips or invalid payload fail. Cutout coverage, padding and vector normal filtering are offline responsibilities.

## Deferred feature sections

Types 8–11 are reserved in v1. Their nonempty payloads are explicitly rejected by this pilot, never silently treated as ordinary skin. Their detailed binary record schema is not yet frozen; they require a section version specification before authoring assets.

* 8 morph/drivers: named strength/grace/height/fullness and separate RPG strength/dexterity inputs; ranges/defaults/knots; bone controls; gameBind and root-relative retarget; legacy-driver/bind-node→palette map; twist/damped/limit correctors. FIG8 cross terms s*g/s*t/g*t/s*g*t and BON bind updates must be preserved. Morph is before skin.
* 9 surface bindings: source topology ID, three source indices/barycentric weights, tangent-frame offsets, normal offset, dependency stage and LOD correspondence. Dependencies are acyclic; order is source samples→morph/skin→post-skin bindings→garment support/folds→normals.
* 10 regions/wounds: versioned per-triangle body/underwear regions with L/R detail; wound masks/stages and eye regions. Keep gameplay proxy unchanged.
* 11 outfit: skeleton/bind/driver hashes; slots head/torso/legs/hands/feet/back/accessory; underwear→base→outer; hideBody/hideLowerLayers bitsets. Union only ready visible clothing; removing/failing an asset restores lower layers. Owner decision of 2026-11-10 overrides the proposal: fitted shirt/pants/underwear/wraps/cuffs/socks use surface binding; loose clothing, rigid armour and eyelids use ordinary bone weights.

bahonec remains read-only. No bahonec asset is included pending the owner's rights clarification; the rigid tool does not claim to reproduce its procedural rig, resolver, eyelid contact or surface-bound clothes.

## Reference tool

Build `tools/fig-to-skn.vcxproj`; run `fig-to-skn OUTPUT_DIR ARCHIVE.res [ARCHIVE.res ...]`, archives in first-wins priority. It converts unhufe at (0.2,0.3,0.85) and unmogo at (0.5,0.5,0.2), all available FIG submeshes (including alternative equipment), with weight 1.0 on their part's LNK node and bind from BON. Geometry is frozen at that shape; shape morph authoring is deferred. Its factor-only material does not pretend old MMP files are BC7/BC5 PBR maps.

The serialized SKN is decoded and compared against the renderer's actual CPU functions `sample_visual_skeleton` and `visual_node_world_matrix`. Idle and attack are sampled at integer/fractional phases with nonidentity root/facing, tolerance 1e-4 metres. ANM vertex morphs reject the skin-only reference. This comparison verifies transform math, not GPU execution or gameplay presentation.

## Current implementation limits (X64SKIN02 work in progress)

The runtime selector currently supports one LOD, base-color DDS and base/emission color factors. Full PBR factors are not integrated. It explicitly rejects multiple LODs and nonempty normal/ORM/emission maps: their complete GPU material/LOD integration is unfinished. The decoder itself validates the full required table layouts. Conservative bounds for arbitrary new silhouettes and the binary schemas of deferred sections also remain to be completed. This is not a finished implementation of BRIEF.
