#include "StdAfx.h"
#include "ObjectIdentity.h"
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace ObjectIdentity {
namespace {
std::atomic<ObjectId> nextId{1};
thread_local RestoreNamespace *restoreNamespace = nullptr;
}

namespace Detail {
namespace { thread_local RestoreTicket *pending = nullptr; }
RestoreTicket::RestoreTicket(ObjectId savedId) : id(savedId), previous(pending)
{
    if (!savedId || savedId >= (std::numeric_limits<ObjectId>::max)() - 1)
        throw std::invalid_argument("invalid restored ObjectId");
    pending = this;
}
RestoreTicket::~RestoreTicket() { pending = previous; }
void RestoreTicket::RequireConsumed() const
{
    if (id) throw std::invalid_argument("factory did not construct an identity base");
}
ObjectId InitialId()
{
    if (pending && pending->id)
    {
        const ObjectId saved = std::exchange(pending->id, 0);
        EnsureNext(saved + 1);
        return saved;
    }
    return Next();
}
}

RestoreNamespace::RestoreNamespace(ObjectId minimum)
    : previous(restoreNamespace), next((std::max)(GetNext(), minimum))
{
    if (!minimum || minimum >= (std::numeric_limits<ObjectId>::max)())
        throw std::invalid_argument("invalid restore namespace");
    restoreNamespace = this;
}
RestoreNamespace::~RestoreNamespace() { restoreNamespace = previous; }
bool RestoreNamespace::Active() { return restoreNamespace != nullptr; }
void RestoreNamespace::KeepInParent()
{
    if (previous) previous->next = (std::max)(previous->next, next);
}
void RestoreNamespace::Publish(ObjectId minimum)
{
    if (restoreNamespace) throw std::logic_error("cannot publish inside restore namespace");
    EnsureNext(minimum);
}

ObjectId Next()
{
    if (restoreNamespace)
    {
        const auto id = restoreNamespace->next;
        if (!id || id >= (std::numeric_limits<ObjectId>::max)() - 1)
            throw std::overflow_error("restored ObjectId namespace exhausted");
        ++restoreNamespace->next;
        return id;
    }
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
    return restoreNamespace ? restoreNamespace->next : nextId.load(std::memory_order_relaxed);
}

void EnsureNext(ObjectId id)
{
    if (!id) throw std::invalid_argument("Next ObjectId must be nonzero");
    if (restoreNamespace) { restoreNamespace->next = (std::max)(restoreNamespace->next, id); return; }
    ObjectId next=nextId.load(std::memory_order_relaxed);
    while(next<id && !nextId.compare_exchange_weak(next,id,std::memory_order_relaxed)) {}
}

void SetNext(ObjectId id)
{
    if (!id)
        throw std::invalid_argument("Next ObjectId must be nonzero");
    if (restoreNamespace) { restoreNamespace->next = id; return; }
    nextId.store(id, std::memory_order_relaxed);
}
} // namespace ObjectIdentity
