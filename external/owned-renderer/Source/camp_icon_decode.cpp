// Camp item icon decode (camp_icon_decode.h): shared by the x86 icon workers
// and the x64 host's icon worker.
#include "camp_icon_decode.h"
#include "owned_asset_loader.h"

#include <cstring>
#include <memory>
#include <vector>

namespace owned {

unsigned camp_icon_canvas(const std::string& name, unsigned variant) noexcept {
    if (!variant) return 256;
    const auto begins = [&](const char* prefix) { return name.compare(0,std::strlen(prefix),prefix)==0; };
    if (begins("unhuma")) return 1024;
    for (const auto* prefix : {"spell","modifier","material","qitem","litem","quitem","prototype00"})
        if (begins(prefix)) return 2048;
    for (const auto* prefix : {"pl","lg","sh","pt","gl","bt"})
        if (begins(prefix)) return 1024;
    return variant == 4 ? 2048 : 1024;
}

std::string decode_camp_icon(const ResourceStore& store, const std::string& name, bool high_resolution,
                             SurfaceImage& image, OwnedVisualSurface& description) {
    ResourceBlob blob; TextureCpuAsset texture; ResourceFailure why;
    unsigned variant = 0;
    bool resolved = false;
    if (high_resolution) {
        resolved = store.resolve(name + ".x4.mmp",blob,why);
        if (resolved) variant = 4;
        else {
            resolved = store.resolve(name + ".x2.mmp",blob,why);
            if (resolved) variant = 2;
        }
    }
    if (!resolved) resolved = store.resolve(name + ".mmp",blob,why);
    const unsigned canvas = camp_icon_canvas(name,variant);
    if (!resolved || !parse_texture_asset(blob,texture,why)) return why.message;
    if (texture.format != MmpFormat::argb8888 || texture.bit_count != 32 ||
        texture.width > canvas || texture.height > canvas)
        return "icon does not match the admitted BGRA/PNT3 canvas contract";
    description = {};
    description.width=description.height=canvas; description.pitch=canvas*2;
    description.format.dwSize=sizeof(DDPIXELFORMAT);
    description.format.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;
    description.format.dwRGBBitCount=16; description.format.dwRBitMask=0x7c00;
    description.format.dwGBitMask=0x03e0; description.format.dwBBitMask=0x001f;
    description.format.dwRGBAlphaBitMask=0x8000;
    image.width=image.height=canvas; image.pitch=canvas*4;
    image.format.dwSize=sizeof(DDPIXELFORMAT);
    image.format.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;
    image.format.dwRGBBitCount=32; image.format.dwRBitMask=0x00ff0000;
    image.format.dwGBitMask=0x0000ff00; image.format.dwBBitMask=0x000000ff;
    image.format.dwRGBAlphaBitMask=0xff000000;
    image.pixels.resize(std::size_t(image.pitch)*canvas);
    auto cut=std::make_shared<std::vector<std::uint8_t>>(
        (std::size_t(canvas)*canvas+7)/8,std::uint8_t{0xff});
    bool opaque=texture.width==canvas && texture.height==canvas;
    const auto* source=texture.source->data()+texture.mips[0].byte_offset;
    for (unsigned y=0;y<texture.height;++y)
        for (unsigned x=0;x<texture.width;++x) {
            std::uint32_t colour;
            std::memcpy(&colour,source+(y*texture.width+x)*4,4);
            const std::uint32_t alpha=(colour&0x80000000u)?0xff000000u:0;
            // Exactly the native 1555 quantization followed by
            // the normal upload's value*255/31 expansion.
            const std::uint32_t expanded=alpha|
                ((((colour>>19)&31u)*255u/31u)<<16)|
                ((((colour>>11)&31u)*255u/31u)<<8)|
                (((colour>>3)&31u)*255u/31u);
            const std::size_t bit=std::size_t(canvas-texture.height+y)*canvas+x;
            std::memcpy(image.pixels.data()+bit*4,&expanded,4);
            if (alpha) (*cut)[bit/8]&=std::uint8_t(~(1u<<(bit%8)));
            else opaque=false;
        }
    description.alpha_fully_opaque=opaque;
    if (!opaque) description.alpha_cut_mask=std::move(cut);
    return {};
}

}  // namespace owned
