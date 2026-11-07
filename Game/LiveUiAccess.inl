// Read-only access to the same regions/layouts used by native UI hit testing.
// Included at global scope. None of these observations are serialized.
#include "I3DScreenBase.h"
#include "I3DScreenCamp.h"
#include "I3DScreenMessageBox.h"
#include "I3DScreenTutorial.h"
#include "I3DScreenMission.h"
#include "I3DScreenGlobalMap.h"
#include "I3DCursor.h"
#include "OwnedRender/E3Hud.h"
#include "OwnedRender/E4Camp.h"
#include "hud_layout_context.h"
#include "WideScreen.h"
#include <cstdio>

struct LiveUiAccess {
    struct Area {
        CRect rect;
        CPoint point;
        CString caption;
        bool enabled=false, hasPoint=false, exactAvailability=false;
    };
    static unsigned long long Serial(CI3DScreen* screen) {return screen?screen->liveUiSerial:0;}
    static size_t Depth() {return theInterface.pScreens.size();}
    static CI3DScreen* Screen(size_t i) {return i<Depth()?theInterface.pScreens[i]:nullptr;}
    static bool Pending() {return theInterface.bExitModal||theInterface.bScreensMustDie||theInterface.pNextScreen;}
    static bool SavedBackground() {
        return Depth()>1 && (theInterface.pBGScreen || theInterface.pBGScreen2);
    }
    static std::string Name(CI3DScreen* screen) {
        if(!screen)return "none";
        if(dynamic_cast<CI3DScreenTutorial*>(screen))return "tutorial";
        if(dynamic_cast<CI3DScreenMessageBox*>(screen))return "message_box";
        if(dynamic_cast<CI3DScreenCamp*>(screen))return "camp";
        if(dynamic_cast<CI3DScreenBase*>(screen))return "base";
        if(dynamic_cast<CI3DScreenMission*>(screen))return "mission";
        if(dynamic_cast<CI3DScreenGlobalMap*>(screen))return "global_map";
        std::string name=typeid(*screen).name();
        const std::string prefix="class CI3DScreen";
        if(name.rfind(prefix,0)==0)name.erase(0,prefix.size());
        if(name=="MainMenu")return "main_menu";
        if(name=="EscapeMenu")return "escape_menu";
        if(name=="SaveLoad")return "save_load";
        if(name=="Options")return "options";
        if(name=="Objectives")return "objectives";
        if(name=="EndGame")return "endgame";
        return name;
    }
    static std::string Mode(CI3DScreen* screen) {
        if(auto* base=dynamic_cast<CI3DScreenBase*>(screen)) {
            static const char* names[]={"movement","topic","approach","briefing","summary"};
            const int mode=int(base->eMode);return mode>=0&&mode<5?names[mode]:"unknown";
        }
        if(auto* camp=dynamic_cast<CI3DScreenCamp*>(screen)) {
            static const char* names[]={"equipment","spells","spell_trade","spell_constructor","item_trade","item_constructor","repair","exit"};
            const int mode=int(camp->eScreenMode);return mode>=0&&mode<8?names[mode]:"unknown";
        }
        return "";
    }
    static bool Waiting(CI3DScreen* screen) {
        auto* base=dynamic_cast<CI3DScreenBase*>(screen);
        return Depth()>1 || Cutscene(screen) || (base && base->eMode==BSM_TOPIC);
    }
    static std::string Kind(CI3DScreen* screen) {
        if(auto* base=dynamic_cast<CI3DScreenBase*>(screen))if(base->eMode==BSM_TOPIC || Cutscene(screen))return "dialog";
        if(auto* camp=dynamic_cast<CI3DScreenCamp*>(screen))if(int(camp->eScreenMode)>=2 && int(camp->eScreenMode)<=6)return "shop";
        return Name(screen);
    }
    static CString Title(CI3DScreen* screen) {
        if(auto* tutorial=dynamic_cast<CI3DScreenTutorial*>(screen))return tutorial->mszHeader;
        if(auto* box=dynamic_cast<CI3DScreenMessageBox*>(screen))return box->szTitle;
        return "";
    }
    static CString WindowText(CI3DScreen* screen) {
        if(auto* box=dynamic_cast<CI3DScreenMessageBox*>(screen))return box->szText;
        if(auto* tutorial=dynamic_cast<CI3DScreenTutorial*>(screen))return tutorial->mszTutorial;
        return "";
    }
    static int Context(CI3DScreen* screen) {
        if(auto* tutorial=dynamic_cast<CI3DScreenTutorial*>(screen))return tutorial->mnCurrentPage;
        if(auto* camp=dynamic_cast<CI3DScreenCamp*>(screen))return int(camp->eScreenMode);
        if(auto* base=dynamic_cast<CI3DScreenBase*>(screen))return int(base->eMode)*10000+base->nPos;
        return 0;
    }
    static bool Cutscene(CI3DScreen* screen) {
        auto* base=dynamic_cast<CI3DScreenBase*>(screen);
        return base && (base->eMode==BSM_BRIEFING || base->eMode==BSM_SUMMARY || base->eMode==BSM_APPROACH);
    }
    static bool CutsceneRunning() {
        for(size_t i=0;i<Depth();++i)if(Cutscene(Screen(i)))return true;
        return false;
    }
    static int ObjectCount(CI3DScreen* screen) {return screen?screen->pObjects.GetSize()+1:0;}
    static CI3DObject* Object(CI3DScreen* screen,size_t index) {
        if(!screen || index>=size_t(ObjectCount(screen)))return nullptr;
        return index?screen->pObjects[int(index-1)]:screen;
    }
    static int AreaCount(CI3DObject* object) {return object?object->areas.GetSize():0;}
    static bool Availability(CI3DScreen* screen,CI3DObject* object,int area,bool& exact,CString& caption) {
        exact=false;
        if(object!=screen)return true;
        if(auto* tutorial=dynamic_cast<CI3DScreenTutorial*>(screen)) {
            exact=true;
            if(area==0){if(tutorial->mnCurrentPage>0)caption=tutorial->mszPages[tutorial->mnCurrentPage-1];return tutorial->mnCurrentPage>0;}
            if(area==1){if(caption.IsEmpty())caption="Close";return true;}
            if(area==2){if(tutorial->mnCurrentPage+1<tutorial->mszPages.GetSize())caption=tutorial->mszPages[tutorial->mnCurrentPage+1];return tutorial->mnCurrentPage+1<tutorial->mszPages.GetSize();}
        }
        if(auto* base=dynamic_cast<CI3DScreenBase*>(screen)) {
            exact=true;
            const int row=base->nPos+area;
            if(base->eMode!=BSM_TOPIC || row<0 || row>=base->szBriefings.GetSize())return false;
            if(area<base->liveTopicCaptions.GetSize())caption=base->liveTopicCaptions[area];
            return true;
        }
        if(auto* camp=dynamic_cast<CI3DScreenCamp*>(screen)) {
            exact=true;
            if(area==0)return camp->bValidOK;
            if(area==1)return camp->bValidCancel;
            if(area==2||area==3)return camp->eScreenMode==ISM_WEAPONS;
            if(area>=4 && area<4+camp->szModes.GetSize())caption=camp->szModes[area-4];
        }
        if(dynamic_cast<CI3DScreenMessageBox*>(screen))exact=true;
        return true;
    }
    static bool ReadArea(CI3DScreen* screen,size_t objectIndex,int index,Area& out) {
        auto* object=Object(screen,objectIndex);
        if(!object || !object->IsVisible() || index<0 || index>=AreaCount(object))return false;
        auto* area=object->areas[index];
        if(!area || !area->bIsValid)return false;
        out.caption=area->JournalTip();
        out.enabled=Availability(screen,object,index,out.exactAvailability,out.caption);
        CRect native=area->rect;native+=area->shift;
        const auto& layout=owned::embedded_hud_layout().layout();
        const int panel=OwnedRender::E3HudPanel(object);
        const auto role=E4CampAccess::RoleOf(object);
        if(OwnedRender::E4::Active() && role!=OwnedRender::E4::Role::none) {
            out.rect=OwnedRender::E4::Pixels(E4CampAccess::Area(object,role,index).destination);
        } else if(panel>=0) {
            auto& context=owned::embedded_hud_layout();
            const auto p=owned::HudPanel(panel);
            owned::widescreen::HudComposition composition;
            bool visible=false;
            if(context.slot_panel(unsigned(panel)))visible=context.slot_composition(p,unsigned(index),composition);
            else visible=context.control_composition(p,unsigned(index),composition) || context.body_composition(p,composition);
            if(!visible)return false;
            float minx=FLT_MAX,miny=FLT_MAX,maxx=-FLT_MAX,maxy=-FLT_MAX;
            for(int i=0;i<4;++i) {
                float x=owned::widescreen::placed_to_screen_x(layout,float(i&1?native.right:native.left));
                float y=owned::widescreen::placed_to_screen_y(layout,float(i&2?native.bottom:native.top));
                owned::widescreen::compose_point(x,y,composition);
                minx=(std::min)(minx,x);miny=(std::min)(miny,y);maxx=(std::max)(maxx,x);maxy=(std::max)(maxy,y);
            }
            out.rect=CRect(int(std::floor(minx)),int(std::floor(miny)),int(std::ceil(maxx)),int(std::ceil(maxy)));
            if(composition.clip_native_screen && composition.clip_right>=0.f) {
                CRect clipped;clipped.IntersectRect(out.rect,CRect(int(std::ceil(composition.clip_left)),int(std::ceil(composition.clip_top)),
                    int(std::floor(composition.clip_right)),int(std::floor(composition.clip_bottom))));out.rect=clipped;
            }
        } else {
            out.rect=native;
            const auto canvas=WideScreen::Layout(theInterface.GetWidth(),theInterface.GetHeight());
            owned::widescreen::placed_rect_to_pixels(canvas,out.rect);
        }
        CRect clipped;clipped.IntersectRect(out.rect,CRect(0,0,theInterface.GetWidth(),theInterface.GetHeight()));out.rect=clipped;
        if(out.rect.IsRectEmpty())return false;
        // A polygon's bounding-box centre may be outside it. Resolve a point
        // against the actual stock/c56/camp hit path, never invoke the action.
        for(int y=0;y<5 && !out.hasPoint;++y)for(int x=0;x<5 && !out.hasPoint;++x) {
            CPoint point(out.rect.left+(out.rect.Width()-1)*(2*x+1)/10,
                out.rect.top+(out.rect.Height()-1)*(2*y+1)/10);
            if(object->HitTest(point)==index){out.point=point;out.hasPoint=true;}
        }
        out.enabled=out.enabled&&out.hasPoint;
        return true;
    }
    static void Cursor(CPoint point) {
        theCursor.m_ptCursor=point;theCursor.m_ptLockedCursor=point;
        theCursor.m_ptLastCursor=point;theCursor.m_ptLastInc=CPoint(0,0);
        theInterface.lastCursorPos=point;
    }
};
