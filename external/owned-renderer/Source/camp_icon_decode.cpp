// Camp item icon decode (camp_icon_decode.h): shared by the x86 icon workers
// and the x64 host's icon worker.
#include "camp_icon_decode.h"
#include "owned_asset_loader.h"

#include <algorithm>
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
                             SurfaceImage& image, OwnedVisualSurface& description, bool* placeholder,
                             unsigned basis, unsigned factor, unsigned* level_out) {
    if (level_out) *level_out = 0;
    ResourceBlob blob; TextureCpuAsset texture; ResourceFailure why;
    unsigned variant = 0;
    bool resolved = false, absent = true;
    if (placeholder) *placeholder = false;
    if (high_resolution) {
        resolved = store.resolve(name + ".x4.mmp",blob,why);
        if (resolved) variant = 4;
        else {
            absent = why.code == ResourceFailureCode::not_found;
            resolved = store.resolve(name + ".x2.mmp",blob,why);
            if (resolved) variant = 2;
            else absent = absent && why.code == ResourceFailureCode::not_found;
        }
    }
    if (!resolved) {
        resolved = store.resolve(name + ".mmp",blob,why);
        absent = absent && !resolved && why.code == ResourceFailureCode::not_found;
    }
    if (!resolved && absent) {
        // No archive holds the icon under any of the game's three names
        // (Dejavu: ax_10.0, hl_05.1, hl_05.2, sw_12.0). The game's icon
        // allocator then makes a 256 canvas filled with 0xffff: an opaque
        // white square. The same canvas here, through the owned path.
        const unsigned blank = 256;
        description = {};
        description.width=description.height=blank; description.pitch=blank*2;
        description.format.dwSize=sizeof(DDPIXELFORMAT);
        description.format.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;
        description.format.dwRGBBitCount=16; description.format.dwRBitMask=0x7c00;
        description.format.dwGBitMask=0x03e0; description.format.dwBBitMask=0x001f;
        description.format.dwRGBAlphaBitMask=0x8000;
        description.alpha_fully_opaque=true;
        image.width=image.height=blank; image.pitch=blank*4;
        image.format.dwSize=sizeof(DDPIXELFORMAT);
        image.format.dwFlags=DDPF_RGB|DDPF_ALPHAPIXELS;
        image.format.dwRGBBitCount=32; image.format.dwRBitMask=0x00ff0000;
        image.format.dwGBitMask=0x0000ff00; image.format.dwBBitMask=0x000000ff;
        image.format.dwRGBAlphaBitMask=0xff000000;
        image.pixels.resize(std::size_t(image.pitch)*blank,std::uint8_t{0xff});
        if (placeholder) *placeholder = true;
        return {};
    }
    const unsigned full_canvas = camp_icon_canvas(name,variant);
    if (!resolved || !parse_texture_asset(blob,texture,why)) return why.message;
    if (texture.format != MmpFormat::argb8888 || texture.bit_count != 32 ||
        texture.width > full_canvas || texture.height > full_canvas)
        return "icon does not match the admitted BGRA/PNT3 canvas contract";
    // The icon's own mip level for the size it is shown at (a 1024..2048
    // canvas for a cell of a few hundred pixels): a level is used only when
    // the file holds it at exactly half the previous level's size.
    unsigned level = 0;
    const unsigned side = (std::max)(texture.width, texture.height);
    const auto exact = [&](unsigned candidate) {
        return candidate < texture.mips.size() && (full_canvas >> candidate) &&
            texture.mips[candidate].width == (std::max)(1u, texture.width >> candidate) &&
            texture.mips[candidate].height == (std::max)(1u, texture.height >> candidate) &&
            std::size_t(texture.mips[candidate].byte_offset) + std::size_t(texture.mips[candidate].width) *
                texture.mips[candidate].height * 4u <= texture.source->size();
    };
    if (factor) { while (exact(level + 1) && (2u << level) <= factor) ++level; }
    else if (basis) { while (exact(level + 1) && (side >> (level + 1)) >= 2u * basis) ++level; }
    if (level_out) *level_out = level;
    const unsigned canvas = full_canvas >> level;
    const unsigned width = texture.mips[level].width, height = texture.mips[level].height;
    description = {};
    description.width=description.height=full_canvas; description.pitch=full_canvas*2;
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
    bool opaque=width==canvas && height==canvas;
    const auto* source=texture.source->data()+texture.mips[level].byte_offset;
    for (unsigned y=0;y<height;++y)
        for (unsigned x=0;x<width;++x) {
            std::uint32_t colour;
            std::memcpy(&colour,source+(std::size_t(y)*width+x)*4,4);
            const std::uint32_t alpha=(colour&0x80000000u)?0xff000000u:0;
            // Exactly the native 1555 quantization followed by
            // the normal upload's value*255/31 expansion.
            const std::uint32_t expanded=alpha|
                ((((colour>>19)&31u)*255u/31u)<<16)|
                ((((colour>>11)&31u)*255u/31u)<<8)|
                (((colour>>3)&31u)*255u/31u);
            const std::size_t bit=std::size_t(canvas-height+y)*canvas+x;
            std::memcpy(image.pixels.data()+bit*4,&expanded,4);
            if (alpha) (*cut)[bit/8]&=std::uint8_t(~(1u<<(bit%8)));
            else opaque=false;
        }
    description.alpha_fully_opaque=opaque;
    if (!opaque && !level) description.alpha_cut_mask=std::move(cut);
    return {};
}

}  // namespace owned
