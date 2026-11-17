#include "environment_asset.h"
#define EI_ENVIRONMENT_EXPORT __declspec(dllexport)
#include "environment_exchange.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <stdexcept>

extern "C" __declspec(dllexport) int32_t EI_ENV_CALL ataman_environment_decode(const void* data,uint64_t bytes,const char* name,
    EiEnvironmentSink sink,void* context,char* error,uint32_t capacity) noexcept try {
    if(!data||!name||!sink||bytes>64u*1024*1024)throw std::runtime_error("invalid environment decode input");
    owned::ResourceBlob blob;const auto* begin=static_cast<const std::uint8_t*>(data);
    blob.bytes=std::make_shared<const std::vector<std::uint8_t>>(begin,begin+bytes);blob.identity.canonical_name=name;
    const auto asset=owned::environment::decode(blob,name);
    std::map<const owned::TextureCpuAsset*,std::vector<std::uint8_t>> textures;
    for(const auto& part:asset->parts) {
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
            for(unsigned c=0;c<4;++c){put(24+c*12,texture.masks[c]);put(28+c*12,texture.shifts[c]);put(32+c*12,texture.channel_bits[c]);}
            for(const auto& mip:texture.mips)mmp.insert(mmp.end(),texture.source->begin()+mip.byte_offset,texture.source->begin()+mip.byte_offset+mip.bytes);
        }
        EiEnvironmentPart view{};view.size=sizeof(view);view.abi=EI_ENVIRONMENT_ABI;view.name=part.name.c_str();view.logical_name=part.logical_name.c_str();
        view.vertices=unsigned(count);view.indices=unsigned(mesh.indices.size());view.shapes=mesh.shape_count;view.role=unsigned(part.role);
        view.passability=part.passability;view.opacity=part.opacity;view.render=part.render;view.positions=positions.data();view.normals=normals.data();view.uv=uv.data();view.triangles=mesh.indices.data();view.collision=collision.data();
        view.texture_key=texture.identity.canonical_name.c_str();view.texture=mmp.data();view.texture_bytes=mmp.size();
        view.alpha_mode=part.pbr->alpha;view.two_sided=part.pbr->two_sided;view.alpha_cutoff=part.pbr->cutoff;view.alpha_factor=part.pbr->alpha_factor;
        if(!sink(context,&view))throw std::runtime_error("environment consumer rejected part");
    }
    if(error&&capacity)*error=0;return 0;
}catch(const std::exception& e){if(error&&capacity)std::snprintf(error,capacity,"%s",e.what());return -1;}
catch(...){if(error&&capacity)std::snprintf(error,capacity,"environment decode failed");return -1;}
