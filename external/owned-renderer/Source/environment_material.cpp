#include "environment_material.h"
#include "../../../dependencies/cgltf/v1.15/cgltf.h"
#include "../../../SDKs/JSON/json.hpp"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace owned::environment {
namespace {
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(std::string("environment material: ")+reason);}
void check(HRESULT hr,const char* reason){require(SUCCEEDED(hr),reason);}
float linear(float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}
float srgb(float c){return c<=.0031308f?c*12.92f:1.055f*std::pow(c,1/2.4f)-.055f;}
unsigned u32(const std::uint8_t* b){unsigned v;std::memcpy(&v,b,4);return v;}
std::uint64_t hash(const std::uint8_t* data,size_t size){std::uint64_t h=1469598103934665603ull;for(size_t i=0;i<size;++i){h^=data[i];h*=1099511628211ull;}return h;}
std::shared_ptr<const Texture> finish(std::shared_ptr<TextureCpuAsset> asset,bool colour) {
    // Content + interpretation: identical images in different GLBs share CPU/GPU values.
    const auto digest=hash(asset->source->data(),asset->source->size());
    const std::string key=std::to_string(digest)+"/"+std::to_string(asset->width)+"/"+std::to_string(asset->height)+"/"+
        std::to_string(unsigned(asset->format))+"/"+std::to_string(asset->mips.size())+"/"+std::to_string(colour)+"/"+std::to_string(asset->opaque_mips);
    static std::mutex lock;static std::map<std::string,std::weak_ptr<const Texture>> cache;
    std::lock_guard<std::mutex> guard(lock);
    if(auto previous=cache[key].lock()) {
        require(*previous->asset->source==*asset->source,"texture hash collision");return previous;
    }
    asset->identity={};asset->identity.canonical_name="environment/pixels/"+key;asset->identity.bytes=asset->source->size();
    auto output=std::make_shared<Texture>();output->asset=asset;
    for(const auto& mip:asset->mips)output->mips.push_back({asset->source->data()+mip.byte_offset,mip.width,mip.height,mip.bytes});
    cache[key]=output;
    if(cache.size()>4096)for(auto it=cache.begin();it!=cache.end();)it=it->second.expired()?cache.erase(it):std::next(it);
    return output;
}
std::shared_ptr<TextureCpuAsset> rgba(unsigned w,unsigned h,std::vector<std::uint8_t> pixels,bool colour,unsigned alpha,float cutoff) {
    auto t=std::make_shared<TextureCpuAsset>();t->width=w;t->height=h;t->bit_count=32;t->format=MmpFormat::argb8888;
    t->masks={0xff0000,0xff00,0xff,0xff000000};t->shifts={16,8,0,24};t->channel_bits={8,8,8,8};t->texel_offset=0;
    size_t offset=0;
    float coverage=0;for(size_t i=3;i<pixels.size();i+=4)coverage+=pixels[i]>=cutoff*255;
    coverage/=float(w*h);
    for(unsigned level=0;;++level) {
        const unsigned bytes=w*h*4;t->mips.push_back({w,h,unsigned(offset),bytes});
        bool opaque=true;for(size_t p=offset+3;p<offset+bytes;p+=4)opaque=opaque&&pixels[p]==255;
        if(opaque)t->opaque_mips|=1u<<level;
        if(w==1&&h==1)break;
        const unsigned nw=(std::max)(1u,w/2),nh=(std::max)(1u,h/2);const auto next=pixels.size();pixels.resize(next+nw*nh*4);
        for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x)for(unsigned c=0;c<4;++c) {
            float sum=0;unsigned count=0;
            for(unsigned sy=y*h/nh;sy<(y+1)*h/nh;++sy)for(unsigned sx=x*w/nw;sx<(x+1)*w/nw;++sx) {
                float v=pixels[offset+(sy*w+sx)*4+c]/255.f;sum+=colour&&c<3?linear(v):v;++count;
            }
            float v=sum/count;if(colour&&c<3)v=srgb(v);
            pixels[next+(y*nw+x)*4+c]=std::uint8_t(std::lround((std::clamp)(v,0.f,1.f)*255));
        }
        // MASK mip coverage follows the source silhouette instead of shrinking leaves.
        if(alpha==1&&coverage>0&&coverage<1&&cutoff>0) {
            float lo=0,hi=8;
            for(unsigned attempt=0;attempt<16;++attempt) {
                float scale=(lo+hi)*.5f,covered=0;
                for(size_t p=next+3;p<pixels.size();p+=4)covered+=pixels[p]*scale>=cutoff*255;
                if(covered/(nw*nh)<coverage)lo=scale;else hi=scale;
            }
            for(size_t p=next+3;p<pixels.size();p+=4)pixels[p]=std::uint8_t((std::min)(255.f,std::round(pixels[p]*hi)));
        }
        offset=next;w=nw;h=nh;
    }
    t->source=std::make_shared<const std::vector<std::uint8_t>>(std::move(pixels));return t;
}
std::shared_ptr<const Texture> image(const cgltf_image& input,bool colour,unsigned alpha,float cutoff) {
    require(!input.uri&&input.buffer_view&&input.buffer_view->buffer->data,"embedded image required");
    const auto& v=*input.buffer_view;require(v.offset<=v.buffer->size&&v.size<=v.buffer->size-v.offset&&v.size<=16u*1024*1024,"image byte budget");
    const auto* data=static_cast<const std::uint8_t*>(v.buffer->data)+v.offset;
    if(v.size>=128&&!std::memcmp(data,"DDS ",4)) {
        require(u32(data+4)==124&&u32(data+76)==32,"DDS header");
        unsigned w=u32(data+16),h=u32(data+12),levels=u32(data+28),code=u32(data+84);
        require(w&&h&&w<=1024&&h<=1024&&levels&&levels<=11,"DDS dimensions/mips");
        require(!(u32(data+112)&0xfe00u),"DDS cubemap/volume unsupported");
        auto t=std::make_shared<TextureCpuAsset>();t->width=w;t->height=h;t->bit_count=32;
        t->format=code==0x31545844?MmpFormat::dxt1:code==0x33545844?MmpFormat::dxt3:MmpFormat::dxt5;
        require(code==0x31545844||code==0x33545844||code==0x35545844,"DDS BC1/BC2/BC3 required");
        size_t offset=128;
        for(unsigned l=0;l<levels;++l) {
            const unsigned bytes=((w+3)/4)*((h+3)/4)*(code==0x31545844?8:16);
            require(offset<=v.size&&bytes<=v.size-offset,"DDS mip range");t->mips.push_back({w,h,unsigned(offset),bytes});
            offset+=bytes;if(l+1<levels){require(w>1||h>1,"extra DDS mips");w=(std::max)(1u,w/2);h=(std::max)(1u,h/2);}
        }
        require(w==1&&h==1&&offset==v.size,"complete exact DDS mip chain required");
        t->source=std::make_shared<const std::vector<std::uint8_t>>(data,data+v.size);
        if(alpha==0)t->opaque_mips=UINT_MAX;
        return finish(t,colour);
    }
    require(input.mime_type&&(!std::strcmp(input.mime_type,"image/png")||!std::strcmp(input.mime_type,"image/jpeg")),"PNG/JPEG or prepared DDS required");
    const auto hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Apartment{HRESULT result;~Apartment(){if(SUCCEEDED(result))CoUninitialize();}} apartment{hr};
    require(SUCCEEDED(hr)||hr==RPC_E_CHANGED_MODE,"COM apartment");
    using Microsoft::WRL::ComPtr;ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");
    ComPtr<IWICStream> stream;check(factory->CreateStream(&stream),"WIC stream");
    check(stream->InitializeFromMemory(const_cast<BYTE*>(data),DWORD(v.size)),"WIC input");
    ComPtr<IWICBitmapDecoder> decoder;check(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,&decoder),"image decode");
    ComPtr<IWICBitmapFrameDecode> frame;check(decoder->GetFrame(0,&frame),"image frame");
    UINT w=0,h=0;check(frame->GetSize(&w,&h),"image size");require(w&&h&&w<=1024&&h<=1024,"1024 atlas budget");
    ComPtr<IWICFormatConverter> converter;check(factory->CreateFormatConverter(&converter),"image converter");
    check(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"BGRA conversion");
    std::vector<std::uint8_t> pixels(w*h*4);check(converter->CopyPixels(nullptr,w*4,UINT(pixels.size()),pixels.data()),"image pixels");
    return finish(rgba(w,h,std::move(pixels),colour,alpha,cutoff),colour);
}
std::shared_ptr<const Texture> constant(std::array<std::uint8_t,4> pixel,bool colour) {return finish(rgba(1,1,{pixel.begin(),pixel.end()},colour,0,0),colour);}
}
NativeOwnedTextureView Texture::view() const {
    NativeOwnedTextureView v{};v.identity=1;v.generation=1;v.value_source=1;v.resource=&asset->identity;
    v.format=asset->format==MmpFormat::dxt1?NativeOwnedTextureFormat::dxt1:asset->format==MmpFormat::dxt3?NativeOwnedTextureFormat::dxt3:
        asset->format==MmpFormat::dxt5?NativeOwnedTextureFormat::dxt5:NativeOwnedTextureFormat::argb8888;
    v.mips=mips.data();v.mip_count=unsigned(mips.size());v.opaque_mips=asset->opaque_mips;return v;
}
NativeSknMaterial Material::native() const {
    NativeSknMaterial v{};v.identity=reinterpret_cast<std::uintptr_t>(this);v.factors[0]=roughness;v.factors[1]=metallic;v.factors[2]=1;v.factors[3]=two_sided?2.f:1.f;
    std::copy(base.begin(),base.end(),v.base);v.base[3]=float(alpha);std::copy(emission.begin(),emission.end(),v.emission);v.emission[3]=unlit?1.f:0.f;
    for(unsigned k=0;k<3;++k)if(textures[k+1])v.maps[k]=textures[k+1]->view();return v;
}
std::shared_ptr<const Material> material(const cgltf_material* input,const ResourceIdentity&,const cgltf_data& document) {
    auto m=std::make_shared<Material>();cgltf_material fallback{};
    std::fill_n(fallback.pbr_metallic_roughness.base_color_factor,4,1.f);fallback.pbr_metallic_roughness.metallic_factor=fallback.pbr_metallic_roughness.roughness_factor=1;fallback.alpha_cutoff=.5f;
    const auto& s=input?*input:fallback;const auto& p=s.pbr_metallic_roughness;
    require(!s.has_pbr_specular_glossiness&&!s.has_clearcoat&&!s.has_transmission&&!s.has_volume&&!s.has_sheen&&!s.has_iridescence&&!s.has_anisotropy&&!s.has_dispersion&&!s.has_diffuse_transmission,"unsupported material extension");
    require(!s.has_ior||s.ior.ior==1.5f,"non-default IOR requires material baking");
    require(!s.has_specular||(s.specular.specular_factor==1&&s.specular.specular_color_factor[0]==1&&s.specular.specular_color_factor[1]==1&&s.specular.specular_color_factor[2]==1&&!s.specular.specular_texture.texture&&!s.specular.specular_color_texture.texture),"non-default specular extension requires material baking");
    for(unsigned k=0;k<4;++k){require(std::isfinite(p.base_color_factor[k])&&p.base_color_factor[k]>=0&&p.base_color_factor[k]<=1,"base factor");m->base[k]=k<3?srgb(p.base_color_factor[k]):p.base_color_factor[k];}
    m->alpha_factor=m->base[3];m->roughness=p.roughness_factor;m->metallic=p.metallic_factor;m->cutoff=s.alpha_cutoff;m->alpha=unsigned(s.alpha_mode);m->two_sided=s.double_sided;m->unlit=s.unlit;
    require(std::isfinite(m->roughness)&&m->roughness>=0&&m->roughness<=1&&std::isfinite(m->metallic)&&m->metallic>=0&&m->metallic<=1,"PBR factors");
    require(std::isfinite(m->cutoff)&&m->cutoff>=0&&m->cutoff<=1&&m->alpha<=2,"alpha mode/cutoff");
    const float strength=s.has_emissive_strength?s.emissive_strength.emissive_strength:1;
    for(unsigned k=0;k<3;++k){require(std::isfinite(s.emissive_factor[k])&&s.emissive_factor[k]>=0&&std::isfinite(strength)&&strength>=0,"emission");m->emission[k]=srgb(s.emissive_factor[k]*strength);}
    const cgltf_texture_view* views[]={&p.base_color_texture,&s.normal_texture,&p.metallic_roughness_texture,&s.emissive_texture};
    bool haveUv=false;
    for(unsigned k=0;k<4;++k)if(views[k]->texture) {
        const auto& t=*views[k];unsigned uvSet=t.texcoord;
        std::array<float,6> transform{1,0,0,0,1,0};
        if(t.has_transform) {
            const auto& v=t.transform;if(v.has_texcoord)uvSet=v.texcoord;
            const float c=std::cos(v.rotation),sine=std::sin(v.rotation);
            transform={c*v.scale[0],-sine*v.scale[1],v.offset[0],sine*v.scale[0],c*v.scale[1],v.offset[1]};
            require(std::all_of(transform.begin(),transform.end(),[](float f){return std::isfinite(f);}),"UV transform");
        }
        const auto address=[](unsigned v){require(v==10497||v==33071||v==33648,"sampler wrap");return v==10497?1u:v==33071?3u:2u;};
        const unsigned u=address(t.texture->sampler?t.texture->sampler->wrap_s:10497),v=address(t.texture->sampler?t.texture->sampler->wrap_t:10497);
        if(haveUv)require(m->uv_set==uvSet&&m->uv==transform&&m->address_u==u&&m->address_v==v,"material maps need a common UV/sampler; bake divergent maps during export");
        else{m->uv_set=uvSet;m->uv=transform;m->address_u=u;m->address_v=v;haveUv=true;}
        const auto* imageSource=t.texture->image;
        for(size_t e=0;e<t.texture->extensions_count;++e)if(!std::strcmp(t.texture->extensions[e].name,"MSFT_texture_dds")) {
            const auto index=nlohmann::json::parse(t.texture->extensions[e].data).at("source").get<unsigned>();
            require(index<document.images_count,"DDS image index");imageSource=&document.images[index];
        }
        require(imageSource,"texture image missing");
        m->textures[k]=image(*imageSource,k==0||k==3,k==0?m->alpha:0,k==0&&m->alpha_factor>0?m->cutoff/m->alpha_factor:m->cutoff);
    }
    if(!m->textures[0]) {std::array<std::uint8_t,4> bgra{};for(unsigned k=0;k<3;++k)bgra[2-k]=std::uint8_t(std::lround(m->base[k]*255));bgra[3]=255;m->textures[0]=constant(bgra,true);m->base[0]=m->base[1]=m->base[2]=1;}
    if(s.normal_texture.texture)require(s.normal_texture.scale==1,"normal scale must be baked during preparation");
    if(s.occlusion_texture.texture) {
        require(s.occlusion_texture.scale==1,"AO strength must be baked during preparation");
        require(s.occlusion_texture.texture==p.metallic_roughness_texture.texture,"AO must be packed into ORM during preparation");
    }
    if(!m->textures[3]&&(m->emission[0]>0||m->emission[1]>0||m->emission[2]>0))m->textures[3]=constant({255,255,255,255},true);
    return m;
}
}
