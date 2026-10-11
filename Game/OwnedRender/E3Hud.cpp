#include "StdAfx.h"
#include "E3Hud.h"
#include "E3Input.h"
#include "../I3DScreenMission.h"
#include "../I3DObject.h"
#include "../Unit.h"
#include "../Item.h"
#include "hud_layout_context.h"
#include <algorithm>
#include <cstring>

namespace {
std::array<const CI3DObject*,10> inputOwners{};
}
// This adapter only copies facts owned by the active mission. Slot geometry,
// header docking, scaling and preview layout remain in the renderer library.
struct E3HudAccess {
    static int Panel(const CI3DObject* object) noexcept {
        if(!object || !OwnedRender::E3HudRendererAttached())return -1;
        for(unsigned i=0;i<inputOwners.size();++i)if(inputOwners[i]==object)return int(i);
        return -1;
    }
    static bool AreaInside(CI3DObject* object,unsigned area,POINT pixel,bool& inside) noexcept {
        const int panel=Panel(object);if(panel<0)return false;
        inside=false;POINT native{};
        if(object->bVisible && area<unsigned(object->areas.GetSize()) && object->areas[area] &&
            owned::embedded_hud_layout().area_point(owned::HudPanel(panel),area,pixel,native))
            inside=object->areas[area]->PtInArea(CPoint(native))!=FALSE;
        return true;
    }
    static int Hit(CI3DObject* object,POINT pixel) noexcept {
        if(Panel(object)<0)return -2;
        for(int i=0;i<object->areas.GetSize();++i) {
            bool inside=false;AreaInside(object,unsigned(i),pixel,inside);
            if(inside)return i;
        }
        return -1;
    }
    static bool Inside(CI3DObject* object,POINT pixel,bool& inside) noexcept {
        const int index=Panel(object);if(index<0)return false;
        inside=false;if(!object->bVisible)return true;
        auto& context=owned::embedded_hud_layout();
        const auto panel=owned::HudPanel(index);
        // Slots may have gaps or be preview-only. Never swallow world input there.
        if(context.slot_panel(unsigned(index))) {inside=Hit(object,pixel)>=0;return true;}
        owned::widescreen::HudComposition composition;POINT native{};
        if(context.body_composition(panel,composition) && context.inverse_composed_point(pixel,composition,native))
            inside=object->rect.PtInRect(CPoint(native))!=FALSE;
        if(!inside)inside=Hit(object,pixel)>=0;
        return true;
    }
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
    static std::uint64_t FloatBits(float value) noexcept {
        std::uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));return bits;
    }
    static void UnitVitals(std::vector<std::uint64_t>& values,const CUnit* unit) {
        if(!unit)return;
        values.insert(values.end(),{std::uint64_t(unit->GetID()),std::uint64_t(unit->GetHP()),std::uint64_t(unit->GetMaxHP()),
            std::uint64_t(unit->GetMP()),std::uint64_t(unit->GetMaxMP())});
    }
    static void Activity(owned::HudLayoutFrame& frame,const CI3DScreenMission* mission) {
        auto& p=frame.panels;
        p[0].activity_values={std::uint64_t(mission->pSpells->iSelected)};
        p[1].activity_values={std::uint64_t(mission->pSkills->iSelected)};
        p[2].activity_values={FloatBits(mission->pMap->fXCenter),FloatBits(mission->pMap->fYCenter)};
        if(p[3].slide<1.f) {
            const auto* unit=mission->pInfo->pTempUnit?mission->pInfo->pTempUnit:mission->pInfo->pUnit;
            UnitVitals(p[3].activity_values,unit);
            if(unit)for(const auto& limb:unit->GetUserUnitStats()->hls)
                p[3].activity_values.insert(p[3].activity_values.end(),{std::uint64_t(limb.state),limb.wHP,limb.wMaxHP});
            p[3].activity_available=true;
        }
        for(unsigned i=0;i<(std::min)(unsigned(mission->pUnits.GetSize()),p[4].native_count);++i) {
            const auto* unit=mission->pUnits[i];
            if(!unit)continue;
            UnitVitals(p[4].activity_values,unit);
            p[4].activity_values.push_back(unit->CheckFlag(OBJ_SELECTED)!=0);
        }
        p[5].activity_values={std::uint64_t(mission->pWeapons->iSelected)};
        p[6].activity_values={std::uint64_t(mission->pItems->iSelected)};
        for(unsigned i=0;i<4;++i) {
            // Resolve the current quick item; cached UI descriptors can outlive it.
            auto* item=mission->pItems->pUnit?mission->pItems->pUnit->GetQItem(i):nullptr;
            p[6].activity_values.insert(p[6].activity_values.end(),{item?item->GetID():0u,item?std::uint64_t(item->GetCount()):0u});
            if(item && item->GetSpell(0) && item->GetQuickItemType()==IDB_WAND)
                p[6].activity_values.insert(p[6].activity_values.end(),{std::uint64_t(int(item->GetMP())),std::uint64_t(int(item->GetMaxMP()))});
            else p[6].activity_values.insert(p[6].activity_values.end(),{0,0});
        }
        p[8].activity_values={std::uint64_t(mission->pCommands->nMode),std::uint64_t(mission->pCommands->nAggr)};
        p[9].activity_values={std::uint64_t(mission->pClock->nMode),std::uint64_t(mission->pClock->bNewObj)};
        for(unsigned i:{0u,1u,2u,4u,5u,6u,8u,9u})p[i].activity_available=true;
    }
    static void Publish(const CI3DScreenMission* mission,unsigned width,unsigned height) {
        inputOwners={};
        owned::HudLayoutFrame frame;
        // Current host ingress is full-width, unscaled placed space. Do not
        // announce a different safe area until its drawing/input consumer exists.
        frame.layout=owned::widescreen::make_layout(float(width),float(height));
        frame.active=mission && width && height;
        if(frame.active) {
            inputOwners={mission->pSpells,mission->pSkills,mission->pMap,mission->pInfo,mission->pFaces,
                mission->pWeapons,mission->pItems,mission->pOutput,mission->pCommands,mission->pClock};
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
            p[2].slide_pixels=mission->pMap->slide_nCurPixels;
            p[3].slide_pixels=mission->pInfo->slide_nCurPixels;
            p[7].slide_pixels=mission->pOutput->slide_nCurPixels;
            p[2].slide_closing=mission->pMap->slide_dir>0.f;
            p[3].slide_closing=mission->pInfo->slide_dir>0.f;
            p[7].slide_closing=mission->pOutput->slide_dir>0.f;
            Activity(frame,mission);
        }
        owned::embedded_hud_layout().publish(std::move(frame));
    }
};
namespace OwnedRender {
int E3HudHit(CI3DObject* object,POINT pixel) noexcept {return E3HudAccess::Hit(object,pixel);}
bool E3HudInside(CI3DObject* object,POINT pixel,bool& inside) noexcept {return E3HudAccess::Inside(object,pixel,inside);}
bool E3HudAreaInside(CI3DObject* object,unsigned area,POINT pixel,bool& inside) noexcept {return E3HudAccess::AreaInside(object,area,pixel,inside);}
void E3PublishHud(const CI3DScreenMission* mission,unsigned width,unsigned height) noexcept {
    try { E3HudAccess::Publish(mission,width,height); }
    catch(const std::bad_alloc&) { inputOwners={};owned::embedded_hud_layout().retire(); }
}
}
