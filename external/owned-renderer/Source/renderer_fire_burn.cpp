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
#include "ambient_particles.h"
#include "game_particles.h"
#include "material_library.h"
#include "owned_resource_runtime.h"
#include "owned_resource_store.h"
#include <algorithm>
#include <future>
#include <chrono>
#include <thread>
#include <cctype>
#include <cfloat>
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
constexpr float kWallHalfWidth = .5f;    // a wall's line, each side (soft to 1.6x): the game's own wall danger strip
// A fire wall's ground burn (char, snow melt, cover) spans its flames, about
// 1.2-1.5 m each side (the snow trail melt's 1.2 m), not the danger strip:
// a 0.5 m strip on the 1 m mask rasterised to ragged blobs, the snowfall
// standing between them (user, cand-1128: the snow did not clear under the
// fire wall). Trees still catch from the danger strip.
constexpr float kWallBurnHalfWidth = 1.4f;
constexpr float kLeafDarkness = .85f;
constexpr float kStrikeRadius = 2.5f;    // a lightning bolt's burn at its ground point
constexpr float kCrownOverlap = .5f;     // how far a crown must reach over the scorched ground

// These shipped atlases have only plant wood/foliage labels (the authored
// burn_ maps). MOBs also place the same tree models as generic family 54
// decorations, so their material is authoritative for this cosmetic rule.
bool plant_atlas(const std::string& name) {
    for (const char* atlas : {"tree02", "tree03", "tree04", "tree28", "tree29", "deathtree", "bush03", "bushset01", "bushset02"})
        if (name == atlas || name == std::string(atlas) + ".mmp") return true;
    return false;
}

// A tree model's crown over its root (metres, model z up): the horizontal
// half extent and height span of its crown parts (every part when it names
// none), from each part's vertices at its LNK/BON place. Morph endpoints
// (8 BON/FIG shapes) are averaged: a placement's shape moves them slightly.
struct TreeCrown { bool valid = false, stump = false, crowned = false; float radius = 0, low = 0, high = 0, cx = 0, cy = 0; };
TreeCrown tree_crown(ResourceStore& store, const std::string& name) {
    TreeCrown out;
    ResourceFailure failure; ResourceBlob mod, bon; ModelCpuAsset model;
    if (!store.resolve(name + ".mod", mod, failure) || !store.resolve(name + ".bon", bon, failure) ||
        !decode_model_resources(mod, bon, name, model, failure)) return out;
    std::vector<std::array<float,3>> place(model.nodes.size());
    float lo[2][3], hi[2][3];
    bool any[2]{};
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& node = model.nodes[i];
        std::array<float,3> at{};
        const std::size_t ends = node.bone_positions.size() / 3;
        for (std::size_t e = 0; e < ends; ++e) for (int k = 0; k < 3; ++k) at[k] += node.bone_positions[e * 3 + k] / float(ends);
        const auto parent = i < model.links.size() ? model.links[i].parent_index : UINT_MAX;
        if (parent < i) for (int k = 0; k < 3; ++k) at[k] += place[parent][k];
        place[i] = at;
        // The part's own vertices (shape-averaged, as the drawn rest shape):
        // the FIG header boxes do not bound the mesh (the palm nafltr73's say
        // +-0.9 m for a crown drawn 2.8-5.2 m up), and a crown part may carry
        // its height in its vertices rather than a bone offset.
        const auto& fig = node.figure;
        if (!node.transform_available || !fig.shape_count || !fig.position_groups || fig.components.empty()) continue;
        float mn[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, mx[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (const auto& c : fig.components) {
            const std::uint32_t group = c.position / 4u, lane = c.position % 4u;
            if (group >= fig.position_groups) continue;
            float v[3]{};
            for (std::uint32_t sh = 0; sh < fig.shape_count; ++sh) {
                const std::size_t g = std::size_t(sh) * fig.position_groups + group;
                if (g >= fig.positions.size()) break;
                v[0] += fig.positions[g].x[lane] / float(fig.shape_count);
                v[1] += fig.positions[g].y[lane] / float(fig.shape_count);
                v[2] += fig.positions[g].z[lane] / float(fig.shape_count);
            }
            for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], v[k]); mx[k] = std::max(mx[k], v[k]); }
        }
        if (mn[0] > mx[0]) continue;
        std::string lower = node.name;
        for (auto& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        const int set = lower.find("crown") != std::string::npos ? 0 : 1;
        for (int k = 0; k < 3; ++k) {
            const float a = at[k] + mn[k], b = at[k] + mx[k];
            if (!std::isfinite(a) || !std::isfinite(b)) return out;
            lo[set][k] = any[set] ? std::min(lo[set][k], a) : a;
            hi[set][k] = any[set] ? std::max(hi[set][k], b) : b;
        }
        any[set] = true;
    }
    if (!any[0] && !any[1]) return out;
    if (!any[0]) for (int k = 0; k < 3; ++k) { lo[0][k] = lo[1][k]; hi[0][k] = hi[1][k]; }
    out.radius = std::max({std::fabs(lo[0][0]), std::fabs(hi[0][0]), std::fabs(lo[0][1]), std::fabs(hi[0][1])});
    out.low = std::max(lo[0][2], 0.f); out.high = hi[0][2];
    out.cx = .5f * (lo[0][0] + hi[0][0]); out.cy = .5f * (lo[0][1] + hi[0][1]);   // the crown's centre over the root (a palm leans)
    out.valid = out.radius > .1f && out.radius < 15 && out.high > out.low && out.high < 60;
    // A stump (no crown part, under 1.5 m, as the critter perches judge it)
    // glows and chars without the game's fire, like grass.
    out.stump = !any[0] && out.high < 1.5f;
    out.crowned = any[0];   // a named crown part (cacti have none)
    return out;
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
            if (s.kind >= 1 && s.kind <= 4 && std::isfinite(s.radius) && s.radius > 0 && s.radius < 100 &&
                std::isfinite(s.center[0]) && std::isfinite(s.center[1]) && std::isfinite(s.wall[0]) && std::isfinite(s.wall[1]))
                fire_spells_.push_back({s.kind, s.center[0], s.center[1], s.radius, s.wall[0], s.wall[1]});
    } catch (...) { fire_spells_.clear(); }
}

// A lightning bolt (CLightning: weather and the spell; engine_bridge.cpp
// Draw3DLine slot 16, world points) ignites where it reaches the ground:
// the first point along it, from either end, at or below 1.5 m over the
// terrain there (the rain's strike, InvokeLightning, runs its bolt to 5 m
// under the ground; the spell's ends at its target). Forks that end in the
// air do not. The scene thread draws ribbons and updates burns, so the
// list needs no lock.
void Renderer::note_fire_strike(const GameRibbonView& view) noexcept try {
    if (!d3d12_ || view.interface_pass || !view.points || view.count < 2 || fire_strikes_.size() >= 16 ||
        !configured_boolean(debug_overlay_.config(), "fire_burn", false)) return;
    AmbientWorldSource area;
    if (!bridge_ambient_world_source(area) || !area.terrain) return;
    // Walk from the higher end down the bolt.
    const bool forward = view.points[0].z >= view.points[view.count - 1].z;
    const GameRibbonPoint* hit = nullptr;
    float ground = 0, top = -1e9f;
    for (std::uint32_t k = 0; k < view.count; ++k) {
        const auto& p = view.points[forward ? k : view.count - 1 - k];
        top = std::max(top, p.z);
        float h = 0;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !sample_terrain_height(*area.terrain, p.x, p.y, h)) continue;
        if (p.z <= h + 1.5f) { hit = &p; ground = h; break; }
    }
    // Diagnostic (#254): the first bolts of a session, accepted or not.
    static unsigned logged = 0;
    static float last[2]{1e9f, 1e9f};
    const auto& end = view.points[forward ? view.count - 1 : 0];
    if (logged < 40 && std::hypot(end.x - last[0], end.y - last[1]) > 3.f) {
        ++logged; last[0] = end.x; last[1] = end.y;
        char line[200];
        std::snprintf(line, sizeof(line), "bolt %u points, top %.1f, end %.1f,%.1f,%.1f: %s %.1f,%.1f ground %.1f", view.count, top,
            end.x, end.y, end.z, hit ? "strike" : "in the air", hit ? hit->x : 0.f, hit ? hit->y : 0.f, ground);
        bridge_report_event("fire_burn_bolt", line);
    }
    // A bolt that runs along the ground (the lightning wall's, 1 m over the
    // surface end to end) strikes nothing: its spell burns what it touches
    // (kind 4). A strike comes down from the sky.
    if (hit && top >= ground + 4.f) fire_strikes_.push_back({hit->x, hit->y});
} catch (...) {}

