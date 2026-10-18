#include "owned_terrain_front_end.h"
#include "fast_float.h"
#include "procedural_falls.h"
#include "liquid_surface_shader.h"
#include "terrain_material.h"
#include "worker_priority.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>

namespace owned {

namespace {

bool transform_terrain_bounds(const NativeWorldBounds& source,
    const std::array<float,16>& matrix,NativeWorldBounds& bounds) noexcept {
    bounds={};
    if(!source.valid)return false;
    for(float value:matrix)if(!finite_value(value))return false;
    for(unsigned corner=0;corner<8;++corner) {
        std::array<float,3> point{};
        for(unsigned row=0;row<3;++row) {
            double value=matrix[row*4+3];
            for(unsigned column=0;column<3;++column)
                value+=double(matrix[row*4+column])*
                    ((corner&(1u<<column))?source.maximum[column]:source.minimum[column]);
            point[row]=static_cast<float>(value);
            if(!finite_value(point[row]))return false;
            if(!bounds.valid)bounds.minimum[row]=bounds.maximum[row]=point[row];
            else {
                bounds.minimum[row]=(std::min)(bounds.minimum[row],point[row]);
                bounds.maximum[row]=(std::max)(bounds.maximum[row],point[row]);
            }
        }
        bounds.valid=true;
    }
    for(unsigned axis=0;axis<3;++axis) {
        bounds.minimum[axis]=next_float(bounds.minimum[axis],-INFINITY);
        bounds.maximum[axis]=next_float(bounds.maximum[axis],INFINITY);
    }
    return true;
}

void include_terrain_bounds(NativeWorldBounds& target,
    const NativeWorldBounds& value) noexcept {
    if(!value.valid)return;
    if(!target.valid)target=value;
    else for(unsigned axis=0;axis<3;++axis) {
        target.minimum[axis]=(std::min)(target.minimum[axis],value.minimum[axis]);
        target.maximum[axis]=(std::max)(target.maximum[axis],value.maximum[axis]);
    }
}

} // namespace

bool owned_terrain_vertex_height(const TerrainVertex& vertex,
    const OwnedTerrainGeometryValues& values, float& height) noexcept {
    height = vertex.z;
    if (vertex.lighting_object != UINT16_MAX) {
        if (vertex.lighting_object >= values.layer_shifts.size()) return false;
        height += values.layer_shifts[vertex.lighting_object];
    }
    return finite_value(height);
}

bool owned_terrain_base_liquid_depth(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base,
    const OwnedTerrainGeometryValues& values, float& depth) noexcept {
    depth=0;
    if (terrain_base_liquid_object(source,sector,base)==UINT16_MAX) return true;
    float height=0;
    if (!owned_terrain_vertex_height(sector.vertices[base+33u*33u],values,height)) return false;
    depth=(std::max)(0.0f,height-sector.vertices[base].z);
    return finite_value(depth);
}

namespace {
// Zone-grid node position (clamped at the zone edge) for the per-vertex
// terrain shape values.
std::array<float,3> owned_zone_node(const TerrainCpuAsset& source,int x,int y) {
    const int xmax=int(source.sector_count_x*32),ymax=int(source.sector_count_y*32);
    x=(std::clamp)(x,0,xmax);y=(std::clamp)(y,0,ymax);
    const auto sx=(std::min)(unsigned(x/32),source.sector_count_x-1);
    const auto sy=(std::min)(unsigned(y/32),source.sector_count_y-1);
    const auto& owner=source.sectors[sy*source.sector_count_x+sx];
    const unsigned index=unsigned(y-int(sy*32))*33+unsigned(x-int(sx*32));
    if(index>=owner.vertices.size())return std::array<float,3>{float(x),float(y),0.f};
    const auto& vertex=owner.vertices[index];
    return std::array<float,3>{vertex.x,vertex.y,vertex.z};
}
} // namespace

std::uint32_t owned_terrain_facet_flatness(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept {
    if(base<sector.base_facet_flatness.size())return sector.base_facet_flatness[base];
    if(base>=1089||!source.sector_count_x||!source.sector_count_y)return 255u|(255u<<8);
    return terrain_facet_flatness([&](int x,int y){return owned_zone_node(source,x,y);},
        int(sector.map_x*32+base%33),int(sector.map_y*32+base/33));
}

float owned_terrain_pit_fill(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept {
    if(base>=1089||!source.sector_count_x||!source.sector_count_y)return 0;
    const int x=int(sector.map_x*32+base%33),y=int(sector.map_y*32+base/33);
    const float z=owned_zone_node(source,x,y)[2];
    const float across=.5f*(owned_zone_node(source,x-1,y)[2]+owned_zone_node(source,x+1,y)[2])-z;
    const float along=.5f*(owned_zone_node(source,x,y-1)[2]+owned_zone_node(source,x,y+1)[2])-z;
    const float fill=(std::min)(across,along);
    return finite_value(fill)&&fill>0?fill:0.f;
}

std::uint32_t owned_terrain_slope_normal(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, std::uint32_t base) noexcept {
    if(base<sector.base_slope_normals.size())return sector.base_slope_normals[base];
    if(base>=1089||!source.sector_count_x||!source.sector_count_y)return 0;
    const int xmax=int(source.sector_count_x*32),ymax=int(source.sector_count_y*32);
    // Clamping at the zone edge makes that difference one-sided; the cross
    // product only scales, so the direction stays exact.
    auto position=[&](int x,int y) {
        x=(std::clamp)(x,0,xmax);y=(std::clamp)(y,0,ymax);
        const auto sx=(std::min)(unsigned(x/32),source.sector_count_x-1);
        const auto sy=(std::min)(unsigned(y/32),source.sector_count_y-1);
        const auto& owner=source.sectors[sy*source.sector_count_x+sx];
        const unsigned index=unsigned(y-int(sy*32))*33+unsigned(x-int(sx*32));
        if(index>=owner.vertices.size())return std::array<float,3>{float(x),float(y),0.f};
        const auto& vertex=owner.vertices[index];
        return std::array<float,3>{vertex.x,vertex.y,vertex.z};
    };
    return terrain_slope_normal(position,int(sector.map_x*32+base%33),int(sector.map_y*32+base/33));
}

void owned_terrain_prepare_shading(TerrainCpuAsset& source) noexcept {
    const std::size_t count=source.sectors.size();
    if(!count)return;
    try {
        // Computed into separate vectors first: the readers above must see
        // either none or all of a sector's values.
        std::vector<std::vector<std::uint32_t>> slopes(count),flatness(count);
        std::atomic<std::size_t> next{0};
        const auto work=[&] {
            for(std::size_t i=next++;i<count;i=next++) {
                const auto& sector=source.sectors[i];
                slopes[i].resize(1089);flatness[i].resize(1089);
                for(std::uint32_t base=0;base<1089;++base) {
                    slopes[i][base]=owned_terrain_slope_normal(source,sector,base);
                    flatness[i][base]=owned_terrain_facet_flatness(source,sector,base);
                }
            }
        };
        const unsigned threads=(std::min)(4u,(std::max)(1u,std::thread::hardware_concurrency()/2));
        std::vector<std::thread> helpers;
        try {for(unsigned t=1;t<threads;++t)helpers.emplace_back([&work] {const BackgroundWorkerPriority priority;work();});} catch(...) {}
        work();
        for(auto& helper:helpers)helper.join();
        for(std::size_t i=0;i<count;++i) {
            source.sectors[i].base_slope_normals=std::move(slopes[i]);
            source.sectors[i].base_facet_flatness=std::move(flatness[i]);
        }
    } catch(...) {
        for(auto& sector:source.sectors){sector.base_slope_normals.clear();sector.base_facet_flatness.clear();}
    }
}

std::uint32_t owned_terrain_fill_flags(const TerrainCpuAsset& source,
    const TerrainSectorCpuAsset& sector, const TerrainTileDescriptor& tile,
    const std::array<NativeTextureCoordinate,9>& uvs) noexcept {
    const std::uint32_t first = tile.vertex_indices[0];
    if(tile.transparent||first>=1089||!source.sector_count_x||!source.sector_count_y)return 0;
    const int xmax=int(source.sector_count_x*16),ymax=int(source.sector_count_y*16);
    auto id=[&](int x,int y) {
        if(x<0||y<0||x>=xmax||y>=ymax)return -1;
        const auto& owner=source.sectors[unsigned(y/16)*source.sector_count_x+unsigned(x/16)];
        const std::uint16_t value=owner.base_tile_ids[unsigned(y%16)*16+unsigned(x%16)];
        return value==UINT16_MAX?-1:int(value);
    };
    const int tx=int(sector.map_x*16+(first%33)/2),ty=int(sector.map_y*16+(first/33)/2);
    const std::uint32_t grid=terrain_fill_corners(id,tx,ty);
    if(!grid)return 0;
    constexpr std::uint32_t lanes[4]={0,2,6,8}; // Grid corners (cy*2+cx) of the 3x3 lanes.
    float cu=0,cv=0;
    for(const auto lane:lanes){cu+=uvs[lane].u*.25f;cv+=uvs[lane].v*.25f;}
    std::uint32_t flags=0;
    for(std::uint32_t corner=0;corner<4;++corner){
        if(!(grid&(1u<<corner)))continue;
        const auto& uv=uvs[lanes[corner]];
        flags|=1u<<(8u+(uv.u>cu?1u:0u)+(uv.v>cv?2u:0u));
    }
    return flags;
}

namespace {

// Surface height of a visible liquid layer vertex at global grid (x,y); false
// off the map, where the layer vertex carries no liquid object, or where the
// layer lies under the ground (authors bury overlay edges on steep banks;
// those never show and must not read as falls).
bool liquid_surface_height(const TerrainCpuAsset& source,int x,int y,
    const OwnedTerrainGeometryValues* values,float& z) noexcept {
    const int xmax=int(source.sector_count_x*32),ymax=int(source.sector_count_y*32);
    if(x<0||y<0||x>xmax||y>ymax) {
        // An adjoining map's liquid beyond the edge (rest heights).
        const auto& outside=source.outside_liquid;const int r=outside.reach,w=xmax+1+2*r;
        if(!r||x<-r||y<-r||x>xmax+r||y>ymax+r||
           outside.heights.size()!=std::size_t(w)*std::size_t(ymax+1+2*r))return false;
        z=outside.heights[std::size_t(y+r)*std::size_t(w)+std::size_t(x+r)];
        return finite_value(z);
    }
    const auto sx=(std::min)(unsigned(x/32),source.sector_count_x-1);
    const auto sy=(std::min)(unsigned(y/32),source.sector_count_y-1);
    const auto& sector=source.sectors[sy*source.sector_count_x+sx];
    const unsigned index=unsigned(y-int(sy*32))*33+unsigned(x-int(sx*32));
    if(terrain_base_liquid_object(source,sector,index)==UINT16_MAX)return false;
    const auto& vertex=sector.vertices[index+1089];
    if(values){if(!owned_terrain_vertex_height(vertex,*values,z))return false;}
    else z=vertex.z;
    return finite_value(z)&&z>=sector.vertices[index].z-kLiquidBuriedTolerance;
}

// Central difference, one-sided beside a missing neighbour.
bool liquid_surface_gradient(const TerrainCpuAsset& source,int x,int y,
    const OwnedTerrainGeometryValues* values,std::array<float,2>& result) noexcept {
    result={};float c=0;
    if(!liquid_surface_height(source,x,y,values,c))return false;
    for(int axis=0;axis<2;++axis) {
        float a=0,b=0;
        const bool left=axis?liquid_surface_height(source,x,y-1,values,a):liquid_surface_height(source,x-1,y,values,a);
        const bool right=axis?liquid_surface_height(source,x,y+1,values,b):liquid_surface_height(source,x+1,y,values,b);
        result[axis]=left&&right?(b-a)*.5f:left?c-a:right?b-c:0.f;
    }
    return true;
}

// The flow terms read gradients at most this far from the vertex (bend and
// tent reach 2, the cascade disc reaches reach-1).
constexpr int kLiquidFlowGradientReach=int(kLiquidCascadeReach)-1;
// Pit rays stay inside the same window.
constexpr int kLiquidPitReach=4;
static_assert(kLiquidPitReach<=kLiquidFlowGradientReach,"pit rays must stay inside the flow window");
static_assert(kLiquidFlowGradientReach>=2 && float(kLiquidFlowGradientReach+1)==kLiquidCascadeReach &&
    kLiquidFlowGradientReach==OwnedLiquidFlowSector::kReach,
    "the sector flow window must cover every gradient the flow terms read");

// Cascade foam strength of one gradient: its slope ramped between the art
// limits and smoothstepped.
float liquid_cascade_steepness(const std::array<float,2>& g) noexcept {
    const float t=std::clamp((std::hypot(g[0],g[1])-kLiquidCascadeSlope[0])/
                             (kLiquidCascadeSlope[1]-kLiquidCascadeSlope[0]),0.f,1.f);
    return t*t*(3-2*t);
}

// Offsets inside the cascade disc with their distance fade, in scan order.
struct LiquidCascadeTap { int dx=0,dy=0; float closeness=0; };
struct LiquidCascadeTaps {
    std::array<LiquidCascadeTap,(2*kLiquidFlowGradientReach+1)*(2*kLiquidFlowGradientReach+1)> taps{};
    std::size_t count=0;
};
const LiquidCascadeTaps& liquid_cascade_taps() noexcept {
    static const LiquidCascadeTaps taps=[]() noexcept {
        LiquidCascadeTaps values;
        const int reach=int(kLiquidCascadeReach);
        for(int dy=-reach+1;dy<reach;++dy)for(int dx=-reach+1;dx<reach;++dx) {
            const float distance=std::hypot(float(dx),float(dy));
            if(distance>=kLiquidCascadeReach)continue;
            values.taps[values.count++]={dx,dy,1-distance/kLiquidCascadeReach};
        }
        return values;
    }();
    return taps;
}

// One body for the per-vertex and the per-sector form, so both give
// bit-identical results. gradient(x,y,g), steepness(x,y,s) and height(x,y,z)
// are pure functions of the grid (steepness = liquid_cascade_steepness of a
// valid gradient; height is valid exactly where the gradient is).
template<class Gradient,class Steepness,class Height>
bool liquid_flow_from_gradients(const Gradient& gradient,const Steepness& steepness,
    const Height& height,int gx,int gy,OwnedLiquidFlow& flow) noexcept {
    flow={};
    std::array<float,2> centre{};
    if(!gradient(gx,gy,centre))return true;
    // How fast the surface gradient (hence the flow) changes here: the shader
    // widens its texture footprint by it so the advected ripples never alias.
    float bend=0;
    for(int axis=0;axis<2;++axis) {
        std::array<float,2> a{},b{};
        const bool low=axis?gradient(gx,gy-2,a):gradient(gx-2,gy,a);
        const bool high=axis?gradient(gx,gy+2,b):gradient(gx+2,gy,b);
        const float span=low&&high?4.f:2.f;
        if(!low)a=centre;
        if(!high)b=centre;
        bend+=((b[0]-a[0])*(b[0]-a[0])+(b[1]-a[1])*(b[1]-a[1]))/(span*span);
    }
    flow.bend=std::sqrt(bend);
    // 5x5 tent of the liquid vertices' gradients: on 1 m vertices a narrower
    // stencil turned the flow direction sharply between triangles.
    float weight=0;
    for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
        std::array<float,2> g{};
        if(!gradient(gx+dx,gy+dy,g))continue;
        const float w=float((3-std::abs(dx))*(3-std::abs(dy)));
        flow.current[0]-=g[0]*w;flow.current[1]-=g[1]*w;weight+=w;
    }
    for(auto& value:flow.current)value/=weight;
    // Cascade foam source: the steepest visible surface within reach, faded
    // smoothly by distance (every vertex, so the field has no tile steps).
    // Impact: the same source, but only on flat water lying below the steep
    // surface (where a fall lands), never on the falling face or above it.
    float centreHeight=0;
    if(!height(gx,gy,centreHeight))return false;
    const float pool=1-std::clamp(std::hypot(centre[0],centre[1])/kLiquidCascadeSlope[0],0.f,1.f);
    const auto& cascade=liquid_cascade_taps();
    float lip=centreHeight;
    for(std::size_t index=0;index<cascade.count;++index) {
        const auto& tap=cascade.taps[index];
        // Fall lip: the highest liquid surface within reach. Falls are steep, so
        // their lip lies within a few cells horizontally even for tall drops.
        float tapHeight=0;
        if(height(gx+tap.dx,gy+tap.dy,tapHeight))lip=(std::max)(lip,tapHeight);
        float steep=0;
        if(!steepness(gx+tap.dx,gy+tap.dy,steep))continue;
        const float closeness=tap.closeness;
        const float source=steep*closeness*closeness*(3-2*closeness);
        flow.cascade=(std::max)(flow.cascade,source);
        float fallHeight=0;
        if(pool>0&&height(gx+tap.dx,gy+tap.dy,fallHeight)) {
            const float drop=std::clamp((fallHeight-centreHeight)/kLiquidCascadeSlope[1],0.f,1.f);
            flow.impact=(std::max)(flow.impact,source*pool*drop);
        }
    }
    flow.drop=lip-centreHeight;
    // Pit fill: in each of eight directions the highest surface within
    // kLiquidPitReach cells (stopping at the liquid's edge); the lowest of
    // those is where a pit spills over. Running water always has a downhill
    // direction, so only closed dips (impact funnels) get a fill.
    float spill=std::numeric_limits<float>::max();
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
        if(!dx&&!dy)continue;
        float highest=centreHeight;
        for(int step=1;step<=kLiquidPitReach;++step) {
            float z=0;
            if(!height(gx+dx*step,gy+dy*step,z))break;
            highest=(std::max)(highest,z);
        }
        spill=(std::min)(spill,highest);
    }
    flow.fill=spill-centreHeight;
    return finite_value(flow.current[0])&&finite_value(flow.current[1])&&finite_value(flow.bend)&&
        finite_value(flow.cascade)&&finite_value(flow.impact)&&finite_value(flow.drop)&&finite_value(flow.fill);
}

} // namespace

// Global grid addressing gives both copies of a sector-edge vertex the same
// values. Missing liquid neighbours use one-sided differences, never invented
// layer heights.
bool owned_terrain_liquid_flow(const TerrainCpuAsset& source,
    std::uint32_t sector_index,std::uint32_t base,
    const OwnedTerrainGeometryValues* values,OwnedLiquidFlow& flow) noexcept {
    flow={};
    if(sector_index>=source.sectors.size()||base>=1089||!source.sector_count_x||!source.sector_count_y)return false;
    const auto& origin=source.sectors[sector_index];
    const int gx=int(origin.map_x*32+base%33),gy=int(origin.map_y*32+base/33);
    const auto gradient=[&](int x,int y,std::array<float,2>& g) {
        return liquid_surface_gradient(source,x,y,values,g);
    };
    const auto steepness=[&](int x,int y,float& steep) {
        std::array<float,2> g{};
        if(!gradient(x,y,g))return false;
        steep=liquid_cascade_steepness(g);
        return true;
    };
    const auto height=[&](int x,int y,float& z) {
        return liquid_surface_height(source,x,y,values,z);
    };
    return liquid_flow_from_gradients(gradient,steepness,height,gx,gy,flow);
}

bool OwnedLiquidFlowSector::prepare(const TerrainCpuAsset& source,std::uint32_t sector_index,
    const OwnedTerrainGeometryValues* values) noexcept {
    ready_=false;done_.fill(0);
    if(sector_index>=source.sectors.size()||!source.sector_count_x||!source.sector_count_y)return false;
    const auto& origin=source.sectors[sector_index];
    origin_x_=int(origin.map_x*32)-kLiquidFlowGradientReach;
    origin_y_=int(origin.map_y*32)-kLiquidFlowGradientReach;
    for(int y=0;y<kSide;++y)for(int x=0;x<kSide;++x) {
        const std::size_t cell=std::size_t(y)*kSide+std::size_t(x);
        valid_[cell]=liquid_surface_gradient(source,origin_x_+x,origin_y_+y,values,gradients_[cell])?1u:0u;
        steepness_[cell]=valid_[cell]?liquid_cascade_steepness(gradients_[cell]):0.f;
        heights_[cell]=0;
        if(valid_[cell])liquid_surface_height(source,origin_x_+x,origin_y_+y,values,heights_[cell]);
    }
    ready_=true;
    return true;
}

bool OwnedLiquidFlowSector::flow(std::uint32_t base,OwnedLiquidFlow& flow) noexcept {
    flow={};
    if(!ready_||base>=1089)return false;
    if(!done_[base]) {
        const int gx=origin_x_+kLiquidFlowGradientReach+int(base%33);
        const int gy=origin_y_+kLiquidFlowGradientReach+int(base/33);
        bool inside=true;
        const auto at=[&](int x,int y,std::size_t& cell) {
            x-=origin_x_;y-=origin_y_;
            if(x<0||y<0||x>=kSide||y>=kSide){inside=false;return false;}
            cell=std::size_t(y)*kSide+std::size_t(x);
            return valid_[cell]!=0;
        };
        const auto gradient=[&](int x,int y,std::array<float,2>& g) {
            std::size_t cell=0;g={};
            if(!at(x,y,cell))return false;
            g=gradients_[cell];return true;
        };
        const auto steepness=[&](int x,int y,float& steep) {
            std::size_t cell=0;
            if(!at(x,y,cell))return false;
            steep=steepness_[cell];return true;
        };
        const auto height=[&](int x,int y,float& z) {
            std::size_t cell=0;
            if(!at(x,y,cell))return false;
            z=heights_[cell];return true;
        };
        if(!liquid_flow_from_gradients(gradient,steepness,height,gx,gy,flows_[base])||!inside)return false;
        done_[base]=1;
    }
    flow=flows_[base];
    return true;
}

void owned_terrain_set_outside_liquid(TerrainCpuAsset& terrain,int offset_x,int offset_y,
    const TerrainCpuAsset& adjoining) {
    const int xmax=int(terrain.sector_count_x*32),ymax=int(terrain.sector_count_y*32);
    const int r=kLiquidFlowGradientReach+1,w=xmax+1+2*r,h=ymax+1+2*r;
    TerrainOutsideLiquid outside;outside.reach=r;
    outside.heights.assign(std::size_t(w)*std::size_t(h),std::numeric_limits<float>::quiet_NaN());
    bool any=false;
    for(int y=-r;y<=ymax+r;++y)for(int x=-r;x<=xmax+r;++x) {
        if(x>=0&&y>=0&&x<=xmax&&y<=ymax)continue;
        float z=0;
        if(!liquid_surface_height(adjoining,x+offset_x,y+offset_y,nullptr,z))continue;
        outside.heights[std::size_t(y+r)*std::size_t(w)+std::size_t(x+r)]=z;any=true;
    }
    // The adjoining map's own flow at each shared edge vertex (it was decoded
    // without this terrain, so its edge reads one-sided; both now carry it).
    const int axmax=int(adjoining.sector_count_x*32),aymax=int(adjoining.sector_count_y*32);
    for(unsigned side=0;side<4;++side) {
        const bool along_x=side<2;const int count=(along_x?xmax:ymax)+1;
        auto& edge=outside.edge_flow[side];edge.assign(std::size_t(count),TerrainEdgeFlow{});
        for(int k=0;k<count;++k) {
            const int x=along_x?k:(side==2?0:xmax),y=along_x?(side==0?0:ymax):k;
            const int ax=x+offset_x,ay=y+offset_y;
            if(ax<0||ay<0||ax>axmax||ay>aymax)continue;
            float z=0;if(!liquid_surface_height(adjoining,ax,ay,nullptr,z))continue;
            const auto sx=(std::min)(unsigned(ax/32),adjoining.sector_count_x-1),sy=(std::min)(unsigned(ay/32),adjoining.sector_count_y-1);
            OwnedLiquidFlow flow;
            if(!owned_terrain_liquid_flow(adjoining,sy*adjoining.sector_count_x+sx,
                unsigned(ay-int(sy*32))*33+unsigned(ax-int(sx*32)),nullptr,flow))continue;
            auto& e=edge[std::size_t(k)];e.current[0]=flow.current[0];e.current[1]=flow.current[1];
            e.bend=flow.bend;e.cascade=flow.cascade;e.impact=flow.impact;e.drop=flow.drop;e.fill=flow.fill;e.valid=true;any=true;
        }
    }
    terrain.outside_liquid=any?std::move(outside):TerrainOutsideLiquid{};
}

void owned_terrain_weld_edge_flow(const TerrainCpuAsset& terrain,int x,int y,OwnedLiquidFlow& flow) noexcept {
    const auto& outside=terrain.outside_liquid;
    const int xmax=int(terrain.sector_count_x*32),ymax=int(terrain.sector_count_y*32);
    int best=kTerrainEdgeFlowBand;const TerrainEdgeFlow* edge=nullptr;
    const int distance[4]={y,ymax-y,x,xmax-x},along[4]={x,x,y,y};
    for(unsigned side=0;side<4;++side) {
        const auto& e=outside.edge_flow[side];
        if(distance[side]<0||distance[side]>=best||along[side]<0||std::size_t(along[side])>=e.size()||!e[std::size_t(along[side])].valid)continue;
        best=distance[side];edge=&e[std::size_t(along[side])];
    }
    if(!edge)return;
    float t=float(best)/float(kTerrainEdgeFlowBand);t=t*t*(3-2*t);
    const auto mix=[t](float a,float own){return a+(own-a)*t;};
    flow.current[0]=mix(edge->current[0],flow.current[0]);flow.current[1]=mix(edge->current[1],flow.current[1]);
    flow.bend=mix(edge->bend,flow.bend);flow.cascade=mix(edge->cascade,flow.cascade);flow.impact=mix(edge->impact,flow.impact);
    flow.drop=mix(edge->drop,flow.drop);flow.fill=mix(edge->fill,flow.fill);
}

// Swamp frames with open water painted in. The shipped swamp atlas holds,
// beside its plain algae frames, transition frames whose art is part algae,
// part open water, placed along a swamp's border; drawn as the algae art
// they cut hard tile-shaped water patches into the mat (save1138 swamp_debug
// views 1, 4, 5). Each frame is read over the bounds the liquid shader
// samples (terrain_tile_uvs), level 0. Reference colours are the mean art of
// the most placed swamp frame and of the most placed open-water frame of the
// map's liquid tiles; a texel is water when nearer the water reference. A
// swamp frame whose water share exceeds the reference swamp frame's own by
// kSwampFrameWater or more gets cover 1 - excess/(1 - baseline).
std::string owned_terrain_classify_swamp_frames(TerrainCpuAsset& terrain) {
    terrain.swamp_frame_cover.clear();terrain.swamp_frame_texels.clear();terrain.swamp_frame_side=0;
    // Square-atlas slots that have a tile type (the map declares tile_count
    // types, which may be fewer than the pages' slot grid).
    const std::uint32_t slots=static_cast<std::uint32_t>((std::min)(std::size_t(terrain.texture_count)*terrain.tiles_per_texture,std::size_t((std::min)(std::size_t(terrain.tile_count),terrain.tile_types.size()))));
    if(!slots)return "no square-atlas tile slots";
    std::vector<std::uint32_t> swamp_count(slots),water_count(slots);
    for(const auto& sector:terrain.sectors)for(const auto& tile:sector.tiles) {
        if(!tile.transparent||tile.object_id>=terrain.objects.size()||tile.tile_index>=slots||
           !terrain_liquid_object(terrain.objects[tile.object_id].type))continue;
        ++(terrain_swamp_tile(terrain,tile)?swamp_count:water_count)[tile.tile_index];
    }
    std::uint32_t swamp_tiles=0,water_tiles=0;
    for(std::uint32_t s=0;s<slots;++s){swamp_tiles+=swamp_count[s];water_tiles+=water_count[s];}
    const auto most=[&](const std::vector<std::uint32_t>& c){
        std::uint32_t best=slots;for(std::uint32_t s=0;s<slots;++s)if(c[s]&&(best==slots||c[s]>c[best]))best=s;return best;};
    const std::uint32_t swamp_ref=most(swamp_count),water_ref=most(water_count);
    if(swamp_ref==slots||water_ref==slots){
        char text[128];std::snprintf(text,sizeof(text),"no reference: %u swamp and %u open-water liquid tiles in %u slots",swamp_tiles,water_tiles,slots);
        return text;
    }
    // A frame's level-0 texels over its shader bounds (BC1 pages only).
    const char* unread="";int rect_w=0,rect_h=0;
    const auto texels=[&](std::uint32_t slot,std::vector<std::array<int,3>>& out)->bool {
        out.clear();
        const std::uint32_t page=slot/terrain.tiles_per_texture;
        if(page>=terrain.pages.size()){unread="page not loaded";return false;}
        const auto& texture=terrain.pages[page].texture;
        if(!texture){unread="page has no CPU texture";return false;}
        if(texture->format!=MmpFormat::dxt1||texture->mips.empty()||!texture->source){unread="page is not BC1";return false;}
        const auto& mip=texture->mips[0];
        if(std::size_t(mip.byte_offset)+mip.bytes>texture->source->size()||
           std::size_t((mip.width+3)/4)*((mip.height+3)/4)*8>mip.bytes)return false;
        TerrainTileDescriptor probe{};probe.tile_index=static_cast<std::uint16_t>(slot);
        const auto uvs=terrain_tile_uvs(terrain,probe);
        float u0=uvs[0].u,v0=uvs[0].v,u1=u0,v1=v0;
        for(const auto& uv:uvs){u0=(std::min)(u0,uv.u);v0=(std::min)(v0,uv.v);u1=(std::max)(u1,uv.u);v1=(std::max)(v1,uv.v);}
        const int x0=(std::max)(0,int(std::ceil(u0*mip.width))),x1=(std::min)(int(mip.width),int(std::floor(u1*mip.width)));
        const int y0=(std::max)(0,int(std::ceil(v0*mip.height))),y1=(std::min)(int(mip.height),int(std::floor(v1*mip.height)));
        rect_w=(std::max)(0,x1-x0);rect_h=(std::max)(0,y1-y0);
        const std::uint8_t* base=texture->source->data()+mip.byte_offset;
        const auto channel=[](unsigned c,int k){return k==0?int((c>>11)&31)*255/31:k==1?int((c>>5)&63)*255/63:int(c&31)*255/31;};
        for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x) {
            const std::uint8_t* b=base+(std::size_t(y/4)*((mip.width+3)/4)+std::size_t(x/4))*8;
            const unsigned c0=b[0]|(b[1]<<8),c1=b[2]|(b[3]<<8),sel=(b[4+(y%4)]>>(2*(x%4)))&3u;
            std::array<int,3> rgb{};
            for(int k=0;k<3;++k){const int a=channel(c0,k),e=channel(c1,k);
                rgb[std::size_t(k)]=sel==0?a:sel==1?e:c0>c1?(sel==2?(2*a+e)/3:(a+2*e)/3):(sel==2?(a+e)/2:0);}
            out.push_back(rgb);
        }
        if(out.empty())unread="empty frame bounds";
        return !out.empty();
    };
    const auto mean=[](const std::vector<std::array<int,3>>& p){
        std::array<double,3> m{};for(const auto& t:p)for(int k=0;k<3;++k)m[std::size_t(k)]+=t[std::size_t(k)];
        for(auto& v:m)v/=double(p.size());return m;};
    std::vector<std::array<int,3>> pixels;
    if(!texels(swamp_ref,pixels))return std::string("reference swamp frame unread: ")+unread;
    const auto swamp_mean=mean(pixels);
    if(!texels(water_ref,pixels))return std::string("reference water frame unread: ")+unread;
    const auto water_mean=mean(pixels);
    const auto water_share=[&](const std::vector<std::array<int,3>>& p){
        std::size_t n=0;
        for(const auto& t:p){double ds=0,dw=0;
            for(int k=0;k<3;++k){const double s=t[std::size_t(k)]-swamp_mean[std::size_t(k)],w=t[std::size_t(k)]-water_mean[std::size_t(k)];ds+=s*s;dw+=w*w;}
            n+=dw<ds;}
        return double(n)/double(p.size());};
    texels(swamp_ref,pixels);
    const double baseline=water_share(pixels);
    constexpr double kSwampFrameWater=.10;
    terrain.swamp_frame_cover.assign(slots,1.f);
    std::string list;std::uint32_t frames=0,flagged=0,tiles=0,skipped=0;
    for(std::uint32_t s=0;s<slots;++s) {
        if(!swamp_count[s])continue;
        if(!texels(s,pixels)){++skipped;continue;}
        ++frames;
        const double share=water_share(pixels),excess=share-baseline;
        char item[48];std::snprintf(item,sizeof(item),"%s%u:%.2f",list.empty()?"":" ",s,share);list+=item;
        if(excess<kSwampFrameWater)continue;
        terrain.swamp_frame_cover[s]=float(std::clamp(1-excess/(std::max)(1-baseline,1e-6),0.,1.));
        ++flagged;tiles+=swamp_count[s];list+="*";
    }
    // The map swamp frame (swamp_frame_texels): the reference frame's art,
    // resampled bilinearly to a power-of-two square for its own texture.
    std::uint32_t side=0;
    if(texels(swamp_ref,pixels)&&rect_w>0&&rect_h>0) {
        side=1;while(side<std::uint32_t((std::max)(rect_w,rect_h))&&side<256)side*=2;
        terrain.swamp_frame_texels.assign(std::size_t(side)*side,{});
        terrain.swamp_frame_side=side;
        for(std::uint32_t y=0;y<side;++y)for(std::uint32_t x=0;x<side;++x) {
            const float fx=(std::min)((x+.5f)*rect_w/side-.5f,float(rect_w-1)),fy=(std::min)((y+.5f)*rect_h/side-.5f,float(rect_h-1));
            const int ax=(std::max)(0,int(std::floor(fx))),ay=(std::max)(0,int(std::floor(fy)));
            const int bx=(std::min)(ax+1,rect_w-1),by=(std::min)(ay+1,rect_h-1);
            const float tx=(std::max)(0.f,fx-ax),ty=(std::max)(0.f,fy-ay);
            auto& out=terrain.swamp_frame_texels[std::size_t(y)*side+x];
            for(int k=0;k<3;++k){
                const auto at=[&](int px,int py){return float(pixels[std::size_t(py)*rect_w+px][std::size_t(k)])/255.f;};
                out[std::size_t(k)]=(at(ax,ay)*(1-tx)+at(bx,ay)*tx)*(1-ty)+(at(ax,by)*(1-tx)+at(bx,by)*tx)*ty;
            }
            out[3]=1.f;
        }
    }
    // Liquid tiles per atlas page by class; a page without a pure swamp
    // frame has no same-page frame to lend, so its transition and nearby
    // open-water tiles take the map swamp frame.
    std::map<std::uint32_t,std::array<std::uint32_t,3>> pages;
    for(std::uint32_t s=0;s<slots;++s) {
        auto& page=pages[s/terrain.tiles_per_texture];
        if(swamp_count[s])page[terrain.swamp_frame_cover[s]<1.f?1:0]+=swamp_count[s];
        page[2]+=water_count[s];
    }
    std::string page_list;
    for(const auto& [page,c]:pages) {
        if(!c[0]&&!c[1]&&!c[2])continue;
        char item[96];std::snprintf(item,sizeof(item),"%s%u: %u pure, %u transition, %u water%s",page_list.empty()?"":"; ",page,c[0],c[1],c[2],c[0]?"":" (no pure swamp frame)");
        page_list+=item;
    }
    char head[400];
    std::snprintf(head,sizeof(head),"%u swamp and %u open-water liquid tiles; reference swamp slot %u, water slot %u, swamp baseline %.2f, threshold +%.2f: %u of %u swamp frames (%u tiles) hold open water (*), %u unread%s%s; map swamp frame %ux%u from %dx%d; water share by slot: ",
        swamp_tiles,water_tiles,swamp_ref,water_ref,baseline,kSwampFrameWater,flagged,frames,tiles,skipped,skipped?" ":"",skipped?unread:"",side,side,rect_w,rect_h);
    return head+list+"; tiles by page: "+page_list;
}

