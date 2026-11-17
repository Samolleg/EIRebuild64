# glTF equipment: common model and material textures

`models/registry.v1.json` selects equipment by shape, material and slot. Existing material-specific arrays keep their previous meaning. A material-specific array is the complete selection: it takes precedence over the shape default, and an empty array deliberately selects no visual.

For a common model with different base-color textures, add `defaults` for the slot and give each material a texture selection object:

```json
{
  "items": {
    "hood": {
      "defaults": {
        "armor_0": [
          {"profile":"human.v1", "resource":"models/items/hood.glb",
           "socket":"socket_head", "visibility":"always"}
        ]
      },
      "materials": {
        "wolf hide": {
          "armor_0": {"textures": [
            {"binding":0, "material":0, "baseColor":"models/items/textures/hood-wolf.png"}
          ]}
        },
        "special hide": {
          "armor_0": [
            {"profile":"human.v1", "resource":"models/items/hood-special.glb",
             "socket":"socket_head", "visibility":"always"}
          ]
        }
      }
    }
  }
}
```

`binding` indexes the default slot's attachment array; `material` is the glTF material index in that GLB. `baseColor` names a PNG below `models/items/textures/`; it replaces only that material's base-color map. Other PBR maps, alpha mode, cutoff, culling and factors come from the selected GLB. The original linear base-color factor is applied to the replacement during decoding, matching embedded glTF images. Texture overrides require a material already used by a submesh with a base-color texture and UVs. Duplicate targets, unknown fields, missing variants, invalid paths, unsupported formats and out-of-range indices are errors; they do not select a different material silently.

The common GLB is decoded and cached by resource identity, independently of the selected texture. Selected PNGs are decoded on appearance changes and weak-cached by source identity and color factor; retained draw groups hold their pixel data while needed. Thus material changes do not create another CPU/GPU geometry identity, and actors using the same material share the selected texture. The same prepared groups feed world and character preview. This mechanism adds no per-frame mesh deformation and changes no shader ABI.

The format changes the color texture only; it does not supply separate normal, metallic/roughness or emissive maps. These and renderer-specific effects require separate work if a particular item needs them. A rigid item follows its named socket and has no runtime FIG8 shape interpolation. Authoring sources and their shapes should be kept separately.
