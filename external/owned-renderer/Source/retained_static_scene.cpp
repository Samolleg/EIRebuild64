#include "retained_static_scene.h"
#include "retained_static_submission.h"
#include "visual_highlight.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <iterator>
#include <set>
#include <utility>

namespace owned {
// Membership and resource loading share one scene without making a direct
// host publication depend on the loader/device implementation. Keep the same
// process lifetime as RuntimeState: normal Release resets it; DLL teardown
// must not destroy retained resources under the loader lock.
RetainedStaticScene& retained_visual_scene() noexcept {
    static RetainedStaticScene* scene = new RetainedStaticScene();
    return *scene;
}
using codec::io;
namespace {

struct ExclusiveLock {
    explicit ExclusiveLock(SRWLOCK& value) noexcept : lock(&value) {
        AcquireSRWLockExclusive(lock);
    }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(lock); }
    SRWLOCK* lock;
};

struct SharedLock {
    explicit SharedLock(SRWLOCK& value) noexcept : lock(&value) {
        AcquireSRWLockShared(lock);
    }
    ~SharedLock() { ReleaseSRWLockShared(lock); }
    SRWLOCK* lock;
};

void put_resource(RetainedDeltaOut& d, const VisualResourceIncarnation& resource) {
    io(d.out, resource.signed_id); io(d.out, resource.serial);
}

bool same_resource(const ResourceIdentity& a, const ResourceIdentity& b) noexcept {
    return a.container_incarnation == b.container_incarnation &&
        a.canonical_name == b.canonical_name && a.absolute_offset == b.absolute_offset &&
        a.bytes == b.bytes;
}

bool same_selection(const VisualClipSelection& a, const VisualClipSelection& b) noexcept {
    if (a.neutral || b.neutral) return a.neutral == b.neutral;
    return a.ordinal == b.ordinal && a.database && b.database && a.clip && b.clip &&
        same_resource(a.database->identity,b.database->identity) &&
        same_resource(a.clip->identity,b.clip->identity);
}

unsigned char ascii_lower(unsigned char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

bool resource_name_equal(const std::string& left,
                         const char* right) noexcept {
    if (!right) return false;
    std::size_t index = 0;
    while (index < left.size() && right[index]) {
        if (ascii_lower(static_cast<unsigned char>(left[index])) !=
            ascii_lower(static_cast<unsigned char>(right[index])))
            return false;
        ++index;
    }
    return index == left.size() && right[index] == '\0';
}

} // namespace

const char* retained_scene_identity_status_name(
    RetainedSceneIdentityStatus status) noexcept {
    switch (status) {
    case RetainedSceneIdentityStatus::unobserved: return "unobserved";
    case RetainedSceneIdentityStatus::selected: return "selected";
    case RetainedSceneIdentityStatus::loading: return "loading";
    case RetainedSceneIdentityStatus::published: return "published";
    }
    return "unobserved";
}

RetainedStaticBindingView::RetainedStaticBindingView(
    const RetainedStaticScene* scene) noexcept : scene_(scene) {
    if (scene_) {
        SharedLock guard(scene_->lock_);
        frame_ = scene_->frame_snapshot_unlocked();
    }
}

RetainedStaticBindingView::~RetainedStaticBindingView() { release(); }

RetainedStaticBindingView::RetainedStaticBindingView(
    RetainedStaticBindingView&& other) noexcept
    : scene_(std::exchange(other.scene_, nullptr)), frame_(other.frame_) {}

RetainedStaticBindingView& RetainedStaticBindingView::operator=(
    RetainedStaticBindingView&& other) noexcept {
    if (this == &other) return *this;
    release();
    scene_ = std::exchange(other.scene_, nullptr);
    frame_ = other.frame_;
    return *this;
}

void RetainedStaticBindingView::release() noexcept {
    scene_ = nullptr;
    frame_ = {};
}

bool RetainedStaticBindingView::current() const noexcept {
    if (!scene_) return false;
    SharedLock guard(scene_->lock_);
    return frame_.source_revision == scene_->source_revision_ &&
        frame_.binding_revision == scene_->binding_revision_;
}

bool RetainedStaticBindingView::empty() const noexcept {
    // This is only the captured empty fast path; binding() revalidates before
    // any candidate escapes. Do not add a second lock to every lookup.
    return !scene_ || frame_.binding_count == 0;
}

RetainedStaticFrameSnapshot
RetainedStaticBindingView::frame_snapshot() const noexcept {
    return current() ? frame_ : RetainedStaticFrameSnapshot{};
}

bool RetainedStaticBindingView::binding(
    std::int32_t signed_id, RetainedStaticBinding& result) const noexcept {
    result = {};
    if (!scene_) return false;
    SharedLock guard(scene_->lock_);
    return frame_.source_revision == scene_->source_revision_ &&
        frame_.binding_revision == scene_->binding_revision_ &&
        scene_->binding_unlocked(signed_id, result);
}

std::uint64_t RetainedStaticScene::begin_source(
    MobSourceRole role, const char* name, const char* selected_mob,
    bool* reused_existing) noexcept {
    if (reused_existing) *reused_existing = false;
    if (!name || !name[0]) return 0;
    if (selected_mob && !selected_mob[0]) return 0;
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::begin_source, [&](RetainedDeltaOut& d) {
            io(d.out, role); delta_text(d, name); delta_text(d, selected_mob);
        });
        if (next_transaction_ == (std::numeric_limits<std::uint64_t>::max)())
            return 0;
        const std::uint64_t serial = ++next_transaction_;
        bool reuse_published_main = false;
        if (role == MobSourceRole::main) {
            // The shared main loader is the authoritative world-source
            // transition for both fresh and serialized loads.  It may retain
            // the existing static MOB when both source names are unchanged;
            // keep only that exact main publication and retire supplements.
            selected_main_transition_ = 0;
            transactions_.clear();
            auto reusable = sources_.end();
            if (selected_mob) {
                reusable = std::find_if(
                    sources_.begin(), sources_.end(),
                    [name, selected_mob](const Source& source) {
                        return source.role == MobSourceRole::main &&
                            resource_name_equal(source.outer_name, name) &&
                            resource_name_equal(source.name, selected_mob);
                    });
            }
            std::vector<Source> next_sources;
            if (reusable != sources_.end()) {
                Source retained = *reusable;
                retained.transition_serial = serial;
                next_sources.push_back(std::move(retained));
                reuse_published_main = true;
            }
            const bool membership_changed =
                next_sources.size() != sources_.size();
            if (membership_changed) {
                if (!replace_sources(std::move(next_sources), true, nullptr,
                                     0, 0, false))
                    return 0;
            } else {
                sources_.swap(next_sources);
            }
        }
        Transaction value{};
        value.serial = serial;
        value.role = role;
        value.outer_name = name;
        if (selected_mob) {
            value.mob_name = selected_mob;
            value.mob_preselected = true;
        }
        value.reuse_published_main = reuse_published_main;
        transactions_.emplace(serial, std::move(value));
        if (role == MobSourceRole::main) {
            selected_main_transition_ = serial;
        }
        if (reused_existing) *reused_existing = reuse_published_main;
        return serial;
    } catch (...) {
        return 0;
    }
}

bool RetainedStaticScene::record_source(
    std::uint64_t transaction, const char* name) noexcept {
    if (!transaction || !name || !name[0]) return false;
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::record_parser_source, [&](RetainedDeltaOut& d) {
            io(d.out, transaction); delta_text(d, name);
        });
        const auto found = transactions_.find(transaction);
        if (found == transactions_.end() || found->second.engine_finished)
            return false;
        Transaction& value = found->second;
        if (value.source_confirmed) {
            // Each owner confirms exactly one selected source.
            // Refuse to mix admissions from an unestablished second source.
            value.discarded = true;
            return false;
        }
        if (value.mob_preselected) {
            if (!resource_name_equal(value.mob_name, name)) {
                value.discarded = true;
                return false;
            }
        } else {
            value.mob_name = name;
        }
        value.source_confirmed = true;
        return true;
    } catch (...) {
        return false;
    }
}

void RetainedStaticScene::record_admission(
    std::uint64_t transaction, std::uint32_t nid, std::uint32_t family,
    std::int32_t signed_id) noexcept {
    if (!transaction) return;
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::record_admission, [&](RetainedDeltaOut& d) {
            io(d.out, transaction); io(d.out, nid); io(d.out, family); io(d.out, signed_id);
        });
        const auto found = transactions_.find(transaction);
        if (found == transactions_.end() || found->second.engine_finished) return;
        found->second.admissions.push_back({nid, family, signed_id});
    } catch (...) {
    }
}

bool RetainedStaticScene::finish_source(
    std::uint64_t transaction, bool owner_success,
    bool common_source_confirmed, std::string& authoritative_name,
    std::string* terrain_resource, bool* reused_existing) noexcept {
    authoritative_name.clear();
    if (terrain_resource) terrain_resource->clear();
    if (reused_existing) *reused_existing = false;
    if (!transaction) return false;
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::finish_source, [&](RetainedDeltaOut& d) {
            io(d.out, transaction); io(d.out, owner_success); io(d.out, common_source_confirmed);
        });
        const auto found = transactions_.find(transaction);
        if (found == transactions_.end() || found->second.engine_finished)
            return false;
        Transaction& value = found->second;
        value.engine_finished = true;
        value.engine_success = owner_success;
        // A new source needs confirmation from its loader or accepted saved
        // document. Exact main reuse retains the already-established identity.
        value.discarded = value.discarded || !owner_success ||
            (!value.reuse_published_main &&
             (!common_source_confirmed || !value.source_confirmed));
        if (value.discarded) {
            if (selected_main_transition_ == value.serial)
                selected_main_transition_ = 0;
            if (value.reuse_published_main) {
                std::vector<Source> next_sources;
                next_sources.reserve(sources_.size());
                for (const Source& source : sources_)
                    if (source.role != MobSourceRole::main ||
                        source.transition_serial != transaction)
                        next_sources.push_back(source);
                if (next_sources.size() != sources_.size())
                    replace_sources(std::move(next_sources), true, nullptr,
                                    0, 0, false);
            }
            transactions_.erase(found);
            return false;
        }
        authoritative_name = value.mob_name;
        if (terrain_resource && value.role == MobSourceRole::main)
            *terrain_resource = value.outer_name;
        if (value.reuse_published_main) {
            if (reused_existing) *reused_existing = true;
            transactions_.erase(found);
        }
        return true;
    } catch (...) {
        return false;
    }
}

void RetainedStaticScene::abandon_source(std::uint64_t transaction) noexcept {
    if (!transaction) return;
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::abandon_source, [&](RetainedDeltaOut& d) { io(d.out, transaction); });
    const auto found = transactions_.find(transaction);
    if (found == transactions_.end()) return;
    if (selected_main_transition_ == transaction)
        selected_main_transition_ = 0;
    transactions_.erase(found);
}

bool RetainedStaticScene::publish_source(
    std::uint64_t transaction,
    std::shared_ptr<const OwnedMapSnapshot> snapshot_value) noexcept {
    if (!transaction || !snapshot_value) return false;
    // The loader's parse/publication boundary owns the complete immutable
    // terrain scan. Never admit an unvalidated descriptor and never repeat
    // that scan here or from per-frame terrain preparation.
    if (snapshot_value->terrain &&
        (!snapshot_value->terrain->immutable_descriptor_validated ||
         snapshot_value->terrain->immutable_descriptor_validation_count != 1u))
        return false;
    // Defined outside the scene lock (terrain and models are large); the
    // delta below is recorded only at the commit, because the unlocked
    // candidate phase can lose a race the replica would not reproduce.
    if (auto* r = recorder()) r->asset(snapshot_value);
    try {
        Source source{};
        std::shared_ptr<const TerrainCpuAsset> terrain;
        std::uint64_t source_revision = 0;
        {
            ExclusiveLock guard(lock_);
            const auto found = transactions_.find(transaction);
            if (found == transactions_.end() ||
                !found->second.engine_finished || found->second.discarded)
                return false;
            const Transaction& transaction_value = found->second;
            source.role = transaction_value.role;
            source.transition_serial =
                transaction_value.role == MobSourceRole::main
                ? transaction_value.serial : 0;
            source.outer_name = transaction_value.outer_name;
            source.name = transaction_value.mob_name;
            source.admissions = transaction_value.admissions;
            source.snapshot = snapshot_value;
            source_revision = source_revision_;
            if (transaction_value.role == MobSourceRole::main) {
                terrain = snapshot_value->terrain;
            } else {
                const auto main = std::find_if(
                    sources_.begin(), sources_.end(), [](const Source& value) {
                        return value.role == MobSourceRole::main;
                    });
                if (main != sources_.end() && main->snapshot)
                    terrain = main->snapshot->terrain;
            }
        }

        // Asset decode has already completed on the bounded loader workers.
        // Compile the scene-dependent immutable placement once outside the
        // scene lock, then reject it if source membership changed meanwhile.
        RetainedStaticPrepareDiagnostic publication_diagnostic{};
        publication_diagnostic.result = RetainedStaticPrepareResult::success;
        std::uint64_t candidate_preparations = 0;
        source.candidates.reserve(snapshot_value->objects.size());
        for (std::size_t index = 0;
             index < snapshot_value->objects.size(); ++index) {
            const StaticObjectSource& object = snapshot_value->objects[index];
            if (!retained_static_source_eligible(object, *snapshot_value))
                continue;
            ++candidate_preparations;
            std::shared_ptr<RetainedStaticSourceCandidate> candidate;
            RetainedStaticPrepareDiagnostic candidate_diagnostic{};
            const bool candidate_prepared = prepare_retained_static_candidate(
                snapshot_value, terrain, object, candidate,
                &candidate_diagnostic, index);
            if (!candidate) return false;
            candidate->prepared = candidate_prepared;
            candidate->preparation = candidate_diagnostic;
            source.candidates.push_back(std::move(candidate));
            if (!candidate_prepared && publication_diagnostic.result ==
                    RetainedStaticPrepareResult::success) {
                publication_diagnostic = candidate_diagnostic;
            }
        }

        ExclusiveLock guard(lock_);
        const auto found = transactions_.find(transaction);
        if (found == transactions_.end() ||
            !found->second.engine_finished || found->second.discarded ||
            found->second.role != source.role ||
            source_revision_ != source_revision)
            return false;
        std::vector<Source> next_sources;
        if (source.role != MobSourceRole::main)
            next_sources = sources_;
        next_sources.push_back(std::move(source));
        if (!replace_sources(
                std::move(next_sources), true, &publication_diagnostic,
                snapshot_value->objects.size(), candidate_preparations, true))
            return false;
        if (found->second.role == MobSourceRole::main) {
            ++source_generation_;
            // A main publication replaces the entire source list. Save its
            // revision under the same lock as the resource owner, before any
            // snapshot or value publication can observe it.
            sources_.back().publication_revision = source_revision_;
        }
        if (auto* r = recorder()) {
            r->delta(RetainedOp::publish_source, [&](RetainedDeltaOut& d) {
                io(d.out, transaction); d.ref(snapshot_value);
            });
        }
        transactions_.erase(found);
        return true;
    } catch (...) {
        return false;
    }
}

void RetainedStaticScene::lifecycle_added(
    const LifecycleIdentity& identity) noexcept {
    if (identity.id <= 0 || !identity.generation ||
        !identity.positive_world_epoch)
        return;
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::lifecycle_added, [&](RetainedDeltaOut& d) { io(d.out, identity); });
        const auto previous = live_.find(identity.id);
        const bool same_membership=previous!=live_.end()&&previous->second.generation==identity.generation&&
            previous->second.positive_world_epoch==identity.positive_world_epoch;
        if(!same_membership){
            quest_lights_.erase(std::remove_if(quest_lights_.begin(),quest_lights_.end(),
                [&](const RetainedQuestLight& light){return light.source.id==identity.id;}),quest_lights_.end());
            const auto visual=visuals_.find(identity.id);
            if(visual!=visuals_.end()&&visual->second.assembly)
                visual->second.prepared=std::make_shared<VisualPreparedResources>();
            visual_structure_changed_unlocked();
            visual_values_changed_unlocked(identity.id);
        }
        bool binding_changed = false;
        if (previous != live_.end() &&
            (previous->second.id != identity.id ||
             previous->second.generation != identity.generation ||
             previous->second.positive_world_epoch !=
                 identity.positive_world_epoch))
            binding_changed = retire_binding(previous->second);
        live_[identity.id] = identity;
        retire_replaced_detached_unlocked(identity.id);
        shadow_refresh_time_activity_unlocked(identity.id);
        positive_epoch_ = identity.positive_world_epoch;
        binding_changed = insert_binding(identity) || binding_changed;
        if (binding_changed) advance_binding_revision();
    } catch (...) {
    }
}

void RetainedStaticScene::lifecycle_removed(std::int32_t signed_id) noexcept {
    if (signed_id <= 0) return;
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::lifecycle_removed, [&](RetainedDeltaOut& d) { io(d.out, signed_id); });
    lifecycle_removed_unlocked(signed_id);
}

void RetainedStaticScene::lifecycle_removed_unlocked(std::int32_t signed_id) noexcept {
    const auto found = live_.find(signed_id);
    if (found == live_.end()) return;
    visual_structure_changed_unlocked();
    visual_values_changed_unlocked(signed_id);
    quest_lights_.erase(std::remove_if(quest_lights_.begin(),quest_lights_.end(),
        [&](const RetainedQuestLight& light){return light.source.id==signed_id;}),quest_lights_.end());
    const bool binding_changed = retire_binding(found->second);
    live_.erase(found);
    shadow_refresh_time_activity_unlocked(signed_id);
    const auto visual = visuals_.find(signed_id);
    if (visual != visuals_.end()) {
        visual->second.prepared.reset();
        visual->second.intent = {};
        visual->second.anchor = {};
        visual->second.effects = {};
        visual->second.effects_anchored = false;
        visual->second.route.reset();
        visual->second.has_object_pose = false;
        bridge_index_dirty_ |= !visual->second.bridge_planes.empty();
        visual->second.bridge_planes.clear();
        // Membership cannot repair a rejected resource/appearance publication.
        if (
            visual->second.status != RetainedVisualStatus::unsupported_appearance &&
            visual->second.status != RetainedVisualStatus::invalid_asset &&
            visual->second.status != RetainedVisualStatus::publication_fault &&
            visual->second.status != RetainedVisualStatus::unsupported_callback &&
            visual->second.status != RetainedVisualStatus::unsupported_selector) {
            visual->second.status = RetainedVisualStatus::awaiting_values;
            visual->second.readiness_reason = "route_retired_by_membership";
            visual->second.failure_scope = nullptr;
        }
    }
    if (binding_changed) advance_binding_revision();
}