void Renderer::reset_fire_burns() noexcept {
    if (fire_mask12_) { native_shadow_valid_.fill(false); native_shadow_static_valid_.fill(false); }
    fire_shadow_clock_ = fire_shadow_until_ = fire_shadow_refresh_ = -1;
    if (!fire_shadow_ids_.empty()) { fire_shadow_ids_.clear(); ++fire_shadow_revision_; }
    if (!fire_rt_gone_ids_.empty()) { fire_rt_gone_ids_.clear(); ++fire_rt_gone_revision_; }
    fire_burns_.clear(); fire_trees_.clear(); fire_objects_.clear(); fire_spread_clock_ = -1; fire_mask_.clear(); fire_melt_.clear(); fire_melt_active_.clear(); fire_campfire_seen_.clear(); fire_burn_cells_.clear(); fire_burn_cells_w_ = 0; fire_ground_types_.clear();
    fire_strikes_.clear(); fire_tree_textures_.clear(); fire_flames_.clear(); fire_segments_ = -1;
    fire_map_.reset(); fire_terrain_.reset();
    fire_census_job_ = {}; fire_census_ready_ = false; ++fire_label_generation_;
    fire_mask_w_ = fire_mask_h_ = 0; fire_dirty_y0_ = UINT_MAX; fire_dirty_y1_ = 0; fire_melt_dirty_.clear(); fire_melt_all_ = false;
    fire_mask12_.reset();
    fire_material_table12_.reset(); fire_material_table_.clear();
    for (auto& item : material_profiles_) { item.second.burn12.reset(); item.second.burn_tried = false; }
    std::fill(std::begin(fire_burn_values_), std::end(fire_burn_values_), 0.f);
    fire_burn_bound_ = false;
}

// The area's census, off the render thread (update_fire_burns): every tree
// (its model's crown), every burn-class structure and the textures the tree
// models label, and the game fire's texture (tendrismoke.mmp). Reads only
// immutable snapshots and the material library's fixed burn classes.
std::shared_ptr<Renderer::FireCensus> Renderer::build_fire_census(std::shared_ptr<const TerrainCpuAsset> terrain,
    std::shared_ptr<const OwnedMapSnapshot> map, std::shared_ptr<const MaterialLibrary> library, bool structures) noexcept try {
    auto out = std::make_shared<FireCensus>();
    if (!terrain) return out;
    {
        std::size_t by_family = 0, by_atlas = 0, by_model = 0, shaped = 0, stumps = 0;
        // Each tree model's crown, read once for the area.
        std::unordered_map<std::string, TreeCrown> crowns;
        ResourceStoreConfig mounts; ResourceStore store; ResourceFailure why;
        const bool readable = owned_resource_runtime_mounts(mounts) && store.open(mounts, why);
        // burn.json's "burn" structures (fences, huts, tents, carts): they
        // catch whole when a burn touches them and pass the fire on to the
        // ones touching them (fire_burn_structures).
        structures = structures && library;
        if (map)
            for (const auto& object : map->objects) {
                // MOB B003 family 53 is the tree family (also used by
                // neighbour_maps.cpp). The ambient litter census excludes
                // palms and replacement spruces; it is not a tree census.
                // Tree models (ambient::tree_kind) placed as decorations too.
                const auto kind = ambient::tree_kind(object.visual_reference.c_str());
                if (object.family != 53 && !plant_atlas(object.primary_texture) && kind == ambient::tree_none) {
                    if (!structures || !readable || object.visual_reference.empty() || object.primary_texture.empty()) continue;
                    std::string key = object.primary_texture;
                    for (auto& ch : key) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".mmp") == 0) key.resize(key.size() - 4);
                    if (library->fire_burn_class(key) != 3) continue;
                    auto found = crowns.find(object.visual_reference);
                    if (found == crowns.end()) found = crowns.emplace(object.visual_reference, tree_crown(store, object.visual_reference)).first;
                    if (!found->second.valid) continue;
                    FireObject o; o.x = object.position[0]; o.y = object.position[1];
                    if (!sample_terrain_height(*terrain, o.x, o.y, o.z)) o.z = object.position[2];
                    o.radius = found->second.radius; o.high = found->second.high;
                    out->objects.push_back(o);
                    continue;
                }
                // The tree family also places rocks, walls and fences (stone03,
                // strongfence00): a texture burn.json classes is not a tree, so it
                // never catches as one (no crown fire, no flames on a rock).
                if (library && !object.primary_texture.empty()) {
                    std::string key = object.primary_texture;
                    for (auto& ch : key) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".mmp") == 0) key.resize(key.size() - 4);
                    if (library->fire_burn_class(key) != 255) continue;
                }
                (object.family == 53 ? by_family : plant_atlas(object.primary_texture) ? by_atlas : by_model)++;
                FireTree tree; tree.x = object.position[0]; tree.y = object.position[1];
                if (object.nid && object.nid <= 0x7fffffffu) tree.id = std::int32_t(object.nid);   // its shadow caster (active statics)
                if (!sample_terrain_height(*terrain, tree.x, tree.y, tree.z)) tree.z = object.position[2];
                if (readable && !object.visual_reference.empty()) {
                    auto found = crowns.find(object.visual_reference);
                    if (found == crowns.end()) found = crowns.emplace(object.visual_reference, tree_crown(store, object.visual_reference)).first;
                    if (found->second.valid) {
                        tree.crown = found->second.radius; tree.low = found->second.low; tree.high = found->second.high; tree.stump = found->second.stump; tree.crowned = found->second.crowned; ++shaped; stumps += tree.stump;
                        // The crown's centre turned as the object is placed
                        // (its quaternion w,x,y,z, retained_object_matrix's
                        // rows): the fire sits on the crown, not on a ring round
                        // the root (user, cand-1128: tree fires floated).
                        const auto& q = object.quaternion;
                        const float w = q[0], x = q[1], y = q[2], z = q[3], cx = found->second.cx, cy = found->second.cy;
                        tree.cx = (1 - 2 * (y * y + z * z)) * cx + 2 * (x * y - w * z) * cy;
                        tree.cy = 2 * (x * y + w * z) * cx + (1 - 2 * (x * x + z * z)) * cy;
                        if (!std::isfinite(tree.cx) || !std::isfinite(tree.cy)) tree.cx = tree.cy = 0;
                    }
                }
                out->trees.push_back(tree);
                // Every tree's model labels its texture (crown parts foliage;
                // bare trees' crowns wood): family 53 holds models outside
                // the tree_kind list (the Gipat spruces samspr*, samt56*).
                if (!object.primary_texture.empty() && !object.visual_reference.empty()) {
                    std::string key = object.primary_texture;
                    for (auto& ch : key) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".mmp") == 0) key.resize(key.size() - 4);
                    auto& models = out->tree_textures[key];
                    const std::pair<std::string,int> entry{object.visual_reference, kind == ambient::tree_bare ? 1 : 2};
                    if (std::find(models.begin(), models.end(), entry) == models.end()) models.push_back(entry);
                }
            }
        char line[200];
        std::snprintf(line, sizeof(line), "area trees %zu (family 53 %zu, plant atlas %zu, tree model %zu), tree textures %zu, trees with a read crown %zu (stumps %zu), burning structures %zu, models %zu",
            out->trees.size(), by_family, by_atlas, by_model, out->tree_textures.size(), shaped, stumps, out->objects.size(), crowns.size());
        out->line = line;
    }
    // The game fire's texture: its first mip as a surface image.
    {
        const char* why = nullptr;
        ResourceStoreConfig mounts; ResourceStore store; ResourceFailure failure; ResourceBlob blob; TextureCpuAsset asset;
        if (!owned_resource_runtime_mounts(mounts) || !store.open(mounts, failure) || !store.resolve("tendrismoke.mmp", blob, failure) ||
            !parse_texture_asset(blob, asset, failure) || asset.mips.empty() || !asset.source) why = "tendrismoke.mmp unreadable";
        auto image = std::make_shared<SurfaceImage>();
        if (!why) {
            DDPIXELFORMAT& f = image->format;
            f.dwSize = sizeof(f);
            const auto& mip = asset.mips[0];
            image->width = mip.width; image->height = mip.height;
            const auto rgb = [&](DWORD bits, DWORD r, DWORD g, DWORD b, DWORD a) {
                f.dwFlags = DDPF_RGB | (a ? DDPF_ALPHAPIXELS : 0); f.dwRGBBitCount = bits;
                f.dwRBitMask = r; f.dwGBitMask = g; f.dwBBitMask = b; f.dwRGBAlphaBitMask = a;
                image->pitch = LONG(mip.width * bits / 8);
            };
            const auto dxt = [&](DWORD code, UINT block) {
                f.dwFlags = DDPF_FOURCC; f.dwFourCC = code; image->pitch = LONG(std::max(1u, (mip.width + 3) / 4) * block);
            };
            switch (asset.format) {
            case MmpFormat::dxt1: dxt(MAKEFOURCC('D','X','T','1'), 8); break;
            case MmpFormat::dxt3: dxt(MAKEFOURCC('D','X','T','3'), 16); break;
            case MmpFormat::dxt5: dxt(MAKEFOURCC('D','X','T','5'), 16); break;
            case MmpFormat::rgb565: rgb(16, 0xf800, 0x07e0, 0x001f, 0); break;
            case MmpFormat::argb1555: rgb(16, 0x7c00, 0x03e0, 0x001f, 0x8000); break;
            case MmpFormat::argb4444: rgb(16, 0x0f00, 0x00f0, 0x000f, 0xf000); break;
            case MmpFormat::argb8888: rgb(32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000); break;
            default: why = "tendrismoke.mmp format unsupported"; break;
            }
            if (!why && std::size_t(mip.byte_offset) + mip.bytes > asset.source->size()) why = "tendrismoke.mmp truncated";
            if (!why) {
                image->pixels = std::vector<std::uint8_t>(asset.source->begin() + mip.byte_offset, asset.source->begin() + mip.byte_offset + mip.bytes);
                image->id = 0xf1e0000000000001ull; image->static_mip_hint = true;
            }
        }
        if (why) out->flame_failure = why; else out->flame = std::move(image);
    }
    return out;
} catch (...) { return nullptr; }

