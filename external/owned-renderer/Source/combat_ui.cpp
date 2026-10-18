#include "combat_ui.h"
#include "widescreen.h"
#ifdef EID3D11_EMBEDDED
#include "hud_layout_context.h"
#endif
#include "engine_bridge.h"
#include "debug_overlay_config.h"
#include "ui_language.h"
#include "owned_asset_loader.h"
#include "owned_resource_runtime.h"
#include "imgui.h"
#include <wrl/client.h>
#include <algorithm>
#include <condition_variable>
#include <cfloat>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace owned {
namespace {
using Microsoft::WRL::ComPtr;
struct PlateAnchor { std::int32_t id; float x, y; };
struct SpellList {
    CombatUnitValue unit;
    std::uint64_t epoch=0,frame=0;
    RECT local{};
    ImVec2 lo{},hi{},clip_lo{},clip_hi{};
    float opacity=1;
    bool placed=false;
};
struct State {
    std::mutex lock;
    std::unordered_map<std::int32_t, CombatUnitValue> units;
    std::unordered_map<std::int32_t, unsigned> incoming;
    std::vector<PlateAnchor> plates;
    std::vector<CombatPortraitAnchor> portraits;
    SpellList spell_list;
    std::array<CombatEffectDefinition, 256> catalog{};
    std::uint64_t catalog_revision = 0;
    std::vector<std::uint8_t> font_bytes;
    std::uint64_t font_generation = 0;
    bool mission = false;
    bool paused = false;
    std::int32_t hover_id = 0;
    std::uint64_t hover_incarnation = 0;
    bool plates_enabled = true;
    bool portraits_enabled = true;
    bool show_combat = true, show_all = false, show_alt = true, show_paused = true, show_hover = true;
    float idle_opacity = .5f;
    std::string status = "Waiting for an active mission.";
};
State& state() { static State value; return value; }
float plate_opacity(const State& s,const CombatUnitValue& unit) {
    if (unit.dead || unit.health<=0) return 0;
    const bool combat=unit.attacking || s.incoming.count(unit.id)!=0;
    if ((s.show_combat && combat) ||
        (s.show_hover && s.hover_id==unit.id && s.hover_incarnation==unit.incarnation)) return 1;
    const bool revealed=s.show_all || (!s.show_combat && ((s.show_paused && s.paused) ||
        (s.show_alt && (GetAsyncKeyState(VK_MENU)&0x8000)!=0)));
    return revealed ? (combat ? 1.f : s.idle_opacity) : 0;
}
void remove_attack(State& s, const CombatUnitValue& v) {
    for (const auto target : v.attack_targets) {
        if (!target) continue;
        const auto it = s.incoming.find(target);
        if (it != s.incoming.end() && --it->second == 0) s.incoming.erase(it);
    }
}
struct DrawUnit {
    CombatUnitValue unit;
    float x = 0, y = 0, width = 0;
    float opacity = 1;
    bool portrait = false;
};

// Decode only on the icon worker, after the normal MMP parser has validated
// format, mip and byte bounds. The atlas keeps all HUD icons in one GPU batch.
std::uint32_t channel(std::uint32_t raw, std::uint32_t mask, unsigned fallback) {
    if (!mask) return fallback;
    unsigned shift = 0;
    while (!(mask & 1u)) { mask >>= 1; ++shift; }
    return ((raw >> shift) & mask) * 255u / mask;
}
std::uint32_t rgb565(std::uint16_t raw) {
    return 0xff000000u | (channel(raw, 0xf800, 0) << 16) |
           (channel(raw, 0x07e0, 0) << 8) | channel(raw, 0x001f, 0);
}
std::uint32_t mix(std::uint32_t a, std::uint32_t b, unsigned wa, unsigned wb) {
    std::uint32_t result = 0xff000000u;
    for (unsigned shift : {0u, 8u, 16u})
        result |= ((((a >> shift) & 255u) * wa + ((b >> shift) & 255u) * wb) / (wa + wb)) << shift;
    return result;
}
bool icon_pixels(const TextureCpuAsset& source, std::vector<std::uint32_t>& pixels, unsigned size) {
    if (!source.source || source.mips.empty() || !source.width || !source.height) return false;
    const auto& mip = source.mips.front();
    if (mip.byte_offset > source.source->size() || mip.bytes > source.source->size() - mip.byte_offset) return false;
    const auto* bytes = source.source->data() + mip.byte_offset;
    const bool bc1 = source.format == MmpFormat::dxt1, bc2 = source.format == MmpFormat::dxt3,
        bc3 = source.format == MmpFormat::dxt5;
    pixels.resize(static_cast<std::size_t>(size) * size);
    for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x) {
        const unsigned sx = x * source.width / size, sy = source.height - 1 - y * source.height / size;
        std::uint32_t value = 0;
        if (bc1 || bc2 || bc3) {
            const auto offset = ((sy / 4) * ((source.width + 3) / 4) + sx / 4) * (bc1 ? 8u : 16u);
            if (offset + (bc1 ? 8u : 16u) > mip.bytes) return false;
            const auto* block = bytes + offset;
            std::uint64_t alpha = ~std::uint64_t{0};
            const std::uint8_t* alpha_block = block;
            if (bc2) { std::memcpy(&alpha, block, 8); block += 8; }
            if (bc3) { std::memcpy(&alpha, block + 2, 6); block += 8; }
            std::uint16_t c0 = 0, c1 = 0; std::uint32_t selectors = 0;
            std::memcpy(&c0, block, 2); std::memcpy(&c1, block + 2, 2);
            std::memcpy(&selectors, block + 4, 4);
            std::array<std::uint32_t, 4> colors{{rgb565(c0), rgb565(c1), 0, 0}};
            if (c0 > c1 || bc2 || bc3) { colors[2] = mix(colors[0], colors[1], 2, 1); colors[3] = mix(colors[0], colors[1], 1, 2); }
            else { colors[2] = mix(colors[0], colors[1], 1, 1); colors[3] = 0; }
            const unsigned lane = (sy % 4) * 4 + sx % 4;
            value = colors[(selectors >> (lane * 2)) & 3u];
            if (bc2) value = (value & 0xffffffu) | (std::uint32_t((alpha >> (lane * 4)) & 15u) * 17u << 24);
            if (bc3) {
                const unsigned a0 = alpha_block[0], a1 = alpha_block[1];
                const unsigned code = unsigned((alpha >> (lane * 3)) & 7u);
                unsigned a = code == 0 ? a0 : code == 1 ? a1 :
                    a0 > a1 ? ((8u - code) * a0 + (code - 1u) * a1) / 7u :
                    code == 6 ? 0u : code == 7 ? 255u : ((6u - code) * a0 + (code - 1u) * a1) / 5u;
                value = (value & 0xffffffu) | (std::uint32_t(a) << 24);
            }
        } else {
            const unsigned stride = source.bit_count / 8;
            const auto offset = (static_cast<std::size_t>(sy) * source.width + sx) * stride;
            if (!stride || stride > 4 || offset + stride > mip.bytes) return false;
            std::uint32_t raw = 0; std::memcpy(&raw, bytes + offset, stride);
            // PNT3 is already decoded to BGRA; its original header has no
            // channel masks. Use the parser's established pixel format, as
            // the normal owned texture uploader does.
            if (source.format == MmpFormat::argb8888) value = raw;
            else {
                const auto red = source.format == MmpFormat::rgb565 ? 0xf800u :
                    source.format == MmpFormat::argb1555 ? 0x7c00u : 0x0f00u;
                const auto green = source.format == MmpFormat::rgb565 ? 0x07e0u :
                    source.format == MmpFormat::argb1555 ? 0x03e0u : 0x00f0u;
                const auto blue = source.format == MmpFormat::argb4444 ? 0x000fu : 0x001fu;
                const auto alpha = source.format == MmpFormat::argb1555 ? 0x8000u :
                    source.format == MmpFormat::argb4444 ? 0xf000u : 0u;
                value = (channel(raw, alpha, 255) << 24) | (channel(raw, red, 0) << 16) |
                    (channel(raw, green, 0) << 8) | channel(raw, blue, 0);
            }
        }
        pixels[y * size + x] = value;
    }
    return true;
}
void parse_ids(const std::string& text, std::array<int, 256>& rank) {
    rank.fill(-1); const char* cursor = text.c_str(); int order = 0;
    while (*cursor) {
        if (*cursor < '0' || *cursor > '9') { ++cursor; continue; }
        char* end = nullptr; const auto id = std::strtol(cursor, &end, 10);
        if (id >= 0 && id < 256 && rank[static_cast<std::size_t>(id)] < 0) rank[static_cast<std::size_t>(id)] = order++;
        cursor = end;
    }
}
void parse_effect_priorities(const std::string& text,std::array<int,256>& rank) {
    rank.fill(100); const char* cursor=text.c_str();
    while (*cursor) {
        char* end=nullptr; const long id=std::strtol(cursor,&end,10);
        if (end==cursor) { ++cursor; continue; }
        cursor=end; while (*cursor==' ') ++cursor;
        if (*cursor!=':') continue;
        const char* value=++cursor; const long priority=std::strtol(value,&end,10);
        cursor=end;
        if (end!=value && id>=0 && id<256) rank[static_cast<unsigned>(id)]=int(std::clamp(priority,0L,1000L));
    }
}
std::string text_utf8(const std::string& text) {
    if (text.empty()) return {};
    const int count=MultiByteToWideChar(1251,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    if (!count) return {};
    std::wstring wide(count,L'\0');
    MultiByteToWideChar(1251,0,text.data(),static_cast<int>(text.size()),wide.data(),count);
    const int bytes=WideCharToMultiByte(CP_UTF8,0,wide.data(),count,nullptr,0,nullptr,nullptr);
    std::string result(bytes,'\0');
    WideCharToMultiByte(CP_UTF8,0,wide.data(),count,result.data(),bytes,nullptr,nullptr);
    return result;
}
std::string text_record(ResourceStore& store,const std::string& name) {
    ResourceLocation location; ResourceBlob blob; ResourceFailure failure;
    if (!store.locate(name,location,failure) || !store.read(location,blob,failure) ||
        !blob.bytes || blob.bytes->empty() || blob.bytes->size()>65536) return {};
    return std::string(reinterpret_cast<const char*>(blob.bytes->data()),blob.bytes->size());
}
std::string text_label(ResourceStore& store,const char* name) {
    const auto bytes=text_record(store,std::string("string ")+name);
    return text_utf8(bytes.substr(0,bytes.find_first_of("\r\n\0",0,3)));
}
void spell_text(ResourceStore& store,const char* code,std::string& title,std::string& description) {
    const auto bytes=text_record(store,std::string("spell ")+code);
    const auto line=bytes.find_first_of("\r\n");
    title=text_utf8(bytes.substr(0,line));
    auto start=bytes.find('\n');
    if (start==std::string::npos) return;
    ++start;
    while (start+1<bytes.size() && bytes[start]=='#' && bytes[start+1]=='#') {
        start=bytes.find('\n',start);
        if (start==std::string::npos) return;
        ++start;
    }
    const auto end=bytes.find_first_of("#\0",start,2);
    description=text_utf8(bytes.substr(start,end==std::string::npos ? end : end-start));
    while (!description.empty() && (description.back()=='\r' || description.back()=='\n')) description.pop_back();
}
bool contains(ImVec2 lo,ImVec2 hi,ImVec2 point,float margin=0) {
    return point.x>=lo.x-margin && point.y>=lo.y-margin && point.x<hi.x+margin && point.y<hi.y+margin;
}
} // namespace