void owned_terrain_liquid_materials(const TerrainCpuAsset& source,std::uint32_t sector_index,
    std::array<TerrainObjectDescriptor,1089>& materials,std::array<float,1089>* swamp) noexcept {
    materials={};
    for(auto& material:materials)material.opacity=0;
    if(swamp)swamp->fill(0.f);
    if(sector_index>=source.sectors.size())return;
    const auto& origin=source.sectors[sector_index];
    std::array<unsigned,1089> counts{};
    // Liquid tiles of this sector and its ring in local tile coordinates
    // -2..17 (their swamp amount: 0 water, the frame's algae cover for swamp,
    // negative none), for the swamp share below.
    std::array<float,20*20> liquid_tiles;liquid_tiles.fill(-1.f);
    for(int sy=(std::max)(0,int(origin.map_y)-1);sy<=(std::min)(int(source.sector_count_y)-1,int(origin.map_y)+1);++sy)
    for(int sx=(std::max)(0,int(origin.map_x)-1);sx<=(std::min)(int(source.sector_count_x)-1,int(origin.map_x)+1);++sx) {
        const auto& sector=source.sectors[sy*source.sector_count_x+sx];
        for(const auto& tile:sector.tiles) {
            if(!tile.transparent||tile.object_id>=source.objects.size())continue;
            const auto& object=source.objects[tile.object_id];
            if(!terrain_liquid_object(object.type))continue;
            const bool swamp_tile=terrain_swamp_tile(source,tile);
            {unsigned low_x=33,low_y=33;
             for(auto index:tile.vertex_indices){const unsigned grid=index>=1089?index-1089u:index;
                 low_x=(std::min)(low_x,grid%33);low_y=(std::min)(low_y,grid/33);}
             const int tx=(sx-int(origin.map_x))*16+int(low_x/2),ty=(sy-int(origin.map_y))*16+int(low_y/2);
             if(tx>=-2&&tx<=17&&ty>=-2&&ty<=17)liquid_tiles[std::size_t(ty+2)*20+std::size_t(tx+2)]=swamp_tile?terrain_swamp_frame_cover(source,tile):0.f;}
            for(auto index:tile.vertex_indices) {
                if(index<1089)continue;
                const int base=index-1089;
                const int x=sx*32+base%33-int(origin.map_x)*32;
                const int y=sy*32+base/33-int(origin.map_y)*32;
                if(x<0||x>32||y<0||y>32)continue;
                auto& material=materials[y*33+x];++counts[y*33+x];material.type=2;
                for(unsigned axis=0;axis<3;++axis)material.color[axis]+=object.color[axis];
                material.opacity+=object.opacity;material.self_illumination+=object.self_illumination;
                material.wave_multiplier+=object.wave_multiplier;
            }
        }
    }
    for(unsigned n=0;n<1089;++n)if(counts[n]) {
        auto& material=materials[n];const float scale=1.f/counts[n];
        for(auto& channel:material.color)channel*=scale;
        material.opacity*=scale;material.self_illumination*=scale;material.wave_multiplier*=scale;
    }
    if(!swamp)return;
    // Swamp share: the swamp amount of the liquid tiles whose centres lie
    // within kSwampShareRadius grid cells, weighted (1-(d/r)^2)^2; a swamp
    // transition frame counts by its algae cover (swamp_frame_cover). Averaging
    // only the tiles touching a vertex ramped the share across one cell at
    // tile edges, so the shader's threshold drew the tile grid as a
    // staircase. This radius rounds a diagonal of tile steps into a smooth
    // line (residual wobble ~.01 share) while a lone swamp tile still peaks
    // above one half. Tile positions are map-global through the ring, so
    // both sectors of a seam vertex compute the same share.
    constexpr float kSwampShareRadius=2.75f;
    for(int y=0;y<=32;++y)for(int x=0;x<=32;++x) {
        if(!counts[std::size_t(y)*33+std::size_t(x)])continue;
        float liquid=0,share=0;
        for(int ty=y/2-2;ty<=y/2+1;++ty)for(int tx=x/2-2;tx<=x/2+1;++tx) {
            if(tx<-2||tx>17||ty<-2||ty>17)continue;
            const float amount=liquid_tiles[std::size_t(ty+2)*20+std::size_t(tx+2)];
            if(amount<0)continue;
            const float dx=float(x-(2*tx+1)),dy=float(y-(2*ty+1));
            const float d2=(dx*dx+dy*dy)/(kSwampShareRadius*kSwampShareRadius);
            if(d2>=1.f)continue;
            const float w=(1.f-d2)*(1.f-d2);
            liquid+=w;share+=w*amount;
        }
        (*swamp)[std::size_t(y)*33+std::size_t(x)]=liquid>0?share/liquid:0.f;
    }
}

