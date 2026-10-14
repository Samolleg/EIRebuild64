#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <d3d11.h>

namespace owned {
class RendererConfig;
namespace widescreen { struct HudComposition; }

struct CombatEffectValue {
    std::uint8_t id;
    std::int32_t ticks;
    float amount = 0;
    bool amount_available = false;
    bool whole_amount = false;
};

// Completed gameplay values. No borrowed engine address survives publication.
struct CombatUnitValue {
    std::int32_t id = 0;
    std::uint64_t incarnation = 0;
    int health = 0, maximum_health = 0;
    bool dead = false;
    std::array<std::int32_t, 3> attack_targets{};
    bool attacking = false;
    std::uint16_t effect_count = 0;
    // Only [0, effect_count) is initialized and consumed.
    std::array<CombatEffectValue, 256> effects;
};
struct CombatEffectDefinition {
    bool valid = false;
    bool hd_icon = false;
    bool debuff = false;
    std::uint32_t icon = 0;
    std::array<char, 64> name{};
};
struct CombatPortraitAnchor {
    std::int32_t id = 0;
    float left = 0, top = 0, right = 0, bottom = 0;
};

void combat_ui_publish_unit(const CombatUnitValue& value) noexcept;
void combat_ui_retire_unit(std::int32_t id) noexcept;
void combat_ui_publish_catalog(const std::array<CombatEffectDefinition, 256>& catalog) noexcept;
void combat_ui_publish_font(const void* bytes, std::size_t size, std::uint64_t generation) noexcept;
void combat_ui_set_mission(bool active) noexcept;
void combat_ui_set_options(const RendererConfig& config) noexcept;
void combat_ui_publish_paused(bool paused) noexcept;
void combat_ui_publish_hover(std::int32_t id, std::uint64_t incarnation) noexcept;
void combat_ui_set_status(const char* status) noexcept;
void combat_ui_publish_plate(std::int32_t id, float x, float y) noexcept;
void combat_ui_publish_portraits(const CombatPortraitAnchor* anchors, std::size_t count) noexcept;
void combat_ui_publish_spell_list(std::int32_t id, std::uint64_t incarnation,
    std::uint64_t epoch, std::uint64_t frame, const RECT& local_viewport) noexcept;
void combat_ui_place_spell_list(std::uint64_t epoch, std::uint64_t frame,
    const RECT& destination, const RECT& source, const widescreen::HudComposition& composition,
    unsigned canvas_width, unsigned canvas_height) noexcept;
bool combat_ui_has_output() noexcept;
bool combat_ui_needs_plate(std::int32_t id) noexcept;
void register_combat_ui_options(RendererConfig& config);

// One icon atlas and the existing owned UI backend; background I/O never calls
// an engine resource manager or the immediate D3D context.
class CombatUiRenderer {
public:
    CombatUiRenderer();
    ~CombatUiRenderer();
    void initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void shutdown() noexcept;
    bool prepare(RendererConfig& config);
    void draw(RendererConfig& config, bool interactive = true);
    void draw_settings(RendererConfig& config);
    bool process_input(UINT message, WPARAM wparam, LPARAM lparam) noexcept;
private:
    struct Implementation;
    std::unique_ptr<Implementation> implementation_;
};
} // namespace owned
