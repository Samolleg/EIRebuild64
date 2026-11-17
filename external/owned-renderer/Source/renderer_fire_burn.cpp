// Fire burn (option fire_burn, issue: fire spells burn the ground and plants).
//
// The game's fireball explosions, fire walls and ground lightning strikes
// (RetainedFireSpell, read each
// world tick from the local server's active spell list) start renderer-only
// burns. Each burn is stamped once into the area's burn mask (t72, one texel
// per map metre, nativeBurnMask in native_shader.h):
//  - r/g: the ground burn (soft disk or wall strip) and the game second the
//    fire front reaches each texel (the front runs out from a fireball's
//    centre across its radius in kFrontSeconds); negative r marks inert
//    terrain, whose attached vegetation can still receive the heat;
//  - a/b: whole tree crowns. A tree ignites only when the burn reaches past
//    kTreeMargin into its kTreeFootprint (its trunk disk); its whole crown
//    disk is stamped, the fire spreading through it from the side the burn
//    came from at kCrownSpread metres per game second.
// Grass and plants test their root against the mask (the whole tuft or
// none), terrain and crowns read it per pixel (an ignited root also reaches
// the authored crown beyond the stamped disk); the snow trails melt the
// burning ground. Everything is on the game clock (frozen while paused,
// scaled by game speed) and resets when the area changes.
// Option off: no spell read, no burn mask and no burn shader instructions
// (the untwinned programs).
#include "renderer.h"
#include "engine_bridge.h"
#include "retained_static_scene.h"
#include "owned_asset_loader.h"
#include "ambient_terrain_grids.h"
#include "weather_wind.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace owned {
#ifndef EID3D11_SHADER_EMBED
namespace {
constexpr float kFrontSeconds = 1.2f;    // a fireball's fire front crosses its radius
constexpr float kGroundSeconds = 6.f;    // the ground and plants burn this long once reached
constexpr float kCrownSeconds = 8.f;     // a part of a crown burns this long once reached
constexpr float kTreeFootprint = 2.5f;   // trunk disk a burn must reach into (metres)
constexpr float kTreeMargin = 1.f;       // how far past the footprint's edge it must reach
constexpr float kCrownRadius = 3.5f;     // the crown disk an ignited tree burns over
constexpr float kCrownSpread = 1.2f;     // metres per game second through a crown
constexpr float kWallHalfWidth = 1.f;    // a fire wall's burnt strip, each side (soft to 1.6x)
constexpr float kLeafDarkness = .85f, kLeafLoss = 1.f;

// These shipped atlases have only plant wood/foliage labels (the authored
// burn_ maps). MOBs also place the same tree models as generic family 54
// decorations, so their material is authoritative for this cosmetic rule.
bool plant_atlas(const std::string& name) {
    for (const char* atlas : {"tree02", "tree03", "tree04", "tree28", "tree29", "deathtree", "bush03", "bushset01", "bushset02"})
        if (name == atlas || name == std::string(atlas) + ".mmp") return true;
    return false;
}

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3 - 2 * t);
}
// Distance from p to segment a-b and the position along it (0..1).
float segment_distance(float px, float py, float ax, float ay, float bx, float by, float& along) {
    const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    along = l2 > 0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l2, 0.f, 1.f) : 0.f;
    return std::hypot(px - (ax + along * dx), py - (ay + along * dy));
}
} // namespace

void Renderer::set_fire_spells(const std::vector<RetainedFireSpell>& spells, double world_time) noexcept {
    fire_spells_.clear();
    (void)world_time;
    if (!configured_boolean(debug_overlay_.config(), "fire_burn", false)) return;
    try {
        for (const auto& s : spells)
            if ((s.kind == 1 || s.kind == 2 || s.kind == 3) && std::isfinite(s.radius) && s.radius > 0 && s.radius < 100 &&
                std::isfinite(s.center[0]) && std::isfinite(s.center[1]) && std::isfinite(s.wall[0]) && std::isfinite(s.wall[1]))
                fire_spells_.push_back({s.kind, s.center[0], s.center[1], s.radius, s.wall[0], s.wall[1]});
    } catch (...) { fire_spells_.clear(); }
}