void RetainedStaticScene::detached_changed_unlocked() noexcept {
    detached_count_.store(detached_.size(), std::memory_order_release);
    visual_structure_changed_unlocked();
}

bool RetainedStaticScene::visual_detach(std::int32_t signed_id) noexcept {
    if (signed_id <= 0) return false;
    try {
        ExclusiveLock guard(lock_);
        const auto member = live_.find(signed_id);
        const auto visual = visuals_.find(signed_id);
        if (member == live_.end() || visual == visuals_.end()) return false;
        const auto& entity = visual->second;
        // Only a complete static presentation can stand without its engine
        // object: units, pending construction and unavailable bytes retire.
        if (!entity.constructed || entity.status != RetainedVisualStatus::ready ||
            entity.tick.unit || !entity.has_object_pose || !entity.mesh_declared ||
            !entity.assembly || !entity.source_available ||
            entity.excluded_membership_generation == member->second.generation)
            return false;
        // A client surface can disappear inside the original removal. Only
        // detach after the renderer retained each such generation's copy.
        std::vector<DetachedSurfaceTexture> textures;
        for (const auto& part : entity.assembly->parts) {
            if (!part.surface || replica_) continue;
            if (std::any_of(textures.begin(), textures.end(), [&](const auto& texture) {
                    return texture.surface_id == part.surface->id &&
                        texture.generation == part.surface->generation;
                })) continue;
            auto texture = surface_texture_retainer_ ?
                surface_texture_retainer_(surface_texture_retainer_context_, *part.surface) : nullptr;
            if (!texture) return false;
            textures.push_back({part.surface->id, part.surface->generation, std::move(texture)});
        }
        // Allocate before taking the live value: bad_alloc must leave it intact.
        auto slot = detached_.try_emplace(signed_id).first;
        auto& detached = slot->second;
        detached.lifecycle = member->second;
        detached.textures = std::move(textures);
        detached.entity = std::move(visual->second);
        // Bridge planes are game collision data that leave with the object.
        bridge_index_dirty_ |= !detached.entity.bridge_planes.empty();
        detached.entity.bridge_planes.clear();
        visuals_.erase(visual);
        lifecycle_removed_unlocked(signed_id);
        detached_changed_unlocked();
        // Recorded only once it succeeded: success depends on the renderer's
        // texture retainer, which the replica does not have.
        if (auto* r = recorder()) r->delta(RetainedOp::visual_detach, [&](RetainedDeltaOut& d) { io(d.out, signed_id); });
        return true;
    } catch (...) {
        return false;
    }
}

// A ready member incarnation replaces the window-exited copy of its ID.
void RetainedStaticScene::retire_replaced_detached_unlocked(std::int32_t signed_id) noexcept {
    const auto copy = detached_.find(signed_id);
    if (copy == detached_.end()) return;
    const auto member = live_.find(signed_id);
    const auto visual = visuals_.find(signed_id);
    if (member == live_.end() || visual == visuals_.end() || !visual->second.constructed ||
        visual->second.status != RetainedVisualStatus::ready ||
        visual->second.excluded_membership_generation == member->second.generation)
        return;
    detached_.erase(copy);
    shadow_refresh_time_activity_unlocked(signed_id);
    detached_changed_unlocked();
}

// A live visual can reuse a detached ID: recompute each ID's time activity
// from what remains instead of erasing it.
void RetainedStaticScene::retire_all_detached_unlocked() noexcept {
    if (detached_.empty()) return;
    std::vector<std::int32_t> ids;
    try {
        ids.reserve(detached_.size());
        for (const auto& item : detached_) ids.push_back(item.first);
    } catch (...) { shadow_tracking_failed_ = true; }
    detached_.clear();
    for (const auto id : ids) shadow_refresh_time_activity_unlocked(id);
    detached_changed_unlocked();
}

// Server-side retirement of one copy (window exit or seed). Releases its
// renderer texture backing under the scene lock and recomputes the ID's time
// activity from what remains. The caller publishes detached_changed_unlocked.
std::map<std::int32_t, RetainedStaticScene::DetachedVisual>::iterator
RetainedStaticScene::retire_detached_unlocked(
    std::map<std::int32_t, DetachedVisual>::iterator copy) noexcept {
    const auto id = copy->first;
    const auto next = detached_.erase(copy);
    shadow_refresh_time_activity_unlocked(id);
    visual_values_changed_unlocked(id);
    return next;
}

void RetainedStaticScene::visual_retire_detached(std::int32_t signed_id) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_retire_detached, [&](RetainedDeltaOut& d) { io(d.out, signed_id); });
    const auto copy = detached_.find(signed_id);
    if (copy == detached_.end()) return;
    retire_detached_unlocked(copy);
    detached_changed_unlocked();
}

void RetainedStaticScene::visual_set_surface_texture_retainer(
    VisualSurfaceTextureRetainer retainer, void* context) noexcept {
    ExclusiveLock guard(lock_);
    surface_texture_retainer_ = retainer;
    surface_texture_retainer_context_ = context;
}

void RetainedStaticScene::visual_clear_surface_texture_retainer(void* context) noexcept {
    ExclusiveLock guard(lock_);
    if (surface_texture_retainer_context_ != context) return;
    if (auto* r = recorder()) r->delta(RetainedOp::retainer_cleared, [](RetainedDeltaOut&) {});
    surface_texture_retainer_ = nullptr;
    surface_texture_retainer_context_ = nullptr;
    // Copies belong to the departing device; never carry them to the next.
    retire_detached_using_unlocked([](const VisualPartAsset& part) { return bool(part.surface); });
}

std::shared_ptr<ID3D11Texture2D> RetainedStaticScene::visual_detached_surface_texture(
    std::uint64_t surface_id, std::uint64_t generation) const noexcept {
    SharedLock guard(lock_);
    for (const auto& item : detached_)
        for (const auto& texture : item.second.textures)
            if (texture.surface_id == surface_id && texture.generation == generation)
                return texture.texture;
    return nullptr;
}

bool RetainedStaticScene::visual_detached_surface_textures(
    std::vector<std::shared_ptr<ID3D11Texture2D>>& output) const noexcept {
    output.clear();
    SharedLock guard(lock_);
    try {
        for (const auto& item : detached_)
            for (const auto& texture : item.second.textures) output.push_back(texture.texture);
    } catch (...) {
        output.clear();
        return false;
    }
    return true;
}

bool RetainedStaticScene::visual_seed(std::int32_t signed_id,
    std::shared_ptr<const VisualAssemblyAsset> assembly, const VisualObjectPose& pose) noexcept {
    // A seed is a rigid, unanimated presentation: owned texture bytes only
    // (no client surface exists, so it never needs renderer texture backing
    // and its `textures` stay empty) and the neutral bind pose.
    if (signed_id <= 0 || !assembly || !assembly->revision || !assembly->skeleton ||
        assembly->parts.empty() || assembly->parts.size() > kVisualPartLimit ||
        !pose.selection.neutral || pose.selection.database || pose.selection.clip ||
        pose.phase != 0) return false;
    for (const auto& part : assembly->parts)
        if (!part.geometry || (part.geometry->indices.empty() && !part.explicitly_hidden) ||
            part.skeleton_node >= assembly->skeleton->nodes.size() ||
            !assembly->skeleton->nodes[part.skeleton_node].transform_available ||
            !part.material || !part.texture || part.surface || !part.source_available)
            return false;
    const auto finite = [](float value) { return std::isfinite(value); };
    std::array<float,16> matrix{};
    if (!std::all_of(pose.shape.begin(),pose.shape.end(),finite) ||
        !retained_object_matrix(pose.position,pose.quaternion,matrix)) return false;
    auto* r = recorder();
    if (r) r->asset(assembly);
    try {
        ExclusiveLock guard(lock_);
        if (r) r->delta(RetainedOp::visual_seed, [&](RetainedDeltaOut& d) {
            io(d.out, signed_id); d.ref(assembly); delta_io(d, pose);
        });
        if (live_.count(signed_id) || detached_.count(signed_id)) return false;
        DetachedVisual seed{};
        if (++next_seed_generation_ >= (1ull << 63)) next_seed_generation_ = 1;
        seed.lifecycle = {signed_id, (1ull << 63) | next_seed_generation_, positive_epoch_};
        seed.seeded = true;
        auto& entity = seed.entity;
        if (++next_visual_incarnation_ == 0) ++next_visual_incarnation_;
        entity.resource = {signed_id, next_visual_incarnation_};
        entity.assembly = std::move(assembly);
        entity.prepared = std::make_shared<VisualPreparedResources>();
        entity.mesh_declared = true;
        entity.constructed = true;
        entity.source_available = true;
        entity.object_pose = pose;
        entity.object_pose.appearance_revision = entity.assembly->revision;
        entity.has_object_pose = true;
        entity.tick.object_flags = pose.object_flags;
        entity.pose_revision = 1;
        if (!visual_refresh_envelope(entity, false)) return false;
        entity.status = RetainedVisualStatus::ready;
        entity.readiness_reason = nullptr;
        // Built entirely in a local: a bad_alloc here leaves no partial copy
        // and touches no live value.
        detached_.emplace(signed_id, std::move(seed));
        shadow_refresh_time_activity_unlocked(signed_id);
        visual_values_changed_unlocked(signed_id);
        detached_changed_unlocked();
        return true;
    } catch (...) {
        return false;
    }
}

void RetainedStaticScene::visual_reconcile_seeds(const std::vector<std::int32_t>& eligible,
    bool authoritative, std::vector<std::int32_t>& missing) noexcept {
    missing.clear();
    const auto retired = [&](std::int32_t id, const DetachedVisual& copy) {
        return authoritative ? !std::binary_search(eligible.begin(), eligible.end(), id) : copy.seeded;
    };
    try {
        bool retire = false;
        {
            SharedLock guard(lock_);
            for (const auto& item : detached_)
                if (retired(item.first, item.second)) { retire = true; break; }
            if (authoritative)
                for (const auto id : eligible)
                    if (!live_.count(id) && !detached_.count(id)) missing.push_back(id);
        }
        if (!retire) return;
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::visual_reconcile_seeds, [&](RetainedDeltaOut& d) {
            io(d.out, eligible); io(d.out, authoritative);
        });
        for (auto it = detached_.begin(); it != detached_.end();)
            it = retired(it->first, it->second) ? retire_detached_unlocked(it) : std::next(it);
        detached_changed_unlocked();
    } catch (...) {
        missing.clear();
    }
}

void RetainedStaticScene::retire_detached_using_unlocked(
    const std::function<bool(const VisualPartAsset&)>& uses) noexcept {
    bool changed = false;
    for (auto it = detached_.begin(); it != detached_.end();) {
        const auto& parts = it->second.entity.assembly->parts;
        if (std::any_of(parts.begin(), parts.end(), uses)) {
            shadow_source_changed_unlocked(it->first);
            const auto id=it->first;
            it = detached_.erase(it);
            shadow_refresh_time_activity_unlocked(id);
            changed = true;
        } else ++it;
    }
    if (changed) {
        visual_frame_changed_unlocked();
        detached_changed_unlocked();
    }
}

void RetainedStaticScene::positive_lifecycle_reset(
    std::uint64_t new_epoch) noexcept {
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::positive_lifecycle_reset, [&](RetainedDeltaOut& d) { io(d.out, new_epoch); });
        shadow_input_domain_.reset();
        // Main selection is the authoritative zone-source lifetime. Positive
        // client resets rebuild lifecycle identities beneath that selection;
        // they retire supplements and bindings but do not deselect the main.
        const std::uint64_t current_main = selected_main_transition_;
        std::vector<Source> next_sources;
        next_sources.reserve(sources_.size());
        for (const Source& source : sources_)
            if (current_main && source.role == MobSourceRole::main &&
                source.transition_serial == current_main)
                next_sources.push_back(source);
        const bool source_changed = next_sources.size() != sources_.size();
        for (auto iterator = transactions_.begin();
             iterator != transactions_.end();) {
            if (current_main && iterator->first == current_main &&
                iterator->second.role == MobSourceRole::main)
                ++iterator;
            else
                iterator = transactions_.erase(iterator);
        }
        visual_structure_changed_unlocked();
        for (auto iterator = live_.begin(); iterator != live_.end();) {
            if (iterator->first > 0)
                iterator = live_.erase(iterator);
            else
                ++iterator;
        }
        positive_epoch_ = new_epoch;
        retire_all_detached_unlocked();
        quest_lights_.clear();
        world_point_lights_.clear();fire_spells_.clear();
        visual_values_changed_unlocked();
        for(auto it=shadow_time_active_visuals_.begin();it!=shadow_time_active_visuals_.end();)
            if(*it>0)it=shadow_time_active_visuals_.erase(it);
            else ++it;
        for (auto& item : visuals_) {
            // The spatial index is retired below, including any surviving
            // negative-ID bridge. The next completed plane publication must
            // rebuild that index even when its numeric values are unchanged.
            item.second.bridge_planes.clear();
            if (item.first <= 0) continue;
            item.second.prepared.reset();
            item.second.intent = {};
            item.second.anchor = {};
            item.second.effects = {};
            item.second.effects_anchored = false;
            item.second.route.reset();
            item.second.has_object_pose = false;
            item.second.bridge_planes.clear();
            item.second.last_idle_event = 0;
            // Membership cannot repair a rejected resource/appearance publication.
            if (
                item.second.status != RetainedVisualStatus::unsupported_appearance &&
                item.second.status != RetainedVisualStatus::invalid_asset &&
                item.second.status != RetainedVisualStatus::publication_fault &&
                item.second.status != RetainedVisualStatus::unsupported_callback &&
                item.second.status != RetainedVisualStatus::unsupported_selector) {
                item.second.status = RetainedVisualStatus::awaiting_values;
                item.second.readiness_reason = "route_retired_by_reset";
                item.second.failure_scope = nullptr;
            }
        }
        bridge_cells_.clear();
        accepted_bridge_planes_.clear();
        bridge_index_dirty_ = false;
        visual_tick_time_ = -1;
        if (source_changed && !replace_sources(
                std::move(next_sources), true, nullptr, 0, 0, false)) {
            sources_.clear();
            source_index_.clear();
            collision_free_ = true;
            instrumentation_.binding_retirements += bindings_.size();
            bindings_.clear();
            geometry_refs_.clear();
            ++source_revision_;
            if (source_revision_ == 0) ++source_revision_;
            prepare_diagnostic_ = {};
            advance_binding_revision();
        } else if (!source_changed && !bindings_.empty()) {
            instrumentation_.binding_retirements += bindings_.size();
            bindings_.clear();
            geometry_refs_.clear();
            advance_binding_revision();
        }
    } catch (...) {
    }
}

void RetainedStaticScene::reset() noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::reset, [](RetainedDeltaOut&) {});
    shadow_input_domain_.reset();
    visual_structure_changed_unlocked();
    visual_values_changed_unlocked();
    shadow_time_active_visuals_.clear();
    shadow_time_active_terrain_sectors_.clear();
    shadow_wave_active_terrain_sectors_.clear();
    next_transaction_ = 0;
    source_generation_ = 0;
    // A reset must invalidate an outstanding generation token even if the next
    // source/load sequence repeats the previous generation and signed IDs.
    ++source_revision_;

    positive_epoch_ = 0;
    selected_main_transition_ = 0;
    transactions_.clear();
    sources_.clear();
    source_index_.clear();
    live_.clear();
    quest_lights_.clear();
    world_point_lights_.clear();fire_spells_.clear();
    visuals_.clear();
    detached_.clear();
    detached_count_.store(0, std::memory_order_release);
    bridge_cells_.clear();
    accepted_bridge_planes_.clear();
    bridge_index_dirty_ = false;
    visual_tick_time_ = -1;
    bindings_.clear();
    geometry_refs_.clear();
    collision_free_ = true;
    prepare_diagnostic_ = {};
    instrumentation_ = {};
    // Binding views can outlive one owned-resource runtime acquisition. Keep
    // this invalidation token monotonic so a later disjoint binding universe
    // cannot reuse revision-owned consumer storage without observing a change.
    advance_binding_revision();
}

bool RetainedStaticScene::build_source_index(
    const std::vector<Source>& sources, SourceIndex& index,
    bool& collision_free, std::uint64_t& candidate_visits) noexcept {
    index.clear();
    collision_free = true;
    candidate_visits = 0;
    try {
        using SourceKey = std::pair<std::uint32_t, std::uint32_t>;
        std::map<SourceKey, const Source*> candidate_source_by_key;
        std::set<std::int32_t> candidate_ids;
        std::vector<std::shared_ptr<const RetainedStaticSourceCandidate>>
            candidates;
        for (const Source& source : sources) {
            for (const auto& candidate : source.candidates) {
                ++candidate_visits;
                if (!candidate) continue;
                const SourceKey key{candidate->nid, candidate->family};
                const std::int32_t signed_id = candidate->signed_id;
                if (!candidate_source_by_key.emplace(key, &source).second ||
                    !candidate_ids.emplace(signed_id).second)
                    collision_free = false;
                candidates.push_back(candidate);
            }
        }
        if (!collision_free) return true;

        std::map<std::int32_t, const RetainedAdmission*> admission_by_id;
        std::set<SourceKey> seen_candidate_admissions;
        for (const Source& source : sources) {
            for (const RetainedAdmission& admission : source.admissions) {
                const SourceKey key{admission.nid, admission.family};
                const auto candidate_source = candidate_source_by_key.find(key);
                const bool is_candidate =
                    candidate_source != candidate_source_by_key.end() &&
                    candidate_source->second == &source;
                const bool targets_candidate_id = admission.signed_id > 0 &&
                    candidate_ids.count(admission.signed_id) != 0;
                if (!is_candidate && !targets_candidate_id) continue;

                if (is_candidate &&
                    !seen_candidate_admissions.emplace(key).second) {
                    collision_free = false;
                    continue;
                }

                // The shipped loader deliberately returns -1 for records
                // excluded by the current static/dynamic load mode. Such a
                // record did not enter the live ID domain and must not poison
                // an unrelated retained slice. A retained candidate that was
                // not admitted simply remains unclaimed and legacy-owned.
                if (admission.signed_id <= 0) continue;

                if (!admission.nid ||
                    admission.nid != static_cast<std::uint32_t>(
                        admission.signed_id) ||
                    !is_candidate ||
                    !admission_by_id.emplace(
                        admission.signed_id, &admission).second)
                    collision_free = false;
            }
        }
        if (!collision_free) return true;

        for (const auto& candidate : candidates) {
            const auto admission = admission_by_id.find(candidate->signed_id);
            if (admission == admission_by_id.end() ||
                admission->second->family != candidate->family)
                continue;
            index.emplace(admission->first, candidate);
        }
        return true;
    } catch (...) {
        index.clear();
        collision_free = false;
        return false;
    }
}