bool Renderer::update_fire_burns(double seconds) noexcept try {
    std::fill(std::begin(fire_burn_values_), std::end(fire_burn_values_), 0.f);
    fire_burn_bound_ = false;
    // D3D12 only: the burn programs are twins in the host's DXIL table.
    if (!d3d12_ || !configured_boolean(debug_overlay_.config(), "fire_burn", false)) {
        if (!fire_burns_.empty() || fire_mask12_) reset_fire_burns();
        return true;
    }
    // The area: a new terrain (another map, a reload) starts over; its census
    // runs on a thread from now (fire_burn is on), before any spell.
    AmbientWorldSource area;
    if (!bridge_ambient_world_source(area) || !area.terrain) return true;
    // The census needs the material library (tree/structure/rock classes):
    // wait until prepare_material_profiles has tried to load it, also at material Off.
    if (area.terrain != fire_terrain_ && !material_library_tried_) return true;
    if (area.terrain != fire_terrain_) {
        reset_fire_burns();
        fire_terrain_ = area.terrain; fire_map_ = area.map;
        const bool structures = configured_boolean(debug_overlay_.config(), "fire_burn_structures", true);
        std::promise<std::shared_ptr<FireCensus>> promise;
        fire_census_job_ = promise.get_future().share();
        try {
            std::thread([promise = std::move(promise), terrain = fire_terrain_, map = fire_map_,
                         library = std::shared_ptr<const MaterialLibrary>(material_library_), structures]() mutable {
                promise.set_value(build_fire_census(terrain, map, library, structures));
            }).detach();
        } catch (...) { fire_census_job_ = {}; }
    }
    // Its result, once ready; a burn that comes before it waits for it.
    if (!fire_census_ready_ && fire_census_job_.valid()) {
        const bool needed = !fire_burns_.empty() || !fire_spells_.empty() || !fire_strikes_.empty();
        if (!needed && fire_census_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return true;
        const auto census = fire_census_job_.get();
        fire_census_job_ = {}; fire_census_ready_ = true;
        if (census) {
            fire_trees_ = std::move(census->trees); fire_objects_ = std::move(census->objects);
            fire_tree_textures_ = std::move(census->tree_textures);
            if (census->flame) fire_flame_texture_ = std::move(census->flame);
            else { fire_flame_texture_failed_ = true; bridge_report_event("fire_burn_flames_unavailable", census->flame_failure.c_str()); }
            bridge_report_event("fire_burn", census->line.c_str());
        }
    }
    // Campfires melt the weather snow round them while it lies (a standing
    // melt, below), with or without any burn (user, cand-1128: weather built
    // snow should also melt round campfires).
    bool campfire_melt = false;
    if (snow_cover_ > .02f)
        for (const auto& c : fire_campfire_seen_) campfire_melt = campfire_melt || double(native_frame_index_) - c[2] < 120.0;
    if (fire_burns_.empty() && fire_spells_.empty() && fire_strikes_.empty() && !campfire_melt) return true;
    if (!std::isfinite(seconds) || seconds < 0) return true;
    // New burns: a spell matches a burn of its kind at the same place and size
    // seen within the last two game seconds (the same explosion or wall a
    // tick on); anything else starts a burn now.
    std::vector<std::size_t> started;
    // Lightning bolts at the ground: one burn per strike place (a bolt is
    // redrawn, and re-forked, every frame of its life).
    for (const auto& p : fire_strikes_) {
        bool known = false;
        for (auto& b : fire_burns_)
            if (b.kind == 3 && std::hypot(b.x - p[0], b.y - p[1]) < 3.f && seconds - b.last < 2.0) { b.last = seconds; known = true; break; }
        if (known) continue;
        FireBurn b; b.kind = 3; b.x = p[0]; b.y = p[1]; b.radius = kStrikeRadius; b.start = b.last = seconds;
        started.push_back(fire_burns_.size()); fire_burns_.push_back(b);
    }
    fire_strikes_.clear();
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
    if (fire_burns_.empty() && !campfire_melt) return true;
    // The mask: one texel per map metre, made at the area's first burn.
    if (fire_mask_.empty()) {
        fire_mask_w_ = fire_terrain_->sector_count_x * 32u; fire_mask_h_ = fire_terrain_->sector_count_y * 32u;
        if (!fire_mask_w_ || !fire_mask_h_ || fire_mask_w_ > 4096 || fire_mask_h_ > 4096) { reset_fire_burns(); return false; }
        // One more row: this frame's values (native_shader.h nativeBurnRow).
        fire_mask_.assign(std::size_t(fire_mask_w_) * (fire_mask_h_ + 1u), {0, 0, 0, 0});
        fire_melt_.assign(std::size_t(fire_mask_w_) * fire_mask_h_, {0.f, 0.f, 0.f});
        fire_burn_cells_w_ = (fire_mask_w_ + 15) / 16;
        fire_burn_cells_.assign(std::size_t(fire_burn_cells_w_) * ((fire_mask_h_ + 15) / 16), 0);
        fire_ground_types_ = ambient::terrain_base_types(ambient::terrain_base_slots(*fire_terrain_), fire_terrain_->tile_types, {});
        fire_dirty_y0_ = 0; fire_dirty_y1_ = fire_mask_h_;
    }
    const auto stamp_rows = [&](float y0, float y1) {
        const UINT a = UINT(std::clamp(std::floor(y0), 0.f, float(fire_mask_h_))), b = UINT(std::clamp(std::ceil(y1) + 1, 0.f, float(fire_mask_h_)));
        fire_dirty_y0_ = std::min(fire_dirty_y0_, a); fire_dirty_y1_ = std::max(fire_dirty_y1_, b);
    };
    // Visits each texel centre within [x0,x1]x[y0,y1] (map metres); a burn's
    // visit marks its 16 m cells (marking false: a campfire's melt).
    bool marking = true;
    const auto each = [&](float x0, float y0, float x1, float y1, auto&& fn) {
        const int ax = std::max(0, int(std::floor(x0))), bx = std::min(int(fire_mask_w_) - 1, int(std::ceil(x1)));
        const int ay = std::max(0, int(std::floor(y0))), by = std::min(int(fire_mask_h_) - 1, int(std::ceil(y1)));
        for (int y = ay; y <= by; ++y) for (int x = ax; x <= bx; ++x) fn(fire_mask_[std::size_t(y) * fire_mask_w_ + x], x + .5f, y + .5f);
        stamp_rows(float(ay), float(by));
        if (marking && ax <= bx && ay <= by)
            for (int cy = ay / 16; cy <= by / 16; ++cy) for (int cx = ax / 16; cx <= bx / 16; ++cx)
                fire_burn_cells_[std::size_t(cy) * fire_burn_cells_w_ + std::size_t(cx)] = 1;
    };
    const auto earliest = [](float& slot, float t) { slot = slot == 0 ? t : std::min(slot, t); };
    // The snow's melt time (fire_melt_, the mask texture's right half): the
    // latest fire that heats the ground at a texel, so every new fire melts
    // the snow there again over its own 1.5 s, on roofs and ground alike;
    // only where it heats (no neighbours) (user, cand-1128: a recast after the
    // snow refilled melted instantly).
    const auto melt_time = [&](float px, float py, float t, float heat) {
        if (heat <= .05f) return;
        // Its time and its own heat: the snow melts by the fire that melted it,
        // never by another burn's heat over it (user, cand-1128: a lightning
        // wall cut square holes in the snow next to an older fire wall).
        const std::size_t index = std::size_t(py) * fire_mask_w_ + std::size_t(px);
        auto& slot = fire_melt_[index];
        if (slot[0] <= 0) { slot = {t, heat, 0.f}; fire_melt_active_.push_back(std::uint32_t(index)); return; }
        if (t <= slot[0]) { slot[1] = std::max(slot[1], heat); return; }
        // A new fire continues the melt from where the snow is now (in over
        // 1.5 s, refilling after the ground's burning seconds, as
        // native_shader.h nativeBurnSnowMelt): its time is dated back by the
        // ramp the current melt has already covered, so the snow never pops
        // back to full before melting again (user, cand-1128: spamming fire
        // walls reset the snow to max, then melted it).
        const float age = t - slot[0], ramp = smoothstep(0.f, 1.5f, age);
        const float current = slot[1] * ramp * float(std::exp(-std::max(age - kGroundSeconds, 0.f) / 90.0));
        const float h = std::max(heat, current);
        const float f = std::clamp(current / h, 0.f, 1.f);
        slot[0] = t - 1.5f * (.5f - std::sin(std::asin(1 - 2 * f) / 3));   // smoothstep's inverse
        slot[1] = h;
    };
    // The ground fire's time (g): the earliest front, but a new fire over
    // ground whose fire is out burns it again - its embers and the snow's
    // refill restart (user, cand-1128: fire walls cast again on the same
    // spot burned on snow that had already come back). Dated 60% into its
    // burning seconds so the char, already full, never lightens. Only where
    // this fire heats the ground: its times reach past its soft edge, and
    // restarting them there melted the snow again on old burns beside it
    // (user, cand-1128: a wall melted unrelated areas).
    const auto ground_time = [](float& slot, float t, float heat) {
        slot = slot == 0 ? t : heat > .05f && t > slot + kGroundSeconds ? std::max(slot, t - .6f * kGroundSeconds) : std::min(slot, t);
    };
    // The crown channel (a): crown burn 0..1, plus 2 where a tall tree's
    // crown burns (crown top 3 m or more, or unread): a plant on sand takes
    // the sand's light share of a burn, a tall tree there (a desert palm)
    // burns fully (user, #254).
    const auto crown_burn = [](float& slot, float v, bool tall) {
        const bool was_tall = slot > 1.5f;
        const float current = was_tall ? slot - 2.f : slot;
        slot = std::max(current, v) + (was_tall || tall ? 2.f : 0.f);
    };
    // Negative r means heat over inert ground. Plants on rock still receive
    // the heat (absolute r); the rock itself never chars or emits ground fire.
    const auto ground = [&](std::array<float,4>& m, float px, float py, float intensity) {
        const auto type = fire_ground_types_[std::size_t(UINT(py) / 2) * (fire_mask_w_ / 2) + UINT(px) / 2];
        // Snow-painted ground (snow, snowballs) chars as soil: the fire melts
        // its snow and shows the charred ground under it; as inert ground the
        // char stopped dead at every 2 m tile border between a road and snow,
        // straight edges and steps (user F11 542 of launch 093322554). Its
        // weather snow refills over it.
        const bool soil = type == ambient::tt_grass || type == ambient::tt_ground || type == ambient::tt_field ||
            type == ambient::tt_drygrass || type == ambient::tt_swamp || type == ambient::tt_snow || type == ambient::tt_snowballs;
        // Sand takes a light scorch (2 + heat: native_shader.h reads it at 0.35
        // of the char, no ash). Rock and ice never char; their plants still
        // burn (negative heat).
        const bool sand = type == ambient::tt_sand;
        const float heat = std::max(m[0] > 1.5f ? m[0] - 2.f : std::fabs(m[0]), intensity);
        m[0] = soil ? heat : sand ? 2.f + heat : -heat;
    };
    // The game's fire on an ignited tree (several sources up its trunk and
    // crown, each lit when the crown fire reaches it) or bush (one source).
    // A source's particles rise about 2.4 m (8 segments of 0.3 m).
    const auto add_tree_flames = [&](const FireTree& tree, float nx, float ny, double spread) {
        std::uint32_t seed = std::uint32_t(std::lround(tree.x * 16)) * 0x9e3779b1u ^ std::uint32_t(std::lround(tree.y * 16)) * 0x85ebca77u;
        // The game fire's scale from the tree's size (its crown radius; a
        // 1.5 m crown is the game's own size), so a big tree burns bigger,
        // shared out over its sources (the trunk's foot and up to 5 in the
        // crown), as a structure's (user, cand-1128: tree fires looked full
        // scaled at every source).
        // The highest source sits its flame's rise (about 1.8 m at the 0.75
        // scale) under the top, so the fire ends at the tree's top instead of
        // standing over it (user F11 361: a fire floated above a bare tree).
        const float low = tree.crown > 0 ? std::max(tree.low, 1.f) : 2.f, high = tree.crown > 0 ? std::max(tree.high - 2.f, low) : 4.f;
        const int count = std::clamp(int((high - low) / 2.5f) + 1, 1, 5);
        const bool bush = tree.crown > 0 && tree.high < 3.f;
        // Divided by the count of its sources (not its root): each of a
        // tree's several fires is a small one (user, cand-1128: tree fires
        // still too big).
        const float scale = std::clamp((tree.crown > 0 ? tree.crown : 1.5f) / 1.5f, .7f, 2.2f) / (bush ? 1.f : float(1 + count));
        // A leafy crown's fire goes out as its leaves are gone (they are
        // discarded at about 3.5-4 s of the crown's burn): burning on, it hung
        // in the air where the crown had been (c5 shots, palm at 6 s). A bare
        // crown (no named part: its wood stays, charred) burns its full time.
        const double crown_fire = tree.crowned ? 3.5 : kCrownSeconds;
        const auto add = [&](float x, float y, float z, double start, double end) {
            if (fire_flames_.size() >= 256) return;
            seed = seed * 1664525u + 1013904223u;
            FireFlame f; f.x = x; f.y = y; f.z = z; f.scale = scale; f.start = start; f.end = end; f.seed = seed | 1u;
            fire_flames_.push_back(std::move(f));
        };
        const double t = tree.ignite;
        if (tree.stump) return;   // stumps glow and char, no flames
        if (bush) {   // a bush: one source in it
            add(tree.x + tree.cx, tree.y + tree.cy, tree.z + std::max(.15f, tree.low), t, t + spread + crown_fire);
            return;
        }
        add(tree.x, tree.y, tree.z + .15f, t, t + kCrownSeconds);   // the trunk's foot
        const float crown = tree.crown > 0 ? tree.crown : 1.5f;
        for (int i = 0; i < count; ++i) {
            const float along = count > 1 ? float(i) / float(count - 1) : 0.f;
            const float angle = 2.39996f * float(i) + float(seed & 1023u) * .00614f;
            // Round the crown's centre (a palm's leans off its root), a quarter
            // of its radius out, narrowing upwards (conifers).
            // A bare tree (no named crown part: its branches are its crown)
            // keeps its fire at the trunk, its twigs being too thin to carry
            // it (user F11 180: a fire beside a bare tree's trunk in the air).
            const float out = crown * (tree.crowned ? .15f : .05f) * (1 - .6f * along);   // close in on the crown's core (user: better, still not quite attached)
            const float x = tree.x + tree.cx + out * std::cos(angle), y = tree.y + tree.cy + out * std::sin(angle);
            const double start = t + (std::hypot(x - nx, y - ny) + (low + (high - low) * along) * .5f) / kCrownSpread;
            add(x, y, tree.z + low + (high - low) * along, start, start + crown_fire);
        }
    };
    // A burn-class structure catches whole: its footprint disk in the crown
    // channel (the structure char reads it at every height), the fire
    // spreading over it from the point it caught, and the game's fire on it.
    const auto ignite_object = [&](FireObject& o, double at, float nx, float ny) {
        o.ignite = at;
        const float r = std::max(o.radius, .3f), reach = r + 1, ti = float(at);
        each(o.x - reach, o.y - reach, o.x + reach, o.y + reach, [&](std::array<float,4>& m, float px, float py) {
            const float d = std::hypot(px - o.x, py - o.y);
            if (d > reach) return;
            crown_burn(m[3], 1 - smoothstep(.85f, 1.1f, d / r), false);
            earliest(m[2], ti + std::hypot(px - nx, py - ny) / kCrownSpread);
        });
        // The game's fire on it, scaled by its footprint, and more sources
        // round a big structure (one per ~2 m of its radius, up to 5).
        const float scale = std::clamp(r / 1.2f, .8f, 2.5f);
        const int sources = std::clamp(int(r / 2.f) + 1, 1, 5);
        std::uint32_t seed = (std::uint32_t(std::lround(o.x * 16)) * 0x9e3779b1u ^ std::uint32_t(std::lround(o.y * 16)) * 0x85ebca77u) | 1u;
        for (int k = 0; k < sources && fire_flames_.size() < 256; ++k) {
            const float angle = 2.39996f * float(k), out = k ? r * .55f : 0.f;
            FireFlame f; f.x = o.x + out * std::cos(angle); f.y = o.y + out * std::sin(angle);
            f.z = o.z + std::clamp(o.high * .4f, .2f, 1.5f); f.scale = scale / std::sqrt(float(sources));
            f.start = at + out / kCrownSpread; f.end = f.start + r / kCrownSpread + kCrownSeconds;
            seed = seed * 1664525u + 1013904223u; f.seed = seed | 1u;
            fire_flames_.push_back(std::move(f));
        }
    };
    for (const std::size_t index : started) {
        const FireBurn& b = fire_burns_[index];
        const float t0 = float(b.start);
        fire_shadow_until_ = std::max(fire_shadow_until_, b.start + 1.05 * kFrontSeconds + kGroundSeconds);
        // Ground: a fireball's disk (the front from the centre) or a wall's
        // strip (alight at once). Times reach two texels past the soft edge
        // so the bilinear read inside the burn stays on valid times.
        const bool wall = b.kind == 2 || b.kind == 4;
        if (!wall) {
            const float r = b.radius, reach = 1.05f * r + 2;
            each(b.x - reach, b.y - reach, b.x + reach, b.y + reach, [&](std::array<float,4>& m, float px, float py) {
                const float d = std::hypot(px - b.x, py - b.y);
                if (d > reach) return;
                const float heat = 1 - smoothstep(.7f, 1.05f, d / r);
                ground(m, px, py, heat);
                ground_time(m[1], t0 + kFrontSeconds * std::min(d / r, 1.05f), heat);
                melt_time(px, py, t0 + kFrontSeconds * std::min(d / r, 1.05f), heat);
            });
        } else {
            const float ax = b.x - b.wx, ay = b.y - b.wy, bx2 = b.x + b.wx, by2 = b.y + b.wy, reach = 1.6f * kWallBurnHalfWidth + 2;
            each(std::min(ax, bx2) - reach, std::min(ay, by2) - reach, std::max(ax, bx2) + reach, std::max(ay, by2) + reach,
                [&](std::array<float,4>& m, float px, float py) {
                    float along = 0; const float d = segment_distance(px, py, ax, ay, bx2, by2, along);
                    if (d > reach) return;
                    const float hw = b.kind == 2 ? kWallBurnHalfWidth : kWallHalfWidth;   // the lightning wall keeps its line
                    const float heat = 1 - smoothstep(.6f * hw, 1.6f * hw, d);
                    // The lightning wall runs 1 m over the ground: the grass
                    // and moss it touches burn (negative heat: plants only),
                    // the ground itself does not char.
                    if (b.kind == 4) { if (m[0] <= 0) m[0] = -std::max(-m[0], heat); }
                    else ground(m, px, py, heat);
                    ground_time(m[1], t0, heat);
                    if (b.kind != 4) melt_time(px, py, t0, heat);
                });
        }
        // Trees the burn reaches past the margin into their footprint: the
        // whole crown disk, spreading from the footprint point nearest the burn.
        for (auto& tree : fire_trees_) {
            if (tree.ignite >= 0) continue;
            // Anything whose crown overlaps the scorched ground catches (the
            // model's own crown; unread models keep the trunk footprint rule).
            const float crown = tree.crown > 0 ? tree.crown : kCrownRadius;
            const float extent = tree.crown > 0 ? std::max(tree.crown, kTreeFootprint * .4f) : kTreeFootprint;
            const float need = tree.crown > 0 ? kCrownOverlap : kTreeMargin;
            float reach_into = 0, nx = 0, ny = 0, front = 0;
            if (!wall) {
                const float d = std::hypot(tree.x - b.x, tree.y - b.y);
                reach_into = 1.05f * b.radius + extent - d;
                const float k = d > 1e-3f ? std::max(d - extent, 0.f) / d : 0.f;
                nx = b.x + (tree.x - b.x) * k; ny = b.y + (tree.y - b.y) * k;
                if (d <= extent) { nx = tree.x; ny = tree.y; }
                front = kFrontSeconds * std::min(std::max(d - extent, 0.f) / b.radius, 1.f);
            } else {
                float along = 0;
                const float d = segment_distance(tree.x, tree.y, b.x - b.wx, b.y - b.wy, b.x + b.wx, b.y + b.wy, along);
                // A wall lights what its line passes through: the crown (most
                // of its radius) or, unread, the trunk's footprint, plus the
                // wall's own half width. Not everything around it.
                const float touch = (tree.crown > 0 ? .85f * tree.crown : .5f * kTreeFootprint) + kWallHalfWidth;
                reach_into = touch - d + need;
                const float px = b.x - b.wx + 2 * b.wx * along, py = b.y - b.wy + 2 * b.wy * along;
                const float k = d > 1e-3f ? std::max(d - extent, 0.f) / d : 0.f;
                nx = px + (tree.x - px) * k; ny = py + (tree.y - py) * k;
            }
            if (reach_into < need) continue;
            tree.ignite = b.start + front + .5;
            // The crown disk round the crown's own centre (a crown sits off its
            // root) and to 1.2 of its radius before it fades: its outer
            // needles stay inside it, where the traced shadows read the burn
            // (user, cand-1128: burnt firs kept shadows of their outer bits).
            const float ti = float(tree.ignite), reach = 1.3f * crown + 1, ccx = tree.x + tree.cx, ccy = tree.y + tree.cy;
            const double spread = (crown + std::hypot(nx - tree.x, ny - tree.y)) / kCrownSpread;
            // Root delay + bounded whole-crown spread + final leaf loss.
            tree.settle = tree.ignite + spread + 2 * kCrownSeconds;
            fire_shadow_until_ = std::max(fire_shadow_until_, tree.settle);
            each(ccx - reach, ccy - reach, ccx + reach, ccy + reach, [&](std::array<float,4>& m, float px, float py) {
                const float d = std::hypot(px - ccx, py - ccy);
                if (d > reach) return;
                // Every tree takes the full burn. Tall (crown channel 2 +
                // burn): palms and spruces lose their leaves and their trunks
                // char, cacti (no crown part) char. Not tall: a bush (a crowned
                // plant under 3 m) burns down to near nothing, everywhere
                // (native_shader.h nativeVertex; user, cand-1128: bushes need
                // to burn down to near nothing, not char, even on sand).
                crown_burn(m[3], 1 - smoothstep(1.2f, 1.3f, d / crown), !(tree.crown > 0 && tree.crowned && tree.high < 3.f));
                earliest(m[2], ti + std::hypot(px - nx, py - ny) / kCrownSpread);
            });
            add_tree_flames(tree, nx, ny, float(spread));
            // The grass and moss around a burning tree's foot catch from it:
            // a small disk (a bush's smaller) spreading out from the trunk.
            // Stumps carry no fire of their own and light nothing.
            if (!tree.stump) {
                const float rg = tree.crown > 0 && tree.high < 3.f ? 1.f : 1.75f, rr = rg + 1;
                each(tree.x - rr, tree.y - rr, tree.x + rr, tree.y + rr, [&](std::array<float,4>& m, float px, float py) {
                    const float d = std::hypot(px - tree.x, py - tree.y);
                    if (d > rr) return;
                    ground(m, px, py, 1 - smoothstep(.6f, 1.f, d / rg));
                    earliest(m[1], ti + d / kCrownSpread);
                    melt_time(px, py, ti + d / kCrownSpread, 1 - smoothstep(.6f, 1.f, d / rg));
                });
            }
        }
        // Burn-class structures the burn touches: a fireball's disk reaching
        // their footprint, or a wall's line passing through it.
        for (auto& o : fire_objects_) {
            if (o.ignite >= 0) continue;
            float nx = o.x, ny = o.y; double at = b.start + .3;
            if (!wall) {
                const float d = std::hypot(o.x - b.x, o.y - b.y);
                if (d - o.radius > 1.05f * b.radius) continue;
                const float k = d > 1e-3f ? std::max(d - o.radius, 0.f) / d : 0.f;
                nx = b.x + (o.x - b.x) * k; ny = b.y + (o.y - b.y) * k;
                at += kFrontSeconds * std::min(std::max(d - o.radius, 0.f) / b.radius, 1.f);
            } else {
                float along = 0;
                const float d = segment_distance(o.x, o.y, b.x - b.wx, b.y - b.wy, b.x + b.wx, b.y + b.wy, along);
                if (d > .85f * o.radius + kWallHalfWidth) continue;
                nx = b.x - b.wx + 2 * b.wx * along; ny = b.y - b.wy + 2 * b.wy * along;
            }
            ignite_object(o, at, nx, ny);
        }
    }
    // The fire passes from a burning structure to the burn-class structures
    // touching it (a fence's next segment), a second after it caught plus the
    // gap at the crown spread, on the game clock (none while paused), at most
    // 16 new ones a game tick.
    if (seconds > fire_spread_clock_ && !fire_objects_.empty()) {
        fire_spread_clock_ = seconds;
        unsigned lit = 0;
        for (std::size_t a = 0; a < fire_objects_.size() && lit < 16; ++a) {
            const FireObject s = fire_objects_[a];
            if (s.ignite < 0 || s.ignite > seconds || seconds > s.ignite + kCrownSeconds) continue;
            for (auto& o : fire_objects_) {
                if (o.ignite >= 0) continue;
                const float d = std::hypot(o.x - s.x, o.y - s.y), gap = d - s.radius - o.radius;
                if (gap > .5f) continue;
                const float k = d > 1e-3f ? std::min(s.radius / d, 1.f) : 0.f;
                ignite_object(o, std::max(seconds, s.ignite + 1.0 + std::max(gap, 0.f) / kCrownSpread),
                    s.x + (o.x - s.x) * k, s.y + (o.y - s.y) * k);
                if (++lit >= 16) break;
            }
        }
    }
    // A burning campfire keeps the weather snow melted within 2.4 m (full to
    // 1.2 m, soft beyond), refreshed while it burns; once it is out the snow
    // falls back on the refill law. No heat, no char: only the melt.
    marking = false;
    if (campfire_melt)
        for (const auto& c : fire_campfire_seen_) {
            if (double(native_frame_index_) - c[2] >= 120.0) continue;
            const float cx = float(c[0]), cy = float(c[1]);
            each(cx - 2.4f, cy - 2.4f, cx + 2.4f, cy + 2.4f, [&](std::array<float,4>&, float px, float py) {
                const float heat = 1 - smoothstep(1.2f, 2.4f, std::hypot(px - cx, py - cy));
                if (heat <= .05f) return;
                const std::size_t index = std::size_t(py) * fire_mask_w_ + std::size_t(px);
                auto& slot = fire_melt_[index];
                if (slot[0] <= 0) fire_melt_active_.push_back(std::uint32_t(index));
                slot[1] = slot[0] > 0 && float(seconds) - slot[0] < 6.f ? std::max(slot[1], heat) : heat;
                slot[0] = float(seconds) - 1.5f;
            });
        }
    // Each melting texel's melt resolved now (z): its heat with the burn's
    // ragged edge, in over 1.5 s from its fire, back on the refill law after
    // the ground's burning seconds - so every program's snow code only reads
    // one bilinear value (native_shader.h nativeBurnSnowMelt; size gate:
    // the per-pixel noise and ramp cost ~210 instructions in every pixel
    // variant). A texel leaves the list once its snow is back.
    for (std::size_t k = 0; k < fire_melt_active_.size();) {
        const std::uint32_t index = fire_melt_active_[k];
        auto& m = fire_melt_[index];
        const float age = float(seconds) - m[0];
        const UINT x = index % fire_mask_w_, y = index / fire_mask_w_;
        // Value noise about 1.4 m across, as the burn's own ragged edge.
        const auto hash = [](int i, int j) {
            std::uint32_t h = std::uint32_t(i) * 0x8da6b343u ^ std::uint32_t(j) * 0xd8163841u;
            h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; return float(h & 0xffffu) / 65535.f;
        };
        const float nx = (float(x) + .5f) * .7f, ny = (float(y) + .5f) * .7f;
        const int ix = int(std::floor(nx)), iy = int(std::floor(ny));
        const float fx = smoothstep(0.f, 1.f, nx - float(ix)), fy = smoothstep(0.f, 1.f, ny - float(iy));
        const float noise = (hash(ix, iy) * (1 - fx) + hash(ix + 1, iy) * fx) * (1 - fy) + (hash(ix, iy + 1) * (1 - fx) + hash(ix + 1, iy + 1) * fx) * fy;
        const bool gone = age > kGroundSeconds + 600.f;   // the refill has brought the snow back
        const float v = gone || age < 0 ? 0.f : smoothstep(.25f, .75f, m[1] + (noise - .5f) * .45f) * smoothstep(0.f, 1.5f, age) *
            float(std::exp(-std::max(age - kGroundSeconds, 0.f) / 90.0));
        // In steps of 1/64 (and its ends exactly): the 90 s refill moves a
        // texel by that about every second and a half, so only a handful of
        // texels go up per frame (user, cand-1128: keep the upload cheap).
        if (std::fabs(v - m[2]) > 1.f / 64.f || (v != m[2] && (v == 0 || v >= .999f || m[2] >= .999f)) || gone) {
            m[2] = v;
            fire_melt_dirty_.push_back(index);
        }
        if (gone) { m = {0.f, 0.f, 0.f}; fire_melt_active_[k] = fire_melt_active_.back(); fire_melt_active_.pop_back(); }
        else ++k;
    }
    // This frame's values in the last row (nativeBurnRow): now, texels per
    // metre, a crown's burning seconds; leaf darkness, leaf loss, the ground's
    // burning seconds, active.
    auto* row = fire_mask_.data() + std::size_t(fire_mask_h_) * fire_mask_w_;
    row[0] = {float(seconds), 1.f, kCrownSeconds,
        configured_boolean(debug_overlay_.config(), "fire_burn_structures", true) ? 1.f : 0.f};
    // y: fire_burn_debug_view (0 off, 1 burn labels, 2 burn state).
    row[1] = {kLeafDarkness, float(std::clamp(configured_integer(debug_overlay_.config(), "fire_burn_debug_view", 0), 0, 3)), kGroundSeconds, 1.f};
    const UINT pitch = fire_mask_w_ * UINT(sizeof(fire_mask_[0]));
    // Twice as wide: the mask on the left, the snow's melt times (r) on the
    // right (native_shader.h nativeBurnMeltAge).
    if (!fire_mask12_) {
        if (FAILED(d3d12_->create_texture(fire_mask_w_ * 2u, fire_mask_h_ + 1u, DXGI_FORMAT_R32G32B32A32_FLOAT, fire_mask12_))) {
            bridge_report_event("fire_burn_unavailable", "mask texture"); reset_fire_burns(); return false;
        }
        fire_melt_all_ = true;   // the new texture's melt half, once
    }
    // The changed rows (the first burn: the whole mask), then the values row.
    if (fire_dirty_y0_ < fire_dirty_y1_) {
        const RECT rows{0, LONG(fire_dirty_y0_), LONG(fire_mask_w_), LONG(fire_dirty_y1_)};
        if (FAILED(d3d12_->write_texture(*fire_mask12_, rows, fire_mask_.data() + std::size_t(fire_dirty_y0_) * fire_mask_w_, pitch))) { reset_fire_burns(); return false; }
        fire_dirty_y0_ = UINT_MAX; fire_dirty_y1_ = 0;
    }
    // The melt half: only its changed texels, as runs along their rows, and
    // only the resolved melt (z; the shaders read nothing else there). A new
    // texture takes the whole half once.
    {
        const auto upload = [&](UINT x0, UINT y, UINT w) {
            std::vector<std::array<float,4>> melt(w, {0, 0, 0, 0});
            for (UINT x = 0; x < w; ++x) melt[x][2] = fire_melt_[std::size_t(y) * fire_mask_w_ + x0 + x][2];
            return SUCCEEDED(d3d12_->write_texture(*fire_mask12_, RECT{LONG(fire_mask_w_ + x0), LONG(y), LONG(fire_mask_w_ + x0 + w), LONG(y + 1)},
                                                   melt.data(), w * UINT(sizeof(melt[0]))));
        };
        bool ok = true;
        if (fire_melt_all_) {
            const std::vector<std::array<float,4>> zero(std::size_t(fire_mask_w_) * fire_mask_h_, {0, 0, 0, 0});
            ok = SUCCEEDED(d3d12_->write_texture(*fire_mask12_, RECT{LONG(fire_mask_w_), 0, LONG(fire_mask_w_ * 2u), LONG(fire_mask_h_)},
                                                 zero.data(), fire_mask_w_ * UINT(sizeof(zero[0]))));
            fire_melt_all_ = false;
        }
        // Then this update's changed texels - also on the clearing update,
        // whose melts would otherwise stay cleared (a campfire's melt, steady
        // from its first update, never changes again; user F11 on 22c7c4c5).
        if (ok && !fire_melt_dirty_.empty()) {
            std::sort(fire_melt_dirty_.begin(), fire_melt_dirty_.end());
            fire_melt_dirty_.erase(std::unique(fire_melt_dirty_.begin(), fire_melt_dirty_.end()), fire_melt_dirty_.end());
            for (std::size_t k = 0; ok && k < fire_melt_dirty_.size();) {
                std::size_t e = k + 1;
                while (e < fire_melt_dirty_.size() && fire_melt_dirty_[e] == fire_melt_dirty_[e - 1] + 1 &&
                       fire_melt_dirty_[e] / fire_mask_w_ == fire_melt_dirty_[k] / fire_mask_w_) ++e;
                ok = upload(fire_melt_dirty_[k] % fire_mask_w_, fire_melt_dirty_[k] / fire_mask_w_, UINT(e - k));
                k = e;
            }
        }
        fire_melt_dirty_.clear();
        if (!ok) { reset_fire_burns(); return false; }
    }
    if (FAILED(d3d12_->write_texture(*fire_mask12_, RECT{0, LONG(fire_mask_h_), 2, LONG(fire_mask_h_) + 1}, row, pitch))) { reset_fire_burns(); return false; }
    fire_burn_bound_ = true;
    // Static shadow depth contains alpha coverage. A burning tree leaves the
    // close maps' cached layer and casts there every frame (its records,
    // draw_native_fire_shadows; the far maps keep it leafy until it settles).
    // So the cached layers are redrawn only when a tree catches or settles.
    {
        // Settled trees (their crowns fully charred, leaves gone): the ray
        // tracing scene drops their leaf records (renderer_native_d3d12.cpp).
        std::vector<std::int32_t> gone;
        for (const auto& tree : fire_trees_)
            if (tree.id && tree.ignite >= 0 && tree.settle >= 0 && seconds > tree.settle) gone.push_back(tree.id);
        std::sort(gone.begin(), gone.end());
        gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
        if (gone != fire_rt_gone_ids_) { fire_rt_gone_ids_ = std::move(gone); ++fire_rt_gone_revision_; }
    }
    {
        std::vector<std::int32_t> burning;
        for (const auto& tree : fire_trees_)
            if (tree.id && tree.ignite >= 0 && seconds <= fire_shadow_until_) burning.push_back(tree.id);
        std::sort(burning.begin(), burning.end());
        burning.erase(std::unique(burning.begin(), burning.end()), burning.end());
        if (burning != fire_shadow_ids_) {
            // The set changes when a burn lights trees (from then on, before
            // they catch) and when the last of them settles, not per tree.
            // A tree catching or settling: the close maps' cached layers are
            // drawn once more without (with) it; one that settled is charred
            // in the far maps too.
            ++fire_shadow_revision_;
            fire_shadow_ids_ = std::move(burning);
            for (unsigned slice = 0; slice < 3; ++slice) { native_shadow_valid_[slice] = false; native_shadow_static_valid_[slice] = false; }
            invalidate_fire_shadows(fire_shadow_refresh_, seconds);
            fire_shadow_refresh_ = seconds;
        }
    }
    fire_shadow_clock_ = seconds;
    fire_burn_values_[0] = float(seconds); fire_burn_values_[1] = 1.f; fire_burn_values_[2] = kCrownSeconds;
    fire_burn_values_[4] = kLeafDarkness; fire_burn_values_[5] = row[1][1]; fire_burn_values_[6] = kGroundSeconds; fire_burn_values_[7] = 1.f;
    step_fire_flames(seconds / kWeatherSecondsPerSegment);
    return true;
} catch (...) { reset_fire_burns(); return false; }

bool Renderer::native_draw_burn_xy(const NativeQueuedDraw& draw, float lo[2], float hi[2]) const noexcept {
    const bool zero = draw.bounds_min[0] == 0 && draw.bounds_min[1] == 0 && draw.bounds_min[2] == 0 &&
                      draw.bounds_max[0] == 0 && draw.bounds_max[1] == 0 && draw.bounds_max[2] == 0;
    if (!zero) { lo[0] = draw.bounds_min[0]; lo[1] = draw.bounds_min[1]; hi[0] = draw.bounds_max[0]; hi[1] = draw.bounds_max[1]; return true; }
    if (draw.instance.pose_node == UINT_MAX || draw.pose_slot >= native_pose_slots_.size() || !(draw.geometry_radius > 0)) return false;
    // The root's translation (row-major root_to_world, as renderer_fur.cpp
    // reads it); the geometry radius bounds the posed parts round it.
    const auto& root = native_pose_slots_[draw.pose_slot].value.root_to_world;
    const float r = draw.geometry_radius;
    lo[0] = root[3] - r; lo[1] = root[7] - r; hi[0] = root[3] + r; hi[1] = root[7] + r;
    return std::isfinite(lo[0]) && std::isfinite(lo[1]) && std::isfinite(hi[0]) && std::isfinite(hi[1]);
}

bool Renderer::fire_burn_touches(const float lo[2], const float hi[2]) const noexcept {
    if (fire_burn_cells_.empty() || !(lo[0] <= hi[0]) || !(lo[1] <= hi[1])) return false;
    const int rows = int(fire_burn_cells_.size() / fire_burn_cells_w_);
    // Two metres of margin: a plant or wall whose bounds end just short of a
    // burnt cell still chars at its edge.
    const int x0 = std::max(0, int(std::floor((lo[0] - 2) / 16))), x1 = std::min(int(fire_burn_cells_w_) - 1, int(std::floor((hi[0] + 2) / 16)));
    const int y0 = std::max(0, int(std::floor((lo[1] - 2) / 16))), y1 = std::min(rows - 1, int(std::floor((hi[1] + 2) / 16)));
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
        if (fire_burn_cells_[std::size_t(y) * fire_burn_cells_w_ + std::size_t(x)]) return true;
    return false;
}

bool Renderer::fire_burning_at(float x, float y) const noexcept {
    if (!fire_burn_bound_ || fire_mask_.empty() || !(x >= 0) || !(y >= 0)) return false;
    const UINT tx = UINT(x), ty = UINT(y);
    if (tx >= fire_mask_w_ || ty >= fire_mask_h_) return false;
    const auto& m = fire_mask_[std::size_t(ty) * fire_mask_w_ + tx];
    const double now = fire_burn_values_[0];
    const float heat = m[0] > 1.5f ? m[0] - 2.f : std::fabs(m[0]);
    const bool ground = heat > .3f && m[1] > 0 && now >= m[1] && now <= m[1] + kGroundSeconds;
    const bool crown = m[3] > .3f && m[2] > 0 && now >= m[2] && now <= m[2] + kCrownSeconds;
    return ground || crown;
}

void Renderer::fire_ignite_flame(float x, float y, float z, float scale) noexcept try {
    if (!fire_burn_bound_ || fire_flames_.size() >= 256) return;
    float ground = 0;
    if (fire_terrain_ && sample_terrain_height(*fire_terrain_, x, y, ground)) z += ground;
    FireFlame f; f.x = x; f.y = y; f.z = z; f.scale = scale;
    f.start = fire_burn_values_[0]; f.end = f.start + 2.5;
    f.seed = (std::uint32_t(std::lround(x * 16)) * 0x9e3779b1u ^ std::uint32_t(std::lround(y * 16)) * 0x85ebca77u) | 1u;
    fire_flames_.push_back(std::move(f));
} catch (...) {}

// The sun maps that show trees that settled in (since, seconds] (and trees
// with no caster of their own that burned then), their crown and height
// widened by the reach of their shadows, against each map's camera depth
// band (nativeShadowSlice selects a map by that band).
void Renderer::invalidate_fire_shadows(double since, double seconds) noexcept {
    float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    const auto grow = [&](float x, float y, float z0, float z1, float r) {
        lo[0] = std::min(lo[0], x - r); lo[1] = std::min(lo[1], y - r); lo[2] = std::min(lo[2], z0);
        hi[0] = std::max(hi[0], x + r); hi[1] = std::max(hi[1], y + r); hi[2] = std::max(hi[2], z1);
    };
    for (const auto& tree : fire_trees_) {
        if (tree.ignite < 0 || tree.ignite > seconds || tree.settle < since) continue;
        if (tree.id && (tree.settle > seconds || tree.settle <= since)) continue;
        const float height = tree.high > 0 ? tree.high : 12.f;
        grow(tree.x, tree.y, tree.z, tree.z + height, (tree.crown > 0 ? tree.crown : kCrownRadius) + 2 * height);
    }
    if (lo[0] > hi[0]) return;
    const std::uint8_t bands = fire_shadow_bands(lo, hi);
    for (unsigned slice = 0; slice < 5; ++slice)
        if (bands & (1u << slice)) { native_shadow_valid_[slice] = false; native_shadow_static_valid_[slice] = false; }
}

// The sun maps whose camera depth band holds the box lo..hi.
std::uint8_t Renderer::fire_shadow_bands(const float lo[3], const float hi[3]) const noexcept {
    std::uint8_t bands = 0;
    for (unsigned slice = 0; slice < 5; ++slice) {
        const auto& m = native_shadow_frame_.slices[slice].camera_slice;
        bool front = false, behind = false;
        float zmin = FLT_MAX, zmax = -FLT_MAX;
        for (unsigned corner = 0; corner < 8; ++corner) {
            const float p[4] = {corner & 1 ? hi[0] : lo[0], corner & 2 ? hi[1] : lo[1], corner & 4 ? hi[2] : lo[2], 1};
            const float z = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
            const float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];
            if (!(w > 1e-4f)) { behind = true; continue; }
            front = true; zmin = std::min(zmin, z / w); zmax = std::max(zmax, z / w);
        }
        if (front && (behind || (zmax >= 0 && zmin <= 1))) bands |= std::uint8_t(1u << slice);
    }
    return bands;
}

