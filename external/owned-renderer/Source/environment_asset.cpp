#include "environment_asset.h"
#include "owned_resource_runtime.h"
#include "../../../dependencies/cgltf/v1.15/cgltf.h"
#include "../../../SDKs/JSON/json.hpp"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>

namespace owned::environment {
namespace {
using Json=nlohmann::json;
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error("environment GLB: "+why);}
Json metadata(const cgltf_extras& extras){require(extras.data!=nullptr,"missing extras.ei");return Json::parse(extras.data).at("ei");}
std::array<float,3> vector3(const Json& j){
    require(j.is_array()&&j.size()==3,"vec3");std::array<float,3> v{};
    for(unsigned a=0;a<3;++a){v[a]=j[a].get<float>();require(std::isfinite(v[a]),"non-finite vec3");}return v;
}
std::string lower(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c+='a'-'A';return s;}
float weight(unsigned endpoint,const std::array<float,3>& shape){float v=1;for(unsigned a=0;a<3;++a)v*=endpoint&(1u<<a)?shape[a]:1-shape[a];return v;}
std::vector<float> values(const cgltf_accessor* accessor,unsigned width) {
    if(!accessor)return {};
    require(accessor->count<=3000000&&cgltf_num_components(accessor->type)==width,"accessor width/budget");
    std::vector<float> result(accessor->count*width);
    require(cgltf_accessor_unpack_floats(accessor,result.data(),result.size())==result.size(),"sparse/dense accessor decode");return result;
}
void pack(std::vector<NativePackedGroup>& out,unsigned groups,unsigned shape,unsigned i,const std::array<float,3>& p){
    auto& g=out[shape*groups+i/4];unsigned k=i%4;g.x[k]=p[0];g.y[k]=p[1];g.z[k]=p[2];g.w[k]=1;
}
FigureBounds bounds(const Json& j){
    FigureBounds b;b.minimum=vector3(j.at("min"));b.maximum=vector3(j.at("max"));
    for(unsigned a=0;a<3;++a)require(b.minimum[a]<=b.maximum[a],"inverted collider");return b;
}
#ifndef ENVIRONMENT_ASSET_CHECK
ResourceBlob read(ResourceStore& store,const std::string& key) {
    ResourceBlob b;ResourceFailure f;require(store.resolve(key,b,f)&&b.bytes,"resource "+key+": "+f.message);return b;
}
#endif
}

