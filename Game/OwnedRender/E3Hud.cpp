#include "StdAfx.h"
#include "E3Hud.h"
#include "../I3DScreenMission.h"
#include "../I3DObject.h"
#include "hud_layout_context.h"
#include <algorithm>

// This adapter only copies facts owned by the active mission. Slot geometry,
// header docking, scaling and preview layout remain in the renderer library.
struct E3HudAccess {
    static void Copy(owned::HudNativePanel& dst,const CI3DObject* object) {
        if(!object || !object->bVisible)return;
        dst.visible=true;dst.rect=object->rect;
        dst.slots.resize(object->areas.GetSize());
        for(int i=0;i<object->areas.GetSize();++i) {
            if(object->areas[i]) {
                dst.slots[i].rect=object->areas[i]->rect;
                dst.slots[i].shift=object->areas[i]->shift;
            }
        }
    }
    static void Shell(owned::HudNativePanel& dst,const CI3DObject* object,unsigned frame,unsigned arrow) noexcept {
        if(object && unsigned(object->figures.GetSize())>frame && unsigned(object->figures.GetSize())>arrow &&
           object->figures[frame] && object->figures[arrow])
            dst.shell_visible=object->figures[frame]->IsVisible() || object->figures[arrow]->IsVisible();
    }
    static void PortraitFrames(owned::HudNativePanel& dst,const CI3DObject* object) noexcept {
        const unsigned figures=unsigned(object->figures.GetSize());
        Slots(dst,figures?(figures-1)/5:0);
        for(unsigned i=0;i<dst.native_count;++i)
            dst.slots[i].selection_frame=object->figures[5*i+4] && object->figures[5*i+4]->IsVisible();
    }
    static float Slide(int current,int total) noexcept {
        return total>0?std::clamp(float(current)/float(total),0.f,1.f):0.f;
    }
    static void Slots(owned::HudNativePanel& dst,int count) noexcept {
        dst.native_count=(std::min)(unsigned((std::max)(0,count)),unsigned(dst.slots.size()));
        for(unsigned i=0;i<dst.native_count;++i)dst.slots[i].active=true;
    }
    static void Publish(const CI3DScreenMission* mission,unsigned width,unsigned height) {
        owned::HudLayoutFrame frame;
        // Current host ingress is full-width, unscaled placed space. Do not
        // announce a different safe area until its drawing/input consumer exists.
        frame.layout=owned::widescreen::make_layout(float(width),float(height));
        frame.active=mission && width && height;
        if(frame.active) {
            auto& p=frame.panels;
            Copy(p[0],mission->pSpells);Copy(p[1],mission->pSkills);
            Copy(p[2],mission->pMap);Copy(p[3],mission->pInfo);
            Copy(p[4],mission->pFaces);Copy(p[5],mission->pWeapons);
            Copy(p[6],mission->pItems);Copy(p[7],mission->pOutput);
            Copy(p[8],mission->pCommands);Copy(p[9],mission->pClock);
            Slots(p[0],mission->pSpells->nSpells);Slots(p[1],mission->pSkills->nSkills);
            PortraitFrames(p[4],mission->pFaces);Slots(p[5],mission->pWeapons->figures.GetSize());
            for(unsigned i=0;i<(std::min)(unsigned(p[6].slots.size()),8u);++i) {
                const int figure=mission->pItems->nSubst[i];
                if(figure>=0)p[6].native_count=i+1;
                p[6].slots[i].active=figure>=0 && figure<mission->pItems->figures.GetSize();
            }
            Shell(p[2],mission->pMap,9,5);
            Shell(p[3],mission->pInfo,7,2);
            Shell(p[7],mission->pOutput,5,2);
            p[2].slide=Slide(mission->pMap->slide_nCurPixels,mission->pMap->slide_nTotalPixels);
            p[3].slide=Slide(mission->pInfo->slide_nCurPixels,mission->pInfo->slide_nTotalPixels);
            p[7].slide=Slide(mission->pOutput->slide_nCurPixels,mission->pOutput->slide_nTotalPixels);
        }
        owned::embedded_hud_layout().publish(std::move(frame));
    }
};
namespace OwnedRender {
void E3PublishHud(const CI3DScreenMission* mission,unsigned width,unsigned height) noexcept {
    try { E3HudAccess::Publish(mission,width,height); }
    catch(const std::bad_alloc&) { owned::embedded_hud_layout().retire(); }
}
}
