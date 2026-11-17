#pragma once
#include <cstddef>
#include <cstdint>

namespace owned {

// Semantic caller facts replace return-address tests in source hosts. A
// preview is independent of the world pass, even for a positive world id.
// Both interface domains keep private poses and never update world picking.
// Only character previews opt into the camera-relative studio light.
enum class ModelDrawDomain : std::uint8_t { normal, preview, interface_decoration };
inline thread_local ModelDrawDomain model_draw_domain = ModelDrawDomain::normal;
class ModelDrawScope {
    ModelDrawDomain previous_;
public:
    explicit ModelDrawScope(ModelDrawDomain domain) noexcept
        : previous_(model_draw_domain) { model_draw_domain = domain; }
    ~ModelDrawScope() { model_draw_domain = previous_; }
    ModelDrawScope(const ModelDrawScope&) = delete;
    ModelDrawScope& operator=(const ModelDrawScope&) = delete;
};
inline bool model_draw_is_preview() noexcept {
    return model_draw_domain != ModelDrawDomain::normal;
}
inline bool model_draw_is_character_preview() noexcept {
    return model_draw_domain == ModelDrawDomain::preview;
}
inline bool model_draw_uses_retained_world(bool world_pass, bool claimed,
    ModelDrawDomain domain) noexcept {
    return world_pass && claimed && domain == ModelDrawDomain::normal;
}
inline bool model_draw_writes_pick_state(ModelDrawDomain domain) noexcept {
    return domain == ModelDrawDomain::normal;
}

struct ModelPartFacts {
    std::uint32_t texture_set = 0;
    bool inactive = false;
    bool hidden = false;
    std::uint32_t visible = 0;
};
struct ModelPartResult {
    std::uint32_t visible = 0;
    std::size_t submitted = 0;
};

// Shared object producer: immutable membership comes from either source fields
// or the retail render plan. Only adapters read game objects. The producer owns
// admission, ordered texture-set transitions and part execution; it never
// retains a callback or borrowed input beyond this synchronous invocation.
template<class Read, class Bind, class Draw>
bool produce_model_parts(std::size_t count, Read&& read, Bind&& bind, Draw&& draw,
    ModelPartResult& result) {
    result = {};
    std::uint32_t active_set = ~std::uint32_t(0);
    for (std::size_t i = 0; i < count; ++i) {
        ModelPartFacts facts;
        if (!read(i, facts)) return false;
        if (facts.inactive) continue;
        if (facts.hidden) { result.visible |= facts.visible; continue; }
        if (active_set != facts.texture_set) {
            if (!bind(facts.texture_set)) return false;
            active_set = facts.texture_set;
        }
        std::uint32_t visible = 0;
        if (!draw(i, visible)) return false;
        result.visible |= visible;
        if (visible) ++result.submitted;
    }
    return true;
}

} // namespace owned
