#define CGLTF_IMPLEMENTATION
#include "../dependencies/cgltf/v1.15/cgltf.h"
#include "../external/owned-renderer/Source/environment_asset.h"
#include <filesystem>
#include <fstream>
#include <iostream>

owned::ResourceBlob read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("fixture missing");
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    owned::ResourceBlob blob;blob.bytes=bytes;blob.identity.canonical_name=path.string();return blob;
}
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int wmain(int argc,wchar_t** argv) try {
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
    std::cout<<"PASS: rigid GLB, axes/scale, 6 malformed inputs, explicit collider, 1000 shared instances; geometry_bytes="<<asset->geometry_bytes<<"\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