void owned_terrain_liquid_neighbour_masks(const TerrainCpuAsset& source,std::uint32_t sector_index,
    std::array<std::uint16_t,256>& masks) noexcept {
    masks={};
    if(sector_index>=source.sectors.size()||!source.sector_count_x||!source.sector_count_y)return;
    const auto& origin=source.sectors[sector_index];
    // Liquid tiles of this sector and its ring, in local tile coordinates -1..16.
    std::array<bool,18*18> liquid{};
    const int map_x=int(source.sector_count_x)*16,map_y=int(source.sector_count_y)*16;
    const int base_x=int(origin.map_x)*16,base_y=int(origin.map_y)*16;
    for(int sy=(std::max)(0,int(origin.map_y)-1);sy<=(std::min)(int(source.sector_count_y)-1,int(origin.map_y)+1);++sy)
    for(int sx=(std::max)(0,int(origin.map_x)-1);sx<=(std::min)(int(source.sector_count_x)-1,int(origin.map_x)+1);++sx) {
        for(const auto& tile:source.sectors[sy*source.sector_count_x+sx].tiles) {
            if(!tile.transparent||tile.object_id>=source.objects.size()||
               !terrain_liquid_object(source.objects[tile.object_id].type))continue;
            unsigned low_x=33,low_y=33;
            for(auto index:tile.vertex_indices){const unsigned grid=index>=1089?index-1089u:index;
                low_x=(std::min)(low_x,grid%33);low_y=(std::min)(low_y,grid/33);}
            const int x=sx*16+int(low_x/2)-base_x,y=sy*16+int(low_y/2)-base_y;
            if(x>=-1&&x<=16&&y>=-1&&y<=16)liquid[std::size_t(y+1)*18+std::size_t(x+1)]=true;
        }
    }
    // Beyond the map edge the nearest tile repeats, so a liquid sheet meeting
    // the edge keeps its straight border.
    const auto at=[&](int x,int y){
        x=(std::clamp)(x+base_x,0,map_x-1)-base_x;y=(std::clamp)(y+base_y,0,map_y-1)-base_y;
        return liquid[std::size_t(y+1)*18+std::size_t(x+1)];
    };
    for(int y=0;y<16;++y)for(int x=0;x<16;++x){
        std::uint16_t mask=0;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)
            if(at(x+dx,y+dy))mask|=std::uint16_t(1u<<unsigned((dy+1)*3+(dx+1)));
        masks[std::size_t(y)*16+std::size_t(x)]=mask;
    }
}

