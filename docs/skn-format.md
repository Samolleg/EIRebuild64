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

Vertex offsets: 0 f32 position[3]; 12 SNORM16 normal[4]; 20 SNORM16 tangent[4], w=handedness; 28 f16 UV0[2]; 32 u16 joint[4]; 40 UNORM16 weight[4]. Weights sum exactly to 65535; all four joint indices must exist, including zero-weight lanes. Indices are u32 absolute vertex references, grouped in threes. Authoring uses MikkTSpace and splits UV/normal/region seams. The rigid reference tool writes a perpendicular reference tangent and may attach a flat BC5 normal; it is not an authoring-quality MikkTSpace bake.

Submesh offsets: 0 u32 first index; 4 u32 index count; 8 u32 material; 12 u16 region; 14 u16 layer. Ranges contain whole triangles and valid material/vertex references.

LOD offsets: 0/4 u32 first vertex/count; 8/12 u32 first index/count; 16/20 u32 first submesh/count; 24 f32 screen error; 28 f32 hysteresis; 32 f32 min[3]; 44 f32 max[3]. There are 1–4 levels, with strictly increasing nonnegative screen error, hysteresis in [0,1]. Submeshes partition each LOD's index range without gaps/overlap. Indices address only that LOD's vertices. Bind bounds contain every vertex; authored render bounds must also cover deformation. All LODs share skeleton/material IDs; production art targets 3–4 levels, a rigid reference may use one.

Section 12 has one u32 semantic joint index per submesh. It maps visual sections to unchanged legacy part admission/visibility; it does not replace FIG/BV/navbody. The unit selector requires it. Vertices may still weight any four joints. Existing gameplay events, collision, attachment proxies and navigation continue to use the original resources.

For native human units, `BuildOwnedAssembly` supplies the current Wear/Remove and hair selection. Match section 12 by the complete LNK node name, including `hr.NN` and `.armorNN`; absent and hidden parts draw nothing. The reference converter retains all nonempty variants from the selected MOD, rather than baking a prototype's outfit. `CGeometryManager::ReadFigure` selects the MOD entry when a model container exists; otherwise it selects `<mask><part>.fig`. Portrait `infa*face.fig` resources are separate UI assets, not replacement body meshes.

Human SKN groups use their selected assembly part's completed surface (primary or secondary `AssembleTexture` atlas), including its generation and FIG alpha/culling policy. Primary assembly layers skin, shirt through gloves, then plate/leggings in wear order; secondary assembly layers helm and current weapons, using the existing HD quadrant layout when enabled. Appearance invalidation rebuilds SKN groups from the new assembly after equipment or hair changes; atlas updates retain the host surface contract. Models without character assembly keep SKN DDS materials. The reference converter writes empty normal/ORM/emission IDs; no flat placeholder BC textures are needed. This changes no SKN v1 section layout.

## Materials and DDS

Material offsets: 0 f32 baseColor RGBA; 16 f32 emission RGB; 28 f32 roughness; 32 f32 metallic; 36 f32 AO; 40 f32 alpha cutoff; 44 u32 alpha mode (0 opaque, 1 mask, 2 blend); 48 u32 twoSided (0/1); 52 u32 DDS string references[4] in base/normal/ORM/emission order; 68/72 reserved u32=0. Base factors, roughness, metallic, AO and cutoff are [0,1]; emission is nonnegative. Empty IDs select material factors/default samples: white base and ORM, flat normal, black emission. Factors multiply map samples. ORM is R=AO/G=roughness/B=metallic.

DDS requires DX10, one 2D texture, no cube/volume/array, top-level width and height each a multiple of four in 4–16384 (1x1 and 2x2 top levels are invalid), exact block payload and complete authored mip chain through 1×1. Base/emission use BC7 sRGB (DXGI 99); normal XY uses BC5 UNORM linear (83), positive Z reconstructed; ORM uses BC7 UNORM linear (98). Lower mips may have dimensions below four and still occupy complete BC blocks. A mip has ceil(width/4)×ceil(height/4)×16 bytes. Missing declared maps, wrong color space/format, missing mips or invalid payload fail. Cutout coverage, padding and vector normal filtering are offline responsibilities.

## Nala feature sections (design; partial bind decoder)

