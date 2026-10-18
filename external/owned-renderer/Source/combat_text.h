#pragma once

// Floating combat text (damage numbers, backstab/critical/experience labels
// and the briefing marker), owned by the renderer. The game keeps the damage
// list (CFlyingHitPoints, world +0x1B4: texture +0, STLport list +4 of
// {unit id, amount, flags, time}) and the units; the renderer reads them at the
// verified Draw call (0x0056A337) and lays out the glyph quads the stock
// CFlyingHitPoints::Draw (0x005E18E0) built, drawn as GPU-expanded quads
// (gameGlyphVS, game_particles.h). Stock rules (shipped bytes):
// - progress = (int)(now - time) * (float)(1/3000); outside [0, 1] draws
//   nothing; time is game time (the receive and draw clocks both read the
//   game clock, so the text freezes on pause and follows game speed);
// - briefing entries (flag bit 31): 32x10 quad centred 20 px above the
//   anchor, opaque yellow, UV (0.5, 1)-(1, 1 - 20/128), shifted by 1/256;
// - damage entries: anchor + (trunc(120 p), trunc(-100 p)); "%d" digits
//   8x12 from a 4-wide digit grid (u = (d & 3) / 8, v = 1 - (d >> 2) * 20/128);
//   backstab (1), critical (2) and experience (8) add 64x10 labels below
//   (v 59/78, 78/97, 97/116 of 128) and widen the box to 64; the box is
//   centred on the anchor; colour (64,255,255) for experience, else
//   (255,64,64) for an owned unit and (64,255,64) for others, alpha
//   trunc((1 - p) * 255); UVs shifted by 1/512;
// - an entry whose box leaves the screen (w, h of the current mode) is not
//   drawn; each entry's quads scale by combat_text_scale around their own
//   bounding-box centre (the renderer's independent text size setting).

#include "game_particles.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace owned {

constexpr std::uint32_t kCombatTextBackstab = 1u, kCombatTextCritical = 2u, kCombatTextExperience = 8u;
constexpr std::uint32_t kCombatTextBriefing = 0x80000000u;
constexpr float kCombatTextProgressPerMs = 0.00033333333f;  // the stock float constant (0x73D61C)

struct CombatTextEntry {
    int anchor_x = 0, anchor_y = 0;  // screen anchor before the drift
    int amount = 0;
    std::uint32_t flags = 0;
    float progress = 0;              // [0, 1]
    bool owned_unit = false;
};

// One glyph quad: corners 0 (x, y), 1 (x + w, y), 2 (x, y + h), 3 (x + w, y + h)
// with UVs (u0, v0) (u1, v0) (u0, v1) (u1, v1); stock triangles (0,2,1) (1,2,3).
inline void combat_text_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                             float uv_shift, std::uint32_t color, std::vector<GameParticleGpu>& out) {
    GameParticleGpu g{};
    g.center_radius[0] = x; g.center_radius[1] = y; g.center_radius[2] = w; g.center_radius[3] = h;
    g.uv[0] = u0 + uv_shift; g.uv[1] = v0 - uv_shift; g.uv[2] = u1 + uv_shift; g.uv[3] = v1 - uv_shift;
    g.color = color;
    out.push_back(g);
}

// Scales the quads [first, end) around their joint bounding-box centre.
inline void combat_text_scale_group(std::vector<GameParticleGpu>& out, std::size_t first, float scale) {
    if (scale == 1.0f || first >= out.size()) return;
    float x0 = out[first].center_radius[0], y0 = out[first].center_radius[1], x1 = x0, y1 = y0;
    for (std::size_t i = first; i < out.size(); ++i) {
        const float* q = out[i].center_radius;
        x0 = (std::min)(x0, q[0]); y0 = (std::min)(y0, q[1]);
        x1 = (std::max)(x1, q[0] + q[2]); y1 = (std::max)(y1, q[1] + q[3]);
    }
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    for (std::size_t i = first; i < out.size(); ++i) {
        float* q = out[i].center_radius;
        q[0] = cx + scale * (q[0] - cx); q[1] = cy + scale * (q[1] - cy);
        q[2] *= scale; q[3] *= scale;
    }
}

