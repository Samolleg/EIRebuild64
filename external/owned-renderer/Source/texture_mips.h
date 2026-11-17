#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
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
// 128-bit content value of block-compressed rows (`row` bytes each, `pitch`
// apart): two independent 64-bit mixes. A game surface and an archive member's
// level 0 with the same size, format and value hold the same bytes
// (authored_surface_chain); zero means "no value".
inline void block_content_value(const std::uint8_t* pixels,std::size_t pitch,std::size_t row,std::size_t rows,
                                std::uint64_t value[2]) noexcept {
    std::uint64_t a=0x9E3779B97F4A7C15ull^(std::uint64_t(row)<<32)^rows,b=0xC2B2AE3D27D4EB4Full+row*31u+rows;
    for(std::size_t y=0;y<rows;++y){
        const std::uint8_t* p=pixels+y*pitch;
        for(std::size_t x=0;x+8<=row;x+=8){
            std::uint64_t word;std::memcpy(&word,p+x,8);
            a=(a^word)*0x100000001B3ull;a^=a>>29;
            b=(b+word)*0xFF51AFD7ED558CCDull;b^=b>>32;
        }
    }
    value[0]=a?a:1;value[1]=b;
}
// One BGRA8 mip level as the 2x2 box of the level above (w x h).
inline void box_mip_level(const std::uint32_t* above,unsigned w,unsigned h,std::uint32_t* out) noexcept {
    const unsigned nw=std::max(1u,w/2),nh=std::max(1u,h/2);
    for(unsigned y=0;y<nh;++y){
        const std::uint32_t* const r0=above+std::size_t(std::min(y*2,h-1))*w;
        const std::uint32_t* const r1=above+std::size_t(std::min(y*2+1,h-1))*w;
        for(unsigned x=0;x<nw;++x){
            const unsigned x0=std::min(x*2,w-1),x1=std::min(x*2+1,w-1);
            const std::uint32_t a=r0[x0],b=r0[x1],c=r1[x0],d=r1[x1];
            std::uint32_t value=0;
            for(unsigned shift=0;shift<32;shift+=8)
                value|=((((a>>shift)&255u)+((b>>shift)&255u)+((c>>shift)&255u)+((d>>shift)&255u)+2u)/4u)<<shift;
            out[std::size_t(y)*nw+x]=value;
        }
    }
}
// Alpha-test coverage across generated levels. A cut-out (every level-0 alpha
// 0 or 255, both present) loses coverage under a box filter: thin leaves and
// bars fall below the test and thin out with distance. cut_out_coverage gives
// level 0's solid share; preserve_alpha_coverage scales a generated level's
// alpha up until the share of its texels that pass the test is that again.
// The test is the static cut-out one, alpha > 200 (the active map's retained
// statics, and the neighbour figures that match them).
// Textures with any partial alpha at level 0 (blended art) are not cut-outs.
inline bool cut_out_coverage(const std::uint32_t* pixels,std::size_t count,float& coverage) noexcept {
    std::size_t solid=0;
    for(std::size_t i=0;i<count;++i){const std::uint32_t a=pixels[i]>>24;if(a==255u)++solid;else if(a)return false;}
    if(!solid||solid==count)return false;
    coverage=float(solid)/float(count);return true;
}
inline void preserve_alpha_coverage(std::uint32_t* pixels,std::size_t count,float coverage) noexcept {
    if(count<16)return; // the last levels have too few texels for a share
    const auto share=[&](float scale){std::size_t n=0;
        for(std::size_t i=0;i<count;++i)n+=float(pixels[i]>>24)*scale+.5f>=201.f?1u:0u;
        return float(n)/float(count);};
    if(share(1.f)>=coverage)return;
    float lo=1.f,hi=8.f;
    for(int step=0;step<12;++step){const float mid=(lo+hi)*.5f;if(share(mid)<coverage)lo=mid;else hi=mid;}
    for(std::size_t i=0;i<count;++i){
        const std::uint32_t a=std::min(255u,static_cast<std::uint32_t>(float(pixels[i]>>24)*hi+.5f));
        pixels[i]=(pixels[i]&0x00ffffffu)|(a<<24);
    }
}
// One DXT level (kind 1, 3 or 5: 8-, 16-, 16-byte blocks) as BGRA8, w x h.
inline void decode_dxt_level(const std::uint8_t* data,unsigned w,unsigned h,unsigned kind,std::uint32_t* out) noexcept {
    const unsigned bw=std::max(1u,(w+3)/4),bh=std::max(1u,(h+3)/4);
    const std::size_t block=kind==1?8u:16u;
    for(unsigned by=0;by<bh;++by)for(unsigned bx=0;bx<bw;++bx){
        const std::uint8_t* b=data+(std::size_t(by)*bw+bx)*block;
        const std::uint8_t* colour=kind==1?b:b+8;
        const unsigned c0=colour[0]|(colour[1]<<8),c1=colour[2]|(colour[3]<<8);
        const std::uint32_t bits=colour[4]|(colour[5]<<8)|(colour[6]<<16)|(std::uint32_t(colour[7])<<24);
        std::uint32_t rgb[4][3];bool clear3=false;
        const auto expand=[](unsigned c,std::uint32_t* v){const unsigned r=c>>11,g=(c>>5)&63u,bl=c&31u;
            v[0]=(r<<3)|(r>>2);v[1]=(g<<2)|(g>>4);v[2]=(bl<<3)|(bl>>2);};
        expand(c0,rgb[0]);expand(c1,rgb[1]);
        if(kind!=1||c0>c1)for(int k=0;k<3;++k){rgb[2][k]=(2*rgb[0][k]+rgb[1][k]+1)/3;rgb[3][k]=(rgb[0][k]+2*rgb[1][k]+1)/3;}
        else{for(int k=0;k<3;++k){rgb[2][k]=(rgb[0][k]+rgb[1][k])/2;rgb[3][k]=0;}clear3=true;}
        std::uint32_t alpha[16];
        if(kind==1)for(int i=0;i<16;++i)alpha[i]=255u;
        else if(kind==3)for(int i=0;i<16;++i)alpha[i]=((b[i/2]>>((i&1)*4))&15u)*17u;
        else{
            std::uint32_t a[8];a[0]=b[0];a[1]=b[1];
            if(a[0]>a[1])for(std::uint32_t k=1;k<7;++k)a[k+1]=((7-k)*a[0]+k*a[1]+3)/7;
            else{for(std::uint32_t k=1;k<5;++k)a[k+1]=((5-k)*a[0]+k*a[1]+2)/5;a[6]=0;a[7]=255;}
            std::uint64_t index=0;for(int k=0;k<6;++k)index|=std::uint64_t(b[2+k])<<(8*k);
            for(int i=0;i<16;++i)alpha[i]=a[(index>>(3*i))&7u];
        }
        for(unsigned y=0;y<4&&by*4+y<h;++y)for(unsigned x=0;x<4&&bx*4+x<w;++x){
            const unsigned i=y*4+x,pick=(bits>>(2*i))&3u;
            const std::uint32_t a=clear3&&pick==3?0u:alpha[i];
            out[std::size_t(by*4+y)*w+bx*4+x]=(a<<24)|(rgb[pick][0]<<16)|(rgb[pick][1]<<8)|rgb[pick][2];
        }
    }
}
inline std::uint64_t uncompressed_mip_bytes(unsigned width,unsigned height,unsigned bytes) noexcept {
    std::uint64_t total=0;for(;;){total+=std::uint64_t(width)*height*bytes;if(width==1&&height==1)return total;width=std::max(1u,width/2);height=std::max(1u,height/2);}
}
}
