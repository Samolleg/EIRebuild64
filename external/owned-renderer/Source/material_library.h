#pragma once
// Material classes for owned character and equipment shading (pseudo-PBR
// S1). One authoritative data table (eid3d_materials\classes.json next to
// the renderer), the game's item catalog (items.idb, active database
// archives) and reviewed region maps (eid3d_materials\maps\<key>.png,
// 8-bit indexed: value = class index, 0 = the layer's item class). A
// composed atlas' profile is built from its layer list (MaterialComposition)
// in the game's layer order with each layer's own alpha. No engine pointer
// or object identity: keys are resource names.
#include "owned_resource_store.h"

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace owned {

struct MaterialComposition;
struct TextureCpuAsset;
struct TextureAlphaPixels;

struct MaterialClass {
    std::string name;
    float roughness = .8f, metal = 0, f0 = .02f, relief = 0, sheen = 0;
    // Woven metal (chain mail): per-pixel link and gap contrast in the
    // shader. Metals have no cloth sheen, so a metal texel's profile alpha
    // carries this instead (weave_or_sheen).
    float weave = 0;
    float weave_or_sheen() const noexcept { return metal >= .5f ? weave : sheen; }
    // Chain mail: the layer's own item class, at least this rough.
    bool inherit_primary = false;
    float roughness_min = 0, relief_min = 0;
    // A coloured metal's own hue range in degrees (bronze, gold); none: a grey metal.
    float tint_min = 0, tint_max = -1;
    // Grows strands (the fur pass): fur and pelt armour layers.
    bool strands = false;
    // No creature coat grows here (the fur pass): horn, bone, eyes on a furry
    // creature's own art (antlers and tusks are islands of its head part).
    bool bare = false;
    // A creature coat's length on this class, a multiple of the model's coat
    // (a zebra's sleek hide, its mane). Bare classes take 0.
    float coat = 1;
    // A gem (diamond): the shader's fire and light-through branch. It has no
    // profile channel of its own: a gem is the only dielectric with F0 of
    // .12 and up (the loader enforces both ways), which the shader tests.
    bool gem = false;
    // A crystal (world crystals, crystal golems): the crystal instance programs
    // (native_shader.h NATIVE_CRYSTAL). Marked like the gem by its F0: a
    // crystal is the only dielectric with F0 .10 (the loader enforces both
    // ways); the shader shades it with its own constants.
    bool crystal = false;
    bool defined = false;
};

class MaterialLibrary {
public:
    // Reads classes.json and items.idb. False: material shading stays off
    // (error names the reason, for one log line and the overlay notice).
    bool load(std::string& error);
    float metal_diffuse_keep() const noexcept { return diffuse_keep_; }
    // RGBA8 profile at side x side: r roughness, g metal, b dielectric F0 x4,
    // a sheen (metals: the weave); relief (R8) the strength of the relief the shader derives from
    // the colour texture; and beside it the class index of each texel (R8: the topmost
    // layer covering at least half the texel, its region class where a map
    // names one). False when no layer is usable (the atlas stays unclassified).
    // metal_mask (F10 material_metal_mask, classes.json "metal_mask"): metal
    // layers without a region map read texels clearly off their metal tint as
    // leather or cloth; on success note then reports the reclassified texels.
    bool build_profile(const MaterialComposition& composition, std::uint32_t side, bool metal_mask,
                       std::vector<std::uint8_t>& rgba, std::vector<std::uint8_t>& relief,
                       std::vector<std::uint8_t>& classes, std::string& note) const;
    // A fixed-texture unit's profile (units.json, keyed by its game texture
    // name): one class, plus listed figure parts painted with their own class
    // over their UV footprint. False when the texture is not listed (note
    // says why); side is the profile's size (4 uniform, 128 with parts).
    bool build_unit_profile(const std::string& texture, std::uint32_t& side, std::vector<std::uint8_t>& rgba,
                            std::vector<std::uint8_t>& relief, std::vector<std::uint8_t>& classes,
                            std::string& note) const;
    // Scenery (static world objects): scenery.json lists textures like
    // units.json (class, optional maps\<texture>.png). Unlisted world
    // textures keep the texture-derived world response (S2).
    bool scenery_listed(const std::string& texture) const;
    // Explicit plant labels (maps/burn_<texture>.png), otherwise the
    // reviewed scenery classes/regions. Unclassified material is inert.
    bool build_fire_burn_profile(const std::string& texture, std::uint32_t& width,
        std::uint32_t& height, std::vector<std::uint8_t>& labels) const;
    // The classes that grow strands, one bit per class index (256 bits).
    bool scenery_listed(const std::string& texture) const;
    std::array<std::uint32_t, 8> strand_classes() const noexcept;
    // The crystal classes, one bit per class index.
    std::array<std::uint32_t, 8> crystal_classes() const noexcept;
    // Each class's creature coat factor (0 bare, 32 = 1x), one byte per
    // class index, four to a word.
    std::array<std::uint32_t, 64> coat_classes() const noexcept;

private:
    struct Shape { std::string type, material; std::uint32_t first = 0, second = 0, type_id = 0; };  // material: the shape's item type (metal, bone...)
    struct Material { std::string type, code; };
    struct RegionMap {
        std::uint32_t width = 0, height = 0;
        std::vector<std::uint8_t> classes;  // row-major class indices
    };
    // classes.json "metal_mask". A texel is off the metal when its saturation
    // exceeds saturation and its hue lies outside the metal class' own tint
    // (a grey metal has none: any coloured texel is off). The layer's own
    // colours are no reference: EI's metal items are often mostly leather or
    // cloth, and bronze leather straps share the bronze hue. Off texels with
    // a hue in brown_hue become leather; others with a saturation of at least
    // cloth_saturation become cloth; the rest stay. Texels darker than
    // min_value are left to the shader's gap test.
    struct MetalMask {
        bool defined = false;
        float saturation = .3f, cloth_saturation = .45f;
        float brown_hue_min = 0.f, brown_hue_max = 60.f, min_value = .08f;
        std::uint8_t leather = 0, cloth = 0;
    };
    MetalMask metal_mask_;
    struct LayerClass {
        std::uint8_t primary = 0;           // class index of the whole layer
        std::shared_ptr<const RegionMap> regions;
    };
    std::array<MaterialClass, 256> classes_{};
    std::map<std::string, std::uint8_t> class_index_, items_;
    std::uint8_t skin_ = 0, fallback_ = 0;
    struct UnitTexture {
        std::uint8_t primary = 0;
        std::string model;                                         // .mod of the listed parts
        std::vector<std::pair<std::string, std::uint8_t>> parts;  // figure part -> class
    };
    std::map<std::string, UnitTexture> units_;       // lower-case texture name (units.json, then scenery.json)
    std::set<std::string> scenery_;                  // the scenery.json names among them
    std::map<std::string, std::uint8_t> unit_skins_;  // model -> class of <model>skin_NN layers
    float diffuse_keep_ = .35f;
    std::vector<Material> materials_;
    std::vector<Shape> armor_, weapon_;
    std::wstring folder_;
    // The texture archives the layers come from, opened once (render thread only).
    mutable ResourceStore layers_;
    mutable bool layers_open_ = false;
    // Layers already reported as having no region map (one bridge event per name).
    mutable std::set<std::string> unmapped_layers_;
    // Across builds (builds run one at a time): decoded region maps by file
    // key, type and code (null: no usable map), and parsed layer textures by
    // name. Equips mostly reuse layers: without these each atlas re-read and
    // re-decoded every layer and map (~100 ms an atlas).
    mutable std::map<std::string, std::shared_ptr<const RegionMap>> region_cache_;
    mutable std::map<std::string, std::shared_ptr<const TextureCpuAsset>> layer_cache_;
    mutable std::vector<std::string> layer_cache_order_;
    std::shared_ptr<const TextureCpuAsset> cached_layer(const std::string& name) const;

    bool load_classes(const std::string& text, std::string& error);
    bool load_items(const std::vector<std::uint8_t>& bytes);
    bool load_units(const std::string& text, std::string& error, bool scenery = false);
    bool open_layers(std::string& note) const;
    // One metal layer's mip under metal_mask_ and its metal class' tint:
    // per texel 0 (metal) or the leather/cloth class; counts its opaque and
    // reclassified texels. False: its colour is unreadable (the layer stays
    // uniformly metal).
    bool metal_mask_texels(const TextureCpuAsset& texture, std::size_t mip, const TextureAlphaPixels& alpha,
                           const MaterialClass& metal_class, std::vector<std::uint8_t>& off, std::uint32_t& metal,
                           std::uint32_t& leather, std::uint32_t& cloth) const;
    LayerClass layer_class(const std::string& layer, const std::vector<std::uint8_t>& user_data,
                           std::uint32_t width, std::uint32_t height) const;
    LayerClass untagged_layer_class(const std::string& layer, const std::string& name, LayerClass result,
                                    std::uint32_t width, std::uint32_t height) const;
    std::shared_ptr<const RegionMap> region_map(const std::string& family, const std::string& type,
        const std::string& code, std::uint32_t variant, std::uint32_t width, std::uint32_t height) const;
    std::shared_ptr<const RegionMap> read_region_map(const std::string& key, const std::string& type,
                                                     const std::string& code) const;
    std::shared_ptr<const RegionMap> read_region_map_uncached(const std::string& key, const std::string& type,
                                                     const std::string& code) const;
};

// Terrain 2.0 roughness by cell class (pseudo-PBR S3; terrain2.hlsl cell
// bits 12-14): ground, hard, grass, snow, ice, sand, mud, flat-only. The
// defaults stand unless classes.json "terrain" names a class {name: roughness};
// an unreadable file or value keeps them and logs one "materials" event.
void load_terrain_roughness(std::array<float, 8>& roughness);

} // namespace owned
