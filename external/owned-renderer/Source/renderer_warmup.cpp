// Pipeline warm-up under the load hold (renderer.h pump_pipeline_warmup).
//
// D3D11 drivers finish a shader's machine code lazily: at the first draw
// with that program combination and its output state (stage linkage, render
// target formats and count, depth format, topology class). A held load frame
// already draws everything the camera sees; this covers what a later first
// draw would otherwise compile mid-play: the settings-specialised sibling sets
// (point shadows toggled, contact off, fast combiner) and the variants the
// view lacks, liquids, the water reflection passes, game particles (and their
// reflection depth), and the point-shadow depth passes. Each combination is
// one Draw(3) into 4x4 scratch targets of the real formats, with no vertex
// input and no readback; the hold then waits for an event query, so the
// compile has finished before the world is shown. With a warm driver the
// whole list costs a few milliseconds of CPU once per load.
#include "renderer.h"
#include "engine_bridge.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

namespace owned {
namespace {
constexpr UINT kWarmSide = 4;
// Held-frame pacing: a cold driver may compile on this thread inside a draw,
// so a held frame stops issuing after this long and presents (the loading
// picture keeps showing); the rest goes in the next held frame.
constexpr auto kWarmSliceTime = std::chrono::milliseconds(30);

struct WarmItem {
    ID3D11VertexShader* vs = nullptr;
    ID3D11HullShader* hs = nullptr;
    ID3D11DomainShader* ds = nullptr;
    ID3D11PixelShader* ps = nullptr;
    DXGI_FORMAT targets[3]{DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN};
    UINT target_count = 0;
    DXGI_FORMAT depth = DXGI_FORMAT_UNKNOWN;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depth_state = nullptr;
    ID3D11RasterizerState* raster = nullptr;
};

// The colour targets one world pass binds (an unknown format is an unbound slot).
struct WarmTargets {
    DXGI_FORMAT formats[3]{DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_UNKNOWN};
    UINT count = 0;
    bool operator==(const WarmTargets& o) const noexcept {
        return count == o.count && std::memcmp(formats, o.formats, sizeof(formats)) == 0;
    }
};

DXGI_FORMAT view_format(ID3D11RenderTargetView* view) noexcept {
    if (!view) return DXGI_FORMAT_UNKNOWN;
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    view->GetDesc(&desc);
    return desc.Format;
}
DXGI_FORMAT view_format(ID3D11DepthStencilView* view) noexcept {
    if (!view) return DXGI_FORMAT_UNKNOWN;
    D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
    view->GetDesc(&desc);
    return desc.Format;
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept {
    hash ^= value;
    return hash * 1099511628211ull;
}
std::uint64_t item_hash(std::uint64_t hash, const WarmItem& item) noexcept {
    hash = mix(hash, reinterpret_cast<std::uintptr_t>(item.vs));
    hash = mix(hash, reinterpret_cast<std::uintptr_t>(item.hs));
    hash = mix(hash, reinterpret_cast<std::uintptr_t>(item.ds));
    hash = mix(hash, reinterpret_cast<std::uintptr_t>(item.ps));
    for (const DXGI_FORMAT format : item.targets) hash = mix(hash, static_cast<std::uint64_t>(format));
    hash = mix(hash, item.depth);
    hash = mix(hash, reinterpret_cast<std::uintptr_t>(item.blend));
    hash = mix(hash, reinterpret_cast<std::uintptr_t>(item.depth_state));
    return mix(hash, reinterpret_cast<std::uintptr_t>(item.raster));
}
}  // namespace

void Renderer::reset_pipeline_warmup() noexcept {
    warmup_done_ = warmup_query_pending_ = false;
    warmup_signature_ = 0;
    warmup_next_ = 0;
    warmup_draws_ = 0;
    warmup_items_ = 0;
}

void Renderer::pump_pipeline_warmup() noexcept try {
    if (warmup_done_ || !device_ || !context_) return;
    if (warmup_query_pending_) {
        if (context_->GetData(warmup_query_.Get(), nullptr, 0, 0) == S_OK) warmup_done_ = true;
        return;
    }
    // In a mission the hold first waits for the current key's specialised
    // sets (load_reveal_waiting_on); the list is built from them once settled.
    if (bridge_local_single_player_mission() && !native_specialisation_settled()) return;
    if (feature_level_ < D3D_FEATURE_LEVEL_11_0) { warmup_done_ = true; return; }

    const RendererConfig& config = debug_overlay_.config();
    ID3D11RenderTargetView* const scene = scene_composite_.target() ? scene_composite_.target() : rtv_.Get();
    ID3D11DepthStencilView* const world_depth_view = world_dsv_ ? world_dsv_.Get() : dsv_.Get();
    const DXGI_FORMAT scene_format = view_format(scene), world_depth = view_format(world_depth_view);
    if (scene_format == DXGI_FORMAT_UNKNOWN || world_depth == DXGI_FORMAT_UNKNOWN) return; // no world target yet

    std::vector<WarmItem> items;
    items.reserve(320);
    const auto add = [&](ID3D11VertexShader* vs, ID3D11HullShader* hs, ID3D11DomainShader* ds, ID3D11PixelShader* ps,
                         const WarmTargets& targets, DXGI_FORMAT depth, ID3D11BlendState* blend = nullptr,
                         ID3D11DepthStencilState* depth_state = nullptr, ID3D11RasterizerState* raster = nullptr) {
        // A colour pass without its program is not drawn; depth-only passes may have none.
        if (!vs || (hs != nullptr) != (ds != nullptr) || (!ps && targets.count)) return;
        WarmItem item;
        item.vs = vs; item.hs = hs; item.ds = ds; item.ps = ps;
        std::memcpy(item.targets, targets.formats, sizeof(item.targets));
        item.target_count = targets.count; item.depth = depth;
        item.blend = blend; item.depth_state = depth_state; item.raster = raster;
        items.push_back(item);
    };

    // World material pass targets (flush_native wanted_targets): the scene,
    // the shadow history for non-ordered batches, the AO ambient share for all.
    const DXGI_FORMAT history = view_format(shadow_history_rtv_[0].Get());
    const DXGI_FORMAT ambient = view_format(ao_ambient_rtv_.Get());
    WarmTargets ordered;
    ordered.formats[0] = scene_format;
    ordered.count = 1;
    if (ambient != DXGI_FORMAT_UNKNOWN) { ordered.formats[2] = ambient; ordered.count = 3; }
    WarmTargets lit = ordered;
    if (history != DXGI_FORMAT_UNKNOWN) { lit.formats[1] = history; lit.count = (std::max)(lit.count, 2u); }
    const bool two_configs = !(lit == ordered);

    const bool low = native_low_ps_ && native_low_liquid_ps_ && native_low_pixel_path(config);
    const bool tessellation = configured_integer(config, "terrain_tessellation", 1) > 0 && terrain_tess_vs_ &&
        terrain_hs_ && terrain_ds_ && terrain_hs_plain_ && terrain_ds_plain_;
    const bool masked = configured_boolean(config, "depth_prepass", true) && native_prepass_vs_ && masked_depth_state_ &&
        native_shadow_mask_vs_ && shadow_mask_rtv_;
    const DXGI_FORMAT mask_format = view_format(shadow_mask_rtv_.Get());

    // The specialised sets native_pixel can bind for the current key: its own
    // and its siblings (bits 2, 5 and 11 differ: contact, point shadows, fast
    // combiner), each once resident; the general programs while the key's own
    // set is not.
    constexpr std::uint32_t kSiblingBits = 4u | 32u | 2048u;
    const std::uint32_t base = native_spec_current_key_;
    std::vector<const NativeSpecialisedSet*> sets;
    for (const auto& entry : native_spec_sets_)
        if (entry.first && (entry.first & ~kSiblingBits) == (base & ~kSiblingBits) && entry.second[0]) sets.push_back(&entry.second);
    const auto base_set = base ? native_spec_sets_.find(base) : native_spec_sets_.end();
    const bool general = base_set == native_spec_sets_.end() || !base_set->second[0];
    const auto members = native_specialised_members();
    // NativeSpecialisedSet order (native_specialised_members): 0 object,
    // 1 terrain, 2 snow, 3 low, 4 low unlit, 5 terrain flat, 6 snow flat,
    // 7 masked, 8 masked low, 9 masked terrain, 10 masked snow, 11 masked
    // terrain flat, 12 masked snow flat, 13 shadow mask, 14 low shadow mask,
    // 15 masked terrain slim, 16 masked snow slim, 17 and 18 their flat variants.
    const auto material = [&](std::size_t index, ID3D11PixelShader* ps) {
        if (!ps) return;
        const bool low_member = index == 3 || index == 4 || index == 8 || index == 14;
        const bool snow = index == 2 || index == 6 || index == 10 || index == 12 || index == 16 || index == 18;
        // The Low path binds only the low members (and snow terrain under a
        // trail field); the full path never binds a low member.
        if (low ? !(low_member || snow) : low_member) return;
        if (index == 13 || index == 14) { // the full-screen sun-shadow mask pass
            if (masked)
                add(native_shadow_mask_vs_.Get(), nullptr, nullptr, ps, WarmTargets{{mask_format}, 1}, world_depth,
                    nullptr, shadow_mask_depth_state_.Get(), shadow_mask_raster_.Get());
            return;
        }
        const bool masked_member = index >= 7;
        if (masked_member && !masked) return;
        // Terrain programs draw untessellated (neighbour maps, flat regions)
        // and as patches (on the Low path terrain takes the low members).
        const bool terrain = index == 1 || index == 2 || index == 5 || index == 6 || index >= 9 ||
            (low && (index == 3 || index == 8));
        ID3D11DepthStencilState* const depth_state = masked_member ? masked_depth_state_.Get() : nullptr;
        for (const WarmTargets* targets : {&lit, &ordered}) {
            if (targets == &ordered && (!two_configs || masked_member)) continue; // prepassed batches are never ordered
            if (index == 4 && targets == &lit && two_configs) continue;           // low unlit: ordered batches only
            add(native_vs_.Get(), nullptr, nullptr, ps, *targets, world_depth, nullptr, depth_state);
            if (terrain && tessellation)
                add(terrain_tess_vs_.Get(), snow ? terrain_hs_.Get() : terrain_hs_plain_.Get(),
                    snow ? terrain_ds_.Get() : terrain_ds_plain_.Get(), ps, *targets, world_depth, nullptr, depth_state);
        }
    };
    // Then the material instance programs (renderer.h kNativeInstance*), each
    // drawn as the object member (0) or the prepassed object member (7).
    for (const NativeSpecialisedSet* set : sets) {
        for (std::size_t n = 0; n < kNativeSpecialisedMembers; ++n) material(n, (*set)[n].Get());
        for (std::size_t n = 0; n < kNativeInstancePrograms; ++n)
            if (native_instance_built(n / 2u))
                material((n & 1u) ? 7u : 0u, (*set)[kNativeSpecialisedMembers + native_built_instance_slots_before(n / 2u) * 2u + (n & 1u)].Get());
    }
    if (general) {
        for (std::size_t n = 0; n < kNativeSpecialisedMembers; ++n) material(n, (this->*members[n]).Get());
        for (std::size_t n = 0; n < kNativeInstancePrograms; ++n) material((n & 1u) ? 7u : 0u, native_instance_ps_[n].Get());
    }

    // Liquid surfaces (general programs only): patches while tessellation is
    // on, else the plain vertex stage; the program choice follows flush_native.
    {
        ID3D11VertexShader* const vs = tessellation ? terrain_tess_vs_.Get() : native_vs_.Get();
        const bool snow_stages = snow_terrain_srv_ != nullptr;
        ID3D11HullShader* const hs = tessellation ? (snow_stages ? terrain_hs_.Get() : terrain_hs_plain_.Get()) : nullptr;
        ID3D11DomainShader* const ds = tessellation ? (snow_stages ? terrain_ds_.Get() : terrain_ds_plain_.Get()) : nullptr;
        ID3D11PixelShader* const full[] = {native_water_ps_.Get(), native_water_ssr_ps_.Get(), native_liquid_ps_.Get(),
            native_liquid_ssr_ps_.Get()};
        ID3D11PixelShader* const always[] = {native_lava_ps_.Get(), native_low_water_ps_.Get(), native_low_liquid_ps_.Get(),
            native_liquid_foam_ps_.Get()};
        for (ID3D11PixelShader* ps : always)
            if (ps) for (const WarmTargets* targets : {&lit, &ordered}) if (targets == &lit || two_configs) add(vs, hs, ds, ps, *targets, world_depth);
        if (!low)
            for (ID3D11PixelShader* ps : full)
                if (ps) for (const WarmTargets* targets : {&lit, &ordered}) if (targets == &lit || two_configs) add(vs, hs, ds, ps, *targets, world_depth);
        // Half-resolution water reflection (trace_water): the liquid input
        // pass, then the march and the filter over full-screen triangles.
        if (!low && configured_boolean(config, "water_reflections", true)) {
            add(vs, hs, ds, native_liquid_ssr_input_ps_.Get(), WarmTargets{{kWaterSsrFormats[0], kWaterSsrFormats[1]}, 2},
                kWaterSsrDepthFormat, nullptr, water_ssr_depth_state_.Get());
            add(native_shadow_mask_vs_.Get(), nullptr, nullptr, native_liquid_ssr_march_ps_.Get(),
                WarmTargets{{kWaterSsrFormats[2], kWaterSsrFormats[3]}, 2}, DXGI_FORMAT_UNKNOWN, nullptr, nullptr, shadow_mask_raster_.Get());
            add(native_shadow_mask_vs_.Get(), nullptr, nullptr, native_liquid_ssr_filter_ps_.Get(),
                WarmTargets{{kWaterSsrFormats[4], kWaterSsrFormats[5]}, 2}, DXGI_FORMAT_UNKNOWN, nullptr, nullptr, shadow_mask_raster_.Get());
        }
    }

    // Game particles (draw_game_particles): the scene, and with bloom the
    // glow target beside it; the reflection depth draw (draw_game_particle_depth).
    if (SUCCEEDED(ensure_game_particle_pipeline())) {
        add(game_particle_vs_.Get(), nullptr, nullptr, game_particle_ps_.Get(), WarmTargets{{scene_format}, 1}, world_depth);
        if (configured_integer(config, "bloom", 0) > 0)
            add(game_particle_vs_.Get(), nullptr, nullptr, game_particle_ps_.Get(), WarmTargets{{scene_format, kGlowFormat}, 2}, world_depth);
        if (configured_boolean(config, "water_reflections", true) && configured_integer(config, "water_reflection_quality", 2) >= 1 &&
            ensure_particle_depth_pipeline())
            add(game_particle_vs_.Get(), nullptr, nullptr, game_particle_depth_ps_.Get(), WarmTargets{{kParticleDepthFormat}, 1},
                world_depth, particle_depth_blend_.Get(), particle_depth_test_.Get());
    }

    // Point-shadow faces (atlas D16, overlay D32): the cell clear, then the
    // casters with and without the alpha-tested pixel stage.
    if (configured_boolean(config, "point_shadows", true) && native_shadow_vs_)
        for (const DXGI_FORMAT depth : kPointShadowDepthFormats) {
            add(native_depth_clear_vs_.Get(), nullptr, nullptr, nullptr, WarmTargets{}, depth, nullptr,
                clear_depth_states_[0].Get(), native_point_clear_raster_.Get());
            add(native_shadow_vs_.Get(), nullptr, nullptr, nullptr, WarmTargets{}, depth);
            add(native_shadow_vs_.Get(), nullptr, nullptr, native_shadow_ps_.Get(), WarmTargets{}, depth);
        }
    // The precipitation shelter bake: every caster with the pixel stage into
    // its R8 foliage mask over D32 depth.
    if (configured_integer(config, "precipitation", 3) > 0 && native_shadow_vs_)
        add(native_shadow_vs_.Get(), nullptr, nullptr, native_shadow_ps_.Get(), WarmTargets{{DXGI_FORMAT_R8_UNORM}, 1}, DXGI_FORMAT_D32_FLOAT);

    std::uint64_t signature = 14695981039346656037ull;
    for (const WarmItem& item : items) signature = item_hash(signature, item);
    if (signature != warmup_signature_) { warmup_signature_ = signature; warmup_next_ = 0; }
    warmup_items_ = items.size();
    if (items.empty()) { warmup_done_ = true; return; }

    // 4x4 scratch targets per format, kept for the session.
    const auto target_view = [&](DXGI_FORMAT format) -> ID3D11RenderTargetView* {
        for (const auto& entry : warmup_targets_) if (entry.first == format) return entry.second.Get();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = kWarmSide; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = format; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> view;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture)) ||
            FAILED(device_->CreateRenderTargetView(texture.Get(), nullptr, &view))) return nullptr;
        warmup_targets_.emplace_back(format, view);
        return view.Get();
    };
    const auto depth_view = [&](DXGI_FORMAT format) -> ID3D11DepthStencilView* {
        for (const auto& entry : warmup_depths_) if (entry.first == format) return entry.second.Get();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = kWarmSide; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = format; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11DepthStencilView> view;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture)) ||
            FAILED(device_->CreateDepthStencilView(texture.Get(), nullptr, &view))) return nullptr;
        warmup_depths_.emplace_back(format, view);
        return view.Get();
    };

    // Runs between the frame's last pass and Present. It binds every state it
    // reads and restores nothing: the gamma pass and the next frame's passes
    // bind their own. It leaves the tessellation stages null (the legacy and
    // interface draws never set them) and its scratch targets unbound.
    context_->IASetInputLayout(nullptr);
    context_->GSSetShader(nullptr, nullptr, 0);
    const D3D11_VIEWPORT tiny{0, 0, float(kWarmSide), float(kWarmSide), 0, 1};
    context_->RSSetViewports(1, &tiny);
    const auto begin = std::chrono::steady_clock::now();
    while (warmup_next_ < items.size()) {
        const WarmItem& item = items[warmup_next_++];
        ID3D11RenderTargetView* views[3]{};
        bool ready = true;
        for (UINT n = 0; n < item.target_count; ++n)
            if (item.targets[n] != DXGI_FORMAT_UNKNOWN && !(views[n] = target_view(item.targets[n]))) ready = false;
        ID3D11DepthStencilView* const depth_target = item.depth != DXGI_FORMAT_UNKNOWN ? depth_view(item.depth) : nullptr;
        if (!ready || (item.depth != DXGI_FORMAT_UNKNOWN && !depth_target)) continue;
        context_->OMSetRenderTargets(item.target_count, item.target_count ? views : nullptr, depth_target);
        context_->IASetPrimitiveTopology(item.hs ? D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(item.vs, nullptr, 0);
        context_->HSSetShader(item.hs, nullptr, 0);
        context_->DSSetShader(item.ds, nullptr, 0);
        context_->PSSetShader(item.ps, nullptr, 0);
        context_->OMSetBlendState(item.blend, nullptr, 0xffffffffu);
        context_->OMSetDepthStencilState(item.depth_state, 0);
        context_->RSSetState(item.raster);
        context_->Draw(3, 0);
        ++warmup_draws_;
        if (std::chrono::steady_clock::now() - begin >= kWarmSliceTime) break;
    }

    context_->HSSetShader(nullptr, nullptr, 0); context_->DSSetShader(nullptr, nullptr, 0);
    context_->OMSetRenderTargets(0, nullptr, nullptr);

    // Every draw issued: the hold waits until the GPU has run them.
    if (warmup_next_ >= items.size()) {
        if (!warmup_query_) {
            D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
            if (FAILED(device_->CreateQuery(&desc, &warmup_query_))) { warmup_done_ = true; return; }
        }
        context_->End(warmup_query_.Get());
        warmup_query_pending_ = true;
    }
} catch (...) {
    warmup_done_ = true; // never hold a load on the warm-up's own failure
}

}  // namespace owned
