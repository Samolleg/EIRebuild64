#include "StdAfx.h"
#include "ObjectIdentity.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace ObjectIdentity {
namespace {
std::atomic<ObjectId> nextId{1};
}

ObjectId Next()
{
    const ObjectId id = nextId.fetch_add(1, std::memory_order_relaxed);
    if (!id || id == (std::numeric_limits<ObjectId>::max)())
    {
        // Fatal rather than recoverable: fetch_add has reached the wrap boundary.
        // No caller may continue creating objects with reused IDs.
        std::fputs("ObjectId namespace exhausted\n", stderr);
        std::abort();
    }
    return id;
}

ObjectId GetNext()
{
    return nextId.load(std::memory_order_relaxed);
}

void SetNext(ObjectId id)
{
    if (!id)
        throw std::invalid_argument("Next ObjectId must be nonzero");
    nextId.store(id, std::memory_order_relaxed);
}
} // namespace ObjectIdentity