The following is the X64NALASKN01 section design. The decoder supports the
section-9 bind-contact subset described at the end, and rejects nonempty
sections 8/10/11. Runtime submission and the CPU Nala replacement remain pending.

### Common envelope for sections 8, 9 and 11

Directory section version is 1, stride is 1, count is payload byte size.
The payload starts with four u32: headerBytes=16, tableCount,
tableDirectoryOffset=16, reserved=0. Each table directory entry is four u32:
kind, section-relative byte offset, count, stride. Table payloads start on
16-byte boundaries. Reject overflow, out-of-bounds ranges, overlap with another
table/header/directory, duplicate or unknown kinds, unsupported versions and
nonzero reserved fields. Strings reference section 1. All numbers are finite.
Indices are zero based; an absent optional reference is 0xffffffff.

### Section 8: five inputs, controls, vertices and bones

The five inputs are database build/fullness/height and RPG strength/dexterity,
in that order. They are not the 45 resolved editor controls. Match
`Game/BodyShapeResolver.h::NalaMorphs::Resolve`: clamp RPG inputs to [15,35]
and database inputs to authored knots; bilinearly interpolate the 3x3 RPG
samples; add database deviations for ordinary controls and multiply them for
height/stature; preserve the fixed 1.05 head size. Reject nonfinite input before
clamping. Independent linear deltas for five axes do not describe this resolver.

| Kind | Ordered fields | Stride |
|---|---|---:|
| 1 inputs, exactly five | u32 name, semantic; f32 min, default, max; u32 reserved | 24 |
| 2 scalar instructions | u32 opcode, a, b, c; f32 immediate; u32 reserved | 24 |
| 3 named controls | u32 name, scalar; f32 min, max | 16 |
| 4 vertex positions | u32 vertex, xScalar, yScalar, zScalar | 16 |
| 5 bone components | u32 joint, component, scalar, reserved | 16 |

Input semantics are 0..4, unique and complete, with min<=default<=max.
Instruction index is its result register. Opcodes: 0 constant(immediate),
1 input(a), 2 add(a,b), 3 subtract(a,b), 4 multiply(a,b), 5 divide(a,b),
6 min(a,b), 7 max(a,b), 8 negate(a), 9 sqrt(a), 10 sin(a), 11 cos(a),
12 atan2(a,b), 13 acos(a), 14 select(a,b,c): b when a<0, otherwise c.
Except input indices, operands reference preceding instructions only. Unused
fields are zero. No loops, executable shader text or arbitrary memory access.
Operations use binary32 without implicit saturation or fused operations.
Invalid domains, zero divisors and nonfinite results reject evaluation.
Required clamps are explicit instructions. Named controls check authored limits.

Vertex records replace absolute pre-skin positions in section 3, one output
per vertex. Unlisted vertices retain their bind positions. Bone components
0..2 are local translation, 3..6 quaternion x/y/z/w, 7..9 positive scale;
unlisted components retain section-2 values. Duplicate output destinations
are invalid. Rebuild global bind/inverse bind in parent order after shape
changes. Preserve FIG8 cross terms and shape-dependent bind changes.

Evaluate the shape program on GPU at load and only when the effective five
inputs change. Cache by asset identity/generation and effective tuple; separate
shape buffers for different unit shapes. Pose, outfit and wounds have separate
invalidation. Recompute appropriate normals/tangents from deformed geometry.
No CPU vertex stream or GPU readback is part of evaluation.

Pose serialization remains a prerequisite beyond these shape tables: bahonec
uses `Data::bindNode`, gameBind, driver retargeting, dual/damped/semi controllers
and joint limits. Its palette cannot be reduced to the 15 LNK drivers without
changing deformation. The file path must preserve those operations and mappings;
a file needing unimplemented controllers must fail. Hand, hip, sole placement,
attachment and locomotion corrections belong to pose evaluation. They must not
call the CPU body/garment vertex generators.

### Section 9: surface bindings and ordered dependencies

