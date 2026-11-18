# glTF equipment (X64GLTF10)

The host publishes one immutable equipment snapshot from the client visual's
local weapon/armor arrays. World, character panel and camp preview consume the
same selection. The camp copy never reads the world character's inventory.
`ResolveVisualLeftHand` continues to govern shields, paired weapons, staves and
two-handed exclusion. Changing/removing an item invalidates the appearance.

## Resource table

`models/registry.v1.json` retains version 1. `items` is keyed by the exact item
shape database `GetCode()` string (including spaces/case), then `materials` by
the exact material `GetCode()`, then by the selected visual slot: `hand_r`,
`hand_l`, or `armor_<ERPGArmorTypes index>`. These are database identifiers,
not translated display labels or row numbers.

```json
{
  "items": {
    "saber": {
      "materials": {
        "iron": {
          "hand_r": [
            {"profile":"human.v1", "resource":"models/items/saber-iron.glb",
             "socket":"socket_hand_r", "visibility":"weapon"}
          ],
          "hand_l": [
            {"profile":"human.v1", "resource":"models/items/saber-iron.glb",
             "socket":"socket_hand_l", "visibility":"weapon"}
          ]
        }
      }
    }
  }
}
```

This is an illustrative mapping, not a shipped model or an assertion about
which material name the active database uses. Resources must exist in the same
published package as the registry. A slot may list up to 16 rigid pieces, each
with its own resource/socket. An empty list explicitly has no visual. A shape
absent from `items` is not registered for the new equipment path; no item mesh
is invented. A registered shape with a missing material/slot or invalid GLB
is rejected with diagnostics, without substituting another item's surface.

Allowed targets: `socket_hand_r`, `socket_hand_l`, `socket_back`, `socket_belt`,
`socket_head`, or any named bone declared in `human.v1` and present in the body.
For example, a strapped shield can select `CC_Base_Forearm.L` with its own grip.
Generated helper nodes and unknown bones are rejected. X64GLTF10J adds these
profile bone targets without changing the profile bind or GPU ABI. The registry chooses placement; GLB metadata declares a valid
default socket. `visibility` is `weapon`, `arrow`, or `always`. Its default is
`weapon` for weapons and `always` for armor. Arrow visibility includes weapon
hiding. Private preview visibility uses its own clip phase and the host's
existing hide-event rules. Both color and shadow submissions apply the mask.

## Rigid GLB contract

Use the existing Blender exporter `tools/character-export/export.py --kind item`.
The saved GLB must be self-contained, have one active scene and contain triangle
meshes with normals. UV/tangent/material requirements are the same as body GLBs.
Rigid items have no skins, morph targets, animation clips or ADB bindings.

```json
{"profile":"human.v1", "kind":"rigid-item", "socket":"socket_hand_r",
 "grip":{"translation":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
 "anchors":{"cast":[0,0.7,0],"particle":[0,0.7,0]}}
```

This metadata is `asset.extras.ei`. Coordinates are glTF profile meters/Y-up.
`grip` is mandatory; it identifies the grip frame in the item's model space.
`anchors` is optional, supports `cast` and `particle`, and is also in item model
space (not mesh-node local space). The loader bakes node hierarchy and inverse
grip into the immutable vertices/anchors once. Positive finite scales and unit
quaternions are required. Runtime placement is:

`bodyWorld * animatedSocket * inverse(grip) * itemMeshWorld * vertex`.

The GPU vertex uses one socket influence and identity inverse bind. The body
owns the only animation library, clock, transitions and pose palette; items
neither copy clips nor allocate another animation program. Material/geometry
resources are shared by identity and mount generation. CPU effect queries
sample only indexed socket ancestor tracks with the same playback/transition;
they do not deform the body or read the GPU back. Without an explicit anchor,
the cast/effect point is the corresponding hand socket origin.

Bounds include each item's radius under the socket's all-clip envelope. Preview
uses the same expanded bounds and the item's own material, including blend
ordering. Changing equipment only rebuilds the cold appearance/group binding.

## Boundaries and verification