void Renderer::reset_fire_burns() noexcept {
    if (fire_mask12_) { native_shadow_valid_.fill(false); native_shadow_static_valid_.fill(false); }
    fire_shadow_clock_ = fire_shadow_until_ = -1;
    fire_burns_.clear(); fire_trees_.clear(); fire_mask_.clear(); fire_ground_types_.clear();
    fire_map_.reset(); fire_terrain_.reset();
    fire_mask_w_ = fire_mask_h_ = 0; fire_dirty_y0_ = UINT_MAX; fire_dirty_y1_ = 0;
    fire_mask12_.reset();
    fire_material_table12_.reset(); fire_material_table_.clear();
    for (auto& item : material_profiles_) { item.second.burn12.reset(); item.second.burn_tried = false; }
    std::fill(std::begin(fire_burn_values_), std::end(fire_burn_values_), 0.f);
    fire_burn_bound_ = false;
}

bool Renderer::update_fire_burns(double seconds) noexcept try {
    std::fill(std::begin(fire_burn_values_), std::end(fire_burn_values_), 0.f);
    fire_burn_bound_ = false;
    // D3D12 only: the burn programs are twins in the host's DXIL table.
    if (!d3d12_ || !configured_boolean(debug_overlay_.config(), "fire_burn", false)) {
        if (!fire_burns_.empty() || fire_mask12_) reset_fire_burns();
        return true;
    }
    if (fire_burns_.empty() && fire_spells_.empty()) return true;
    // The area: a new terrain (another map, a reload) starts over.
    AmbientWorldSource area;
    if (!bridge_ambient_world_source(area) || !area.terrain) return true;
    if (area.terrain != fire_terrain_) {
        reset_fire_burns();
        fire_terrain_ = area.terrain; fire_map_ = area.map;
        if (fire_map_)
            for (const auto& object : fire_map_->objects)
                // MOB B003 family 53 is the tree family (also used by
                // neighbour_maps.cpp). The ambient litter census excludes
                // palms and replacement spruces; it is not a tree census.
                if (object.family == 53 || plant_atlas(object.primary_texture))
                    fire_trees_.push_back({object.position[0], object.position[1], -1});
    }
    if (!std::isfinite(seconds) || seconds < 0) return true;
    // New burns: a spell matches a burn of its kind at the same place and size
    // seen within the last two game seconds (the same explosion or wall a
    // tick on); anything else starts a burn now.
    std::vector<std::size_t> started;
    for (const auto& s : fire_spells_) {
        bool known = false;
        for (auto& b : fire_burns_)
            if (b.kind == s.kind && std::hypot(b.x - s.x, b.y - s.y) < .5f && std::fabs(b.radius - s.radius) < .05f &&
                std::hypot(b.wx - s.wx, b.wy - s.wy) < .5f && seconds - b.last < 2.0) { b.last = seconds; known = true; break; }
        if (known) continue;
        FireBurn b; b.kind = s.kind; b.x = s.x; b.y = s.y; b.radius = s.radius; b.wx = s.wx; b.wy = s.wy;
        b.start = b.last = seconds;
        started.push_back(fire_burns_.size()); fire_burns_.push_back(b);
    }
    if (fire_burns_.empty()) return true;
    // The mask: one texel per map metre, made at the area's first burn.
    if (fire_mask_.empty()) {
        fire_mask_w_ = fire_terrain_->sector_count_x * 32u; fire_mask_h_ = fire_terrain_->sector_count_y * 32u;
        if (!fire_mask_w_ || !fire_mask_h_ || fire_mask_w_ > 4096 || fire_mask_h_ > 4096) { reset_fire_burns(); return false; }
        // One more row: this frame's values (native_shader.h nativeBurnRow).
        fire_mask_.assign(std::size_t(fire_mask_w_) * (fire_mask_h_ + 1u), {0, 0, 0, 0});
        fire_ground_types_ = ambient::terrain_base_types(ambient::terrain_base_slots(*fire_terrain_), fire_terrain_->tile_types, {});
        fire_dirty_y0_ = 0; fire_dirty_y1_ = fire_mask_h_;
    }
    const auto stamp_rows = [&](float y0, float y1) {
        const UINT a = UINT(std::clamp(std::floor(y0), 0.f, float(fire_mask_h_))), b = UINT(std::clamp(std::ceil(y1) + 1, 0.f, float(fire_mask_h_)));
        fire_dirty_y0_ = std::min(fire_dirty_y0_, a); fire_dirty_y1_ = std::max(fire_dirty_y1_, b);
    };
    // Visits each texel centre within [x0,x1]x[y0,y1] (map metres).
    const auto each = [&](float x0, float y0, float x1, float y1, auto&& fn) {
        const int ax = std::max(0, int(std::floor(x0))), bx = std::min(int(fire_mask_w_) - 1, int(std::ceil(x1)));
        const int ay = std::max(0, int(std::floor(y0))), by = std::min(int(fire_mask_h_) - 1, int(std::ceil(y1)));
        for (int y = ay; y <= by; ++y) for (int x = ax; x <= bx; ++x) fn(fire_mask_[std::size_t(y) * fire_mask_w_ + x], x + .5f, y + .5f);
        stamp_rows(float(ay), float(by));
    };
    const auto earliest = [](float& slot, float t) { slot = slot == 0 ? t : std::min(slot, t); };
    // Negative r means heat over inert ground. Plants on rock still receive
    // the heat (absolute r); the rock itself never chars or emits ground fire.
    const auto ground = [&](std::array<float,4>& m, float px, float py, float intensity) {
        const auto type = fire_ground_types_[std::size_t(UINT(py) / 2) * (fire_mask_w_ / 2) + UINT(px) / 2];
        const bool soil = type == ambient::tt_grass || type == ambient::tt_ground || type == ambient::tt_field ||
            type == ambient::tt_drygrass || type == ambient::tt_snow || type == ambient::tt_snowballs || type == ambient::tt_swamp;
        m[0] = (soil ? 1.f : -1.f) * std::max(std::fabs(m[0]), intensity);
    };
    for (const std::size_t index : started) {
        const FireBurn& b = fire_burns_[index];
        const float t0 = float(b.start);
        fire_shadow_until_ = std::max(fire_shadow_until_, b.start + 1.05 * kFrontSeconds + kGroundSeconds);
        // Ground: a fireball's disk (the front from the centre) or a wall's
        // strip (alight at once). Times reach two texels past the soft edge
        // so the bilinear read inside the burn stays on valid times.
        if (b.kind != 2) {
            const float r = b.radius, reach = 1.05f * r + 2;
            each(b.x - reach, b.y - reach, b.x + reach, b.y + reach, [&](std::array<float,4>& m, float px, float py) {
                const float d = std::hypot(px - b.x, py - b.y);
                if (d > reach) return;
                ground(m, px, py, 1 - smoothstep(.7f, 1.05f, d / r));
                earliest(m[1], t0 + kFrontSeconds * std::min(d / r, 1.05f));
            });
        } else {
            const float ax = b.x - b.wx, ay = b.y - b.wy, bx2 = b.x + b.wx, by2 = b.y + b.wy, reach = 1.6f * kWallHalfWidth + 2;
            each(std::min(ax, bx2) - reach, std::min(ay, by2) - reach, std::max(ax, bx2) + reach, std::max(ay, by2) + reach,
                [&](std::array<float,4>& m, float px, float py) {
                    float along = 0; const float d = segment_distance(px, py, ax, ay, bx2, by2, along);
                    if (d > reach) return;
                    ground(m, px, py, 1 - smoothstep(.6f * kWallHalfWidth, 1.6f * kWallHalfWidth, d));
                    earliest(m[1], t0);
                });
        }
        // Trees the burn reaches past the margin into their footprint: the
        // whole crown disk, spreading from the footprint point nearest the burn.
        for (auto& tree : fire_trees_) {
            if (tree.ignite >= 0) continue;
            float reach_into = 0, nx = 0, ny = 0, front = 0;
            if (b.kind != 2) {
                const float d = std::hypot(tree.x - b.x, tree.y - b.y);
                reach_into = b.radius + kTreeFootprint - d;
                const float k = d > 1e-3f ? std::max(d - kTreeFootprint, 0.f) / d : 0.f;
                nx = b.x + (tree.x - b.x) * k; ny = b.y + (tree.y - b.y) * k;
                if (d <= kTreeFootprint) { nx = tree.x; ny = tree.y; }
                front = kFrontSeconds * std::min(std::max(d - kTreeFootprint, 0.f) / b.radius, 1.f);
            } else {
                float along = 0;
                const float d = segment_distance(tree.x, tree.y, b.x - b.wx, b.y - b.wy, b.x + b.wx, b.y + b.wy, along);
                reach_into = kWallHalfWidth + kTreeFootprint - d;
                const float px = b.x - b.wx + 2 * b.wx * along, py = b.y - b.wy + 2 * b.wy * along;
                const float k = d > 1e-3f ? std::max(d - kTreeFootprint, 0.f) / d : 0.f;
                nx = px + (tree.x - px) * k; ny = py + (tree.y - py) * k;
            }
            if (reach_into < kTreeMargin) continue;
            tree.ignite = b.start + front + .5;
            const float ti = float(tree.ignite), reach = kCrownRadius + 1;
            // Root delay + bounded whole-crown spread + final leaf loss.
            fire_shadow_until_ = std::max(fire_shadow_until_, tree.ignite +
                (reach + kTreeFootprint) / kCrownSpread + 2 * kCrownSeconds);
            each(tree.x - reach, tree.y - reach, tree.x + reach, tree.y + reach, [&](std::array<float,4>& m, float px, float py) {
                const float d = std::hypot(px - tree.x, py - tree.y);
                if (d > reach) return;
                m[3] = std::max(m[3], 1 - smoothstep(.85f, 1.1f, d / kCrownRadius));
                earliest(m[2], ti + std::hypot(px - nx, py - ny) / kCrownSpread);
            });
        }
    }
    // This frame's values in the last row (nativeBurnRow): now, texels per
    // metre, a crown's burning seconds; leaf darkness, leaf loss, the ground's
    // burning seconds, active.
    auto* row = fire_mask_.data() + std::size_t(fire_mask_h_) * fire_mask_w_;
    row[0] = {float(seconds), 1.f, kCrownSeconds,
        configured_boolean(debug_overlay_.config(), "fire_burn_structures", false) ? 1.f : 0.f};
    row[1] = {kLeafDarkness, kLeafLoss, kGroundSeconds, 1.f};
    const UINT pitch = fire_mask_w_ * UINT(sizeof(fire_mask_[0]));
    if (!fire_mask12_ && FAILED(d3d12_->create_texture(fire_mask_w_, fire_mask_h_ + 1u, DXGI_FORMAT_R32G32B32A32_FLOAT, fire_mask12_))) {
        bridge_report_event("fire_burn_unavailable", "mask texture"); reset_fire_burns(); return false;
    }
    // The changed rows (the first burn: the whole mask), then the values row.
    if (fire_dirty_y0_ < fire_dirty_y1_) {
        const RECT rows{0, LONG(fire_dirty_y0_), LONG(fire_mask_w_), LONG(fire_dirty_y1_)};
        if (FAILED(d3d12_->write_texture(*fire_mask12_, rows, fire_mask_.data() + std::size_t(fire_dirty_y0_) * fire_mask_w_, pitch))) { reset_fire_burns(); return false; }
        fire_dirty_y0_ = UINT_MAX; fire_dirty_y1_ = 0;
    }
    if (FAILED(d3d12_->write_texture(*fire_mask12_, RECT{0, LONG(fire_mask_h_), 2, LONG(fire_mask_h_) + 1}, row, pitch))) { reset_fire_burns(); return false; }
    fire_burn_bound_ = true;
    // Static shadow depth contains alpha coverage. Invalidate it on changed
    // game ticks while coverage evolves, including the final settled tick.
    // A paused or fully charred scene keeps its normal cached shadow path.
    if (seconds != fire_shadow_clock_ && fire_shadow_clock_ <= fire_shadow_until_) {
        native_shadow_valid_.fill(false); native_shadow_static_valid_.fill(false);
    }
    fire_shadow_clock_ = seconds;
    fire_burn_values_[0] = float(seconds); fire_burn_values_[1] = 1.f; fire_burn_values_[2] = kCrownSeconds;
    fire_burn_values_[4] = kLeafDarkness; fire_burn_values_[5] = kLeafLoss; fire_burn_values_[6] = kGroundSeconds; fire_burn_values_[7] = 1.f;
    return true;
} catch (...) { reset_fire_burns(); return false; }


