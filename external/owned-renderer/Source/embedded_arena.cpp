#include "renderer.h"
#include "render_resources.h"
#include "embedded_arena.h"
#include <cstring>
#include <limits>

namespace owned {
namespace arena {
constexpr float CrystalTipZ=11.3f, ShieldRadius=3.4f, ShieldFoot=.045f;
#include "embedded_arena_geometry.inl"

struct FractureVertex {float x,y,z,u,v;};
struct FracturePart {
    unsigned role=0;
    Vec centre{};
    std::vector<FractureVertex> vertices;
};
struct Fragments {
    std::array<FracturePart,8> parts{};
    bool valid=false;
    Fragments() {
        const auto module=owned::render_resource_module();
        if(!module)return;
        const auto resource=FindResourceW(module,L"EID3D11_ARENA_FRAGMENTS_V1",MAKEINTRESOURCEW(10));
        if(!resource)return;
        const auto handle=LoadResource(module,resource);
        const auto* bytes=static_cast<const unsigned char*>(LockResource(handle));
        size_t remaining=SizeofResource(module,resource);
        auto read=[&](void* target,size_t count) {
            if(!bytes||count>remaining)return false;
            std::memcpy(target,bytes,count);bytes+=count;remaining-=count;return true;
        };
        char magic[4];unsigned version=0,count=0;
        if(!read(magic,4)||std::memcmp(magic,"AFV1",4)||!read(&version,4)||version!=1||!read(&count,4)||count!=parts.size())return;
        for(unsigned i=0;i<count;++i) {
            auto& part=parts[i];unsigned n=0;
            if(!read(&part.role,4)||part.role!=(i==0?0u:i<6?1u:2u)||!read(&n,4)||!n||n>12000||n%3||!read(&part.centre,12))return;
            if(!std::isfinite(part.centre.x)||!std::isfinite(part.centre.y)||!std::isfinite(part.centre.z))return;
            part.vertices.resize(n);
            if(!read(part.vertices.data(),n*sizeof(FractureVertex)))return;
            for(const auto& v:part.vertices)
                if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||!std::isfinite(v.u)||!std::isfinite(v.v))return;
        }
        valid=remaining==0;
    }
};

Mesh Debris(const Fragments& resource,Vec base,int tower,float elapsed,unsigned role) {
    Mesh mesh;
    for(unsigned index=1;index<resource.parts.size();++index) {
        const auto& p=resource.parts[index];if(p.role!=role)continue;
        const bool crystal=role==2;
        const float t=(std::max)(0.f,(std::min)(elapsed-.18f,2.4f));
        const float a=index*2.3999632f+tower*1.7f;
        Vec outward={std::cos(a),std::sin(a),0},axis=Unit(Vec{-outward.y,outward.x,.12f});
        const float angle=(std::min)(t,1.5f)*(crystal?1.6f:1.1f);
        float low=10000;
        std::vector<Vec> points;points.reserve(p.vertices.size());
        for(const auto& v:p.vertices) {
            const auto q=Spin(Vec{v.x,v.y,v.z}-p.centre,axis,angle);
            points.push_back(q);low=(std::min)(low,q.z);
        }
        const auto centre=base+Vec{p.centre.x,p.centre.y,(std::max)(p.centre.z+1.4f*t-4.9f*t*t,-low+.10f)}+
            outward*((crystal?1.8f:1.05f)*t);
        for(unsigned n=0;n<points.size();++n) {
            const auto& v=p.vertices[n];
            mesh.indices.push_back(std::uint16_t(mesh.Add(points[n]+centre,v.u,1-v.v)));
        }
    }
    return mesh;
}

HRESULT Submit(Renderer& renderer,Mesh& mesh,const EmbeddedArenaInput& input,
    const FixedState& frame,Renderer::EmbeddedEffectTexture texture,std::uint32_t color,
    EmbeddedArenaCache& cache,unsigned slot,bool lit,bool opaque=false,bool additive=false) {
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
    const auto hr=renderer.d3d12_backend()?renderer.queue_native(view,state,texture.image):
        renderer.queue_native_direct(view,state,texture.srv);
    if(FAILED(hr))return hr;
    return renderer.flush_native();
}
} // namespace arena

