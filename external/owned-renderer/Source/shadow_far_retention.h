#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace owned {

// Far sun maps (slices 3 and 4) keep the light projection they were drawn
// with while it still serves the current frame. `Slice` is NativeShadowSlice:
// world_to_light rows are light axes scaled by 1/radius (XY) and 1/depth (Z),
// receiver bounds are coordinates along the unscaled light axes, bias[0] is
// the texel size.

namespace far_retention_detail {
template<class Slice>
bool unit_axes(const Slice& slice,std::array<std::array<double,3>,3>& axes,
    std::array<double,3>& lengths) noexcept {
    for(unsigned axis=0;axis<3;++axis){
        double squared=0;
        for(unsigned component=0;component<3;++component)
            squared+=double(slice.world_to_light[axis*4+component])*slice.world_to_light[axis*4+component];
        if(!(squared>0))return false;
        lengths[axis]=std::sqrt(squared);
        for(unsigned component=0;component<3;++component)
            axes[axis][component]=slice.world_to_light[axis*4+component]/lengths[axis];
    }
    return true;
}
template<class Slice>
double project(const Slice& slice,unsigned axis,const std::array<double,3>& world) noexcept {
    double value=slice.world_to_light[axis*4+3];
    for(unsigned component=0;component<3;++component)
        value+=double(slice.world_to_light[axis*4+component])*world[component];
    return value;
}
}

// The retained map still covers every current receiver: the current receiver
// volume (in the new sun basis) reprojects inside the retained square, and the
// sun has turned less than the slice's angle budget.
template<class Slice>
bool far_shadow_projection_covers(const Slice& old,const Slice& next,unsigned index) noexcept {
    if(index==4 && (old.bias[2]>.5f)!=(next.bias[2]>.5f))return false;
    std::array<std::array<double,3>,3> old_axes{},new_axes{};
    std::array<double,3> old_lengths{},new_lengths{};
    if(!far_retention_detail::unit_axes(old,old_axes,old_lengths) ||
       !far_retention_detail::unit_axes(next,new_axes,new_lengths))return false;
    // A distant map may hold a small shadow-only sun step, but large time
    // jumps must refresh immediately. C3 has the stricter angle budget.
    constexpr double cos_quarter_degree=0.9999904807207345;
    constexpr double cos_half_degree=0.9999619230641713;
    const double minimum_dot=index==3?cos_quarter_degree:cos_half_degree;
    for(unsigned axis=0;axis<3;++axis){
        double dot=0;
        for(unsigned component=0;component<3;++component)
            dot+=old_axes[axis][component]*new_axes[axis][component];
        if(dot<minimum_dot)return false;
    }
    const double guard=8.0*old.bias[0]*old_lengths[0];
    if(!(guard>=0 && guard<1))return false;
    for(unsigned corner=0;corner<8;++corner){
        std::array<double,3> world{};
        for(unsigned axis=0;axis<3;++axis){
            const double coordinate=(corner&(1u<<axis))?
                next.receiver_maximum[axis]:next.receiver_minimum[axis];
            for(unsigned component=0;component<3;++component)
                world[component]+=new_axes[axis][component]*coordinate;
        }
        for(unsigned axis=0;axis<3;++axis){
            const double projected=far_retention_detail::project(old,axis,world);
            if(axis<2 ? std::abs(projected)>1-guard : projected<0 || projected>1)return false;
        }
    }
    return true;
}