std::shared_ptr<const Asset> decode(const ResourceBlob& blob,const std::string& visual) {
    require(blob.bytes&&blob.bytes->size()<=64u*1024*1024,"GLB byte budget");
    cgltf_options options{};cgltf_data* raw=nullptr;
    require(cgltf_parse(&options,blob.bytes->data(),blob.bytes->size(),&raw)==cgltf_result_success,"parse");
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> owner(raw,cgltf_free);auto& d=*raw;
    require(d.file_type==cgltf_file_type_glb&&d.buffers_count==1&&!d.buffers[0].uri,"embedded GLB buffer");
    require(!d.skins_count&&!d.animations_count&&d.nodes_count<=4096,"static scene: no skins/animations");
    for(size_t i=0;i<d.nodes_count;++i){size_t depth=0;for(auto* n=&d.nodes[i];n;n=n->parent)require(++depth<=64,"cyclic/deep node hierarchy");}
    for(size_t i=0;i<d.extensions_required_count;++i){
        const std::string name=d.extensions_required[i];
        require(name=="MSFT_texture_dds"||name=="KHR_materials_unlit"||name=="KHR_materials_emissive_strength"||name=="KHR_texture_transform","required extension "+name);
    }
    require(cgltf_load_buffers(&options,&d,nullptr)==cgltf_result_success&&cgltf_validate(&d)==cgltf_result_success,"accessors/buffers");
    const auto meta=metadata(d.asset.extras);require(meta.at("profile")=="environment.v1"&&meta.at("kind")=="environment","profile/kind");
    const bool forms=meta.value("shape",std::string())=="trilinear8";
    auto* scene=d.scene?d.scene:(d.scenes_count==1?d.scenes:nullptr);require(scene,"unambiguous scene");
    auto out=std::make_shared<Asset>();out->identity=blob.identity;
    out->minimum={FLT_MAX,FLT_MAX,FLT_MAX};out->maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    if(meta.contains("wind")){
        const auto& w=meta.at("wind");out->plant=w.value("enabled",true);out->wind_root=vector3(w.value("root",Json::array({0,0,0})));
        if(w.contains("shapes"))for(const auto& s:w.at("shapes"))out->wind.push_back(vector3(s));
        else out->wind.push_back({w.at("trunkRadius").get<float>(),w.at("trunkHeight").get<float>(),w.at("height").get<float>()});
        require(out->wind.size()==1||(forms&&out->wind.size()==8),"wind endpoint count");
        for(auto s:out->wind)require(std::all_of(s.begin(),s.end(),[](float f){return std::isfinite(f)&&f>=0;})&&s[2]>0&&s[1]<=s[2],"wind dimensions");
    }
    auto model=std::make_shared<ModelCpuAsset>();model->visual_reference=visual;model->model_container=blob.identity;
    ModelNodeCpuAsset root;root.name="environment_root";root.bone_positions.assign(out->wind_root.begin(),out->wind_root.end());
    model->nodes.push_back(std::move(root));model->links.push_back({"environment_root","",UINT_MAX});out->transforms=model;
    std::map<const cgltf_material*,std::shared_ptr<const Material>> materials;
    std::set<const TextureCpuAsset*> countedTextures;
    std::set<const cgltf_node*> visited;std::set<std::string> names;
    std::function<void(const cgltf_node*,unsigned)> visit=[&](const cgltf_node* node,unsigned depth){
        require(depth<64&&visited.insert(node).second,"cyclic/repeated scene node");require(!node->skin,"rigid node");
        if(node->mesh) {
            require(node->name&&*node->name,"named mesh node");const std::string logical=node->name;
            require(logical.find('#')==std::string::npos&&names.insert(lower(logical)).second,"unique logical node name");
            const auto nm=metadata(node->extras);const auto c=nm.at("collision");
            const auto role=c.at("role").get<std::string>();
            require(role=="none"||role=="solid"||role=="crown"||role=="walkable","collision role");
            std::vector<FigureBounds> cb;
            if(c.contains("shapes"))for(const auto& b:c.at("shapes"))cb.push_back(bounds(b));else cb.push_back(bounds(c));
            require(cb.size()==1||(forms&&cb.size()==8),"collision shape count");
            float m[16]{};cgltf_node_transform_world(node,m);
            const std::array<float,9> cof={m[5]*m[10]-m[6]*m[9],m[6]*m[8]-m[4]*m[10],m[4]*m[9]-m[5]*m[8],
                m[9]*m[2]-m[10]*m[1],m[10]*m[0]-m[8]*m[2],m[8]*m[1]-m[9]*m[0],
                m[1]*m[6]-m[2]*m[5],m[2]*m[4]-m[0]*m[6],m[0]*m[5]-m[1]*m[4]};
            const float det=m[0]*cof[0]+m[1]*cof[1]+m[2]*cof[2];require(std::isfinite(det)&&std::abs(det)>1e-12f,"singular transform");
            for(size_t pi=0;pi<node->mesh->primitives_count;++pi) {
                require(out->parts.size()<128,"part budget");const auto& p=node->mesh->primitives[pi];
                require(p.type==cgltf_primitive_type_triangles&&!p.has_draco_mesh_compression,"plain triangles");
                require(p.targets_count==0||(forms&&p.targets_count==7),"shape requires seven endpoint-delta morph targets");
                const unsigned shapes=p.targets_count?8:1;
                for(size_t a=0;a<p.attributes_count;++a)require(p.attributes[a].type==cgltf_attribute_type_position||p.attributes[a].type==cgltf_attribute_type_normal||
                    p.attributes[a].type==cgltf_attribute_type_texcoord||p.attributes[a].type==cgltf_attribute_type_tangent,"prepare vertex colours before export");
                const auto* pos=cgltf_find_accessor(&p,cgltf_attribute_type_position,0);
                const auto* nor=cgltf_find_accessor(&p,cgltf_attribute_type_normal,0);
                auto& mat=materials[p.material];if(!mat) {
                    mat=material(p.material,blob.identity,d);
                    for(const auto& t:mat->textures)if(t&&countedTextures.insert(t->asset.get()).second)out->texture_bytes+=t->asset->source->size();
                    require(out->texture_bytes<=128u*1024*1024,"decoded texture budget");
                }
                const auto* uv=cgltf_find_accessor(&p,cgltf_attribute_type_texcoord,int(mat->uv_set));
                require(pos&&nor&&pos->type==cgltf_type_vec3&&nor->type==cgltf_type_vec3&&pos->count==nor->count&&pos->count&&pos->count<=65532,"position/normal budget");
                require(!uv||(uv->type==cgltf_type_vec2&&uv->count==pos->count),"UV0 count");
                Part part;part.logical_name=logical;part.name=node->mesh->primitives_count==1?logical:logical+"#"+std::to_string(pi);
                part.collision=cb;part.minimum=cb.front().minimum;part.maximum=cb.front().maximum;
                part.role=role=="solid"?Part::Role::solid:role=="walkable"?Part::Role::walkable:role=="crown"?Part::Role::crown:Part::Role::none;
                part.solid=part.role==Part::Role::solid;part.passability=c.value("passability",role=="crown"||role=="none"?2u:0u);
                part.opacity=c.value("opacity",role=="crown"||role=="none"?2u:0u);
                require(part.passability<=3&&part.opacity<=3,"collision passability/opacity");
                part.foliage=nm.value("foliage",role=="crown");
                part.render=nm.value("render",true);
                part.pbr=mat;part.texture=mat->textures[0]->asset;
                if(p.material) {
                    const auto& mr=p.material->pbr_metallic_roughness;
                    require(uv||(!mr.base_color_texture.texture&&!mr.metallic_roughness_texture.texture&&!p.material->normal_texture.texture&&
                        !p.material->occlusion_texture.texture&&!p.material->emissive_texture.texture),"textured primitive requires UV0");
                }
                auto legacy=std::make_shared<MaterialCpuAsset>();legacy->source=MaterialValueSource::completed_native;
                legacy->diffuse=legacy->ambient={1,1,1,1};legacy->specular={0,0,0,1};legacy->emissive={0,0,0,1};part.material=legacy;
                auto mesh=std::make_shared<FigureCpuAsset>();mesh->identity=blob.identity;mesh->identity.canonical_name+="#environment/"+part.name;
                mesh->signature=shapes==1?0x31474946u:0x38474946u;mesh->shape_count=shapes;
                const unsigned count=unsigned(pos->count);mesh->position_groups=mesh->normal_groups=(count+3)/4;
                mesh->positions.resize(shapes*mesh->position_groups);mesh->normals.resize(shapes*mesh->normal_groups);
                mesh->components.resize(count);mesh->texture_coordinates.resize(uv?count:1);
                const auto indexCount=p.indices?p.indices->count:pos->count;
                require(indexCount&&indexCount%3==0&&indexCount<=3000000,"indices budget");
                const auto sparseIndices=p.indices&&p.indices->is_sparse?values(p.indices,1):std::vector<float>{};
                for(float index:sparseIndices)require(std::isfinite(index)&&index>=0&&index<float(count)&&std::floor(index)==index,"sparse index range");
                for(size_t k=0;k<indexCount;++k){const auto index=sparseIndices.empty()?(p.indices?cgltf_accessor_read_index(p.indices,k):k):cgltf_size(sparseIndices[k]);require(index<count,"index range");mesh->indices.push_back(std::uint16_t(index));}
                if(det<0)for(size_t k=0;k<mesh->indices.size();k+=3)std::swap(mesh->indices[k+1],mesh->indices[k+2]);
                const auto basePositions=values(pos,3),baseNormals=values(nor,3),texcoords=values(uv,2);
                for(unsigned shape=0;shape<shapes;++shape) {
                    const cgltf_accessor *dp=nullptr,*dn=nullptr;
                    if(shape)for(size_t a=0;a<p.targets[shape-1].attributes_count;++a){const auto& attr=p.targets[shape-1].attributes[a];
                        require(attr.type==cgltf_attribute_type_position||attr.type==cgltf_attribute_type_normal,"morph position/normal");
                        require(attr.data&&attr.data->type==cgltf_type_vec3&&attr.data->count==count,"morph accessor");
                        if(attr.type==cgltf_attribute_type_position)dp=attr.data;else dn=attr.data;
                    }
                    FigureBounds vb;vb.minimum={FLT_MAX,FLT_MAX,FLT_MAX};vb.maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
                    const auto positionDeltas=values(dp,3),normalDeltas=values(dn,3);
                    for(unsigned i=0;i<count;++i) {
                        float point[3]{},normal[3]{};
                        for(unsigned a=0;a<3;++a){point[a]=basePositions[i*3+a]+(dp?positionDeltas[i*3+a]:0);normal[a]=baseNormals[i*3+a]+(dn?normalDeltas[i*3+a]:0);}
                        const float x=m[0]*point[0]+m[4]*point[1]+m[8]*point[2]+m[12],y=m[1]*point[0]+m[5]*point[1]+m[9]*point[2]+m[13],z=m[2]*point[0]+m[6]*point[1]+m[10]*point[2]+m[14];
                        std::array<float,3> v{x-out->wind_root[0],-z-out->wind_root[1],y-out->wind_root[2]};
                        float n[3]{};for(unsigned a=0;a<3;++a)n[a]=(cof[a]*normal[0]+cof[3+a]*normal[1]+cof[6+a]*normal[2])/det;
                        const float length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);require(std::isfinite(length)&&length>1e-12f,"normal");
                        pack(mesh->positions,mesh->position_groups,shape,i,v);pack(mesh->normals,mesh->normal_groups,shape,i,{n[0]/length,-n[2]/length,n[1]/length});
                        float radius2=0;
                        for(unsigned a=0;a<3;++a){require(std::isfinite(v[a])&&std::abs(v[a])<100000,"vertex range");vb.minimum[a]=(std::min)(vb.minimum[a],v[a]);vb.maximum[a]=(std::max)(vb.maximum[a],v[a]);radius2+=v[a]*v[a];
                            out->minimum[a]=(std::min)(out->minimum[a],v[a]+out->wind_root[a]);out->maximum[a]=(std::max)(out->maximum[a],v[a]+out->wind_root[a]);}
                        mesh->shape_position_radii[shape]=(std::max)(mesh->shape_position_radii[shape],std::sqrt(radius2));
                        if(shape==0){mesh->components[i]={std::uint16_t(i),std::uint16_t(i),std::uint16_t(uv?i:0)};
                            if(uv){const float* t=texcoords.data()+i*2;require(std::isfinite(t[0])&&std::isfinite(t[1]),"UV");const auto& u=mat->uv;mesh->texture_coordinates[i]={u[0]*t[0]+u[1]*t[1]+u[2],u[3]*t[0]+u[4]*t[1]+u[5]};}}
                    }
                    vb.radius=mesh->shape_position_radii[shape];mesh->bounds.push_back(vb);
                }
                for(const auto& b:cb)for(unsigned a=0;a<3;++a){out->minimum[a]=(std::min)(out->minimum[a],b.minimum[a]);out->maximum[a]=(std::max)(out->maximum[a],b.maximum[a]);}
                // Private CPU endpoints. Rendering resolves these into ordinary
                // one-shape geometry before publishing an assembly.
                if(shapes==8)mesh->normal_groups*=8;
                out->geometry_bytes+=mesh->positions.size()*sizeof(NativePackedGroup)+mesh->normals.size()*sizeof(NativePackedGroup)+
                    mesh->components.size()*sizeof(NativeVertexComponent)+mesh->indices.size()*2+mesh->texture_coordinates.size()*sizeof(NativeTextureCoordinate);
                require(out->geometry_bytes<=64u*1024*1024,"decoded geometry budget");
                part.geometry=mesh;out->parts.push_back(std::move(part));
            }
        }
        for(size_t k=0;k<node->children_count;++k)visit(node->children[k],depth+1);
    };
    for(size_t k=0;k<scene->nodes_count;++k)visit(scene->nodes[k],0);
    require(!out->parts.empty(),"empty scene");
    return out;
}

