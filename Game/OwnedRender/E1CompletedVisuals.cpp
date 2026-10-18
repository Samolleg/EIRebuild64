#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../stdafx.h"
#include "E1VisualFrame.h"
#include "E1Scene.h"
#include "../WorldClient.h"
#include "../Object3DClientSpecific.h"
#include "../Unit.h"
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

// Capture only on the synchronous host seam, before either depth or colour.
// The retained scene still owns completed object poses; changing a frame's
// rendering class must never switch the scene to the retail route evaluator.
bool E1CompleteVisualFrame(owned::RetainedVisualFrame& frame) noexcept try {
    for(auto& sample:frame.entities) {
        if(sample.status!=owned::RetainedVisualStatus::ready || !sample.assembly)continue;
        auto* object=theWorld.GetMapObject(sample.resource.signed_id);
        if(!object || object->WhoAmI()!=HPC_UNIT)continue;
        const auto current=E1Scene().Resource(object);
        if(current.serial!=sample.resource.serial)return false;
        auto* specific=static_cast<CObject3DClientSpecific*>(object->GetObjectSpecific());
        if(!specific)return false;
        const auto source=sample.assembly;
        const auto& previous=*source;
        std::shared_ptr<owned::VisualAssemblyAsset> changed;
        for(std::size_t i=0;i<source->parts.size();++i) {
            const auto& part=source->parts[i];
            auto* material=specific->FindMaterial(static_cast<ETypeIDs>(part.texture_type),
                static_cast<ETypeIDs>(part.material_type));
            auto* figure=specific->FindFigure(part.name.c_str());
            if(!material || !figure)return false;
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
            // Publish the immutable replacement as well as this frame's copy:
            // the normal scene journal invalidates colour/depth admission and
            // shadow reuse when alpha, highlight or part visibility changes.
            if(!E1Scene().Scene().visual_publish_assembly(sample.resource,changed))return false;
            sample.assembly=std::move(changed);
        }
        sample.completed_pose=true;
        sample.values.unit=true;
        sample.values.dead=static_cast<CUnit*>(object)->bDead!=FALSE;
        sample.visibility.casts_shadow=object->CheckFlag(OBJ_NOSHADOW)==0;
        sample.values.object_flags=(sample.values.object_flags&~0x00400000u)|
            (object->CheckFlag(UNIT_VISIBLE)?0x00400000u:0u);
        // Both the skeleton and its bounds retain the exact host quaternion,
        // complection, animation phase and placement already in object_pose.
    }
    return true;
} catch(...) {
    owned::embedded_report_render_failure("object_render_plan_failure","embedded.completed_visuals");
    return false;
}
}