// Appends the stock quads of one entry; false when the stock rules draw none.
inline bool combat_text_layout(const CombatTextEntry& e, float screen_w, float screen_h, float scale,
                               std::vector<GameParticleGpu>& out) {
    const std::size_t first = out.size();
    if (e.flags & kCombatTextBriefing) {
        const float x = static_cast<float>(e.anchor_x) - 16.0f, y = static_cast<float>(e.anchor_y - 20) - 5.0f;
        if (x < 0 || x + 32.0f > screen_w || y < 0 || y + 10.0f > screen_h) return false;
        combat_text_quad(x, y, 32, 10, 0.5f, 1.0f, 1.0f, 1.0f - 20.0f / 128.0f, 1.0f / 256.0f, 0xffffff00u, out);
        combat_text_scale_group(out, first, scale);
        return true;
    }
    const int px = e.anchor_x + static_cast<int>(e.progress * 120.0f);
    const int py = e.anchor_y + static_cast<int>(e.progress * -100.0f);
    char digits[16];
    const int length = std::snprintf(digits, sizeof(digits), "%d", e.amount);
    if (length <= 0) return false;
    int width = length * 8, height = 12;
    if (e.flags & kCombatTextBackstab) { width = (std::max)(width, 64); height += 10; }
    if (e.flags & kCombatTextCritical) { width = (std::max)(width, 64); height += 10; }
    if (e.flags & kCombatTextExperience) { width = (std::max)(width, 64); height += 10; }
    const float xx = static_cast<float>(px) - static_cast<float>(width) * 0.5f;
    float y = static_cast<float>(py) - static_cast<float>(height) * 0.5f;
    if (xx < 0 || xx + static_cast<float>(width) > screen_w || y < 0 || y + static_cast<float>(height) > screen_h)
        return false;
    const std::uint32_t alpha = static_cast<std::uint32_t>(static_cast<int>((1.0f - e.progress) * 255.0f)) & 0xffu;
    const std::uint32_t rgb = (e.flags & kCombatTextExperience) ? 0x40ffffu : (e.owned_unit ? 0xff4040u : 0x40ff40u);
    const std::uint32_t color = alpha << 24 | rgb;
    constexpr float shift = 1.0f / 512.0f, row = 20.0f / 128.0f;
    float x = xx + static_cast<float>(width - length * 8) * 0.5f;
    for (int i = 0; i < length; ++i, x += 8.0f) {
        const int d = digits[i] - '0';
        if (d < 0 || d > 9) return false;
        const float tu = static_cast<float>(d & 3) * 0.125f, tv = 1.0f - static_cast<float>(d >> 2) * row;
        combat_text_quad(x, y, 8, 12, tu, tv, tu + 0.125f, tv - row, shift, color, out);
    }
    y += 12.0f;
    const auto label = [&](float top, float bottom) {
        combat_text_quad(xx + static_cast<float>(width - 64) * 0.5f, y, 64, 10, 0.0f, 1.0f - top / 128.0f, 1.0f,
                         1.0f - bottom / 128.0f, shift, color, out);
        y += 10.0f;
    };
    if (e.flags & kCombatTextBackstab) label(59.0f, 78.0f);
    if (e.flags & kCombatTextCritical) label(78.0f, 97.0f);
    if (e.flags & kCombatTextExperience) label(97.0f, 116.0f);
    combat_text_scale_group(out, first, scale);
    return true;
}

// The Deja Vu addon's combat text design (adapted under the addon author's
// graphics permission; matches ref-addon-ls EIClasses/FlyingHP.h at
// 39454a3a). Used when the addon's hook framework chains the stock Draw entry
// (it replaces Draw); the renderer takes the call before that chain, so this
// is the only layout drawn. Rules of that design:
// - progress = (float)(int)(now - time) / 3000 (division);
// - no briefing special case and briefing entries are not erased each frame
//   (they expire with the 3 s rule); a briefing flag only reserves 64x10;
// - icons for crit (32), parry (64), block (128), chest (256), potion (512),
//   avoid (1024), in that order, 22.5x23 each at x - 2, left of the digits,
//   from the atlas rectangles below; they count toward the box width unless
//   (crit and amount < 1) or (parry and amount > 0);
// - digits 15x23, u = (d & 3) / 8, v = 1 - (d >> 2) * 20/128, height 19/128;
//   amount < 1 draws no digits and zeroes the box before the labels;
// - labels 64x10 at rows 3/4/5 of 20/128 (backstab, critical, experience),
//   19/128 tall;
// - colour: experience (64,255,255); otherwise, owned unit: crit/parry/avoid/
//   block (255,74,74), chest or potion white, else (255,64,64); other units:
//   crit/parry/avoid/block (74,255,74), chest or potion white, else
//   (64,255,64); alpha trunc((1 - p) * 255); UVs shifted by 1/512.
constexpr std::uint32_t kAddonTextCrit = 32u, kAddonTextParry = 64u, kAddonTextBlock = 128u;
constexpr std::uint32_t kAddonTextChest = 256u, kAddonTextPotion = 512u, kAddonTextAvoid = 1024u;