#ifndef ENVIRONMENT_ASSET_CHECK
namespace {
std::string reference(ResourceStore& store,const std::string& visual) {
    ResourceBlob registryBlob;ResourceFailure failure;
    if(!store.resolve("models/registry.v1.json",registryBlob,failure)){require(failure.code==ResourceFailureCode::not_found,"registry read");return {};}
    static std::mutex lock;static std::map<std::string,Json> registries;
    std::lock_guard<std::mutex> guard(lock);const auto key=registryBlob.identity.key();
    if(!registries.count(key)){if(registries.size()>=16)registries.erase(registries.begin());registries.emplace(key,Json::parse(registryBlob.bytes->begin(),registryBlob.bytes->end()));}
    const auto& registry=registries.at(key);require(registry.at("version")==1,"registry version");
    if(!registry.contains("environment"))return {};
    const auto& entries=registry.at("environment");require(entries.is_object(),"environment registry");
    const auto it=entries.find(lower(visual));if(it==entries.end())return {};
    require(it->at("profile")=="environment.v1","registry profile");const auto path=it->at("resource").get<std::string>();
    require(path.rfind("models/environment/",0)==0&&path.size()>4&&path.substr(path.size()-4)==".glb"&&
        path.find("..")==std::string::npos&&path.find(':')==std::string::npos&&path.find('\\')==std::string::npos,"resource path");
    return path;
}
}
bool assigned(ResourceStore& store,const std::string& visual){return !reference(store,visual).empty();}
std::shared_ptr<const Asset> select(ResourceStore& store,const std::string& visual) {
    const auto path=reference(store,visual);if(path.empty())return {};
    ResourceLocation location;ResourceFailure failure;require(store.locate(path,location,failure),"resource "+path+": "+failure.message);
    auto identity=location.identity();const bool physical=identity.container_file_key!=0;
    if(physical)identity.container_incarnation=identity.container_file_key;
    const auto key=std::string(physical?"file:":"scope:")+identity.key()+"|"+lower(visual);
    static std::mutex lock;static std::map<std::string,std::weak_ptr<const Asset>> cache;
    std::lock_guard<std::mutex> guard(lock);if(auto found=cache[key].lock())return found;
    ResourceBlob blob;require(store.read(location,blob,failure),"resource read: "+failure.message);
    auto result=decode(blob,lower(visual));cache[key]=result;
    if(cache.size()>1024)for(auto i=cache.begin();i!=cache.end();)i=i->second.expired()?cache.erase(i):std::next(i);
    return result;
}
std::shared_ptr<const Asset> select(const std::string& visual) {
    static std::mutex lock;static ResourceStore store;static std::uint64_t generation=0;
    static std::map<std::string,std::weak_ptr<const Asset>> cache;static std::set<std::string> absent;
    std::lock_guard<std::mutex> guard(lock);ResourceStoreConfig mounts;std::uint64_t current=0;ResourceFailure failure;
    if(!owned_resource_runtime_mounts(mounts,&current))return {};
    if(current!=generation){store.close();require(store.open(mounts,failure),"mounts");generation=current;cache.clear();absent.clear();}
    const auto key=lower(visual);if(absent.count(key))return {};if(auto found=cache[key].lock())return found;
    auto result=select(store,key);if(result)cache[key]=result;else absent.insert(key);return result;
}
#endif
FigureBounds collision(const Part& p,const std::array<float,3>& shape) {
    FigureBounds b;if(p.collision.size()==1)return p.collision.front();
    require(p.collision.size()==8,"collision endpoints");
    for(unsigned s=0;s<8;++s){const float w=weight(s,shape);for(unsigned a=0;a<3;++a){b.minimum[a]+=w*p.collision[s].minimum[a];b.maximum[a]+=w*p.collision[s].maximum[a];}}
    for(unsigned a=0;a<3;++a){require(std::isfinite(b.minimum[a])&&std::isfinite(b.maximum[a]),"collision shape finite");if(b.minimum[a]>b.maximum[a])std::swap(b.minimum[a],b.maximum[a]);}
    return b;
}
bool selected(const Part& p,const std::vector<std::string>& names) {
    return names.empty()||std::any_of(names.begin(),names.end(),[&](const auto& s){return lower(s)==lower(p.name)||lower(s)==lower(p.logical_name);});
}
namespace {
// One immutable ordinary mesh per resource and exact authored form. Instance
// count does not multiply vertex buffers; no work is scheduled per frame.
std::shared_ptr<const FigureCpuAsset> resolved(const std::shared_ptr<const FigureCpuAsset>& source,const std::array<float,3>& shape) {
    require(std::all_of(shape.begin(),shape.end(),[](float v){return std::isfinite(v);}),"shape finite");
    if(source->shape_count==1)return source;
    require(source->shape_count==8&&source->normal_groups==source->position_groups*8,"source endpoint layout");
    std::array<std::uint32_t,3> bits{};for(unsigned a=0;a<3;++a){const float v=shape[a]==0?0:shape[a];std::memcpy(&bits[a],&v,4);}
    using Key=std::pair<const FigureCpuAsset*,decltype(bits)>;
    struct Entry {std::weak_ptr<const FigureCpuAsset> source,geometry;};
    static std::mutex lock;static std::map<Key,Entry> cache;
    std::lock_guard<std::mutex> guard(lock);const Key key{source.get(),bits};auto& entry=cache[key];
    if(entry.source.lock()==source)if(auto geometry=entry.geometry.lock())return geometry;
    auto output=std::make_shared<FigureCpuAsset>();
    output->identity=source->identity;output->identity.canonical_name+="/form/"+std::to_string(bits[0])+"/"+std::to_string(bits[1])+"/"+std::to_string(bits[2]);
    output->signature=0x31474946;output->shape_count=1;output->position_groups=output->normal_groups=source->position_groups;
    output->material_group=source->material_group;output->texture_number=source->texture_number;
    output->components=source->components;output->indices=source->indices;output->texture_coordinates=source->texture_coordinates;
    output->positions.resize(output->position_groups);output->normals.resize(output->normal_groups);
    std::array<float,8> weights{};for(unsigned s=0;s<8;++s)weights[s]=weight(s,shape);
    FigureBounds bounds;bounds.minimum={FLT_MAX,FLT_MAX,FLT_MAX};bounds.maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};float radius2=0;
    for(unsigned i=0;i<source->components.size();++i) {
        std::array<float,3> position{},normal{};
        for(unsigned s=0;s<8;++s) {
            const auto& p=source->positions[s*source->position_groups+i/4];const auto& n=source->normals[s*source->position_groups+i/4];const auto lane=i%4;
            const float xyz[]{p.x[lane],p.y[lane],p.z[lane]},nn[]{n.x[lane],n.y[lane],n.z[lane]};
            for(unsigned a=0;a<3;++a){position[a]+=xyz[a]*weights[s];normal[a]+=nn[a]*weights[s];}
        }
        float norm2=0,r2=0;for(unsigned a=0;a<3;++a){require(std::isfinite(position[a])&&std::abs(position[a])<100000,"resolved vertex range");norm2+=normal[a]*normal[a];r2+=position[a]*position[a];bounds.minimum[a]=(std::min)(bounds.minimum[a],position[a]);bounds.maximum[a]=(std::max)(bounds.maximum[a],position[a]);}
        require(std::isfinite(norm2),"resolved normal finite");if(norm2>1e-12f)for(auto& v:normal)v/=std::sqrt(norm2);
        else {const auto& n=source->normals[i/4];normal={n.x[i%4],n.y[i%4],n.z[i%4]};}
        pack(output->positions,output->position_groups,0,i,position);pack(output->normals,output->normal_groups,0,i,normal);radius2=(std::max)(radius2,r2);
    }
    bounds.radius=std::sqrt(radius2);output->bounds.push_back(bounds);output->shape_position_radii[0]=bounds.radius;
    entry={source,output};
    if(cache.size()>4096)for(auto it=cache.begin();it!=cache.end();)it=it->second.geometry.expired()?cache.erase(it):std::next(it);
    return output;
}
}
void reshape(VisualAssemblyAsset& out,const std::array<float,3>& shape) {
    for(auto& part:out.parts)if(part.environment_geometry)part.geometry=resolved(part.environment_geometry,shape);
    out.shape=shape;if(out.environment_wind.empty())return;
    std::array<float,3> trunk{};for(unsigned s=0;s<out.environment_wind.size();++s){const float w=out.environment_wind.size()==1?1:weight(s,shape);for(unsigned a=0;a<3;++a)trunk[a]+=w*out.environment_wind[s][a];}
    auto code=[](float v,float steps){require(std::isfinite(v),"wind shape finite");return unsigned((std::clamp)(std::ceil(v*steps),0.f,63.f));};
    out.sways=true;out.sway_root=0;out.sway_shape=(code(trunk[1],4)<<10)|(code(trunk[0],16)<<16)|((std::max)(code(trunk[2],4),1u)<<22);
}
std::shared_ptr<const VisualAssemblyAsset> assembly(const std::shared_ptr<const Asset>& asset,
    const std::vector<std::string>& selection,std::uint64_t revision,const std::array<float,3>& shape) {
    require(asset!=nullptr,"assembly asset");
    for(const auto& name:selection)require(std::any_of(asset->parts.begin(),asset->parts.end(),[&](const auto& p){return selected(p,{name});}),"unknown selected part "+name);
    auto out=std::make_shared<VisualAssemblyAsset>();out->skeleton=asset->transforms;out->revision=revision;out->shape=shape;
    out->environment_source=asset;
    for(const auto& source:asset->parts)if(source.render&&selected(source,selection)){
        VisualPartAsset part;part.name=source.name;part.environment_geometry=source.geometry;part.material=source.material;part.texture=source.texture;
        part.environment_material=source.pbr;part.environment_foliage=source.foliage;part.texture_source_name=source.texture->identity.canonical_name;
        part.skeleton_node=0;out->parts.push_back(std::move(part));
    }
    require(!out->parts.empty(),"empty selection");
    // Ordered BLEND must see this assembly's opaque depth/colour first.
    // Resolve once at assembly creation; do not sort every frame.
    std::stable_partition(out->parts.begin(),out->parts.end(),[](const auto& part){
        return !part.environment_material || part.environment_material->alpha!=2;
    });
    if(asset->plant) {
        out->environment_wind=asset->wind;
    }
    reshape(*out,shape);
    return out;
}
}
