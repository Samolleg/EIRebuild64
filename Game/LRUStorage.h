#pragma once
#include "OwnedRender/ModelPreviewCache.h"

// Keep one source storage and its existing trim policy.
struct ModelPreviewMillisecondClock {
    static std::uint32_t now() noexcept { return timeGetTime(); }
};
template<class T,class M>
using CLRUStorage = OwnedRender::ModelPreviewCache<T,M,ModelPreviewMillisecondClock>;