OwnedTerrainGeometryValues frozen_owned_terrain_geometry(const TerrainCpuAsset& source) {
    OwnedTerrainGeometryValues values;
    values.revision = 1;
    values.layer_shifts.assign(source.objects.size(), 0.f);
    values.layer_shift_range.assign(source.objects.size(), {0.f, 0.f});
    return values;
}

bool owned_terrain_command_bounds(const TerrainCpuAsset& source,
    const OwnedTerrainCommand& command, const OwnedTerrainGeometryValues& values,
    NativeWorldBounds& bounds) noexcept {
    bounds = {};
    if (!values.revision || command.source_sector >= source.sectors.size() ||
        command.subpart_mask >= 16u) return false;
    const auto& sector=source.sectors[command.source_sector];
    if (command.material >= sector.materials.size()) return false;
    const auto& material=sector.materials[command.material];
    for (unsigned part=0;part<4;++part) if (command.subpart_mask&(1u<<part)) {
        const auto& range=material.subparts[part];
        if (range.tile_count && range.envelopes.empty()) return false;
        for (const auto& group:range.envelopes) {
            auto box=group.bounds;
            // The shader lifts layer vertices by their live shift; the box
            // covers the map's whole shift range instead of the current value.
            if (group.layer_object != UINT16_MAX && group.layer_object < values.layer_shift_range.size()) {
                box.minimum[2]+=values.layer_shift_range[group.layer_object][0];
                box.maximum[2]+=values.layer_shift_range[group.layer_object][1];
            }
            const float wave=values.animation.waves_enabled
                ? std::fabs(values.animation.wind_force)*group.maximum_wave_multiplier : 0;
            const std::array<float,3> padding{wave*3,wave*3,wave*.25f};
            for (unsigned axis=0;axis<3;++axis) {
                box.minimum[axis]-=padding[axis]; box.maximum[axis]+=padding[axis];
                box.minimum[axis]=next_float(box.minimum[axis],-INFINITY);
                box.maximum[axis]=next_float(box.maximum[axis],INFINITY);
                if (!finite_value(box.minimum[axis]) || !finite_value(box.maximum[axis]) ||
                    box.minimum[axis]>box.maximum[axis]) return false;
            }
            if (!bounds.valid) { bounds.minimum=box.minimum;bounds.maximum=box.maximum;bounds.valid=true; }
            else for (unsigned axis=0;axis<3;++axis) {
                bounds.minimum[axis]=(std::min)(bounds.minimum[axis],box.minimum[axis]);
                bounds.maximum[axis]=(std::max)(bounds.maximum[axis],box.maximum[axis]);
            }
        }
    }
    return true;
}