void combat_ui_publish_unit(const CombatUnitValue& value) noexcept {
    try {
        auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
        auto& previous = s.units[value.id];
        if (previous.incarnation != value.incarnation || previous.attacking != value.attacking ||
            previous.attack_targets != value.attack_targets) {
            remove_attack(s, previous);
            for (const auto target : value.attack_targets) if (target) ++s.incoming[target];
        }
        // Copy only the native count, rather than the unused 256-effect tail.
        previous.id = value.id; previous.incarnation = value.incarnation;
        previous.health = value.health; previous.maximum_health = value.maximum_health;
        previous.dead = value.dead;
        previous.attacking = value.attacking; previous.attack_targets = value.attack_targets;
        previous.effect_count = value.effect_count;
        std::copy_n(value.effects.begin(), value.effect_count, previous.effects.begin());
    } catch (...) { combat_ui_set_status("Combat UI publication failed."); }
}
void combat_ui_retire_unit(std::int32_t id, std::uint64_t incarnation) noexcept {
    auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
    const auto it = s.units.find(id);
    if (incarnation && (it == s.units.end() || it->second.incarnation != incarnation)) return;
    if (it != s.units.end()) { remove_attack(s, it->second); s.units.erase(it); }
    if (s.spell_list.unit.id==id) s.spell_list={};
    if (s.hover_id==id) { s.hover_id=0; s.hover_incarnation=0; }
}
#ifdef EID3D11_EMBEDDED
void combat_ui_begin_embedded_frame(bool paused) noexcept {
    auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
    // Anchors belong to this world draw, even if its colour pass is rejected.
    s.plates.clear();
    s.paused = s.mission && paused;
}
void combat_ui_retire_embedded_world() noexcept {
    auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
    s.units.clear(); s.incoming.clear(); s.plates.clear(); s.portraits.clear();
    s.spell_list = {}; s.paused = false; s.hover_id = 0; s.hover_incarnation = 0;
}
bool combat_ui_embedded_portrait(std::int32_t id, unsigned slot,
    const RECT& native, const POINT& shift, CombatPortraitAnchor& output) noexcept {
    auto& context = embedded_hud_layout();
    widescreen::HudComposition h;
    if (!id || !context.slot_composition(HudPanel::portraits, slot, h)) return false;
    const auto& layout = context.layout();
    const float x = widescreen::compose_x(widescreen::placed_to_screen_x(layout,
        float(native.left + shift.x)), h);
    const float y = widescreen::compose_y(widescreen::placed_to_screen_y(layout,
        float(native.top + shift.y)), h);
    output = {id, x, y, x + float(native.right-native.left)*layout.unit*h.scale,
        y + float(native.bottom-native.top)*layout.unit*h.scale};
    return true;
}
#endif
void combat_ui_publish_spell_list(std::int32_t id,std::uint64_t incarnation,
    std::uint64_t epoch,std::uint64_t frame,const RECT& local_viewport) noexcept {
    auto& s=state(); std::lock_guard<std::mutex> guard(s.lock);
    s.spell_list={};
    const auto found=s.units.find(id);
    if (found==s.units.end() || found->second.incarnation!=incarnation) return;
    auto& list=s.spell_list;
    list.unit.id=id; list.unit.incarnation=incarnation; list.unit.effect_count=found->second.effect_count;
    std::copy_n(found->second.effects.begin(),list.unit.effect_count,list.unit.effects.begin());
    list.epoch=epoch; list.frame=frame; list.local=local_viewport;
}
void combat_ui_place_spell_list(std::uint64_t epoch,std::uint64_t frame,
    const RECT& destination,const RECT& source,const widescreen::HudComposition& h,
    unsigned width,unsigned height) noexcept {
    auto& s=state(); std::lock_guard<std::mutex> guard(s.lock);
    auto& list=s.spell_list;
    if (list.epoch!=epoch || list.frame!=frame || source.right<=source.left || source.bottom<=source.top) return;
    const auto x=[&](float pixel) { return widescreen::compose_x(destination.left+
        (pixel-source.left)*float(destination.right-destination.left)/(source.right-source.left),h); };
    const auto y=[&](float pixel) { return widescreen::compose_y(destination.top+
        (pixel-source.top)*float(destination.bottom-destination.top)/(source.bottom-source.top),h); };
    list.lo={x(float(list.local.left)),y(float(list.local.top))};
    list.hi={x(float(list.local.right)),y(float(list.local.bottom))};
    list.clip_lo={(std::max)({list.lo.x,x(float(source.left)),h.clip_left,0.f}),
        (std::max)({list.lo.y,y(float(source.top)),h.clip_top,0.f})};
    list.clip_hi={(std::min)({list.hi.x,x(float(source.right)),h.clip_right<0 ? float(width) : h.clip_right,float(width)}),
        (std::min)({list.hi.y,y(float(source.bottom)),h.clip_bottom<0 ? float(height) : h.clip_bottom,float(height)})};
    list.opacity=h.opacity;
    list.placed=list.hi.x>list.lo.x && list.hi.y>list.lo.y &&
        list.clip_hi.x>list.clip_lo.x && list.clip_hi.y>list.clip_lo.y && list.opacity>0;
}
void combat_ui_publish_catalog(const std::array<CombatEffectDefinition, 256>& catalog) noexcept {
    auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
    if (std::memcmp(s.catalog.data(), catalog.data(), sizeof(catalog)) != 0) {
        s.catalog = catalog; ++s.catalog_revision;
    }
}
void combat_ui_publish_font(const void* bytes,std::size_t size,std::uint64_t generation) noexcept {
    try {
        auto& s=state(); std::lock_guard<std::mutex> guard(s.lock);
        s.font_bytes.clear(); s.font_generation=0;
        if (bytes && size) {
            const auto* begin=static_cast<const std::uint8_t*>(bytes);
            s.font_bytes.assign(begin,begin+size); s.font_generation=generation;
        }
    } catch (...) { combat_ui_set_status("Combat text omitted: font snapshot allocation failed."); }
}
void combat_ui_set_mission(bool active) noexcept {
    auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
    s.mission = active;
    s.plates.clear(); s.portraits.clear();
    if (!active) {
        s.units.clear(); s.incoming.clear(); s.spell_list={};
        s.paused=false; s.hover_id=0; s.hover_incarnation=0;
    }
}
void combat_ui_publish_paused(bool paused) noexcept {
    auto& s=state(); std::lock_guard<std::mutex> guard(s.lock); s.paused=paused;
}
void combat_ui_set_options(const RendererConfig& config) noexcept {
    auto& s=state(); std::lock_guard<std::mutex> guard(s.lock);
    s.plates_enabled=config.find("combat_healthplates")->boolean_value;
    s.portraits_enabled=config.find("combat_portrait_effects")->boolean_value;
    s.show_combat=config.find("combat_plate_combat")->boolean_value;
    s.show_all=config.find("combat_plate_all")->boolean_value;
    s.show_alt=config.find("combat_plate_alt")->boolean_value;
    s.show_paused=config.find("combat_plate_paused")->boolean_value;
    s.show_hover=config.find("combat_plate_hover")->boolean_value;
    s.idle_opacity=config.find("combat_plate_idle_opacity")->integer_value/100.f;
}
void combat_ui_publish_hover(std::int32_t id,std::uint64_t incarnation) noexcept {
    auto& s=state(); std::lock_guard<std::mutex> guard(s.lock);
    s.hover_id=id; s.hover_incarnation=incarnation;
}
void combat_ui_set_status(const char* status) noexcept {
    try { auto& s = state(); std::lock_guard<std::mutex> guard(s.lock); s.status = status; } catch (...) {}
}
void combat_ui_publish_plate(std::int32_t id, float x, float y) noexcept {
    try {
        auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
        for (auto& plate : s.plates) if (plate.id == id) { plate.x = x; plate.y = y; return; }
        s.plates.push_back({id, x, y});
    } catch (...) {}
}
void combat_ui_publish_portraits(const CombatPortraitAnchor* anchors, std::size_t count) noexcept {
    try {
        auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
        if (anchors && count) s.portraits.assign(anchors, anchors + count);
        else s.portraits.clear();
    } catch (...) {}
}
bool combat_ui_has_output() noexcept {
    auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
    return s.mission && (s.plates_enabled || s.portraits_enabled ||
        (s.spell_list.placed && s.spell_list.unit.effect_count &&
         bridge_character_pane_text_epoch_current(s.spell_list.epoch,s.spell_list.frame)));
}
bool combat_ui_needs_plate(std::int32_t id) noexcept {
    auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
    if (!s.mission || !s.plates_enabled) return false;
    const auto found = s.units.find(id);
    // Admission precedes root sampling/projection. Alt/pause/hover use current
    // state, so newly revealed plates still get an anchor in this frame.
    return found != s.units.end() && plate_opacity(s,found->second)>0;
}
void register_combat_ui_options(RendererConfig& config) {
    config.register_boolean("combat_portrait_effects", "Portrait buffs / debuffs", "Combat UI", true, false,
        "Show active effect icons above character portraits.");
    config.register_boolean("combat_healthplates", "Combat healthplates", "Combat UI", true, false,
        "Enable character healthplates. The display rules below choose when they appear.");
    config.register_boolean("combat_plate_combat", "Show in combat", "Combat UI", false, false,
        "Always show combat participants at full opacity, including while unpaused and without Alt. Overrides and disables the Alt and pause rules.");
    config.register_boolean("combat_plate_alt", "Show all plates while Alt is held", "Combat UI", true, false,
        "Reveal all visible characters while Alt is held. Available when Show in combat is off.");
    config.register_boolean("combat_plate_paused", "Show all plates when paused", "Combat UI", true, false,
        "Reveal all visible characters while the game is paused. Available when Show in combat is off.");
    config.register_boolean("combat_plate_hover", "Show plate on hover", "Combat UI", true, false,
        "The hovered character's plate has full opacity.");
    config.register_boolean("combat_plate_all", "Show all visible characters", "Combat UI", false, false,
        "Always show every visible living character, regardless of combat, Alt or pause.");
    config.register_integer("combat_plate_idle_opacity", "Non-combat plate opacity (%)", "Combat UI", 50, 0, 100, 1, false,
        "Opacity of characters revealed outside combat. Hovered plates remain fully opaque.");
    config.register_integer("combat_plate_width", "HP bar width", "Combat UI", 120, 30, 320, 1, false,
        "Health bar width at 1080p. Scales with the screen height.");
    config.register_integer("combat_plate_height", "HP bar height", "Combat UI", 8, 3, 40, 1, false,
        "Health bar height at 1080p. Scales with the screen height.");
    config.register_integer("combat_plate_spacing", "Space above character", "Combat UI", 28, 0, 40, 1, false,
        "Gap between the top of the character and the health bar, scaled from 1080p.");
    config.register_boolean("combat_plate_stacking", "Keep healthplates apart", "Combat UI", true, false,
        "Move overlapping healthplates apart in screen space, including their effect rows. Crowded screen edges may still overlap.");
    config.register_integer("combat_plate_stack_gap", "Stacking gap (screen pixels)", "Combat UI", 4, 0, 80, 1, false,
        "Minimum space between neighbouring plate groups when Keep healthplates apart is enabled.");
    config.register_floating_point("combat_icon_scale", "Effect icon scale", "Combat UI", 1.15f, 0.5f, 2.5f, 0.05f, false,
        "Size of buff and debuff icons on healthplates and portraits.");
    config.register_boolean("combat_split_effects", "Separate buffs and debuffs", "Combat UI", true, false,
        "Healthplates: buffs above HP and debuffs below. Portraits: separate rows above the portrait to keep them on screen.");
    config.register_boolean("combat_effect_outlines", "Buff / debuff outlines", "Combat UI", true, false,
        "Inset gold outlines for buffs and crimson outlines for debuffs.");
    config.register_boolean("combat_hp_values", "Show current / maximum HP", "Combat UI", true, false,
        "Print current and maximum health on the health bar.");
    config.register_boolean("combat_hp_percent", "Show HP percentage", "Combat UI", false, false,
        "Print remaining health as a percentage on the health bar.");
    config.register_integer("combat_hp_text_size", "HP text size", "Combat UI", 18, 8, 32, 1, false,
        "Health text size at 1080p, using the selected game font.");
    config.register_integer("combat_max_effects", "Maximum effects", "Combat UI", 3, 0, 32, 1, false,
        "Limit per group when buffs and debuffs are separate, including each group's +X. Always-show spells may exceed the limit.");
    config.register_boolean("combat_durations", "Show remaining duration", "Combat UI", true, false,
        "Print the time left on each effect icon. Hold Alt over an icon for its spell description.");
    config.register_integer("combat_duration_size", "Duration text size", "Combat UI", 10, 8, 32, 1, false,
        "Effect timer and +X text size at 1080p, using the selected game font.");
    config.register_text("combat_priority", "Always show (effect IDs)", "Combat UI", "Ordered comma-separated IDs. Priorities count toward the maximum and always show.", "29,34,25,32,30,15,38");
    config.register_text("combat_effect_priorities", "Effect priorities (ID:priority)", "Combat UI", "Lower numbers stay visible first. Default is 100; range 0 to 1000. Edit each effect below or enter ID:priority pairs.",
        "9:99,10:99,11:99,15:102,25:102,26:101,29:102,30:102,31:101,33:101,34:101,38:102");
    config.register_text("combat_blacklist", "Never show (effect IDs)", "Combat UI", "Comma-separated IDs. Blacklist takes precedence over priority.", "13,14,12");
    config.register_text("combat_buff_overrides", "Treat as buffs (effect IDs)", "Combat UI", "Optional display classification overrides.");
    config.register_text("combat_debuff_overrides", "Treat as debuffs (effect IDs)", "Combat UI", "Optional display classification overrides. Debuff overrides win.", "16,50");
}

