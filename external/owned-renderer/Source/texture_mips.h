#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
namespace owned {
inline unsigned full_mip_count(unsigned width,unsigned height) noexcept {
    unsigned levels=1;for(unsigned size=std::max(width,height);size>1;size>>=1)++levels;return levels;
}
// A shipped BC1 mip chain may stand in for a one-level surface only when its
// level 0 is byte-identical to that surface (`pixels`, row pitch `pitch`) and
// every further level has the exact halved size and enough bytes.
template<class Mip>
bool authored_bc1_chain_matches(const Mip* mips,std::uint32_t count,unsigned width,unsigned height,
                                const std::uint8_t* pixels,std::size_t pixel_bytes,std::size_t pitch) noexcept {
    if(!mips||count<2||!pixels||mips[0].width!=width||mips[0].height!=height)return false;
    for(std::uint32_t level=0;level<count;++level){
        const unsigned w=std::max(1u,width>>level),h=std::max(1u,height>>level);
        const std::size_t row=std::size_t(std::max(1u,(w+3)/4))*8,rows=std::max(1u,(h+3)/4);
        if(!mips[level].data||mips[level].width!=w||mips[level].height!=h||mips[level].bytes<row*rows)return false;
        if(level==0){
            if(pitch<row||pitch*rows>pixel_bytes)return false;
            for(std::size_t y=0;y<rows;++y)
                if(std::memcmp(mips[0].data+y*row,pixels+y*pitch,row)!=0)return false;
        }
    }
    return true;
}
inline std::uint64_t uncompressed_mip_bytes(unsigned width,unsigned height,unsigned bytes) noexcept {
    std::uint64_t total=0;for(;;){total+=std::uint64_t(width)*height*bytes;if(width==1&&height==1)return total;width=std::max(1u,width/2);height=std::max(1u,height/2);}
}
}