std::uint64_t owned_terrain_command_structure(const TerrainCpuAsset& source,
    const OwnedTerrainCommand& command, const OwnedTerrainGeometryValues& values) noexcept {
    if (command.source_sector>=source.sectors.size()) return 0;
    const auto& sector=source.sectors[command.source_sector];
    if (command.material>=sector.materials.size()) return 0;
    std::uint64_t hash=1469598103934665603ull;
    const auto mix=[&](std::uint64_t value) { hash^=value;hash*=1099511628211ull; };
    for (unsigned part=0;part<4;++part) if (command.subpart_mask&(1u<<part))
        for (const auto& group:sector.materials[command.material].subparts[part].envelopes) {
            if (group.layer_object==UINT16_MAX) continue;
            // Layer vertices decode at their authored rest heights with the
            // static wind noise; live layer shifts (water levels) and the
            // depths they imply are applied by the shader.
            mix(group.layer_object);mix(values.noise_revision);
        }
    return hash ? hash : 1;
}

namespace {

bool finite_inputs(const OwnedTerrainFrameInputs& inputs) noexcept {
    for (float value : inputs.terrain_origin)
        if (!finite_value(value)) return false;
    for (float value : inputs.camera_position)
        if (!finite_value(value)) return false;
    for (float value : inputs.lod_distances)
        if (!finite_value(value) || value < 0.0f) return false;
    for (float value : inputs.object_to_clip)
        if (!finite_value(value)) return false;
    if (inputs.game_visibility_rows.size() > 65536u) return false;
    return inputs.source_generation != 0 && inputs.source_revision != 0 &&
        inputs.world_frame_serial != 0 && inputs.maximum_lod >= 0 &&
        inputs.maximum_lod <= 2;
}

bool game_visibility_admits(const OwnedTerrainFrameInputs& inputs,
                            std::uint32_t map_x,
                            std::uint32_t map_y) noexcept {
    if (!inputs.game_visibility_domain_present) return true;
    const std::int64_t row = static_cast<std::int64_t>(map_y) -
        static_cast<std::int64_t>(inputs.game_visibility_min_y);
    if (row < 0 || static_cast<std::uint64_t>(row) >=
            inputs.game_visibility_rows.size())
        return false;
    const auto& span = inputs.game_visibility_rows[
        static_cast<std::size_t>(row)];
    const std::int64_t x = map_x;
    return span[0] <= span[1] && x >= span[0] && x <= span[1];
}

bool bounds_visible(const TerrainBounds& bounds,
                    const std::array<float, 16>& transform) noexcept {
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (!finite_value(bounds.minimum[axis]) ||
            !finite_value(bounds.maximum[axis]) ||
            bounds.minimum[axis] > bounds.maximum[axis])
            return false;
    std::uint32_t outside = 0x3fu;
    for (std::uint32_t corner = 0; corner < 8u; ++corner) {
        const float x = (corner & 1u) ? bounds.maximum[0] : bounds.minimum[0];
        const float y = (corner & 2u) ? bounds.maximum[1] : bounds.minimum[1];
        const float z = (corner & 4u) ? bounds.maximum[2] : bounds.minimum[2];
        const float clip_x = transform[0] * x + transform[1] * y +
            transform[2] * z + transform[3];
        const float clip_y = transform[4] * x + transform[5] * y +
            transform[6] * z + transform[7];
        const float clip_z = transform[8] * x + transform[9] * y +
            transform[10] * z + transform[11];
        const float clip_w = transform[12] * x + transform[13] * y +
            transform[14] * z + transform[15];
        if (!finite_value(clip_x) || !finite_value(clip_y) ||
            !finite_value(clip_z) || !finite_value(clip_w))
            return false;
        std::uint32_t mask = 0;
        if (clip_x < -clip_w) mask |= 1u;
        if (clip_x > clip_w) mask |= 2u;
        if (clip_y < -clip_w) mask |= 4u;
        if (clip_y > clip_w) mask |= 8u;
        if (clip_z < 0.0f) mask |= 16u;
        if (clip_z > clip_w) mask |= 32u;
        outside &= mask;
    }
    return outside == 0;
}

std::uint8_t select_lod(const TerrainSectorCpuAsset& sector,
                        const OwnedTerrainFrameInputs& inputs) noexcept {
    if (inputs.force_full_detail) return 0;
    const float x = inputs.terrain_origin[0] +
        (static_cast<float>(sector.map_x) + 0.5f) * 32.0f -
        inputs.camera_position[0];
    const float y = inputs.terrain_origin[1] +
        (static_cast<float>(sector.map_y) + 0.5f) * 32.0f -
        inputs.camera_position[1];
    const float z = inputs.terrain_origin[2] + sector.average_z -
        inputs.camera_position[2];
    const float distance_squared = x * x + y * y + z * z;
    if (distance_squared < inputs.lod_distances[0] * inputs.lod_distances[0] &&
        inputs.maximum_lod <= 0)
        return 0;
    if (distance_squared < inputs.lod_distances[1] * inputs.lod_distances[1] &&
        inputs.maximum_lod <= 1)
        return 1;
    return 2;
}

bool trusted_source(const TerrainCpuAsset& source) noexcept {
    const std::uint64_t sector_count =
        static_cast<std::uint64_t>(source.sector_count_x) *
        source.sector_count_y;
    return source.immutable_descriptor_validated &&
        source.immutable_descriptor_validation_count == 1u &&
        source.sector_count_x && source.sector_count_y &&
        sector_count == source.sectors.size() && source.texture_count &&
        source.texture_count <= 32766u &&
        source.texture_resources.size() ==
            static_cast<std::size_t>(source.texture_count) + 2u &&
        source.tiles_per_texture_side && source.tiles_per_texture;
}

} // namespace

