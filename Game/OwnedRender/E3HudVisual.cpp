#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../stdafx.h"
#include "E3HudVisual.h"
#include "../ResourceManager.h"
#include "../I3DFigure.h"
#include "../Renderer.h"
#include "../dx11/RendererDX11.h"
#include "../GameJournal.h"
#include "../WorldClient.h"
#include "../ModRuntime.h"
#include "../ModPackage.h"
#include "../FigureClientSpecific.h"
#include "embedded_hud_visual.h"
#include <exception>

namespace {
struct State {
    owned::HudVisualPublication& publication=owned::hud_embedded_publication();
    CResourceManager* textures=nullptr;
    CResourceManager* figures=nullptr;
    const void* screen=nullptr;
    std::uint64_t epoch=0;
    CI3DObject* receiver=nullptr;
    bool capture=false;
};
State& state() { static auto* value=new State; return *value; }
owned::HudVisualLane lane(OwnedRender::E3HudLane value) {return owned::HudVisualLane(value);}
void omitted(owned::HudVisualLane value, unsigned ordinal, const char* stage,
    const char* member=nullptr, owned::ResourceFailureCode code=owned::ResourceFailureCode::none) noexcept;
bool resolve(void*, owned::HudResourceRole role, const char* name,
    owned::ResourceBlob& blob, owned::HudPublicationFailureStage& stage,
    owned::ResourceFailureCode& code) {
    auto* manager=role==owned::HudResourceRole::texture?state().textures:state().figures;
    stage=owned::HudPublicationFailureStage::resolver; code=owned::ResourceFailureCode::not_found;
    if(!manager){
        stage=owned::HudPublicationFailureStage::resolver_unavailable;
        omitted(state().publication.capturing_lane_id(),0,"resolver_unavailable",name,code);return false;
    }
    DWORD size=0;
    BYTE* bytes=manager->GetFile(name,&size);
    if(!bytes){
        omitted(state().publication.capturing_lane_id(),0,
            role==owned::HudResourceRole::texture?"texture_resolver":"figure_resolver",name,code);return false;
    }
    struct Close { CResourceManager* manager; BYTE* bytes; ~Close(){manager->CloseFile(bytes);} } close{manager,bytes};
    static const owned::ResourceStore store;
    owned::ResourceFailure failure;
    if(!store.copy_selected_memory(name,bytes,size,blob,failure)) {
        stage=owned::HudPublicationFailureStage::copy;code=failure.code;return false;
    }
    stage=owned::HudPublicationFailureStage::none;code=owned::ResourceFailureCode::none;return true;
}
void omitted(owned::HudVisualLane value, unsigned ordinal, const char* stage,
    const char* member, owned::ResourceFailureCode code) noexcept {
    // Bound repeated failures without hiding every later failure in this screen.
    static std::uint64_t reportedEpoch=0;
    static std::array<DWORD,5> last{};
    static std::array<unsigned,5> counts{};
    if(reportedEpoch!=state().epoch){reportedEpoch=state().epoch;last={};counts={};}
    const unsigned index=unsigned(value);
    const DWORD now=GetTickCount();
    if(index>=counts.size() || counts[index]>=16 || (counts[index] && now-last[index]<2000))return;
    ++counts[index];last[index]=now;
    GameJournal::Event("RENDER","hud_visual_omitted","lane=%u ordinal=%u stage=%s member=%s resource=%s",
        index,ordinal,stage,member?member:"",owned::resource_failure_code_name(code));
}
}
namespace OwnedRender {
E3HudRoleCreation::E3HudRoleCreation(CResourceManager* owner, bool isTexture):texture(isTexture) {
    active=owner->GetWorld()==&theWorld;
    if(!active)return;
    auto& s=state();
    (texture?s.textures:s.figures)=owner;
    s.publication.set_resolver(nullptr,resolve);
    s.publication.begin_role_recreation(texture?owned::HudResourceRole::texture:owned::HudResourceRole::figure);
}
E3HudRoleCreation::~E3HudRoleCreation() {
    if(!active)return;
    state().publication.finish_role_recreation(texture?owned::HudResourceRole::texture:owned::HudResourceRole::figure,success);
}
bool E3HudRoleCreation::Complete(bool value) noexcept {success=value;return value;}
void E3HudResourceChanged(CResourceManager* owner, bool destroying) noexcept {
    auto& s=state();
    for(auto role:{owned::HudResourceRole::texture,owned::HudResourceRole::figure}) {
        auto*& manager=role==owned::HudResourceRole::texture?s.textures:s.figures;
        if(manager!=owner)continue;
        if(destroying){s.publication.begin_role_recreation(role);manager=nullptr;}
        else s.publication.invalidate_role(role);
    }
}
void E3HudBeginScreen(const CI3DScreenMission* owner, bool hd) noexcept {
    auto& s=state();s.screen=owner;s.receiver=nullptr;
    ++s.epoch;
    s.publication.initialize_overlay(hd?1u:0u);
    if(!s.publication.begin_activation())omitted(owned::HudVisualLane::none,0,"activation");
}
void E3HudCommitScreen(const CI3DScreenMission* owner, const std::uint32_t* counts) noexcept {
    auto& s=state();
    if(s.screen!=owner)return;
    if(!counts || !s.publication.commit_activation({counts[0],counts[1],counts[2],counts[3]})) {
        s.publication.abort_activation();omitted(owned::HudVisualLane::none,0,"activation_commit");
    }
}
void E3HudRetireScreen(const void* owner) noexcept {
    auto& s=state();if(s.screen!=owner)return;
    s.publication.retire_screen();s.screen=nullptr;s.receiver=nullptr;
}
E3HudLanePublication::E3HudLanePublication(CI3DObject* owner, E3HudLane value) noexcept {
    auto& s=state();previous=s.receiver;exceptions=std::uncaught_exceptions();
    previousCapture=s.capture;
    armed=s.publication.begin_lane(lane(value));
    s.receiver=owner;s.capture=armed;
}
E3HudLanePublication::~E3HudLanePublication() {
    auto& s=state();
    if(armed)s.publication.finish_lane(std::uncaught_exceptions()==exceptions);
    s.receiver=previous;s.capture=previousCapture;
}
bool E3HudIcon(E3HudLane value, const char* name, unsigned atlas, unsigned sw,
    unsigned sh, unsigned w, unsigned h, unsigned x, unsigned y, bool hd,
    unsigned sx, unsigned sy) noexcept {
    auto& p=state().publication;
    if(state().receiver && !state().capture){omitted(lane(value),0,"lane_begin",name);return false;}
    const owned::HudIconPlacement placement{atlas,sw,sh,w,h,x,y,sx,sy,hd?1u:0u};
    try {
        const std::string canonical=std::string(name?name:"")+(hd?".x2.mmp":".mmp");
        owned::HudPublicationFailure failure;
        if(p.capturing_lane_id()==lane(value)) {
            if(p.observe_icon(canonical.c_str(),placement))return true;
            failure=p.classify_missing(lane(value),0,p.active_screen_generation(),true);
        } else {
            if(p.repaint_icon(lane(value),canonical.c_str(),placement,&failure))return true;
        }
        omitted(lane(value),failure.ordinal,owned::hud_publication_failure_stage_name(failure.stage),
            failure.logical_member.data(),failure.resource_failure);
    } catch(...) {p.reject_icon_attempt();omitted(lane(value),0,"icon",name);}
    return false;
}
bool E3HudAttachFigure(CI3DObject* owner, const char* name) noexcept {
    auto& s=state();
    if(owner!=s.receiver)return false;
    try {if(s.capture && !s.publication.attach_figure(name)) {
        const auto value=s.publication.capturing_lane_id();
        const auto failure=s.publication.classify_missing(value,0,s.publication.active_screen_generation(),true);
        omitted(value,failure.ordinal,owned::hud_publication_failure_stage_name(failure.stage),
            failure.logical_member.data(),failure.resource_failure);
    }}
    catch(...) {s.publication.invalidate_open_lane();}
    return true; // A claimed carrier is empty even when publication failed.
}
}

