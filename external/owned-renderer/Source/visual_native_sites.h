// Stock unit-appearance sites the bridge uses when no addon yields a
// visual-publication contract (vanilla game). With an addon, its own
// ChangeWeapon/LoadTextures/LoadFigures/compose hooks are used instead.
// These sites form the visual_native group in game_sites.h.
#pragma once

#include <cstdint>

namespace owned {

// thiscall CUnitClientSpecific::ChangeWeapon(weapon), RET 4, returns 1. It
// loads each weapon part through LoadFigure; that call returns here.
constexpr std::uint32_t kVisualNativeChangeWeaponRva = 0x00135590;
constexpr std::uint32_t kVisualNativeWeaponLoadFigureReturnRva = 0x001356e0;
// thiscall CObject3DClientSpecific::LoadTextures with two stack arguments,
// RET 8, returns 1; and the handler operand its prologue pushes.
constexpr std::uint32_t kVisualNativeLoadTexturesRva = 0x0011c250;
constexpr std::uint32_t kVisualNativeLoadTexturesHandlerRva = 0x0031d998;
// The stock animation receiver test (no addon receiver jump) and the
// texture resource Get slot, which holds the native Get directly.
constexpr std::uint32_t kVisualNativeReceiverRva = 0x0012f3a9;
constexpr std::uint32_t kVisualNativeResourceGetSlotRva = 0x003432f4;
// Stock animation-rate test in SetExactPosition: mode 2 scales by the unit
// coefficient (cmp eax,2 / jne). The addon rewrites it to cmp eax,1 / je.
constexpr std::uint32_t kVisualNativeAnimationRateTestRva = 0x00135c71;
// thiscall CTextureManager::ComposeTexture(result, set, desired, alternate,
// mips, background, format), RET 1Ch: the stock character atlas composer
// (material classes read its layer list; the addon redirects its callers).
constexpr std::uint32_t kVisualStockComposeRva = 0x00293180;

} // namespace owned