| Kind | Ordered fields | Stride |
|---|---|---:|
| 1 surfaces | u32 name, firstVertex, vertexCount, firstIndex, indexCount, lod, flags, reserved | 32 |
| 2 stages | u32 operation, sourceSurface, targetSurface, firstRecord, recordCount, reserved[3] | 32 |
| 3 anchors | u32 target, source[3]; f32 barycentric[3], offset[3]; u32 frame, reserved | 48 |
| 4 triangles | u32 a,b,c | 12 |
| 5 weld groups | u32 vertex, group | 8 |
| 6 copies | u32 target, source; f32 normalOffset; u32 reserved | 16 |

Surfaces own disjoint section-3 vertex ranges and section-4 index ranges.
Their indices address only their own vertices; lod references section 6.
flags=1 marks sampling-only geometry with no draw submesh; flags=0 is visible
geometry. No other flags are defined. Sampling geometry still deforms when
hidden. Bindings never infer correspondence from proximity or reuse indices
across LODs: every LOD has an explicit source surface and bindings.

Stage operations: 0 anchor replacement, 1 anchor offset addition,
2 area-normal recalculation, 3 position weld, 4 normal weld,
5 copy with normal offset. firstRecord/count addresses anchors (0/1),
triangles (2), welds (3/4) or copies (5). For anchors/copies, source indices
are local to sourceSurface and target indices local to targetSurface.
Triangles/welds are local to targetSurface; group<vertexCount. Each anchor,
copy or weld stage contains a target at most once. Operations 2..4 require
sourceSurface==targetSurface.

Skin ordinary and sampling geometry first. Execute stages in stored order,
with a GPU barrier between each producer and consumer. Cross-surface
sources must be completed by earlier stages; cyclic dependencies and reads
of unfinished targets fail. Anchor/copy sources and targets are distinct
surfaces. In-place offset addition reads the previous target state; weld and
normal stages read a completed snapshot and write their completed result.

For source p0,p1,p2, each weight is in [0,1] and sum(w)=1 within 1e-5.
c=sum(wi*pi). Frame 0: n=normalize(cross(p1-p0,p2-p0)), t=normalize(p1-p0).
Frame 1: n=normalize(sum(wi*ni)),
t=normalize((p1-p0)-n*dot(p1-p0,n)). Both use b=cross(n,t) and
q=c+t*offset.x+b*offset.y+n*offset.z. Replacement writes q; offset addition
adds q-c to the previous target position. Frame 0 matches underwear; frame 1
matches shirt, pants, wraps, cuffs and socks. Degenerate frames fail evaluation.

Area normals sum unnormalized triangle crosses before normalization.
Position weld averages positions per group; normal weld sums normals per group
before normalization. Copy writes source+normalOffset*sourceNormal, with the
normal reversed for negative offsets. Inner faces/rims and their UV/index
splits are explicit mesh data. Final normals/tangents follow final positions;
intermediate normals required by later binding stages must also be computed.

Preserve multi-stage source behavior: shirt support normals precede folds;
pants support normals/weld precede refinement, position weld and normal weld;
wraps select bare/over-shirt anchors. The waist shirt correction adds only its
anchor offset, not the interpolated body position. Inner offsets follow outer
normals. A single body-to-cloth anchor pass cannot replace this pipeline.

Keep native sampling topology separate from render topology: bahonec has
20,579 native body vertices, while NalaLidContact produces 21,003 render
vertices. Cook its accepted proxies/edits, UVs and indices into the ordinary
visible mesh. Clothing source indices remain native. Render vertices with
weighted source edits must retain their actual deformation; cooking must
report any edit not expressible by four influences instead of silently
truncating weights. No runtime lid topology generation is allowed.

### Section 13: internal Nala pose rig, version 1

The codec is `Source/nala_pose_asset.h`; the offline extraction command is
in `tools/nala-to-skn/README.md`. The container decoder retains the rig and
validates the contact subset described below. Retained GPU submission is not
implemented: runtime selection rejects this bind snapshot explicitly. A
standalone `.section13` is also an intermediate, not an installable model.

Use the common 16-byte table envelope above with exactly nine tables. The
outer section uses version=1, stride=1, count=payload byte size. All scalar
parameters below use little-endian binary64, preserving the accepted kernel's
parameters. No C++ structure layout or source pointers are written. Matrices
are row-major affine 3x4 (three rows of four binary64 values, translation in
column 3); quaternions here follow the kernel's w,x,y,z order. These deliberately
differ from section 2's binary32 column-major TRS. Nonfinite/singular matrices,
invalid references, cyclic hierarchy and invalid controller ranges reject.

