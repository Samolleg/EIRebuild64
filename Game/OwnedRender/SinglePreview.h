#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace OwnedRender {

struct PreviewRotation {
    std::array<float,3> axis{};
    float angle = 0;
};
struct SinglePreviewPlacement {
    float x = 0, y = 0, z = 0, scale = 1, ambient = 1;
    std::uint32_t specular = 0xff000000u;
    std::array<PreviewRotation,2> rotations{};
    unsigned rotation_count = 0;
};
struct SinglePreviewFacts {
    float x = 0, y = 0, z = 0, size = 0, viewing_tangent = 0;
    float elapsed = 0;
    bool slot = false, hovered = false, flat = false, weapon = false;
    bool instruction = false, gray_in_front = false;
    bool pulse = false, valid_palette = false;
    std::array<float,3> palette{};
    std::array<float,2> bounds_max{}, bounds_center{};
    unsigned rune_count = 0;
};
struct SinglePreviewRotationState { float angle = 0, speed = 0, phase = 0; };
struct PreviewBar {
    float z, left, top, right, bottom;
    bool gray = false;
};
struct PreviewRune {
    float x, y, z, scale, ambient, u, v;
};

// The complete single-figure presentation producer. Adapters provide current
// item classification, the existing rotation state and bounded resource/draw
// operations. No item pointer, allocator, RNG or callback escapes this call.
template<class Adapter>
void produce_single_item_preview(const SinglePreviewFacts& f,
    SinglePreviewRotationState& state, Adapter& adapter) {
    constexpr float pi = 3.14159265358979323846f;
    SinglePreviewPlacement p;
    p.x=f.x; p.y=f.y; p.z=f.z;
    p.scale=f.z*f.viewing_tangent*f.size*.75f/800;
    if (f.slot) {
        if (f.pulse) {
            if (f.valid_palette) {
                // Stock slot arithmetic stores the phase before evaluating sin.
                // Part9 uses advance_model_preview_glow's native double path.
                state.phase=float(std::fmod(state.phase+f.elapsed*5.0,2.0*3.141592653589793));
                const float strength=float(.15f+std::sin(state.phase)*.15f);
                p.specular=0xff000000u | (std::uint32_t(f.palette[0]*strength*255.f)<<16) |
                    (std::uint32_t(f.palette[1]*strength*255.f)<<8) |
                    std::uint32_t(f.palette[2]*strength*255.f);
            } else state.phase=0;
        }
        if (f.hovered) state.speed+=f.elapsed*8.f;
        else if (state.speed<.1f) state.angle=state.speed=0;
        else state.speed=(2*pi-state.angle)*4.f;
        if (state.speed>5.f) state.speed=5.f;
        state.angle+=f.elapsed*state.speed;
        while (state.angle>2*pi) state.angle-=2*pi;
        if (f.flat) p.rotations[p.rotation_count++]={{0,0,1},pi};
        else {
            if (f.weapon) p.rotations[p.rotation_count++]={{1,1,0},state.angle};
            else if (!f.instruction) p.rotations[p.rotation_count++]={{0,1,0},state.angle};
            p.rotations[p.rotation_count++]={{1,0,0},pi};
        }
    } else {
        p.ambient=.5f;
        p.rotations[p.rotation_count++]={f.flat?std::array<float,3>{0,0,1}:std::array<float,3>{1,0,0},pi};
        p.x=f.x-f.bounds_center[0]*f.size/2;
        p.y=f.y+f.bounds_center[1]*f.size/2;
        p.scale=f.z*f.viewing_tangent*f.size/800/(std::max)(f.bounds_max[0],f.bounds_max[1]);
    }
    adapter.place(p);
    adapter.rotation(state);
    if (f.slot) adapter.bar(PreviewBar{f.z*1.3f,f.x-35,f.y-35,f.x+35,f.y+35});
    adapter.draw_item();
    if (f.rune_count) {
        // Readback follows the established widescreen ingress of the figure;
        // the source adapter must not apply its anchor twice to the base point.
        float x,y,z,scale;
        adapter.position(x,y,z,scale);
        for (unsigned i=0;i<f.rune_count;++i) {
            const int rune=adapter.rune_index(i)-1;
            float dx=550*scale/z,dy=550*scale/z;
            if (i==0||i==4) dx=0;
            else if (i==5||i==6||i==7) dx=-dx;
            if (i==2||i==6) dy=0;
            else if (i==0||i==1||i==7) dy=-dy;
            adapter.rune(PreviewRune{x+dx,y+dy,z*.9f,scale,f.slot?1.f:.5f,
                (rune&7)/8.f,-(rune>>3)/8.f});
        }
        adapter.end_runes();
    }
    if (f.slot&&adapter.grayed())
        adapter.bar(PreviewBar{f.z*(f.gray_in_front?.8f:1.25f),f.x-35,f.y-35,f.x+35,f.y+35,true});
}

template<class Adapter>
void produce_single_head_preview(float x,float y,float z,float size,float viewing_tangent,
    Adapter& adapter) {
    constexpr float pi=3.14159265358979323846f;
    SinglePreviewPlacement p;
    p.x=x;p.y=y;p.z=z;p.scale=z*viewing_tangent*size*.75f/800;
    p.rotations[0]={{-1,0,0},(y-300)/1000};
    p.rotations[1]={{1,0,0},pi};p.rotation_count=2;
    adapter.place(p);
    adapter.bar(PreviewBar{z*1.3f,x-35,y-35,x+35,y+35});
    adapter.draw_head();
}
} // namespace OwnedRender
