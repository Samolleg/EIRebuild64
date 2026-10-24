#include "StdAfx.h"
#include "ObjectIdentity.h"
#include <limits>
#include <stdexcept>

namespace ObjectIdentity {
ObjectId Registry::Register(const void *object)
{
    if (!object)
        throw std::invalid_argument("ObjectId requires a live owner");
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.nextId)
        throw std::overflow_error("ObjectId namespace exhausted");
    const auto id = state.nextId;
    if (!objects.emplace(id, object).second)
        throw std::logic_error("Duplicate ObjectId");
    // Zero marks exhaustion, never an allocated ID.
    state.nextId = id == (std::numeric_limits<ObjectId>::max)() ? 0 : id + 1;
    return id;
}

void Registry::Unregister(ObjectId id, const void *object) noexcept
{
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = objects.find(id);
    if (found != objects.end() && found->second == object)
        objects.erase(found);
}

const void *Registry::Resolve(ObjectId id, std::uint64_t epoch) const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!id || epoch != state.epoch)
        return nullptr;
    const auto found = objects.find(id);
    return found == objects.end() ? nullptr : found->second;
}

State Registry::GetState() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return state;
}

void Registry::BeginEpoch()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!objects.empty())
        throw std::logic_error("Cannot reset ObjectId with live objects");
    if (state.epoch == (std::numeric_limits<std::uint64_t>::max)())
        throw std::overflow_error("ObjectId epoch exhausted");
    ++state.epoch;
    state.nextId = 1;
}

Registry &Runtime()
{
    // Process lifetime: global objects may unregister during static teardown.
    static Registry *registry = new Registry;
    return *registry;
}

Instance::Instance(const void *object, Registry &domain)
    : registry(&domain), owner(object), id(domain.Register(object))
{
}

Instance::~Instance()
{
    if (registry)
        registry->Unregister(id, owner);
}

Instance::Instance(Instance &&other) noexcept
    : registry(other.registry), owner(other.owner), id(other.id)
{
    other.registry = nullptr;
    other.owner = nullptr;
    other.id = 0;
}

void Instance::Swap(Instance &other) noexcept
{
    std::swap(registry, other.registry);
    std::swap(owner, other.owner);
    std::swap(id, other.id);
}
} // namespace ObjectIdentity
