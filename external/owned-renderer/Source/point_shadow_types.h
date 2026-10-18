#pragma once
#include <array>
#include <cstdint>
namespace owned {
// Point shadows live in two atlases (renderer_point_shadows.cpp): static cubes
// (D16) at t25 and overlay cubes (D32) at t26. Each array slice holds an
// atlas_tiles-square grid of cube faces, each inside a one-texel gutter; the
// slice count follows demand up to the D3D11 2048-slice limit, so an atlas
// holds at most cubes_per_atlas cubes (D3D11 limits, not quality settings).
// A slot below cubes_per_atlas names a static cube, the others overlay cubes.
// t122: the terrain noise texture (terrain_material.h). t123-t126: the terrain
// material cache's bake table, far atlas, page table and near atlas
// (terrain_bake.h); t127 the terrain frame buffer; t22-t24 liquid resources.
struct PointShadowTextureLayout {
    static constexpr unsigned atlas_tiles=4, faces_per_slice=atlas_tiles*atlas_tiles, max_slices=2048,
        cubes_per_atlas=max_slices*faces_per_slice/6, first_srv=25, array_bindings=2, terrain_noise_srv=122,
        terrain_bake_bake_pages_srv=123, terrain_bake_far_srv=124, terrain_bake_pages_srv=125, terrain_bake_srv=126, terrain_frame_srv=127;
};
struct NativePointShadowStats {
    std::uint64_t baked=0,rebaked=0,overlays=0,budget=0;
    std::uint64_t radius_dirty=0,terrain=0,light=0,list=0,reset=0,allocation_failures=0,pressure_drops=0;
    std::uint64_t reorder_only=0,reset_epoch=0,reset_journal_full=0,reset_class_flags=0,reset_undetected=0,reset_pool=0;
    std::uint64_t eligibility_lost=0,strength_zeroed=0,static_to_dynamic=0,merged_lights=0;
    std::uint64_t casting_lights=0; // latest frame
    bool pass_ran=false;
};
struct NativePointLightValue {
    std::array<float,4> center_inverse_radius_squared{},color{},shadow_center{},shadow_slots{};
};
inline bool native_point_light_inputs_equal(const NativePointLightValue& a,
    const NativePointLightValue& b) noexcept {
    // Publication changes never make an unchanged input light look dirty.
    return a.center_inverse_radius_squared==b.center_inverse_radius_squared &&
        a.color==b.color && a.shadow_center==b.shadow_center;
}
} // namespace owned
