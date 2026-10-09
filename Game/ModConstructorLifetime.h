#pragma once
#include <cstring>
#include "EngineDiagnostics.h"

namespace ModConstructorLifetime {
enum Owner { World = 0, Party = 1 };

// A native patch survives Lua-session teardown. Keep only its loaded decision
// in constant-initialized POD storage; static world destructors must not ask Lua.
inline volatile LONG& PolicyState()
{
    static volatile LONG state = -1; // No mod policy has been bound.
    return state;
}
inline bool PolicyBound()
{
    return InterlockedCompareExchange(&PolicyState(), 0, 0) >= 0;
}
inline bool GuardEnabled()
{
    return InterlockedCompareExchange(&PolicyState(), 0, 0) > 0;
}
inline void BindPolicy(bool enabled)
{
    InterlockedExchange(&PolicyState(), enabled ? 1 : 0);
    EngineDiagnostics::Event("Constructor shutdown policy bound enabled=%d lifetime=process", enabled ? 1 : 0);
}

inline void NoteInvalid(Owner site, const void* object, const void* vtable)
{
    static volatile LONG counts[2] = {0, 0};
    const LONG count = InterlockedIncrement(&counts[site]);
    const char* name = site == World ? "world_spell" : "party_spell";
    if (count <= 4)
        EngineDiagnostics::Event("Constructor shutdown skipped owner=%s object=%p vtable=%p event=%ld", name, object, vtable, count);
    else if (count == 5)
        EngineDiagnostics::Event("Constructor shutdown further invalid releases suppressed owner=%s", name);
}

// The installed addon guards the deleting virtual call, after the owner's
// existing reference decrement. It does not validate dangling pointers.
template<class T, class Skipped>
bool Release(T*& owner, bool guarded, Skipped skipped)
{
    if (!owner)
        return false;
    T* object = owner;
    object->DecRefCount();
    const void* vtable = nullptr;
    if (guarded)
        std::memcpy(&vtable, static_cast<const void*>(object), sizeof(vtable));
    if (guarded && !vtable)
    {
        skipped(object, vtable);
        owner = nullptr;
        return false;
    }
    delete object;
    owner = nullptr;
    return true;
}

template<class T, class Skipped>
bool ReleaseOwned(T*& owner, Skipped skipped)
{
    return Release(owner, GuardEnabled(), skipped);
}
}