#endif

// The burn twins (D3D12 only, the host's DXIL table): each world program that
// draws burnable surfaces again with NATIVE_FIRE_BURN, so the programs drawn
// without a burn stay exactly the builds without the feature. Twinned: the
// native build, vertex/tessellation and material/depth/shadow programs (with
// their Ray Reconstruction guide twins and the traced sun prepass), and
// ground cover's vertex/pixel programs. Not twinned: the debug check, astral,
// unlit and testing programs and the profiled ones (NATIVE_PROFILED=1 draws
// the twin of its unprofiled program while a burn exists).
namespace {
bool burn_entry(const char* entry) noexcept {
    for (const char* e : {"nativePS", "nativePrepassPS", "nativeShadowPS", "nativeVS", "nativePrepassVS", "nativeShadowVS",
        "nativeMotionWorldVS", "nativeBuildCS", "nativeTessVS", "nativeTerrainHS", "nativeTerrainHSPlain",
        "nativeTerrainDS", "nativeTerrainDSPlain", "nativePrepassTessVS", "nativePrepassHS", "nativePrepassSnowHS",
        "nativePrepassDS", "nativePrepassSnowDS", "gcPS", "gcVS", "gcDepthPS", "gcDepthVS", "gcShadowVS"})
        if (std::strcmp(entry, e) == 0) return true;
    return false;
}
bool burn_twinned(const ShaderProgram& program) noexcept {
    if (!program.entry || !burn_entry(program.entry)) return false;
    for (const D3D_SHADER_MACRO* d = program.defines; d && d->Name; ++d) {
        for (const char* excluded : {"NATIVE_UNLIT", "NATIVE_DOMAIN_CHECK", "EID3D11_TESTING", "NATIVE_ASTRAL", "NATIVE_FIRE_BURN"})
            if (std::strcmp(d->Name, excluded) == 0) return false;
        if (std::strcmp(d->Name, "NATIVE_PROFILED") == 0 && d->Definition && std::strcmp(d->Definition, "1") == 0) return false;
    }
    return true;
}
// A program's identity for the twin lookup: source, entry, profile and its
// defines with values (sorted); `unprofiled` reads NATIVE_PROFILED=1 as 0.
std::string burn_key(const ShaderProgram& program, bool unprofiled) {
    std::vector<std::string> defines;
    for (const D3D_SHADER_MACRO* d = program.defines; d && d->Name; ++d) {
        if (std::strcmp(d->Name, "NATIVE_FIRE_BURN") == 0) continue;
        std::string value = d->Definition ? d->Definition : "";
        if (unprofiled && std::strcmp(d->Name, "NATIVE_PROFILED") == 0) value = "0";
        defines.push_back(std::string(d->Name) + "=" + value);
    }
    std::sort(defines.begin(), defines.end());
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(program.source.data())) + ":" +
        std::to_string(program.source.size()) + "|" + program.entry + "|" + program.profile;
    for (const auto& d : defines) key += "|" + d;
    return key;
}
// Every program a twin is made from, once.
const std::vector<ShaderProgram>& burn_twin_programs() {
    static const auto built = [] {
        struct Built { std::vector<ShaderProgram> programs; std::vector<std::vector<D3D_SHADER_MACRO>> macros; };
        Built b; std::vector<ShaderProgram> sources;
        append_native_shader_programs(sources); append_native_off_material_programs(sources); append_native_motion_programs(sources);
        append_native_indexed_programs(sources); append_native_slim_lean_programs(sources); append_native_guide_programs(sources);
        append_ground_cover_shader_programs(sources); append_ground_cover_guide_programs(sources);
        { std::vector<ShaderProgram> rt; append_raytracing_shader_programs(rt);
          for (const auto& program : rt) if (std::strcmp(program.entry, "nativePrepassPS") == 0) sources.push_back(program); }
        std::vector<std::string> seen;
        for (const auto& program : sources) {
            if (!burn_twinned(program)) continue;
            std::string key = burn_key(program, false);
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(std::move(key));
            std::vector<D3D_SHADER_MACRO> macros;
            for (const D3D_SHADER_MACRO* d = program.defines; d && d->Name; ++d) macros.push_back(*d);
            macros.push_back({"NATIVE_FIRE_BURN", "1"}); macros.push_back({nullptr, nullptr});
            b.macros.push_back(std::move(macros)); b.programs.push_back(program);
        }
        for (std::size_t n = 0; n < b.programs.size(); ++n) b.programs[n].defines = b.macros[n].data();
        return b;
    }();
    return built.programs;
}
} // namespace

