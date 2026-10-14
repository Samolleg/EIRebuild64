#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace owned::camp {

// Camp layout coordinates use the game's centred 800x600 virtual space.
// These rectangles are shared by visual placement and native hit inversion.
struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    float width() const noexcept { return right - left; }
    float height() const noexcept { return bottom - top; }
    bool contains(float x, float y) const noexcept {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

struct Placement {
    Rect source, destination;
    int quarter_turn = 0;
};

// A draw-owned rectangle in primary output pixels, after visual placement.
struct Clip {
    Rect bounds;
    bool enabled = false;
    bool operator==(const Clip& r) const noexcept {
        return enabled == r.enabled && (!enabled ||
            (bounds.left == r.bounds.left && bounds.top == r.bounds.top &&
             bounds.right == r.bounds.right && bounds.bottom == r.bounds.bottom));
    }
};

struct Layout {
    bool wide = false, compact = false;
    float width = 800, height = 600, unit = 1, left = 0;
    float cell = 55, slot = 200.f / 3;
    int columns = 4;
    Rect merchant, stash, workspace, character, stats, belt, dock, actions, heading, title_plate;
    Rect ordinary_info, result_info, comparison_info, content, workbench, weapons, quick_items;
    Rect skills, learned, available;
    std::array<Rect,2> rotation;

    int grid_columns(bool vendor) const noexcept { return vendor ? 4 : columns; }
    int grid_rows(bool vendor) const noexcept { return vendor ? 4 : 8; }

    Rect grid(bool vendor) const noexcept {
        const auto& bank = vendor ? merchant : stash;
        const float x = bank.left + (bank.width() - grid_columns(vendor) * cell) * .5f;
        return {x, 200.f / 3, x + grid_columns(vendor) * cell, 200.f / 3 + grid_rows(vendor) * cell};
    }
    Rect inventory_cell(bool vendor, int visible) const noexcept {
        const auto r = grid(vendor);
        const int cols = grid_columns(vendor);
        const float x = r.left + (visible % cols) * cell;
        const float y = r.top + (visible / cols) * cell;
        return {x, y, x + cell, y + cell};
    }
    Rect category(bool vendor, int index) const noexcept {
        const auto& bank = vendor ? merchant : stash;
        constexpr float size = 80.f / 3;
        const float x = bank.left + (bank.width() - 6 * size) * .5f + index * size;
        // Leave the complete divider and its padding above the inventory.
        return {x, bank.top, x + size, bank.top + size};
    }
    Rect equipped(int index, int first, bool equipment = false) const noexcept {
        if(equipment){
            const auto& column=index<4?weapons:quick_items;
            const float y=column.top+(index%4)*250.f/3;
            return {column.left,y,column.right,y+220.f/3};
        }
        const float x=belt.left+(index-first)*(slot+5);
        return {x,belt.top,x+slot,belt.bottom};
    }
    Rect mode_button(int mode) const noexcept {
        // Native enum order differs from its original two-row arrangement.
        constexpr int columns_by_mode[] = {0, 4, 5, 6, 1, 2, 3, 7};
        const float w = dock.width() / 8;
        const float x = dock.left + columns_by_mode[mode] * w;
        const float y = dock.top;
        return {x, y, x + w, y + w * .75f};
    }
    Rect trade_cell(int native_index) const noexcept {
        const int side = native_index / 8, item = native_index % 8;
        const float size = workspace.width() / 4;
        const float x = workspace.left + (side * 2 + item % 2) * size;
        const float y = workspace.top + (item / 2) * size;
        return {x, y, x + size, y + size};
    }
};

inline Layout make_layout(float pixel_width, float pixel_height, float frame_size = 72) noexcept {
    Layout l;
    l.unit = pixel_height / 600;
    if (l.unit <= 0 || pixel_width <= 0) return l;
    l.width = pixel_width / l.unit;
    l.left = 400 - l.width * .5f;
    const float aspect = pixel_width / pixel_height;
    l.wide = aspect >= 16.f / 10.f - .001f;
    if (!l.wide) return l;
    l.compact = aspect < 16.f / 9.f - .001f;
    // Keep full-output coordinates above; cap only the interactive working area.
    const float working_width=(std::min)(l.width,1200.f);
    const float working_left=400-working_width*.5f;
    // Final 720-high proposal expressed in the game's 600-high virtual space.
    l.cell=frame_size*55.f/72;
    if(l.compact){
        // Keep the complete native 200-wide perk rows and their text/rail
        // gutters. Only the square inventory cells spend the narrower width.
        constexpr float perk_content=2*(200.f+40.f)+50.f/3;
        l.cell=(std::min)(l.cell,(working_width-730.f/3-35-perk_content)/4);
    }
    l.columns=working_width>=3500.f/3-.01f?6:4;
    l.merchant={working_left+10,20,working_left+10+4*l.cell,860.f/3};
    l.stash={working_left+working_width-20-l.columns*l.cell,20,working_left+working_width-20,570};
    l.content={working_left+730.f/3,160,l.stash.left-15,570};
    const float perk_half=(l.content.width()-50.f/3)*.5f;
    l.skills={l.content.left,160,l.content.left+perk_half,313};
    l.learned={l.content.left,317,l.content.left+perk_half,570};
    l.available={l.content.left+perk_half+50.f/3,160,l.content.right,570};
    const float middle=(l.content.left+l.content.right)*.5f;
    l.workspace={middle-200,250.f/3,middle+200,1450.f/3};
    const float construction_size=(std::min)(360.f,l.content.width()-625.f/3);
    l.workbench={l.content.left,250.f/3,l.content.left+construction_size,250.f/3+construction_size};
    l.character=l.workspace;
    if(l.compact){
        const float size=(std::min)(400.f,l.content.width()-500.f/3);
        const float y=300; // Enclose the full fixed-height equipment columns.
        l.character={middle-size*.5f,y-size*.5f,middle+size*.5f,y+size*.5f};
    }
    l.weapons={l.character.left-250.f/3,415.f/3,l.character.left-10,1385.f/3};
    l.quick_items={l.character.right+10,415.f/3,l.character.right+250.f/3,1385.f/3};
    // Native arrows remain baked into the lower character tiles. Picking uses
    // that same 200,100..600,500 character-canvas transform.
    const float character_x=l.character.width()/400,character_y=l.character.height()/400;
    l.rotation={Rect{l.character.left+100*character_x,l.character.top+370*character_y,
                     l.character.left+130*character_x,l.character.bottom},
                Rect{l.character.left+270*character_x,l.character.top+370*character_y,
                     l.character.left+300*character_x,l.character.bottom}};
    // Uniform fit preserves all native 200x300 stats in the square allotment.
    l.stats={working_left+140.f/3,200.f/3,working_left+580.f/3,860.f/3};
    if(l.compact)l.slot=(std::min)(l.slot,(l.content.width()-7*5)/8);
    const float spell_width=8*l.slot+7*5;
    l.belt={middle-spell_width*.5f,220.f/3,middle+spell_width*.5f,l.compact?220.f/3+l.slot:140};
    l.dock={middle-120,10,middle+120,32.5f};
    l.actions={middle-100,500,middle+100,600};
    // The native 200x30 surface contains centered text in its lower 10..30 band.
    // Keep its scale and center that band inside the shared plate.
    l.heading={middle-100,34,middle+100,64};
    l.title_plate={middle-150,37,middle+150,71};
    l.ordinary_info = {working_left + 26.6667f, 306.6667f, working_left + 213.3333f, 586.6667f};
    l.result_info = {l.content.right - 186.6667f, 83.3333f, l.content.right, 363.3333f};
    l.comparison_info={l.ordinary_info.right+20,l.ordinary_info.top,
        l.ordinary_info.right+20+l.ordinary_info.width(),l.ordinary_info.bottom};
    if(l.compact){
        // Two anchored cards leave the complete footer clear. Preserve the
        // native 200x300 aspect for text and item previews in all three slots.
        const float card_width=(std::min)(l.ordinary_info.width(),
            (l.actions.left-5-l.ordinary_info.left-20)*.5f);
        const float card_height=card_width*1.5f;
        l.ordinary_info={l.ordinary_info.left,l.ordinary_info.bottom-card_height,
                         l.ordinary_info.left+card_width,l.ordinary_info.bottom};
        l.comparison_info={l.ordinary_info.right+20,l.ordinary_info.top,
                           l.ordinary_info.right+20+card_width,l.ordinary_info.bottom};
        l.result_info={l.content.right-card_width,250.f/3,l.content.right,250.f/3+card_height};
    }
    return l;
}

// The renderer composes the game's completed transform. Depth, W, winding,
// lighting and UVs remain untouched. This is the same affine operation used
// by the project's owned HUD placement, restricted here to camp widgets.
struct Composition {
    float a = 1, b = 0, c = 0, d = 1, x = 0, y = 0;
    Clip clip;
    bool operator==(const Composition& r) const noexcept {
        return a == r.a && b == r.b && c == r.c && d == r.d && x == r.x && y == r.y && clip == r.clip;
    }
};

inline Composition composition(const Layout& l, const Placement& p) noexcept {
    const float sx = p.destination.width() / p.source.width();
    const float sy = p.destination.height() / p.source.height();
    const float source_x = (p.source.left + p.source.right) * .5f;
    const float source_y = (p.source.top + p.source.bottom) * .5f;
    const float target_x = (p.destination.left + p.destination.right) * .5f;
    const float target_y = (p.destination.top + p.destination.bottom) * .5f;
    Composition h;
    h.a = sx; h.d = sy;
    switch ((p.quarter_turn % 4 + 4) % 4) {
    case 1: h.a = h.d = 0; h.b = -sy; h.c = sx; break;
    case 2: h.a = -sx; h.d = -sy; break;
    case 3: h.a = h.d = 0; h.b = sy; h.c = -sx; break;
    }
    const float native_x = (source_x - l.left) * l.unit;
    const float native_y = source_y * l.unit;
    h.x = (target_x - l.left) * l.unit - h.a * native_x - h.b * native_y;
    h.y = target_y * l.unit - h.c * native_x - h.d * native_y;
    return h;
}

inline Composition clipped_composition(const Layout& l, const Placement& p,
                                       const Rect& source_clip) noexcept {
    auto h = composition(l, p);
    bool first = true;
    const float xs[]{source_clip.left, source_clip.right};
    const float ys[]{source_clip.top, source_clip.bottom};
    for (const float x : xs)
        for (const float y : ys) {
            const float native_x = (x - l.left) * l.unit, native_y = y * l.unit;
            const float px = h.a * native_x + h.b * native_y + h.x;
            const float py = h.c * native_x + h.d * native_y + h.y;
            if (first) { h.clip.bounds = {px, py, px, py}; first = false; }
            else {
                h.clip.bounds.left = (std::min)(h.clip.bounds.left, px);
                h.clip.bounds.top = (std::min)(h.clip.bounds.top, py);
                h.clip.bounds.right = (std::max)(h.clip.bounds.right, px);
                h.clip.bounds.bottom = (std::max)(h.clip.bounds.bottom, py);
            }
        }
    h.clip.enabled = true;
    return h;
}

inline void compose_clip(float* m, const Composition& h, float width, float height,
                         bool row_vectors = false) noexcept {
    const float tx = h.a + h.b * height / width + 2 * h.x / width - 1;
    const float ty = 1 - h.c * width / height - h.d - 2 * h.y / height;
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned xi = row_vectors ? i * 4 : i;
        const unsigned yi = row_vectors ? i * 4 + 1 : i + 4;
        const unsigned wi = row_vectors ? i * 4 + 3 : i + 12;
        const float x = m[xi], y = m[yi], w = m[wi];
        m[xi] = h.a * x - h.b * height / width * y + tx * w;
        m[yi] = -h.c * width / height * x + h.d * y + ty * w;
    }
}

inline thread_local Composition visual{};
inline thread_local bool composing = false;
inline thread_local bool remapping_blit = false;
inline thread_local bool primary_output = true;

struct VisualScope {
    Composition previous = visual;
    bool enabled = composing;
    VisualScope(const Composition& value, bool active) noexcept { visual = value; composing = active; }
    ~VisualScope() { visual = previous; composing = enabled; }
};
struct OutputScope {
    bool previous = primary_output;
    explicit OutputScope(bool output) noexcept { primary_output = output; }
    ~OutputScope() { primary_output = previous; }
};

} // namespace owned::camp
