#pragma once
#include <mutex>

namespace OwnedRender {
// Serialize whole frame producers as well as client API calls. Reentrancy is
// needed by movie frames invoked inside a main-thread UI draw callback.
class E1RenderGate {
    mutable std::recursive_mutex mutex;
public:
    using Lease = std::unique_lock<std::recursive_mutex>;
    Lease Lock() const { return Lease(mutex); }
    // A loading worker must not block acquiring the gate: the main thread can
    // stop/join it from a nested load while it already owns a frame lease.
    Lease Try() const { return Lease(mutex, std::try_to_lock); }
};
}
