#include "WorldBackgroundPixels.h"
#include <algorithm>
#include <stdexcept>

namespace WorldBackground {
namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint32_t Read(const Blob& b,size_t p,unsigned count=4){
    Need(p<=b.size()&&count<=b.size()-p,"Truncated MMP pixels");
    uint32_t value=0;for(unsigned i=0;i<count;++i)value|=uint32_t(b[p+i])<<(8*i);return value;
}
uint32_t Straight(uint32_t color){
    const uint32_t a=color>>24;if(!a)return 0;
    uint32_t result=a<<24;for(unsigned shift:{0u,8u,16u})result|=std::min(255u,(((color>>shift)&255)*255+a/2)/a)<<shift;
    return result;
}
uint32_t Rgb565(uint32_t v){return 0xff000000u|(((v>>11)*255/31)<<16)|((((v>>5)&63)*255/63)<<8)|((v&31)*255/31);}
uint32_t Mix(uint32_t a,uint32_t b,unsigned wa,unsigned wb,unsigned d){
    uint32_t result=0xff000000u;for(unsigned s:{0u,8u,16u})result|=((((a>>s)&255)*wa+((b>>s)&255)*wb)/d)<<s;return result;
}
}
Pixels DecodePixels(const Blob& b,size_t budget){
    Need(b.size()>=76&&Read(b,0)==0x00504d4d,"Invalid MMP header");
    Pixels image;image.width=Read(b,4);image.height=Read(b,8);
    Need(image.width&&image.height&&image.width<=4096&&image.height<=4096,"Invalid MMP dimensions");
    const uint64_t count=uint64_t(image.width)*image.height;
    Need(count*4<=budget,"Decoded MMP exceeds byte budget");
    const uint32_t format=Read(b,16),levels=Read(b,12);
    const bool dxt=format>=0x31545844&&format<=0x35545844&&((format-0x31545844)%0x1000000)==0;
    // Validate before allocating. A corrupt large header cannot allocate an image.
    if(dxt){
        Need(levels&&levels<=13,"Invalid DXT mip count");
        uint32_t w=image.width,h=image.height;uint64_t length=76;
        for(uint32_t level=0;level<levels;++level){length+=uint64_t((w+3)/4)*((h+3)/4)*(format==0x31545844?8:16);if(level+1<levels)Need(w>1||h>1,"Excess DXT mip levels");w=std::max(1u,w/2);h=std::max(1u,h/2);}
        Need(length<=b.size(),"Truncated DXT mip chain");
    }else if(format==0x33544e50){Need(levels<=b.size()-76,"Truncated PNT3 payload");}
    else {
        Need(format==0x5650||format==0x5551||format==0x4444||format==0x8888,"Unsupported MMP pixel format");
        const uint32_t bits=Read(b,20);Need(bits==16||bits==32,"Invalid MMP pixel depth");
        Need(count*(bits/8)<=b.size()-76,"Truncated raw MMP pixels");
        uint32_t used=0;
        for(size_t at:{size_t(24),size_t(36),size_t(48),size_t(60)}){
            const uint32_t mask=Read(b,at),shift=Read(b,at+4);
            Need(shift<bits,"Invalid MMP channel shift");
            if(mask){const uint32_t maximum=mask>>shift;
                Need(maximum&&!(maximum&(maximum+1))&&((uint64_t(maximum)<<shift)==mask)&&
                    (bits==32||(mask>>16)==0)&&!(used&mask),"Invalid MMP channel mask");used|=mask;
            }
        }
    }
    image.argb.resize(size_t(count));
    if(dxt){
        const unsigned kind=(format>>24)-'0',blockBytes=kind==1?8:16;
        size_t block=76;
        for(uint32_t by=0;by<image.height;by+=4)for(uint32_t bx=0;bx<image.width;bx+=4,block+=blockBytes){
            const size_t colorsAt=block+(kind==1?0:8);const uint32_t c0=Read(b,colorsAt,2),c1=Read(b,colorsAt+2,2);
            uint32_t colors[4]={Rgb565(c0),Rgb565(c1),0,0};
            if(kind!=1||c0>c1){colors[2]=Mix(colors[0],colors[1],2,1,3);colors[3]=Mix(colors[0],colors[1],1,2,3);}
            else colors[2]=Mix(colors[0],colors[1],1,1,2);
            const uint32_t indices=Read(b,colorsAt+4);uint64_t alphaBits=0;uint32_t alpha[8]={};
            if(kind==2||kind==3){alphaBits=uint64_t(Read(b,block))|(uint64_t(Read(b,block+4))<<32);}
            if(kind==4||kind==5){
                alpha[0]=b[block];alpha[1]=b[block+1];for(unsigned i=0;i<6;++i)alphaBits|=uint64_t(b[block+2+i])<<(i*8);
                if(alpha[0]>alpha[1])for(unsigned i=1;i<=6;++i)alpha[i+1]=((7-i)*alpha[0]+i*alpha[1])/7;
                else{for(unsigned i=1;i<=4;++i)alpha[i+1]=((5-i)*alpha[0]+i*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
            }
            for(unsigned i=0;i<16;++i){
                const uint32_t x=bx+i%4,y=by+i/4;if(x>=image.width||y>=image.height)continue;
                uint32_t color=colors[(indices>>(2*i))&3];
                if(kind==2||kind==3)color=(color&0xffffffu)|(uint32_t((alphaBits>>(4*i))&15)*17<<24);
                if(kind==4||kind==5)color=(color&0xffffffu)|(alpha[(alphaBits>>(3*i))&7]<<24);
                image.argb[size_t(y)*image.width+x]=(kind==2||kind==4)?Straight(color):color;
            }
        }
    }else if(format==0x33544e50){
        size_t out=0,p=76,end=76+size_t(levels);
        while(p<end){Need(end-p>=4,"Truncated PNT3 marker");const uint32_t word=Read(b,p);
            if(word&&(word&0xff000000u)==0){Need(!(word&15)&&word/4<=image.argb.size()-out,"Invalid PNT3 transparent run");out+=word/4;p+=4;}
            else {Need(end-p>=16&&4<=image.argb.size()-out,"Truncated or excessive PNT3 literal block");for(unsigned i=0;i<4;++i)image.argb[out++]=Straight(Read(b,p+i*4));p+=16;}
        }
        // The native compositor permits an implicit trailing transparent region.
    }else{
        const unsigned bytes=Read(b,20)/8;
        for(size_t i=0;i<image.argb.size();++i){const uint32_t pixel=Read(b,76+i*bytes,bytes);uint32_t result=0;
            for(unsigned channel=0;channel<4;++channel){const size_t at=24+channel*12;const uint32_t mask=Read(b,at),shift=Read(b,at+4),maximum=mask>>shift;
                const uint32_t value=mask?uint32_t((uint64_t((pixel&mask)>>shift)*255+maximum/2)/maximum):255;
                result|=value<<(24-channel*8);
            }image.argb[i]=result;
        }
    }
    return image;
}
}