bool RetainedStaticScene::build_bindings(
    const SourceIndex& index, bool collision_free,
    const std::map<std::int32_t, LifecycleIdentity>& live,
    std::uint64_t positive_epoch, BindingMap& bindings,
    std::map<std::uintptr_t, std::size_t>& geometry_refs) noexcept {
    bindings.clear();
    geometry_refs.clear();
    if (!collision_free) return true;
    try {
        bindings.reserve(live.size());
        for (const auto& entry : live) {
            const LifecycleIdentity& identity = entry.second;
            if (identity.id != entry.first || identity.id <= 0 ||
                !identity.generation || !identity.positive_world_epoch ||
                (positive_epoch &&
                 identity.positive_world_epoch != positive_epoch))
                continue;
            const auto candidate = index.find(identity.id);
            if (candidate == index.end() || !candidate->second) continue;
            bindings.emplace(
                identity.id,
                RetainedStaticBinding{identity, candidate->second});
            if (candidate->second->prepared)
                for (const RetainedPreparedNode& node : candidate->second->nodes)
                    ++geometry_refs[node.geometry_identity];
        }
        return true;
    } catch (...) {
        bindings.clear();
        geometry_refs.clear();
        return false;
    }
}

bool RetainedStaticScene::replace_sources(
    std::vector<Source>&& sources, bool source_revision_changed,
    const RetainedStaticPrepareDiagnostic* prepare,
    std::uint64_t object_visits,
    std::uint64_t candidate_preparations,
    bool publication) noexcept {
    SourceIndex next_index;
    BindingMap next_bindings;
    std::map<std::uintptr_t, std::size_t> next_geometry_refs;
    bool next_collision_free = true;
    std::uint64_t candidate_visits = 0;
    if (!build_source_index(sources, next_index, next_collision_free,
                            candidate_visits) ||
        !build_bindings(next_index, next_collision_free, live_,
                        positive_epoch_, next_bindings,
                        next_geometry_refs))
        return false;

    std::uint64_t retired = 0;
    std::uint64_t inserted = 0;
    for (const auto& binding : bindings_) {
        const auto next = next_bindings.find(binding.first);
        if (next == next_bindings.end() ||
            next->second.candidate != binding.second.candidate) {
            ++retired;
        }
    }
    for (const auto& binding : next_bindings) {
        const auto previous = bindings_.find(binding.first);
        if (previous == bindings_.end() ||
            previous->second.candidate != binding.second.candidate)
            ++inserted;
    }

    sources_.swap(sources);
    if (std::none_of(sources_.begin(), sources_.end(), [](const Source& source) {
            return source.role == MobSourceRole::main;
        }))
        shadow_input_domain_.reset();
    // Sources reach shadow casters only through the main terrain (visual
    // entities never read sources_ or bindings_), so a source replacement is
    // a terrain change, not a reset of every visual caster. A full journal
    // reset here rebuilt the whole static caster store and sent every static
    // through slow admission on each supplement publication (#162).
    shadow_terrain_changed_unlocked(false);
    shadow_refresh_terrain_activity_unlocked();
    source_index_.swap(next_index);
    bindings_.swap(next_bindings);
    geometry_refs_.swap(next_geometry_refs);
    collision_free_ = next_collision_free;
    if (source_revision_changed) {
        ++source_revision_;
        if (source_revision_ == 0) ++source_revision_;
    }
    if (retired || inserted) advance_binding_revision();

    instrumentation_.source_index_rebuilds += 1;
    instrumentation_.source_index_candidate_visits += candidate_visits;
    instrumentation_.binding_retirements += retired;
    instrumentation_.binding_insertions += inserted;
    if (publication) {
        ++instrumentation_.source_publications;
        instrumentation_.source_object_visits += object_visits;
        instrumentation_.source_candidate_validations += object_visits;
        instrumentation_.source_candidate_preparations +=
            candidate_preparations;
    }

    prepare_diagnostic_ = prepare ? *prepare : RetainedStaticPrepareDiagnostic{};
    if (!collision_free_)
        prepare_diagnostic_.result =
            RetainedStaticPrepareResult::source_collision;
    else if (!prepare && !sources_.empty())
        prepare_diagnostic_.result = RetainedStaticPrepareResult::success;
    prepare_diagnostic_.binding_revision = binding_revision_;
    prepare_diagnostic_.source_bindings = source_index_.size();
    return true;
}

void RetainedStaticScene::advance_binding_revision() noexcept {
    ++binding_revision_;
    if (binding_revision_ == 0) ++binding_revision_;
    prepare_diagnostic_.binding_revision = binding_revision_;
}

bool RetainedStaticScene::retire_binding(
    const LifecycleIdentity& identity) noexcept {
    const auto found = bindings_.find(identity.id);
    if (found == bindings_.end() ||
        found->second.lifecycle.id != identity.id ||
        found->second.lifecycle.generation != identity.generation ||
        found->second.lifecycle.positive_world_epoch !=
            identity.positive_world_epoch)
        return false;
    if (found->second.candidate && found->second.candidate->prepared)
        for (const RetainedPreparedNode& node :
             found->second.candidate->nodes) {
            const auto geometry = geometry_refs_.find(node.geometry_identity);
            if (geometry == geometry_refs_.end()) continue;
            if (geometry->second > 1)
                --geometry->second;
            else
                geometry_refs_.erase(geometry);
        }
    bindings_.erase(found);
    ++instrumentation_.binding_retirements;
    return true;
}

bool RetainedStaticScene::insert_binding(
    const LifecycleIdentity& identity) noexcept {
    ++instrumentation_.lifecycle_lookups;
    if (!collision_free_ || identity.id <= 0 || !identity.generation ||
        !identity.positive_world_epoch ||
        (positive_epoch_ &&
         identity.positive_world_epoch != positive_epoch_))
        return false;
    const auto candidate = source_index_.find(identity.id);
    if (candidate == source_index_.end() || !candidate->second) return false;
    std::size_t geometry_insertions = 0;
    try {
        const auto inserted = bindings_.emplace(
            identity.id,
            RetainedStaticBinding{identity, candidate->second});
        if (!inserted.second) return false;
        if (candidate->second->prepared)
            for (const RetainedPreparedNode& node : candidate->second->nodes) {
                ++geometry_refs_[node.geometry_identity];
                ++geometry_insertions;
            }
        ++instrumentation_.binding_insertions;
        return true;
    } catch (...) {
        bindings_.erase(identity.id);
        for (std::size_t index = 0; index < geometry_insertions; ++index) {
            const auto geometry = geometry_refs_.find(
                candidate->second->nodes[index].geometry_identity);
            if (geometry == geometry_refs_.end()) continue;
            if (geometry->second > 1)
                --geometry->second;
            else
                geometry_refs_.erase(geometry);
        }
        return false;
    }
}

RetainedStaticSceneSnapshot RetainedStaticScene::snapshot() const {
    SharedLock guard(lock_);
    RetainedStaticSceneSnapshot result{};
    result.source_generation = source_generation_;
    result.source_revision = source_revision_;
    result.binding_revision = binding_revision_;
    result.collision_free = collision_free_;
    result.pending_transactions = transactions_.size();
    result.bindings.reserve(bindings_.size());
    for (const auto& binding : bindings_)
        result.bindings.push_back(binding.second);
    std::sort(result.bindings.begin(), result.bindings.end(),
              [](const RetainedStaticBinding& left,
                 const RetainedStaticBinding& right) {
                  if (left.lifecycle.id != right.lifecycle.id)
                      return left.lifecycle.id < right.lifecycle.id;
                  if (left.lifecycle.generation != right.lifecycle.generation)
                      return left.lifecycle.generation <
                          right.lifecycle.generation;
                  return left.lifecycle.positive_world_epoch <
                      right.lifecycle.positive_world_epoch;
              });
    result.source_membership.reserve(sources_.size());
    for (const Source& source : sources_) {
        result.source_membership.push_back(source.name);
        if (source.role == MobSourceRole::main && source.snapshot)
            result.terrain = source.snapshot->terrain;
    }
    return result;
}

RetainedTerrainSourceSnapshot
RetainedStaticScene::terrain_source_snapshot() const noexcept {
    SharedLock guard(lock_);
    RetainedTerrainSourceSnapshot result{};
    for (const Source& source : sources_) {
        if (source.role != MobSourceRole::main || !source.snapshot) continue;
        result.source_generation = source_generation_;
        result.source_revision = source.publication_revision;
        result.terrain = source.snapshot->terrain;
        result.geometry = source.terrain_geometry;
        result.prepared = source.terrain_prepared;
        result.textures = source.terrain_textures;
        result.map = source.snapshot;
        break;
    }
    return result;
}

RetainedTerrainSourceIdentity
RetainedStaticScene::terrain_source_identity() const noexcept {
    SharedLock guard(lock_);
    for (const Source& source : sources_)
        if (source.role == MobSourceRole::main && source.snapshot)
            return {source_generation_, source.publication_revision};
    return {};
}

RetainedStaticBindingView RetainedStaticScene::binding_view() const noexcept {
    return RetainedStaticBindingView(this);
}

RetainedStaticFrameSnapshot RetainedStaticScene::frame_snapshot() const noexcept {
    SharedLock guard(lock_);
    return frame_snapshot_unlocked();
}

RetainedStaticFrameSnapshot
RetainedStaticScene::frame_snapshot_unlocked() const noexcept {
    RetainedStaticFrameSnapshot result{};
    result.source_revision = source_revision_;
    result.binding_revision = binding_revision_;
    result.binding_count = bindings_.size();
    result.unique_geometry_variants = geometry_refs_.size();
    result.collision_free = collision_free_;
    result.prepare = prepare_diagnostic_;
    return result;
}

RetainedStaticInstrumentation RetainedStaticScene::instrumentation() const noexcept {
    SharedLock guard(lock_);
    return instrumentation_;
}

bool RetainedStaticScene::binding(
    std::int32_t signed_id, RetainedStaticBinding& result) const noexcept {
    result = {};
    if (signed_id <= 0) return false;
    SharedLock guard(lock_);
    return binding_unlocked(signed_id, result);
}

#ifdef EID3D11_TESTING
std::shared_ptr<const OwnedMapSnapshot> RetainedStaticScene::published_source_for_test(MobSourceRole role) const noexcept {
    SharedLock guard(lock_);
    for (auto source=sources_.rbegin();source!=sources_.rend();++source)
        if (source->role==role) return source->snapshot;
    return nullptr;
}
#endif

bool RetainedStaticScene::binding_unlocked(
    std::int32_t signed_id, RetainedStaticBinding& result) const noexcept {
    if (signed_id <= 0) return false;
    const auto found = bindings_.find(signed_id);
    if (found == bindings_.end() || !found->second.candidate) return false;
    if (found->second.lifecycle.id != signed_id) return false;
    result = found->second;
    return true;
}

RetainedSceneIdentitySnapshot RetainedStaticScene::identity_snapshot() const {
    SharedLock guard(lock_);
    RetainedSceneIdentitySnapshot result{};
    result.source_generation = source_generation_;
    result.source_revision = source_revision_;
    result.binding_revision = binding_revision_;
    result.positive_world_epoch = positive_epoch_;
    result.pending_transactions = transactions_.size();

    const auto published_main = std::find_if(
        sources_.begin(), sources_.end(), [](const Source& source) {
            return source.role == MobSourceRole::main;
        });
    if (published_main != sources_.end()) {
        result.status = RetainedSceneIdentityStatus::published;
        result.main_outer_resource = published_main->outer_name;
        result.main_mob_resource = published_main->name;
    } else {
        const auto selected_main = std::find_if(
            transactions_.rbegin(), transactions_.rend(),
            [](const auto& entry) {
                return entry.second.role == MobSourceRole::main;
            });
        if (selected_main != transactions_.rend()) {
            const Transaction& transaction = selected_main->second;
            result.status = transaction.engine_finished
                ? RetainedSceneIdentityStatus::loading
                : RetainedSceneIdentityStatus::selected;
            result.main_outer_resource = transaction.outer_name;
            if (transaction.source_confirmed)
                result.main_mob_resource = transaction.mob_name;
        }
    }

    const std::size_t copied = std::min(
        sources_.size(), kRetainedSceneIdentitySourceLimit);
    result.published_source_membership.reserve(copied);
    for (std::size_t index = 0; index < copied; ++index)
        result.published_source_membership.push_back(sources_[index].name);
    result.published_source_membership_truncated =
        copied != sources_.size();
    return result;
}

RetainedStaticScene::VisualEntity* RetainedStaticScene::visual_find_unlocked(
    VisualResourceIncarnation resource) noexcept {
    const auto found = visuals_.find(resource.signed_id);
    return found != visuals_.end() && resource.serial &&
        found->second.resource.serial == resource.serial ? &found->second : nullptr;
}

bool RetainedStaticScene::publish_terrain_geometry(std::uint64_t generation,
    std::uint64_t revision, const OwnedTerrainGeometryValues* incoming) noexcept {
    ExclusiveLock guard(lock_);
    // Recorded only where it mutates: an unchanged per-frame publication
    // (the common case) changes nothing and costs no message.
    const auto record = [&] {
        if (auto* r = recorder()) r->delta(RetainedOp::publish_terrain_geometry, [&](RetainedDeltaOut& d) {
            io(d.out, generation); io(d.out, revision);
            const bool present = incoming != nullptr;
            io(d.out, present);
            if (present) delta_io(d, *incoming);
        });
    };
    for (auto& source : sources_) {
        if (source.role != MobSourceRole::main || !source.snapshot || !source.snapshot->terrain) continue;
        if (generation != source_generation_ || revision != source.publication_revision) return false;
        if (!incoming) {
            record();
            if(source.terrain_geometry)visual_frame_changed_unlocked();
            source.terrain_geometry.reset();
            source.terrain_textures.reset();
            shadow_refresh_terrain_activity_unlocked();
            return true;
        }
        const auto& values=*incoming;
        const auto& terrain=*source.snapshot->terrain;
        if (values.layer_shifts.size()!=terrain.objects.size() ||
            values.animation.animated_tiles_enabled || values.animation.animation_phase_count ||
            values.animation.waves_enabled>1 || values.animation.fog_enabled>1) return false;
        for (float value:values.layer_shifts) if (!std::isfinite(value)) return false;
        for (float value:values.wind_noise) if (!std::isfinite(value)) return false;
        const auto& a=values.animation;
        const float scalars[]{a.wind_direction[0],a.wind_direction[1],a.wind_force,
            a.wind_force_normalized,a.wind_phase,a.precise_time,a.maximum_wind_multiplier,
            a.fog_min[0],a.fog_min[1],a.fog_max[0],a.fog_max[1],a.fog_distance};
        for (float value:scalars) if (!std::isfinite(value)) return false;
        const auto& previous=source.terrain_geometry;
        // Layer shifts (game water levels) are shader values: they never
        // change the structural revision or any geometry key. Only the map's
        // shift range, which bounds cover, may widen (rarely).
        const bool same_structure=previous && previous->wind_noise==values.wind_noise;
        const bool same_shifts=previous && previous->layer_shifts==values.layer_shifts;
        if (same_structure && same_shifts &&
            std::memcmp(&previous->animation,&values.animation,sizeof(a))==0) return true;
        record();
        try {
            auto next=std::make_shared<OwnedTerrainGeometryValues>(values);
            next->layer_shift_range=previous && previous->layer_shift_range.size()==values.layer_shifts.size()
                ? previous->layer_shift_range : std::vector<std::array<float,2>>(values.layer_shifts.size(),{0.f,0.f});
            grow_owned_terrain_layer_shift_range(next->layer_shift_range,values.layer_shifts);
            next->revision=source.terrain_geometry_serial+1;
            next->structural_revision=same_structure ? previous->structural_revision : next->revision;
            next->noise_revision=previous && previous->wind_noise==values.wind_noise
                ? previous->noise_revision : next->revision;
            if (!next->revision || !next->structural_revision || !next->noise_revision) return false;
            if (!source.terrain_prepared) source.terrain_prepared=std::make_shared<OwnedTerrainPreparedResources>();
            if(!previous)visual_frame_changed_unlocked();
            const bool activity_changed=!previous ||
                previous->animation.waves_enabled!=values.animation.waves_enabled;
            source.terrain_geometry_serial=next->revision;
            source.terrain_geometry=std::move(next);
            // Base caster geometry is immutable; shifts and wind belong to upper layers.
            if(activity_changed)shadow_refresh_terrain_activity_unlocked();
            return true;
        } catch (...) { return false; }
    }
    return false;
}

bool RetainedStaticScene::publish_terrain_texture(std::uint64_t generation,
    std::uint64_t revision, std::uint32_t material,
    std::shared_ptr<const OwnedVisualSurface> surface) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::publish_terrain_texture, [&](RetainedDeltaOut& d) {
        io(d.out, generation); io(d.out, revision); io(d.out, material); d.ref(surface);
    });
    if (surface && (!surface->id || !surface->generation)) return false;
    for (auto& source : sources_) {
        if (source.role != MobSourceRole::main || !source.snapshot || !source.snapshot->terrain) continue;
        if (generation != source_generation_ || revision != source.publication_revision) return false;
        const auto count = std::uint64_t(source.snapshot->terrain->texture_count) + 2u;
        if (count > 4096u || material >= count) return false;
        try {
            const auto& previous = source.terrain_textures;
            if (previous) {
                const auto found = previous->find(material);
                if (found != previous->end() && found->second.available == bool(surface) &&
                    (!surface || (found->second.surface && found->second.surface->id == surface->id &&
                                  found->second.surface->generation == surface->generation))) return true;
            }
            auto next = previous ? std::make_shared<RetainedTerrainTextures>(*previous)
                                 : std::make_shared<RetainedTerrainTextures>();
            auto& binding = (*next)[material];
            binding.available = bool(surface);
            if (surface) binding.surface = std::move(surface);
            source.terrain_textures = std::move(next);
            // Terrain textures affect receivers/color only, never depth.
            return true;
        } catch (...) { return false; }
    }
    return false;
}