inline bool combat_text_layout_addon(const CombatTextEntry& e, float screen_w, float screen_h, float scale,
                                     std::vector<GameParticleGpu>& out) {
    const std::size_t first = out.size();
    constexpr int glyph_w = 15, glyph_h = 23;
    constexpr float row = 20.0f / 128.0f, glyph_v = 19.0f / 128.0f, shift = 1.0f / 512.0f;
    struct Icon { std::uint32_t flag; float u, v, w; };
    static constexpr Icon icons[6] = {
        {kAddonTextCrit, 263.0f / 512.0f, 433.0f / 512.0f, 71.0f / 512.0f},
        {kAddonTextParry, 336.0f / 512.0f, 431.0f / 512.0f, 71.0f / 512.0f},
        {kAddonTextBlock, 412.0f / 512.0f, 436.0f / 512.0f, 84.0f / 512.0f},
        {kAddonTextChest, 263.0f / 512.0f, 357.0f / 512.0f, 81.0f / 512.0f},
        {kAddonTextPotion, 345.0f / 512.0f, 356.0f / 512.0f, 81.0f / 512.0f},
        {kAddonTextAvoid, 336.0f / 512.0f, 431.0f / 512.0f, 71.0f / 512.0f}};
    int icon_count = 0;
    for (const Icon& icon : icons) icon_count += (e.flags & icon.flag) ? 1 : 0;
    if (((e.flags & kAddonTextCrit) && e.amount < 1) || ((e.flags & kAddonTextParry) && e.amount > 0)) icon_count = 0;
    const int px = e.anchor_x + static_cast<int>(e.progress * 120.0f);
    const int py = e.anchor_y + static_cast<int>(e.progress * -100.0f);
    char digits[16];
    int length = std::snprintf(digits, sizeof(digits), "%d", e.amount);
    if (length <= 0) return false;
    int width = (length + icon_count) * glyph_w, height = glyph_h;
    if (e.amount < 1) { length = 0; width = 0; height = 0; }
    if (e.flags & kCombatTextBackstab) { width = (std::max)(width, 64); height += 10; }
    if (e.flags & kCombatTextCritical) { width = (std::max)(width, 64); height += 10; }
    if (e.flags & kCombatTextExperience) { width = (std::max)(width, 64); height += 10; }
    if (e.flags & kCombatTextBriefing) { width = (std::max)(width, 64); height += 10; }
    const float xx = static_cast<float>(px) - static_cast<float>(width) / 2.0f;
    float y = static_cast<float>(py) - static_cast<float>(height) / 2.0f;
    if (xx < 0 || xx + static_cast<float>(width) > screen_w || y < 0 || y + static_cast<float>(height) > screen_h)
        return false;
    const std::uint32_t alpha = static_cast<std::uint32_t>(static_cast<int>((1.0f - e.progress) * 255.0f)) & 0xffu;
    const bool marked = (e.flags & (kAddonTextCrit | kAddonTextParry | kAddonTextAvoid | kAddonTextBlock)) != 0;
    const bool white = (e.flags & (kAddonTextChest | kAddonTextPotion)) != 0;
    std::uint32_t rgb;
    if (e.flags & kCombatTextExperience) rgb = 0x40ffffu;
    else if (e.owned_unit) rgb = marked ? 0xff4a4au : white ? 0xffffffu : 0xff4040u;
    else rgb = marked ? 0x4aff4au : white ? 0xffffffu : 0x40ff40u;
    const std::uint32_t color = alpha << 24 | rgb;
    float x = xx + static_cast<float>(width - (length + icon_count) * glyph_w) / 2.0f;
    for (const Icon& icon : icons) {
        if (!(e.flags & icon.flag)) continue;
        combat_text_quad(x - 2.0f, y, glyph_w * 1.5f, glyph_h, icon.u, icon.v, icon.u + icon.w, icon.v - glyph_v,
                         shift, color, out);
        x += glyph_w * 1.5f;
    }
    for (int i = 0; i < length; ++i, x += glyph_w) {
        const int d = digits[i] - '0';
        if (d < 0 || d > 9) return false;
        const float tu = static_cast<float>(d & 3) * 0.125f, tv = 1.0f - static_cast<float>(d >> 2) * row;
        combat_text_quad(x, y, glyph_w, glyph_h, tu, tv, tu + 0.125f, tv - glyph_v, shift, color, out);
    }
    y += glyph_h;
    const auto label = [&](int label_row) {
        const float tv = 1.0f - static_cast<float>(label_row) * row;
        combat_text_quad(xx + static_cast<float>(width - 64) / 2.0f, y, 64, 10, 0.0f, tv, 1.0f, tv - glyph_v,
                         shift, color, out);
        y += 10.0f;
    };
    if (e.flags & kCombatTextBackstab) label(3);
    if (e.flags & kCombatTextCritical) label(4);
    if (e.flags & kCombatTextExperience) label(5);
    combat_text_scale_group(out, first, scale);
    return out.size() > first;
}

}  // namespace owned
