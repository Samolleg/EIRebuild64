#pragma once
#include "../PhysicsFxCore.h"
#include "engine_bridge.h"
#include <cmath>
namespace owned { class Renderer; struct FixedState; }

namespace OwnedRender {
struct DebrisMesh {
    std::array<owned::NativePackedGroup,2> positions{},normals{};
    std::array<owned::NativeVertexComponent,24> components{};
    std::array<std::uint16_t,24> indices{};
    std::array<owned::NativeTextureCoordinate,24> uv{};
    explicit DebrisMesh(unsigned shape){
        const auto p=PhysicsFx::StoneVertices(shape,1.f);
        for(unsigned i=0;i<6;++i){auto& g=positions[i/4];g.x[i%4]=p[i].x;g.y[i%4]=p[i].y;g.z[i%4]=p[i].z;g.w[i%4]=1;}
        const unsigned faces[8][3]={{4,0,2},{4,2,1},{4,1,3},{4,3,0},{5,2,0},{5,1,2},{5,3,1},{5,0,3}};
        for(unsigned f=0;f<8;++f){auto a=p[faces[f][0]],b=p[faces[f][1]],c=p[faces[f][2]];
            const float x=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),y=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),z=(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
            const float length=std::sqrt(x*x+y*y+z*z);auto& n=normals[f/4];n.x[f%4]=x/length;n.y[f%4]=y/length;n.z[f%4]=z/length;
            // Each facet uses an inset triangle in one of stones00's four
            // atlas cells. Separate UVs preserve hard edges without atlas bleed.
            const unsigned cell=(f+shape)%4;
            const float u=.5f*float(cell%2),v=.5f*float(cell/2);
            const owned::NativeTextureCoordinate corners[3]={{u+.035f,v+.045f},{u+.455f,v+.075f},{u+.22f,v+.455f}};
            for(unsigned j=0;j<3;++j){const auto index=std::uint16_t(f*3+j);
                uv[index]=corners[(j+shape)%3];components[index]={std::uint16_t(faces[f][j]),std::uint16_t(f),index};indices[index]=index;}
        }
    }
};
inline std::array<float,16> DebrisMatrix(const PhysicsFx::Stone& s){
    const auto q=s.pose.rotation;const float x=q[0],y=q[1],z=q[2],w=q[3],r=s.radius;
    return {(1-2*y*y-2*z*z)*r,(2*x*y-2*z*w)*r,(2*x*z+2*y*w)*r,s.pose.position.x,
        (2*x*y+2*z*w)*r,(1-2*x*x-2*z*z)*r,(2*y*z-2*x*w)*r,s.pose.position.y,
        (2*x*z-2*y*w)*r,(2*y*z+2*x*w)*r,(1-2*x*x-2*y*y)*r,s.pose.position.z,0,0,0,1};
}
inline owned::NativeGeometryView DebrisView(const DebrisMesh& mesh,const PhysicsFx::Stone& stone,const std::array<float,16>& clip,unsigned char fog){
    owned::NativeGeometryView v;
    v.positions=mesh.positions.data();v.position_groups=2;v.normals=mesh.normals.data();v.normal_groups=2;
    v.components=mesh.components.data();v.component_count=24;v.indices=mesh.indices.data();v.index_count=24;
    v.texture_coordinates=mesh.uv.data();v.texture_coordinate_count=24;
    v.cache_identity=reinterpret_cast<std::uintptr_t>(&mesh);v.geometry_revision_valid=true;v.geometry_revision={2,0,0,0,0};
    v.semantic_kind=owned::SemanticKind::figure;v.ordering=owned::NativeOrdering::preserve;
    v.lighting.enabled=1;v.lighting.object_to_world=DebrisMatrix(stone);v.lighting.material_alpha=stone.alpha;
    v.lighting.material_diffuse={.75f+.25f*float((stone.color>>16)&255)/255,.75f+.25f*float((stone.color>>8)&255)/255,.75f+.25f*float(stone.color&255)/255};
    v.fog=fog;v.default_specular=std::uint32_t(fog)<<24;
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)for(unsigned k=0;k<4;++k)
        v.object_to_clip[row*4+col]+=clip[row*4+k]*v.lighting.object_to_world[k*4+col];
    return v;
}
struct E1VisualInputs;
bool E1DrawPhysicsDebris(owned::Renderer&,const owned::FixedState&,const E1VisualInputs&) noexcept;
}
