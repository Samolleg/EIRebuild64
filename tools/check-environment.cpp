#define CGLTF_IMPLEMENTATION
#include "../dependencies/cgltf/v1.15/cgltf.h"
#include "../external/owned-renderer/Source/environment_asset.h"
#include "../external/owned-renderer/Source/environment_exchange.h"
#include "../external/owned-renderer/Source/owned_asset_codec.h"
#include <filesystem>
#include <fstream>
#include <iostream>

owned::ResourceBlob read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("fixture missing");
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    owned::ResourceBlob blob;blob.bytes=bytes;blob.identity.canonical_name=path.string();return blob;
}
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
namespace owned {
void owned_terrain_reset_liquid_outflow(TerrainCpuAsset&) noexcept { std::terminate(); }
void owned_terrain_report_astral_received(const TerrainCpuAsset&) { throw std::runtime_error("unexpected terrain in environment codec check"); }
}
void wireCheck(const std::shared_ptr<const owned::VisualAssemblyAsset>& source) {
    struct Wire {owned::codec::AssetDecoder decoder;std::vector<std::uint8_t> pending;bool valid=true;} wire;
    const auto sink=[](void* context,const std::uint8_t* data,std::size_t bytes,bool last) noexcept {
        auto& w=*static_cast<Wire*>(context);
        try {w.pending.insert(w.pending.end(),data,data+bytes);if(!last)return;
            owned::codec::In in(w.pending.data(),w.pending.size());std::uint8_t tag=0;owned::codec::io(in,tag);std::string error;
            w.valid=w.valid&&tag==owned::codec::kAssetDefine&&w.decoder.define(in,error)&&in.done();w.pending.clear();
        }catch(...){w.valid=false;}
    };
    owned::codec::AssetEncoder encoder(sink,&wire,257);const auto id=encoder.id(source);
    std::shared_ptr<const owned::VisualAssemblyAsset> copy;
    check(wire.valid&&wire.decoder.get(id,copy)&&copy,"environment wire decode");
    check(copy->shape==source->shape&&copy->environment_wind==source->environment_wind&&copy->sway_shape==source->sway_shape,"wire shape/wind");
    check(copy->parts.size()==source->parts.size(),"wire parts");
    for(size_t i=0;i<copy->parts.size();++i) {
        const auto& a=source->parts[i];const auto& b=copy->parts[i];
        check(a.geometry->positions.size()==b.geometry->positions.size()&&a.environment_foliage==b.environment_foliage,"wire geometry/foliage");
        check(b.environment_material&&a.environment_material->base==b.environment_material->base&&a.environment_material->alpha==b.environment_material->alpha,"wire material");
        for(unsigned j=0;j<4;++j)if(a.environment_material->textures[j]) {
            const auto& texture=b.environment_material->textures[j];check(texture&&*texture->asset->source==*a.environment_material->textures[j]->asset->source,"wire texture bytes");
            check(texture->view().mips&&texture->view().mips[0].data==texture->asset->source->data()+texture->asset->mips[0].byte_offset,"wire texture lifetime");
        }
    }
    const auto count=wire.decoder.live();check(encoder.id(source)==id&&wire.decoder.live()==count,"wire resource sharing");
}
int wmain(int argc,wchar_t** argv) try {
    if(argc==3&&std::wstring(argv[1])==L"--environment") {
        auto asset=owned::environment::decode(read(argv[2]),"environment-check");
        std::cout<<"ACCEPTED environment.v1 parts="<<asset->parts.size()<<" geometry_bytes="<<asset->geometry_bytes<<"\n";return 0;
    }
    check(argc==2,"fixture directory required");const std::filesystem::path root=argv[1];
    auto asset=owned::environment::decode(read(root/L"sgcrate01.glb"),"sgcrate01");
    check(asset->parts.size()==1,"parts");const auto& mesh=*asset->parts[0].geometry;
    check(mesh.components.size()==24 && mesh.indices.size()==36,"geometry count");
    check(mesh.shape_count==1 && mesh.morph_components.empty(),"rigid memory layout");
    check(asset->minimum==std::array<float,3>{-1,-1,0} && asset->maximum==std::array<float,3>{1,1,2},"axis conversion/collider bounds");
    check(asset->geometry_bytes<=1100,"compact vertex/index budget");
    check(*asset->parts[0].texture->source==std::vector<std::uint8_t>{53,97,160,255},"linear baseColorFactor to sRGB BGRA");
    check(asset->parts[0].material->diffuse==std::array<float,4>{1,1,1,1},"albedo factor applied once");
    for(unsigned i=0;i<1000;++i) {
        auto instance=owned::environment::assembly(asset,{},i+1);
        check(instance->parts[0].geometry==asset->parts[0].geometry && instance->parts[0].texture==asset->parts[0].texture,"resource duplication");
        check(instance->skeleton==asset->transforms && instance->equipment.empty(),"shared rigid transforms");
    }
    auto scaled=owned::environment::decode(read(root/L"scaled.glb"),"scaled");
    check(scaled->minimum[0]==-2 && scaled->minimum[1]==-3 && scaled->maximum[2]==2,"node scale/axes");
    unsigned rejected=0;
    for(const auto* name:{L"missing-collider",L"singular",L"skin",L"inverted-collider",L"unsupported-material",L"bad-index"}) {
        try{owned::environment::decode(read(root/(std::wstring(name)+L".glb")),"bad");}
        catch(const std::exception&){++rejected;}
    }
    check(rejected==6,"malformed asset accepted");
    bool refused=false;try{owned::environment::assembly(asset,{"absent"},1);}catch(const std::exception&){refused=true;}
    check(refused,"invalid selection accepted");
    auto multiple=owned::environment::decode(read(root/L"multipart.glb"),"multipart");
    check(owned::environment::assembly(multiple,{"crate"},1)->parts.size()==2,"logical material primitives selection");
    check(owned::environment::assembly(multiple,{"crate#1"},1)->parts.size()==1,"single primitive selection");
    auto shaped=owned::environment::decode(read(root/L"forms-wind.glb"),"forms-wind");
    const auto& sp=shaped->parts[0];check(sp.geometry->shape_count==8&&sp.geometry->normal_groups==sp.geometry->position_groups*8,"endpoint layout");
    const auto middle=owned::environment::collision(sp,{.5f,.5f,.5f});
    check(middle.minimum==std::array<float,3>{-1.5f,-1.5f,0}&&middle.maximum==std::array<float,3>{1.5f,1.5f,3},"mixed collision forms");
    const auto outside=owned::environment::collision(sp,{2,0,0});check(outside.maximum[0]==3,"unclamped MOB shape");
    auto plant=owned::environment::assembly(shaped,{},1,{.5f,.5f,.5f});
    check(plant->sways&&plant->sway_root==0&&plant->parts[0].skeleton_node==0&&plant->parts[0].environment_foliage,"explicit anchored wind");
    const auto form=plant->parts[0].geometry;
    check(form->shape_count==1&&form->normal_groups==form->position_groups,"ordinary renderer mesh layout");
    check(form->bounds[0].minimum==std::array<float,3>{-1.5f,-1.5f,0}&&form->bounds[0].maximum==std::array<float,3>{1.5f,1.5f,3},"resolved geometry matches collision");
    for(unsigned i=0;i<1000;++i)check(owned::environment::assembly(shaped,{},i,{.5f,.5f,.5f})->parts[0].geometry==form,"1000 forms share one prepared mesh");
    auto other=owned::environment::assembly(shaped,{},1,{1,1,1});
    check(other->parts[0].geometry!=form&&other->parts[0].geometry->bounds[0].maximum==std::array<float,3>{2,2,4},"distinct prepared form");
    auto restored=*other;owned::environment::reshape(restored,{.5f,.5f,.5f});check(restored.parts[0].geometry==form,"reshape reuses prepared geometry");
    auto pbr=owned::environment::decode(read(root/L"pbr.glb"),"pbr");const auto& m=*pbr->parts[0].pbr;
    check(m.alpha==1&&m.two_sided&&m.cutoff==200.f/255.f&&m.alpha_factor==1&&m.roughness==.6f&&m.metallic==.4f&&m.textures[3],"PBR stock mask/emissive maps");
    auto textured=owned::environment::decode(read(root/L"textured.glb"),"textured");
    check(textured->parts[0].texture->width==2&&textured->parts[0].texture->mips.size()==2,"embedded image mip chain");
    check(textured->parts[0].geometry->texture_coordinates.size()==24,"UV preserved");
    check((*textured->parts[0].texture->source)[3]>200&&(*textured->parts[0].texture->source)[7]<=200,"stock alpha preserves opaque/transparent pixels");
    wireCheck(plant);wireCheck(owned::environment::assembly(pbr,{},1));wireCheck(owned::environment::assembly(textured,{},1));
    const auto exchangeSource=read(root/L"forms-wind.glb");unsigned parts=0;char error[512]{};
    const auto sink=[](void* context,const EiEnvironmentPart* p)->int32_t {
        if(p->abi!=EI_ENVIRONMENT_ABI||p->shapes!=8||p->vertices!=24||p->indices!=36||p->role!=2||p->texture_bytes<80)return 0;
        ++*static_cast<unsigned*>(context);return 1;
    };
    check(ataman_environment_decode(exchangeSource.bytes->data(),exchangeSource.bytes->size(),"exchange",sink,&parts,error,sizeof(error))==0&&parts==1,"shared editor decoder ABI");
    std::cout<<"PASS: rigid GLB, axes/scale, 6 malformed inputs, explicit collider, 1000 shared instances; geometry_bytes="<<asset->geometry_bytes<<"\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