| Kind | Ordered fields | Stride |
|---|---|---:|
| 1 nodes | i32 parent, driver; f64 rest[12], local[12], stretch[12], restQ[4] | 328 |
| 2 skin slots | u32 bindNode; f64 bindInverse[12] | 100 |
| 3 game bind, exactly 15 | f64 gameBind[12] | 96 |
| 4 hierarchy order | u32 node | 4 |
| 5 dual controllers | u32 side, a, b, i1, i2 | 20 |
| 6 damped controllers | u32 side, own, target | 12 |
| 7 semi controllers | u32 side, own, target; f64 weight | 20 |
| 8 limits | u32 own, parent; f64 frame[12], low, high, y, z | 136 |
| 9 forward, exactly one | f64 x, y, z | 24 |

Node IDs preserve the original rig indices. Parents need not have smaller
indices; table 4 is a complete permutation with parents preceding children.
Reconstruct children from parent IDs. Parent/driver -1 means absent. Each of
the 15 drivers occurs exactly once. Driver indices are, in order,
`hp,bd,hd,lh1,rh1,lh2,rh2,lh3,rh3,ll1,rl1,ll2,rl2,ll3,rl3`.
Vertex influences address table-2 slots, **not** table-1 nodes. Multiple slots
can point to one node while retaining different inverse matrices; merging
these slots changes the eyes/teeth/tongue bind frames. Skin-slot count is
independent of node count. No reduction to 15 LNK weights is performed.

Execute `Retarget`, then `LimitRetarget`, then `Control`. Limits retain table
order. Control runs side 0 then side 1, each with dual, damped, semi in that
order, preserving each table's per-side order. Dual nlerp factor remains the
accepted `0.3330000042915344`. `Replace` propagates the delta through descendants.
Skin palette slot i is `world[bindNode[i]] * bindInverse[i]`, in rig-local
coordinates; root/world placement remains a separate caller responsibility.
Shape-dependent rest/local/stretch/restQ/inverse-bind/limit-frame changes must
be applied before this bone-only evaluator. It neither evaluates the shape
program nor applies appearance scale, hands, hips, sole placement or attachment
corrections. Those are not implicitly replaced by this section.

### Section 11: outfit, material variants, layers and hiding

| Kind | Ordered fields | Stride |
|---|---|---:|
| 1 items | u32 name, nativeSlot, subtypeMask, subtypeValue, layer, firstSubmesh, submeshCount, reserved | 32 |
| 2 hide rules | u32 item, targetSubmesh, requiredItem, reserved | 16 |
| 3 geometry variants | u32 item, requiredItem, firstStage, stageCount | 16 |
| 4 identity, exactly one | u8 skeletonSHA256[32], bindSHA256[32], driversSHA256[32] | 96 |

Skeleton hash covers ordered UTF-8 joint names including terminating NUL,
followed by parent i32 LE for each joint. Bind hash covers section-2 payload;
driver hash covers section-8 payload. Hashes describe cooked compatibility,
not implementation support.

Layers: 0 underwear, 1 base, 2 outer. Slots use actual CUnit Wear IDs:
3 shirt, 4 pants, 6 cuffs, 5 socks in the current source. An always-present
body/underwear item has nativeSlot=0xffffffff and mask=value=0. Other items
match (wornSubtype & subtypeMask)==subtypeValue, with value & ~mask=0.
Overlapping selectors in the same slot/layer reject the file. Unsupported
subtypes select no cooked garment. Material variants retain their accepted
cut and texture mapping; geometry is not inferred from resource ID alone.

An item owns a nonempty contiguous submesh range, with unique ownership and
matching submesh layer. Split body/underwear zones into ordinary submeshes.
Hide rules apply only when item is selected and ready and requiredItem, if
not absent, is also selected and ready. A rule may hide a lower layer or an
explicit body zone, never itself. Union applicable hide sets after readiness
resolution; removing/failing a garment restores lower layers immediately.
Shirt hides bandeau and pants hide panties only after readiness. No CPU
vertex/index regeneration is needed to select visible parts.