// The close maps (0-2) that show the burning trees (fire_shadow_ids_): their
// crowns and heights, widened by the reach of their shadows.
std::uint8_t Renderer::fire_shadow_close_bands() const noexcept {
    float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const auto& tree : fire_trees_) {
        if (!tree.id || !std::binary_search(fire_shadow_ids_.begin(), fire_shadow_ids_.end(), tree.id)) continue;
        const float height = tree.high > 0 ? tree.high : 12.f, r = (tree.crown > 0 ? tree.crown : kCrownRadius) + 2 * height;
        lo[0] = std::min(lo[0], tree.x - r); lo[1] = std::min(lo[1], tree.y - r); lo[2] = std::min(lo[2], tree.z);
        hi[0] = std::max(hi[0], tree.x + r); hi[1] = std::max(hi[1], tree.y + r); hi[2] = std::max(hi[2], tree.z + height);
    }
    return lo[0] > hi[0] ? 0 : std::uint8_t(fire_shadow_bands(lo, hi) & 7u);
}

// The game's fire (PTID_FIRE) on what burns: each source runs CParticle's
// campfire law on the game's segments (55 ms of game time, so it holds on
// pause and follows game speed). A segment ages every particle one
// animation phase (a fire particle dies past phase 7), lifts it 0.3 m,
// drifts it by its spawn offset over its first two phases and shrinks it to
// 0.85 a segment; then 6 tries at 88% each make new ones (one at least) of
// size 0.8 (40%: 0.4 with a wider spawn) within 0.1 m of the source, up to
// 0.3 m over it. The wind term the game applies from phase 7 on is left out.
void Renderer::step_fire_flames(double segments) noexcept try {
    fire_segments_ = segments;
    if (fire_flames_.empty() || !std::isfinite(segments)) return;
    const long long now = static_cast<long long>(std::floor(segments));
    for (auto& f : fire_flames_) {
        const long long first = static_cast<long long>(std::floor(f.start / kWeatherSecondsPerSegment));
        if (now < first) continue;
        if (f.tick < 0) f.tick = std::max(first - 1, now - 16);
        if (now - f.tick > 16) f.tick = now - 16;   // a long hitch: the last 16 segments only
        while (f.tick < now) {
            ++f.tick;
            auto& parts = f.parts;
            std::size_t kept = 0;
            for (auto& p : parts) {
                p.px = p.x; p.py = p.y; p.pz = p.z; p.pw = p.w; p.previous = p.phase;
                if (++p.phase > 7) continue;
                p.z += p.vz;
                if (p.phase < 2) { p.x += p.rx; p.y += p.ry; }
                p.w *= p.phase < 7 ? .85f : 1.06f;
                parts[kept++] = p;
            }
            parts.resize(kept);
            if (double(f.tick) * kWeatherSecondsPerSegment > f.end) continue;
            const auto random = [&f]() { f.seed ^= f.seed << 13; f.seed ^= f.seed >> 17; f.seed ^= f.seed << 5; return f.seed; };
            const auto uniform = [&](float a, float b) { return a + (b - a) * float(random() & 0xffffu) / 65535.f; };
            int made = 0;
            for (int n = 0; n < 6; ++n) {
                if (random() % 100u >= 88u && !(n == 5 && made == 0)) continue;
                FireFlameParticle p;
                // The game's CParticleSource Scale: size, spread and rise;
                // every burn fire at 0.75 of it (user, cand-1128: fires too
                // big on almost everything), each source 0.85-1.15 of that by
                // a hash of its position, so fires differ but never flicker
                // (user: not all the same scale).
                const std::uint32_t h = (std::uint32_t(std::lround(f.x * 8)) * 0x9e3779b1u ^ std::uint32_t(std::lround(f.y * 8)) * 0x85ebca77u ^
                    std::uint32_t(std::lround(f.z * 8)) * 0xc2b2ae35u) >> 16;
                const float scale = .75f * f.scale * (.85f + .3f * float(h) / 65535.f);
                float add = 0; p.w = .8f * scale;
                if (random() % 100u < 40u) { p.w = .4f * scale; add = .24f; }
                p.rx = uniform(-.2f - add, .2f + add) * .5f * scale; p.ry = uniform(-.2f - add, .2f + add) * .5f * scale;
                p.x = f.x + p.rx; p.y = f.y + p.ry; p.z = f.z + uniform(0, .3f) * scale; p.vz = .3f * scale;
                p.px = p.x; p.py = p.y; p.pz = p.z; p.pw = p.w;
                parts.push_back(p); ++made;
            }
        }
    }
    fire_flames_.erase(std::remove_if(fire_flames_.begin(), fire_flames_.end(), [&](const FireFlame& f) {
        return f.parts.empty() && double(now) * kWeatherSecondsPerSegment > f.end; }), fire_flames_.end());
} catch (...) { fire_flames_.clear(); }