bool owned_terrain_frame_inputs_equal(
    const OwnedTerrainFrameInputs& left,
    const OwnedTerrainFrameInputs& right) noexcept {
    return left.source_generation == right.source_generation &&
        left.source_revision == right.source_revision &&
        left.world_frame_serial == right.world_frame_serial &&
        left.maximum_lod == right.maximum_lod &&
        left.force_full_detail == right.force_full_detail &&
        left.runtime_texture_count == right.runtime_texture_count &&
        left.terrain_render_state == right.terrain_render_state &&
        left.game_visibility_fingerprint == right.game_visibility_fingerprint &&
        left.game_visibility_domain_present ==
            right.game_visibility_domain_present &&
        left.game_visibility_min_y == right.game_visibility_min_y &&
        left.game_visibility_rows == right.game_visibility_rows &&
        left.dynamic_state_fingerprint == right.dynamic_state_fingerprint &&
        std::memcmp(left.terrain_origin.data(), right.terrain_origin.data(),
                    sizeof(left.terrain_origin)) == 0 &&
        std::memcmp(left.camera_position.data(), right.camera_position.data(),
                    sizeof(left.camera_position)) == 0 &&
        std::memcmp(left.lod_distances.data(), right.lod_distances.data(),
                    sizeof(left.lod_distances)) == 0 &&
        std::memcmp(left.object_to_clip.data(), right.object_to_clip.data(),
                    sizeof(left.object_to_clip)) == 0;
}

std::array<std::uint32_t, 5> owned_terrain_geometry_revision(
    const OwnedTerrainFramePacket& packet,
    const OwnedTerrainCommand& command,
    std::uint64_t structural_generation) noexcept {
    if (!packet.source ||
        command.source_sector >= packet.source->sectors.size())
        return {};
    const auto& sector = packet.source->sectors[command.source_sector];
    const auto mix = [](std::uint64_t& hash, std::uint64_t value) noexcept {
        for (std::uint32_t byte = 0; byte < 8u; ++byte) {
            hash ^= static_cast<std::uint8_t>(value >> (byte * 8u));
            hash *= 1099511628211ull;
        }
    };
    std::uint64_t identity_hash = 1469598103934665603ull;
    mix(identity_hash, packet.inputs.source_generation);
    mix(identity_hash, packet.inputs.source_revision);
    mix(identity_hash, sector.map_x);
    mix(identity_hash, sector.map_y);
    mix(identity_hash, command.material);
    std::uint64_t content_hash = identity_hash;
    mix(content_hash, structural_generation);
    mix(content_hash, command.lod);
    mix(content_hash, command.subpart_mask);
    mix(content_hash, command.lu_higher ? 1u : 0u);
    mix(content_hash, command.rd_higher ? 1u : 0u);
    return {
        static_cast<std::uint32_t>(identity_hash),
        static_cast<std::uint32_t>(identity_hash >> 32u),
        static_cast<std::uint32_t>(content_hash),
        static_cast<std::uint32_t>(content_hash >> 32u),
        static_cast<std::uint32_t>(command.lod) |
            (static_cast<std::uint32_t>(command.subpart_mask) << 8u) |
            (command.lu_higher ? 0x10000u : 0u) |
            (command.rd_higher ? 0x20000u : 0u)};
}

bool owned_terrain_receiver_bounds(const TerrainCpuAsset& source,
    const OwnedTerrainGeometryValues& values,
    const std::array<float,16>& world_to_clip,
    std::vector<NativeWorldBounds>& bounds) noexcept {
    bounds.clear();
    if (!trusted_source(source) || !values.revision) return false;
    try {
        for (std::uint32_t sector=0;sector<source.sectors.size();++sector) {
            const auto& materials=source.sectors[sector].materials;
            for (std::uint32_t material=0;material<materials.size();++material)
                for (unsigned part=0;part<4;++part) {
                    if (!materials[material].subparts[part].tile_count) continue;
                    OwnedTerrainCommand command{};
                    command.source_sector=sector;command.material=static_cast<std::uint16_t>(material);
                    command.subpart_mask=static_cast<std::uint8_t>(1u<<part);
                    NativeWorldBounds box{};
                    if (!owned_terrain_command_bounds(source,command,values,box)) return false;
                    if (box.valid && bounds_visible({box.minimum,box.maximum},world_to_clip))
                        bounds.push_back(box);
                }
        }
        return true;
    } catch (...) { bounds.clear();return false; }
}

bool select_owned_terrain_descriptor_counts(
    const TerrainMaterialDescriptor& descriptor, std::uint8_t lod,
    bool lu_higher, bool rd_higher, std::uint8_t subpart_mask,
    std::uint32_t& tile_count, std::uint32_t& index_count) noexcept {
    tile_count = 0;
    index_count = 0;
    if (lod >= 3u || subpart_mask >= 16u) return false;
    const std::uint32_t stitch = (lu_higher ? 1u : 0u) |
        (rd_higher ? 2u : 0u);
    for (std::uint32_t part = 0; part < 4u; ++part) {
        if (!(subpart_mask & (1u << part))) continue;
        tile_count += descriptor.subparts[part].tile_count;
        index_count += descriptor.subparts[part].index_counts[lod][stitch];
    }
    return true;
}

bool prepare_owned_terrain_frame(
    std::shared_ptr<const TerrainCpuAsset> source,
    const OwnedTerrainFrameInputs& inputs,
    OwnedTerrainFramePacket& packet) noexcept {
    packet.status = OwnedTerrainPacketStatus::pending_source;
    packet.source.reset();
    packet.sectors.clear();
    packet.spatial_parts.clear();
    packet.base_commands.clear();
    packet.transparent_commands.clear();
    packet.spatial_object_to_world={};
    packet.spatial_geometry_revision=0;
    packet.spatial_bounds_computations=0;
    packet.spatial_bounds_reuses=0;
    packet.spatial_front_end_reuses=0;
    packet.spatial_receiver_reuses=0;
    packet.spatial_caster_reuses=0;
    packet.spatial_record_reuses=0;
    packet.preparation_count = 1;
    packet.base_submission_count = 0;
    packet.transparent_reuse_count = 0;
    packet.base_requested = false;
    packet.base_submitted = false;
    packet.failed_after_claim = false;
    packet.spatial_ready = false;
    try {
        packet.inputs = inputs;
    } catch (...) {
        packet.inputs = {};
        packet.status = OwnedTerrainPacketStatus::invalid;
        return false;
    }
    packet.source = std::move(source);
    if (!packet.source) {
        packet.status = OwnedTerrainPacketStatus::pending_source;
        return true;
    }
    if (!finite_inputs(inputs) || !trusted_source(*packet.source) ||
        inputs.runtime_texture_count != packet.source->texture_count) {
        packet.status = OwnedTerrainPacketStatus::invalid;
        return false;
    }
    try {
        packet.sectors.resize(packet.source->sectors.size());
        for (std::size_t index = 0; index < packet.source->sectors.size(); ++index) {
            const auto& source_sector = packet.source->sectors[index];
            auto& state = packet.sectors[index];
            state.source_sector = static_cast<std::uint32_t>(index);
            state.visible = game_visibility_admits(
                inputs, source_sector.map_x, source_sector.map_y) &&
                bounds_visible(source_sector.bounds, inputs.object_to_clip);
            for (std::size_t part = 0; part < state.subpart_visible.size(); ++part)
                state.subpart_visible[part] = state.visible && bounds_visible(
                    source_sector.subpart_bounds[part], inputs.object_to_clip);
            state.lod = select_lod(source_sector, inputs);
        }
        const auto state_at = [&](int x, int y) -> const OwnedTerrainSectorState* {
            if (x < 0 || y < 0 || x >= static_cast<int>(packet.source->sector_count_x) ||
                y >= static_cast<int>(packet.source->sector_count_y))
                return nullptr;
            return &packet.sectors[static_cast<std::size_t>(y) *
                packet.source->sector_count_x + static_cast<std::size_t>(x)];
        };
        for (auto& state : packet.sectors) {
            if (!state.visible || state.lod >= 2u) continue;
            const auto& source_sector =
                packet.source->sectors[state.source_sector];
            if (state.lod == 0u) {
                const auto* up = state_at(static_cast<int>(source_sector.map_x),
                                          static_cast<int>(source_sector.map_y) - 1);
                const auto* down = state_at(static_cast<int>(source_sector.map_x),
                                            static_cast<int>(source_sector.map_y) + 1);
                state.lu_higher = up && up->visible && up->lod == 1u;
                state.rd_higher = down && down->visible && down->lod == 1u;
            } else {
                const auto* left = state_at(static_cast<int>(source_sector.map_x) - 1,
                                            static_cast<int>(source_sector.map_y));
                const auto* right = state_at(static_cast<int>(source_sector.map_x) + 1,
                                             static_cast<int>(source_sector.map_y));
                state.lu_higher = left && left->visible && left->lod == 2u;
                state.rd_higher = right && right->visible && right->lod == 2u;
            }
        }
        const std::uint32_t opaque_materials = packet.source->texture_count + 2u;
        for (std::uint32_t material = 0; material < opaque_materials * 2u;
             ++material) {
            auto& commands = material < opaque_materials
                ? packet.base_commands : packet.transparent_commands;
            for (const auto& state : packet.sectors) {
                if (!state.visible) continue;
                const auto& source_sector =
                    packet.source->sectors[state.source_sector];
                if (material >= source_sector.materials.size()) {
                    packet.status = OwnedTerrainPacketStatus::invalid;
                    return false;
                }
                const auto& descriptor = source_sector.materials[material];
                OwnedTerrainCommand command{};
                command.source_sector = state.source_sector;
                command.material = static_cast<std::uint16_t>(material);
                command.lod = state.lod;
                command.lu_higher = state.lu_higher;
                command.rd_higher = state.rd_higher;
                for (std::size_t part = 0;
                     part < state.subpart_visible.size(); ++part) {
                    if (!state.subpart_visible[part]) continue;
                    command.subpart_mask |= static_cast<std::uint8_t>(1u << part);
                }
                if (!select_owned_terrain_descriptor_counts(
                        descriptor, state.lod, state.lu_higher,
                        state.rd_higher, command.subpart_mask,
                        command.tile_count, command.index_count)) {
                    packet.status = OwnedTerrainPacketStatus::invalid;
                    return false;
                }
                if (!command.tile_count) continue;
                static constexpr std::uint32_t vertices_per_tile[]{9u, 6u, 4u};
                command.vertex_count = command.tile_count *
                    vertices_per_tile[state.lod];
                if (command.vertex_count > UINT16_MAX ||
                    command.index_count > 0x100000u) {
                    packet.status = OwnedTerrainPacketStatus::invalid;
                    return false;
                }
                commands.push_back(command);
            }
        }
        packet.status = OwnedTerrainPacketStatus::ready;
        return true;
    } catch (...) {
        packet.sectors.clear();
        packet.base_commands.clear();
        packet.transparent_commands.clear();
        packet.status = OwnedTerrainPacketStatus::invalid;
        return false;
    }
}

