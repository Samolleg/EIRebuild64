# Static environment GLB: first rigid slice

The environment registry selects a separate rigid loader. It does not use the
character profile, skin, character database or animation assets. A new object
requires no FIG/MOD/BON/ANM counterpart. Existing unassigned objects keep their
normal path; a selected invalid GLB throws a labelled error.

This is an implementation checkpoint, not the complete building/vegetation
feature. The verified fixture is a single solid matte part. Trees, wind, alpha
foliage, texture maps, authored LOD/impostors, deformation forms, interactive
objects, walkable bridges and Ataman authoring remain subsequent work.

## Resource contract

Merge an entry into the existing `models/registry.v1.json`, preserving the other
sections:

```json
{"version":1,"environment":{"sgcrate01":{
  "profile":"environment.v1",
  "resource":"models/environment/sgcrate01.glb"
}}}
```

GLB asset extras: `{"ei":{"profile":"environment.v1","kind":"environment"}}`.
Each named mesh node has one triangle primitive with positions and normals and
explicit extras, for example:

```json
{"ei":{"collision":{"role":"solid","min":[-1,-1,0],"max":[1,1,2]}}}
```

Collision coordinates are model-local **game Z-up** coordinates. They are
independent of visual mesh bounds and render simplification. `none` ignores that
part as an obstacle. The first slice exposes no bridge/crown/passability modes.
Vertices use normal glTF Y-up coordinates; flattening node transforms converts
them to `(x,-z,y)`, transforms normals with the inverse transpose and reverses
winding for a mirrored transform. Collision coordinates must already match the
flattened model; a node transform does not transform them a second time.

Limits: embedded GLB buffer, 64 MiB file, 128 parts, 65532 vertices and 3000000
indices per part. Reject skin, animation, morphs, images/maps, unsupported vertex
attributes and non-matte materials. A supplied material must be opaque,
single-sided metallic-roughness with metallic=0 and roughness=1. The linear
baseColorFactor is converted once to a shared one-pixel sRGB albedo; no full PBR
equivalence is claimed. The alpha factor is ignored for opaque material.

Only building, stone and stuff-object host classes enter this path. Use an empty
MOB bodyparts list for the first verified whole-model case. Multipart selection,
save/load, neighbouring maps and gameplay visibility queries still need their
own acceptance cases before broadening the supported authoring workflow.

## Runtime and costs

The immutable packed mesh uses the renderer's existing `FigureCpuAsset` storage
as an internal vertex/index layout. No FIG is serialized or decoded. Rigid
scene nodes use shared neutral transforms, with no skin weights or animated
host figure objects. Assets are cached by name and resource mount generation;
instances share geometry, material, texture and rigid transforms. The host
publishes a retained scene object and skips legacy figure/texture loading.

Game bounding volumes come from explicit collision metadata. Nav64 receives the
same rigid geometry directly where it formerly requested a FIG. The shared
renderer can build its regular static LODs from this packed layout; this does
not prove authored LOD, vegetation impostors or wind support.

## Reproduction

`tools/environment-fixture.py --output <new-private-directory>` generates the
24-vertex, 12-triangle fixture and malformed/scaled inputs. Run
`tools/check-environment.ps1 -OutputDirectory <private-check> -Fixtures <fixtures>`
after preparing worktree dependencies. The check verifies axis/scale conversion,
explicit bounds, invalid input rejection, exact albedo pixels and resource
sharing over 1000 assembly descriptions. Packed geometry is 992 bytes; this
excludes GPU allocations, instance state, cache/container overhead and textures.

Runtime verification and private evidence belong to STATICGLB02/STATICGLB02A,
not this repository. Do not treat this CPU check as 1000 GPU instances or as
proof of scene performance.
