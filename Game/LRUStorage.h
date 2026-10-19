#pragma once
#include "model_preview_cache.h"

// Keep the source API; the renderer owns the only storage and trim policy.
struct ModelPreviewMillisecondClock {
    static std::uint32_t now() noexcept { return timeGetTime(); }
};
template<class T,class M>
using CLRUStorage = owned::ModelPreviewCache<T,M,ModelPreviewMillisecondClock>;
