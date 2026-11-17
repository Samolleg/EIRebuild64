#include "environment_asset.h"
#include "owned_resource_runtime.h"
#include "../../../dependencies/cgltf/v1.15/cgltf.h"
#include "../../../SDKs/JSON/json.hpp"
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <mutex>
#include <set>
#include <stdexcept>

namespace owned::environment {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(std::string("environment GLB: ")+message);
}
Json metadata(const cgltf_extras& extras) {
    require(extras.data!=nullptr,"missing extras.ei metadata");
    return Json::parse(extras.data).at("ei");
}
std::array<float,3> vector3(const Json& value) {
    require(value.is_array() && value.size()==3,"expected vec3");
    std::array<float,3> result{};
    for(unsigned a=0;a<3;++a) { result[a]=value[a].get<float>(); require(std::isfinite(result[a]),"non-finite vec3"); }
    return result;
}
std::string lower(std::string value) {
    for(auto& c:value)if(c>='A'&&c<='Z')c+='a'-'A';
    return value;
}
std::array<float,3> point(const float* m,const float* p) {
    const float x=m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12];
    const float y=m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13];
    const float z=m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14];
    return {x,-z,y};
}
void pack(std::vector<NativePackedGroup>& groups,unsigned index,const std::array<float,3>& p) {
    auto& g=groups[index/4];unsigned k=index%4;
    g.x[k]=p[0];g.y[k]=p[1];g.z[k]=p[2];g.w[k]=1;
}
float srgb(float x) {return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1.f/2.4f)-.055f;}
#ifndef ENVIRONMENT_ASSET_CHECK
ResourceBlob read(ResourceStore& store,const std::string& key) {
    ResourceBlob blob;ResourceFailure failure;
    if(!store.resolve(key,blob,failure)||!blob.bytes)throw std::runtime_error("environment GLB: "+key+": "+failure.message);
    return blob;
}
#endif
}

