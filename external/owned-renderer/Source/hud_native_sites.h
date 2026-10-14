// Stock HUD panel entries the bridge hooks when no addon yields a HUD
// contract (vanilla game). With an addon the addon's own panel wrappers are
// hooked instead. These sites form the hud_native group in game_sites.h.
#pragma once

#include <cstddef>
#include <cstdint>

namespace owned {

// thiscall SetUnit(unit) of the weapon and spell panels (RET 4) and the
// skill panel's SetUnit with two stack arguments (RET 8), which the hook
// forwards unchanged. Each reaches its stock
// producer lane (kHudStockProducerCallRvas) through the panel's Add routine.
constexpr std::uint32_t kHudNativeWeaponSetUnitRva = 0x0023c180;
constexpr std::uint32_t kHudNativeSpellSetUnitRva = 0x0023e820;
constexpr std::uint32_t kHudNativeSkillSetUnitRva = 0x0023f800;
// Exception-handler operands pushed by those prologues.
constexpr std::uint32_t kHudNativeWeaponHandlerRva = 0x0032dee8;
constexpr std::uint32_t kHudNativeSpellHandlerRva = 0x0032e080;
constexpr std::uint32_t kHudNativeSkillHandlerRva = 0x0032e138;
// Bytes each entry hook replaces: push -1 / push handler (7) for weapon and
// spell; mov eax, fs:[0] (6) for skill.
constexpr std::size_t kHudNativeWeaponHookBytes = 7;
constexpr std::size_t kHudNativeSpellHookBytes = 7;
constexpr std::size_t kHudNativeSkillHookBytes = 6;

} // namespace owned