This stage adds the common rigid attachment runtime. The installed content
registry currently has no item mappings. X64GLTF10A prepares a control saber
and strapped shield as separate resources; the rest of the item catalog still
requires explicit assets/mappings. Do not install an EXE alone and
claim that every existing weapon has been converted. Clothing surface binding,
body coverage and shared garment morphs belong to the following stage; the
author's ready starter wool set is its source. Animated bow/crossbow mechanics
are not representable as a single rigid item and require authored separate data.

Checks:

```powershell
tools/check-gltf-animation.ps1 -Equipment -OutputDirectory <task/check> -Glb <body.glb> -Profile <human.v1.json>
tools/check-model-preview.ps1 -OutputDirectory <task/check-preview>
EIRebuild64ModelCheck.exe <item.glb> --profile <human.v1.json>
```

The equipment check covers host selection, removal/private-copy isolation,
registry failures, rigid hierarchy/grip/material/anchors, and all five sockets
of the actual body with D3D11/D3D12 production pose compute, including transition.
The preview check exercises the production producer with distinct body/item
materials, expanded bounds, hidden weapons/arrows and the existing FIG path.
These checks do not replace in-game fit, lighting, effects and FPS acceptance.

## Skinned wardrobe items

`asset.extras.ei.kind = "skinned-item"` uses standard GLB skins and the existing
human.v1 named bones/inverse binds. Registry armor entries select `"binding":"skin"`
and omit `socket`. Each item has its own geometry/materials; only referenced joints
are mapped into the body's current palette, once per immutable outfit change.
No item animation, morph channels, private GPU pose, or per-frame CPU mesh deformation.
Unbound morphs and per-item clips are rejected. Future morph-bearing clothes require
an explicit shared-weight contract; absence is never silently treated as support.

World/panel/camp use the existing immutable equipment snapshot. Invalid bindings
reject the visual atomically. Removing a selection removes its groups. This does
not claim general multilayer occlusion or arbitrary body-shape morph support.
Bounds use the body's animation envelopes and each weighted vertex in joint space.

`tools/check-gltf-animation.ps1 -Equipment -Glb ... -Profile ... -ClothingDirectory ...`
checks saved garments, actual registry combinations, body/weapon preservation and
shared D3D11/D3D12 poses. Surface-bound source clothes are cooked offline to four
ordinary bone influences; their visual fit must be reviewed, not inferred from
successful decoding. No shader/pose ABI change is needed for this path.

## Outfit conditions and default layers (X64GLTF14)

An attachment may specify `requiresEquipment` or `unlessEquipment`, each an object
with an exact `slot`, a `shape`, and optional `material`. These conditions read the
immutable inventory snapshot, not the results of other conditions. Mutual shirt
and pants conditions therefore cannot recurse. The garment cut and fabric remain
data; no character-specific names are present in the renderer.

The shape may also be `"*"` to match any armor shape in the specified slot.
With `"hasSelection": true`, it matches only when that item's selected material
has a nonempty explicit attachment array, or a texture override with a nonempty
common attachment array. Empty selections and unknown items do not match. This
checks the configured selection without recursively evaluating its attachment
conditions; use it for unconditional slot visuals. For example, a closed
helmet can select a different braid for an equipped upper armor and restore the
original braid on removal. Exact-shape conditions retain their earlier behavior.

Optional `baseLayers` is keyed by the selected body's exact resource key. Each
entry is an ordinary skinned attachment plus a unique `base_*` slot and a
`hiddenBySlots` array of armor slots. A base layer is hidden only if a selected
skinned attachment actually covers one of those slots; empty/unregistered visuals
do not suppress it. Maximum eight default layers, selected only on outfit changes.
This is whole-layer visibility, not body polygon removal or general cloth collision.
The complete body and its animation library are unchanged.

The wardrobe regression loads all 66 render assets, checks 44 selectable variants,
48 equip/remove cases with mixed materials, base-layer return, shirt/wrap variants,
and body identity isolation, followed by D3D11/D3D12 shared-pose comparisons.
