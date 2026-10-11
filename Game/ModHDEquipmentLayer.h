#pragma once

namespace ModHDEquipmentLayer {
constexpr bool Supported(unsigned width, unsigned height, DWORD format)
{
    return width == 512 && height == 512 &&
        (format == MMP_FORMAT_PNT3 || format == MMP_FORMAT_PAINT32 ||
         format == MMP_FORMAT_8888);
}

constexpr bool UsesRunLength(DWORD format)
{
    return format == MMP_FORMAT_PNT3;
}

static_assert(Supported(512, 512, MMP_FORMAT_8888), "Raw HD equipment layers must be composable");
static_assert(Supported(512, 512, MMP_FORMAT_PNT3), "PNT3 HD equipment layers must be composable");
static_assert(Supported(512, 512, MMP_FORMAT_PAINT32), "PAINT32 HD equipment layers must be composable");
static_assert(!Supported(511, 512, MMP_FORMAT_8888), "Wrong-sized HD equipment layers must be rejected");
static_assert(!Supported(512, 512, MMP_FORMAT_DXT3), "Compressed HD equipment layers must be rejected");
}
