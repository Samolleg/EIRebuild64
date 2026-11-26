#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>
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
// One BC1 block (8 bytes) from 16 BGRA8 texels (row-major 4x4). Endpoints
// along the texels' principal axis (power iteration on the RGB covariance),
// at their extremes, then two least-squares refinements of the endpoints for
// the chosen indices (range fit). A block with texels below alpha 128 uses
// the three-colour mode with index 3 transparent.
inline void encode_bc1_block(const std::uint32_t* texels,std::uint8_t* out) noexcept {
    float c[16][3];bool clear[16];bool any_clear=false;
    float mean[3]={0,0,0};int solid=0;
    for(int i=0;i<16;++i){
        c[i][0]=float((texels[i]>>16)&255u);c[i][1]=float((texels[i]>>8)&255u);c[i][2]=float(texels[i]&255u);
        clear[i]=(texels[i]>>24)<128u;any_clear|=clear[i];
        if(!clear[i]){for(int k=0;k<3;++k)mean[k]+=c[i][k];++solid;}
    }
    const auto pack=[](const float* v){
        const auto q=[](float x,float levels){return unsigned(std::clamp(x,0.f,255.f)*levels/255.f+.5f);};
        return std::uint16_t((q(v[0],31)<<11)|(q(v[1],63)<<5)|q(v[2],31));};
    const auto unpack=[](unsigned p,float* v){const unsigned r=p>>11,g=(p>>5)&63u,b=p&31u;
        v[0]=float((r<<3)|(r>>2));v[1]=float((g<<2)|(g>>4));v[2]=float((b<<3)|(b>>2));};
    if(!solid){std::memset(out,0,4);out[4]=out[5]=out[6]=out[7]=255;return;} // all clear: c0==c1, index 3
    for(int k=0;k<3;++k)mean[k]/=float(solid);
    float cov[6]={0,0,0,0,0,0};
    for(int i=0;i<16;++i)if(!clear[i]){
        const float d[3]={c[i][0]-mean[0],c[i][1]-mean[1],c[i][2]-mean[2]};
        cov[0]+=d[0]*d[0];cov[1]+=d[0]*d[1];cov[2]+=d[0]*d[2];cov[3]+=d[1]*d[1];cov[4]+=d[1]*d[2];cov[5]+=d[2]*d[2];
    }
    float axis[3]={.577f,.577f,.577f};
    for(int it=0;it<8;++it){
        const float n[3]={cov[0]*axis[0]+cov[1]*axis[1]+cov[2]*axis[2],cov[1]*axis[0]+cov[3]*axis[1]+cov[4]*axis[2],
                          cov[2]*axis[0]+cov[4]*axis[1]+cov[5]*axis[2]};
        const float len=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(len<1e-6f)break;
        for(int k=0;k<3;++k)axis[k]=n[k]/len;
    }
    float lo=1e30f,hi=-1e30f;
    for(int i=0;i<16;++i)if(!clear[i]){
        const float t=(c[i][0]-mean[0])*axis[0]+(c[i][1]-mean[1])*axis[1]+(c[i][2]-mean[2])*axis[2];
        lo=std::min(lo,t);hi=std::max(hi,t);
    }
    float e0[3],e1[3];
    for(int k=0;k<3;++k){e0[k]=mean[k]+axis[k]*hi;e1[k]=mean[k]+axis[k]*lo;}
    const int colours=any_clear?3:4;
    // Endpoint 0's weight per palette entry (four: 1, 0, 2/3, 1/3; three: 1, 0, 1/2).
    static const float w4[4]={1,0,2.f/3,1.f/3},w3[3]={1,0,.5f};
    const float* weight=colours==4?w4:w3;
    unsigned idx[16]={};
    const auto assign=[&](const float* a,const float* b){
        for(int i=0;i<16;++i){
            if(clear[i]){idx[i]=3;continue;}
            float best=1e30f;
            for(int p=0;p<colours;++p){
                float e=0;for(int k=0;k<3;++k){const float v=a[k]*weight[p]+b[k]*(1-weight[p])-c[i][k];e+=v*v;}
                if(e<best){best=e;idx[i]=unsigned(p);}
            }
        }
    };
    for(int pass=0;pass<2;++pass){
        float q0[3],q1[3];unpack(pack(e0),q0);unpack(pack(e1),q1);
        assign(q0,q1);
        // Least squares for texel = w*e0 + (1-w)*e1 over the solid texels.
        float aa=0,ab=0,bb=0,ax[3]={0,0,0},bx[3]={0,0,0};
        for(int i=0;i<16;++i)if(!clear[i]){
            const float a=weight[idx[i]],b=1-a;
            aa+=a*a;ab+=a*b;bb+=b*b;for(int k=0;k<3;++k){ax[k]+=a*c[i][k];bx[k]+=b*c[i][k];}
        }
        const float det=aa*bb-ab*ab;
        if(std::fabs(det)<1e-6f)break;
        for(int k=0;k<3;++k){e0[k]=(ax[k]*bb-bx[k]*ab)/det;e1[k]=(bx[k]*aa-ax[k]*ab)/det;}
    }
    std::uint16_t p0=pack(e0),p1=pack(e1);
    // Four colours need c0 > c1, three need c0 <= c1 (the indices are chosen
    // again for the ordered endpoints).
    if(colours==4?p0<p1:p0>p1)std::swap(p0,p1);
    out[0]=std::uint8_t(p0);out[1]=std::uint8_t(p0>>8);out[2]=std::uint8_t(p1);out[3]=std::uint8_t(p1>>8);
    if(colours==4&&p0==p1){out[4]=out[5]=out[6]=out[7]=0;return;} // a flat block: every index 0
    float q0[3],q1[3];unpack(p0,q0);unpack(p1,q1);
    assign(q0,q1);
    std::uint32_t bits=0;for(int i=0;i<16;++i)bits|=idx[i]<<(2*i);
    out[4]=std::uint8_t(bits);out[5]=std::uint8_t(bits>>8);out[6]=std::uint8_t(bits>>16);out[7]=std::uint8_t(bits>>24);
}
// The encode error of a level: squared channel differences summed, their
// count, and the largest single difference (0-255 a channel).
struct Bc1EncodeError{double squares=0;std::size_t samples=0;unsigned worst=0;};
// One BGRA8 level (w x h) as BC1 blocks (edge blocks repeat their last
// texel). error: each block decoded again and compared with the 16 texels
// it encodes (no whole-level copy).
inline void encode_bc1_level(const std::uint32_t* pixels,unsigned w,unsigned h,std::uint8_t* out,Bc1EncodeError* error=nullptr) noexcept {
    const unsigned bw=std::max(1u,(w+3)/4),bh=std::max(1u,(h+3)/4);
    std::uint32_t block[16],check[16];
    for(unsigned by=0;by<bh;++by)for(unsigned bx=0;bx<bw;++bx){
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)
            block[y*4+x]=pixels[std::size_t(std::min(by*4+y,h-1))*w+std::min(bx*4+x,w-1)];
        std::uint8_t* const encoded=out+(std::size_t(by)*bw+bx)*8;
        encode_bc1_block(block,encoded);
        if(!error)continue;
        decode_dxt_level(encoded,4,4,1,check);
        for(unsigned i=0;i<16;++i)for(unsigned s=0;s<24;s+=8){
            const int d=int((check[i]>>s)&255u)-int((block[i]>>s)&255u);
            error->squares+=double(d)*d;++error->samples;error->worst=std::max(error->worst,unsigned(std::abs(d)));
        }
    }
}
// One BGRA8 level at twice the size (bilinear, texel centres aligned, edges clamped).
inline void upsample_level(const std::uint32_t* in,unsigned w,unsigned h,std::uint32_t* out) noexcept {
    const unsigned ow=w*2,oh=h*2;
    for(unsigned y=0;y<oh;++y){
        const float sy=std::clamp((float(y)+.5f)*.5f-.5f,0.f,float(h-1));
        const unsigned y0=unsigned(sy),y1=std::min(y0+1,h-1);const float fy=sy-float(y0);
        for(unsigned x=0;x<ow;++x){
            const float sx=std::clamp((float(x)+.5f)*.5f-.5f,0.f,float(w-1));
            const unsigned x0=unsigned(sx),x1=std::min(x0+1,w-1);const float fx=sx-float(x0);
            const std::uint32_t a=in[std::size_t(y0)*w+x0],b=in[std::size_t(y0)*w+x1],c=in[std::size_t(y1)*w+x0],d=in[std::size_t(y1)*w+x1];
            std::uint32_t v=0;
            for(unsigned s=0;s<32;s+=8){
                const float t=float((a>>s)&255u)*(1-fx)*(1-fy)+float((b>>s)&255u)*fx*(1-fy)+
                              float((c>>s)&255u)*(1-fx)*fy+float((d>>s)&255u)*fx*fy;
                v|=std::min(255u,unsigned(t+.5f))<<s;
            }
            out[std::size_t(y)*ow+x]=v;
        }
    }
}
// A one-level BC1 page as a full chain whose level 0 is at least `side`
// texels square (the terrain 2.0 pool layout, contact_blend_atlas_side): a
// smaller page upsampled (bilinear) up to the side, then the page's own
// level byte for byte, then each 2x2 box of the decoded level above, every
// generated level encoded with encode_bc1_block. rms and worst: the encoded
// generated levels against the colours they encode, block by block (0-255
// a channel; the root mean square and the largest difference). peak: the
// most bytes its working buffers held at once (the output, the decoded page
// and at most two working levels: one level and the one made from it).
struct Bc1ChainLevel{unsigned width=0,height=0;std::size_t offset=0,bytes=0;};
inline std::vector<std::uint8_t> normalised_bc1_chain(const std::uint8_t* level0,unsigned width,unsigned height,unsigned side,
    std::vector<Bc1ChainLevel>& levels,double& rms,unsigned& worst,std::size_t& peak){
    levels.clear();rms=0;worst=0;peak=0;
    const auto block_bytes=[](unsigned w,unsigned h){return std::size_t(std::max(1u,(w+3)/4))*std::max(1u,(h+3)/4)*8u;};
    // The layout first: the upsampled levels (largest first), the page's own
    // level, then its boxes down to 1x1; the output is sized once.
    unsigned ups=0;while((width<<ups)<side||(height<<ups)<side)++ups;
    std::size_t total=0;
    for(unsigned k=ups;;--k){
        const unsigned lw=width<<k,lh=height<<k;
        levels.push_back({lw,lh,total,block_bytes(lw,lh)});total+=levels.back().bytes;
        if(!k)break;
    }
    for(unsigned w=width,h=height;w>1||h>1;){
        w=std::max(1u,w/2);h=std::max(1u,h/2);
        levels.push_back({w,h,total,block_bytes(w,h)});total+=levels.back().bytes;
    }
    std::vector<std::uint8_t> bytes(total);
    std::vector<std::uint32_t> decoded(std::size_t(width)*height);
    decode_dxt_level(level0,width,height,1,decoded.data());
    Bc1EncodeError error;
    const auto sample=[&](std::size_t working){peak=std::max(peak,bytes.capacity()+decoded.capacity()*4+working);};
    // Upsampled levels, smallest first, each from the one below it (slot ups-k).
    {
        std::vector<std::uint32_t> below,up;unsigned w=width,h=height;
        for(unsigned k=1;k<=ups;++k){
            up.assign(std::size_t(w)*2*h*2,0u);
            upsample_level(below.empty()?decoded.data():below.data(),w,h,up.data());
            w*=2;h*=2;
            sample((below.capacity()+up.capacity())*4);
            const auto& slot=levels[ups-k];
            encode_bc1_level(up.data(),w,h,bytes.data()+slot.offset,&error);
            below.swap(up);
        }
    }
    std::memcpy(bytes.data()+levels[ups].offset,level0,levels[ups].bytes); // the page's own level
    // Boxes of the decoded page, each from the one above it.
    {
        std::vector<std::uint32_t> above=std::move(decoded),next;unsigned w=width,h=height;
        for(std::size_t slot=ups+1;slot<levels.size();++slot){
            const unsigned nw=std::max(1u,w/2),nh=std::max(1u,h/2);
            next.assign(std::size_t(nw)*nh,0u);box_mip_level(above.data(),w,h,next.data());
            sample((above.capacity()+next.capacity())*4);
            encode_bc1_level(next.data(),nw,nh,bytes.data()+levels[slot].offset,&error);
            above.swap(next);w=nw;h=nh;
        }
    }
    worst=error.worst;rms=error.samples?std::sqrt(error.squares/double(error.samples)):0;
    return bytes;
}
inline std::uint64_t uncompressed_mip_bytes(unsigned width,unsigned height,unsigned bytes) noexcept {
    std::uint64_t total=0;for(;;){total+=std::uint64_t(width)*height*bytes;if(width==1&&height==1)return total;width=std::max(1u,width/2);height=std::max(1u,height/2);}
}
}