void append_fire_burn_programs(std::vector<ShaderProgram>& out) {
    const auto& programs = burn_twin_programs();
    out.insert(out.end(), programs.begin(), programs.end());
}

// The map the D3D12 frame draws twins by (set_program_substitutes): every
// program of the host's world lists with a twin, rebuilt when a list grew.
#ifndef EID3D11_SHADER_EMBED
void Renderer::update_fire_burn_substitutes() noexcept try {
    if (!d3d12_) return;
    // Option off: no twins at all. On: the twins warm from area load on and
    // are drawn while the area has a burn.
    if (!configured_boolean(debug_overlay_.config(), "fire_burn", false)) { d3d12_->set_program_substitutes(nullptr, false); return; }
    const std::vector<ShaderProgram>* lists[]{&native12_.programs, &native12_.guides, &gc12_.programs, &gc12_.guides, &rt12_.programs, &ambient12_.ambient};
    std::size_t total = 0;
    for (const auto* list : lists) total += list->size();
    if (total != fire_burn_programs_seen_) {
        fire_burn_programs_seen_ = total; fire_burn_substitutes_.clear();
        std::unordered_map<std::string, const ShaderProgram*> twins;
        for (const auto& twin : burn_twin_programs()) twins.emplace(burn_key(twin, false), &twin);
        for (const auto* list : lists)
            for (const auto& program : *list) {
                if (!program.entry || !burn_entry(program.entry)) continue;
                bool skip = false;
                for (const D3D_SHADER_MACRO* d = program.defines; d && d->Name && !skip; ++d)
                    skip = std::strcmp(d->Name, "NATIVE_FIRE_BURN") == 0;
                if (skip) continue;
                const auto found = twins.find(burn_key(program, true));
                if (found != twins.end()) fire_burn_substitutes_.emplace(&program, found->second);
            }
        // The ambient particles' burn twins (append_ambient_particle_shader_programs 4 and 5).
        if (ambient12_.ambient.size() >= 6) {
            fire_burn_substitutes_[&ambient12_.ambient[0]] = &ambient12_.ambient[4];
            fire_burn_substitutes_[&ambient12_.ambient[1]] = &ambient12_.ambient[5];
        }
    }
    d3d12_->set_program_substitutes(&fire_burn_substitutes_, fire_burn_bound_);
} catch (...) { fire_burn_substitutes_.clear(); fire_burn_programs_seen_ = 0; if (d3d12_) d3d12_->set_program_substitutes(nullptr, false); }

#endif
} // namespace owned