HRESULT Renderer::draw_embedded_arena(const EmbeddedArenaInput& input,const FixedState& frame,
    const std::array<Renderer::EmbeddedEffectTexture,8>& textures) try {
    SCENE_ENTRY("arena_visual");
    using namespace arena;
    if(!input.world_epoch||input.boss_id!=1000105||(!input.dome&&!input.flow))return E_INVALIDARG;
    if(!std::isfinite(input.dome_time)||!std::isfinite(input.flow_time)||!std::isfinite(input.wave_radius))return E_INVALIDARG;
    // This synchronous post-scene producer submits no world/static records.
    // Updating only its own transient meshes must not dirty the static scene's
    // cached draw layout. All pending world work was flushed at the host seam.
    const auto staticRevision=native_static_content_revision_;
    struct RestoreStaticRevision {
        std::uint64_t& value;std::uint64_t saved;
        ~RestoreStaticRevision(){value=saved;}
    } restoreStatic{native_static_content_revision_,staticRevision};
    const Vec base{input.base[0],input.base[1],input.base[2]},camera{input.camera[0],input.camera[1],input.camera[2]};
    HRESULT failure=S_OK;
    ++embedded_arena_cache_.serial;unsigned slot=0;
    auto draw=[&](Mesh mesh,Renderer::EmbeddedEffectTexture texture,unsigned color,bool lit=false,bool opaque=false,bool additive=false) {
        if(slot>=embedded_arena_cache_.slots.size()){failure=E_INVALIDARG;return;}
        const auto hr=Submit(*this,mesh,input,frame,texture,color,embedded_arena_cache_,slot++,lit,opaque,additive);
        if(FAILED(hr)&&SUCCEEDED(failure))failure=hr;
    };
    static const Fragments fragments;
    constexpr float xy[3][2]={{49,56},{58.107695f,26.741670f},{78.892305f,49.258330f}};
    if(input.flow)for(int i=0;i<3;++i)if(input.towers[i].broken) {
        const float age=input.flow_time-input.towers[i].break_time;
        const Vec origin{xy[i][0],xy[i][1],base.z};
        if(!fragments.valid){if(SUCCEEDED(failure))failure=HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND);}
        else for(int material=0;material<2;++material) {
            if(textures[2+i*2+material])draw(Debris(fragments,origin,i,age,material?2u:1u),textures[2+i*2+material],0xffffffff,true,true);
            else if(SUCCEEDED(failure))failure=HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
        }
        draw(FractureFlash(origin,age),nullptr,0xe0b1edff);
    }
    if(input.dome) {
        if(textures[0]) {
            draw(Hemisphere(base,camera,input.dome_time,1),nullptr,0x1457a6e8);
            draw(Hemisphere(base,camera,input.dome_time,2.4f),textures[0],0xa8b5e7ff);
        } else if(SUCCEEDED(failure))failure=HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    }
    if(input.flow&&textures[1]) {
        const auto end=ShieldApex(base,input.dome_time);
        for(int i=0;i<3;++i)if(input.towers[i].present&&!input.towers[i].broken) {
            const auto& p=input.towers[i].crystal;const Vec start{p[0],p[1],p[2]+CrystalTipZ};
            for(int strand=0;strand<3;++strand)draw(Ribbon(start,end,camera,input.flow_time,strand),textures[1],0xb0c1edff);
        }
        // The authoritative radius has already hit all heroes inside it.
        // Interpolation advances only the visible leading edge, capped at 56ms.
        float radius=input.wave_radius;
        if(radius>0)radius=(std::min)(120.f,radius+60.f*input.flow_fraction);
        if(input.wave>=1&&input.wave<=3) {
            auto fx=ElementalWave(base,camera,radius>0?radius:1.7f,input.wave,input.flow_time,radius<=0);
            if(input.wave==1) {
                draw(std::move(fx.bolts),textures[1],0xc8b5ddff,false,false,true);
                draw(std::move(fx.cores),nullptr,0xe0e6f5ff,false,false,true);
            }
            if(input.wave==3)for(unsigned b=0;b<8;++b)
                draw(std::move(fx.ice.opacity[b]),nullptr,(((b+1)*255/8)<<24)|0x00acebff,true);
        }
    } else if(input.flow&&SUCCEEDED(failure))failure=HRESULT_FROM_WIN32(ERROR_RESOURCE_NAME_NOT_FOUND);
    return failure;
} catch(const std::bad_alloc&) {
    return E_OUTOFMEMORY;
}
} // namespace owned