// Access only placement/scale and completed color values. No host mesh or
// atlas texture is consumed for the four renderer-owned icon lanes.
struct E3HudVisualAccess {
    static bool Portrait(CI3DFigure* figure, CRenderer* renderer,
        unsigned slot, int unitId, const char* texture, const char* base, bool model) {
        auto* gpu=renderer->GetGPU();
        figure->CreateCurrentPlacement();
        SHMatrix placement;figure->GetPlacement(placement);
        gpu->PushMainMatrix(placement);gpu->PushMainMatrix(figure->scale);
        struct Pop { CDXGeometryProcessor* gpu; ~Pop(){gpu->PopMainMatrix();gpu->PopMainMatrix();} } pop{gpu};
        renderer->SetProjectionMatrix();
        if(!gpu->IsHaveMatrix(MAT_STACK_PROJSPACE))return false;
        owned::EmbeddedHudPortraitInput input;
        const auto& clip=gpu->GetMatrix(MAT_STACK_PROJSPACE);
        memcpy(input.state.object_to_clip.data(),&clip,sizeof(clip));
        input.state.u_shift=figure->GetUShift();input.state.v_shift=figure->GetVShift();
        input.state.diffuse=figure->CheckFlag(FIG_USE_DEFAULT_COLORS)?figure->GetDefaultColor():
            RGBA_MAKE(int(figure->ambientLight.x*255),int(figure->ambientLight.y*255),int(figure->ambientLight.z*255),255);
        input.state.specular=figure->GetDefaultSpecular();
        input.state.color_sum=figure->pMaterial && figure->pMaterial->CheckFlag(MAT_USE_SPECULAR);
        if(figure->GetTextureTypeID()>=TTID_ANIMATED1 && figure->GetTextureTypeID()<=TTID_ANIMATED4) {
            auto* specific=static_cast<CFigureClientSpecific*>(figure->GetFigureSpecific());
            if(!specific)return false;
            specific->CalcAnimatedTextureFrame(theWorld.GetPreciseTime());
        }
        auto& view=input.model;
        view.positions=reinterpret_cast<const owned::NativePackedGroup*>(figure->plots4.GetData());
        view.position_groups=figure->plots4.GetSize();
        view.normals=reinterpret_cast<const owned::NativePackedGroup*>(figure->norms4.GetData());
        view.normal_groups=figure->norms4.GetSize();
        view.texture_coordinates=reinterpret_cast<const owned::NativeTextureCoordinate*>(figure->maps.GetData());
        view.texture_coordinate_count=figure->maps.GetSize();
        view.components=reinterpret_cast<const owned::NativeVertexComponent*>(figure->components.GetData());
        view.component_count=figure->components.GetSize();
        view.indices=figure->indices.GetData();view.index_count=figure->indices.GetSize();
        view.morphs=reinterpret_cast<const owned::NativeMorphOffset*>(figure->morphs.GetData());
        view.morph_count=figure->morphs.GetSize();
        if(figure->CheckFlag(FIG_LIGHTED) && !figure->CheckFlag(FIG_USE_DEFAULT_COLORS)) {
            view.diffuse=reinterpret_cast<const std::uint32_t*>(figure->colors.GetData());
            view.specular=reinterpret_cast<const std::uint32_t*>(figure->speculars.GetData());
        }
        input.slot=slot;input.unit_id=unitId;
        input.whole_atlas = !model;
        const bool hd=ModRuntime::Current() && ModRuntime::Current()->CombatSetting("JMVGraphics2");
        std::string selected,original;
        if(texture) {
            selected=std::string(texture)+(model&&hd?".x2.mmp":".mmp");input.texture_name=selected.c_str();
            if(base){original=std::string(base)+(model&&hd?".x2.mmp":".mmp");input.base_texture_name=original.c_str();}
            const unsigned size=model?(hd?512u:128u):256u;
            const unsigned atlas=model?size*2u:size;
            input.placement={atlas,size,size,size,size,model?(slot%2)*size:0u,model?((slot%4)/2)*size:0u,0u,0u,model&&hd?1u:0u};
        }
        return renderer->GetD3D()->SubmitHudPortrait(input);
    }
    static bool Draw(CI3DFigure* figure, CRenderer* renderer,
        const owned::HudPublishedVisual& visual) {
        auto* gpu=renderer->GetGPU();
        figure->CreateCurrentPlacement();
        SHMatrix placement;figure->GetPlacement(placement);
        gpu->PushMainMatrix(placement);gpu->PushMainMatrix(figure->scale);
        struct Pop { CDXGeometryProcessor* gpu; ~Pop(){gpu->PopMainMatrix();gpu->PopMainMatrix();} } pop{gpu};
        renderer->SetProjectionMatrix();
        if(!gpu->IsHaveMatrix(MAT_STACK_PROJSPACE))return false;
        owned::EmbeddedHudVisualInput input;
        const auto& clip=gpu->GetMatrix(MAT_STACK_PROJSPACE);
        static_assert(sizeof(clip)==sizeof(input.object_to_clip));
        memcpy(input.object_to_clip.data(),&clip,sizeof(clip));
        input.u_shift=figure->GetUShift();input.v_shift=figure->GetVShift();
        input.default_colors=figure->CheckFlag(FIG_USE_DEFAULT_COLORS)!=FALSE;
        input.diffuse=input.default_colors?figure->GetDefaultColor():renderer->GetAmbientColor();
        input.specular=figure->GetDefaultSpecular();
        auto* material=figure->GetMaterial();
        input.color_sum=material && material->CheckFlag(MAT_USE_SPECULAR);
        return renderer->GetD3D()->SubmitHudVisual(visual,input);
    }
};
namespace OwnedRender {
void E3HudDrawPortrait(CI3DFigure* figure, CRenderer* renderer,
    unsigned slot, int unitId, const char* texture, const char* base, bool model) noexcept {
    try {
        if(!figure || !renderer || !E3HudVisualAccess::Portrait(figure,renderer,slot,unitId,texture,base,model))
            omitted(owned::HudVisualLane::none,slot,"portrait_submission");
    } catch(...) {omitted(owned::HudVisualLane::none,slot,"portrait_exception");}
}
void E3HudDrawVisual(CI3DFigure* figure, CRenderer* renderer, E3HudLane value, unsigned ordinal) noexcept {
    if(figure && figure->CheckFlag(FIG_HIDDEN))return;
    const auto* visual=state().publication.find(lane(value),ordinal);
    if(!visual){
        const auto failure=state().publication.classify_missing(lane(value),ordinal,
            state().publication.active_screen_generation(),true);
        omitted(lane(value),failure.ordinal,owned::hud_publication_failure_stage_name(failure.stage),
            failure.logical_member.data(),failure.resource_failure);return;
    }
    try {
        if(!figure || !renderer || !E3HudVisualAccess::Draw(figure,renderer,*visual))
            omitted(lane(value),ordinal,"submission");
    } catch(...) {omitted(lane(value),ordinal,"submission_exception");}
}
}