struct CombatUiRenderer::Implementation {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ComPtr<ID3D11Texture2D> atlas;
    ComPtr<ID3D11ShaderResourceView> atlas_view;
    struct Request { unsigned id; std::uint32_t icon; bool hd=false; std::array<char,64> code{}; };
    struct Completed {
        unsigned id; std::vector<std::uint32_t> pixels; bool valid;
        std::string title,description,value_label,detail_seconds;
    };
    std::mutex work_lock;
    std::condition_variable wake;
    std::deque<Request> requests;
    std::deque<Completed> completed;
    bool stopping = false;
    std::thread worker;
    ResourceStoreConfig mounts;
    ResourceStoreConfig text_mounts;
    std::array<CombatEffectDefinition, 256> catalog{};
    std::array<bool, 256> requested{}, urgent{}, ready{}, failed{};
    bool health_art_ready = false;
    std::uint64_t catalog_revision = ~std::uint64_t{0};
    std::vector<DrawUnit> draw_units;
    struct Lane { std::array<unsigned,256> shown{},hidden{}; unsigned count=0,overflow=0; };
    struct PlateLayout {
        float x=0,bar_top=0,bar_bottom=0;
        ImVec2 lo{},hi{},hp_extent{};
        std::array<char,64> hp_text{};
        std::array<Lane,2> lanes{};
    };
    std::vector<PlateLayout> plate_layouts;
    std::vector<unsigned> plate_order,placed_plates;
    std::array<int, 256> priority{}, blacklist{};
    std::string last_priority, last_blacklist;
    std::array<int,256> effect_priorities{};
    std::string last_effect_priorities;
    std::array<int,256> buff_override{},debuff_override{};
    std::string last_buffs,last_debuffs;
    std::array<std::string,256> titles,descriptions;
    std::string value_label=u8"\u2039",detail_seconds=u8"\u0441\u0435\u043a.";
    void effect_value(const CombatEffectValue& effect,char* text,std::size_t size) const {
        if (effect.amount_available)
            std::snprintf(text,size,effect.whole_amount ? "%s%.0f" : "%s%.1f",value_label.c_str(),effect.amount);
        else std::snprintf(text,size,u8"%s\u2014",value_label.c_str());
    }
    ImFont* font=nullptr;
    std::vector<std::uint8_t> font_bytes;
    std::uint64_t font_generation=0;
    struct Popup {
        std::int32_t unit=0;
        std::uint64_t incarnation=0;
        bool portrait=false;
        unsigned lane=0;
        ImVec2 trigger_lo{},trigger_hi{},lo{},hi{};
        double last_hover=0;
        int scroll=0;
        bool open=false,hovered=false;
    } popup;
    float popup_wheel=0;
    SpellList spell_list;
    std::int32_t scroll_unit=0;
    std::uint64_t scroll_incarnation=0;
    float list_scroll=0,list_wheel=0;
    bool list_drag=false;
    float drag_offset=0;
    ImVec2 list_clip_lo{},list_clip_hi{},list_track_lo{},list_track_hi{};
    float list_thumb_height=0,list_max_scroll=0;
    ImDrawListSplitter layers;

    bool debuff(unsigned id) const {
        return debuff_override[id]>=0 || (buff_override[id]<0 && catalog[id].debuff);
    }
    void sync_font() {
        auto& shared=state();
        {
            std::lock_guard<std::mutex> guard(shared.lock);
            if (font_generation==shared.font_generation) return;
            if (font) { ImGui::GetIO().Fonts->RemoveFont(font); font=nullptr; }
            font_generation=shared.font_generation;
            font_bytes=shared.font_bytes;
        }
        if (font_bytes.size()<=100) return;
        // GetFontData returns the selected standalone SFNT. Collection data
        // cannot identify its selected face here and is explicitly omitted.
        const auto* b=font_bytes.data();
        if (!((b[0]==0 && b[1]==1 && b[2]==0 && b[3]==0) || std::memcmp(b,"OTTO",4)==0)) {
            combat_ui_set_status("Combat text omitted: selected font format is unsupported."); return;
        }
        ImFontConfig config; config.FontDataOwnedByAtlas=false;
        font=ImGui::GetIO().Fonts->AddFontFromMemoryTTF(font_bytes.data(),static_cast<int>(font_bytes.size()),16,&config);
        if (!font) combat_ui_set_status("Combat text omitted: native font could not be loaded.");
    }

