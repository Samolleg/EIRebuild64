#pragma once
#include <cstdint>
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

// Zero is null. One process-wide counter, not reset at zone changes.
// Map/quest IDs and resource labels are independent of this runtime namespace.
ObjectId Next();
ObjectId GetNext();
// P15/P17 load boundary only: creation must be stopped and restored live IDs
// must be below this value. Restoring object IDs/save data is outside P08.
void SetNext(ObjectId id);

// Copying an object creates a fresh Instance; assignment keeps the target ID.
// Move is used only to preserve identity across Clear. No registry or owner.
class Instance
{
    ObjectId id;

public:
    Instance() : id(Next()) {}
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;
    Instance(Instance &&other) noexcept : id(std::exchange(other.id, 0)) {}
    Instance &operator=(Instance &&) = delete;
    ObjectId GetId() const { return id; }
    void Swap(Instance &other) noexcept { std::swap(id, other.id); }
};
} // namespace ObjectIdentity