std::shared_ptr<const Asset> decode(const ResourceBlob& blob,const std::string& visual) {
    require(blob.bytes && blob.bytes->size()<=64u*1024*1024,"missing/oversized GLB");
    cgltf_options options{};cgltf_data* raw=nullptr;
    require(cgltf_parse(&options,blob.bytes->data(),blob.bytes->size(),&raw)==cgltf_result_success,"parse");
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> owner(raw,cgltf_free);
    auto& data=*raw;
    require(data.file_type==cgltf_file_type_glb && data.buffers_count==1 && !data.buffers[0].uri,"embedded GLB buffer required");
    require(!data.skins_count && !data.animations_count,"rigid environment does not accept skins/animations");
    require(!data.extensions_required_count && !data.images_count && !data.textures_count,"first rigid slice: no extensions or texture maps");
    require(cgltf_load_buffers(&options,&data,nullptr)==cgltf_result_success && cgltf_validate(&data)==cgltf_result_success,"buffer/accessor validation");
    const auto meta=metadata(data.asset.extras);
    require(meta.at("profile")=="environment.v1" && meta.at("kind")=="environment","profile/kind");
    auto* scene=data.scene?data.scene:(data.scenes_count==1?data.scenes:nullptr);
    require(scene!=nullptr,"unambiguous scene required");
    auto output=std::make_shared<Asset>();output->identity=blob.identity;
    output->minimum={FLT_MAX,FLT_MAX,FLT_MAX};output->maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    auto model=std::make_shared<ModelCpuAsset>();model->visual_reference=visual;
    model->model_container=blob.identity;
    std::set<const cgltf_node*> visited;std::set<std::string> names;
    std::function<void(const cgltf_node*,unsigned)> visit;
    visit=[&](const cgltf_node* node,unsigned depth) {
        require(depth<64 && visited.insert(node).second,"cyclic/repeated scene node");
        require(!node->skin && !node->weights_count,"rigid node required");
        if(node->mesh) {
            require(output->parts.size()<128,"part budget");
            require(node->name && *node->name,"named mesh node required");
            const std::string name=node->name;
            require(names.insert(lower(name)).second,"duplicate part name");
            require(node->mesh->primitives_count==1 && !node->mesh->weights_count,"first rigid slice: one primitive per part, no morphs");
            const auto& primitive=node->mesh->primitives[0];
            require(primitive.type==cgltf_primitive_type_triangles && !primitive.targets_count && !primitive.has_draco_mesh_compression,"plain triangles required");
            for(cgltf_size a=0;a<primitive.attributes_count;++a)
                require(primitive.attributes[a].type==cgltf_attribute_type_position || primitive.attributes[a].type==cgltf_attribute_type_normal ||
                    primitive.attributes[a].type==cgltf_attribute_type_texcoord || primitive.attributes[a].type==cgltf_attribute_type_tangent,
                    "first rigid slice: unsupported vertex attribute");
            const auto* positions=cgltf_find_accessor(&primitive,cgltf_attribute_type_position,0);
            const auto* normals=cgltf_find_accessor(&primitive,cgltf_attribute_type_normal,0);
            require(positions && normals && positions->type==cgltf_type_vec3 && normals->type==cgltf_type_vec3 &&
                    positions->count==normals->count && positions->count>0 && positions->count<=65532,"position/normal or vertex budget");
            const auto count=static_cast<unsigned>(positions->count);
            const auto indexCount=primitive.indices?primitive.indices->count:positions->count;
            require(indexCount && indexCount%3==0 && indexCount<=3000000,"triangle index count");
            Part part;part.name=name;
            const auto collision=metadata(node->extras).at("collision");
            const auto role=collision.at("role").get<std::string>();
            require(role=="solid" || role=="none","first rigid slice collision role: solid/none");
            part.solid=role=="solid";part.minimum=vector3(collision.at("min"));part.maximum=vector3(collision.at("max"));
            for(unsigned a=0;a<3;++a)require(part.minimum[a]<=part.maximum[a],"inverted collider");
            auto mesh=std::make_shared<FigureCpuAsset>();mesh->identity=blob.identity;
            mesh->signature=0x31474946u; // shared packed single-shape layout, never serialized as FIG
            mesh->identity.canonical_name+="#environment/"+name;
            mesh->shape_count=1;mesh->position_groups=mesh->normal_groups=(count+3)/4;
            mesh->positions.resize(mesh->position_groups);mesh->normals.resize(mesh->normal_groups);
            mesh->texture_coordinates.push_back({0,0});mesh->components.resize(count);
            float m[16]{};cgltf_node_transform_world(node,m);
            // Inverse transpose, including authored non-uniform scale.
            const std::array<float,9> cof={m[5]*m[10]-m[6]*m[9],m[6]*m[8]-m[4]*m[10],m[4]*m[9]-m[5]*m[8],
                m[9]*m[2]-m[10]*m[1],m[10]*m[0]-m[8]*m[2],m[8]*m[1]-m[9]*m[0],
                m[1]*m[6]-m[2]*m[5],m[2]*m[4]-m[0]*m[6],m[0]*m[5]-m[1]*m[4]};
            const float determinant=m[0]*cof[0]+m[1]*cof[1]+m[2]*cof[2];
            require(std::isfinite(determinant) && std::abs(determinant)>1e-12f,"singular node transform");
            FigureBounds bounds;bounds.minimum={FLT_MAX,FLT_MAX,FLT_MAX};bounds.maximum={-FLT_MAX,-FLT_MAX,-FLT_MAX};
            for(unsigned i=0;i<count;++i) {
                float p[3]{},n[3]{};
                require(cgltf_accessor_read_float(positions,i,p,3) && cgltf_accessor_read_float(normals,i,n,3),"vertex decode");
                const auto position=point(m,p);
                float transformed[3]{};
                for(unsigned a=0;a<3;++a)transformed[a]=(cof[a]*n[0]+cof[3+a]*n[1]+cof[6+a]*n[2])/determinant;
                float length=std::sqrt(transformed[0]*transformed[0]+transformed[1]*transformed[1]+transformed[2]*transformed[2]);
                require(std::isfinite(length) && length>1e-12f,"invalid normal");
                pack(mesh->positions,i,position);pack(mesh->normals,i,{transformed[0]/length,-transformed[2]/length,transformed[1]/length});
                mesh->components[i]={static_cast<std::uint16_t>(i),static_cast<std::uint16_t>(i),0};
                float radius2=0;
                for(unsigned a=0;a<3;++a) {
                    require(std::isfinite(position[a]) && std::abs(position[a])<100000,"invalid position");
                    bounds.minimum[a]=(std::min)(bounds.minimum[a],position[a]);bounds.maximum[a]=(std::max)(bounds.maximum[a],position[a]);
                    output->minimum[a]=(std::min)({output->minimum[a],position[a],part.minimum[a]});
                    output->maximum[a]=(std::max)({output->maximum[a],position[a],part.maximum[a]});
                    radius2+=position[a]*position[a];
                }
                mesh->shape_position_radii[0]=(std::max)(mesh->shape_position_radii[0],std::sqrt(radius2));
            }
            for(cgltf_size i=0;i<indexCount;++i) {
                const auto index=primitive.indices?cgltf_accessor_read_index(primitive.indices,i):i;
                require(index<count,"index out of range");mesh->indices.push_back(static_cast<std::uint16_t>(index));
            }
            if(determinant<0)for(size_t i=0;i<mesh->indices.size();i+=3)std::swap(mesh->indices[i+1],mesh->indices[i+2]);
            bounds.radius=mesh->shape_position_radii[0];mesh->bounds.push_back(bounds);
            auto material=std::make_shared<MaterialCpuAsset>();material->source=MaterialValueSource::completed_native;
            material->diffuse=material->ambient={1,1,1,1};material->specular={0,0,0,1};material->emissive={0,0,0,1};
            if(const auto* input=primitive.material) {
                require(input->alpha_mode==cgltf_alpha_mode_opaque && !input->double_sided && input->has_pbr_metallic_roughness &&
                        input->pbr_metallic_roughness.metallic_factor==0 && input->pbr_metallic_roughness.roughness_factor==1 &&
                        !input->extensions_count && !input->has_pbr_specular_glossiness && !input->has_clearcoat &&
                        !input->has_transmission && !input->has_volume && !input->has_ior && !input->has_specular && !input->has_sheen &&
                        !input->has_emissive_strength && !input->has_iridescence && !input->has_diffuse_transmission &&
                        !input->has_anisotropy && !input->has_dispersion && !input->unlit,"first rigid slice: opaque matte material");
                for(unsigned a=0;a<3;++a) {
                    const float c=input->pbr_metallic_roughness.base_color_factor[a];
                    require(std::isfinite(c) && c>=0 && c<=1,"base colour");material->diffuse[a]=material->ambient[a]=srgb(c);
                    require(input->emissive_factor[a]==0,"first rigid slice: no emission");
                }
            }
            output->geometry_bytes+=mesh->positions.size()*sizeof(NativePackedGroup)+mesh->normals.size()*sizeof(NativePackedGroup)+
                mesh->components.size()*sizeof(NativeVertexComponent)+mesh->indices.size()*sizeof(std::uint16_t)+sizeof(NativeTextureCoordinate);
            part.geometry=mesh;part.material=material;output->parts.push_back(std::move(part));
            ModelNodeCpuAsset transform;transform.name=name;transform.bone_positions={0,0,0};
            model->nodes.push_back(std::move(transform));model->links.push_back({name,"",UINT_MAX});
        }
        for(cgltf_size i=0;i<node->children_count;++i)visit(node->children[i],depth+1);
    };
    for(cgltf_size i=0;i<scene->nodes_count;++i)visit(scene->nodes[i],0);
    require(!output->parts.empty(),"empty scene");output->transforms=model;
    auto texture=std::make_shared<TextureCpuAsset>();texture->identity=blob.identity;texture->identity.canonical_name+="#environment-white";
    texture->width=texture->height=1;texture->bit_count=32;texture->format=MmpFormat::argb8888;
    texture->masks={0xff0000,0xff00,0xff,0xff000000};texture->shifts={16,8,0,24};texture->channel_bits={8,8,8,8};
    texture->source=std::make_shared<const std::vector<std::uint8_t>>(4,255);texture->texel_offset=0;
    texture->opaque_mips=1;texture->mips.push_back({1,1,0,4});output->white=texture;
    return output;
}

