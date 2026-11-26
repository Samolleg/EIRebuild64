#include "environment_asset.h"
#define EI_ENVIRONMENT_EXPORT __declspec(dllexport)
#include "environment_exchange.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <stdexcept>

using ModelHandle=std::shared_ptr<const owned::environment::Asset>;
static int32_t decodeView(const void* data,uint64_t bytes,const char* name,const owned::environment::ModelView* view,
    EiEnvironmentSink sink,void* context,char* error,uint32_t capacity,void** motion=nullptr) noexcept try {
    if(!data||!name||!sink||bytes>64u*1024*1024)throw std::runtime_error("invalid environment decode input");
    owned::ResourceBlob blob;const auto* begin=static_cast<const std::uint8_t*>(data);
    blob.bytes=std::make_shared<const std::vector<std::uint8_t>>(begin,begin+bytes);blob.identity.canonical_name=name;
    const auto asset=owned::environment::decode(blob,name,view);
    std::map<const owned::TextureCpuAsset*,std::vector<std::uint8_t>> textures;
    for(const auto& part:asset->parts) {
        if(!part.render && view)continue;
        const auto& mesh=*part.geometry;const auto count=mesh.components.size();
        std::vector<float> positions(count*3*mesh.shape_count),normals(positions.size()),uv(count*2),collision(mesh.shape_count*6);
        for(unsigned s=0;s<mesh.shape_count;++s) {
            const auto& b=part.collision[part.collision.size()==1?0:s];
            std::copy(b.minimum.begin(),b.minimum.end(),collision.begin()+s*6);std::copy(b.maximum.begin(),b.maximum.end(),collision.begin()+s*6+3);
            for(size_t v=0;v<count;++v) {
                const auto& c=mesh.components[v];const auto& p=mesh.positions[s*mesh.position_groups+c.position/4];
                const auto& n=mesh.normals[s*mesh.position_groups+c.normal/4];
                const unsigned k=c.position%4,j=c.normal%4;const float xyz[]={p.x[k],p.y[k],p.z[k]},nn[]={n.x[j],n.y[j],n.z[j]};
                for(unsigned a=0;a<3;++a){positions[(s*count+v)*3+a]=xyz[a]+asset->wind_root[a];normals[(s*count+v)*3+a]=nn[a];}
                uv[v*2]=mesh.texture_coordinates[c.texture].u;uv[v*2+1]=mesh.texture_coordinates[c.texture].v;
            }
        }
        const auto& texture=*part.texture;auto& mmp=textures[&texture];
        if(mmp.empty()) {
            mmp.resize(76);auto put=[&](unsigned at,std::uint32_t value){std::memcpy(mmp.data()+at,&value,4);};
            put(0,0x00504d4d);put(4,texture.width);put(8,texture.height);put(12,unsigned(texture.mips.size()));
            const auto format=texture.format==owned::MmpFormat::dxt1?0x31545844u:texture.format==owned::MmpFormat::dxt3?0x33545844u:texture.format==owned::MmpFormat::dxt5?0x35545844u:0x8888u;
            put(16,format);put(20,format==0x8888?32:format==0x31545844?4:8);
            // TextureCpuAsset uses RGBA order. The DirectDraw MMP header uses ARGB.
            for(unsigned c=0;c<4;++c){
                const auto source=(c+3)%4;
                put(24+c*12,texture.masks[source]);put(28+c*12,texture.shifts[source]);put(32+c*12,texture.channel_bits[source]);
            }
            for(const auto& mip:texture.mips)mmp.insert(mmp.end(),texture.source->begin()+mip.byte_offset,texture.source->begin()+mip.byte_offset+mip.bytes);
        }
        EiEnvironmentPart view{};view.size=sizeof(view);view.abi=EI_ENVIRONMENT_ABI;view.name=part.name.c_str();view.logical_name=part.logical_name.c_str();
        view.vertices=unsigned(count);view.indices=unsigned(mesh.indices.size());view.shapes=mesh.shape_count;view.role=unsigned(part.role);
        view.passability=part.passability;view.opacity=part.opacity;view.render=part.render;view.positions=positions.data();view.normals=normals.data();view.uv=uv.data();view.triangles=mesh.indices.data();view.collision=collision.data();
        view.texture_key=texture.identity.canonical_name.c_str();view.texture=mmp.data();view.texture_bytes=mmp.size();
        view.alpha_mode=part.pbr->alpha;view.two_sided=part.pbr->two_sided;view.alpha_cutoff=part.pbr->cutoff;view.alpha_factor=part.pbr->alpha_factor;
        if(!sink(context,&view))throw std::runtime_error("environment consumer rejected part");
    }
    if(motion && asset->motion && !asset->view.clip.empty())*motion=new ModelHandle(asset);
    if(error&&capacity)*error=0;return 0;
}catch(const std::exception& e){if(error&&capacity)std::snprintf(error,capacity,"%s",e.what());return -1;}
catch(...){if(error&&capacity)std::snprintf(error,capacity,"environment decode failed");return -1;}