const RetainedStaticScene::VisualEntity* RetainedStaticScene::visual_find_unlocked(
    VisualResourceIncarnation resource) const noexcept {
    const auto found = visuals_.find(resource.signed_id);
    return found != visuals_.end() && resource.serial &&
        found->second.resource.serial == resource.serial ? &found->second : nullptr;
}

VisualResourceIncarnation RetainedStaticScene::visual_begin_resource(
    std::int32_t signed_id) noexcept {
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::visual_begin_resource, [&](RetainedDeltaOut& d) { io(d.out, signed_id); });
        VisualEntity next{};
        visual_structure_changed_unlocked();
        visual_values_changed_unlocked(signed_id);
        if (++next_visual_incarnation_ == 0) ++next_visual_incarnation_;
        next.resource = {signed_id, next_visual_incarnation_};
        const auto member = live_.find(signed_id);
        if (member != live_.end())
            next.excluded_membership_generation = member->second.generation;
        const auto previous=visuals_.find(signed_id);
        bridge_index_dirty_ |= previous!=visuals_.end() && !previous->second.bridge_planes.empty();
        visuals_[signed_id] = std::move(next);
        shadow_refresh_time_activity_unlocked(signed_id);
        return visuals_.at(signed_id).resource;
    } catch (...) { return {}; }
}

bool RetainedStaticScene::visual_finish_resource(
    VisualResourceIncarnation resource, bool success) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_finish_resource, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); io(d.out, success);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity) return false;
    if (!success) {
        visual_structure_changed_unlocked();
        visual_values_changed_unlocked(resource.signed_id);
        bridge_index_dirty_ |= !entity->bridge_planes.empty();
        visuals_.erase(resource.signed_id);
        shadow_refresh_time_activity_unlocked(resource.signed_id);
        return true;
    }
    entity->constructed = true;
    visual_refresh_status(*entity);
    return true;
}

void RetainedStaticScene::visual_destroy_resource(std::int32_t signed_id) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_destroy_resource, [&](RetainedDeltaOut& d) { io(d.out, signed_id); });
    visual_values_changed_unlocked(signed_id);
    const auto previous=visuals_.find(signed_id);
    if(previous==visuals_.end())return;
    visual_structure_changed_unlocked();
    bridge_index_dirty_ |= !previous->second.bridge_planes.empty();
    visuals_.erase(signed_id);
    shadow_refresh_time_activity_unlocked(signed_id);
}

VisualResourceIncarnation RetainedStaticScene::visual_resource(
    std::int32_t signed_id) const noexcept {
    SharedLock guard(lock_);
    const auto found = visuals_.find(signed_id);
    return found == visuals_.end() ? VisualResourceIncarnation{} : found->second.resource;
}

bool RetainedStaticScene::visual_declare_mesh(VisualResourceIncarnation resource, bool present) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_declare_mesh, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); io(d.out, present);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity) return false;
    if (entity->mesh_declared == present) return true;
    entity->mesh_declared = present;
    if (!present) {
        entity->assembly.reset();
        entity->prepared.reset();
        entity->envelope_radius = 0;
    }
    visual_refresh_status(*entity);
    return true;
}

bool RetainedStaticScene::visual_claims_mesh(const LifecycleIdentity& identity) const noexcept {
    SharedLock guard(lock_);
    const auto member = live_.find(identity.id);
    const auto entity = visuals_.find(identity.id);
    return member != live_.end() && entity != visuals_.end() && entity->second.mesh_declared &&
        entity->second.excluded_membership_generation != identity.generation &&
        member->second.generation == identity.generation &&
        member->second.positive_world_epoch == identity.positive_world_epoch;
}

bool RetainedStaticScene::visual_bind_database(VisualResourceIncarnation resource,
    std::uint64_t source_generation,
    std::shared_ptr<const AnimationDatabaseAsset> database) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_bind_database, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); io(d.out, source_generation); d.ref(database);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity || !source_generation || !database ||
        !database->identity.container_incarnation || database->identity.canonical_name.empty()) return false;
    // The game's ADB getter caches once per resource incarnation. A new visual
    // mask cannot silently replace that binding while the object survives.
    if (entity->database) return entity->resolver_generation == source_generation &&
        same_resource(entity->database->identity, database->identity);
    visual_values_changed_unlocked(resource.signed_id);entity->database = std::move(database);
    entity->resolver_generation = source_generation;
    return true;
}

std::shared_ptr<const AnimationDatabaseAsset> RetainedStaticScene::visual_database(
    VisualResourceIncarnation resource, std::uint64_t& source_generation) const noexcept {
    SharedLock guard(lock_);
    const auto* entity = visual_find_unlocked(resource);
    source_generation = entity ? entity->resolver_generation : 0;
    return entity ? entity->database : nullptr;
}

void RetainedStaticScene::visual_refresh_status(VisualEntity& entity, bool invalidate) noexcept {
    if(invalidate)visual_values_changed_unlocked(entity.resource.signed_id);
    if (entity.detection_unavailable) return;
    if (entity.status != RetainedVisualStatus::constructing &&
        entity.status != RetainedVisualStatus::awaiting_values &&
        entity.status != RetainedVisualStatus::ready) return;
    if (!entity.constructed) { entity.status = RetainedVisualStatus::constructing; return; }
    // Record the first predicate of the existing short-circuit readiness test.
    // No later diagnostics pass rereads or reconstructs this decision.
    entity.failure_scope = nullptr;
    entity.readiness_reason = !entity.mesh_declared ? "mesh_absent" : !entity.assembly ? "assembly" :
        !entity.tick.unit ? (!entity.has_object_pose ? "object_pose" : nullptr) :
        !entity.route ? "route" : !entity.intent.revision ? "intent_revision" :
        entity.anchor.intent_revision != entity.intent.revision ? "anchor_intent_revision" :
        entity.tick.rate.tilted_facing && !entity.anchor.root_rotation_valid ? "root_rotation" : nullptr;
    entity.status = entity.readiness_reason ? RetainedVisualStatus::awaiting_values : RetainedVisualStatus::ready;
    if (entity.status == RetainedVisualStatus::ready) retire_replaced_detached_unlocked(entity.resource.signed_id);
    if (entity.status == RetainedVisualStatus::ready && !entity.prepared) {
        try { entity.prepared=std::make_shared<VisualPreparedResources>(); }
        catch (...) {
            entity.status=RetainedVisualStatus::invalid_asset;
            entity.readiness_reason="prepared_allocation";
        }
    }
}

bool RetainedStaticScene::visual_refresh_envelope(VisualEntity& entity, bool invalidate) noexcept {
    if(invalidate)visual_values_changed_unlocked(entity.resource.signed_id);
    if (!entity.assembly) return true;
    try {
    const auto& assembly = *entity.assembly;
    const auto& shape = !entity.tick.unit && entity.has_object_pose ? entity.object_pose.shape : assembly.shape;
    const std::vector<VisualClipSelection> object_selection = !entity.tick.unit && entity.has_object_pose
        ? std::vector<VisualClipSelection>{entity.object_pose.selection} : std::vector<VisualClipSelection>{};
    const auto& selections = !entity.tick.unit && entity.has_object_pose ? object_selection : entity.intent.selections;
    float radius = 0, rotation_scale = 1;
    if (!visual_model_envelope(*assembly.skeleton, selections,
        shape, assembly.center_shift, radius, &rotation_scale)) return false;
    if (selections.empty()) {
        // Unanimated rigid sources retain the BON root offset. The animated
        // root envelope above deliberately models the ANM/center-shift route.
        double root_radius = 0;
        for (std::size_t i = 0; i < assembly.skeleton->nodes.size(); ++i) {
            if (!assembly.skeleton->nodes[i].transform_available) continue;
            if (assembly.skeleton->links[i].parent_index != UINT_MAX) continue;
            const auto& bone = assembly.skeleton->nodes[i].bone_positions;
            if (bone.size() != 3 && bone.size() != 24) return false;
            double complete = 0;
            for (unsigned endpoint = 0; endpoint < bone.size()/3; ++endpoint) {
                double weight = 1, squared = 0;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    if (bone.size() == 24)
                        weight *= endpoint & (1u << axis) ? shape[axis] : 1.0-shape[axis];
                    const double value = bone[endpoint*3+axis];
                    squared += value*value;
                }
                complete += std::fabs(weight)*std::sqrt(squared);
            }
            root_radius = (std::max)(root_radius,complete);
        }
        radius = static_cast<float>(radius+root_radius);
    }
    float attachment_radius = 0;
    // Published FIG endpoint radii are metadata, not a vertex scan. Each part
    // attaches to the one skeleton; different geometry does not clone it.
    for (const auto& part : assembly.parts) {
        if (!part.geometry || (part.geometry->shape_count != 1 && part.geometry->shape_count != 8))
            return false;
        double value = 0;
        for (std::size_t endpoint = 0; endpoint < part.geometry->shape_count; ++endpoint) {
            double weight = 1;
            if (part.geometry->shape_count == 8) for (std::size_t axis = 0; axis < 3; ++axis)
                weight *= endpoint & (1u << axis) ? shape[axis] : 1.0-shape[axis];
            value += std::fabs(weight)*part.geometry->shape_position_radii[endpoint];
        }
        if (!std::isfinite(value)) return false;
        attachment_radius = (std::max)(attachment_radius, static_cast<float>(value));
    }
    double scale = 0;
    for (float axis : assembly.scale) {
        if (!std::isfinite(axis)) return false;
        scale = (std::max)(scale, std::fabs(static_cast<double>(axis)));
    }
    const double complete = (radius + static_cast<double>(rotation_scale)*attachment_radius)*scale;
    entity.envelope_radius = std::nextafter(static_cast<float>(complete), INFINITY);
    return std::isfinite(entity.envelope_radius);
    } catch (...) { return false; }
}

bool RetainedStaticScene::visual_publish_assembly(VisualResourceIncarnation resource,
    std::shared_ptr<const VisualAssemblyAsset> assembly, bool weapons_replaced) noexcept {
    auto* r = recorder();
    if (r) r->asset(assembly); // parts and textures: outside the scene lock
    ExclusiveLock guard(lock_);
    if (r) r->delta(RetainedOp::visual_publish_assembly, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); d.ref(assembly); io(d.out, weapons_replaced);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity || !assembly || !assembly->revision || !assembly->skeleton ||
        assembly->parts.empty() || assembly->parts.size() > kVisualPartLimit ||
        !std::isfinite(assembly->envelope_radius) || assembly->envelope_radius < 0) return false;
    for (const auto& part : assembly->parts)
        // A part without triangles draws nothing in the game; it must arrive
        // explicitly hidden so no consumer treats it as drawable geometry.
        if (!part.geometry || (part.geometry->indices.empty() && !part.explicitly_hidden) ||
            part.skeleton_node >= assembly->skeleton->nodes.size() ||
            !assembly->skeleton->nodes[part.skeleton_node].transform_available ||
            !part.material || (bool(part.texture) == bool(part.surface)) ||
            !part.source_available ||
            (part.surface && !part.surface->id)) return false;
    entity->assembly = std::move(assembly);
    entity->mesh_declared = true;
    try {
        if (!entity->prepared) entity->prepared = std::make_shared<VisualPreparedResources>();
    } catch (...) {
        entity->status = RetainedVisualStatus::invalid_asset;
        return false;
    }
    entity->source_available = true;
    entity->surface_lost = false;
    entity->envelope_radius = entity->assembly->envelope_radius;
    // Animated units compile the current selection's conservative envelope in
    // the retained consumer. Appearance publication must not walk animation
    // metadata while holding the scene lock. Common objects retain their
    // existing completed-pose envelope contract.
    if (entity->tick.unit)
        visual_values_changed_unlocked(resource.signed_id);
    else if (!visual_refresh_envelope(*entity)) {
        entity->status = RetainedVisualStatus::invalid_asset;
        return false;
    }
    if (entity->status == RetainedVisualStatus::invalid_asset ||
        entity->status == RetainedVisualStatus::unsupported_appearance)
        entity->status = RetainedVisualStatus::awaiting_values;
    if (weapons_replaced) entity->anchor.weapon_hidden = false;
    visual_refresh_status(*entity);
    return true;
}

bool RetainedStaticScene::visual_surface_rebind_candidates(std::uint64_t world_frame,
    std::vector<std::int32_t>& ids) noexcept {
    ids.clear();
    ExclusiveLock guard(lock_);
    if (!surface_lost_any_) return true;
    bool any = false;
    try {
        for (auto& item : visuals_) {
            auto& entity = item.second;
            if (!entity.surface_lost) continue;
            if (!entity.assembly || entity.source_available) { entity.surface_lost = false; continue; }
            any = true;
            const std::uint64_t wait = std::uint64_t{30} << (std::min)(entity.surface_rebind_attempts, 5u);
            const bool published = entity.surface_rebind_serial != surface_publish_serial_;
            if (!published && entity.surface_rebind_attempts &&
                world_frame - entity.surface_rebind_frame < wait) continue;
            entity.surface_rebind_serial = surface_publish_serial_;
            entity.surface_rebind_frame = world_frame;
            if (entity.surface_rebind_attempts < 31) ++entity.surface_rebind_attempts;
            ids.push_back(item.first);
        }
    } catch (...) { ids.clear(); return false; }
    surface_lost_any_ = any;
    return true;
}

bool RetainedStaticScene::visual_preview_rebind_due(VisualResourceIncarnation resource,
    std::uint64_t frame) noexcept {
    ExclusiveLock guard(lock_);
    auto* entity = visual_find_unlocked(resource);
    if (!entity || !entity->surface_lost || !entity->assembly || entity->source_available) return false;
    const std::uint64_t wait = std::uint64_t{30} << (std::min)(entity->surface_rebind_attempts, 5u);
    const bool published = entity->surface_rebind_serial != surface_publish_serial_;
    if (!published && entity->surface_rebind_attempts && frame - entity->surface_rebind_frame < wait) return false;
    entity->surface_rebind_serial = surface_publish_serial_;
    entity->surface_rebind_frame = frame;
    if (entity->surface_rebind_attempts < 31) ++entity->surface_rebind_attempts;
    return true;
}

std::shared_ptr<const VisualAssemblyAsset> RetainedStaticScene::visual_assembly(
    VisualResourceIncarnation resource) const noexcept {
    SharedLock guard(lock_);
    const auto* entity = visual_find_unlocked(resource);
    return entity ? entity->assembly : nullptr;
}

std::shared_ptr<const VisualAssemblyAsset> RetainedStaticScene::visual_standing_assembly(
    VisualResourceIncarnation resource) const noexcept {
    SharedLock guard(lock_);
    const auto* entity = visual_find_unlocked(resource);
    if (!entity || !entity->assembly || !entity->source_available || entity->surface_lost ||
        entity->status == RetainedVisualStatus::invalid_asset ||
        entity->status == RetainedVisualStatus::unsupported_appearance) return nullptr;
    return entity->assembly;
}

bool RetainedStaticScene::visual_replace_surface(std::uint64_t id,
    std::shared_ptr<const OwnedVisualSurface> value) noexcept {
    if (!id || (value && value->id != id)) return false;
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_replace_surface, [&](RetainedDeltaOut& d) {
        io(d.out, id); d.ref(value);
    });
    visual_frame_changed_unlocked();
    if (value) ++surface_publish_serial_;
    bool success = true;
    for (auto& source : sources_) {
        if (!source.terrain_textures) continue;
        const auto needs_update = [&](const auto& entry) {
            const auto& binding = entry.second;
            return binding.surface && binding.surface->id == id &&
                (binding.available != bool(value) ||
                 (value && binding.surface->generation != value->generation));
        };
        if (!std::any_of(source.terrain_textures->begin(), source.terrain_textures->end(), needs_update)) continue;
        try {
            auto next = std::make_shared<RetainedTerrainTextures>(*source.terrain_textures);
            for (auto& entry : *next) if (needs_update(entry)) {
                entry.second.available = bool(value);
                if (value) entry.second.surface = value;
            }
            source.terrain_textures = std::move(next);
            // Terrain textures affect receivers/color only, never depth.
        } catch (...) {
            source.terrain_textures.reset();
            // Terrain textures affect receivers/color only, never depth.
            success = false;
        }
    }
    // A detached copy has no engine object to republish its bytes: any
    // change to a surface it draws retires it.
    // Destruction alone keeps it: the copy owns that generation's texture.
    retire_detached_using_unlocked([id, &value](const VisualPartAsset& part) {
        return part.surface && part.surface->id == id && value;
    });
    for (auto& item : visuals_) {
        auto& entity = item.second;
        if (!entity.assembly) continue;
        const auto uses = [id](const VisualPartAsset& part) {
            return part.surface && part.surface->id == id;
        };
        const auto needs_update = [&](const VisualPartAsset& part) {
            const bool available = bool(value);
            return uses(part) && (part.source_available != available ||
                (available && part.surface->generation != value->generation));
        };
        if (!std::any_of(entity.assembly->parts.begin(), entity.assembly->parts.end(), needs_update)) continue;
        try {
            auto next = std::make_shared<VisualAssemblyAsset>(*entity.assembly);
            bool changed = false;
            for (auto& part : next->parts) if (uses(part)) {
                const bool available = bool(value);
                if (part.source_available == available && (!available ||
                    part.surface->generation == value->generation)) continue;
                part.source_available = available;
                if (value) part.surface = value;
                changed = true;
            }
            if (!changed) continue;
            ++next->revision;
            if (!next->revision) ++next->revision;
            entity.source_available = std::all_of(next->parts.begin(), next->parts.end(),
                [](const VisualPartAsset& part) { return part.source_available; });
            if (!value) {
                entity.surface_lost = true;surface_lost_any_ = true;
                entity.surface_rebind_attempts = 0;entity.surface_rebind_frame = 0;
            }
            entity.assembly = std::move(next);
            shadow_source_changed_unlocked(entity.resource.signed_id);
            // Topology, pose and its deformation envelope have not changed.
        } catch (...) {
            entity.source_available = false;
            entity.detection_unavailable = false;
            entity.status = RetainedVisualStatus::publication_fault;
            visual_record_failure_unlocked(entity, "assembly_replace_exception", "local_keep.surface");
            success = false;
        }
    }
    return success;
}

