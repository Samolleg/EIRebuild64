#pragma once
#include "CampItemFit.h"
#include <cfloat>

namespace CampItemFit {
// Wearables are authored in the standing character's rest pose. In an
// inventory FIG each boot or bracer is posed independently for the cell.
struct Box {
    Point low{FLT_MAX,FLT_MAX,FLT_MAX},high{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    void add(const Point& p) {
        for(unsigned k=0;k<3;++k) {low[k]=(std::min)(low[k],p[k]);high[k]=(std::max)(high[k],p[k]);}
    }
    bool valid() const {return low[0]<=high[0];}
    Point middle() const {return {(low[0]+high[0])*.5f,(low[1]+high[1])*.5f,(low[2]+high[2])*.5f};}
    Point size() const {return delta(high,low);}
};
inline Basis yaw(float angle) {
    const float c=std::cos(angle),s=std::sin(angle);
    return {Point{c,0,s},Point{0,1,0},Point{-s,0,c}};
}
inline Basis roll(float angle) {
    const float c=std::cos(angle),s=std::sin(angle);
    return {Point{c,-s,0},Point{s,c,0},Point{0,0,1}};
}
inline Basis compose(const Basis& a,const Basis& b) {
    Basis result{};
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c)
        for(unsigned k=0;k<3;++k) result[r][c]+=a[r][k]*b[k][c];
    return result;
}
struct ClothingPiece { Box box,top,bottom; };
struct ClothingLayout {
    ClothingPiece piece[2];
    bool paired=false;
    float split=0;
    Basis rotation[2]{identity(),identity()};
    unsigned side(const Point& p) const { return paired && p[0]>split?1:0; }

    static ClothingLayout measure(const std::vector<Triangle>& tris,bool pair,bool wornGloves) {
        ClothingLayout layout;
        Box all;
        for(const auto& t:tris) {all.add(t.a);all.add(t.b);all.add(t.c);}
        if(!all.valid()) return layout;
        layout.split=all.middle()[0];
        // A continuous mesh straddling the middle must remain a single piece.
        layout.paired=pair && std::none_of(tris.begin(),tris.end(),[&](const Triangle& t) {
            bool right=t.a[0]>layout.split;
            return right!=(t.b[0]>layout.split) || right!=(t.c[0]>layout.split);
        });
        if(layout.paired && wornGloves) {
            layout.rotation[0]=roll(1.5707963268f);
            layout.rotation[1]=roll(-1.5707963268f);
        }
        for(const auto& t:tris) for(const auto& p:{t.a,t.b,t.c}) {
            const auto i=layout.side(p);layout.piece[i].box.add(rotate(layout.rotation[i],p));
        }
        for(const auto& t:tris) for(const auto& p:{t.a,t.b,t.c}) {
            const auto i=layout.side(p);auto& piece=layout.piece[i];
            const Point v=rotate(layout.rotation[i],p);
            const float band=piece.box.size()[1]*.23f;
            if(v[1]<=piece.box.low[1]+band) piece.bottom.add(v);
            if(v[1]>=piece.box.high[1]-band) piece.top.add(v);
        }
        return layout;
    }
};
struct ClothingPose {
    Placement piece[2];
    float split=0;
    bool paired=false;

    ClothingPose(const std::vector<Triangle>& triangles,const ClothingLayout& ref,bool gloves,bool boots) {
        auto source=ClothingLayout::measure(triangles,ref.paired,gloves);
        paired=source.paired && ref.paired; split=source.split;
        // When a mod supplies a one-piece variant of a paired reference, fit
        // its intact mesh within the union of both original pieces.
        Box targetUnion=ref.piece[0].box;
        if(ref.paired){targetUnion.add(ref.piece[1].box.low);targetUnion.add(ref.piece[1].box.high);}
        Box turned[2];
        for(unsigned i=0;i<(paired?2u:1u);++i) {
            auto basis=source.rotation[i];
            if(paired) {
                const auto& a=source.piece[i];const auto& b=ref.piece[i];
                const auto from=delta(a.bottom.middle(),a.top.middle());
                const auto to=delta(b.bottom.middle(),b.top.middle());
                // Feet point toward the toe, cuffs taper from elbow to wrist.
                if(boots)
                    basis=compose(yaw(std::atan2(to[0],to[2])-std::atan2(from[0],from[2])),basis);
                if(gloves)
                    basis=compose(roll(std::atan2(to[0],-to[1])-std::atan2(from[0],-from[1])),basis);
            }
            piece[i].basis=basis;
        }
        for(const auto& t:triangles) for(const auto& p:{t.a,t.b,t.c}) {
            const auto i=side(p);turned[i].add(rotate(piece[i].basis,p));
        }
        for(unsigned i=0;i<(paired?2u:1u);++i) {
            auto& pose=piece[i];
            const auto& box=paired?ref.piece[i].box:targetUnion;
            const Point a=turned[i].size(),b=box.size();
            pose.scale=(std::min)(b[0]/(std::max)(a[0],1.e-6f),b[1]/(std::max)(a[1],1.e-6f));
            // The center is in the rotated coordinate frame below.
            pose.sourceCenter=turned[i].middle();pose.targetCenter=box.middle();
        }
    }
    unsigned side(const Point& p) const {return paired && p[0]>split?1:0;}
    void apply(Point& pos,Point& normal) const {
        const auto& pose=piece[side(pos)];
        pos=rotate(pose.basis,pos);normal=rotate(pose.basis,normal);
        for(unsigned k=0;k<3;++k)pos[k]=(pos[k]-pose.sourceCenter[k])*pose.scale+pose.targetCenter[k];
    }
};
}
