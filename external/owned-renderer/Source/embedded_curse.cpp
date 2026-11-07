#include "renderer.h"
#include "embedded_curse.h"
#include <stdexcept>
namespace owned {namespace curse {
constexpr float CrystalTipZ=11.3f,ShieldRadius=3.4f,ShieldFoot=.045f;
// Reuse the existing renderer geometry and native/HLSL consumer unchanged.
#include "embedded_arena_geometry.inl"
float Clamp(float v,float lo,float hi){return (std::max)(lo,(std::min)(hi,v));}
void SpellRing(Mesh&m,Vec center,float radius,float width){
 unsigned start=(unsigned)m.uv.size();for(int i=0;i<=96;i++){float a=i*6.283185307f/96;for(float r:{radius-width,radius})m.Add(center+Vec{r*std::cos(a),r*std::sin(a),.025f},0,0);}
 for(int i=0;i<96;i++){unsigned k=start+i*2;m.Quad(k,k+2,k+3,k+1);}
}
static void CurseRune(Mesh&m,Vec p,float radius,float time){
 for(int i=0;i<12;i++){
  float a=i*.523598776f;Vec r={std::cos(a),std::sin(a),0},t={-r.y,r.x,0};
  Vec c=p+r*radius+Vec{0,0,.06f};unsigned n=m.Add(c-r*.16f-t*.10f,0,0);
  m.Add(c+r*.16f-t*.10f,1,0);m.Add(c+r*.16f+t*.10f,1,1);m.Add(c-r*.16f+t*.10f,0,1);m.Quad(n,n+1,n+2,n+3);
 }
 SpellRing(m,p,radius,.035f+.008f*std::sin(time*4));
}
HRESULT Submit(Renderer& renderer,Mesh& mesh,const EmbeddedCurseInput& input,
    const FixedState& frame,ID3D11ShaderResourceView* texture,std::uint32_t color,
    EmbeddedCurseCache& cache,unsigned slot,bool lit,bool opaque=false,bool additive=false,const std::vector<Vec>* authored=nullptr) {
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
    if(authored){if(authored->size()!=count)return E_INVALIDARG;for(unsigned i=0;i<count;++i){const auto n=(*authored)[i];auto& g=normals[i/4];g.x[i%4]=n.x;g.y[i%4]=n.y;g.z[i%4]=n.z;g.w[i%4]=1;}}
    else if(lit)for(unsigned i=0;i<mesh.indices.size();i+=3) {
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

}
HRESULT Renderer::draw_embedded_curse(const EmbeddedCurseInput& input,const FixedState& frame,ID3D11ShaderResourceView* texture) try {
 SCENE_ENTRY("curse_visual");using namespace curse;
 if(!input.world_epoch||input.boss_id!=666666||!input.ground)return E_INVALIDARG;
 // Authored beams/channels require 720101's material. No plain replacement.
 const bool beams=input.active||input.threat==2||input.threat==4||
  (input.ward.present&&input.ward_at>0&&input.tick-input.ward_at>=0&&input.tick-input.ward_at<14);
 if(beams&&!texture)return HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
 const auto revision=native_static_content_revision_;
 struct Restore {std::uint64_t& value;std::uint64_t saved;~Restore(){value=saved;}} restore{native_static_content_revision_,revision};
 ++embedded_curse_cache_.serial;unsigned slot=0;HRESULT failure=S_OK;
 auto draw=[&](Mesh& mesh,ID3D11ShaderResourceView* material,unsigned color){
  if(mesh.indices.empty())return;if(slot>=embedded_curse_cache_.slots.size()){failure=E_INVALIDARG;return;}
  const auto hr=Submit(*this,mesh,input,frame,material,color,embedded_curse_cache_,slot++,false,false,true);
  if(FAILED(hr)&&SUCCEEDED(failure))failure=hr;
 };
 const int tick=input.tick,type=input.threat;const float time=tick/18.f,age=tick-input.threat_born;
 const Vec camera={input.camera[0],input.camera[1],input.camera[2]};const auto& boss=input.boss;const auto& mage=input.mage;
 auto ground=[&](float x,float y){const float z=input.ground(x,y);if(!std::isfinite(z))throw std::runtime_error("Curse ground query failed");return Vec{x,y,z+.035f};};
 auto plain=[&](Mesh& mesh,unsigned color){draw(mesh,nullptr,color);};
 auto beam=[&](Vec a,Vec b,float width,unsigned color){Mesh mesh;VfxBeam(mesh,a,b,camera,width,time*.7f,time*.7f+3);draw(mesh,texture,color);};
 if(input.active==1){
  // A low translucent seal outlines the playable area without hiding actors.
  Vec corners[]={ground(54,46),ground(106,46),ground(106,102),ground(54,102)};
  for(int i=0;i<4;i++){beam(corners[i]+Vec{0,0,.12f},corners[(i+1)%4]+Vec{0,0,.12f},.10f,0x759974ed);}
 }
 if(mage.present&&input.active==1){
  Vec m=Vec{mage.base[0],mage.base[1],mage.base[2]};
  if(input.mage_mode==2){for(int i=0;i<6;i++){float a=i*1.0472f+time*.4f;Vec c=m+Vec{1.1f*std::cos(a),1.1f*std::sin(a),.7f};beam(c,c+Vec{0,0,.7f},.055f,0xa088c9ff);}}
  if(input.mage_mode==3){Mesh pad;CurseRune(pad,ground(input.pad_x,input.pad_y),1.5f,time);plain(pad,0xa6ffdb9f);}
  for(int i=0;i<12;i++)if(input.enemies[i].channel){const auto& u=input.enemies[i];if(u.present)for(int strand=0;strand<2;strand++){auto flow=Ribbon(Vec{u.base[0],u.base[1],u.base[2]}+Vec{0,0,1.7f},m+Vec{0,0,1.5f},camera,time,strand);draw(flow,texture,0x667e54ce);}}
 }
 if(type==1||type==3){
  Vec p=ground(input.x,input.y);float radius=type==1?3.f:5.f;Mesh m;CurseRune(m,p,radius,time);plain(m,age<36?0x99ff7058:0xd0ff493d);
  float fraction=Clamp(age/(type==1?float(72):90.f),0,1);Mesh countdown;SpellRing(countdown,p,radius*(1-fraction),.045f);plain(countdown,0xafffa48b);
 }
 if(type==2){
  Vec a=ground(input.lance_ax,input.lance_ay)+Vec{0,0,1.2f},b=ground(input.lance_bx,input.lance_by)+Vec{0,0,1.2f};
  if(age<72){beam(a,b,.07f,0x9ed2a5ff);}
  else {float t=Clamp((age-72)/36,0,1);Vec p=a+(b-a)*t;beam(p-(b-a)*.09f,p,.2f,0xf0e7b8ff);}
 }
 if(type==4&&boss.present){Vec a=Vec{boss.base[0],boss.base[1],boss.base[2]};Vec d=Unit(Vec{input.x-a.x,input.y-a.y,0});for(float side:{-1.f,1.f}){Vec r=Spin(d,{0,0,1},side*.98f);beam(a+Vec{0,0,.1f},a+r*10+Vec{0,0,.1f},.07f,0xafff988e);}}
 float impactAge=tick-input.impact_at;if(input.impact_at>0&&impactAge>=0&&impactAge<18){Mesh ring;SpellRing(ring,ground(input.impact_x,input.impact_y),.4f+impactAge*.25f,.08f);plain(ring,((unsigned)(160*(1-impactAge/18))<<24)|0xcf96fa);}
 float wardAge=tick-input.ward_at;
 if(input.ward_at>0&&wardAge>=0&&wardAge<14){const auto& u=input.ward;if(u.present){Vec p={u.base[0],u.base[1],u.base[2]};unsigned alpha=(unsigned)(200*(1-wardAge/14));Mesh m;CurseRune(m,p,.8f+wardAge*.07f,time);plain(m,(alpha<<24)|0xffe6ac);for(int i=0;i<3;i++){float a=i*2.0944f;Vec c=p+Vec{.6f*std::cos(a),.6f*std::sin(a),.5f};beam(c,c+Vec{0,0,1.5f},.08f,(alpha<<24)|0xffe6ac);}}}
 return failure;
}catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(const std::exception&){return E_INVALIDARG;}
}