bool owned_terrain_render_command(const OwnedTerrainFramePacket& packet,
    std::uint32_t sector_index,std::uint32_t material,std::uint8_t subparts,
    OwnedTerrainCommand& command) noexcept {
    command={};
    if(!packet.source || sector_index>=packet.sectors.size() ||
       sector_index>=packet.source->sectors.size() || !subparts || (subparts&~15u))return false;
    const auto& source=*packet.source;const auto& sector=source.sectors[sector_index];
    const auto& state=packet.sectors[sector_index];
    if(material>=sector.materials.size() || material>UINT16_MAX || state.lod>2)return false;
    command.source_sector=sector_index;command.material=static_cast<std::uint16_t>(material);
    command.subpart_mask=subparts;command.lod=state.lod;
    const auto higher=[&](int x,int y) {
        return x>=0 && y>=0 && x<int(source.sector_count_x) && y<int(source.sector_count_y) &&
            packet.sectors[std::size_t(y)*source.sector_count_x+x].lod==state.lod+1;
    };
    if(state.lod<2) {
        command.lu_higher=state.lod==0 ? higher(int(sector.map_x),int(sector.map_y)-1)
            : higher(int(sector.map_x)-1,int(sector.map_y));
        command.rd_higher=state.lod==0 ? higher(int(sector.map_x),int(sector.map_y)+1)
            : higher(int(sector.map_x)+1,int(sector.map_y));
    }
    if(!select_owned_terrain_descriptor_counts(sector.materials[material],command.lod,
        command.lu_higher,command.rd_higher,subparts,command.tile_count,command.index_count))return false;
    constexpr std::uint32_t vertices[]{9,6,4};
    command.vertex_count=command.tile_count*vertices[command.lod];
    return command.vertex_count<=UINT16_MAX && command.index_count<=0x100000u;
}

bool owned_terrain_far_shadow_command(const OwnedTerrainFramePacket& packet,
    std::uint32_t sector_index,std::uint32_t material,std::uint8_t subparts,
    OwnedTerrainCommand& command) noexcept {
    if(!owned_terrain_render_command(packet,sector_index,material,subparts,command))return false;
    command.lod=2;command.lu_higher=false;command.rd_higher=false;
    const auto& descriptor=packet.source->sectors[sector_index].materials[material];
    if(!select_owned_terrain_descriptor_counts(descriptor,command.lod,false,false,subparts,
        command.tile_count,command.index_count))return false;
    command.vertex_count=command.tile_count*4u;
    return command.vertex_count<=UINT16_MAX && command.index_count<=0x100000u;
}

bool owned_terrain_world_to_clip(const std::array<float,16>& object_to_clip,
    const std::array<float,16>& object_to_world,
    std::array<float,16>& world_to_clip) noexcept {
    world_to_clip={};
    for(float value:object_to_clip)if(!finite_value(value))return false;
    for(float value:object_to_world)if(!finite_value(value))return false;
    // General 4x4 inverse of the placement by cofactors (double precision).
    const auto& m=object_to_world;
    double inverse[16];
    inverse[0]=m[5]*double(m[10])*m[15]-m[5]*double(m[11])*m[14]-m[9]*double(m[6])*m[15]+m[9]*double(m[7])*m[14]+m[13]*double(m[6])*m[11]-m[13]*double(m[7])*m[10];
    inverse[4]=-m[4]*double(m[10])*m[15]+m[4]*double(m[11])*m[14]+m[8]*double(m[6])*m[15]-m[8]*double(m[7])*m[14]-m[12]*double(m[6])*m[11]+m[12]*double(m[7])*m[10];
    inverse[8]=m[4]*double(m[9])*m[15]-m[4]*double(m[11])*m[13]-m[8]*double(m[5])*m[15]+m[8]*double(m[7])*m[13]+m[12]*double(m[5])*m[11]-m[12]*double(m[7])*m[9];
    inverse[12]=-m[4]*double(m[9])*m[14]+m[4]*double(m[10])*m[13]+m[8]*double(m[5])*m[14]-m[8]*double(m[6])*m[13]-m[12]*double(m[5])*m[10]+m[12]*double(m[6])*m[9];
    inverse[1]=-m[1]*double(m[10])*m[15]+m[1]*double(m[11])*m[14]+m[9]*double(m[2])*m[15]-m[9]*double(m[3])*m[14]-m[13]*double(m[2])*m[11]+m[13]*double(m[3])*m[10];
    inverse[5]=m[0]*double(m[10])*m[15]-m[0]*double(m[11])*m[14]-m[8]*double(m[2])*m[15]+m[8]*double(m[3])*m[14]+m[12]*double(m[2])*m[11]-m[12]*double(m[3])*m[10];
    inverse[9]=-m[0]*double(m[9])*m[15]+m[0]*double(m[11])*m[13]+m[8]*double(m[1])*m[15]-m[8]*double(m[3])*m[13]-m[12]*double(m[1])*m[11]+m[12]*double(m[3])*m[9];
    inverse[13]=m[0]*double(m[9])*m[14]-m[0]*double(m[10])*m[13]-m[8]*double(m[1])*m[14]+m[8]*double(m[2])*m[13]+m[12]*double(m[1])*m[10]-m[12]*double(m[2])*m[9];
    inverse[2]=m[1]*double(m[6])*m[15]-m[1]*double(m[7])*m[14]-m[5]*double(m[2])*m[15]+m[5]*double(m[3])*m[14]+m[13]*double(m[2])*m[7]-m[13]*double(m[3])*m[6];
    inverse[6]=-m[0]*double(m[6])*m[15]+m[0]*double(m[7])*m[14]+m[4]*double(m[2])*m[15]-m[4]*double(m[3])*m[14]-m[12]*double(m[2])*m[7]+m[12]*double(m[3])*m[6];
    inverse[10]=m[0]*double(m[5])*m[15]-m[0]*double(m[7])*m[13]-m[4]*double(m[1])*m[15]+m[4]*double(m[3])*m[13]+m[12]*double(m[1])*m[7]-m[12]*double(m[3])*m[5];
    inverse[14]=-m[0]*double(m[5])*m[14]+m[0]*double(m[6])*m[13]+m[4]*double(m[1])*m[14]-m[4]*double(m[2])*m[13]-m[12]*double(m[1])*m[6]+m[12]*double(m[2])*m[5];
    inverse[3]=-m[1]*double(m[6])*m[11]+m[1]*double(m[7])*m[10]+m[5]*double(m[2])*m[11]-m[5]*double(m[3])*m[10]-m[9]*double(m[2])*m[7]+m[9]*double(m[3])*m[6];
    inverse[7]=m[0]*double(m[6])*m[11]-m[0]*double(m[7])*m[10]-m[4]*double(m[2])*m[11]+m[4]*double(m[3])*m[10]+m[8]*double(m[2])*m[7]-m[8]*double(m[3])*m[6];
    inverse[11]=-m[0]*double(m[5])*m[11]+m[0]*double(m[7])*m[9]+m[4]*double(m[1])*m[11]-m[4]*double(m[3])*m[9]-m[8]*double(m[1])*m[7]+m[8]*double(m[3])*m[5];
    inverse[15]=m[0]*double(m[5])*m[10]-m[0]*double(m[6])*m[9]-m[4]*double(m[1])*m[10]+m[4]*double(m[2])*m[9]+m[8]*double(m[1])*m[6]-m[8]*double(m[2])*m[5];
    const double determinant=m[0]*inverse[0]+m[1]*inverse[4]+m[2]*inverse[8]+m[3]*inverse[12];
    if(!finite_value(determinant) || std::fabs(determinant)<1e-12)return false;
    for(unsigned row=0;row<4;++row)
        for(unsigned column=0;column<4;++column) {
            double value=0;
            for(unsigned k=0;k<4;++k)
                value+=double(object_to_clip[row*4+k])*inverse[k*4+column]/determinant;
            world_to_clip[row*4+column]=static_cast<float>(value);
            if(!finite_value(world_to_clip[row*4+column]))return false;
        }
    return true;
}

