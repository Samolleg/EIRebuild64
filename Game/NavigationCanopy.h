#pragma once
#include "../Navigation/Types.h"
#include "../external/owned-renderer/Source/owned_asset_loader.h"
#include <algorithm>
#include <cfloat>
#include <stdexcept>

// Offline coverage of the selected texture. Zero-alpha leaf-card pixels are
// empty; every nonzero alpha remains an obstacle, including filtered edges.
struct NavigationCanopy {
    struct Vertex { Navigation::Point p; float u=0,v=0; };
    using Polygon=std::vector<Vertex>;
    int width=0,height=0;
    bool opaque=false;
    std::vector<std::uint32_t> prefix;

    explicit NavigationCanopy(const owned::TextureCpuAsset& texture) {
        owned::TextureAlphaPixels alpha;
        if(!owned::texture_alpha_pixels(texture,0,alpha)||!alpha.width||!alpha.height)
            throw std::runtime_error("Nav64: missing canopy texture alpha");
        width=int(alpha.width);height=int(alpha.height);
        std::vector<unsigned char> visible(size_t(width)*height);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x)
            visible[size_t(y)*width+x]=!owned::texture_alpha_texel_empty(alpha,x,y);
        prefix.assign(size_t(width+1)*(height+1),0);
        for(int y=0;y<height;++y) {
            unsigned row=0;
            for(int x=0;x<width;++x) {
                bool filled=false;
                // One texel of support includes bilinear filtering at the
                // cutout boundary. Texture addressing is the renderer's wrap.
                for(int dy=-1;dy<=1&&!filled;++dy)for(int dx=-1;dx<=1;++dx)
                    if(visible[size_t((y+dy+height)%height)*width+(x+dx+width)%width]){filled=true;break;}
                row+=filled;
                prefix[size_t(y+1)*(width+1)+x+1]=prefix[size_t(y)*(width+1)+x+1]+row;
            }
        }
        const auto total=prefix.back();
        opaque=total==size_t(width)*height;
        if(!total||opaque)prefix.clear();
    }

    static Polygon Clip(const Polygon& input,int axis,float edge,bool greater) {
        Polygon out;if(input.empty())return out;
        auto coordinate=[&](const Vertex& p){return axis==0?p.p.x:axis==1?p.p.y:axis==2?p.u:p.v;};
        auto previous=input.back();float pv=coordinate(previous)-edge;
        bool inside=greater?pv>=0:pv<=0;
        for(const auto& current:input) {
            const float cv=coordinate(current)-edge;const bool next=greater?cv>=0:cv<=0;
            if(inside!=next) {
                const float f=pv/(pv-cv);
                out.push_back({{previous.p.x+(current.p.x-previous.p.x)*f,
                    previous.p.y+(current.p.y-previous.p.y)*f,previous.p.z+(current.p.z-previous.p.z)*f},
                    previous.u+(current.u-previous.u)*f,previous.v+(current.v-previous.v)*f});
            }
            if(next)out.push_back(current);
            previous=current;pv=cv;inside=next;
        }
        return out;
    }

    float Minimum(const Polygon& polygon,float best=FLT_MAX) const {
        if(prefix.empty()&&!opaque)return best;
        float u0=FLT_MAX,v0=FLT_MAX,u1=-FLT_MAX,v1=-FLT_MAX;
        for(const auto& p:polygon) {
            u0=(std::min)(u0,p.u);v0=(std::min)(v0,p.v);u1=(std::max)(u1,p.u);v1=(std::max)(v1,p.v);
        }
        if(polygon.size()<3)return best;
        if(!std::isfinite(u0)||!std::isfinite(u1)||!std::isfinite(v0)||!std::isfinite(v1)||
            (std::max)(std::abs(u0),std::abs(u1))*width>1.e8f||(std::max)(std::abs(v0),std::abs(v1))*height>1.e8f)
            throw std::runtime_error("Nav64: canopy UV outside supported range");
        const int x0=int(std::floor(u0*width)),y0=int(std::floor(v0*height));
        const int x1=(std::max)(x0+1,int(std::ceil(u1*width))),y1=(std::max)(y0+1,int(std::ceil(v1*height)));
        unsigned visits=0;Scan(polygon,x0,y0,x1,y1,best,visits);return best;
    }

private:
    std::int64_t Integral(int x,int y) const {
        const auto qx=int(std::floor(double(x)/width)),qy=int(std::floor(double(y)/height));
        const int rx=x-qx*width,ry=y-qy*height;
        return std::int64_t(qx)*qy*prefix.back()+std::int64_t(qx)*prefix[size_t(ry)*(width+1)+width]+
            std::int64_t(qy)*prefix[size_t(height)*(width+1)+rx]+prefix[size_t(ry)*(width+1)+rx];
    }
    std::int64_t Count(int x0,int y0,int x1,int y1) const {
        if(prefix.empty())return opaque?std::int64_t(x1-x0)*(y1-y0):0;
        return Integral(x1,y1)-Integral(x0,y1)-Integral(x1,y0)+Integral(x0,y0);
    }
    void Scan(const Polygon& polygon,int x0,int y0,int x1,int y1,float& best,unsigned& visits) const {
        if(polygon.size()<3)return;
        double area=0;float low=FLT_MAX;const auto& origin=polygon.front().p;
        for(size_t i=0;i<polygon.size();++i) {
            const auto& a=polygon[i].p;const auto& b=polygon[(i+1)%polygon.size()].p;
            area+=double(a.x-origin.x)*(b.y-origin.y)-double(a.y-origin.y)*(b.x-origin.x);
            low=(std::min)(low,a.z);
        }
        if(std::abs(area)<1.e-10||low>=best)return;
        if(++visits>1000000)throw std::runtime_error("Nav64: canopy alpha subdivision limit");
        const auto count=Count(x0,y0,x1,y1);if(!count)return;
        if(count==std::int64_t(x1-x0)*(y1-y0)){best=low;return;}
        const bool horizontal=x1-x0>=y1-y0;const int axis=horizontal?2:3;
        const int middle=horizontal?x0+(x1-x0)/2:y0+(y1-y0)/2;
        const float edge=float(middle)/(horizontal?width:height);
        auto lower=Clip(polygon,axis,edge,false),upper=Clip(polygon,axis,edge,true);
        if(horizontal){Scan(lower,x0,y0,middle,y1,best,visits);Scan(upper,middle,y0,x1,y1,best,visits);}
        else {Scan(lower,x0,y0,x1,middle,best,visits);Scan(upper,x0,middle,x1,y1,best,visits);}
    }
};