Geometry variants select section-9 stage ranges. requiredItem absent means
unconditional; a matching conditional variant takes priority. Multiple
applicable conditional variants fail. This carries shirt-under-wrap and
bare/over-shirt wrap choices independently of fabric. Active variants must
retain acyclic dependencies. Hidden sampling sources still execute when
needed by visible clothing. Draw once per visible part/material.

Cook every current material variant from natural-nails-runtime.json, including
underwear, shirts, pants, wraps, finishes, cuffs and socks. Fitted garments
use surface bindings; loose garments, rigid armour and lids use bone weights,
per the owner decision. Missing support must not change appearance silently.

### Wounds and failure behavior

Section 10 remains reserved and rejected when nonempty. Wounds are an open
part of the file implementation: preserve resource 202's 19 decoded masks
and six independent wound-stage selections with the accepted composite.
A baked current wound state in body albedo is not full wound support.

Reject a failed required file/section with one diagnostic line identifying
asset, section/stage and cause. Deduplicate by failed asset identity and
resource generation across passes/frames. A new generation may retry.
Never select the legacy body or call a CPU vertex generator after failure.
Keep gameplay FIG/BV proxies, event/navigation behavior and attachment
admission separate from the visual file.

bahonec remains read-only. Rights and provenance are recorded below; this task cooks its clothing textures outside Git/kit. The rigid tool does not claim to reproduce its procedural rig, resolver, eyelid contact or surface-bound clothes.

## Reference tool

Build `tools/fig-to-skn.vcxproj`; run `fig-to-skn OUTPUT_DIR ARCHIVE.res [ARCHIVE.res ...]`, archives in first-wins priority. It converts unhufe at (0.2,0.3,0.85) and unmogo at (0.5,0.5,0.2), all available FIG submeshes (including alternative equipment), with weight 1.0 on their part's LNK node and bind from BON. Geometry is frozen at that shape; shape morph authoring is deferred. The optional `--dds-directory DIRECTORY` argument between OUTPUT_DIR and the archives attaches `dds/<model>_color.dds`, `_normal.dds` and `_orm.dds`. All three files must exist. Runtime validation still enforces their BC formats, color spaces and complete mip chains. The geometry and CPU reference comparison are unchanged.

`fig-to-skn --export-mmp TEXTURE.mmp OUTPUT.rgb ARCHIVE.res [...]` exports mip zero through the existing MMP RGB decoder and prints dimensions. Feed those bytes to `skn_texpack --role color --space srgb --input OUTPUT.rgb --width W --height H --output <model>_color.dds`. Cook a constant positive-Z normal and ORM (AO=1, roughness=1, metallic=0) with skn_texpack as well. This reference is opaque and exports RGB, not translucent MMP alpha.

The serialized SKN is decoded and compared against the renderer's actual CPU functions `sample_visual_skeleton` and `visual_node_world_matrix`. Idle and attack are sampled at integer/fractional phases with nonidentity root/facing, tolerance 1e-4 metres. ANM vertex morphs reject the skin-only reference. This comparison verifies transform math, not GPU execution or gameplay presentation.

## Runtime material, LOD and bounds (X64SKIN03)

The retained GPU path accepts all four DDS channels and factors. BC5 reconstructs positive Z in the skinned, orthonormalized tangent frame; ORM supplies AO/roughness/metallic. SKN enters the existing materialGgx/Schlick sun, point-light and environment path without class-derived pseudo-relief. Emission is independent of base color. Opaque ignores authored alpha, mask uses the cutoff, blend uses source-alpha compositing without depth writes or opaque shadow casting; two-sided faces reverse their shading normal on the back.

One draw per semantic part/material/region owns the union of its LOD vertices and separate authored index lists; large sets split into native 16-bit component pages without changing vertices. A persistent GPU level switches discretely by projected error at the renderer's pixel limit. Moving coarser uses limit*(1-hysteresis), moving finer limit*(1+hysteresis). Shadows reuse that selected camera level. Static FIG LOD crossfades are unchanged.