// Sun maps follow the camera, so a pan moves their texel-snapped square every
// frame (close maps every frame, far maps whenever coverage is lost). The
// cached static layer is then scrolled instead of redrawn: with the sun axes
// and texel size unchanged the new square differs from the drawn one by whole
// texels, so its static depth is the old layer shifted by (dx, dy) texels plus
// the static casters of the newly exposed strip. Depth keeps the drawn
// projection's z row (far_shadow_projection_retainable must hold) so copied
// values stay exact. Texel u grows with light x and texel v with -light y.
struct CloseShadowScroll {
    int dx = 0, dy = 0;
    // The old square in the new map's light NDC (x0, x1, y0, y1).
    std::array<double,4> copied{};
};
enum class ShadowScrollRefusal : unsigned { none, axes, texel, fraction };
template<class Slice>
ShadowScrollRefusal close_shadow_scroll(const Slice& old,const Slice& next,unsigned side,
    CloseShadowScroll& scroll) noexcept {
    if(old.bias[0]!=next.bias[0] || side<16)return ShadowScrollRefusal::texel;
    for(unsigned row=0;row<2;++row)
        for(unsigned component=0;component<3;++component)
            if(old.world_to_light[row*4+component]!=next.world_to_light[row*4+component])
                return ShadowScrollRefusal::axes;
    const double shift_x=double(next.world_to_light[3])-old.world_to_light[3];
    const double shift_y=double(next.world_to_light[7])-old.world_to_light[7];
    const double texels_x=shift_x*side*.5,texels_y=-shift_y*side*.5;
    const double whole_x=std::round(texels_x),whole_y=std::round(texels_y);
    if(std::abs(texels_x-whole_x)>1e-2 || std::abs(texels_y-whole_y)>1e-2 ||
       std::abs(whole_x)>=side || std::abs(whole_y)>=side)return ShadowScrollRefusal::fraction;
    scroll.dx=static_cast<int>(whole_x);scroll.dy=static_cast<int>(whole_y);
    scroll.copied={(std::max)(-1.0,-1+shift_x),(std::min)(1.0,1+shift_x),
        (std::max)(-1.0,-1+shift_y),(std::min)(1.0,1+shift_y)};
    return ShadowScrollRefusal::none;
}

// Whether a caster with this light-NDC footprint must be drawn into a
// scrolled map: it reaches the new square (widened by two texels) and is not
// wholly inside the copied old square (inset by two texels). Drawing a whole
// caster again over copied depth is harmless (min depth).
inline bool shadow_scroll_strip_reached(const double low[2],const double high[2],
    const std::array<double,4>& copied,unsigned side) noexcept {
    const double inset=4.0/side;
    if(high[0]< -1-inset||low[0]>1+inset||high[1]< -1-inset||low[1]>1+inset)return false;
    return !(low[0]>=copied[0]+inset&&high[0]<=copied[1]-inset&&
             low[1]>=copied[2]+inset&&high[1]<=copied[3]-inset);
}
// The same test from bounds along the light axes (RetainedShadowCasterValue
// light_bounds): rows 0 and 1 of world_to_light are those axes over radius.
template<class Bounds>
bool shadow_scroll_strip_reached_light(const Bounds& light,const std::array<float,16>& world_to_light,
    const std::array<double,4>& copied,unsigned side) noexcept {
    if(!light.valid)return true;
    double low[2],high[2];
    for(unsigned axis=0;axis<2;++axis){
        double scale=0;
        for(unsigned component=0;component<3;++component)
            scale+=double(world_to_light[axis*4+component])*world_to_light[axis*4+component];
        scale=std::sqrt(scale);
        low[axis]=light.minimum[axis]*scale+world_to_light[axis*4+3];
        high[axis]=light.maximum[axis]*scale+world_to_light[axis*4+3];
    }
    return shadow_scroll_strip_reached(low,high,copied,side);
}

// The cached static layer may scroll only if its content is unchanged where
// it is kept: the persistent-static summary entries are identical and every
// terrain part (sector, material, subparts) present in both lists has
// identical entries, so a changed LOD variant forces a redraw. Parts only in
// the old list left the map's membership (their copied depth stays correct);
// parts only in the new list are returned in `entered` (key: sector << 32 |
// part, ascending) and are drawn whole, because a far map's membership
// follows its receivers and can admit a part inside the copied square.
// Terrain entries are sorted by sector and part (caster order).
// `summary_changed` tells a changed persistent-static summary (store content)
// from a changed terrain part.
template<class Revision>
bool static_validity_scroll_compatible(const std::vector<Revision>& old,
    const std::vector<Revision>& next,std::vector<std::uint64_t>& entered,bool& summary_changed) {
    entered.clear();summary_changed=false;
    std::vector<Revision> old_other,next_other;
    for(const auto& value:old)if(!value.terrain)old_other.push_back(value);
    for(const auto& value:next)if(!value.terrain)next_other.push_back(value);
    if(old_other!=next_other){summary_changed=true;return false;}
    std::vector<const Revision*> a,b;
    for(const auto& value:old)if(value.terrain)a.push_back(&value);
    for(const auto& value:next)if(value.terrain)b.push_back(&value);
    const auto group=[](const Revision& value){
        return (std::uint64_t(std::uint32_t(value.signed_id))<<32)|(value.part>>4);};
    const auto full=[](const Revision& value){
        return (std::uint64_t(std::uint32_t(value.signed_id))<<32)|value.part;};
    std::size_t i=0,j=0;
    while(i<a.size() || j<b.size()){
        if(j==b.size() || (i<a.size() && group(*a[i])<group(*b[j]))){++i;continue;}
        if(i==a.size() || group(*b[j])<group(*a[i])){entered.push_back(full(*b[j]));++j;continue;}
        const auto key=group(*a[i]);
        std::size_t ea=i,eb=j;
        while(ea<a.size() && group(*a[ea])==key)++ea;
        while(eb<b.size() && group(*b[eb])==key)++eb;
        if(ea-i!=eb-j)return false;
        for(std::size_t k=0;k<ea-i;++k)if(*a[i+k]!=*b[j+k])return false;
        i=ea;j=eb;
    }
    return true;
}