bool RetainedStaticScene::visual_replace_generated_texture(const std::string& source_name,
    std::shared_ptr<const TextureCpuAsset> value, TextureValueSource source) noexcept {
    if (source_name.empty() || (value &&
        (value->value_source != source ||
         value->source_name != source_name || !value->source || !value->value_serial))) return false;
    auto* r = recorder();
    if (r) r->asset(value); // texture bytes: outside the scene lock
    ExclusiveLock guard(lock_);
    if (r) r->delta(RetainedOp::visual_replace_generated_texture, [&](RetainedDeltaOut& d) {
        io(d.out, source_name); d.ref(value); io(d.out, source);
    });
    bool success = true;
    bool values_changed = false;
    retire_detached_using_unlocked([&source_name, source](const VisualPartAsset& part) {
        return part.texture && part.texture->value_source == source &&
            part.texture->source_name == source_name;
    });
    for (auto& item : visuals_) {
        auto& entity = item.second;
        if (!entity.assembly) continue;
        const auto uses = [&source_name, source](const VisualPartAsset& part) {
            return part.texture && part.texture->value_source == source &&
                part.texture->source_name == source_name;
        };
        const auto needs_update = [&](const VisualPartAsset& part) {
            return uses(part) && (part.source_available != bool(value) ||
                (value && part.texture != value));
        };
        if (!std::any_of(entity.assembly->parts.begin(), entity.assembly->parts.end(), needs_update)) continue;
        try {
            bool changed = false;
            auto next = std::make_shared<VisualAssemblyAsset>(*entity.assembly);
            for (auto& part : next->parts) if (uses(part)) {
                const bool available = bool(value);
                if (part.source_available == available && (!available || part.texture == value)) continue;
                part.source_available = available;
                if (value) part.texture = value;
                changed = true;
            }
            if (!changed) continue;
            ++next->revision;
            if (!next->revision) ++next->revision;
            entity.source_available = std::all_of(next->parts.begin(), next->parts.end(),
                [](const VisualPartAsset& part) { return part.source_available; });
            if (!values_changed) {
                visual_frame_changed_unlocked();
                values_changed = true;
            }
            entity.assembly = std::move(next);
            shadow_source_changed_unlocked(entity.resource.signed_id);
            // Texture/alpha changed; geometry and conservative pose envelope did not.
        } catch (...) {
            entity.source_available = false;
            entity.detection_unavailable = false;
            entity.status = RetainedVisualStatus::publication_fault;
            visual_record_failure_unlocked(entity, "assembly_replace_exception", "local_keep.generated_texture");
            success = false;
        }
    }
    return success;
}

bool RetainedStaticScene::visual_publish_route(VisualResourceIncarnation resource,
    std::shared_ptr<const CompiledVisualRoute> route) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_route, [&](RetainedDeltaOut& d) {
        put_resource(d, resource);
        const bool present = bool(route);
        io(d.out, present);
        if (present) delta_io(d, *route);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity || !route || !route->generation) return false;
    entity->tick.route_start_tick = route->values.start_tick;
    entity->route = std::move(route);
    if (entity->status == RetainedVisualStatus::invalid_route)
        entity->status = RetainedVisualStatus::awaiting_values;
    visual_refresh_status(*entity);
    return true;
}

bool RetainedStaticScene::visual_publish_intent(VisualResourceIncarnation resource,
    VisualPoseIntent intent, const std::array<float, 3>& root, const char** failure) noexcept {
    VisualPoseState anchor{};
    if (!anchor_visual_pose(intent, root, anchor, failure)) return false;
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_intent, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); delta_io(d, intent); io(d.out, root);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity) return visual_failure(failure, "intent.resource");
    if (intent.resource_source_generation && entity->resolver_generation != intent.resource_source_generation)
        return visual_failure(failure, "intent.source_generation");
    for (const auto& selected : intent.selections)
        if (!selected.neutral && (!selected.database || !entity->database ||
            !same_resource(selected.database->identity, entity->database->identity)))
            return visual_failure(failure, "intent.database_identity");
    // Re-observing a producer's same event is harmless. Camera SetTime/Step
    // never calls this entry point and therefore cannot replace our anchor.
    if (entity->intent.revision == intent.revision) return true;
    // Preserve publication cache invalidation independently of the removed
    // animation-envelope scan.
    visual_values_changed_unlocked(resource.signed_id);
    // Packet phase already includes the initial counter+1 step. Facing was
    // not advanced by that producer: retain the prior owned facing anchor so
    // the next world tick applies it exactly once without double-stepping phase.
    // A completed placement may precede the first intent (load/creation), or
    // re-establish the anchor after a rewind. It is authoritative even at dt=0.
    if (entity->anchor.root_rotation_valid ||
        (entity->intent.revision && entity->status != RetainedVisualStatus::clock_discontinuity)) {
        anchor.facing = entity->anchor.facing;
        anchor.facing_time = entity->anchor.facing_time;
        anchor.weapon_hidden = entity->anchor.weapon_hidden;
    }
    anchor.root_rotation = entity->anchor.root_rotation;
    anchor.root_rotation_valid = entity->anchor.root_rotation_valid;
    anchor.tilted_facing = entity->tick.rate.tilted_facing;
    entity->intent = std::move(intent);
    entity->anchor = anchor;
    entity->height_valid = true; // This explicit producer supplied a complete root.
    ++entity->pose_revision;
    if (entity->status == RetainedVisualStatus::invalid_intent ||
        entity->status == RetainedVisualStatus::clock_discontinuity ||
        (entity->status == RetainedVisualStatus::publication_fault &&
         !entity->detection_unavailable) ||
        entity->status == RetainedVisualStatus::unsupported_callback ||
        entity->status == RetainedVisualStatus::unsupported_selector)
        entity->status = RetainedVisualStatus::awaiting_values;
    visual_refresh_status(*entity);
    return true;
}

bool RetainedStaticScene::visual_publish_root_rotation(VisualResourceIncarnation resource,
    const std::array<float, 4>& rotation, double time, const char** failure) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_root_rotation, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); io(d.out, rotation); io(d.out, time);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity) return visual_failure(failure, "placement.resource");
    auto next = entity->anchor;
    next.tilted_facing = entity->tick.rate.tilted_facing;
    if (!anchor_visual_root_rotation(rotation, time, next, failure)) return false;
    const bool changed=!entity->anchor.root_rotation_valid || entity->anchor.root_rotation != rotation ||
        entity->anchor.tilted_facing != next.tilted_facing || entity->anchor.facing != next.facing;
    if (changed) {
        ++entity->pose_revision;
        visual_values_changed_unlocked(resource.signed_id);
    } else visual_frame_changed_unlocked();
    entity->anchor = next;
    visual_refresh_status(*entity,false);
    return true;
}

bool RetainedStaticScene::visual_append_idle(VisualResourceIncarnation resource,
    std::uint64_t event, double time, VisualClipSelection selected, const char** failure) noexcept {
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::visual_append_idle, [&](RetainedDeltaOut& d) {
            put_resource(d, resource); io(d.out, event); io(d.out, time); delta_io(d, selected);
        });
        auto* entity = visual_find_unlocked(resource);
        if (!entity) return visual_failure(failure, "idle.resource");
        if (!event) return visual_failure(failure, "idle.event");
        if (!std::isfinite(time)) return visual_failure(failure, "idle.time");
        if (!entity->intent.revision) return visual_failure(failure, "idle.intent_revision");
        if (selected.neutral || !selected.database || !selected.clip || selected.ordinal >= selected.database->entries.size())
            return visual_failure(failure, "idle.selection");
        if (!entity->database || !same_resource(selected.database->identity, entity->database->identity))
            return visual_failure(failure, "idle.database_identity");
        if (event <= entity->last_idle_event) return true;
        visual_values_changed_unlocked(resource.signed_id);
        // Discard consumed owned selections before appending one producer
        // event. Never replace this queue with the camera controller's queue.
        const auto consumed = entity->anchor.selection;
        if (consumed) {
            entity->intent.selections.erase(entity->intent.selections.begin(),
                entity->intent.selections.begin() + consumed);
            entity->anchor.selection = 0;
        }
        if (entity->intent.selections.size() == kVisualQueueLimit) return visual_failure(failure, "idle.queue_limit");
        if (entity->intent.selections.size() == 1 && entity->intent.selections[0].neutral)
            entity->anchor.time = (std::max)(entity->anchor.time,time);
        entity->intent.selections.push_back(std::move(selected));
        entity->intent.resource_source_generation = entity->resolver_generation;
        entity->last_idle_event = event;
        return true;
    } catch (...) { return visual_failure(failure, "idle.exception"); }
}

bool RetainedStaticScene::visual_publish_tick_values(VisualResourceIncarnation resource,
    const VisualTickValues& values, const char** failure) noexcept {
    if (!std::isfinite(values.rate.coefficient) || values.rate.coefficient < 0)
        return visual_failure(failure, "tick.coefficient");
    if (!std::isfinite(values.rate.tall)) return visual_failure(failure, "tick.tall");
    if (!std::isfinite(values.route_start_tick)) return visual_failure(failure, "tick.route_start");
    if (!std::isfinite(values.base_z)) return visual_failure(failure, "tick.base_z");
    if (!std::isfinite(values.fade_alpha) || values.fade_alpha < 0 || values.fade_alpha > 1)
        return visual_failure(failure, "tick.fade");
    for (float extent : values.target_half_extent)
        if (!std::isfinite(extent) || extent < 0) return visual_failure(failure, "tick.target_extent");
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_tick_values, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); delta_io(d, values);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity) return visual_failure(failure, "tick.resource");
    const auto& old=entity->tick;
    const auto& a=old.rate;const auto& b=values.rate;
    const auto& x=old.equipment;const auto& y=values.equipment;
    const bool detection_recovery=entity->detection_unavailable &&
        entity->status==RetainedVisualStatus::publication_fault;
    const bool changed=entity->detection_unavailable || old.object_flags!=values.object_flags || old.unit!=values.unit ||
        old.timed_highlight_active!=values.timed_highlight_active ||
        old.target_half_extent!=values.target_half_extent ||
        a.coefficient!=b.coefficient || a.tall!=b.tall || a.frozen!=b.frozen ||
        a.dont_move!=b.dont_move || a.movement_frozen!=b.movement_frozen ||
        a.tilted_facing!=b.tilted_facing || a.scaled_animation!=b.scaled_animation ||
        a.action_coefficient!=b.action_coefficient ||
        x.has_weapon!=y.has_weapon || x.weapon_is_bow!=y.weapon_is_bow ||
        x.arrow_is_bow!=y.arrow_is_bow || x.human!=y.human ||
        x.speaking!=y.speaking || x.forced_hidden!=y.forced_hidden ||
        old.route_start_tick!=values.route_start_tick || old.base_z!=values.base_z ||
        old.fade_alpha!=values.fade_alpha || old.locomotion!=values.locomotion ||
        old.visibility!=values.visibility || old.loudness!=values.loudness ||
        old.pose!=values.pose || old.moving!=values.moving ||
        old.start_animation!=values.start_animation || old.dead!=values.dead ||
        old.hidden!=values.hidden || old.invisible!=values.invisible ||
        old.stunned!=values.stunned;
    if (changed) visual_values_changed_unlocked(resource.signed_id);
    else visual_frame_changed_unlocked();
    entity->tick = values;
    entity->detection_unavailable = false;
    if (detection_recovery) {
        entity->status = RetainedVisualStatus::awaiting_values;
    }
    entity->anchor.tilted_facing = values.rate.tilted_facing;
    shadow_refresh_time_activity_unlocked(resource.signed_id);
    visual_refresh_status(*entity,false);
    return true;
}

bool RetainedStaticScene::visual_publish_detection(
    VisualResourceIncarnation resource, bool detected) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_detection, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); io(d.out, detected);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity || !entity->tick.unit) return false;
    const auto flags = (entity->tick.object_flags & ~0x00400000u) |
        (detected ? 0x00400000u : 0u);
    const bool unavailable = entity->detection_unavailable;
    const bool recovery = unavailable && entity->status==RetainedVisualStatus::publication_fault;
    if (flags == entity->tick.object_flags && !unavailable) return true;
    entity->tick.object_flags = flags;
    entity->detection_unavailable = false;
    if (recovery) {
        entity->status = RetainedVisualStatus::awaiting_values;
        visual_refresh_status(*entity,false);
    }
    visual_values_changed_unlocked(resource.signed_id);
    return true;
}

void RetainedStaticScene::visual_publish_scene_detection(
    std::int32_t signed_id, bool detected) noexcept {
    VisualResourceIncarnation resource{};
    {
        SharedLock guard(lock_);
        const auto found = visuals_.find(signed_id);
        if (found == visuals_.end()) return;
        const auto& entity = found->second;
        if (!entity.tick.unit || entity.detection_unavailable ||
            ((entity.tick.object_flags & 0x00400000u) != 0) == detected) return;
        resource = entity.resource;
    }
    (void)visual_publish_detection(resource, detected);
}

void RetainedStaticScene::visual_reject_detection_unlocked(
    VisualEntity& entity, const char* predicate) noexcept {
    if (!entity.tick.unit || entity.detection_unavailable ||
        (entity.status!=RetainedVisualStatus::constructing &&
         entity.status!=RetainedVisualStatus::awaiting_values &&
         entity.status!=RetainedVisualStatus::ready)) return;
    entity.detection_unavailable = true;
    entity.status = RetainedVisualStatus::publication_fault;
    visual_record_failure_unlocked(entity,predicate,"local_keep.visible_list",true);
}

void RetainedStaticScene::visual_reject_detection(
    VisualResourceIncarnation resource, const char* predicate) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_reject_detection, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); delta_text(d, predicate);
    });
    auto* entity = visual_find_unlocked(resource);
    if (entity) visual_reject_detection_unlocked(*entity,predicate);
}

void RetainedStaticScene::visual_reject_detected_unit_detection(const char* predicate) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_reject_detected_unit_detection, [&](RetainedDeltaOut& d) {
        delta_text(d, predicate);
    });
    for (auto& item : visuals_)
        if (item.second.tick.object_flags & 0x00400000u)
            visual_reject_detection_unlocked(item.second,predicate);
}

bool RetainedStaticScene::visual_tick_values(VisualResourceIncarnation resource,
    VisualTickValues& values) const noexcept {
    SharedLock guard(lock_);
    const auto* entity = visual_find_unlocked(resource);
    if (!entity) return false;
    values = entity->tick;
    return true;
}

void RetainedStaticScene::visual_publish_preview(VisualResourceIncarnation resource, std::uint32_t view,
    const VisualClipSelection& selection) noexcept {
    try {
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_preview, [&](RetainedDeltaOut& d) {
            put_resource(d, resource); io(d.out, view); delta_io(d, selection);
        });
        const std::uint64_t key = (std::uint64_t(std::uint32_t(resource.signed_id)) << 8) | (view & 0xffu);
        preview_selections_[key] = {resource, selection};
    } catch (...) {}
}

void RetainedStaticScene::material_publish_composition(const std::string& name,
    MaterialComposition value) noexcept {
    try {
        if (name.empty()) return;
        ExclusiveLock guard(lock_);
        if (auto* r = recorder()) r->delta(RetainedOp::material_publish_composition, [&](RetainedDeltaOut& d) {
            io(d.out, name); io(d.out, value.layers); io(d.out, value.slots); io(d.out, value.dimension);
            io(d.out, value.user_data);
        });
        auto& stored = material_compositions_[name];
        if (stored.serial && stored.layers == value.layers && stored.slots == value.slots &&
            stored.dimension == value.dimension && stored.user_data == value.user_data) return;
        value.serial = ++material_composition_serial_;
        stored = std::move(value);
    } catch (...) {}
}

std::uint64_t RetainedStaticScene::material_composition_revision() const noexcept {
    SharedLock guard(lock_);
    return material_composition_serial_;
}

bool RetainedStaticScene::material_composition(const std::string& name,
    MaterialComposition& value) const noexcept {
    SharedLock guard(lock_);
    const auto found = material_compositions_.find(name);
    if (found == material_compositions_.end()) return false;
    try { value = found->second; } catch (...) { return false; }
    return true;
}

bool RetainedStaticScene::visual_preview_selection(VisualResourceIncarnation resource, std::uint32_t view,
    VisualClipSelection& selection) const noexcept {
    SharedLock guard(lock_);
    const std::uint64_t key = (std::uint64_t(std::uint32_t(resource.signed_id)) << 8) | (view & 0xffu);
    const auto found = preview_selections_.find(key);
    if (found == preview_selections_.end() || found->second.first.signed_id != resource.signed_id ||
        found->second.first.serial != resource.serial) return false;
    try { selection = found->second.second; } catch (...) { return false; }
    return true;
}

bool RetainedStaticScene::visual_attachment_source(VisualResourceIncarnation resource,
    double time, RetainedVisualSample& sample) const noexcept {
    SharedLock guard(lock_);
    const auto* entity=visual_find_unlocked(resource);
    const auto member=live_.find(resource.signed_id);
    if(!entity || !entity->constructed || entity->tick.unit || !entity->has_object_pose ||
       !entity->assembly || !entity->prepared || member==live_.end() ||
       entity->status!=RetainedVisualStatus::ready || !std::isfinite(time))return false;
    sample={};sample.resource=resource;sample.lifecycle=member->second;
    sample.assembly=entity->assembly;sample.prepared=entity->prepared;
    sample.object_pose=entity->object_pose;sample.values=entity->tick;
    sample.values.unit=true;sample.completed_pose=true;sample.status=entity->status;
    sample.pose.root=entity->object_pose.position;sample.pose.root_rotation=entity->object_pose.quaternion;
    sample.pose.root_rotation_valid=true;sample.pose.phase=entity->object_pose.phase;sample.pose.time=time;
    sample.selection=entity->object_pose.selection;sample.pose_revision=entity->pose_revision;
    sample.height_valid=true;sample.envelope_radius=entity->envelope_radius;
    return true;
}

bool RetainedStaticScene::visual_preview_source(VisualResourceIncarnation resource,
    RetainedVisualSample& sample, std::uint64_t& selection_revision) const noexcept {
    SharedLock guard(lock_);
    const auto* entity = visual_find_unlocked(resource);
    if (!entity || !entity->constructed || !entity->source_available ||
        !entity->mesh_declared || !entity->assembly) return false;
    sample = {};
    sample.resource = resource;
    sample.assembly = entity->assembly;
    sample.values = entity->tick;
    sample.status = RetainedVisualStatus::ready;
    selection_revision = entity->intent.revision;
    return true;
}