bool prepare_owned_terrain_spatial_parts(OwnedTerrainFramePacket& packet,
    const OwnedTerrainGeometryValues& values,
    const std::array<float,16>& object_to_world,
    OwnedTerrainPreparedResources* prepared) noexcept {
    packet.spatial_parts.clear();packet.spatial_ready=false;
    packet.spatial_geometry_revision=0;packet.spatial_object_to_world={};
    packet.spatial_bounds_computations=0;packet.spatial_bounds_reuses=0;
    packet.spatial_front_end_reuses=0;packet.spatial_receiver_reuses=0;
    packet.spatial_caster_reuses=0;packet.spatial_record_reuses=0;
    if(packet.status!=OwnedTerrainPacketStatus::ready || !packet.source || !values.revision ||
       values.animation.waves_enabled>1 || !finite_value(values.animation.wind_force) ||
       !std::all_of(values.layer_shift_range.begin(),values.layer_shift_range.end(),
           [](const std::array<float,2>& value){return finite_value(value[0])&&finite_value(value[1]);}) ||
       !std::all_of(object_to_world.begin(),object_to_world.end(),
           [](float value){return finite_value(value);}))return false;
    // A degenerate placement (an uncaptured zero matrix) would collapse every
    // world bound to one point and let GPU culling drop the whole terrain.
    const auto& m=object_to_world;
    const double determinant=
        double(m[0])*(double(m[5])*m[10]-double(m[6])*m[9])-
        double(m[1])*(double(m[4])*m[10]-double(m[6])*m[8])+
        double(m[2])*(double(m[4])*m[9]-double(m[5])*m[8]);
    if(!finite_value(determinant) || std::fabs(determinant)<1e-12)return false;
    try {
        const auto& source=*packet.source;
        const bool waves=values.animation.waves_enabled!=0;
        const float force=waves ? std::fabs(values.animation.wind_force) : 0.f;
        const bool local_reusable=prepared && prepared->spatial_local_ready &&
            prepared->spatial_source_generation==packet.inputs.source_generation &&
            prepared->spatial_source_revision==packet.inputs.source_revision &&
            prepared->spatial_layer_shift_range==values.layer_shift_range &&
            prepared->spatial_waves_enabled==waves &&
            prepared->spatial_absolute_wind_force==force;
        const bool world_reusable=local_reusable && prepared->spatial_world_ready &&
            prepared->spatial_object_to_world==object_to_world;
        if(world_reusable) {
            packet.spatial_parts=prepared->spatial_parts;
            packet.spatial_object_to_world=object_to_world;
            packet.spatial_geometry_revision=values.revision;
            packet.spatial_ready=true;
            return true;
        }
        std::vector<OwnedTerrainSpatialPart> spatial;
        const auto materials=source.texture_count+2u;
        if(local_reusable) {
            spatial=prepared->spatial_parts;
            for(auto& value:spatial)
                if(!value.local_bounds.valid ||
                   !transform_terrain_bounds(value.local_bounds,object_to_world,value.world_bounds))return false;
        } else for(std::uint32_t material=0;material<materials*2u;++material)
            for(std::uint32_t sector=0;sector<source.sectors.size();++sector) {
                const auto& descriptors=source.sectors[sector].materials;
                if(material>=descriptors.size() || material>UINT16_MAX)return false;
                for(unsigned part=0;part<4;++part) {
                    if(!descriptors[material].subparts[part].tile_count)continue;
                    OwnedTerrainCommand command{};
                    command.source_sector=sector;command.material=static_cast<std::uint16_t>(material);
                    command.subpart_mask=static_cast<std::uint8_t>(1u<<part);
                    OwnedTerrainSpatialPart value{};
                    value.source_sector=sector;value.material=command.material;
                    value.subpart_mask=command.subpart_mask;
                    if(!owned_terrain_command_bounds(source,command,values,value.local_bounds) ||
                       !value.local_bounds.valid ||
                       !transform_terrain_bounds(value.local_bounds,object_to_world,value.world_bounds))return false;
                    if(spatial.size()>=UINT32_MAX)return false;
                    spatial.push_back(value);
                    ++packet.spatial_bounds_computations;
                }
            }
        std::vector<OwnedTerrainSpatialPart> prepared_spatial;
        std::vector<std::array<float,2>> prepared_layers;
        if(prepared) {
            // Complete every throwing copy before publishing a new ready key.
            // The swaps below cannot expose new bounds under the old key if
            // allocation fails, and previously copied frame packets stay pinned.
            prepared_spatial=spatial;
            prepared_layers=values.layer_shift_range;
            prepared->spatial_parts.swap(prepared_spatial);
            prepared->spatial_layer_shift_range.swap(prepared_layers);
            prepared->spatial_object_to_world=object_to_world;
            prepared->spatial_absolute_wind_force=force;
            prepared->spatial_source_generation=packet.inputs.source_generation;
            prepared->spatial_source_revision=packet.inputs.source_revision;
            prepared->spatial_waves_enabled=waves;
            prepared->spatial_local_ready=true;
            prepared->spatial_world_ready=true;
        }
        packet.spatial_parts=std::move(spatial);
        packet.spatial_object_to_world=object_to_world;
        packet.spatial_geometry_revision=values.revision;
        packet.spatial_ready=true;
        return true;
    } catch (...) {
        packet.spatial_parts.clear();packet.spatial_ready=false;
        packet.spatial_geometry_revision=0;packet.spatial_bounds_computations=0;
        return false;
    }
}

bool prepare_owned_terrain_render_commands(OwnedTerrainFramePacket& packet) noexcept {
    if(packet.status!=OwnedTerrainPacketStatus::ready || !packet.source || !packet.spatial_ready ||
       !packet.spatial_geometry_revision)return false;
    try {
        packet.base_commands.clear();packet.transparent_commands.clear();
        const auto threshold=packet.source->texture_count+2u;
        for(std::size_t begin=0;begin<packet.spatial_parts.size();) {
            const auto source_sector=packet.spatial_parts[begin].source_sector;
            const auto material=packet.spatial_parts[begin].material;
            // A piece with any visible part draws all of its parts, so its
            // mesh key (sector, material, parts, LOD) stays fixed while the
            // screen edge moves across the sector instead of naming a new
            // mesh for every visible-part combination (frame accounting
            // 2026-10-14: pan frames created colour meshes for new
            // combinations). The extra parts are off screen and clipped; the
            // base terrain set registers its pieces with all parts the same way.
            std::uint8_t subpart_mask=0;
            bool visible=false;
            std::size_t end=begin;
            while(end<packet.spatial_parts.size()) {
                const auto& spatial=packet.spatial_parts[end];
                if(spatial.source_sector!=source_sector || spatial.material!=material)break;
                ++packet.spatial_front_end_reuses;++packet.spatial_bounds_reuses;
                subpart_mask=static_cast<std::uint8_t>(subpart_mask|spatial.subpart_mask);
                if(!visible && bounds_visible({spatial.local_bounds.minimum,spatial.local_bounds.maximum},
                    packet.inputs.object_to_clip))visible=true;
                ++end;
            }
            if(!visible || !subpart_mask) { begin=end;continue; }
            OwnedTerrainCommand command{};
            if(!owned_terrain_render_command(packet,source_sector,material,
                subpart_mask,command))return false;
            command.spatial_index=static_cast<std::uint32_t>(begin);
            (material<threshold?packet.base_commands:packet.transparent_commands).push_back(command);
            begin=end;
        }
        return true;
    } catch (...) { packet.base_commands.clear();packet.transparent_commands.clear();return false; }
}

bool owned_terrain_spatial_bounds(OwnedTerrainFramePacket& packet,
    const OwnedTerrainCommand& command,NativeWorldBounds& local,
    NativeWorldBounds& world) noexcept {
    local={};world={};
    if(!packet.spatial_ready || !packet.spatial_geometry_revision || !command.subpart_mask ||
       (command.subpart_mask&~15u))return false;
    const bool single=(command.subpart_mask&(command.subpart_mask-1u))==0;
    if(single && command.spatial_index<packet.spatial_parts.size()) {
        const auto& value=packet.spatial_parts[command.spatial_index];
        if(value.source_sector!=command.source_sector || value.material!=command.material ||
           value.subpart_mask!=command.subpart_mask)return false;
        local=value.local_bounds;world=value.world_bounds;++packet.spatial_bounds_reuses;
        return true;
    }
    if(command.spatial_index>=packet.spatial_parts.size())return false;
    std::uint8_t found_mask=0;
    for(std::size_t index=command.spatial_index,scanned=0;
        index<packet.spatial_parts.size() && scanned<4;++index,++scanned) {
        const auto& value=packet.spatial_parts[index];
        if(value.source_sector!=command.source_sector || value.material!=command.material)break;
        if(!(command.subpart_mask&value.subpart_mask))continue;
        found_mask=static_cast<std::uint8_t>(found_mask|value.subpart_mask);
        include_terrain_bounds(local,value.local_bounds);
    }
    if(found_mask!=command.subpart_mask)return false;
    if(!local.valid || !transform_terrain_bounds(local,packet.spatial_object_to_world,world))return false;
    ++packet.spatial_bounds_reuses;
    return true;
}

OwnedTerrainRangeAction decide_owned_terrain_range(
    OwnedTerrainFramePacket& packet,
    const OwnedTerrainFrameInputs& current,
    int material_from, int material_to) noexcept {
    if (packet.status == OwnedTerrainPacketStatus::pending_source)
        return OwnedTerrainRangeAction::omit_pending_source;
    if (packet.status != OwnedTerrainPacketStatus::ready ||
        packet.failed_after_claim)
        return OwnedTerrainRangeAction::omit_invalid;
    if (!owned_terrain_frame_inputs_equal(packet.inputs, current))
        return OwnedTerrainRangeAction::omit_stale;
    const int threshold = packet.source
        ? static_cast<int>(packet.source->texture_count + 2u) : 0;
    if (!packet.source || material_from < 0 || material_to < material_from ||
        material_to >= threshold * 2)
        return OwnedTerrainRangeAction::omit_invalid;
    if (material_from == 0 && material_to == threshold - 1 &&
        !packet.base_requested) {
        packet.base_requested = true;
        return OwnedTerrainRangeAction::submit_base;
    }
    if (material_from == threshold && material_to == threshold * 2 - 1 &&
        packet.base_submitted) {
        ++packet.transparent_reuse_count;
        return OwnedTerrainRangeAction::submit_transparent;
    }
    return OwnedTerrainRangeAction::omit_wrong_order;
}

bool complete_owned_terrain_base(OwnedTerrainFramePacket& packet, bool success) noexcept {
    if (!packet.base_requested || packet.base_submitted ||
        packet.status != OwnedTerrainPacketStatus::ready || packet.failed_after_claim) return false;
    if (!success) { packet.failed_after_claim = true; return false; }
    packet.base_submitted = true;
    ++packet.base_submission_count;
    return true;
}

} // namespace owned