For each joint the loader computes a bind-local sphere containing every vertex with a positive weight, across all LODs. Current pose matrices transform sphere centers and row-norm radii; their union contains the weighted convex skin. Pose-keyed bounds feed early CPU selection, part/light admission and the grow-only GPU envelope. No CPU-deformed vertex stream is uploaded. Bind geometry, gameplay proxies and resource paths are unchanged.

## Owner decisions and bahonec provenance

The source `FORMAT.md` of the dropped X64SKIN01 task is no longer present. The accepted specification above retains its SKN/driver/region/outfit decisions, including the 2026-11-10 surface-binding decision. The canonical project knowledge (`C:/Dev/EvilIslands/KNOWLEDGE.md`, Models and illustrations) confirms rights and provides this exact credit:

«Модель взята из личного проекта bahonec, сделанного им специально для Unity, и перенесена автором bahonec с помощью GPT-агента для использования в проекте „Проклятые земли“. Список одежды и материалов и перенос на новый движок bahonec также разрабатывает лично.»

Assets from bahonec/Dejavu-Female-Body are authorized for EIRebuild64. The clone remains read-only; texture cooking alone is not an implementation of its procedural rig or a gameplay acceptance.

## Current bind extraction (X64NALASKN01, incomplete)

The later owner decision in BRIEF supersedes the proposed section-8 GPU scalar
program above: run the existing complete NalaBodyShape/authoring/upper-body
calculation on CPU only on a change of the five inputs, then upload unit-local
bind vertices/normals and rig once. Animation must only evaluate bones on CPU.
The section-8 payload needed to reproduce this calculation is still pending;
the bind snapshot has no section 8 and cannot change shape at runtime.

`nala-to-skn --export-bind-mesh` currently cooks the resolver default
(build=.3, fullness=.3, height=.85, strength=25, dexterity=25), with all accepted
face corrections, 105% head size and proxy vertices. Section 3 contains 21,670
sampling vertices followed by 21,003 visible render vertices, with unchanged
UVs and 36,162 triangles. Eyes/teeth/tongue use their original skin slots and
renderer-local bind coordinates, which need not be common body coordinates.
There is no hair mesh in this extraction; the existing hair FIG is not cooked.

Section 13 is authoritative for affine bind frames and its 1,880 skin slots.
In this mode section 2 enumerates those slots with unique names and identity
TRS/inverse descriptors (all roots), not LNK joints. This explicit mode requires
sections 9 and 13 together; the decoder checks matching slot counts and identity
descriptors. A consumer must never skin these vertices through section 2 alone.
The 378 internal nodes and controller ordering remain in section 13. No slot
merging or reduction to 15 LNK joints occurs. Four influences are quantized by
largest remainder to sum exactly to 65535, without dropping an influence.

Section 9 uses the existing version-1 envelope and six tables. Two surfaces:
0 is sampling-only, with all native/proxy vertices and no indices; 1 is visible,
with all render vertices/indices. A single operation-0 stage replaces every
visible vertex from three sampling sources. Ordinary copies repeat their source
with weights (1,0,0); Edits retain three sources/weights. When the source table
writes a render vertex repeatedly, the last edit wins, as in Render(). All
normal/tangent offsets and frame/reserved fields are zero. Visible placeholder
skin lanes are ignored by the contact stage; they are not a four-bone
approximation of an edit. Blend each source's skinned, normalized normal, then
normalize the result, as in NalaLidContact::Render. This preserves blends that
can involve more than four bones. Proxy positions/normals and their inherited
skin are ordinary sampling vertices; no runtime topology generation is needed.

The container decoder supports only this two-surface/single-anchor-stage
subset. It checks table extents/overlap, strides, surface ownership and indices,
unique target coverage, source references, finite nonnegative normalized weights
and zero offsets/frame. Other contact stages still reject. The cooker decodes
the actual container, round-trips section 13 and compares serialized contact
bind positions/normals to the source Render result (position tolerance 1e-4 m,
normal cosine >= .99999). These cooking checks do not validate posed GPU output.

RuntimeAsset selection refuses Nala bind snapshots until shape data and GPU
contact submission are implemented. The pre-existing selected-Nala CPU route
still runs before SKN selection; replacing it, passing the five raw CUnit inputs,
per-unit shape caching and bone palettes, GPU contact and hair are outstanding.
No claim of a visible file-backed Nala or completed step 1 is made.