extern "C" __declspec(dllexport) int32_t EI_ENV_CALL ataman_environment_decode(const void* data,uint64_t bytes,const char* name,EiEnvironmentSink sink,void* context,char* error,uint32_t capacity) noexcept {
    return decodeView(data,bytes,name,nullptr,sink,context,error,capacity);
}
extern "C" __declspec(dllexport) int32_t EI_ENV_CALL ataman_model_decode(const void* data,uint64_t bytes,const char* name,const char* primary,const char* secondary,const char* clip,float seconds,EiEnvironmentSink sink,void* context,char* error,uint32_t capacity) noexcept try {
    owned::environment::ModelView v{primary?primary:"",secondary?secondary:"",clip?clip:"",seconds};
    return decodeView(data,bytes,name,&v,sink,context,error,capacity);
}catch(...){return -1;}

extern "C" __declspec(dllexport) int32_t EI_ENV_CALL ataman_model_open_form(const void* data,uint64_t bytes,const char* name,const char* primary,const char* secondary,const char* clip,const float* shape,EiEnvironmentSink sink,void* context,char* error,uint32_t capacity,void** handle) noexcept try {
    if(!handle)return -1;
    *handle=nullptr;
    owned::environment::ModelView view{primary?primary:"",secondary?secondary:"",clip?clip:"",0};
    if(shape)std::copy_n(shape,3,view.shape.begin());
    return decodeView(data,bytes,name,&view,sink,context,error,capacity,handle);
}catch(...){return -1;}
extern "C" __declspec(dllexport) int32_t EI_ENV_CALL ataman_model_open(const void* data,uint64_t bytes,const char* name,const char* primary,const char* secondary,const char* clip,EiEnvironmentSink sink,void* context,char* error,uint32_t capacity,void** handle) noexcept {
    return ataman_model_open_form(data,bytes,name,primary,secondary,clip,nullptr,sink,context,error,capacity,handle);
}
extern "C" __declspec(dllexport) void EI_ENV_CALL ataman_model_close(void* handle) noexcept {
    delete static_cast<ModelHandle*>(handle);
}
extern "C" __declspec(dllexport) int32_t EI_ENV_CALL ataman_model_pose(void* handle,float seconds,EiModelPoseSink sink,void* context) noexcept try {
    if(!handle || !sink || !std::isfinite(seconds))return -1;
    const auto& source=*static_cast<ModelHandle*>(handle);
    auto clip=source->view.clip;for(auto& c:clip)if(c>='A'&&c<='Z')c+='a'-'A';
    const auto duration=source->clip_lengths.at(clip);
    if(duration<=0)return 0;
    const auto sampled=owned::environment::sample_model(source,clip,std::fmod(seconds,duration));
    std::vector<float> positions,normals;
    for(const auto& part:sampled->parts)if(part.render){
        const auto& mesh=*part.geometry;positions.resize(mesh.components.size()*3);normals.resize(positions.size());
        for(size_t i=0;i<mesh.components.size();++i){
            const auto& p=mesh.positions[i/4];const auto& n=mesh.normals[i/4];const auto k=i%4;
            positions[3*i]=p.x[k];positions[3*i+1]=p.y[k];positions[3*i+2]=p.z[k];
            normals[3*i]=n.x[k];normals[3*i+1]=n.y[k];normals[3*i+2]=n.z[k];
        }
        if(!sink(context,part.name.c_str(),unsigned(mesh.components.size()),positions.data(),normals.data()))return -1;
    }
    return 0;
}catch(...){return -1;}