#ifndef ENVIRONMENT_ASSET_CHECK
std::shared_ptr<const Asset> select(const std::string& visual) {
    static std::mutex lock;static ResourceStore store;static std::uint64_t generation=0;
    static Json registry;static bool loaded=false;
    static std::map<std::string,std::weak_ptr<const Asset>> cache;
    std::lock_guard<std::mutex> guard(lock);
    ResourceStoreConfig mounts;std::uint64_t current=0;ResourceFailure failure;
    if(!owned_resource_runtime_mounts(mounts,&current))return {};
    if(current!=generation){store.close();require(store.open(mounts,failure),"resource mounts");generation=current;loaded=false;cache.clear();registry=Json();}
    if(!loaded) {
        ResourceBlob blob;
        if(store.resolve("models/registry.v1.json",blob,failure)) {
            registry=Json::parse(blob.bytes->begin(),blob.bytes->end());require(registry.at("version")==1,"registry version");
        } else require(failure.code==ResourceFailureCode::not_found,"registry unavailable");
        loaded=true;
    }
    if(registry.is_null() || !registry.contains("environment"))return {};
    const auto& entries=registry.at("environment");require(entries.is_object(),"environment registry");
    const auto key=lower(visual);const auto entry=entries.find(key);if(entry==entries.end())return {};
    if(auto found=cache[key].lock())return found;
    require(entry->at("profile")=="environment.v1","registry profile");
    const auto path=entry->at("resource").get<std::string>();
    require(path.rfind("models/environment/",0)==0 && path.size()>4 && path.substr(path.size()-4)==".glb" &&
        path.find("..") == std::string::npos && path.find(':')==std::string::npos && path.find('\\')==std::string::npos,"resource key");
    auto result=decode(read(store,path),key);cache[key]=result;return result;
}
#endif

std::shared_ptr<const VisualAssemblyAsset> assembly(const std::shared_ptr<const Asset>& asset,
    const std::vector<std::string>& selected,std::uint64_t revision) {
    require(asset!=nullptr,"assembly asset");
    auto result=std::make_shared<VisualAssemblyAsset>();result->skeleton=asset->transforms;result->revision=revision;
    for(unsigned i=0;i<asset->parts.size();++i) {
        const auto& source=asset->parts[i];
        if(!selected.empty() && std::none_of(selected.begin(),selected.end(),[&](const auto& s){return lower(s)==lower(source.name);}))continue;
        VisualPartAsset part;part.name=source.name;part.geometry=source.geometry;part.material=source.material;
        part.texture=asset->white;part.skeleton_node=i;
        result->parts.push_back(std::move(part));
    }
    require(!result->parts.empty(),"empty part selection");return result;
}
}
