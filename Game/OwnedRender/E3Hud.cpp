#include "StdAfx.h"
#include "E3Hud.h"
#include "E3Input.h"
#include "E3Combat.h"
#include "E3HudVisual.h"
#include "combat_ui.h"
#include "character_pane_owned_text.h"
#include "../WorldInterface.h"
#include "../I3DScreenMission.h"
#include "../I3DObject.h"
#include "../I3DScrollText.h"
#include "../I3DToolTip.h"
#include "../I3DMain.h"
#include <cmath>
#include <cstdio>
#include "../Unit.h"
#include "../Item.h"
#include "../Camera.h"
#include "hud_layout_context.h"
#include <algorithm>
#include <cstring>
#include <typeinfo>
#include "../WideScreen.h"

namespace {
std::array<const CI3DObject*,owned::kHudPanels.size()> inputOwners{};
std::array<int,8> itemFigures{};
int cameraSlot=-1;
const CI3DObject* messageTextOwner=nullptr;
thread_local CI3DSurface* drawingSurface=nullptr;
}
// This adapter only copies facts owned by the active mission. Slot geometry,
// header docking, scaling and preview layout remain in the renderer library.
struct E3HudAccess {
    static void RefreshMessages(CI3DObjectOutput* output) {
        if(!output || output->figures.GetSize()<6 ||
           theInterface.GetScreensNum()!=1 || theInterface.GetCurrentScreen()!=output->pScreen)return;
        auto* text=output->pScreen->pScrollText;if(!text)return;
        text->messageOutput=output;messageTextOwner=text;inputOwners[7]=output;
        auto& context=owned::embedded_hud_layout();
        context.publish_canvas(theInterface.GetWidth(),theInterface.GetHeight());
        const auto publish=[&] {
            owned::HudNativePanel panel;Copy(panel,output);Shell(panel,output,5,2);
            panel.slide=Slide(output->slide_nCurPixels,output->slide_nTotalPixels);
            panel.slide_pixels=output->slide_nCurPixels;panel.slide_closing=output->slide_dir>0.f;
            context.publish_message(std::move(panel));
        };
        publish();owned::MessageGeometry geometry;float scale=1.f;
        WideScreen::RawScope raw;
        if(!context.message_layout(geometry,scale))return;
        const auto& previous=output->ownedMessageGeometry;
        const auto same=[](const owned::MessageGeometry::Quad& a,const owned::MessageGeometry::Quad& b) {
            return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
        };
        if(!output->ownedMessageLayout || !same(previous.background,geometry.background) || !same(previous.toolbar,geometry.toolbar)) {
            const auto bg=geometry.background,title=geometry.toolbar;
            output->figures[0]->MakeBar(20,100,bg.left,bg.top,bg.right,bg.bottom);
            output->figures[1]->MakeBar(20,100,title.left,title.top,title.right,title.bottom);
        }
        output->SetRect(CRect(geometry.panel));
        output->areas[3]->MakeRect(geometry.panel.left,geometry.panel.top,geometry.panel.right,geometry.panel.bottom);
        output->figures[0]->SetRelativeToScreen(geometry.background_x,geometry.background_y,20);
        output->figures[1]->SetRelativeToScreen(geometry.centre_x,10,20);
        output->figures[3]->SetRelativeToScreen(200,10,10);
        output->figures[4]->SetRelativeToScreen(230,10,10);
        output->figures[5]->SetRelativeToScreen(geometry.centre_x,geometry.frame_height*.5f,18);
        output->figures[2]->SetOrientation(output->slide_dir<0?CQuaternion(CPlot3D(0,0,1),FP_PI):QNULL);
        output->figures[2]->SetRelativeToScreen(400,output->slide_dir<0?-52.f:83.f,8.106086f);
        for(int i=1;i<4;++i)output->areas[i]->SetShift(0,0);
        text->SetTextLayout(CRect(geometry.text),scale);
        output->ownedMessageGeometry=geometry;output->ownedMessageLayout=true;
        publish();
    }
    static void MessageLayout(CI3DScrollText* text) {
        if(text && text->messageOutput)RefreshMessages(text->messageOutput);
    }
    static bool ClockTooltip(CI3DToolTip* tip) {
        if(!tip || tip->surfaces.GetSize()!=1 || !tip->surfaces[0])return false;
        auto* surface=tip->surfaces[0];
        bool isClock=false;
        const auto* clock=inputOwners[unsigned(owned::HudPanel::game_speed)];
        if(clock && OwnedRender::E3HudRendererAttached() && clock->bVisible)
            for(int i=0;i<clock->areas.GetSize();++i)
                if(clock->areas[i] && clock->areas[i]->pTip==tip){isClock=true;break;}
        int minute=-1;
        if(isClock)owned::widescreen::campaign_minute(double(IWorldClient.GSGetVar("gtime")),minute);
        if(!isClock && tip->ownedClockMinute<0)return false;
        const bool extra=minute>=0;
        char time[96]{};
        int width=(std::max)(int(tip->m_sizeTip.cx),tip->nFirstLineWidth),height=tip->m_sizeTip.cy;
        const auto textLayout=owned::widescreen::clock_text_layout(height,tip->szStrings.GetSize(),tip->nStringHeight);
        const int top=textLayout.top;
        if(extra) {
            owned::widescreen::format_clock_time(time,sizeof(time),minute);
            SIZE extent{};
            if(!owned::character_pane_measure_text((HFONT)tip->pI3D->GetFont(0)->GetSafeHandle(),time,int(std::strlen(time)),extent))return false;
            width=owned::widescreen::clock_text_width(owned::embedded_hud_layout().layout(),width,extent.cx);
            height=textLayout.height;
        }
        CRect desired=surface->rect;
        tip->pI3D->ScreenToVirtual(desired);
        desired.right=desired.left+width;desired.bottom=desired.top+height;
        const bool resized=surface->SetRectForRedraw(desired);
        if(resized || tip->ownedClockMinute!=minute || tip->ownedClockRevision!=tip->ownedTextRevision) {
            // Redraw from original localized lines and current shortcut. Never
            // append to szStrings: refreshes must not accumulate time lines.
            tip->Update();
            if(extra) {
                if(!surface->GetDC())return false;
                struct Release { CI3DSurface* value; ~Release(){value->ReleaseDC();} } release{surface};
                surface->DrawText(time,CRect(6,top,width-6,top+tip->nStringHeight),0,RGB(228,215,167),DT_LEFT,FALSE);
            }
            tip->ownedClockMinute=minute;tip->ownedClockRevision=tip->ownedTextRevision;
        }
        if(!isClock)return false;
        owned::widescreen::HudComposition composition;
        if(!owned::embedded_hud_layout().clock_tooltip_composition(surface->rect,composition))return false;
        struct Restore {
            owned::widescreen::HudComposition previous=owned::widescreen::hud_blit;
            CI3DSurface* surface=drawingSurface;
            ~Restore(){owned::widescreen::hud_blit=previous;drawingSurface=surface;}
        } restore;
        owned::widescreen::hud_blit=composition;drawingSurface=surface;
        surface->Draw();
        return true;
    }
    static bool Draw2D(CI3DObject* object) {
        if(!object || !OwnedRender::E3HudRendererAttached())return false;
        const bool text=object==messageTextOwner;
        const int index=text?7:Panel(object);if(index<0)return false;
        if(!object->bVisible)return true;
        struct Restore {
            owned::widescreen::HudComposition previous=owned::widescreen::hud_blit;
            CI3DSurface* surface=drawingSurface;
            ~Restore(){owned::widescreen::hud_blit=previous;drawingSurface=surface;}
        } restore;
        auto& context=owned::embedded_hud_layout();
        for(int i=0;i<object->surfaces.GetSize();++i) {
            auto* surface=object->surfaces[i];if(!surface || !surface->IsVisible())continue;
            owned::widescreen::HudComposition composition;
            const bool visible=text?context.message_text_composition(surface->rect,object->rect,composition):
                context.surface_composition(owned::HudPanel(index),i,composition);
            if(!visible)continue;
            owned::widescreen::hud_blit=composition;drawingSurface=surface;
            surface->Draw();
        }
        return true;
    }
    static bool Blit(CI3DSurface* surface) {
        if(!surface || surface!=drawingSurface || !OwnedRender::E3HudRendererAttached())return false;
        if(!surface->pSurface || !surface->IsVisible())return true;
        // Clip only after the renderer has applied the HUD transform. A native
        // rectangle outside the canvas can move into view after relocation.
        CRect destination=surface->rect;
        CRect source(0,0,destination.Width(),destination.Height());
        DDBLTFX fx{};fx.dwSize=sizeof(fx);
        fx.ddckSrcColorkey.dwColorSpaceHighValue=fx.ddckSrcColorkey.dwColorSpaceLowValue=surface->colorKey;
        const bool keyed=surface->HasFlag(BS_COLOR_KEY)!=FALSE;
        const HRESULT result=surface->pSurface->BltToRenderSurface(destination,
            surface->HasFlag(BS_STRETCH)?nullptr:&source,DDBLT_ASYNC|DDBLT_WAIT|(keyed?DDBLT_KEYSRCOVERRIDE:0),keyed?&fx:nullptr);
        IfFailedThrowHR(result,"HUD surface blit");
        return true;
    }
    static bool Draw(CI3DObject* object,CRenderer* renderer) {
        const int index=Panel(object);if(index<0)return false;
        if(!object->bVisible)return true;
        struct Restore {
            owned::widescreen::HudComposition previous=owned::widescreen::hud_blit;
            ~Restore(){owned::widescreen::hud_blit=previous;}
        } restore;
        auto& context=owned::embedded_hud_layout();
        for(int i=0;i<object->figures.GetSize();++i) {
            auto* figure=object->figures[i];if(!figure || !figure->IsVisible())continue;
            int slot=i;
            if(index==4)slot=i?(i-1)/5:cameraSlot;
            else if(index==6) {
                slot=-1;
                for(unsigned j=0;j<itemFigures.size();++j)if(itemFigures[j]==i){slot=int(j);break;}
            }
            owned::widescreen::HudComposition composition;
            const bool visible=object==messageTextOwner?context.surface_composition(owned::HudPanel::messages,-1,composition):
                context.figure_composition(owned::HudPanel(index),unsigned(i),slot,composition);
            if(!visible)continue;
            owned::widescreen::hud_blit=composition;
            if(index==4) {
                auto* owner=static_cast<CI3DObjectFaces*>(object);
                const bool model=i>0 && (i-1)%5==0;
                CString selected,base;
                int unitId=0;
                if(model && slot>=0 && unsigned(slot)<owner->faces.size()) {
                    const auto& face=owner->faces[slot];
                    base=face.szTexName;selected=base;
                    if(face.nCurTex==1)selected+="a";
                    else if(face.nCurTex==2)selected+="c";
                    else if(face.nCurTex==3)selected+="b";
                    if(slot<owner->pScreen->GetNumUnits())unitId=owner->pScreen->GetUnit(slot)->GetID();
                }
                const char* texture=model?(LPCSTR)selected:figure->GetTexture()?"battle00":nullptr;
                OwnedRender::E3HudDrawPortrait(figure,renderer,unsigned((std::max)(slot,0)),unitId,
                    texture,model?(LPCSTR)base:nullptr,model);
            } else if(index==0 || index==1 || index==5 || index==6) {
                const auto lane=index==0?OwnedRender::E3HudLane::spell:index==1?OwnedRender::E3HudLane::skill:
                    index==5?OwnedRender::E3HudLane::weapon:OwnedRender::E3HudLane::item;
                OwnedRender::E3HudDrawVisual(figure,renderer,lane,unsigned(i));
            } else figure->Draw(renderer);
        }
        return true;
    }
    static int Panel(const CI3DObject* object) noexcept {
        if(!object || !OwnedRender::E3HudRendererAttached())return -1;
        if(object==messageTextOwner)return 7;
        for(unsigned i=0;i<inputOwners.size();++i)if(inputOwners[i]==object)return int(i);
        return -1;
    }
    static bool MessagePoint(CI3DObject* object,POINT pixel,POINT& native,bool captured) noexcept {
        if(!object || object!=messageTextOwner || !object->bVisible || !OwnedRender::E3HudRendererAttached())return false;
        auto& context=owned::embedded_hud_layout();
        owned::widescreen::HudComposition composition;
        if(!context.surface_composition(owned::HudPanel::messages,-1,composition))return false;
        // A captured drag must keep mapping outside the visible track so the
        // scrollbar can clamp to either end. Normal hits keep body clipping.
        if(captured)composition.clip_native_screen=false;
        return context.inverse_composed_point(pixel,composition,native);
    }
    static bool NativePoint(CI3DObject* object,unsigned area,POINT pixel,POINT& native) noexcept {
        const int panel=Panel(object);
        if(object==messageTextOwner)return MessagePoint(object,pixel,native,false);
        return panel>=0 && object->bVisible &&
            owned::embedded_hud_layout().area_point(owned::HudPanel(panel),area,pixel,native);
    }
    static bool AreaInside(CI3DObject* object,unsigned area,POINT pixel,bool& inside) noexcept {
        const int panel=Panel(object);if(panel<0)return false;
        inside=false;POINT native{};
        if(object->bVisible && area<unsigned(object->areas.GetSize()) && object->areas[area] &&
            NativePoint(object,area,pixel,native))
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
        if(object==messageTextOwner) {
            POINT native{};inside=MessagePoint(object,pixel,native,false) && object->rect.PtInRect(CPoint(native))!=FALSE;
            return true;
        }
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
    static void TextActivity(std::vector<std::uint64_t>& values,const CI3DScrollText* text) {
        if(!text)return;
        // Compare semantic tokens exactly. Pixel sizes, surface redraws and
        // line wrapping are not content changes and must not restart the fade.
        values.push_back(std::uint64_t(text->m_aLexems.GetSize()));
        for(int i=0;i<text->m_aLexems.GetSize();++i) {
            const auto* token=text->m_aLexems[i];
            values.push_back(token?std::uint64_t(token->m_eType):UINT64_MAX);
            if(!token)continue;
            if(token->m_eType==LT_WORD) {
                const auto& word=static_cast<const SWordLexem*>(token)->szWord;
                const int length=word.GetLength();values.push_back(std::uint64_t(length));
                for(int start=0;start<length;start+=8) {
                    std::uint64_t bytes=0;
                    for(int j=0;j<8 && start+j<length;++j)
                        bytes|=std::uint64_t(static_cast<unsigned char>(word[start+j]))<<(j*8);
                    values.push_back(bytes);
                }
            } else if(token->m_eType==LT_STYLE) {
                const auto* style=static_cast<const SStyleLexem*>(token);
                values.insert(values.end(),{std::uint64_t(style->isNewLine),std::uint64_t(style->nFont),std::uint64_t(style->colorText)});
            }
        }
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
        p[unsigned(owned::HudPanel::stance)].activity_values={std::uint64_t(mission->pCommands->nMode),std::uint64_t(mission->pCommands->nAggr)};
        p[unsigned(owned::HudPanel::game_speed)].activity_values={std::uint64_t(mission->pClock->nMode),std::uint64_t(mission->pClock->bNewObj)};
        TextActivity(p[7].activity_values,mission->pScrollText);
        for(unsigned i:{0u,1u,2u,4u,5u,6u,7u,unsigned(owned::HudPanel::stance),unsigned(owned::HudPanel::game_speed)})p[i].activity_available=true;
    }
    static void CommitVisuals(const CI3DScreenMission* mission) {
        const std::uint32_t counts[]={unsigned(mission->pWeapons->figures.GetSize()),unsigned(mission->pItems->figures.GetSize()),
            unsigned(mission->pSpells->figures.GetSize()),unsigned(mission->pSkills->figures.GetSize())};
        OwnedRender::E3HudCommitScreen(mission,counts);
    }
    static void CombatPortraits(const CI3DScreenMission* mission) {
        std::vector<owned::CombatPortraitAnchor> anchors;
        if(mission && mission->pFaces && mission->pFaces->bVisible) {
            const auto* faces=mission->pFaces;
            const int count=(std::min)(mission->pUnits.GetSize(),faces->areas.GetSize());
            anchors.reserve(count);
            for(int i=0;i<count;++i) {
                auto* unit=mission->pUnits[i];const auto* area=faces->areas[i];
                if(!unit || !area)continue;
                OwnedRender::E3CombatPublishUnit(unit);
                owned::CombatPortraitAnchor anchor;
                if(owned::combat_ui_embedded_portrait(unit->GetID(),unsigned(i),area->rect,area->shift,anchor))
                    anchors.push_back(anchor);
            }
        }
        owned::combat_ui_publish_portraits(anchors.data(),anchors.size());
    }
    static void Publish(const CI3DScreenMission* mission,unsigned width,unsigned height) {
        inputOwners={};
        messageTextOwner=nullptr;
        itemFigures.fill(-1);cameraSlot=-1;
        owned::HudLayoutFrame frame;
        // Publish native, unscaled placed space. HudLayoutContext applies the
        // configured safe area to both composition and pointer membership.
        frame.layout=owned::widescreen::make_layout(float(width),float(height));
        frame.active=mission && width && height;
        if(frame.active) {
            messageTextOwner=mission->pScrollText;
            inputOwners={mission->pSpells,mission->pSkills,mission->pMap,mission->pInfo,mission->pFaces,
                mission->pWeapons,mission->pItems,mission->pOutput,nullptr,mission->pCommands,mission->pClock};
            auto& p=frame.panels;
            Copy(p[0],mission->pSpells);Copy(p[1],mission->pSkills);
            Copy(p[2],mission->pMap);Copy(p[3],mission->pInfo);
            Copy(p[4],mission->pFaces);Copy(p[5],mission->pWeapons);
            Copy(p[6],mission->pItems);Copy(p[7],mission->pOutput);
            Copy(p[unsigned(owned::HudPanel::stance)],mission->pCommands);Copy(p[unsigned(owned::HudPanel::game_speed)],mission->pClock);
            Slots(p[0],mission->pSpells->nSpells);Slots(p[1],mission->pSkills->nSkills);
            for(unsigned i=0;i<p[1].native_count && i<p[1].slots.size() && i<unsigned(mission->pSkills->nTypes.GetSize());++i)
                p[1].slots[i].semantic_type=mission->pSkills->nTypes[i];
            PortraitFrames(p[4],mission->pFaces);Slots(p[5],mission->pWeapons->figures.GetSize());
            for(unsigned i=0;i<(std::min)(unsigned(mission->pUnits.GetSize()),p[4].native_count);++i)
                if(mission->pUnits[i] && mission->pUnits[i]->GetID()==theCamera.GetCarrierID()){cameraSlot=int(i);break;}
            for(unsigned i=0;i<(std::min)(unsigned(p[6].slots.size()),8u);++i) {
                const int figure=mission->pItems->nSubst[i];
                itemFigures[i]=figure;
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
        const bool active=frame.active;
        owned::embedded_hud_layout().publish(std::move(frame));
        CombatPortraits(active?mission:nullptr);
    }
};
namespace OwnedRender {
E3HudCompositionScope::E3HudCompositionScope(CI3DObject* object,int figure) noexcept
    : previous(owned::widescreen::hud_blit) {
    const int panel=E3HudAccess::Panel(object);
    if(panel<0)return;
    auto& context=owned::embedded_hud_layout();
    owned::widescreen::HudComposition h;
    visible=figure<0?context.body_composition(owned::HudPanel(panel),h):
        context.figure_composition(owned::HudPanel(panel),unsigned(figure),-1,h);
    if(visible)owned::widescreen::hud_blit=h;
}
E3HudCompositionScope::~E3HudCompositionScope() {owned::widescreen::hud_blit=previous;}
bool E3HudItemEventInside(CI3DObject* object,POINT pixel) noexcept {
    if(E3HudAccess::Panel(object)!=int(owned::HudPanel::items))return true;
    bool inside=false;E3HudAccess::Inside(object,pixel,inside);return inside;
}
bool E3HudMapOffset(CI3DObject* object,POINT pixel,POINT& offset) noexcept {
    if(E3HudAccess::Panel(object)!=int(owned::HudPanel::minimap))return false;
    auto& context=owned::embedded_hud_layout();
    owned::widescreen::HudComposition h;
    if(!context.body_composition(owned::HudPanel::minimap,h))return false;
    const auto& layout=context.layout();const auto centre=owned::widescreen::map_centre(layout,h);
    offset.x=owned::widescreen::map_offset(owned::widescreen::cursor_x(layout,pixel.x),centre.dx,centre.inverse_scale);
    offset.y=owned::widescreen::map_offset(owned::widescreen::cursor_y(layout,pixel.y),centre.dy,centre.inverse_scale);
    return true;
}

void E3HudCommitVisuals(const CI3DScreenMission* mission) {E3HudAccess::CommitVisuals(mission);}
bool E3MessagePoint(CI3DObject* object,POINT pixel,POINT& native,bool captured,bool& valid) noexcept {
    if(!object || object!=messageTextOwner || !E3HudRendererAttached())return false;
    valid=E3HudAccess::MessagePoint(object,pixel,native,captured);return true;
}
void E3MessageLayout(CI3DScrollText* text) {E3HudAccess::MessageLayout(text);}
void E3RefreshMessages(CI3DObjectOutput* output) {E3HudAccess::RefreshMessages(output);}
bool E3CaptureTutorial(CI3DScreen* screen,owned::HudLayoutContext& snapshot) {
    snapshot.retire();
    if(!screen || typeid(*screen)!=typeid(CI3DScreenMission) || theInterface.GetScreensNum()!=1 ||
       theInterface.GetCurrentScreen()!=screen)return false;
    E3PublishHud(static_cast<CI3DScreenMission*>(screen),theInterface.GetWidth(),theInterface.GetHeight());
    snapshot=owned::embedded_hud_layout();return true;
}
bool E3ClockTooltip(CI3DToolTip* tooltip) {return E3HudAccess::ClockTooltip(tooltip);}
bool E3HudDraw2D(CI3DObject* object) {return E3HudAccess::Draw2D(object);}
bool E3HudBlit(CI3DSurface* surface) {return E3HudAccess::Blit(surface);}
bool E3HudDraw(CI3DObject* object,CRenderer* renderer) {return E3HudAccess::Draw(object,renderer);}
int E3HudHit(CI3DObject* object,POINT pixel) noexcept {return E3HudAccess::Hit(object,pixel);}
bool E3HudInside(CI3DObject* object,POINT pixel,bool& inside) noexcept {return E3HudAccess::Inside(object,pixel,inside);}
bool E3HudAreaInside(CI3DObject* object,unsigned area,POINT pixel,bool& inside) noexcept {return E3HudAccess::AreaInside(object,area,pixel,inside);}
bool E3HudNativePoint(CI3DObject* object,unsigned area,POINT pixel,POINT& native) noexcept {return E3HudAccess::NativePoint(object,area,pixel,native);}
void E3PublishHud(const CI3DScreenMission* mission,unsigned width,unsigned height) noexcept {
    try { E3HudAccess::Publish(mission,width,height); }
    catch(const std::bad_alloc&) {
        inputOwners={};messageTextOwner=nullptr;owned::embedded_hud_layout().retire();
        owned::combat_ui_publish_portraits(nullptr,0);
        owned::combat_ui_set_status("Combat portraits omitted: publication allocation failed.");
    }
}
}
