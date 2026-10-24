#pragma once
#include <cstdint>
#include <utility>
template <class T> class CPtr;
#include <stdexcept>

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

// Zero is null. One process-wide counter, not reset at zone changes.
// Map/quest IDs and resource labels are independent of this runtime namespace.
ObjectId Next();
ObjectId GetNext();
// P15/P17 load boundary only: creation must be stopped and restored live IDs
// must be below this value. Restoring object IDs/save data is outside P08.
void SetNext(ObjectId id);
// Reserve the complete restored namespace before constructors allocate nested IDs.
void EnsureNext(ObjectId id);

// Simulation-thread staging namespace. Constructors allocate locally; the live
// counter changes only on Publish(), after the complete slot has been accepted.
class RestoreNamespace
{
    RestoreNamespace *previous;
    ObjectId next;
    friend ObjectId Next();
    friend ObjectId GetNext();
    friend void SetNext(ObjectId);
    friend void EnsureNext(ObjectId);
public:
    explicit RestoreNamespace(ObjectId minimum);
    ~RestoreNamespace();
    RestoreNamespace(const RestoreNamespace &) = delete;
    RestoreNamespace &operator=(const RestoreNamespace &) = delete;
    // Successful nested documents reserve their allocations in the enclosing
    // slot namespace. This never changes the process counter.
    void KeepInParent();
    static bool Active();
    // Call once at world adoption, outside any rejected/temporary namespace.
    static void Publish(ObjectId minimum);
};

// Construction-only restore. The ticket is consumed by the first Instance base;
// nested members get fresh IDs. It cannot assign an ID to an existing object.
namespace Detail {
class RestoreTicket
{
    ObjectId id;
    RestoreTicket *previous;
    friend ObjectId InitialId();
public:
    explicit RestoreTicket(ObjectId savedId);
    ~RestoreTicket();
    RestoreTicket(const RestoreTicket &) = delete;
    RestoreTicket &operator=(const RestoreTicket &) = delete;
    void RequireConsumed() const;
};
ObjectId InitialId();
}
// Owner defaults to CPtr<T>; CMFundament uses CMPtr<T>, CGameBase/CRefCount
// use CSmartPtr<T>. Matching ownership also cleans up a rejected factory.
// Exclusive load boundary only. No publication from constructors; the caller
// must exclude saved IDs still in a live world before creating the new graph.
template <class Owner, class Factory>
Owner CreateWith(ObjectId savedId, Factory factory)
{
    Detail::RestoreTicket ticket(savedId);
    Owner object(factory());
    ticket.RequireConsumed();
    if (object == nullptr || object->GetRTID() != savedId)
        throw std::invalid_argument("restored ObjectId belongs to a nested object");
    return object;
}
template <class T, class Owner = CPtr<T>, class... Args>
Owner Create(ObjectId savedId, Args &&...args)
{
    return CreateWith<Owner>(savedId, [&] { return new T(std::forward<Args>(args)...); });
}

// Copying an object creates a fresh Instance; assignment keeps the target ID.
// Move is used only to preserve identity across Clear. No registry or owner.
class Instance
{
    ObjectId id;

public:
    Instance() : id(Detail::InitialId()) {}
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    Instance(Instance &&other) noexcept : id(std::exchange(other.id, 0)) {}
    Instance &operator=(Instance &&) = delete;
    ObjectId GetId() const { return id; }
    void Swap(Instance &other) noexcept { std::swap(id, other.id); }
};
} // namespace ObjectIdentity
