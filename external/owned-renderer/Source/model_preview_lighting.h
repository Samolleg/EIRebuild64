#pragma once
#include <array>
#include <cmath>

namespace owned {
enum class PreviewStudioStyle { portrait, soft_figure };
struct PreviewStudioLighting {
    // Native lighting uses max(ambient, directional), not an additive fill.
    std::array<float,4> ambient{.30f,.30f,.30f,1.f};
    std::array<float,4> diffuse{1.f,.98f,.95f,1.f};
    std::array<float,4> direction{};
    std::array<std::array<float,3>,2> fill_centers{};
};

// One existing directional light, in the preview camera's frame. No scene
// lighting inputs, extra GPU work, or model-specific policy. The complete clip
// includes the pane's projection/HUD placement; matrices use row-major order.
inline bool make_preview_studio_lighting(const std::array<float,16>& clip,
    const std::array<float,3>& center, PreviewStudioLighting& result,
    PreviewStudioStyle style = PreviewStudioStyle::portrait) noexcept {
    using Vec = std::array<double,3>;
    const auto dot=[](const Vec& a,const Vec& b) {
        return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    };
    const auto cross=[](const Vec& a,const Vec& b) {
        return Vec{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    };
    const auto normalize=[&](Vec& v) {
        const double square=dot(v,v);
        if(!std::isfinite(square) || square<=0) return false;
        const double scale=1/std::sqrt(square);
        for(auto& c:v)c*=scale;
        return true;
    };
    for(float v:clip)if(!std::isfinite(v))return false;
    for(float v:center)if(!std::isfinite(v))return false;
    const Vec x{clip[0],clip[1],clip[2]},y{clip[4],clip[5],clip[6]},
        w{clip[12],clip[13],clip[14]},depth{clip[8],clip[9],clip[10]},
        point{center[0],center[1],center[2]};
    const auto yw=cross(y,w),wx=cross(w,x),xy=cross(x,y);
    const double determinant=dot(x,yw);
    Vec front{};
    if(dot(w,w)>0) {
        // Perspective: solve the eye position from the x/y/w clip planes.
        const double scale=std::sqrt(dot(x,x)*dot(y,y)*dot(w,w));
        if(scale<=0 || std::abs(determinant)<=scale*1e-12) return false;
        for(unsigned a=0;a<3;++a)
            front[a]=(-clip[3]*yw[a]-clip[7]*wx[a]-clip[15]*xy[a])/determinant-point[a];
    } else {
        // Orthographic: the eye is at infinity along the backwards view ray.
        const double facing=dot(xy,depth);
        if(facing==0) return false;
        for(unsigned a=0;a<3;++a)front[a]=-xy[a]*(facing<0 ? -1. : 1.);
    }
    if(!normalize(front)) return false;
    const double clipX=dot(x,point)+clip[3],clipY=dot(y,point)+clip[7],
        clipW=dot(w,point)+clip[15];
    if(clipW==0) return false;
    // Screen gradients cancel HUD translation and preserve the signs of
    // screen right/up even for a rolled camera or a mirrored view.
    Vec right{},up{};
    for(unsigned a=0;a<3;++a) {
        right[a]=x[a]*clipW-w[a]*clipX;
        up[a]=y[a]*clipW-w[a]*clipY;
    }
    const double rightAlong=dot(right,front);
    for(unsigned a=0;a<3;++a)right[a]-=rightAlong*front[a];
    if(!normalize(right)) return false;
    const double upAlong=dot(up,front),upRight=dot(up,right);
    for(unsigned a=0;a<3;++a)up[a]-=upAlong*front[a]+upRight*right[a];
    if(!normalize(up)) return false;
    // Soft figures: 22 degrees up, 24 degrees left. Standalone heads retain 35/30.
    const bool soft = style == PreviewStudioStyle::soft_figure;
    const double left = soft ? .377119649 : .4095760443;
    const double above = soft ? .374606593 : .5735764364;
    const double towardEye = soft ? .847024599 : .7094064799;
    Vec key{};
    for(unsigned a=0;a<3;++a)key[a]=towardEye*front[a]-left*right[a]+above*up[a];
    if(!normalize(key)) return false;
    PreviewStudioLighting value;
    for(unsigned a=0;a<3;++a)value.direction[a]=static_cast<float>(key[a]);
    if (soft) {
        value.ambient = {.56f,.56f,.56f,1.f};
        value.diffuse = {.84f,.825f,.81f,1.f};
        value.direction[3] = 1.f; // Host binds the studio's own lights for this submission.
        // Broad neutral fills to either side. Both lights are far relative to a figure,
        // so the model can turn and animate without a small bright spot on the face.
        constexpr double distance = 12.;
        for (unsigned a=0; a<3; ++a) {
            value.fill_centers[0][a] = float(point[a]+distance*(-.78*right[a]+.42*up[a]+.46*front[a]));
            value.fill_centers[1][a] = float(point[a]+distance*( .78*right[a]-.18*up[a]+.60*front[a]));
        }
    }
    result=value;
    return true;
}
} // namespace owned