// A deferred far map holds the casters selected for the receivers of the view
// it was drawn for (far casters are selected by receiver cells). A pan keeps
// that view. A camera jump - a rotation or a changed field of view, such as
// the isometric telephoto toggle or its 180-degree turn - shows receivers
// whose casters were never drawn, so the map must be redrawn at once. The
// matrices are normalized camera world-to-clip rows (slice camera_slice):
// rows 0 and 1 are the right and up axes scaled by the projection.
inline bool far_shadow_view_continuous(const std::array<float,16>& drawn,
    const std::array<float,16>& current) noexcept {
    constexpr double cos_two_degrees=0.9993908270190958;
    for(unsigned row=0;row<2;++row){
        double a=0,b=0,dot=0;
        for(unsigned component=0;component<3;++component){
            const double x=drawn[row*4+component],y=current[row*4+component];
            a+=x*x;b+=y*y;dot+=x*y;
        }
        if(!(a>0 && b>0))return false;
        if(std::abs(std::sqrt(b/a)-1)>1e-3)return false;
        if(dot<cos_two_degrees*std::sqrt(a*b))return false;
    }
    return true;
}

// A scheduled far refresh may redraw current casters into the retained
// projection instead of refitting it. That needs the same texel size (a
// changed radius is a quality refit) and every caster that can shade the
// current receivers inside the retained depth range: the receiver columns
// across the current caster span must reproject into old depth [0,1]. The
// fitter places the depth range at least max(1, 8 texels) outside every
// contributing caster and receiver (build_retained_shadow_frame), so that
// guard is removed before the test; a tiny tolerance absorbs float roundoff.
template<class Slice>
bool far_shadow_projection_retainable(const Slice& old,const Slice& next) noexcept {
    if(old.bias[0]!=next.bias[0])return false;
    std::array<std::array<double,3>,3> new_axes{};
    std::array<double,3> new_lengths{};
    if(!far_retention_detail::unit_axes(next,new_axes,new_lengths))return false;
    const double depth=1/new_lengths[2];
    const double guard=(std::max)(1.0,8.0*next.bias[0]);
    const double minimum=-double(next.world_to_light[11])*depth+guard;
    const double maximum=minimum-2*guard+depth;
    if(!(maximum>minimum))return false;
    for(unsigned corner=0;corner<8;++corner){
        const std::array<double,3> coordinate{
            (corner&1u)?next.receiver_maximum[0]:next.receiver_minimum[0],
            (corner&2u)?next.receiver_maximum[1]:next.receiver_minimum[1],
            (corner&4u)?maximum:minimum};
        std::array<double,3> world{};
        for(unsigned axis=0;axis<3;++axis)
            for(unsigned component=0;component<3;++component)
                world[component]+=new_axes[axis][component]*coordinate[axis];
        constexpr double tolerance=1e-4;
        const double projected=far_retention_detail::project(old,2,world);
        if(projected<-tolerance || projected>1+tolerance)return false;
    }
    return true;
}

// Sun-map slices a per-frame depth caster is drawn into. Its contribution was
// fitted against the published coverage; a map drawn in a retained
// projection is not described by that coverage, so a casting part is offered
// to every such slice. Each slice then culls against the projection the map
// actually uses (`visible(slice)`), never the newly fitted one.
template<class Visible>
std::uint8_t shadow_depth_slice_mask(std::uint8_t contribution,std::uint8_t retained,
    std::uint8_t dirty,unsigned slices,const Visible& visible) {
    auto mask=static_cast<std::uint8_t>((contribution|retained)&dirty);
    for(unsigned slice=0;slice<slices;++slice)
        if((mask&(1u<<slice)) && !visible(slice))
            mask&=static_cast<std::uint8_t>(~(1u<<slice));
    return mask;
}

} // namespace owned
