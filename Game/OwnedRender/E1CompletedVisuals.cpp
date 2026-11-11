#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../stdafx.h"
#include "E1VisualFrame.h"
#include "E1Scene.h"
#include "E3Combat.h"
#include "../WorldClient.h"
#include "../Object3DClientSpecific.h"
#include "../Unit.h"
#include "../Item.h"
#include "../ModEncounter.h"
#include "../LiveObserver.h"
#include "../UnitClientSpecific.h"
#include "embedded_host.h"

namespace OwnedRender {
namespace {
owned::MaterialCpuAsset completedMaterial(const CMaterial& source) {
    const auto& m=source.GetMaterial();
    owned::MaterialCpuAsset value;
    value.source=owned::MaterialValueSource::completed_native;
    value.diffuse={m.diffuse.r,m.diffuse.g,m.diffuse.b,m.diffuse.a};
    value.ambient={m.ambient.r,m.ambient.g,m.ambient.b,m.ambient.a};
    value.emissive={m.emissive.r,m.emissive.g,m.emissive.b,m.emissive.a};
    value.specular={m.specular.r,m.specular.g,m.specular.b,m.specular.a};
    value.power=m.power;
    D3DCOLORVALUE recent{},oldest{};
    if(source.GetStoredEmissive(recent,oldest)) {
        value.has_previous_highlight_emissive=value.has_oldest_highlight_emissive=true;
        value.previous_highlight_emissive={recent.r,recent.g,recent.b,recent.a};
        value.oldest_highlight_emissive={oldest.r,oldest.g,oldest.b,oldest.a};
    }
    return value;
}
bool sameMaterial(const owned::MaterialCpuAsset& a,const owned::MaterialCpuAsset& b) {
    return a.diffuse==b.diffuse && a.ambient==b.ambient && a.emissive==b.emissive &&
        a.specular==b.specular && a.power==b.power &&
        a.has_previous_highlight_emissive==b.has_previous_highlight_emissive &&
        a.previous_highlight_emissive==b.previous_highlight_emissive &&
        a.has_oldest_highlight_emissive==b.has_oldest_highlight_emissive &&
        a.oldest_highlight_emissive==b.oldest_highlight_emissive;
}
}

// Synchronous completed source values. No game object or mutable material is
// retained by c74; unchanged parts neither allocate nor publish an assembly.
bool E1CompleteVisualAppearance(CObject3DClientSpecific& specific,
    owned::VisualResourceIncarnation resource) noexcept try {
    auto& scene=E1Scene().Scene();
    const auto reject=[&](const char* reason) {
        scene.visual_reject(resource,owned::RetainedVisualStatus::unsupported_appearance,
            reason,"host.completed_appearance");
        return false;
    };
    const auto source=scene.visual_assembly(resource);
    if(!source)return reject("appearance.completed_assembly");
    const auto& previous=*source;
    std::shared_ptr<owned::VisualAssemblyAsset> changed;
    for(std::size_t i=0;i<source->parts.size();++i) {
        const auto& part=source->parts[i];
        auto* material=specific.FindMaterial(static_cast<ETypeIDs>(part.texture_type),
            static_cast<ETypeIDs>(part.material_type));
        auto* figure=specific.FindFigure(part.name.c_str());
        if(!material || !figure)return reject("appearance.completed_part");
        auto value=completedMaterial(*material);
        const bool hidden=figure->CheckFlag(FIG_HIDDEN) || part.geometry->indices.empty();
        const auto flags=material->CheckFlag(MAT_USE_SPECULAR)?MAT_USE_SPECULAR:0;
        if(sameMaterial(value,*previous.parts[i].material) &&
           hidden==previous.parts[i].explicitly_hidden && flags==previous.parts[i].material_flags)continue;
        if(!changed)changed=std::make_shared<owned::VisualAssemblyAsset>(previous);
        auto& target=changed->parts[i];
        target.material=std::make_shared<owned::MaterialCpuAsset>(std::move(value));
        target.explicitly_hidden=hidden;target.material_flags=flags;
    }
    if(changed) {
        // Complete publication before visual_frame_value takes its immutable
        // snapshot: admission, merged parts, depth and colour see one value.
        if(!scene.visual_publish_assembly(resource,std::move(changed),false))
            return reject("appearance.completed_publish");
    }
    return true;
} catch(...) {
    E1Scene().Scene().visual_reject(resource,owned::RetainedVisualStatus::unsupported_appearance,
        "appearance.completed_exception","host.completed_appearance");
    return false;
}

void E1CompleteNalaInput(CUnit* unit,owned::nala::Input& output) {
        output={};
        if (unit && (unit->GetID()==1000011214 || unit->GetID()==1100011214) &&
            std::strcmp(unit->GetMaskName(),"unhufe")==0) {
            auto& input=output;input.selected=true;
            const auto* shape=unit->GetBodyShape();
            if(!shape){unit->BindNalaSknBody();shape=unit->GetBodyShape();}
            input.valid=shape&&shape->valid;
            if(input.valid){
                input.controls=shape->controls;
                const auto& source=shape->input;
                input.shape_inputs={source.db[0],source.db[1],source.db[2],source.strength,source.dexterity};
            }
            const unsigned slots[]={3,4,6,5};
            const auto* client=dynamic_cast<CUnitClientSpecific*>(unit->GetUnitSpecific());
            for(unsigned i=0;i<4;++i) {
                const auto* armor=unit->GetQArmor(slots[i]);
                input.clothing[i]=client?client->GetArmorSubtype(slots[i]):armor?armor->GetSubtypeID():DWORD(-1);
            }
            const auto* stats=unit->GetUserUnitStats();
            for(unsigned i=0;i<6;++i){
                const auto& h=stats->hls[i];
                if(h.state==0)input.wounds[i]=0;
                else if(h.state==1||h.state==2)input.wounds[i]=3;
                else if(h.state==3&&h.wMaxHP&&h.wHP<=h.wMaxHP)
                    input.wounds[i]=h.wHP==h.wMaxHP?0:h.wHP>=h.wMaxHP*.66?1:h.wHP>=h.wMaxHP*.33?2:3;
                else input.valid=false;
            }
        }
}

// Capture only on the synchronous host seam, before either depth or colour.
// The retained scene still owns completed object poses; changing a frame's
// rendering class must never switch the scene to the retail route evaluator.
namespace {
bool completeVisualFrame(owned::RetainedVisualFrame& frame, bool combat) noexcept try {
    if (combat) E3CombatCatalog();
    for(auto& sample:frame.entities) {
        if(sample.status!=owned::RetainedVisualStatus::ready || !sample.assembly)continue;
        auto* object=theWorld.GetMapObject(sample.resource.signed_id);
        if(!object || object->WhoAmI()!=HPC_UNIT)continue;
        const auto current=E1Scene().Resource(object);
        if(current.serial!=sample.resource.serial)return false;
        auto* unit=static_cast<CUnit*>(object);
        if (combat) E3CombatPublishUnit(unit);
        E1CompleteNalaInput(unit,sample.values.nala);
        sample.completed_pose=true;
        sample.values.unit=true;
        sample.values.dead=unit->IsDead()!=FALSE;
        sample.values.invisible=unit->IsInvisible()!=FALSE;
        sample.values.pose=static_cast<std::uint8_t>(unit->GetCurrentPose());
        sample.values.moving=unit->IsMoving()!=FALSE;
        sample.values.locomotion=unit->GetUserUnitStats()->cLocomotion;
        sample.values.target_half_extent={unit->GetBoundingVolume().ptMax.x,
            unit->GetBoundingVolume().ptMax.y};
        if (auto* specific=dynamic_cast<CUnitClientSpecific*>(unit->GetUnitSpecific()))
            sample.values.fade_alpha=specific->GetFadeOutAlpha();
        sample.visibility.casts_shadow=object->CheckFlag(OBJ_NOSHADOW)==0;
        sample.values.object_flags=(sample.values.object_flags&~0x00400000u)|
            ((object->CheckFlag(UNIT_VISIBLE)||ModEncounter::Draw(*object))?0x00400000u:0u);
        // Both the skeleton and its bounds retain the exact host quaternion,
        // complection, animation phase and placement already in object_pose.
    }
    return true;
} catch(...) {
    owned::embedded_report_render_failure("object_render_plan_failure","embedded.completed_visuals");
    return false;
}
}
bool E1CompleteVisualFrame(owned::RetainedVisualFrame& frame) noexcept {
    const bool completed=completeVisualFrame(frame, true);
    if(completed)LiveObserver::ObserveVisuals(frame);
    return completed;
}
bool E1CompleteAmbientFrame(owned::RetainedVisualFrame& frame) noexcept {
    return completeVisualFrame(frame, false);
}
}