std::string RetainedStaticScene::visual_preview_diagnosis(VisualResourceIncarnation resource) const {
    SharedLock guard(lock_);
    const auto* entity = visual_find_unlocked(resource);
    if (!entity) return "no_entity";
    if (entity->constructed && entity->source_available && entity->mesh_declared && entity->assembly) return {};
    std::string text = "constructed=" + std::to_string(entity->constructed) +
        " source_available=" + std::to_string(entity->source_available) +
        " mesh_declared=" + std::to_string(entity->mesh_declared) +
        " assembly=" + std::to_string(bool(entity->assembly)) +
        " readiness=" + (entity->readiness_reason ? entity->readiness_reason : "-") +
        " failure_scope=" + (entity->failure_scope ? entity->failure_scope : "-");
    if (entity->assembly) {
        text += " parts=" + std::to_string(entity->assembly->parts.size()) + " unavailable:";
        unsigned listed = 0;
        for (const auto& part : entity->assembly->parts) {
            if (part.source_available || listed >= 12) continue;
            ++listed;
            text += " " + part.name + "(tex=" + part.texture_source_name + " material=" + part.material_name +
                " value=" + std::to_string(bool(part.texture)) + " surface=" + std::to_string(bool(part.surface)) + ")";
        }
    }
    return text;
}

bool RetainedStaticScene::visual_publish_object_pose(VisualResourceIncarnation resource,
    const VisualObjectPose& values) noexcept {
    VisualObjectPublicationSnapshot snapshot;
    if (!visual_object_publication_snapshot(resource.signed_id,snapshot) ||
        snapshot.resource.serial!=resource.serial) return false;
    return visual_publish_object_pose(snapshot,values);
}

bool RetainedStaticScene::visual_object_publication_snapshot(std::int32_t signed_id,
    VisualObjectPublicationSnapshot& values) const noexcept {
    SharedLock guard(lock_);
    const auto found=visuals_.find(signed_id);
    if(found==visuals_.end()) return false;
    const auto& entity=found->second;
    values={};
    values.resource=entity.resource;
    values.resolver_generation=entity.resolver_generation;
    values.assembly_revision=entity.assembly ? entity.assembly->revision : 0;
    values.database=entity.database;
    values.prior_pose=entity.object_pose;
    values.has_prior_pose=entity.has_object_pose;
    return true;
}

bool RetainedStaticScene::visual_object_publication_current_unlocked(
    const VisualEntity& entity,const VisualObjectPublicationSnapshot& snapshot) const noexcept {
    if(entity.resource.signed_id!=snapshot.resource.signed_id ||
        entity.resource.serial!=snapshot.resource.serial ||
        entity.resolver_generation!=snapshot.resolver_generation ||
        (entity.assembly ? entity.assembly->revision : 0)!=snapshot.assembly_revision ||
        bool(entity.database)!=bool(snapshot.database) ||
        (entity.database && !same_resource(entity.database->identity,snapshot.database->identity)) ||
        entity.has_object_pose!=snapshot.has_prior_pose) return false;
    if(!snapshot.has_prior_pose)return true;
    const auto& current=entity.object_pose;
    return current.position==snapshot.prior_pose.position &&
        current.quaternion==snapshot.prior_pose.quaternion &&
        current.shape==snapshot.prior_pose.shape &&
        current.object_flags==snapshot.prior_pose.object_flags &&
        current.phase==snapshot.prior_pose.phase &&
        current.source_generation==snapshot.prior_pose.source_generation &&
        current.appearance_revision==snapshot.prior_pose.appearance_revision &&
        same_selection(current.selection,snapshot.prior_pose.selection);
}

bool RetainedStaticScene::visual_publish_object_pose_unlocked(
    const VisualObjectPublicationSnapshot& snapshot,const VisualObjectPose& values,
    bool invalidate,bool& changed_output) noexcept {
    changed_output=false;
    const auto finite = [](float value) { return std::isfinite(value); };
    if (!std::all_of(values.position.begin(),values.position.end(),finite) ||
        !std::all_of(values.quaternion.begin(),values.quaternion.end(),finite) ||
        !std::all_of(values.shape.begin(),values.shape.end(),finite) ||
        !std::isfinite(values.phase) || values.phase < 0) return false;
    auto* entity = visual_find_unlocked(snapshot.resource);
    if (!entity || entity->tick.unit ||
        !visual_object_publication_current_unlocked(*entity,snapshot)) return false;
    if (!values.selection.neutral && (!values.selection.database || !values.selection.clip ||
        values.selection.ordinal >= values.selection.database->entries.size() || !entity->database ||
        values.source_generation != entity->resolver_generation ||
        !same_resource(values.selection.database->identity,entity->database->identity))) return false;
    const auto& old = entity->object_pose;
    const bool envelope_changed = !entity->has_object_pose || old.shape != values.shape ||
        !same_selection(old.selection,values.selection);
    const bool pose_changed = envelope_changed || old.position != values.position ||
        old.quaternion != values.quaternion || old.phase != values.phase;
    const bool changed = pose_changed || old.object_flags != values.object_flags ||
        old.source_generation != values.source_generation || old.appearance_revision != values.appearance_revision;
    const bool recover_pose = entity->status == RetainedVisualStatus::clock_discontinuity ||
        entity->status == RetainedVisualStatus::invalid_intent ||
        entity->status == RetainedVisualStatus::unsupported_selector ||
        entity->status == RetainedVisualStatus::unsupported_callback;
    if (!changed && entity->has_object_pose && !recover_pose) return true;
    // Invalid transforms cannot hide behind an unchanged-value fast path, but
    // they also should not be recomputed before that exact comparison.
    std::array<float,16> matrix{};
    if (!retained_object_matrix(values.position,values.quaternion,matrix)) return false;
    changed_output=true;
    if (pose_changed && ++entity->pose_revision == 0) ++entity->pose_revision;
    entity->object_pose = values;
    entity->has_object_pose = true;
    entity->tick.object_flags = values.object_flags;
    if (envelope_changed && !visual_refresh_envelope(*entity,invalidate)) {
        entity->status = RetainedVisualStatus::invalid_asset;
        visual_record_failure_unlocked(*entity,"object_pose.envelope","local_keep.object_pose",invalidate);
        return false;
    }
    if (recover_pose)
        entity->status = RetainedVisualStatus::awaiting_values;
    visual_refresh_status(*entity,invalidate);
    return true;
}

bool RetainedStaticScene::visual_publish_object_pose(
    const VisualObjectPublicationSnapshot& snapshot,const VisualObjectPose& values) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_object_pose, [&](RetainedDeltaOut& d) {
        delta_io(d, snapshot); delta_io(d, values);
    });
    bool changed=false;
    return visual_publish_object_pose_unlocked(snapshot,values,true,changed);
}

bool RetainedStaticScene::visual_reject_object_pose_if_current(
    const VisualObjectPublicationSnapshot& snapshot,RetainedVisualStatus reason,
    const char* predicate,const char* scope) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_reject_object_pose_if_current, [&](RetainedDeltaOut& d) {
        delta_io(d, snapshot); io(d.out, reason); delta_text(d, predicate); delta_text(d, scope);
    });
    auto* entity=visual_find_unlocked(snapshot.resource);
    if(!entity || !visual_object_publication_current_unlocked(*entity,snapshot))return false;
    visual_reject_unlocked(*entity,reason,predicate,scope,true);
    return true;
}

bool RetainedStaticScene::visual_object_pose(VisualResourceIncarnation resource,
    VisualObjectPose& values) const noexcept {
    SharedLock guard(lock_);
    const auto* entity = visual_find_unlocked(resource);
    if (!entity || !entity->has_object_pose) return false;
    values = entity->object_pose;
    return true;
}

bool RetainedStaticScene::visual_object_pose_current(VisualResourceIncarnation resource,
    const VisualObjectPose& values) const noexcept {
    SharedLock guard(lock_);
    const auto* entity = visual_find_unlocked(resource);
    if (!entity || !entity->has_object_pose || entity->tick.unit || !entity->assembly ||
        entity->assembly->revision != values.appearance_revision ||
        entity->status == RetainedVisualStatus::clock_discontinuity ||
        entity->status == RetainedVisualStatus::invalid_intent ||
        entity->status == RetainedVisualStatus::unsupported_selector ||
        entity->status == RetainedVisualStatus::unsupported_callback) return false;
    if (!values.selection.neutral && (!entity->database || !values.selection.database ||
        values.source_generation != entity->resolver_generation ||
        !same_resource(values.selection.database->identity, entity->database->identity))) return false;
    const auto& current = entity->object_pose;
    return current.position == values.position && current.quaternion == values.quaternion &&
        current.shape == values.shape && current.object_flags == values.object_flags &&
        current.phase == values.phase && current.source_generation == values.source_generation &&
        current.appearance_revision == values.appearance_revision &&
        same_selection(current.selection, values.selection);
}

bool RetainedStaticScene::visual_publish_shape(VisualResourceIncarnation resource,
    const std::array<float,3>& shape) noexcept {
    for (float axis : shape) if (!std::isfinite(axis)) return false;
    VisualObjectPose values;
    if (!visual_object_pose(resource,values)) return true;
    values.shape = shape;
    return visual_publish_object_pose(resource,values);
}

bool RetainedStaticScene::visual_publish_bridges(
    const std::vector<VisualBridgePlane>& planes, const char** failure) noexcept {
    // Each existing entity owns all of its planes. Cells reference an entity
    // and plane ordinal; one object ID may legitimately supply several volumes.
    constexpr double cell_size=16;
    try {
        if (planes.size()>kVisualPartLimit) return visual_failure(failure, "bridges.count");
        ExclusiveLock guard(lock_);
        if(!bridge_index_dirty_ && accepted_bridge_planes_.size()==planes.size() &&
            (planes.empty() || std::memcmp(accepted_bridge_planes_.data(),planes.data(),
                planes.size()*sizeof(VisualBridgePlane))==0)) return true;
        // After the unchanged fast path, which mutates nothing (per tick).
        if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_bridges, [&](RetainedDeltaOut& d) { io(d.out, planes); });
        auto accepted=planes;
        std::map<std::int32_t,std::vector<VisualBridgePlane>> replacement;
        for (const auto& plane:planes) {
            if (!valid_visual_bridge(plane)) return visual_failure(failure, "bridges.plane");
            if (visuals_.find(plane.signed_id)==visuals_.end()) return visual_failure(failure, "bridges.resource");
            replacement[plane.signed_id].push_back(plane);
        }
        bool unchanged=!bridge_index_dirty_;
        for (const auto& entity:visuals_) {
            const auto next=replacement.find(entity.first);
            const auto count=next==replacement.end() ? 0u : next->second.size();
            const auto& old=entity.second.bridge_planes;
            if (old.size()!=count || (count && std::memcmp(old.data(),next->second.data(),
                    count*sizeof(VisualBridgePlane))!=0)) { unchanged=false;break; }
        }
        if (unchanged) {
            accepted_bridge_planes_.swap(accepted);
            return true;
        }
        decltype(bridge_cells_) cells;
        std::size_t references=0;
        for (const auto& entity:replacement) for (std::size_t index=0;index<entity.second.size();++index) {
            const auto& plane=entity.second[index];
            const double x0=std::floor((plane.center[0]-static_cast<double>(plane.radius))/cell_size);
            const double x1=std::floor((plane.center[0]+static_cast<double>(plane.radius))/cell_size);
            const double y0=std::floor((plane.center[1]-static_cast<double>(plane.radius))/cell_size);
            const double y1=std::floor((plane.center[1]+static_cast<double>(plane.radius))/cell_size);
            if (x0<INT_MIN || y0<INT_MIN || x1>=INT_MAX || y1>=INT_MAX ||
                (x1-x0+1)*(y1-y0+1)>65536-references) return visual_failure(failure, "bridges.cell_budget");
            for (auto y=static_cast<std::int32_t>(y0);y<=y1;++y)
                for (auto x=static_cast<std::int32_t>(x0);x<=x1;++x) {
                    cells[{x,y}].emplace_back(entity.first,static_cast<std::uint32_t>(index));
                    ++references;
                }
        }
        // Nothing mutates the current owner until validation and allocation of
        // its complete replacement succeed. Vector swaps cannot allocate.
        // Bridge planes reach only route-sampled unit heights, and units are
        // time-active shadow casters re-evaluated every frame: the frame value
        // changes, the shadow journal does not (a reset rebuilt the whole
        // static caster store and every static's admission, #162).
        visual_frame_changed_unlocked();
        for (auto& entity:visuals_) {
            const auto next=replacement.find(entity.first);
            if (next==replacement.end()) entity.second.bridge_planes.clear();
            else entity.second.bridge_planes.swap(next->second);
        }
        bridge_cells_.swap(cells);
        accepted_bridge_planes_.swap(accepted);
        bridge_index_dirty_=false;
#ifdef EID3D11_TESTING
        ++bridge_index_rebuilds_;
#endif
        return true;
    } catch (...) { return false; }
}

bool RetainedStaticScene::visual_publish_quest_lights(
    std::vector<RetainedQuestLight> lights, const char** failure) noexcept {
    try {
        ExclusiveLock guard(lock_);
        for (const auto& light : lights) {
            const auto member=live_.find(light.source.id);
            if (member==live_.end() ||
                member->second.generation!=light.source.generation ||
                member->second.positive_world_epoch!=light.source.positive_world_epoch)
                return visual_failure(failure,"quest_light.lifecycle");
        }
        if (quest_lights_.size()==lights.size() &&
            std::equal(quest_lights_.begin(),quest_lights_.end(),lights.begin(),
                [](const RetainedQuestLight& a,const RetainedQuestLight& b){
                    return a.source.id==b.source.id &&
                        a.source.generation==b.source.generation &&
                        a.source.positive_world_epoch==b.source.positive_world_epoch;
                })) return true;
        // Recorded where it changes the scene (an unchanged list is per frame).
        if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_quest_lights, [&](RetainedDeltaOut& d) { delta_io(d, lights); });
        quest_lights_.swap(lights);
        visual_frame_changed_unlocked();
        return true;
    } catch (...) { return visual_failure(failure,"quest_light.exception"); }
}

void RetainedStaticScene::visual_publish_highlight(const RetainedHighlightState& state) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_highlight, [&](RetainedDeltaOut& d) { delta_io(d, state); });
    const auto& old=highlight_;
    const bool same_hover=old.hover.signed_id==state.hover.signed_id &&
        old.hover.serial==state.hover.serial;
    if (same_hover && old.show_selected==state.show_selected &&
        old.strip_preselected==state.strip_preselected &&
        old.hover_characters==state.hover_characters &&
        old.hover_objects==state.hover_objects) return;
    // Unchanged static objects are skipped through the change journal; the
    // objects losing or gaining the hover must be reconsidered for colour.
    if (!same_hover) {
        if (old.hover.signed_id) shadow_source_changed_unlocked(old.hover.signed_id);
        if (state.hover.signed_id) shadow_source_changed_unlocked(state.hover.signed_id);
    } else if ((old.hover_characters!=state.hover_characters ||
                old.hover_objects!=state.hover_objects) && state.hover.signed_id) {
        shadow_source_changed_unlocked(state.hover.signed_id);
    }
    highlight_=state;
    visual_frame_changed_unlocked();
}

void RetainedStaticScene::visual_publish_fire_spells(std::vector<RetainedFireSpell> spells) noexcept {
    try {
        ExclusiveLock guard(lock_);
        if (spells == fire_spells_) return;
        if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_fire_spells, [&](RetainedDeltaOut& d) { delta_io(d, spells); });
        fire_spells_.swap(spells);
        visual_frame_changed_unlocked();
    } catch (...) {}
}

bool RetainedStaticScene::visual_publish_world_point_lights(
    std::vector<RetainedWorldPointLight> lights, const char** failure) noexcept {
    try {
        ExclusiveLock guard(lock_);
        for (const auto& light : lights) {
            if (!std::isfinite(light.radius) || light.radius<=0)
                return visual_failure(failure,"world_point_light.radius");
            for (float value : light.center)
                if (!std::isfinite(value)) return visual_failure(failure,"world_point_light.center");
            for (float value : light.color)
                if (!std::isfinite(value)) return visual_failure(failure,"world_point_light.color");
        }
        if (world_point_lights_.size()==lights.size() &&
            std::equal(world_point_lights_.begin(),world_point_lights_.end(),lights.begin(),
                [](const RetainedWorldPointLight& a,const RetainedWorldPointLight& b){
                    return a.source_id==b.source_id && a.center==b.center &&
                        a.color==b.color && a.radius==b.radius &&
                        a.casts_shadow==b.casts_shadow &&
                        a.attached==b.attached && a.hero==b.hero && a.spell==b.spell;
                })) return true;
        // Recorded where it changes the scene (an unchanged list is per frame).
        if (auto* r = recorder()) r->delta(RetainedOp::visual_publish_world_point_lights, [&](RetainedDeltaOut& d) { delta_io(d, lights); });
        world_point_lights_.swap(lights);
        visual_frame_changed_unlocked();
        return true;
    } catch (...) { return visual_failure(failure,"world_point_light.exception"); }
}

#ifdef EID3D11_TESTING
std::uint64_t RetainedStaticScene::visual_bridge_index_rebuilds_for_test() const noexcept {
    SharedLock guard(lock_);
    return bridge_index_rebuilds_;
}

#endif

bool RetainedStaticScene::visual_publish_forced_weapon_ids(
    const std::vector<std::int32_t>& ids) noexcept {
    try {
        auto desired=ids;
        std::sort(desired.begin(),desired.end());
        desired.erase(std::unique(desired.begin(),desired.end()),desired.end());
        ExclusiveLock guard(lock_);
        if (auto* r = recorder(); r && !(desired.empty() && !forced_weapon_hidden_any_))
            r->delta(RetainedOp::visual_publish_forced_weapon_ids, [&](RetainedDeltaOut& d) { io(d.out, desired); });
        // The usual tick: no forced ids and none left hidden. Walking every
        // entity (one cold node each) for nothing cost ~2% of the main
        // thread in a pan sample.
        if(desired.empty() && !forced_weapon_hidden_any_)return true;
        auto next=desired.begin();
        bool changed=false,any=false;
        for(auto& item:visuals_){
            while(next!=desired.end() && *next<item.first)++next;
            const bool hidden=next!=desired.end() && *next==item.first;
            any|=hidden;
            if(item.second.tick.equipment.forced_hidden!=hidden){
                item.second.tick.equipment.forced_hidden=hidden;
                changed=true;
                shadow_source_changed_unlocked(item.first);
            }
        }
        forced_weapon_hidden_any_=any;
        if(changed)visual_frame_changed_unlocked();
        return true;
    } catch (...) { return false; }
}