    void run() noexcept {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        try {
            ResourceStore store,text_store; ResourceFailure failure;
            const bool opened = store.open(mounts, failure);
            const bool texts_opened=text_store.open(text_mounts,failure);
            if (!texts_opened) combat_ui_set_status("Combat spell text unavailable: active text archives could not be opened.");
            for (;;) {
                Request request{};
                {
                    std::unique_lock<std::mutex> guard(work_lock);
                    wake.wait(guard, [&] { return stopping || !requests.empty(); });
                    if (stopping) return;
                    request = requests.front(); requests.pop_front();
                }
                Completed result{}; result.id = request.id;
                if (request.id<256 && texts_opened) spell_text(text_store,request.code.data(),result.title,result.description);
                if (request.id==256 && texts_opened) {
                    result.value_label=text_label(text_store,"infoitem_82");
                    result.detail_seconds=text_label(text_store,"infoskill_49");
                }
                char name[48]{};
                if (request.id == 256) std::snprintf(name, sizeof(name), "battle00.mmp");
                else std::snprintf(name, sizeof(name), request.hd ? "spell%04u.x2.mmp" : "spell%04u.mmp", request.icon);
                ResourceLocation location; ResourceBlob blob; TextureCpuAsset texture;
                result.valid = opened && store.locate(name, location, failure) && store.read(location, blob, failure) &&
                    parse_texture_asset(blob, texture, failure) && icon_pixels(texture, result.pixels, request.id == 256 ? 256u : 64u);
                if (!result.valid) {
                    char diagnostic[160]{}; std::snprintf(diagnostic, sizeof(diagnostic),
                        "EID3D11 combat icon omitted: %s (%s)\n", name, failure.message.c_str());
                    OutputDebugStringA(diagnostic);
                }
                std::lock_guard<std::mutex> guard(work_lock);
                if (result.id == 256 || urgent[result.id]) completed.push_front(std::move(result));
                else completed.push_back(std::move(result));
            }
        } catch (...) { combat_ui_set_status("Combat icon worker failed."); }
    }
    void request(unsigned id, bool visible) {
        if (!catalog[id].valid || ready[id] || failed[id] || (requested[id] && (!visible || urgent[id]))) return;
        std::lock_guard<std::mutex> guard(work_lock);
        if (requested[id]) {
            // Promote a newly visible icon once, including a decoded value
            // awaiting its bounded GPU upload. No queue searches in steady state.
            urgent[id] = true;
            const auto queued = std::find_if(requests.begin(), requests.end(), [id](const Request& v) { return v.id == id; });
            if (queued != requests.end()) { const auto value = *queued; requests.erase(queued); requests.push_front(value); }
            const auto decoded = std::find_if(completed.begin(), completed.end(), [id](const Completed& v) { return v.id == id; });
            if (decoded != completed.end()) { auto value = std::move(*decoded); completed.erase(decoded); completed.push_front(std::move(value)); }
            return;
        }
        requested[id] = true;
        urgent[id] = visible;
        Request value{id,catalog[id].icon,catalog[id].hd_icon,catalog[id].name};
        if (visible) requests.push_front(value);
        else requests.push_back(value);
        wake.notify_one();
    }
    void upload() {
        Completed value;
        {
            std::unique_lock<std::mutex> guard(work_lock, std::try_to_lock);
            if (!guard || completed.empty()) return;
            value = std::move(completed.front()); completed.pop_front();
        }
        if (value.id<256) { titles[value.id]=std::move(value.title); descriptions[value.id]=std::move(value.description); }
        else {
            if (!value.value_label.empty()) value_label=std::move(value.value_label);
            if (!value.detail_seconds.empty()) detail_seconds=std::move(value.detail_seconds);
        }
        if (!value.valid) {
            if (value.id == 256) combat_ui_set_status("Combat healthplate artwork is unavailable.");
            else failed[value.id] = true;
            return;
        }
        if (!atlas) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 1024; desc.Height = 1280; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, &atlas)) ||
                FAILED(device->CreateShaderResourceView(atlas.Get(), nullptr, &atlas_view))) {
                if (value.id < 256) failed[value.id] = true;
                combat_ui_set_status("Combat icon atlas allocation failed."); return;
            }
        }
        D3D11_BOX box{(value.id % 16) * 64, (value.id / 16) * 64, 0,
                     (value.id % 16 + 1) * 64, (value.id / 16 + 1) * 64, 1};
        const unsigned size = value.id == 256 ? 256u : 64u;
        if (value.id == 256) box = {0, 1024, 0, 256, 1280, 1};
        context->UpdateSubresource(atlas.Get(), 0, &box, value.pixels.data(), size * 4, 0);
        if (value.id == 256) health_art_ready = true;
        else ready[value.id] = true;
    }
    bool collect() {
        auto& s = state(); std::lock_guard<std::mutex> guard(s.lock);
        const bool changed = catalog_revision != s.catalog_revision;
        if (changed) { catalog = s.catalog; catalog_revision = s.catalog_revision; }
        std::size_t count = 0;
        const auto append = [&](std::int32_t id, float x, float y, float width, bool portrait) {
            const auto found = s.units.find(id);
            if (found == s.units.end()) return;
            const auto& unit = found->second;
            const float opacity=portrait ? 1.f : plate_opacity(s,unit);
            if (opacity<=0) return;
            if (portrait && !unit.effect_count) return;
            if (count == draw_units.size()) draw_units.emplace_back();
            auto& out = draw_units[count++]; out.x = x; out.y = y; out.width = width; out.portrait = portrait;
            out.opacity=opacity;
            out.unit.id = id; out.unit.incarnation=unit.incarnation;
            out.unit.health = unit.health; out.unit.maximum_health = unit.maximum_health;
            out.unit.dead = unit.dead;
            out.unit.effect_count = unit.effect_count;
            std::copy_n(unit.effects.begin(), unit.effect_count, out.unit.effects.begin());
        };
        if (s.mission && s.plates_enabled) for (const auto& p : s.plates) append(p.id, p.x, p.y, 0, false);
        if (s.mission && s.portraits_enabled) for (const auto& p : s.portraits)
            append(p.id, (p.left + p.right) * .5f, p.top, p.right - p.left, true);
        // Presentation consumes current anchors. Native profiling hooks are
        // optional and cannot own an overlay's lifetime.
        s.plates.clear();
        spell_list=s.spell_list.placed && bridge_character_pane_text_epoch_current(s.spell_list.epoch,s.spell_list.frame)
            ? s.spell_list : SpellList{};
        if (scroll_unit!=spell_list.unit.id || scroll_incarnation!=spell_list.unit.incarnation) {
            scroll_unit=spell_list.unit.id; scroll_incarnation=spell_list.unit.incarnation;
            list_scroll=0; list_wheel=0; list_drag=false;
        }
        draw_units.resize(count);
        return changed;
    }
};
CombatUiRenderer::CombatUiRenderer() = default;
CombatUiRenderer::~CombatUiRenderer() { shutdown(); }
void CombatUiRenderer::initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
    shutdown(); implementation_ = std::make_unique<Implementation>();
    auto& s = *implementation_; s.device = device; s.context = context;
    s.priority.fill(-1); s.blacklist.fill(-1);
    s.effect_priorities.fill(100);
    s.buff_override.fill(-1); s.debuff_override.fill(-1);
    owned_text_resource_mounts(s.text_mounts);
    if (owned_visual_resource_mounts(s.mounts)) {
        s.requests.push_back({256, 0});
        s.worker = std::thread([&s] { s.run(); });
    }
    else combat_ui_set_status("Combat icons unavailable: active resource mounts are missing.");
}
void CombatUiRenderer::shutdown() noexcept {
    if (!implementation_) return;
    auto& s = *implementation_;
    { std::lock_guard<std::mutex> guard(s.work_lock); s.stopping = true; }
    s.wake.notify_one(); if (s.worker.joinable()) s.worker.join();
    if (s.font) ImGui::GetIO().Fonts->RemoveFont(s.font);
    implementation_.reset();
}
bool CombatUiRenderer::prepare(RendererConfig& config) {
    if (!implementation_) return false;
    auto& s = *implementation_;
    s.sync_font();
    const auto* portrait_option = config.find("combat_portrait_effects");
    const auto* plate_option = config.find("combat_healthplates");
    if (!portrait_option || !plate_option) return false;
    combat_ui_set_options(config);
#ifndef EID3D11_EMBEDDED
    bridge_update_combat_portraits();
#endif
    const bool catalog_changed = s.collect();
    const auto& priority = config.find("combat_priority")->text_value;
    const auto& blacklist = config.find("combat_blacklist")->text_value;
    if (priority != s.last_priority) { parse_ids(priority, s.priority); s.last_priority = priority; }
    if (blacklist != s.last_blacklist) { parse_ids(blacklist, s.blacklist); s.last_blacklist = blacklist; }
    const auto& effect_priorities=config.find("combat_effect_priorities")->text_value;
    if (effect_priorities!=s.last_effect_priorities) {
        parse_effect_priorities(effect_priorities,s.effect_priorities); s.last_effect_priorities=effect_priorities;
    }
    const auto& buffs=config.find("combat_buff_overrides")->text_value;
    const auto& debuffs=config.find("combat_debuff_overrides")->text_value;
    if (buffs!=s.last_buffs) { parse_ids(buffs,s.buff_override); s.last_buffs=buffs; }
    if (debuffs!=s.last_debuffs) { parse_ids(debuffs,s.debuff_override); s.last_debuffs=debuffs; }
    bool visible=false;
    if (s.spell_list.placed) {
        for (unsigned i=0;i<s.spell_list.unit.effect_count;++i) {
            const auto& effect=s.spell_list.unit.effects[i];
            if (effect.ticks>1 && s.catalog[effect.id].valid) { visible=true; s.request(effect.id,true); }
        }
    }
    for (const auto& unit : s.draw_units) {
        visible=visible || !unit.portrait;
        for (unsigned i=0;i<unit.unit.effect_count;++i) {
            const auto& effect=unit.unit.effects[i];
            if (effect.ticks<=1 || !s.catalog[effect.id].valid || s.blacklist[effect.id]>=0) continue;
            visible=true;
            s.request(effect.id,true);
        }
    }
    // Prewarm the rest during gameplay; scroll/input never controls streaming.
    if (catalog_changed) for (unsigned id = 0; id < s.catalog.size(); ++id) s.request(id, false);
    s.upload();
    if (!visible) { s.popup={}; s.popup_wheel=0; }
    return visible;
}
void CombatUiRenderer::draw(RendererConfig& config,bool interactive) {
    if (!implementation_) return;
    auto& s=*implementation_;
    if (s.draw_units.empty() && !s.spell_list.placed) { s.popup={}; s.list_drag=false; return; }
    const auto& io=ImGui::GetIO(); const auto output=io.DisplaySize;
    const auto& canvas=widescreen_layout();
    const float sx=output.x/canvas.width,sy=output.y/canvas.height;
    const float dpi=std::clamp(output.y/1080.f,.65f,2.5f);
    const float icon=22.f*config.find("combat_icon_scale")->floating_value*dpi,gap=2.f*dpi;
    const float duration_size=float(config.find("combat_duration_size")->integer_value)*dpi;
    const bool durations=config.find("combat_durations")->boolean_value;
    const bool split=config.find("combat_split_effects")->boolean_value;
    const bool outlines=config.find("combat_effect_outlines")->boolean_value;
    const unsigned limit=unsigned(config.find("combat_max_effects")->integer_value);
    const bool alt=interactive && (GetAsyncKeyState(VK_MENU)&0x8000)!=0; const double now=ImGui::GetTime();
    if (!alt) { s.popup={}; s.popup_wheel=0; }
    const bool inside_popup=s.popup.open && contains(s.popup.lo,s.popup.hi,io.MousePos);
    unsigned tooltip=256; CombatEffectValue tooltip_effect{};
    using Lane=Implementation::Lane;
    const auto select=[&](const DrawUnit& entry,std::array<Lane,2>& lanes) {
        lanes={};
        for (unsigned i=0;i<entry.unit.effect_count;++i) {
            const auto& effect=entry.unit.effects[i];
            if (effect.ticks>1 && s.catalog[effect.id].valid && s.blacklist[effect.id]<0) {
                auto& lane=lanes[split && s.debuff(effect.id) ? 1 : 0]; lane.shown[lane.count++]=i;
            }
        }
        for (auto& lane:lanes) {
            std::sort(lane.shown.begin(),lane.shown.begin()+lane.count,[&](unsigned a,unsigned b) {
                const auto ia=entry.unit.effects[a].id,ib=entry.unit.effects[b].id;
                const bool always_a=s.priority[ia]>=0,always_b=s.priority[ib]>=0;
                if (always_a!=always_b) return always_a;
                if (s.effect_priorities[ia]!=s.effect_priorities[ib]) return s.effect_priorities[ia]<s.effect_priorities[ib];
                if (always_a && s.priority[ia]!=s.priority[ib]) return s.priority[ia]<s.priority[ib];
                return a<b;
            });
            unsigned always=0;
            while (always<lane.count && s.priority[entry.unit.effects[lane.shown[always]].id]>=0) ++always;
            const unsigned visible=lane.count<=limit ? lane.count : (std::max)(always,limit ? limit-1 : 0u);
            // Each separated group owns its limit and +X. Always-show effects
            // can exceed it, while ordinary effects compete by numeric priority.
            if (limit && visible<lane.count) {
                lane.overflow=lane.count-visible;
                std::copy_n(lane.shown.begin()+visible,lane.overflow,lane.hidden.begin());
            }
            lane.count=visible;
        }
    };
    float draw_opacity=1;
    const auto faded=[&](ImU32 color) {
        const unsigned alpha=(color>>IM_COL32_A_SHIFT)&255u;
        return (color&~IM_COL32_A_MASK)|(unsigned(alpha*draw_opacity+.5f)<<IM_COL32_A_SHIFT);
    };
    const auto text=[&](ImDrawList* draw,float size,ImVec2 pos,ImU32 color,const char* value) {
        if (s.font) draw->AddText(s.font,size,pos,faded(color),value);
    };
    const auto frame=[&](ImDrawList* draw,ImVec2 lo,ImVec2 hi,bool harmful,bool overflow) {
        const auto border=faded(harmful && outlines ? IM_COL32(175,40,58,255) : IM_COL32(213,171,75,255));
        draw->AddRectFilled(lo,hi,faded(overflow ? IM_COL32(43,28,0,245) : IM_COL32(22,17,12,245)),dpi);
        if (!overflow) return;
        draw->AddRect(ImVec2(lo.x+dpi,lo.y+dpi),ImVec2(hi.x-dpi,hi.y-dpi),border,dpi,0,2.f*dpi);
        draw->AddRect(ImVec2(lo.x+2*dpi,lo.y+2*dpi),ImVec2(hi.x-2*dpi,hi.y-2*dpi),
            faded(harmful && outlines ? IM_COL32(89,17,29,255) : IM_COL32(107,79,30,255)),0,0,dpi);
        if (overflow) {
            draw->AddLine(ImVec2(lo.x+dpi,lo.y+5*dpi),ImVec2(lo.x+5*dpi,lo.y+dpi),border,dpi);
            draw->AddLine(ImVec2(hi.x-5*dpi,hi.y-dpi),ImVec2(hi.x-dpi,hi.y-5*dpi),border,dpi);
        }
    };
    const auto effect_icon=[&](ImDrawList* draw,ImDrawListSplitter& layers,const CombatEffectValue& effect,ImVec2 lo,float size) {
        const ImVec2 hi(lo.x+size,lo.y+size); const unsigned id=effect.id;
        layers.SetCurrentChannel(draw,0); frame(draw,lo,hi,s.debuff(id),false);
        if (s.ready[id] && s.atlas_view) {
            layers.SetCurrentChannel(draw,1);
            draw->AddImage(ImTextureRef(ImTextureID(reinterpret_cast<std::uintptr_t>(s.atlas_view.Get()))),
                lo,hi,
                ImVec2(((id%16)*64+.5f)/1024.f,((id/16)*64+.5f)/1280.f),
                ImVec2(((id%16+1)*64-.5f)/1024.f,((id/16+1)*64-.5f)/1280.f),faded(IM_COL32_WHITE));
        } else if (s.failed[id]) {
            layers.SetCurrentChannel(draw,2); text(draw,14*dpi,ImVec2(lo.x+size*.35f,lo.y+size*.1f),IM_COL32(255,100,100,255),"!");
        }
        layers.SetCurrentChannel(draw,2);
        if (outlines) draw->AddRect(ImVec2(lo.x+2*dpi,lo.y+2*dpi),ImVec2(hi.x-2*dpi,hi.y-2*dpi),
            faded(s.debuff(id) ? IM_COL32(188,35,56,255) : IM_COL32(218,177,78,255)),dpi,0,2*dpi);
        if (durations && s.font) {
            const int seconds=effect.ticks/15+(effect.ticks%15!=0); char value[32]{};
            if (seconds>=60) std::snprintf(value,sizeof(value),"%d:%02d",seconds/60,seconds%60);
            else std::snprintf(value,sizeof(value),"%d",seconds);
            const auto extent=s.font->CalcTextSizeA(duration_size,FLT_MAX,0,value);
            const ImVec2 pos(lo.x+(size-extent.x)*.5f,hi.y-extent.y);
            draw->AddRectFilled(ImVec2(pos.x-dpi,pos.y),ImVec2(pos.x+extent.x+dpi,hi.y),faded(IM_COL32(0,0,0,180)));
            text(draw,duration_size,pos,IM_COL32(255,244,221,255),value);
        }
        if (alt && (!inside_popup || draw==ImGui::GetForegroundDrawList()) && contains(lo,hi,io.MousePos)) { tooltip=id; tooltip_effect=effect; }
    };
    const float width=float(config.find("combat_plate_width")->integer_value)*dpi;
    const float height=float(config.find("combat_plate_height")->integer_value)*dpi;
    const float hp_size=float(config.find("combat_hp_text_size")->integer_value)*dpi;
    const bool hp=config.find("combat_hp_values")->boolean_value,percent=config.find("combat_hp_percent")->boolean_value;
    const unsigned plate_columns=std::max(1u,unsigned(width/(icon+gap)));
    const auto lane_rows=[&](const Lane& lane,unsigned columns) {
        return (lane.count+(lane.overflow ? 1u : 0u)+columns-1)/columns;
    };
    s.plate_layouts.resize(s.draw_units.size()); s.plate_order.clear(); s.placed_plates.clear();
    for (unsigned index=0;index<s.draw_units.size();++index) {
        const auto& entry=s.draw_units[index]; auto& layout=s.plate_layouts[index]; layout={};
        select(entry,layout.lanes);
        if (entry.portrait) continue;
        layout.x=entry.x*sx; layout.bar_bottom=entry.y*sy-float(config.find("combat_plate_spacing")->integer_value)*dpi;
        layout.bar_top=layout.bar_bottom-height;
        float half_width=width*.5f+5*dpi;
        layout.lo.y=layout.bar_top; layout.hi.y=layout.bar_bottom;
        for (unsigned lane=0;lane<2;++lane) {
            const auto& effects=layout.lanes[lane]; const unsigned rows=lane_rows(effects,plate_columns);
            if (!rows) continue;
            const unsigned columns=std::min(plate_columns,effects.count+(effects.overflow ? 1u : 0u));
            half_width=std::max(half_width,(columns*(icon+gap)-gap)*.5f);
            if (lane) layout.hi.y+=rows*(icon+gap);
            else layout.lo.y-=rows*(icon+gap);
        }
        if (entry.unit.maximum_health>0) {
            const float fraction=std::clamp(float(entry.unit.health)/entry.unit.maximum_health,0.f,1.f);
            auto* value=layout.hp_text.data(); const auto capacity=layout.hp_text.size();
            if (hp && percent) std::snprintf(value,capacity,"%d/%d (%.0f%%)",entry.unit.health,entry.unit.maximum_health,fraction*100);
            else if (hp) std::snprintf(value,capacity,"%d/%d",entry.unit.health,entry.unit.maximum_health);
            else if (percent) std::snprintf(value,capacity,"%.0f%%",fraction*100);
            if (*value && s.font) {
                layout.hp_extent=s.font->CalcTextSizeA(hp_size,FLT_MAX,0,value);
                half_width=std::max(half_width,layout.hp_extent.x*.5f+dpi);
                const float text_top=layout.bar_top+(height-layout.hp_extent.y)*.5f;
                layout.lo.y=std::min(layout.lo.y,text_top); layout.hi.y=std::max(layout.hi.y,text_top+layout.hp_extent.y+dpi);
            }
        }
        layout.lo.x=layout.x-half_width; layout.hi.x=layout.x+half_width;
        s.plate_order.push_back(index);
    }
    // Only plates selected by the current display policy enter this pass.
    // Lowest plates keep their anchors; neighbours move upward as whole groups.
    const bool stacking=config.find("combat_plate_stacking")->boolean_value;
    if (stacking) std::sort(s.plate_order.begin(),s.plate_order.end(),[&](unsigned a,unsigned b) {
        const float ay=s.plate_layouts[a].hi.y,by=s.plate_layouts[b].hi.y;
        return ay==by ? s.draw_units[a].unit.id<s.draw_units[b].unit.id : ay>by;
    });
    const float stack_gap=float(config.find("combat_plate_stack_gap")->integer_value);
    for (unsigned index:s.plate_order) {
        auto& layout=s.plate_layouts[index];
        const float dx=std::clamp(layout.lo.x,0.f,std::max(0.f,output.x-(layout.hi.x-layout.lo.x)))-layout.lo.x;
        float dy=std::clamp(layout.lo.y,0.f,std::max(0.f,output.y-(layout.hi.y-layout.lo.y)))-layout.lo.y;
        if (stacking) for (unsigned previous:s.placed_plates) {
            const auto& placed=s.plate_layouts[previous];
            if (layout.lo.x+dx<placed.hi.x+stack_gap && layout.hi.x+dx+stack_gap>placed.lo.x &&
                layout.lo.y+dy<placed.hi.y+stack_gap && layout.hi.y+dy+stack_gap>placed.lo.y)
                dy=placed.lo.y-stack_gap-layout.hi.y;
        }
        dy=std::max(dy,-layout.lo.y);
        layout.x+=dx; layout.bar_top+=dy; layout.bar_bottom+=dy;
        layout.lo.x+=dx; layout.hi.x+=dx; layout.lo.y+=dy; layout.hi.y+=dy;
        // Keep accepted rectangles ordered by their actual bottom, so one
        // bottom-to-top collision sweep is sufficient; storage is reused.
        if (stacking) {
            const auto position=std::lower_bound(s.placed_plates.begin(),s.placed_plates.end(),layout.hi.y,
                [&](unsigned previous,float bottom) { return s.plate_layouts[previous].hi.y>bottom; });
            s.placed_plates.insert(position,index);
        }
    }
    auto* draw=ImGui::GetBackgroundDrawList(); auto& layers=s.layers; layers.Split(draw,3);
    for (unsigned index=0;index<s.draw_units.size();++index) {
        const auto& entry=s.draw_units[index]; const auto& layout=s.plate_layouts[index]; const auto& lanes=layout.lanes;
        draw_opacity=entry.opacity;
        const float x=entry.portrait ? entry.x*sx : layout.x,y=entry.y*sy;
        const float bar_bottom=layout.bar_bottom,bar_top=layout.bar_top;
        if (!entry.portrait) {
            const ImVec2 lo(x-width*.5f,bar_top),hi(x+width*.5f,bar_bottom);
            layers.SetCurrentChannel(draw,1);
            const auto texture=ImTextureRef(ImTextureID(reinterpret_cast<std::uintptr_t>(s.atlas_view.Get())));
            const auto uv=[](float u,float v) { return ImVec2(u/1024.f,(1024.f+v)/1280.f); };
            if (s.health_art_ready) draw->AddImage(texture,ImVec2(lo.x-5*dpi,lo.y),ImVec2(hi.x+5*dpi,hi.y),uv(59,147),uv(169,152),faded(IM_COL32_WHITE));
            else { layers.SetCurrentChannel(draw,0); draw->AddRect(lo,hi,faded(IM_COL32(180,70,60,255)),0,0,dpi); }
            if (entry.unit.maximum_health>0) {
                const float fraction=std::clamp(float(entry.unit.health)/entry.unit.maximum_health,0.f,1.f);
                if (s.health_art_ready) draw->AddImage(texture,lo,ImVec2(lo.x+width*fraction,hi.y),uv(64,154),uv(64+100*fraction,159),faded(IM_COL32_WHITE));
                if (*layout.hp_text.data() && s.font) {
                    layers.SetCurrentChannel(draw,2); const auto extent=layout.hp_extent;
                    const ImVec2 pos(x-extent.x*.5f,lo.y+(height-extent.y)*.5f);
                    text(draw,hp_size,ImVec2(pos.x+dpi,pos.y+dpi),IM_COL32(0,0,0,255),layout.hp_text.data());
                    text(draw,hp_size,pos,IM_COL32(255,244,221,255),layout.hp_text.data());
                }
            }
        }
        const unsigned columns=entry.portrait ? std::max(1u,unsigned(entry.width*sx/(icon+gap))) : std::max(1u,unsigned(width/(icon+gap)));
        const auto rows=[&](const Lane& lane) { return lane_rows(lane,columns); };
        const float portrait_height=(rows(lanes[0])+rows(lanes[1]))*(icon+gap)-gap;
        const float portrait_top=std::clamp(y-gap-portrait_height,0.f,std::max(0.f,output.y-portrait_height));
        for (unsigned lane_index=0;lane_index<2;++lane_index) {
            const auto& lane=lanes[lane_index]; const unsigned count=lane.count+(lane.overflow ? 1u : 0u); if (!count) continue;
            const float lane_height=rows(lane)*(icon+gap)-gap;
            // Portrait rows stay above their frame; world plates flank HP.
            const float top=entry.portrait ? portrait_top+(lane_index ? rows(lanes[0])*(icon+gap) : 0) :
                (lane_index ? bar_bottom+gap : bar_top-gap-lane_height);
            for (unsigned slot=0;slot<count;++slot) {
                const unsigned row=slot/columns,column=slot%columns,row_count=std::min(columns,count-row*columns);
                const float left=std::clamp(x-(row_count*(icon+gap)-gap)*.5f,0.f,std::max(0.f,output.x-(row_count*(icon+gap)-gap)));
                const ImVec2 lo(left+column*(icon+gap),top+row*(icon+gap)),hi(lo.x+icon,lo.y+icon);
                if (slot<lane.count) effect_icon(draw,layers,entry.unit.effects[lane.shown[slot]],lo,icon);
                else {
                    layers.SetCurrentChannel(draw,0); frame(draw,lo,hi,lane_index==1,true);
                    if (s.font) {
                        char value[24]{}; std::snprintf(value,sizeof(value),"+%u",lane.overflow); const float size=std::min(duration_size,icon*.7f);
                        const auto extent=s.font->CalcTextSizeA(size,FLT_MAX,0,value); layers.SetCurrentChannel(draw,2);
                        text(draw,size,ImVec2(lo.x+(icon-extent.x)*.5f,lo.y+(icon-extent.y)*.5f),IM_COL32(255,215,147,255),value);
                    }
                    if (alt && !inside_popup && contains(lo,hi,io.MousePos)) {
                        const bool same=s.popup.open && s.popup.unit==entry.unit.id && s.popup.incarnation==entry.unit.incarnation && s.popup.portrait==entry.portrait && s.popup.lane==lane_index;
                        if (!same) { s.popup={}; s.popup.open=true; s.popup_wheel=0; }
                        s.popup.unit=entry.unit.id; s.popup.incarnation=entry.unit.incarnation; s.popup.portrait=entry.portrait; s.popup.lane=lane_index;
                        s.popup.trigger_lo=lo; s.popup.trigger_hi=hi; s.popup.last_hover=now;
                    }
                }
            }
        }
    }
    draw_opacity=1;
    layers.Merge(draw);
    if (s.popup.open) {
        const auto entry=std::find_if(s.draw_units.begin(),s.draw_units.end(),[&](const DrawUnit& v) { return v.unit.id==s.popup.unit && v.unit.incarnation==s.popup.incarnation && v.portrait==s.popup.portrait; });
        std::array<Lane,2> lanes{}; if (entry!=s.draw_units.end()) select(*entry,lanes); const auto& lane=lanes[s.popup.lane];
        if (entry==s.draw_units.end() || !lane.overflow) s.popup={};
        else {
            const float row_height=std::max(icon,26*dpi)+4*dpi; const unsigned shown=std::min(8u,lane.overflow);
            const float popup_width=280*dpi,popup_height=shown*row_height+30*dpi;
            if (!contains(s.popup.lo,s.popup.hi,io.MousePos)) {
                const float left=std::clamp(s.popup.trigger_lo.x,0.f,std::max(0.f,output.x-popup_width)); float top=s.popup.trigger_hi.y+3*dpi;
                if (top+popup_height>output.y) top=s.popup.trigger_lo.y-popup_height-3*dpi;
                top=std::clamp(top,0.f,std::max(0.f,output.y-popup_height)); s.popup.lo=ImVec2(left,top); s.popup.hi=ImVec2(left+popup_width,top+popup_height);
            }
            s.popup.hovered=contains(s.popup.lo,s.popup.hi,io.MousePos);
            if (s.popup.hovered || contains(s.popup.trigger_lo,s.popup.trigger_hi,io.MousePos,3*dpi)) s.popup.last_hover=now;
            if (now-s.popup.last_hover>.18) s.popup={};
            else {
                const int steps=int(s.popup_wheel); s.popup_wheel-=steps;
                s.popup.scroll=std::clamp(s.popup.scroll-steps,0,int(lane.overflow-shown));
                auto* pop=ImGui::GetForegroundDrawList(); ImDrawListSplitter popup_layers; popup_layers.Split(pop,3);
                popup_layers.SetCurrentChannel(pop,0); pop->AddRectFilled(s.popup.lo,s.popup.hi,IM_COL32(43,28,0,255)); pop->AddRect(s.popup.lo,s.popup.hi,IM_COL32(255,179,49,255),0,0,dpi);
                popup_layers.SetCurrentChannel(pop,2); text(pop,16*dpi,ImVec2(s.popup.lo.x+6*dpi,s.popup.lo.y+4*dpi),IM_COL32(255,179,49,255),u8"\u0414\u0440\u0443\u0433\u0438\u0435 \u044d\u0444\u0444\u0435\u043a\u0442\u044b");
                pop->PushClipRect(ImVec2(s.popup.lo.x+2*dpi,s.popup.lo.y+27*dpi),ImVec2(s.popup.hi.x-2*dpi,s.popup.hi.y-2*dpi),true);
                for (unsigned n=0;n<shown;++n) {
                    const auto& effect=entry->unit.effects[lane.hidden[n+s.popup.scroll]]; const ImVec2 lo(s.popup.lo.x+6*dpi,s.popup.lo.y+28*dpi+n*row_height);
                    effect_icon(pop,popup_layers,effect,lo,icon); popup_layers.SetCurrentChannel(pop,2);
                    const char* title=s.titles[effect.id].empty() ? u8"\u041d\u0430\u0437\u0432\u0430\u043d\u0438\u0435 \u043e\u0442\u0441\u0443\u0442\u0441\u0442\u0432\u0443\u0435\u0442" : s.titles[effect.id].c_str();
                    text(pop,16*dpi,ImVec2(lo.x+icon+8*dpi,lo.y+3*dpi),IM_COL32(228,215,167,255),title);
                    if (alt && contains(lo,ImVec2(s.popup.hi.x-4*dpi,lo.y+row_height),io.MousePos)) { tooltip=effect.id; tooltip_effect=effect; }
                }
                pop->PopClipRect(); popup_layers.Merge(pop);
                if (lane.overflow>shown) {
                    const float track=shown*row_height,thumb=track*shown/lane.overflow,y=s.popup.lo.y+28*dpi+(track-thumb)*s.popup.scroll/(lane.overflow-shown);
                    pop->AddRectFilled(ImVec2(s.popup.hi.x-5*dpi,y),ImVec2(s.popup.hi.x-2*dpi,y+thumb),IM_COL32(213,171,75,255));
                }
            }
        }
    }
    s.list_clip_lo=s.list_clip_hi={}; s.list_max_scroll=0;
    if (s.spell_list.placed && s.font) {
        const auto& list=s.spell_list;
        const ImVec2 lo(list.lo.x*sx,list.lo.y*sy),hi(list.hi.x*sx,list.hi.y*sy);
        const ImVec2 clip_lo(list.clip_lo.x*sx,list.clip_lo.y*sy),clip_hi(list.clip_hi.x*sx,list.clip_hi.y*sy);
        const float ux=(hi.x-lo.x)/160.f,uy=(hi.y-lo.y)/180.f,row_height=30.f*uy;
        std::array<unsigned,256> rows{}; unsigned count=0;
        for (unsigned i=0;i<list.unit.effect_count;++i)
            if (list.unit.effects[i].ticks>1 && s.catalog[list.unit.effects[i].id].valid) rows[count++]=i;
        const float visible=(hi.y-lo.y)/row_height;
        s.list_max_scroll=(std::max)(0.f,float(count)-visible);
        s.list_scroll=std::clamp(s.list_scroll-s.list_wheel,0.f,s.list_max_scroll); s.list_wheel=0;
        s.list_clip_lo=clip_lo; s.list_clip_hi=clip_hi;
        s.list_track_lo={hi.x-5.f*ux,lo.y}; s.list_track_hi=hi;
        const float track_height=hi.y-lo.y;
        s.list_thumb_height=count ? track_height*(std::min)(1.f,visible/count) : track_height;
        if (s.list_drag && interactive && (GetAsyncKeyState(VK_LBUTTON)&0x8000) && s.list_max_scroll>0)
            s.list_scroll=std::clamp((io.MousePos.y-s.drag_offset-lo.y)/(track_height-s.list_thumb_height)*s.list_max_scroll,0.f,s.list_max_scroll);
        else s.list_drag=false;
        auto* draw=ImGui::GetBackgroundDrawList();
        draw->PushClipRect(clip_lo,clip_hi,true);
        const auto color=[&](ImU32 rgb) { return (rgb&0x00ffffffu) | (ImU32(255*list.opacity)<<24); };
        const int first=(std::max)(0,int(s.list_scroll)),last=(std::min)(int(count),int(std::ceil(s.list_scroll+visible)));
        for (int row=first;row<last;++row) {
            const auto& effect=list.unit.effects[rows[row]]; const auto id=effect.id;
            const float y=lo.y+(row-s.list_scroll)*row_height;
            const ImVec2 icon_lo(lo.x+8*ux,y+3*uy),icon_hi(lo.x+32*ux,y+27*uy);
            if (s.ready[id] && s.atlas_view)
                draw->AddImage(ImTextureRef(ImTextureID(reinterpret_cast<std::uintptr_t>(s.atlas_view.Get()))),icon_lo,icon_hi,
                    ImVec2(((id%16)*64+.5f)/1024.f,((id/16)*64+.5f)/1280.f),
                    ImVec2(((id%16+1)*64-.5f)/1024.f,((id/16+1)*64-.5f)/1280.f),color(IM_COL32_WHITE));
            else if (s.failed[id]) text(draw,12*uy,icon_lo,color(IM_COL32(255,100,100,255)),"!");
            const char* title=s.titles[id].empty() ? u8"\u041d\u0430\u0437\u0432\u0430\u043d\u0438\u0435 \u043e\u0442\u0441\u0443\u0442\u0441\u0442\u0432\u0443\u0435\u0442" : s.titles[id].c_str();
            char duration[48]{};
            std::snprintf(duration,sizeof(duration)," (%d %s)",(std::max)(0,effect.ticks/15),s.detail_seconds.c_str());
            const float name_room=(std::max)(0.f,115*ux-s.font->CalcTextSizeA(12*uy,FLT_MAX,0,duration).x);
            const float name_width=(std::min)(name_room,s.font->CalcTextSizeA(12*uy,FLT_MAX,0,title).x);
            draw->PushClipRect(ImVec2(lo.x+40*ux,y),ImVec2(lo.x+40*ux+name_room,y+15*uy),true);
            text(draw,12*uy,ImVec2(lo.x+40*ux,y),color(IM_COL32(228,215,167,255)),title);
            draw->PopClipRect();
            draw->PushClipRect(ImVec2(lo.x+40*ux,y),ImVec2(hi.x-5*ux,y+15*uy),true);
            text(draw,12*uy,ImVec2(lo.x+40*ux+name_width,y),color(IM_COL32(228,215,167,255)),duration);
            draw->PopClipRect();
            char details[96]{}; s.effect_value(effect,details,sizeof(details));
            draw->PushClipRect(ImVec2(lo.x+40*ux,y+15*uy),ImVec2(hi.x-5*ux,y+row_height),true);
            text(draw,12*uy,ImVec2(lo.x+40*ux,y+15*uy),color(IM_COL32(228,215,167,255)),details);
            draw->PopClipRect();
            if (alt && contains(clip_lo,clip_hi,io.MousePos) && contains(ImVec2(lo.x,y),ImVec2(hi.x-5*ux,y+row_height),io.MousePos)) {
                tooltip=id; tooltip_effect=effect;
            }
        }
        if (s.list_max_scroll>0) {
            draw->AddRectFilled(s.list_track_lo,s.list_track_hi,color(IM_COL32(43,28,0,255)),ux);
            const float top=lo.y+(track_height-s.list_thumb_height)*s.list_scroll/s.list_max_scroll;
            draw->AddRectFilled(ImVec2(s.list_track_lo.x,top),ImVec2(hi.x,top+s.list_thumb_height),color(IM_COL32(218,177,78,255)),ux);
        }
        draw->PopClipRect();
    } else { s.list_drag=false; s.list_wheel=0; }
    if (tooltip<256 && s.font) {
        auto* tip=ImGui::GetForegroundDrawList(); const char* title=s.titles[tooltip].empty() ? u8"\u041d\u0430\u0437\u0432\u0430\u043d\u0438\u0435 \u043e\u0442\u0441\u0443\u0442\u0441\u0442\u0432\u0443\u0435\u0442" : s.titles[tooltip].c_str();
        const char* body=s.descriptions[tooltip].empty() ? u8"\u041e\u043f\u0438\u0441\u0430\u043d\u0438\u0435 \u043e\u0442\u0441\u0443\u0442\u0441\u0442\u0432\u0443\u0435\u0442 \u0432 \u0438\u0433\u0440\u043e\u0432\u044b\u0445 \u0440\u0435\u0441\u0443\u0440\u0441\u0430\u0445." : s.descriptions[tooltip].c_str();
        const float size=16*dpi,wrap=std::min(360*dpi,output.x-24*dpi);
        const auto title_size=s.font->CalcTextSizeA(size,FLT_MAX,wrap,title),body_size=s.font->CalcTextSizeA(size,FLT_MAX,wrap,body);
        char value[96]{},details[256]{}; s.effect_value(tooltip_effect,value,sizeof(value));
        std::snprintf(details,sizeof(details),u8"\u042d\u0444\u0444\u0435\u043a\u0442: %s\n\u0414\u043b\u0438\u0442\u0435\u043b\u044c\u043d\u043e\u0441\u0442\u044c: %d %s",
            value,(std::max)(0,tooltip_effect.ticks/15),s.detail_seconds.c_str());
        const auto details_extent=s.font->CalcTextSizeA(size,FLT_MAX,0,details);
        const float tooltip_width=std::max({title_size.x,body_size.x,details_extent.x})+12*dpi,tooltip_height=title_size.y+body_size.y+details_extent.y+16*dpi;
        const float x=std::clamp(io.MousePos.x+24*dpi,0.f,std::max(0.f,output.x-tooltip_width)); float y=io.MousePos.y+24*dpi;
        if (y+tooltip_height>output.y) y=io.MousePos.y-tooltip_height-12*dpi; y=std::max(0.f,y);
        tip->AddRectFilled(ImVec2(x,y),ImVec2(x+tooltip_width,y+tooltip_height),IM_COL32(43,28,0,255)); tip->AddRect(ImVec2(x,y),ImVec2(x+tooltip_width,y+tooltip_height),IM_COL32(255,179,49,255),0,0,dpi);
        tip->AddText(s.font,size,ImVec2(x+6*dpi,y+4*dpi),IM_COL32(255,179,49,255),title,nullptr,wrap);
        tip->AddText(s.font,size,ImVec2(x+6*dpi,y+8*dpi+title_size.y),IM_COL32(228,215,167,255),body,nullptr,wrap);
        text(tip,size,ImVec2(x+6*dpi,y+12*dpi+title_size.y+body_size.y),IM_COL32(228,215,167,255),details);
    }
}
bool CombatUiRenderer::process_input(UINT message,WPARAM wparam,LPARAM lparam) noexcept {
    if (!implementation_) return false; auto& s=*implementation_;
    if (!(GetAsyncKeyState(VK_MENU)&0x8000)) { s.popup={}; s.popup_wheel=0; }
    if (message==WM_MOUSEWHEEL && s.popup.open && s.popup.hovered) { s.popup_wheel+=float(GET_WHEEL_DELTA_WPARAM(wparam))/WHEEL_DELTA; return true; }
    if (message==WM_KILLFOCUS || (message==WM_ACTIVATEAPP && !wparam)) { s.popup={}; s.list_drag=false; }
    const auto& list=s.spell_list;
    if (!list.placed || !bridge_character_pane_text_epoch_current(list.epoch,list.frame)) { s.list_drag=false; return false; }
    POINT point{static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
    if (message==WM_MOUSEWHEEL) {
        const auto window=static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
        if (!window || !ScreenToClient(window,&point)) return false;
    }
    const ImVec2 mouse(float(point.x),float(point.y));
    if (message==WM_MOUSEWHEEL && s.list_max_scroll>0 && contains(s.list_clip_lo,s.list_clip_hi,mouse)) {
        s.list_wheel+=float(GET_WHEEL_DELTA_WPARAM(wparam))/WHEEL_DELTA; return true;
    }
    if (message==WM_LBUTTONDOWN && s.list_max_scroll>0 && contains(s.list_clip_lo,s.list_clip_hi,mouse) &&
        contains(s.list_track_lo,s.list_track_hi,mouse)) {
        const float top=s.list_track_lo.y+(s.list_track_hi.y-s.list_track_lo.y-s.list_thumb_height)*s.list_scroll/s.list_max_scroll;
        s.drag_offset=mouse.y>=top && mouse.y<top+s.list_thumb_height ? mouse.y-top : s.list_thumb_height*.5f;
        s.list_drag=true; return true;
    }
    if (message==WM_LBUTTONUP && s.list_drag) { s.list_drag=false; return true; }
    if (message==WM_MOUSEMOVE && s.list_drag) return true;
    return false;
}
void CombatUiRenderer::draw_settings(RendererConfig& config) {
    ImGui::TextWrapped("%s", ui_text("Lower effect priorities stay visible first. Always show overrides the limit; blacklist wins. Separate buffs and debuffs each have their own limit."));
    for (const auto& option : config.options()) {
        if (option.group != "Combat UI") continue;
        const bool blocked=config.find("combat_plate_combat")->boolean_value &&
            (option.key=="combat_plate_alt" || option.key=="combat_plate_paused");
        const char* label = ui_option_label(option);
        ImGui::PushID(option.key.c_str());
        ImGui::BeginDisabled(blocked);
        if (option.type == RendererOptionType::boolean) { bool value = option.boolean_value; if (ImGui::Checkbox(label, &value)) config.set_boolean(option.key.c_str(), value); }
        else if (option.type == RendererOptionType::integer) { int value = option.integer_value; if (ImGui::SliderInt(label, &value, option.integer_minimum, option.integer_maximum)) config.set_integer(option.key.c_str(), value); }
        else if (option.type == RendererOptionType::floating_point) { float value = option.floating_value; if (ImGui::SliderFloat(label, &value, option.floating_minimum, option.floating_maximum, "%.2fx")) config.set_floating_point(option.key.c_str(), value); }
        else { char value[1024]{}; strncpy_s(value, option.text_value.c_str(), _TRUNCATE); if (ImGui::InputText(label, value, sizeof(value))) config.set_text(option.key.c_str(), value); }
        ImGui::EndDisabled();
        ImGui::PopID();
        if (!option.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*30.f);
            ImGui::TextUnformatted(ui_option_description(option));
            if (blocked) ImGui::TextUnformatted(ui_text("Disabled while Show in combat is enabled. Your saved choice is preserved."));
            ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
    }
    auto& shared = state(); std::array<CombatEffectDefinition, 256> catalog;
    { std::lock_guard<std::mutex> guard(shared.lock); ImGui::TextDisabled("%s", shared.status.c_str()); catalog = shared.catalog; }
    if (!ImGui::CollapsingHeader(ui_text("Effect list"))) return;
    if (ImGui::BeginTable("CombatEffectList", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 230))) {
        ImGui::TableSetupColumn(ui_text("Effect")); ImGui::TableSetupColumn(ui_text("Priority")); ImGui::TableSetupColumn(ui_text("Visibility")); ImGui::TableSetupColumn(ui_text("Type")); ImGui::TableSetupColumn(ui_text("Icon")); ImGui::TableHeadersRow();
        std::array<int, 256> priority, blacklist,buffs,debuffs,effect_priorities;
        parse_effect_priorities(config.find("combat_effect_priorities")->text_value,effect_priorities);
        parse_ids(config.find("combat_priority")->text_value, priority); parse_ids(config.find("combat_blacklist")->text_value, blacklist);
        parse_ids(config.find("combat_buff_overrides")->text_value,buffs); parse_ids(config.find("combat_debuff_overrides")->text_value,debuffs);
        for (unsigned id = 0; id < catalog.size(); ++id) if (catalog[id].valid) {
            ImGui::PushID(static_cast<int>(id)); ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::Text("%u: %s", id, catalog[id].name.data()); ImGui::TableNextColumn();
            if (ImGui::InputInt("##Priority",&effect_priorities[id],1,10)) {
                effect_priorities[id]=std::clamp(effect_priorities[id],0,1000);
                std::string text;
                for (unsigned n=0;n<256;++n) if (effect_priorities[n]!=100) {
                    if (!text.empty()) text+=',';
                    text+=std::to_string(n)+':'+std::to_string(effect_priorities[n]);
                }
                config.set_text("combat_effect_priorities",text.c_str());
            }
            ImGui::TableNextColumn();
            int value = blacklist[id] >= 0 ? 2 : (priority[id] >= 0 ? 1 : 0);
            const auto edit = [&](const char* key, std::array<int, 256>& ranks, bool add) {
                    ranks[id] = add ? 256 : -1;
                    std::string text; std::array<unsigned, 256> order{}; unsigned count = 0;
                    for (unsigned n = 0; n < 256; ++n) if (ranks[n] >= 0) order[count++] = n;
                    std::sort(order.begin(), order.begin() + count, [&](unsigned a, unsigned b) { return ranks[a] < ranks[b]; });
                    for (unsigned n = 0; n < count; ++n) { if (!text.empty()) text += ','; text += std::to_string(order[n]); }
                    config.set_text(key, text.c_str());
            };
            if (ImGui::Combo("##Visibility", &value, ui_choices("Normal\0Always show\0Never show\0"))) {
                edit("combat_priority", priority, value == 1); edit("combat_blacklist", blacklist, value == 2);
            }
            ImGui::TableNextColumn();
            int type=debuffs[id]>=0 ? 2 : buffs[id]>=0 ? 1 : 0;
            const char* choices=catalog[id].debuff ? "Default: Debuff\0Buff\0Debuff\0" : "Default: Buff\0Buff\0Debuff\0";
            if (ImGui::Combo("##Type",&type,ui_choices(choices))) {
                edit("combat_buff_overrides",buffs,type==1); edit("combat_debuff_overrides",debuffs,type==2);
            }
            ImGui::TableNextColumn();
            if (implementation_ && implementation_->ready[id]) ImGui::TextUnformatted(ui_text("Ready"));
            else if (implementation_ && implementation_->failed[id]) ImGui::TextUnformatted(ui_text("Missing"));
            else ImGui::TextUnformatted(ui_text("Streaming"));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
} // namespace owned