// The burning sources' particles, between their last two segments as the
// game draws them, through the game particle program with the game's fire
// texture (tendrismoke.mmp: 15 phases in a 4x4 grid, half-texel margins),
// blended SRCALPHA/INVSRCALPHA like the game's fires (fog, soft depth,
// bloom glow and reflections as for the game's own particles).
void Renderer::draw_fire_flames() noexcept try {
    if (!d3d12_ || fire_flames_.empty() || !fire_burn_bound_ || scene_composited_ || fire_flame_texture_failed_) return;
    std::size_t count = 0;
    for (const auto& f : fire_flames_) count += f.parts.size();
    if (!count) return;
    if (!fire_flame_texture_) return;   // decoded with the area's census (build_fire_census)
    const auto scene = bridge_scene_world_to_clip_snapshot();
    if (!scene.valid) return;
    const float step = float(std::clamp(fire_segments_ - std::floor(fire_segments_), 0.0, 1.0));
    constexpr float margin = .5f / 256.f;
    fire_flame_draw_.clear();
    fire_flame_draw_.reserve(count);
    for (const auto& f : fire_flames_)
        for (const auto& p : f.parts) {
            GameParticle g{};
            g.x = p.px + (p.x - p.px) * step; g.y = p.py + (p.y - p.py) * step; g.z = p.pz + (p.z - p.pz) * step;
            g.radius = p.pw + (p.w - p.pw) * step;
            const int column = p.previous % 4, row = p.previous / 4;
            g.u0 = column * .25f + margin; g.v0 = 1 - (row * .25f + margin);
            g.u1 = (column + 1) * .25f - margin; g.v1 = 1 - ((row + 1) * .25f - margin);
            g.color = 0xffffffffu;
            fire_flame_draw_.push_back(g);
        }
    const float* m = scene.world_to_clip.data();
    game_particle_order_.resize(fire_flame_draw_.size());
    for (std::uint32_t i = 0; i < game_particle_order_.size(); ++i) game_particle_order_[i] = i;
    std::sort(game_particle_order_.begin(), game_particle_order_.end(), [&](std::uint32_t a, std::uint32_t b) {
        return game_particle_screen_z(m, fire_flame_draw_[a]) > game_particle_screen_z(m, fire_flame_draw_[b]); });
    const UINT drawn = UINT(game_particle_order_.size());
    game_effect_elements_.resize(drawn);
    for (UINT i = 0; i < drawn; ++i) {
        const GameParticle& p = fire_flame_draw_[game_particle_order_[i]]; GameParticleGpu& g = game_effect_elements_[i];
        g.center_radius[0] = p.x; g.center_radius[1] = p.y; g.center_radius[2] = p.z; g.center_radius[3] = p.radius;
        g.uv[0] = p.u0; g.uv[1] = p.v0; g.uv[2] = p.u1; g.uv[3] = p.v1; g.color = p.color; g.padding[0] = g.padding[1] = g.padding[2] = 0;
    }
    FixedState st;
    st.render[D3DRENDERSTATE_ALPHABLENDENABLE] = TRUE;
    st.render[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_SRCALPHA; st.render[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_INVSRCALPHA;
    st.render[D3DRENDERSTATE_ZWRITEENABLE] = FALSE; st.render[D3DRENDERSTATE_CULLMODE] = D3DCULL_NONE;
    st.render[D3DRENDERSTATE_FOGENABLE] = TRUE;
    st.stage[0][D3DTSS_ADDRESSU] = st.stage[0][D3DTSS_ADDRESSV] = D3DTADDRESS_CLAMP;
    st.viewport.dwWidth = std::max<DWORD>(1, width_); st.viewport.dwHeight = std::max<DWORD>(1, height_);
    GameEffectDraw draw{};
    draw.object_to_clip = m;
    // Viewport-sized clip half extents: the program's clip point is the
    // scene's own, the radius (the game's world particle size, clip x units
    // times W) offsets it as for the game's particles.
    draw.clip_half_width = .5f * float(st.viewport.dwWidth); draw.clip_half_height = .5f * float(st.viewport.dwHeight);
    draw.element_count = drawn; draw.vertex_count = drawn * 6u;
    draw.vertex_program = WorldSupportShader::game_particle_vs;
    draw.fog = true; draw.reflection_depth = true; draw.soft = true; draw.profile_pass = "fire_burn_flames";
    if (FAILED(draw_game_effect(draw, st, fire_flame_texture_.get()))) {
        static bool reported = false;
        if (!reported) { reported = true; bridge_report_event("fire_burn_flames_unavailable", "draw failed"); }
    }
} catch (...) { fire_flames_.clear(); }


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
        "nativePrepassDS", "nativePrepassSnowDS", "gcPS", "gcVS", "gcDepthPS", "gcDepthVS", "gcShadowVS",
        "nativeShadowMaskVS", "nativeShadowMaskPS"})
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
        append_native_prepass_object_program(sources); append_terrain_tessellation_shader_programs(sources);
        append_ground_cover_shader_programs(sources); append_ground_cover_guide_programs(sources);
        { std::vector<ShaderProgram> rt; append_raytracing_shader_programs(rt);
          // The traced sun mask too: burnt leaves let the sun rays through.
          for (const auto& program : rt)
              if (std::strcmp(program.entry, "nativePrepassPS") == 0 || std::strcmp(program.entry, "nativeShadowMaskPS") == 0)
                  sources.push_back(program); }
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
    // Off (a preset switch turns it off at Low and Lowest): the map goes too,
    // since the lists may be rebuilt before it is on again.
    if (!configured_boolean(debug_overlay_.config(), "fire_burn", false)) {
        d3d12_->set_program_substitutes(nullptr, false);
        fire_burn_substitutes_.clear(); fire_burn_generation_seen_ = 0;
        return;
    }
    const std::vector<ShaderProgram>* lists[]{&native12_.programs, &native12_.guides, &gc12_.programs, &gc12_.guides, &rt12_.programs, &native12_.tessellation};
    if (world_programs_generation_ != fire_burn_generation_seen_) {
        fire_burn_generation_seen_ = world_programs_generation_; fire_burn_substitutes_.clear();
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
                else {
                    // A world program with no twin draws without the burn
                    // (cand-1128: some Suslanger grass did not burn): named
                    // once per program, with its defines.
                    std::string line = std::string(program.entry) + " " + program.profile;
                    for (const D3D_SHADER_MACRO* d = program.defines; d && d->Name; ++d)
                        line += std::string(" ") + d->Name + "=" + (d->Definition ? d->Definition : "");
                    if (fire_burn_untwinned_.insert(line).second) bridge_report_event("fire_burn_untwinned", line.c_str());
                }
            }
    }
    // The burn programs only while the area holds a spell's burn (char,
    // burning plants, lost leaves); a melt alone (a campfire's) never
    // switches them: the snow melt is in every program's snow code.
    d3d12_->set_program_substitutes(&fire_burn_substitutes_, fire_burn_bound_ && !fire_burns_.empty());
} catch (...) { fire_burn_substitutes_.clear(); fire_burn_generation_seen_ = 0; if (d3d12_) d3d12_->set_program_substitutes(nullptr, false); }

#endif
} // namespace owned
