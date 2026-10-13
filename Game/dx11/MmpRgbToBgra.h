#pragma once

#include "../mmpFormat.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace MmpRgbToBgra {
inline BYTE Expand(DWORD pixel, DWORD mask, DWORD shift, DWORD bits, BYTE fallback) noexcept
{
    if (!mask || !bits || bits > 8) return fallback;
    const DWORD maximum = (DWORD(1) << bits) - 1;
    return static_cast<BYTE>((((pixel & mask) >> shift) * 255u + maximum / 2u) / maximum);
}

inline bool Convert(const SMMPFileHeader& header, const BYTE* source, size_t pixels,
                    std::vector<DWORD>& destination)
{
    if (!source || (header.dwBitDepth != 16 && header.dwBitDepth != 32) ||
        !header.dwRMask || !header.dwGMask || !header.dwBMask)
        return false;
    destination.resize(pixels);
    for (size_t i = 0; i < pixels; ++i) {
        DWORD packed = 0;
        if (header.dwBitDepth == 16) {
            WORD value;
            memcpy(&value, source + i * sizeof(value), sizeof(value));
            packed = value;
        } else {
            memcpy(&packed, source + i * sizeof(packed), sizeof(packed));
        }
        const BYTE r = Expand(packed, header.dwRMask, header.dwRShift, header.dwRBits, 0);
        const BYTE g = Expand(packed, header.dwGMask, header.dwGShift, header.dwGBits, 0);
        const BYTE b = Expand(packed, header.dwBMask, header.dwBShift, header.dwBBits, 0);
        const BYTE a = header.dwABits
            ? Expand(packed, header.dwAMask, header.dwAShift, header.dwABits, 0)
            : 255;
        destination[i] = (DWORD(a) << 24) | (DWORD(r) << 16) | (DWORD(g) << 8) | DWORD(b);
    }
    return true;
}
}