void RetainedStaticScene::visual_record_failure_unlocked(VisualEntity& entity,
    const char* predicate, const char* scope, bool invalidate) noexcept {
    if(invalidate)visual_values_changed_unlocked(entity.resource.signed_id);
    entity.readiness_reason = predicate;
    entity.failure_scope = predicate ? scope : nullptr;
    if (!predicate || visual_first_failure_.stage) return;
    auto& first = visual_first_failure_;
    first.stage = "values"; first.reason = predicate; first.reason_scope = scope;
    first.signed_id = entity.resource.signed_id; first.resource_serial = entity.resource.serial;
    const auto member = live_.find(first.signed_id);
    if (member != live_.end()) first.membership_generation = member->second.generation;
    first.status = static_cast<std::uint32_t>(entity.status);
    first.object_flags = entity.tick.object_flags; first.unit = entity.tick.unit;
    first.hidden = entity.tick.hidden;
}

void RetainedStaticScene::visual_reject_unlocked(VisualEntity& entity,
    RetainedVisualStatus reason,const char* predicate,const char* scope,
    bool invalidate) noexcept {
    entity.detection_unavailable = false;
    if (reason == RetainedVisualStatus::publication_fault ||
        reason == RetainedVisualStatus::unsupported_callback ||
        reason == RetainedVisualStatus::unsupported_selector) {
        entity.assembly.reset();
        entity.route.reset();
        entity.intent = {};
        entity.anchor = {};
        entity.effects = {};
        entity.effects_anchored = false;
        entity.has_object_pose = false;
    }
    entity.status = reason;
    visual_record_failure_unlocked(entity,predicate,scope,invalidate);
}

void RetainedStaticScene::visual_reject(VisualResourceIncarnation resource,
    RetainedVisualStatus reason, const char* predicate, const char* scope) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_reject, [&](RetainedDeltaOut& d) {
        put_resource(d, resource); io(d.out, reason); delta_text(d, predicate); delta_text(d, scope);
    });
    auto* entity = visual_find_unlocked(resource);
    if (!entity) return;
    // A partial/unknown producer cannot leave an apparently coherent old
    // tuple waiting to become ready after one unrelated field changes.
    // A later complete intent, route and appearance must establish it anew.
    visual_reject_unlocked(*entity,reason,predicate,scope,true);
}

void RetainedStaticScene::visual_reject_all(RetainedVisualStatus reason,
    const char* predicate, const char* scope) noexcept {
    ExclusiveLock guard(lock_);
    if (auto* r = recorder()) r->delta(RetainedOp::visual_reject_all, [&](RetainedDeltaOut& d) {
        io(d.out, reason); delta_text(d, predicate); delta_text(d, scope);
    });
    retire_all_detached_unlocked();
    for (auto& item : visuals_) {
        auto& entity = item.second;
        entity.detection_unavailable = false;
        if (reason == RetainedVisualStatus::publication_fault) {
            entity.assembly.reset();
            entity.route.reset();
            entity.intent = {};
            entity.anchor = {};
            entity.effects = {};
            entity.effects_anchored = false;
            entity.has_object_pose = false;
        }
        entity.status = reason;
        visual_record_failure_unlocked(entity, predicate, scope);
    }
}

const char* retained_visual_status_name(std::uint32_t status) noexcept {
    static constexpr const char* names[] = {"constructing", "awaiting_values", "ready",
        "invalid_asset", "invalid_intent", "unsupported_callback", "unsupported_selector",
        "unsupported_appearance", "unavailable_surface", "invalid_route", "invalid_height",
        "clock_discontinuity", "publication_fault", "unavailable_terrain"};
    return status < sizeof(names)/sizeof(names[0]) ? names[status] : "unavailable";
}

void RetainedStaticScene::visual_frame_unlocked(double time, bool diagnose,RetainedVisualFrame& result) const {
    (void)diagnose;
    result.entities.clear();result.diagnostic={};result.first_failure={};
    result.quest_lights=quest_lights_;
    result.world_point_lights=world_point_lights_;
    result.fire_spells=fire_spells_;
    result.time = time;
    // Diagnostics are part of the immutable evaluated value. The public flag
    // controls recorder exposure, not whether a same-time reader gets a second
    // scene evaluation with different contents.
    result.diagnostic.sampled = true;
    result.first_failure = visual_first_failure_;
    if (!std::isfinite(time)) return;
    std::shared_ptr<const TerrainCpuAsset> terrain;
    std::shared_ptr<const OwnedTerrainGeometryValues> terrain_geometry;
    for (const auto& source : sources_)
        if (source.role == MobSourceRole::main && source.snapshot) {
            terrain = source.snapshot->terrain;
            terrain_geometry = source.terrain_geometry;
        }
    auto& joins=visual_frame_joins_;
    if(visual_frame_join_revision_!=visual_structure_revision_) {
        std::vector<VisualFrameJoin> next;
        next.reserve(live_.size()+detached_.size());
        auto visual=visuals_.begin();
        auto detached=detached_.begin();
        // Entities stay in signed-ID order with one sample per ID; a detached
        // copy shares its ID's join with any not-yet-ready next incarnation.
        const auto flush_detached=[&](const std::int32_t* below) {
            for(;detached!=detached_.end() && (!below || detached->first<*below);++detached)
                next.push_back({detached->second.lifecycle,nullptr,&detached->second});
        };
        for(const auto& member:live_) {
            while(visual!=visuals_.end() && visual->first<member.first)++visual;
            flush_detached(&member.first);
            const DetachedVisual* shared=nullptr;
            if(detached!=detached_.end() && detached->first==member.first)shared=&(detached++)->second;
            next.push_back({member.second,
                visual!=visuals_.end() && visual->first==member.first ? &visual->second : nullptr,shared});
        }
        flush_detached(nullptr);
        joins.swap(next);
        visual_frame_join_revision_=visual_structure_revision_;
#ifdef EID3D11_TESTING
        ++visual_join_rebuilds_;
#endif
    }
    if(++visual_frame_serial_==0){
        ++visual_frame_serial_;
        for(const auto& item:visuals_)item.second.frame_serial=0;
        for(const auto& item:detached_)item.second.entity.frame_serial=0;
    }
    auto& roots=visual_frame_roots_;
    auto& records=visual_frame_records_;
    auto& unit_ordinals=visual_frame_unit_ordinals_;
    roots.clear();records.clear();unit_ordinals.clear();
    result.entities.reserve(joins.size());
    roots.reserve(joins.size());
    records.reserve(joins.size());
    unit_ordinals.reserve(joins.size());
    for(const auto& join:joins) {
        const auto* member_identity=&join.lifecycle;
        const auto* found=join.entity;
        if(join.detached) {
            // The detached copy draws until the next incarnation is usable.
            const bool live_usable=found && found->constructed &&
                found->status==RetainedVisualStatus::ready &&
                found->excluded_membership_generation!=join.lifecycle.generation;
            if(!live_usable) {
                member_identity=&join.detached->lifecycle;
                found=&join.detached->entity;
            }
        }
        const auto& member=*member_identity;
        if(found && join.detached && found==&join.detached->entity)++result.diagnostic.detached_members;
        else ++result.diagnostic.live_members;
        const char* rejected = nullptr;
        if (!found) {
            rejected = "missing_resource";
            ++result.diagnostic.missing_resource;
        } else if (!found->constructed) {
            rejected = "unfinished_resource";
            ++result.diagnostic.unfinished_resource;
        } else if (found->excluded_membership_generation == member.generation) {
            rejected = "excluded_generation";
            ++result.diagnostic.excluded_generation;
        }
        if (rejected) {
            if (!result.diagnostic.first_rejection.stage) {
                auto& first = result.diagnostic.first_rejection;
                first.stage = "membership_join"; first.reason = rejected;
                first.signed_id = member.id; first.membership_generation = member.generation;
                if (found) {
                    first.resource_serial = found->resource.serial;
                    first.status = static_cast<std::uint32_t>(found->status);
                }
            }
            continue;
        }
        const auto& entity = *found;
        RetainedVisualSample sample{};
        sample.resource = entity.resource;
        sample.lifecycle = member;
        sample.quest_light_active=std::any_of(quest_lights_.begin(),quest_lights_.end(),
            [&](const RetainedQuestLight& light){
                return light.source.id==member.id && light.source.generation==member.generation &&
                    light.source.positive_world_epoch==member.positive_world_epoch;
            });
        sample.status = entity.status;
        if (entity.status == RetainedVisualStatus::awaiting_values || entity.failure_scope) {
            sample.readiness_reason = entity.readiness_reason;
            sample.failure_scope = entity.failure_scope;
        }
        sample.mesh_declared = entity.mesh_declared;
        sample.assembly = entity.assembly;
        sample.prepared = entity.prepared;
        sample.object_pose = entity.object_pose;
        sample.values = entity.tick;
        sample.values.highlight = static_cast<std::uint8_t>(
            (highlight_.show_selected ? kHighlightSelected : 0) |
            (highlight_.strip_preselected ? kHighlightStripPreselect : 0) |
            (highlight_.hover.signed_id && highlight_.hover.signed_id==entity.resource.signed_id &&
             highlight_.hover.serial==entity.resource.serial &&
             hover_highlight_enabled(entity.tick.unit,entity.tick.dead,
                 highlight_.hover_characters,highlight_.hover_objects) ? kHighlightHover : 0));
        sample.pose_revision = entity.pose_revision;
        sample.height_valid = entity.height_valid;
        sample.envelope_radius = entity.envelope_radius;
        VisualRouteSample route{};
        bool root_valid=entity.has_object_pose;
        if (!entity.tick.unit) {
            if (entity.has_object_pose) {
                route.position = {entity.object_pose.position[0]*2,entity.object_pose.position[1]*2};
                sample.pose.root = entity.object_pose.position;
                sample.pose.root_rotation = entity.object_pose.quaternion;
                sample.pose.root_rotation_valid = true;
                sample.pose.phase = entity.object_pose.phase;
                sample.pose.time = time;
                sample.selection = entity.object_pose.selection;
            }
            sample.height_valid = true;
        } else if (sample.status == RetainedVisualStatus::ready) {
            sample.route_generation = entity.route->generation;
            const double route_time = time - (entity.tick.route_start_tick - entity.route->values.start_tick);
            if (time < visual_tick_time_ || !sample_visual_route(*entity.route, route_time, route))
                sample.status = RetainedVisualStatus::clock_discontinuity;
            if (entity.tick.rate.dont_move || entity.tick.rate.movement_frozen)
                route.position = {entity.anchor.root[0]*2, entity.anchor.root[1]*2};
            root_valid=sample.status==RetainedVisualStatus::ready;
            if(root_valid)
                sample.liquid_speed=liquid_route_world_speed(*entity.route,route_time,
                    entity.tick.rate.dont_move||entity.tick.rate.movement_frozen,&sample.liquid_direction);
        } else if (entity.has_object_pose) {
            route.position = {entity.object_pose.position[0]*2,entity.object_pose.position[1]*2};
            sample.pose.root = entity.object_pose.position;
        }
        entity.frame_serial=visual_frame_serial_;
        entity.frame_ordinal=result.entities.size();
        entity.frame_root_valid=root_valid;
        entity.frame_pose_current=false;
        result.entities.push_back(std::move(sample));
        ++result.diagnostic.samples;
        roots.push_back(route);
        records.push_back(&entity);
        if(entity.tick.unit){
            entity.frame_unit_ordinal=unit_ordinals.size();
            unit_ordinals.push_back(entity.frame_ordinal);
        }
    }
    struct DeathTargetOffset {
        double forward = 0, left = 0;
        bool uses_orientation() const noexcept { return forward != 0 || left != 0; }
    };
    const auto death_target_offset = [](const VisualEntity& target) noexcept {
        DeathTargetOffset offset;
        const auto ordinal = target.tick.start_animation;
        if (!target.tick.dead || !target.database || ordinal >= target.database->entries.size())
            return offset;
        const auto& death = target.database->entries[ordinal];
        if ((death.flags & 0x3c0000u) != 0x1c0000u) return offset;
        switch (death.special_flags & 0x1c00000u) {
        case 0x400000u: offset.forward = target.tick.target_half_extent[1]; break;
        case 0x800000u: offset.forward = -target.tick.target_half_extent[1]; break;
        case 0xc00000u: offset.left = target.tick.target_half_extent[0]; break;
        case 0x1000000u: offset.left = -target.tick.target_half_extent[0]; break;
        }
        return offset;
    };
    // An attacker faces its target's current position, which never depends on
    // the target's own facing or target. Only a nonzero death offset needs the
    // target's orientation; order that dependency before the attacker so it
    // reads this frame's pose. A cycle reads the target's committed anchor.
    const auto count = result.entities.size();
    const auto target_index_for = [&](std::int32_t id) {
        const auto target=visuals_.find(id);
        return target!=visuals_.end()&&target->second.frame_serial==visual_frame_serial_
            ?target->second.frame_ordinal:count;
    };
    const auto unit_count=unit_ordinals.size();
    const auto dependency = [&](std::size_t unit_ordinal) {
        const auto i=unit_ordinals[unit_ordinal];
        if (result.entities[i].status != RetainedVisualStatus::ready ||
            records[i]->route->values.kind != VisualRouteKind::attack)
            return unit_count;
        const auto target=target_index_for(records[i]->route->values.target_id);
        return target!=count && death_target_offset(*records[target]).uses_orientation() &&
            result.entities[target].values.unit &&
            result.entities[target].status==RetainedVisualStatus::ready
            ?records[target]->frame_unit_ordinal:unit_count;
    };
    auto& dependencies=visual_frame_dependencies_;
    bool changed=!visual_frame_graph_valid_||dependencies.size()!=unit_count;
    visual_frame_graph_valid_=false;
    dependencies.resize(unit_count,unit_count);
    for(std::size_t i=0;i<unit_count;++i){
        const auto target=dependency(i);changed|=dependencies[i]!=target;dependencies[i]=target;
    }
    auto& visited=visual_frame_visited_;
    auto& order=visual_frame_order_;
    auto& chain=visual_frame_chain_;
    if(changed){
        visited.assign(unit_count,0);
        order.clear();order.reserve(unit_count);chain.reserve(unit_count);
        for (std::size_t i = 0; i < unit_count; ++i) {
            if (visited[i]) continue;
            chain.clear(); auto next = i;
            while (next != unit_count && !visited[next]) {
                visited[next] = 1; chain.push_back(next); next = dependencies[next];
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) order.push_back(*it);
        }
#ifdef EID3D11_TESTING
        ++visual_dependency_rebuilds_;
#endif
    }
    visual_frame_graph_valid_=true;
    // Reuse ordering only while the current accepted target edges match.
    for (const auto unit_ordinal : order) {
        const auto i=unit_ordinals[unit_ordinal];
        auto& sample = result.entities[i];
        const auto& entity = *records[i];
        if (sample.status != RetainedVisualStatus::ready || !sample.values.unit) continue;
        auto route = roots[i];
        std::array<float,2> target_position{};
        const std::array<float, 2>* target = nullptr;
        // A present target without any current position holds the last owned
        // facing. Target availability never decides whether this unit draws.
        bool hold_facing = false;
        const auto target_index = target_index_for(entity.route->values.target_id);
        if (entity.route->values.kind == VisualRouteKind::attack && target_index != count) {
            const auto index = target_index;
            const auto& target_record = *records[index];
            const auto& target_sample = result.entities[index];
            if (!target_record.frame_root_valid) {
                hold_facing = true;
            } else {
                target_position = roots[index].position;
                if (target_record.tick.dead) {
                    const auto offset = death_target_offset(target_record);
                    if (offset.uses_orientation()) {
                        // Owned facing is forward (-native Y); native X points left.
                        const auto displacement = visual_root_vector(
                            target_record.frame_pose_current ? target_sample.pose : target_record.anchor,
                            {static_cast<float>(offset.left),static_cast<float>(-offset.forward),0});
                        target_position[0] += 2*displacement[0];
                        target_position[1] += 2*displacement[1];
                    }
                } else if ((std::max)(time,entity.tick.route_start_tick) <= entity.route->values.hit_tick &&
                    target_record.route && target_record.route->values.kind == VisualRouteKind::walk &&
                    !(target_record.tick.object_flags & 0x08000000u)) {
                    VisualRouteSample future;
                    const double future_time = entity.route->values.hit_tick -
                        (target_record.tick.route_start_tick-target_record.route->values.start_tick);
                    // A fault in the target's own route keeps its current position.
                    if (sample_visual_route(*target_record.route,future_time,future))
                        target_position = future.position;
                }
                target = &target_position;
            }
        }
        if (!face_visual_target(*entity.route, time, entity.tick.route_start_tick, target, route)) {
            sample.status = RetainedVisualStatus::invalid_route;
            continue;
        }
        if (hold_facing) {
            // Attack routes have zero speed. Phase and effects stay current
            // without displaying an invented facing.
            route.direction = {std::cos(entity.anchor.facing), std::sin(entity.anchor.facing)};
        }
        const char* pose_failure = nullptr;
        if (!sample_visual_pose(entity.intent, entity.anchor, entity.tick.rate, route, time, sample.pose, &pose_failure)) {
            sample.status = RetainedVisualStatus::invalid_intent;
            sample.readiness_reason = pose_failure; sample.failure_scope = "local_keep.pose";
            continue;
        }
        if (hold_facing) sample.pose.facing = entity.anchor.facing;
        entity.frame_pose_current = true;
        if ((!entity.tick.rate.dont_move && !entity.tick.rate.movement_frozen) || !sample.height_valid) {
            // An absent map/layer value is temporary availability, not a broken
            // intent. Keep independent XY/facing/phase/effects current and mark
            // Z unusable until it can be resolved, even if movement then freezes.
            const bool unavailable = !terrain || (entity.tick.locomotion == 2 &&
                !terrain->objects.empty() && !terrain_geometry);
            if (unavailable) sample.height_valid = false;
            else {
                float height = 0;
                if (!sample_terrain_height(*terrain, sample.pose.root[0], sample.pose.root[1],
                    entity.tick.locomotion == 2 ? TerrainHeightLayer::surface : TerrainHeightLayer::ground,
                    height, terrain_geometry ? &terrain_geometry->layer_shifts : nullptr)) {
                    sample.status = RetainedVisualStatus::invalid_height;
                    continue;
                }
                const double x = std::floor(sample.pose.root[0]/16.0);
                const double y = std::floor(sample.pose.root[1]/16.0);
                if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX) {
                    sample.status = RetainedVisualStatus::invalid_height;
                    continue;
                }
                const auto nearby = bridge_cells_.find({static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)});
                if (nearby != bridge_cells_.end()) for (const auto& plane : nearby->second) {
                    const auto bridge = visuals_.find(plane.first);
                    const auto member = live_.find(plane.first);
                    if (bridge == visuals_.end() || member == live_.end() ||
                        plane.second >= bridge->second.bridge_planes.size() ||
                        bridge->second.excluded_membership_generation == member->second.generation) continue;
                    float value = 0;
                    if (sample_visual_bridge(bridge->second.bridge_planes[plane.second], sample.pose.root[0], sample.pose.root[1], value))
                        height = (std::max)(height, value);
                }
                sample.pose.root[2] = height + entity.tick.base_z * (entity.tick.locomotion == 2 ? 2 : 1);
                sample.height_valid = true;
            }
        }
        sample.selection = entity.intent.selections[sample.pose.selection];
        auto unforced_equipment = entity.tick.equipment;
        unforced_equipment.forced_hidden = false;
        sample.visibility = sample_visual_visibility(
            sample.selection.neutral ? nullptr : &sample.selection.database->entries[sample.selection.ordinal],
            sample.pose.phase, entity.tick.dead,
            unforced_equipment, entity.anchor.weapon_hidden);
        sample.pose.weapon_hidden = sample.visibility.hide_weapons;
        sample.visibility.hide_weapons |= entity.tick.equipment.forced_hidden;
        if (unforced_equipment.human && unforced_equipment.arrow_is_bow)
            sample.visibility.hide_arrow |= entity.tick.equipment.forced_hidden;
        auto effects_anchor = entity.effects;
        if (!entity.effects_anchored) effects_anchor.time = time;
        const char* effects_failure = nullptr;
        if (!sample_visual_effects(effects_anchor, entity.tick.invisible, entity.tick.stunned,
                                  entity.tick.fade_alpha, time, sample.effects, &effects_failure)) {
            sample.status = RetainedVisualStatus::invalid_intent;
            sample.readiness_reason = effects_failure; sample.failure_scope = "local_keep.effects";
        }
    }
    // Source availability controls drawing, not authoritative visual time or
    // target placement. Pose/effects remain current while bytes are unavailable.
    for (std::size_t i = 0; i < result.entities.size(); ++i) {
        auto& sample=result.entities[i];
        if (!sample.height_valid && sample.status==RetainedVisualStatus::ready)
            sample.status=RetainedVisualStatus::unavailable_terrain;
        else if (sample.status==RetainedVisualStatus::ready && !records[i]->source_available)
            sample.status=RetainedVisualStatus::unavailable_surface;
    }
}

