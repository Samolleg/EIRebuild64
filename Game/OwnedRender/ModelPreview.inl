#include "contact_blend.h"
#include "model_preview_clock.h"
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
    sample.object_pose.shape = sample.assembly->shape;
    if (++ownedPreviewSerial == 0) ++ownedPreviewSerial;
    OwnedRender::E1AnimationSelection selected;
    if (!animation->GetOwnedCurrentAnimationSelection(selected))
        return reject("preview.selected_clip");
    sample.selection = selected.visual;
    // The source controller has completed its callbacks. The UI owns only the
    // selected clip's presentation anchor and a separate pose for each clock.
    const auto clock = owned::model_preview_time;
    if (!clock.supplied) return reject("preview.clock_source");
    if (!ownedPreviewViews) ownedPreviewViews = std::make_shared<owned::ModelPreviewViews>();
    auto& viewClock = ownedPreviewViews->at(clock.clock);
    if (!owned::prepare_model_preview_phase(viewClock, sample, ownedPreviewRevision,
            renderer->GetD3D()->ModelPreviewFrame(), clock.now, animation->GetTime(),
            OwnedPreviewSourceAccess::Rate(dynamic_cast<const CUnit*>(lpObject))))
        return reject("preview.phase");

    auto* gpu = renderer->GetGPU();
    if (!gpu->IsHaveMatrix(MAT_STACK_PROJSPACE)) return reject("preview.projection");
    std::array<float,16> worldToClip{}, objectToWorld{};
    std::memcpy(worldToClip.data(), &gpu->GetMatrix(MAT_STACK_PROJSPACE), sizeof(SHMatrix));
    if (owned::widescreen::interface_owned) {
        const auto& rect = renderer->GetWindowRect();
        owned::widescreen::compose_hud_clip(worldToClip.data(), owned::widescreen::hud_blit,
            float(rect.right - rect.left), float(rect.bottom - rect.top));
    }
    SHMatrix placement, scale;
    lpObject->CreateCurrentPlacement();
    lpObject->GetPlacement(placement);
    if (GetScaleMatrix(scale)) placement = placement * scale;
    std::memcpy(objectToWorld.data(), &placement, sizeof(placement));
    owned::NativeWorldGeometry::Pose* pose = nullptr;
    if (!owned::retained_visual_gpu_pose(sample, pose) || !pose)
        return reject("preview.gpu_pose");
    pose->root_to_world = objectToWorld;

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
    // Read source light facts once per object, rather than for every body part.
    owned::NativeWorldLighting illumination;
    const DWORD ambient = renderer->GetAmbientColor();
    illumination.ambient = {float((ambient >> 16) & 255) / 255.f,
        float((ambient >> 8) & 255) / 255.f, float(ambient & 255) / 255.f, 1.f};
    illumination.sunlight_diffuse = {};
    if (const auto* sun = renderer->GetSunlight()) {
        const auto& color = sun->GetDiffuse(); const auto& direction = sun->GetDirection();
        illumination.sunlight_diffuse = {color.r, color.g, color.b, 1.f};
        illumination.sunlight_direction = {-direction.x, -direction.y, -direction.z, 0.f};
    }
    std::vector<owned::RetainedWorldPointLight> lights;
    CManagerIterator iterator;
    for (auto* light = static_cast<CLight*>(iterator.GetFirst(theWorld.GetLightManager())); light;
         light = static_cast<CLight*>(iterator.GetNext())) {
        if (light == renderer->GetSunlight() || light->CheckFlag(LIT_INACTIVE) ||
            !(light->GetRange() > 0.f) ||
            fabs2(lpObject->GetCP() - light->GetCP()) > square(light->GetRange() + 10.f)) continue;
        const auto& center = light->GetCP(); const auto& color = light->GetDiffuse();
        owned::RetainedWorldPointLight point;
        point.source_id = int(lights.size()) + 1; point.center = {center.x, center.y, center.z};
        point.color = {color.r, color.g, color.b}; point.radius = light->GetRange();
        lights.push_back(point);
    }
    const BYTE fog = lpObject->CheckFlag(OBJ_USE_DEFAULT_FOG) ? 255 :
        static_cast<CWorldClient*>(GetWorld())->GetTerrain()->GetFogAlpha(lpObject->GetCP());
    const double textureTime = theWorld.GetPreciseTime();
    // Even an omitted preview must leave no wound binding for the next caller.
    struct Unbind { CRenderer* renderer; ~Unbind() { renderer->BindTexture(1, static_cast<CDXGISurface*>(nullptr)); } } unbind{renderer};
    struct FinishPreview {
        CDX11* renderer; bool finished = false;
        ~FinishPreview() { if (!finished) try { renderer->FlushC56CampFigures(); } catch (...) {} }
    } finish{renderer->GetD3D()};
    owned::ModelPartResult result;
    const bool drawn = owned::produce_model_parts(parts.size(),
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
            return renderer->BindTexture(0, sets[set]->lpTexture) &&
                renderer->BindTexture(1, set == 0 ? pWoundsTexture : nullptr);
        },
        [&](std::size_t index, std::uint32_t& visible) {
            const auto& part = parts[index];
            owned::NativeGeometryView view;
            if (!owned::prepare_model_preview_geometry(sample, part.ordinal, *pose,
                    objectToWorld, worldToClip, textureTime, fog, ownedPreviewSerial, view)) return false;
            // A UI object has no world ground-contact layer. Keep unit/world
            // publication policy unchanged while submitting this separate view.
            view.flags &= ~owned::native_ground_contact_flag;
            view.fog = fog;
            owned::NativeLighting lighting;
            const bool defaults = part.figure->CheckFlag(FIG_USE_DEFAULT_COLORS) != 0;
            if (!defaults) {
                if (!part.material) return false;
                const auto& material = part.material->GetMaterial();
                if (!owned::make_native_figure_lighting(&material.diffuse.r, &material.emissive.r,
                        &material.specular.r, objectToWorld, lighting)) return false;
            }
            owned::apply_model_preview_material(view, {defaults,
                part.material && part.material->CheckFlag(MAT_USE_SPECULAR),
                defaults ? part.figure->GetDefaultColor() : 0xffffffffu,
                defaults ? part.figure->GetDefaultSpecular() : (std::uint32_t(fog) << 24),
                lighting, owned::NativeOrdering::preserve});
            // SubmitC74Figure uses the current typed texture bindings; it
            // completes lit submissions before restoring the world lights.
            if (!renderer->GetD3D()->SubmitC74Figure(view, defaults ? nullptr : &illumination, lights)) return false;
            visible = 1; // Producer result only; never FIG_VISIBLE/OBJ_VISIBLE.
            return true;
        }, result);
    // Unlit camp submissions may be queued. Finish before this UI pose owner
    // can be reused by another view of the same object.
    const bool flushed = renderer->GetD3D()->FlushC56CampFigures();
    finish.finished = true;
    return drawn && flushed ? TRUE : reject("preview.submission");
}
catch (...) {
    GameJournal::Event("RENDER", "model_preview_rejected", "stage=preview.exception");
    return FALSE;
}
