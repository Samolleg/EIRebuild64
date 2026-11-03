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
#include "../UnitClientSpecific.h"
#include "embedded_host.h"
#include "../GameJournal.h"
#include "owned_asset_loader.h"
#include <algorithm>
#include <array>

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
        const bool hidden=figure->CheckFlag(FIG_HIDDEN)!=0 || part.geometry->indices.empty();
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

// Capture only on the synchronous host seam, before either depth or colour.
// The retained scene still owns completed object poses; changing a frame's
// rendering class must never switch the scene to the retail route evaluator.
namespace {
// Temporary r6 observation: compare the live selected CFigures with the exact
// immutable frame given to depth/colour. Never infer visibility from BASE names.
void reportWeaponParts(CUnit& object, const owned::RetainedVisualSample& sample) noexcept try {
    static std::uint64_t epoch = 0;
    static std::array<std::uint64_t, 16> reported{};
    const auto currentEpoch = owned::bridge_current_world_epoch();
    if (epoch != currentEpoch) { epoch = currentEpoch; reported = {}; }
    if (!sample.resource.serial || std::find(reported.begin(), reported.end(),
            sample.resource.serial) != reported.end()) return;
    auto slot = std::find(reported.begin(), reported.end(), 0);
    if (slot == reported.end()) return;
    const auto spear = [](const std::string& name) {
        return name.find(".pike") != std::string::npos || name.find(".dpike") != std::string::npos;
    };
    bool hostSpear = false;
    for (int i = 0; i < object.GetNumFigures(); ++i)
        if (const auto* figure = object.GetFigure(i)) hostSpear |= spear(figure->GetName());
    if (!hostSpear && !std::any_of(sample.assembly->parts.begin(), sample.assembly->parts.end(),
            [&](const owned::VisualPartAsset& part) { return spear(part.name); })) return;
    std::string selected, retained;
    for (int i = 0; i < object.GetNumFigures(); ++i) {
        const auto* figure = object.GetFigure(i);
        if (!figure) continue;
        selected += std::string(figure->GetName()) + ":" +
            std::to_string(figure->GetFlags()) + ":" +
            std::to_string(fabs2(figure->GetCP()) > 1e9f) + ",";
    }
    for (const auto& part : sample.assembly->parts) {
        retained += part.name + ":" + std::to_string(part.skeleton_node) + ":" +
            (part.geometry ? part.geometry->identity.canonical_name : "missing") + ":" +
            std::to_string(part.geometry ? part.geometry->indices.size() : 0) + ":" +
            std::to_string(part.explicitly_hidden) + ",";
    }
    *slot = sample.resource.serial;
    GameJournal::Event("RENDER", "weapon_parts_frame",
        "epoch=%llu id=%d serial=%llu mask=%s revision=%llu host_name_flags_inactive=%s retained_name_node_asset_indices_hidden=%s",
        static_cast<unsigned long long>(epoch), sample.resource.signed_id,
        static_cast<unsigned long long>(sample.resource.serial), object.GetMaskName(),
        static_cast<unsigned long long>(sample.assembly->revision),
        GameJournal::Quote(selected.c_str()).c_str(), GameJournal::Quote(retained.c_str()).c_str());
} catch (...) {} // Observation failure cannot reject an otherwise valid frame.

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
            (object->CheckFlag(UNIT_VISIBLE)?0x00400000u:0u);
        reportWeaponParts(*unit, sample);
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
    return completeVisualFrame(frame, true);
}
bool E1CompleteAmbientFrame(owned::RetainedVisualFrame& frame) noexcept {
    return completeVisualFrame(frame, false);
}
}