std::shared_ptr<const RetainedVisualFrame> RetainedStaticScene::visual_frame_value_unlocked(double time,bool diagnose) const {
    const auto key=visual_frame_key_unlocked();
    if(visual_frame_cache_&&visual_frame_key_==key&&visual_frame_cache_->time==time)return visual_frame_cache_;
    // A renderer invocation may still pin the prior value. Refill in place
    // only after that invocation releases it; no pending reader sees edits.
    // With the scene thread (render_thread) one frame's value stays pinned
    // while the next is built: alternate between two values instead of
    // allocating a new one each frame.
    if(!visual_frame_cache_||visual_frame_cache_.use_count()!=1) {
        if(visual_frame_spare_&&visual_frame_spare_.use_count()==1)std::swap(visual_frame_cache_,visual_frame_spare_);
        else { visual_frame_spare_=std::move(visual_frame_cache_);visual_frame_cache_=std::make_shared<RetainedVisualFrame>(); }
    }
    visual_frame_key_={};
    visual_frame_unlocked(time,diagnose,*visual_frame_cache_);
    visual_frame_key_=key;
#ifdef EID3D11_TESTING
    ++visual_frame_evaluations_;
#endif
    return visual_frame_cache_;
}

// World change: the frame value and the evaluation scratch are sized by the
// previous world's entity count and only ever refilled in place. A value a
// renderer invocation still pins stays with that reader.
void RetainedStaticScene::release_visual_frame_scratch() noexcept {
    ExclusiveLock guard(lock_);
    visual_frame_cache_.reset(); visual_frame_spare_.reset(); visual_frame_key_ = {};
    visual_frame_join_revision_ = 0; visual_frame_graph_valid_ = false;
    std::vector<VisualFrameJoin>().swap(visual_frame_joins_);
    std::vector<VisualRouteSample>().swap(visual_frame_roots_);
    std::vector<const VisualEntity*>().swap(visual_frame_records_);
    std::vector<std::size_t>().swap(visual_frame_unit_ordinals_);
    std::vector<std::size_t>().swap(visual_frame_dependencies_);
    std::vector<std::size_t>().swap(visual_frame_order_);
    std::vector<std::size_t>().swap(visual_frame_chain_);
    std::vector<std::uint8_t>().swap(visual_frame_visited_);
}

std::shared_ptr<const RetainedVisualFrame> RetainedStaticScene::visual_frame_value(double time,bool diagnose) const {
    ExclusiveLock guard(lock_);
    return visual_frame_value_unlocked(time,diagnose);
}

RetainedVisualFrame RetainedStaticScene::visual_frame(double time,bool diagnose) const {
    return *visual_frame_value(time,diagnose);
}

void RetainedStaticScene::shadow_refresh_time_activity_unlocked(std::int32_t id) noexcept {
    if (!id) return;
    const auto live=live_.find(id);
    const auto found=visuals_.find(id);
    const auto detached=detached_.find(id);
    const VisualEntity* entity=nullptr;
    if (live!=live_.end() && found!=visuals_.end() &&
        found->second.excluded_membership_generation!=live->second.generation)
        entity=&found->second;
    else if (detached!=detached_.end()) entity=&detached->second.entity;
    // Completed object poses can animate too (including embedded host units).
    // Keep their changing depth out of the persistent static shadow layer.
    const auto* clip=entity && entity->has_object_pose && !entity->object_pose.selection.neutral
        ? entity->object_pose.selection.clip.get() : nullptr;
    const bool animated=clip && std::any_of(clip->tracks.begin(),clip->tracks.end(),
        [](const auto& entry) {
            const auto& track=entry.second;
            return track.rotations.size()>1 || track.translations.size()>1 || track.morph_frames>1;
        });
    const bool active=entity && entity->constructed && entity->mesh_declared &&
        (entity->tick.unit || animated || (entity->assembly && std::any_of(
            entity->assembly->parts.begin(),entity->assembly->parts.end(),
            [](const VisualPartAsset& part) {
                return part.casts_shadow && (part.texture_type==13 ||
                    part.texture_type==14 || part.texture_type==16);
            })));
    try {
        if (active) shadow_time_active_visuals_.insert(id);
        else shadow_time_active_visuals_.erase(id);
    } catch (...) { shadow_tracking_failed_=true; }
}

void RetainedStaticScene::shadow_refresh_terrain_activity_unlocked() noexcept {
    try {
        std::set<std::uint32_t> active,waves;
        // Only immutable base terrain casts. Liquid layers remain receivers.
        shadow_time_active_terrain_sectors_.swap(active);
        shadow_wave_active_terrain_sectors_.swap(waves);
#ifdef EID3D11_TESTING
        ++shadow_terrain_activity_rebuilds_;
#endif
    } catch(...) { shadow_tracking_failed_=true; }
}

void RetainedShadowChangeJournal::reset() noexcept {
    full_revision_=revision_;
    entries_.clear();
    cursors_.fill(0);
    needs_full_.fill(true);
    compacted_through_=0;
}

void RetainedShadowChangeJournal::advance() noexcept {
    if (++revision_==0) {
        revision_=1;
        terrain_revision_=terrain_static_revision_=1;
        reset();
    }
}

void RetainedShadowChangeJournal::visual_changed(std::int32_t id) noexcept {
    advance();
    if (!id) { reset();return; }
    if (entries_.size()>=capacity) {
        // Expire only readers holding the oldest entry. Current readers retain
        // their exact incremental history and do not pay a periodic full refit.
        const auto oldest=entries_.front().revision;
        for (std::size_t i=0;i<cursors_.size();++i)
            if (active_[i] && cursors_[i]<oldest) needs_full_[i]=true;
        compact();
    }
    try { entries_.push_back({revision_,id}); }
    catch (...) { reset(); }
}

void RetainedShadowChangeJournal::terrain_changed(bool time_only) noexcept {
    advance();
    terrain_revision_=revision_;
    if (!time_only) terrain_static_revision_=revision_;
}

RetainedShadowSourceChanges RetainedShadowChangeJournal::changes_since(
    std::uint64_t since) const {
    RetainedShadowSourceChanges changes;
    changes.revision=revision_;
    changes.full=!since || since>revision_ || since<compacted_through_ ||
        full_revision_>since;
    changes.terrain_dirty=changes.full || terrain_revision_>since;
    changes.terrain_time_only=changes.terrain_dirty && !changes.full &&
        terrain_static_revision_<=since;
    if (!changes.full) {
        for (const auto& entry:entries_)
            if (entry.revision>since) changes.dirty_visuals.push_back(entry.id);
        std::sort(changes.dirty_visuals.begin(),changes.dirty_visuals.end());
        changes.dirty_visuals.erase(std::unique(changes.dirty_visuals.begin(),
            changes.dirty_visuals.end()),changes.dirty_visuals.end());
    }
    return changes;
}

RetainedShadowSourceChanges RetainedShadowChangeJournal::read(
    RetainedShadowConsumer consumer) {
    const auto index=static_cast<std::size_t>(consumer);
    active_[index]=true;
    auto result=changes_since(needs_full_[index]?0:cursors_[index]);
    needs_full_[index]=false;
    return result;
}

void RetainedShadowChangeJournal::acknowledge(RetainedShadowConsumer consumer,
    std::uint64_t through) noexcept {
    if (!through || through>revision_) return;
    const auto index=static_cast<std::size_t>(consumer);
    active_[index]=true;
    cursors_[index]=std::max(cursors_[index],through);
    needs_full_[index]=false;
    compact();
}

void RetainedShadowChangeJournal::compact() noexcept {
    auto minimum=revision_;
    for (std::size_t i=0;i<cursors_.size();++i)
        if (active_[i] && !needs_full_[i]) minimum=std::min(minimum,cursors_[i]);
    while (!entries_.empty() && entries_.front().revision<=minimum)
        entries_.pop_front();
    compacted_through_=std::max(compacted_through_,minimum);
}

void RetainedStaticScene::shadow_source_changed_unlocked(std::int32_t id) noexcept {
    shadow_changes_.visual_changed(id);
    if (id) shadow_refresh_time_activity_unlocked(id);
}

void RetainedStaticScene::shadow_terrain_changed_unlocked(bool time_only) noexcept {
    shadow_changes_.terrain_changed(time_only);
}

RetainedShadowSourceChanges RetainedStaticScene::shadow_source_changes(
    std::uint64_t since) const {
    SharedLock guard(lock_);
    auto changes=shadow_changes_.changes_since(since);
    changes.full|=shadow_tracking_failed_;
    changes.terrain_dirty|=changes.full;
    if (changes.full) changes.terrain_time_only=false;
    changes.time_active_visuals.assign(shadow_time_active_visuals_.begin(),
        shadow_time_active_visuals_.end());
    changes.time_active_terrain_sectors.assign(shadow_time_active_terrain_sectors_.begin(),
        shadow_time_active_terrain_sectors_.end());
    changes.wave_active_terrain_sectors.assign(shadow_wave_active_terrain_sectors_.begin(),
        shadow_wave_active_terrain_sectors_.end());
    return changes;
}

RetainedShadowSourceChanges RetainedStaticScene::shadow_source_changes(
    RetainedShadowConsumer consumer) {
    ExclusiveLock guard(lock_);
    auto changes=shadow_changes_.read(consumer);
    changes.full|=shadow_tracking_failed_;
    changes.terrain_dirty|=changes.full;
    if (changes.full) changes.terrain_time_only=false;
    changes.time_active_visuals.assign(shadow_time_active_visuals_.begin(),
        shadow_time_active_visuals_.end());
    changes.time_active_terrain_sectors.assign(shadow_time_active_terrain_sectors_.begin(),
        shadow_time_active_terrain_sectors_.end());
    changes.wave_active_terrain_sectors.assign(shadow_wave_active_terrain_sectors_.begin(),
        shadow_wave_active_terrain_sectors_.end());
    return changes;
}

RetainedShadowSourceChanges RetainedStaticScene::visual_source_changes(
    RetainedShadowConsumer consumer) {
    ExclusiveLock guard(lock_);
    auto changes=shadow_changes_.read(consumer);
    changes.full|=shadow_tracking_failed_;
    changes.terrain_dirty|=changes.full;
    if (changes.full) changes.terrain_time_only=false;
    return changes;
}

void RetainedStaticScene::acknowledge_shadow_source_changes(
    RetainedShadowConsumer consumer,std::uint64_t through) noexcept {
    ExclusiveLock guard(lock_);
    shadow_changes_.acknowledge(consumer,through);
}

std::uint64_t RetainedStaticScene::shadow_source_revision() const noexcept {
    SharedLock guard(lock_);
    return shadow_changes_.revision();
}

std::shared_ptr<const RetainedShadowInputDomain>
RetainedStaticScene::shadow_input_domain() const noexcept {
    SharedLock guard(lock_);
    return shadow_input_domain_;
}

bool RetainedStaticScene::publish_shadow_input_domain(
    std::shared_ptr<const RetainedShadowInputDomain> value) noexcept {
    if(!value || !value->source_revision || !value->coverage.valid)return false;
    ExclusiveLock guard(lock_);
    // The scene thread (render_thread) prepares from a revision the game
    // thread may since have advanced: the publication covers the changes
    // through its own revision, and later ones stay unacknowledged for the
    // next read. Without a scene thread both are always equal.
    const auto through=value->source_revision;
    if(!shadow_changes_.acknowledgeable(through) ||
       (shadow_input_domain_ && shadow_input_domain_->source_revision>through))return false;
    shadow_input_domain_=std::move(value);
    shadow_changes_.acknowledge(RetainedShadowConsumer::directional,through);
    return true;
}

#ifdef EID3D11_TESTING
void RetainedStaticScene::acknowledge_shadow_source_changes(
    std::uint64_t through) noexcept {
    acknowledge_shadow_source_changes(RetainedShadowConsumer::directional,through);
}
#endif
bool RetainedStaticScene::visual_commit_tick(double time, const char** failure,
    std::uint64_t* committed_entities) noexcept {
    try {
        if (committed_entities) *committed_entities = 0;
        ExclusiveLock guard(lock_);
        auto* r = recorder();
        if (r) r->delta(RetainedOp::visual_commit_tick, [&](RetainedDeltaOut& d) { io(d.out, time); });
        if (!std::isfinite(time) || time != std::floor(time)) return visual_failure(failure, "tick.time");
        if (time == visual_tick_time_) {
            if (committed_entities && visual_frame_cache_)
                *committed_entities = visual_frame_cache_->entities.size();
            return true;
        }
        if (visual_tick_time_ >= 0 && time < visual_tick_time_) {
            // The game can set its segment clock backwards: the server's
            // interface packet (CPlayerClient::PrsNewInterface, 669DD8)
            // stores its value into nSegmentTimeCounter (7AF61C) with no
            // world reset and no monotonic guard. Re-baseline in place: every
            // time this scene stored in the old clock (pose and facing
            // anchors, intent anchors, effect anchors) moves by the same
            // delta, so posed and idle objects keep their phase and keep
            // drawing without waiting for a new event. Game-published values
            // (route start ticks, compiled routes) stay as the game sends them.
            const double delta = time - visual_tick_time_;
            visual_values_changed_unlocked();
            for (auto& item : visuals_) {
                auto& entity = item.second;
                entity.anchor.time += delta;
                entity.anchor.facing_time += delta;
                entity.intent.anchor_time += delta;
                entity.effects.time += delta;
            }
            retire_all_detached_unlocked();
            // The frame below is sampled against the new baseline.
            visual_tick_time_ = time;
        }
        const auto frame = visual_frame_value_unlocked(time,false);
        std::shared_ptr<RetainedVisualFrame> committed;
        if(visual_frame_cache_.use_count()>2)committed=std::make_shared<RetainedVisualFrame>(*frame);
        else committed=visual_frame_cache_;
        for (auto& sample : committed->entities) {
            auto* entity = visual_find_unlocked(sample.resource);
            if (!entity || !sample.values.unit) continue;
            if (sample.status == RetainedVisualStatus::ready ||
                sample.status == RetainedVisualStatus::unavailable_surface ||
                sample.status == RetainedVisualStatus::unavailable_terrain) {
                const bool pose_changed = entity->anchor.phase != sample.pose.phase ||
                    entity->anchor.selection != sample.pose.selection ||
                    entity->anchor.facing != sample.pose.facing ||
                    entity->anchor.root_rotation != sample.pose.root_rotation ||
                    entity->anchor.root != sample.pose.root;
                entity->anchor = sample.pose;
                entity->height_valid = sample.height_valid;
                entity->effects = sample.effects;
                entity->effects_anchored = true;
                if (pose_changed) ++entity->pose_revision;
                sample.pose_revision=entity->pose_revision;
            } else if (entity->status == RetainedVisualStatus::ready) {
                entity->status = sample.status;
                visual_record_failure_unlocked(*entity, sample.readiness_reason, sample.failure_scope);
            }
        }
        visual_tick_time_ = time;
        if (committed_entities) *committed_entities = committed->entities.size();
        visual_frame_changed_unlocked();
        visual_frame_cache_=std::move(committed);visual_frame_key_=visual_frame_key_unlocked();
        return true;
    } catch (...) { return visual_failure(failure, "tick.exception"); }
}


} // namespace owned
