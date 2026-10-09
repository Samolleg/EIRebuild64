#pragma once
#include "WorldBackgroundData.h"

#pragma pack(push, 8)
namespace WorldBackground {
struct Pixels {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> argb;
};
// Top mip only, top-to-bottom, straight alpha, no engine texture manager.
// Output allocation is bounded independently of the encoded input.
Pixels DecodePixels(const Blob& mmp, size_t budgetBytes = 64u * 1024u * 1024u);
}
#pragma pack(pop)
