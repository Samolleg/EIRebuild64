#include "contact_blend.h"
#include "ModelPreviewContext.h"
#include "model_preview_presentation.h"
#include "model_preview_producer.h"
#include "ModelPreviewLighting.h"
#include "../ModRuntime.h"
#include "../ModPackage.h"
// Read-only adapter over the same source rate used by SetExactPosition.
struct OwnedPreviewSourceAccess {
    static double Rate(const CUnit* unit) noexcept {
        if (!unit) return 1;
        if (unit->animationSpeed == UAS_FROZEN) return 0;
        const auto* mod = ModRuntime::Current();
        return unit->animationSpeed == UAS_MOVEMENT || (mod && mod->CombatSetting("AttackAnimSpeed"))
            ? unit->fpAnimationCoeff : 1;
    }
};
// Included by Object3DClientSpecific.cpp. Source pointers stay in this adapter;
// the renderer evaluates its immutable FIG/BON/LNK/ANM assembly and GPU pose.
BOOL CObject3DClientSpecific::DrawModelPreview(CRenderer* renderer) try
{
    const auto reject = [&](const char* stage) -> BOOL {
        static std::map<int, std::string> reported;
        auto& previous = reported[lpObject->GetID()];
        if (previous != stage) {
            previous = stage;
            GameJournal::Event("RENDER", "model_preview_rejected", "id=%d stage=%s",
                lpObject->GetID(), stage);
        }
        return FALSE;
    };
    auto* animation = lpObject->GetAnimationSystem2();
    if (!animation) return reject("preview.animation_source");
    // Camp/display copies can have negative ids and are deliberately absent
    // from E1Scene membership. Give the private UI resource its own namespace.
    if (!ownedPreviewResource) {
        static std::atomic<std::uint64_t> next{0};
        ownedPreviewResource = (std::uint64_t(1) << 63) | ++next;
    }
    if (!ownedPreviewAssembly) {
        const char* failure = nullptr;
        ownedPreviewAssembly = BuildOwnedAssembly(ownedPreviewRevision, &failure);
        if (!ownedPreviewAssembly) return reject(failure ? failure : "preview.assembly");
    }
    owned::RetainedVisualSample sample;
    sample.resource = {lpObject->GetID(), ownedPreviewResource};
    sample.lifecycle = {lpObject->GetID(), ownedPreviewResource, ownedPreviewResource};
    sample.assembly = ownedPreviewAssembly;
    sample.status = owned::RetainedVisualStatus::ready;
    bool registrySelected = false;
    const auto registryAsset = dynamic_cast<const CUnit*>(lpObject)
        ? owned::skn::select_gltf(sample.assembly->skeleton->visual_reference,
            sample.assembly->main_hero_visual, registrySelected) : nullptr;
    // Preserve the established FIG/SKN preview facts. Only a registry visual
    // needs unit group admission and a completed, privately placed pose.
    sample.values.unit = registrySelected;
    sample.completed_pose = registrySelected;
    sample.object_pose.shape = sample.assembly->shape;
    if (++ownedPreviewSerial == 0) ++ownedPreviewSerial;
    OwnedRender::E1AnimationSelection selected;
    if (!animation->GetOwnedCurrentAnimationSelection(selected))
        return reject("preview.selected_clip");
    sample.selection = selected.visual;
    // The source controller has completed its callbacks. The UI owns only the
    // selected clip's presentation anchor and a separate pose for each clock.
    const auto clock = OwnedRender::model_preview_time;
    if (!clock.supplied) return reject("preview.clock_source");
    if (!ownedPreviewViews) ownedPreviewViews = std::make_shared<owned::ModelPreviewViews>();
    auto& viewClock = ownedPreviewViews->at(clock.clock);
    if (!owned::prepare_model_preview_phase(viewClock, sample, ownedPreviewRevision,
            renderer->GetD3D()->ModelPreviewFrame(), clock.now, animation->GetTime(),
            OwnedPreviewSourceAccess::Rate(dynamic_cast<const CUnit*>(lpObject))))
        return reject("preview.phase");

#ifndef _A4
    if(auto* unit=dynamic_cast<CUnit*>(lpObject))
        if(auto* client=dynamic_cast<CUnitClientSpecific*>(unit->GetUnitSpecific()))
            client->PopulateOwnedVisibility(sample.visibility,sample.selection,sample.pose.phase);
#endif
    auto* gpu = renderer->GetGPU();
    if (!gpu->IsHaveMatrix(MAT_STACK_PROJSPACE)) return reject("preview.projection");
    std::array<float,16> worldToClip{}, objectToWorld{};
    std::memcpy(worldToClip.data(), &gpu->GetMatrix(MAT_STACK_PROJSPACE), sizeof(SHMatrix));
    if (owned::widescreen::interface_owned) {
        const auto& rect = renderer->GetWindowRect();
        owned::widescreen::compose_hud_clip(worldToClip.data(), owned::widescreen::hud_blit,
            float(rect.right - rect.left), float(rect.bottom - rect.top));
    }
    const auto* presentation = OwnedRender::model_preview_for(lpObject->GetID());
    owned::ModelPlacementFacts placement;
    const auto& sourceCP = lpObject->GetCP();
    placement.position = {sourceCP.x, sourceCP.y, sourceCP.z};
    lpObject->GetOrientation().GetComponents(placement.quaternion[0], placement.quaternion[1],
        placement.quaternion[2], placement.quaternion[3]);
    GetScaleCoeffs(placement.scale[0], placement.scale[1], placement.scale[2]);
    if (presentation && presentation->placed) placement = presentation->placement;
    if (!owned::prepare_model_placement(placement, objectToWorld)) return reject("preview.placement");
    sample.object_pose.position = placement.position;
    sample.object_pose.quaternion = placement.quaternion;
    struct Part { CFigure* figure; CMaterial* material; unsigned set; std::size_t ordinal; };
    std::vector<Part> parts;
    for (unsigned set = 0; set < 2; ++set) {
        if (!sets[set]) continue;
        for (int group = 0; group < sets[set]->matgroups.GetSize(); ++group) {
            const auto* source = sets[set]->matgroups[group];
            for (int index = 0; index < source->figures.GetSize(); ++index) {
                auto* figure = source->figures[index];
                const auto& assembly = *sample.assembly;
                const auto found = std::find_if(assembly.parts.begin(), assembly.parts.end(),
                    [&](const owned::VisualPartAsset& part) { return part.name == figure->GetName(); });
                if (found == assembly.parts.end()) return reject("preview.part_binding");
                parts.push_back({figure, source->lpMaterial, set, std::size_t(found - assembly.parts.begin())});
            }
        }
    }
    OwnedRender::ModelPreviewLighting illumination;
    if(!OwnedRender::produce_model_preview_lighting(worldToClip,placement.position,illumination))
        return reject("preview.studio_lighting");
    const BYTE fog = (presentation && presentation->default_fog) || lpObject->CheckFlag(OBJ_USE_DEFAULT_FOG) ? 255 :
        static_cast<CWorldClient*>(GetWorld())->GetTerrain()->GetObjectFogAlpha(lpObject->GetCP());
    const double textureTime = theWorld.GetPreciseTime();
    // Even an omitted preview must leave no wound binding for the next caller.
    struct Unbind { CRenderer* renderer; ~Unbind() { renderer->BindTexture(1, static_cast<CDXGISurface*>(nullptr)); } } unbind{renderer};
    struct FinishPreview {
        CDX11* renderer; bool finished = false;
        ~FinishPreview() { if (!finished) try { renderer->FlushC56CampFigures(); } catch (...) {} }
    } finish{renderer->GetD3D()};
    owned::ModelPartResult result;
    const owned::ModelPreviewInput input{sample, objectToWorld, worldToClip,
        textureTime, fog, ownedPreviewSerial, false};
    const bool drawn = owned::produce_model_preview(input, parts.size(),
        [&](std::size_t index, owned::ModelPartFacts& facts) {
            const auto& part = parts[index];
            facts.texture_set = part.set;
            facts.inactive = fabs2(part.figure->GetCP()) > 1e9f;
            // Current UI membership is authoritative, including ShowBodyPart.
            const auto& asset = sample.assembly->parts[part.ordinal];
            facts.hidden = part.figure->CheckFlag(FIG_HIDDEN) || !asset.geometry || asset.geometry->indices.empty();
            return true;
        },
        [&](unsigned set) {
            if (!renderer->GetD3D()->GetDevice()) {
                // A missing model texture must not become an untextured draw.
                // Publish the current atlas, including private equipment edits.
                const auto* texture = sets[set]->lpTexture;
                auto* surface = texture ? texture->GetSurface() : nullptr;
                if (!surface || !surface->EnsureOwnedSurfacePublished()) return false;
                if (set == 0 && pWoundsTexture) {
                    auto* wounds = pWoundsTexture->GetSurface();
                    if (!wounds || !wounds->EnsureOwnedSurfacePublished()) return false;
                }
            }
            return renderer->BindTexture(0, sets[set]->lpTexture) &&
                renderer->BindTexture(1, set == 0 ? pWoundsTexture : nullptr);
        },
        [&](std::size_t index, owned::ModelPreviewPart& output) {
            const auto& part = parts[index];
            output.ordinal = part.ordinal;
            owned::NativeLighting lighting;
            const bool overrideColors = presentation && presentation->default_colors;
            const bool defaults = overrideColors || part.figure->CheckFlag(FIG_USE_DEFAULT_COLORS) != 0;
            if (!defaults) {
                if (!part.material) return false;
                const auto& material = part.material->GetMaterial();
                if (!owned::make_native_figure_lighting(&material.diffuse.r, &material.emissive.r,
                        &material.specular.r, objectToWorld, lighting)) return false;
            }
            if(defaults)lighting=OwnedRender::model_preview_tinted_lighting(objectToWorld,
                overrideColors ? presentation->diffuse : part.figure->GetDefaultColor());
            output.material = {false,
                part.material && part.material->CheckFlag(MAT_USE_SPECULAR),
                overrideColors ? presentation->diffuse : defaults ? part.figure->GetDefaultColor() : 0xffffffffu,
                overrideColors ? presentation->specular : defaults ? part.figure->GetDefaultSpecular() : (std::uint32_t(fog) << 24),
                lighting, owned::NativeOrdering::preserve};
            return true;
        },
        [&](const owned::NativeGeometryView& view, const owned::ModelPreviewPart& part) {
            return renderer->GetD3D()->SubmitC74Figure(view,
                &illumination.world,
                illumination.points);
        },
        [](std::size_t, const owned::ModelPreviewPart&) { return true; }, result);
    // Interface submissions may be queued. Finish before this UI pose owner
    // can be reused by another view of the same object.
    const bool flushed = renderer->GetD3D()->FlushC56CampFigures();
    finish.finished = true;
    return drawn && flushed ? TRUE : reject("preview.submission");
}
catch (...) {
    GameJournal::Event("RENDER", "model_preview_rejected", "stage=preview.exception");
    return FALSE;
}
