#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>

using ObjectId = std::uint64_t;

template <class T>
struct ObjectIdLess
{
    bool operator()(const T *a, const T *b) const
    {
        return (a ? a->GetRTID() : 0) < (b ? b->GetRTID() : 0);
    }
};

namespace ObjectIdentity {
template <class T>
ObjectId IdOf(const T *object) { return object ? object->GetRTID() : 0; }

// Zero is null. IDs never wrap or get reused within an epoch. Map/quest IDs
// and resource labels are independent of this runtime namespace.
struct State
{
    ObjectId nextId = 1;
    std::uint64_t epoch = 1;
};

class Registry
{
    mutable std::mutex mutex;
    std::map<ObjectId, const void *> objects;
    State state;

public:
    ObjectId Register(const void *object);
    void Unregister(ObjectId id, const void *object) noexcept;
    // Borrowed address only; the caller must keep the object alive.
    // An epoch is required so an old handle cannot resolve after a reset.
    const void *Resolve(ObjectId id, std::uint64_t epoch) const;
    State GetState() const;
    // Explicit boundary for a new domain. Live objects forbid a reset.
    // Save restoration and assignment of saved IDs belong to P15/P17.
    void BeginEpoch();
};

// One authoritative allocator for runtime creation, shared by the base classes.
// Not reset at zone changes. A new process starts at epoch 1 / ID 1.
Registry &Runtime();

// Owned by one object. Copying an object creates a fresh Instance; assignment
// keeps the target identity. Move is used only to preserve identity across Clear.
class Instance
{
    Registry *registry;
    const void *owner;
    ObjectId id;

public:
    explicit Instance(const void *object, Registry &domain = Runtime());
    ~Instance();
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    Instance(Instance &&other) noexcept;
    Instance &operator=(Instance &&) = delete;
    ObjectId GetId() const { return id; }
    void Swap(Instance &other) noexcept;
};
} // namespace ObjectIdentity
