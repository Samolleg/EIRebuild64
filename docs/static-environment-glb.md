# Static environment GLB

`environment.v1` is the rigid profile for trees, buildings, stones and scenery
(MOB types 53–56). It does not use the character profile/database, skin or
animation assets. No FIG/MOD/BON/ANM counterpart is required. Unassigned models
keep their existing path; an assigned invalid GLB raises a labelled error.

## Resource and gameplay contract

Merge an entry into `models/registry.v1.json`, preserving other sections:

```json
{"version":1,"environment":{"example_tree":{
  "profile":"environment.v1","resource":"models/environment/example_tree.glb"
}}}
```

Asset extras are `{"ei":{"profile":"environment.v1","kind":"environment"}}`.
Each uniquely named mesh node carries explicit collision metadata:

```json
{"ei":{"collision":{"role":"solid","min":[-1,-1,0],"max":[1,1,2]},
        "render":true,"foliage":false}}
```

Roles: `solid`, `none`, `crown`, `walkable`. Optional collision `passability`
uses 0=blocked, 1=hard, 2=easy, 3=danger. `opacity` uses 0=100%, 1=50%, 2=25%,
3=0%. Crown/none default to easy/25%; other roles to blocked/100%. Walkable marks
a bridge surface. `render:false` retains collision without drawing geometry.

Collider coordinates are **model-local game Z-up**, already including node
transforms. Visual vertices use glTF Y-up and flatten to `(x,-z,y)`; normals
use inverse transpose and mirrored transforms reverse winding. Colliders are
not transformed a second time. A doorway needs separate wall/lintel volumes;
a visible hole alone does not provide a legacy AI passage.

Multiple material primitives become `node#0`, `node#1`, etc. Selecting a logical
node selects all its primitives. Empty MOB bodyparts select the whole model.
MOB retains model ID, type, transform and complection. Primary/secondary texture
must be empty or `none`; embedded materials replace legacy texture overrides.
Nonempty overrides are rejected. Interactive doors, levers, destruction and
skeletal/character animation are outside this static profile.

## Materials and preparation

Supported: metallic-roughness PBR, base color, normal, packed ORM, emission,
emissive strength, unlit, OPAQUE/MASK/BLEND, cutoff and double-sided faces.
Repeat/clamp/mirrored-repeat and `KHR_texture_transform` are supported. All maps
of one material need a common UV set, transform and sampler. Vertex colors and
advanced transmission/clearcoat/sheen/iridescence/anisotropy need baking.
Nondefault IOR/specular extensions are rejected.

PNG/JPEG are editable inputs. Distribution preparation bakes base factors,
normal scale and AO strength, packs AO into ORM, and embeds full BC3 DDS mip
chains through `MSFT_texture_dds`. Color mip filtering uses linear light;
MASK preserves coverage. The decoder also accepts BC1/BC2 full mip chains.
Prepared normal scale/AO strength must be 1. Preparation retains editable
sources, writes a new file and validates it before publication.

Limits: embedded buffers/images; 64 MiB GLB; 4096 nodes; 128 primitives; 65532
vertices and 3 million indices per primitive; 64 MiB packed geometry; 128 MiB
unique texture payload per asset; texture sides at most 1024. Split larger
meshes during authoring. Sparse accessors are supported. Skin/animation are
rejected.

## Forms, wind and LOD

Asset `ei.shape:"trilinear8"` enables Basis plus seven morph targets. Target
`s-1` is endpoint `s` minus endpoint 0. Endpoint bits are strength=0, grace=1,
height=2. Position/normal endpoints share topology, UVs and materials. Collision
can have one fixed min/max box or eight `shapes` entries containing min/max.
The same unclamped MOB trilinear weights drive geometry, normals and collision.

Optional asset `ei.wind` contains `enabled`, game-space `root`, `trunkRadius`,
`trunkHeight`, `height`. Eight forms may instead specify eight `shapes` vectors
`[radius,protectedTrunkHeight,plantHeight]`. All parts use one anchored rigid
root and the existing GPU sway. `foliage` explicitly selects leaf behavior.
Wind never moves gameplay volumes. The existing packed sway quantizes radius
at 1/16 and heights at 1/4, saturating at 63 units of each scale.

Immutable geometry/materials/textures are shared by resource identity. Texture
sharing includes format, mip layout, color interpretation and opacity policy.
No GLB parsing, image compression or mip generation occurs per frame. Prepared
DDS avoids PNG decoding/mip generation during distribution asset loading.

`FigureCpuAsset` is the packed renderer layout, not an intermediate FIG file.
Eight-form meshes keep all endpoints in one shared mesh with matching weighted
normals. The existing static simplification and crown impostor pipeline consume
these meshes on active and neighbouring maps. Collision and selection remain
independent of visual LOD. Neighbour piece wire is version 10; asset wire carries
environment material references and wind. Editor CPU exchange ABI 1 exposes
flattened geometry, forms, colliders and albedo without creating a GPU session.

## Tools and verification

Tools repository: `pz_environment_blender.py` exports tagged meshes;
`pz_environment.py prepare` prepares distribution textures; `install` previews
or publishes to an offline content checkout. One guarded engine transaction
updates GLB, registry and catalog while preserving unrelated entries.
`EIRebuild64ModelCheck --environment file.glb` validates the runtime contract.
These commands do not deploy the installed game.

`tools/environment-fixture.py` plus `tools/check-environment.ps1` cover malformed
input, transforms, logical selections, sharing, forms, wind metadata, materials,
mips, editor CPU ABI and resource-wire roundtrip. Private runtime evidence stays
in the task. CPU checks/build success do not establish gameplay, visual, LOD or
performance acceptance; those require control scenes in D3D11 and D3D12.
