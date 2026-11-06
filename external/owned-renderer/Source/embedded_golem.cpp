#include "renderer.h"
#include "embedded_golem.h"
#include <functional>
#include <stdexcept>
#include <cstring>
namespace owned {namespace golem {
constexpr float CrystalTipZ=11.3f,ShieldRadius=3.4f,ShieldFoot=.045f;
#include "embedded_arena_geometry.inl"
#include "embedded_golem_resource.inl"
struct GolemQuaternion {float w,x,y,z;};
Vec GolemRotate(GolemQuaternion q,Vec p){Vec v={q.x,q.y,q.z};return p+Cross(v,Cross(v,p)+p*q.w)*2.f;}
float Clamp(float v,float lo,float hi){return (std::max)(lo,(std::min)(hi,v));}
HRESULT Submit(Renderer& renderer,Mesh& mesh,const EmbeddedGolemInput& input,
    const FixedState& frame,ID3D11ShaderResourceView* texture,std::uint32_t color,
    EmbeddedGolemCache& cache,unsigned slot,bool lit,bool opaque=false,bool additive=false) {
    if(mesh.indices.empty())return S_OK;
    const auto count=mesh.uv.size();
    if(count>65535)return E_INVALIDARG;
    std::vector<NativePackedGroup> normals((count+3)/4);
    std::vector<NativeVertexComponent> components(count);
    auto point=[&](unsigned i) {
        const auto& group=mesh.plots[i/4];const auto lane=i%4;
        return Vec{group.x[lane],group.y[lane],group.z[lane]};
    };
    for(unsigned i=0;i<count;++i)components[i]={std::uint16_t(i),std::uint16_t(i),std::uint16_t(i)};
    if(lit)for(unsigned i=0;i<mesh.indices.size();i+=3) {
        const auto a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];
        const auto normal=Unit(Cross(point(b)-point(a),point(c)-point(a)));
        for(const auto v:{a,b,c}) {
            auto& n=normals[v/4];const auto lane=v%4;
            n.x[lane]=normal.x;n.y[lane]=normal.y;n.z[lane]=normal.z;n.w[lane]=1;
        }
    }
    // Explicit padding for all lanes, including the unused last group.
    mesh.Pad();
    for(auto& group:mesh.plots)for(int lane=0;lane<4;++lane)group.w[lane]=1;
    auto state=frame;
    state.render[D3DRENDERSTATE_ZENABLE]=TRUE;
    state.render[D3DRENDERSTATE_ZWRITEENABLE]=opaque?TRUE:FALSE;
    state.render[D3DRENDERSTATE_ZFUNC]=D3DCMP_LESSEQUAL;
    state.render[D3DRENDERSTATE_CULLMODE]=D3DCULL_NONE;
    state.render[D3DRENDERSTATE_ALPHATESTENABLE]=FALSE;
    state.render[D3DRENDERSTATE_ALPHABLENDENABLE]=opaque?FALSE:TRUE;
    state.render[D3DRENDERSTATE_SRCBLEND]=D3DBLEND_SRCALPHA;
    state.render[D3DRENDERSTATE_DESTBLEND]=additive?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA;
    state.render[D3DRENDERSTATE_SPECULARENABLE]=FALSE;
    state.stage[0][D3DTSS_COLOROP]=state.stage[0][D3DTSS_ALPHAOP]=texture?D3DTOP_MODULATE:D3DTOP_SELECTARG2;
    state.stage[0][D3DTSS_COLORARG1]=state.stage[0][D3DTSS_ALPHAARG1]=D3DTA_TEXTURE;
    state.stage[0][D3DTSS_COLORARG2]=state.stage[0][D3DTSS_ALPHAARG2]=D3DTA_DIFFUSE;
    NativeGeometryView view;
    view.positions=mesh.plots.data();view.position_groups=UINT(mesh.plots.size());
    view.normals=normals.data();view.normal_groups=UINT(normals.size());
    view.texture_coordinates=mesh.uv.data();view.texture_coordinate_count=UINT(count);
    view.components=components.data();view.component_count=UINT(count);
    view.indices=mesh.indices.data();view.index_count=UINT(mesh.indices.size());
    view.object_to_clip=input.world_to_clip;
    view.default_diffuse=color;view.default_specular=0xff000000;
    view.flags=4u|8u;
    view.lighting.enabled=lit?1:0;
    view.lighting.material_alpha=float(color>>24)/255.f;
    view.lighting.object_to_world={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    view.ordering=NativeOrdering::preserve;
    // Explicit revisions reuse the standard mesh upload pool. No per-frame
    // content hash or cache entry for every new animation time is needed.
    view.cache_identity=reinterpret_cast<std::uintptr_t>(&cache.slots[slot]);
    view.geometry_revision_valid=true;
    view.geometry_revision={std::uint32_t(input.world_epoch),std::uint32_t(input.world_epoch>>32),
        std::uint32_t(cache.serial),std::uint32_t(cache.serial>>32),slot};
    const auto hr=renderer.queue_native_direct(view,state,texture);
    if(FAILED(hr))return hr;
    return renderer.flush_native();
}

struct Producer {
 const EmbeddedGolemInput& input;const GolemResource& resource;const std::array<ID3D11ShaderResourceView*,3>& textures;
 std::function<void(Mesh&,ID3D11ShaderResourceView*,unsigned,bool,bool)> draw;
 Vec Base() const {return {input.base[0],input.base[1],input.base[2]};}
 const EmbeddedGolemNode* Node(int index) const {return input.nodes[index].valid?&input.nodes[index]:nullptr;}
 Vec NodePoint(const EmbeddedGolemNode* node,Vec point) const {
  if(!node)throw std::runtime_error("golem attachment missing");const auto& q=node->rotation;const auto& p=node->position;
  return Vec{p[0],p[1],p[2]}+GolemRotate({q[0],q[1],q[2],q[3]},point);
 }
 float GolemGround(float x,float y) const {if(!input.ground)throw std::runtime_error("golem ground query missing");float z=input.ground(x,y);if(!std::isfinite(z))throw std::runtime_error("golem ground query failed");return z;}
 Vec GolemGroundPosition(Vec relative) const {return {relative.x,relative.y,GolemGround(relative.x,relative.y)+relative.z};}
#include "embedded_golem_geometry.inl"
#include "embedded_golem_damage.inl"
};
} // namespace golem
HRESULT Renderer::draw_embedded_golem(const EmbeddedGolemInput& input,const FixedState& frame,const std::array<ID3D11ShaderResourceView*,3>& textures) try {
 SCENE_ENTRY("golem_visual");using namespace golem;
 if(!input.world_epoch||input.boss_id!=990400||(!input.active&&!input.walls&&input.death_born<=0)||!input.ground)return E_INVALIDARG;
 const auto revision=native_static_content_revision_;
 struct Restore {std::uint64_t& value;std::uint64_t saved;~Restore(){value=saved;}} restore{native_static_content_revision_,revision};
 HRESULT failure=S_OK;++embedded_golem_cache_.serial;unsigned slot=0;
 auto draw=[&](Mesh& mesh,ID3D11ShaderResourceView* texture,unsigned color,bool lit,bool additive) {
  if(mesh.indices.empty())return;if(slot>=embedded_golem_cache_.slots.size()){failure=E_INVALIDARG;return;}
  // Closed opaque shards/blades/physical arrows keep the source depth writes;
  // membranes, warning rings, missiles and density aura remain translucent.
  const bool opaque=!additive&&(color>>24)==255;
  auto hr=Submit(*this,mesh,input,frame,texture,color,embedded_golem_cache_,slot++,lit,opaque,additive);if(FAILED(hr)&&SUCCEEDED(failure))failure=hr;
 };
 Producer p{input,GolemResources(),textures,draw};const float time=input.tick/18.f;
 const float z=p.GolemGround(input.target_x,input.target_y);Vec corners[]={{28,26,0},{100,26,0},{100,62,0},{28,62,0}};
 struct Side{int index;float distance;};std::array<Side,4> sides{};Vec camera{input.camera[0],input.camera[1],input.camera[2]};
 for(int i=0;i<4;++i){auto d=(corners[i]+corners[(i+1)%4])*.5f-camera;sides[i]={i,Dot(d,d)};}
 std::sort(sides.begin(),sides.end(),[](const Side&a,const Side&b){return a.distance>b.distance;});
 if(input.active||input.walls)for(const auto& side:sides)for(int band=0;band<5;++band){unsigned alpha=band==4?18:band==3?30:42;
  auto wall=p.FinalWall(corners[side.index],corners[(side.index+1)%4],time,band,false);draw(wall,nullptr,(alpha<<24)|0xe6360b,false,false);
  if(textures[2]){auto web=p.FinalWall(corners[side.index],corners[(side.index+1)%4],time,band,true);draw(web,textures[2],((alpha+35)<<24)|0xff8a32,false,true);}
 }
 if(input.active&&input.phase==2){auto beacon=p.FinalBeacon({input.target_x,input.target_y,z},time);draw(beacon,nullptr,0xedff942b,false,true);}
 if(input.active||input.death_born>0){if(!GolemResources().valid||!textures[0]||!textures[1])return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);p.Fractures();if(input.active&&!input.dead)p.Stage(time);
  p.ReflectAuraGeometry(0,float(input.tick),camera,[&](Mesh& m,unsigned color){draw(m,nullptr,color,false,true);});
  p.ReflectGeometry(float(input.tick),[&](Mesh& m,unsigned color,bool additive){draw(m,nullptr,color,false,additive);});}
 return failure;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(const std::exception&){return E_INVALIDARG;}
}
