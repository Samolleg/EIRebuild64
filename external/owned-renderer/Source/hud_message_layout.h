#pragma once
#include <windows.h>
#include "widescreen.h"
#include <cmath>
namespace owned {
inline RECT message_text_wrap(const RECT& rect,float scale) noexcept {
    return {0,0,(std::max)(LONG(1),LONG(std::floor(float(rect.right-rect.left-14)/scale))),
                (std::max)(LONG(1),LONG(std::floor(float(rect.bottom-rect.top)/scale)))};
}
struct MessageGeometry {
    struct Quad {float left=0,top=0,right=0,bottom=0;};
    Quad background,toolbar;
    RECT panel{},text{};
    float centre_x=0,frame_height=0,background_x=0,background_y=0;
};
// Exact native geometry used by refresh_message_layout. Slide is applied once
// by the renderer composition, never baked into native child rectangles.
template<class Header>
MessageGeometry message_geometry(const Header& header,float unit,float canvas_unit) noexcept {
    MessageGeometry g;
    const auto content=[&](const HudPanelBounds& b) {
        const float left=180.f+(b.left-header.panel.left)/unit,top=(b.top-header.panel.top)/unit;
        return MessageGeometry::Quad{left,top,left+b.width/unit,top+b.height/unit};
    };
    g.background=content(header.background);
    g.background_x=(g.background.left+g.background.right)*.5f;
    g.background_y=(g.background.top+g.background.bottom)*.5f;
    const auto body=content(header.body);
    const float width=header.panel.width/unit;
    g.frame_height=header.panel.height/unit;g.centre_x=180.f+width*.5f;
    const float w=header.width/(canvas_unit*header.scale),h=header.height/(canvas_unit*header.scale);
    g.toolbar={g.centre_x-w*.5f,10.f-h*.5f,g.centre_x+w*.5f,10.f+h*.5f};
    g.panel={180,0,180+std::lround(width),std::lround(g.frame_height)};
    g.text={std::lround(body.left)+10,std::lround(body.top)+5,std::lround(body.right)-10,std::lround(body.bottom)-11};
    return g;
}
}
