#define wmain animation_regression_main
#include "check-gltf-animation.cpp"
#undef wmain
#include "../Game/OwnedRender/EquipmentSelection.h"
#include "gltf_equipment_registry.h"
#include "retained_static_scene.h"
#include "figure_service_nodes.h"
namespace owned {
void bridge_append_probe_event(const char*,const char*) noexcept {}
// Resource IO/cache is outside this group-packing fixture.
std::string ResourceIdentity::key() const { return canonical_name; }
namespace {
template<class Build>
std::shared_ptr<const VisualPreparedResources::MergedGeometry> merged_geometry(const std::string&,Build&& build) {
    auto geometry=std::make_shared<VisualPreparedResources::MergedGeometry>();build(*geometry);return geometry;
}
}
#include "../external/owned-renderer/Source/skn_submission.inl"
}
using Json=nlohmann::json;
static owned::ResourceBlob blob(const void* p,std::size_t n) {
    owned::ResourceBlob b;auto first=static_cast<const std::uint8_t*>(p);
    b.bytes=std::make_shared<std::vector<std::uint8_t>>(first,first+n);return b;
}
static owned::ResourceBlob read_file(const wchar_t* path) {
    std::ifstream in(std::filesystem::path(path),std::ios::binary);
    check(bool(in),"input file");std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});
    return blob(bytes.data(),bytes.size());
}
static owned::ResourceBlob fixture(Json& doc) {
    const float vertices[]={0,0,0,1,0,0,0,1,0, 0,0,1,0,0,1,0,0,1, 0,0,1,0,0,1};
    if(doc.is_null())doc=Json::parse(R"({"asset":{"version":"2.0","extras":{"ei":{
        "profile":"human.v1","kind":"rigid-item","socket":"socket_hand_r",
        "grip":{"translation":[1,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]},
        "anchors":{"cast":[1,5,0],"particle":[1,4,0]}}}},
        "scene":0,"scenes":[{"nodes":[0]}],
        "nodes":[{"translation":[2,0,0],"children":[1]},{"mesh":0,"translation":[0,3,0]}],
        "buffers":[{"byteLength":96}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"material":0}]}],
        "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.6,1],"metallicFactor":0.7,"roughnessFactor":0.3}}]})");
    auto text=doc.dump();while(text.size()%4)text+=' ';
    std::vector<std::uint8_t> bytes;
    const auto u32=[&](std::uint32_t v){for(unsigned i=0;i<4;++i)bytes.push_back(std::uint8_t(v>>(8*i)));};
    u32(0x46546c67);u32(2);u32(unsigned(12+8+text.size()+8+sizeof(vertices)));
    u32(unsigned(text.size()));u32(0x4e4f534a);bytes.insert(bytes.end(),text.begin(),text.end());
    u32(sizeof(vertices));u32(0x004e4942);
    const auto* v=reinterpret_cast<const std::uint8_t*>(vertices);bytes.insert(bytes.end(),v,v+sizeof(vertices));
    return blob(bytes.data(),bytes.size());
}
static auto publish=[](const std::string&,const void* p,std::uint32_t n){return blob(p,n);};
static void must_reject(const std::function<void()>& action) {
    bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
    check(rejected,"invalid equipment accepted");
}
int wmain(int argc,wchar_t** argv)try {
    check(argc==3 || argc==4,"usage: check-gltf-equipment body.glb profile.json [clothing-directory]");
    using W=OwnedRender::EquipmentWeapon;
    const W sword{true,false,false,false},shield{true,false,true,false},staff{true,false,false,true},two{true,true,false,false};
    std::array<W,4> slots{sword,shield,{},two};
    check(OwnedRender::EquipmentLeftHand(slots,0)==1,"sword/shield");
    check(OwnedRender::EquipmentLeftHand(slots,3)==-1,"two handed suppresses left");
    slots[1]=sword;check(OwnedRender::EquipmentLeftHand(slots,1)==0,"switched dual wield");
    slots[0]=staff;check(OwnedRender::EquipmentLeftHand(slots,0)==-1,"staff/sword exclusion");
    slots[1]=staff;check(OwnedRender::EquipmentLeftHand(slots,1)==0,"dual staff");
    auto preview=slots;preview[0]={};check(OwnedRender::EquipmentLeftHand(preview,1)==-1,"removed preview item");
    check(OwnedRender::EquipmentLeftHand(slots,1)==0,"preview independence");
    check(OwnedRender::EquipmentLeftHand(slots,4)==-1,"empty current slot");
    Json entry={{"profile","human.v1"},{"resource","models/items/test.glb"},{"socket","socket_hand_r"}};
    Json registry={{"items",{{"sword01",{{"materials",{{"iron",{{"hand_r",Json::array({entry})},{"hand_l",Json::array()}}}}}}}}}};
    owned::VisualEquipmentItem selected;selected.shape="sword01";selected.material="iron";selected.slot="hand_r";
    check(item_bindings(registry,selected).size()==1,"selected attachment");
    selected.slot="hand_l";check(item_bindings(registry,selected).empty(),"explicit empty visual");
    selected.slot="hand_r";selected.material="missing";must_reject([&]{item_bindings(registry,selected);});
    selected.material="iron";auto broken=registry;
    broken["items"]["sword01"]["materials"]["iron"]["hand_r"][0]["resource"]="models/items/../body.glb";
    must_reject([&]{item_bindings(broken,selected);});
    broken=registry;broken["items"]["sword01"]["materials"]["iron"]["hand_r"].push_back(entry);
    must_reject([&]{item_bindings(broken,selected);});
    selected.shape="unregistered";check(item_bindings(registry,selected).empty(),"unregistered item");
    const auto profile=read_file(argv[2]);Json doc;
    const auto itemBytes=fixture(doc);const auto item=decode(itemBytes,profile,publish);
    check(item->rigid && !item->gpu_animation && !item->animation,"item must not allocate animation");
    check(item->geometry.joints.size()==1 && item->rest_vertices.size()==3,"rigid geometry");
    check_near(item->rest_vertices[0].position[0],1,"grip/hierarchy x");
    check_near(item->rest_vertices[0].position[1],3,"grip/hierarchy y");
    check_near(item->rest_vertices[2].position[1],4,"child mesh position");
    check_near(item->anchors.at("cast")[0],0,"anchor grip");
    check_near(item->anchors.at("cast")[1],5,"anchor position");
    for(const auto& v:item->rest_vertices)check(v.weights[0]==65535&&v.joints[0]==0,"one socket influence");
    check_near(item->geometry.materials[0].roughness,.3f,"item material");
    // Production group packing with an unrelated host skeleton node prefix.
    // This catches accidental item-node indexing, body material substitution,
    // and clearing the already built body groups when appending attachments.
    owned::RetainedVisualSample visual;visual.values.unit=true;
    visual.prepared=std::make_shared<owned::VisualPreparedResources>();
    auto assembly=std::make_shared<owned::VisualAssemblyAsset>();
    auto skeleton=std::make_shared<owned::ModelCpuAsset>();skeleton->nodes.resize(7);
    assembly->skeleton=skeleton;
    owned::VisualPartAsset proxy;proxy.name="body";
    auto geometry=std::make_shared<owned::FigureCpuAsset>();geometry->indices={0,1,2};proxy.geometry=geometry;
    assembly->parts.push_back(proxy);visual.assembly=assembly;
    auto runtime=std::make_shared<owned::skn::RuntimeAsset>();
    runtime->gltf_asset=item;runtime->asset=item->geometry;runtime->asset.vertices=item->rest_vertices;
    owned::build_skn_groups(visual,runtime);
    const std::vector<unsigned> socketNodes{31};
    owned::build_skn_groups(visual,runtime,&socketNodes,true,false);
    check(visual.prepared->merged_groups.size()==2,"attachment erased body groups");
    check(visual.prepared->merged_part_group[0]==0,"attachment replaced body membership");
    const auto& group=visual.prepared->merged_groups[1];
    check(group.gltf_weapon&&!group.gltf_arrow&&group.skn_asset==runtime,"attachment visibility/material owner");
    owned::skn::GpuVertex vertex{};
    std::memcpy(&vertex,group.geometry->skin_vertices.data(),sizeof(vertex));
    check(vertex.joints[0]==31&&vertex.weights[0]==1&&vertex.correction_node==UINT32_MAX,"attachment GPU socket index");
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c)
        check_near(vertex.inverse[0][r*4+c],r==c?1.f:0.f,"attachment inverse bind");
    auto changed=doc;changed["asset"]["extras"]["ei"]["socket"]="wrong";
    must_reject([&]{decode(fixture(changed),profile,publish);});
    changed=doc;changed["asset"]["extras"]["ei"]["grip"]["scale"]={1,0,1};
    must_reject([&]{decode(fixture(changed),profile,publish);});
    changed=doc;changed["asset"]["extras"]["ei"]["kind"]="body";
    must_reject([&]{decode(fixture(changed),profile,publish);});
    changed=doc;changed["asset"]["extras"]["ei"]["socket"]="CC_Base_Forearm.L";
    const auto forearm=decode(fixture(changed),profile,publish);
    check(forearm->socket=="CC_Base_Forearm.L" && !forearm->gpu_animation,"profile bone attachment");
    changed["asset"]["extras"]["ei"]["socket"]="CC_Base_Absent";
    must_reject([&]{decode(fixture(changed),profile,publish);});
    changed=doc;changed["nodes"][0]["scale"]={-1,1,1};
    must_reject([&]{decode(fixture(changed),profile,publish);});
    changed=doc;changed["nodes"][0]["scale"]={2,3,4};
    const auto scaled=decode(fixture(changed),profile,publish);
    check_near(scaled->rest_vertices[0].position[0],1,"scaled grip x");
    check_near(scaled->rest_vertices[0].position[1],9,"scaled hierarchy y");
    std::cout<<"PASS host selection/removal/private preview, registry rejection, rigid GLB hierarchy/grip/material/anchors\n";
    const auto body=decode(read_file(argv[1]),profile,publish);const auto& animation=*body->animation;
    const auto& forearmChain=body->attachment_chain("CC_Base_Forearm.L");
    check(&forearmChain==&body->attachment_chain("CC_Base_Forearm.L") && body->sockets.size()==6,
        "bone attachment chain must be cached once");
    std::vector<std::shared_ptr<owned::gltf::Asset>> garments;
    if(argc==4) {
     Json wardrobe=Json::parse(R"({"items":{"shirt":{"materials":{"wool":{"armor_3":[{"profile":"human.v1","resource":"models/items/shirt.glb","binding":"skin"}]}}}}})");
     owned::VisualEquipmentItem shirt;shirt.kind=owned::VisualEquipmentItem::Kind::armor;shirt.shape="shirt";shirt.material="wool";shirt.slot="armor_3";
     auto binding=item_bindings(wardrobe,shirt);check(binding.size()==1 && binding[0].skinned && binding[0].socket.empty(),"wardrobe skin selection");
     shirt.kind=owned::VisualEquipmentItem::Kind::weapon;must_reject([&]{item_bindings(wardrobe,shirt);});
     shirt.kind=owned::VisualEquipmentItem::Kind::armor;
     wardrobe["items"]["shirt"]["materials"]["wool"]["armor_3"][0]["socket"]="socket_head";
     must_reject([&]{item_bindings(wardrobe,shirt);});
     for(const wchar_t* name:{L"vlcsh-wool.glb",L"vlcpt-wool.glb",L"vlcgl-wool.glb",L"vlcbt-wool.glb"}) {
      auto garment=decode(read_file((std::filesystem::path(argv[3])/name).c_str()),profile,publish);
      check(garment->wearable && !garment->rigid && !garment->animation && !garment->gpu_animation,"garment allocated animation");
      auto nodes=wearable_nodes(*garment,*body);
      check(wearable_radius(*garment,*body,nodes)>0,"garment bounds");
      auto rt=std::make_shared<owned::skn::RuntimeAsset>();rt->gltf_asset=garment;rt->asset=garment->geometry;rt->asset.vertices=garment->rest_vertices;
      for(auto& node:nodes)node+=7;
      auto before=visual.prepared->merged_groups.size();
      owned::build_skn_groups(visual,rt,&nodes);
      check(visual.prepared->merged_groups.size()>before && visual.prepared->merged_part_group[0]==0,"garment replaced body/weapon membership");
      const auto& group=visual.prepared->merged_groups.back();
      check(!group.gltf_weapon && !group.gltf_arrow && group.skn_asset==rt,"garment visibility/material ownership");
      for(std::size_t offset=0;offset<group.geometry->skin_vertices.size();offset+=sizeof(owned::skn::GpuVertex)) {
       owned::skn::GpuVertex v{};std::memcpy(&v,group.geometry->skin_vertices.data()+offset,sizeof(v));
       check(v.correction_node==UINT32_MAX,"unbound garment corrective");
       for(unsigned k=0;k<4;++k)if(v.weights[k])check(v.joints[k]>=7 && v.joints[k]<7+body->geometry.joints.size(),"garment palette range");
      }
      const unsigned used=garment->rest_vertices[0].joints[0];
      auto old=garment->geometry.joints[used].inverse[0];garment->geometry.joints[used].inverse[0]+=1;
      must_reject([&]{wearable_nodes(*garment,*body);});garment->geometry.joints[used].inverse[0]=old;
      auto oldName=garment->geometry.joints[used].name;garment->geometry.joints[used].name="missing-profile-joint";
      must_reject([&]{wearable_nodes(*garment,*body);});garment->geometry.joints[used].name=oldName;
      garments.push_back(std::move(garment));
     }
     const auto registryBlob=read_file((std::filesystem::path(argv[3]).parent_path()/"registry.v1.json").c_str());
     const auto actualRegistry=Json::parse(registryBlob.bytes->begin(),registryBlob.bytes->end());
     const char* shapes[]={"vlcsh","vlcpt","vlcgl","vlcbt"};
     const char* armorSlots[]={"armor_3","armor_4","armor_6","armor_5"};
     for(unsigned mask=0;mask<16;++mask) {
      unsigned count=0;
      for(unsigned i=0;i<4;++i)if(mask&(1u<<i)) {
       owned::VisualEquipmentItem selected;selected.kind=owned::VisualEquipmentItem::Kind::armor;
       selected.shape=shapes[i];selected.material="wool";selected.slot=armorSlots[i];
       auto list=item_bindings(actualRegistry,selected);check(list.size()==1 && list[0].skinned,"actual wool binding");count+=unsigned(list.size());
       for(const char* material:{"cotton","silk red","silk blue","astral thread"}) {
        selected.material=material;check(item_bindings(actualRegistry,selected).empty(),"unconverted cloth material substituted");
       }
      }
      unsigned expected=0;for(unsigned i=0;i<4;++i)expected+=(mask>>i)&1;
      check(count==expected,"partial/removed outfit selection");
     }
     std::cout<<"PASS four clip-free garments, shared palette, bind rejection, actual registry 16 equip/remove combinations and body/weapon group preservation\n";
    }
    const unsigned idle=named_clip(animation,"idle");check(idle!=UINT32_MAX,"body idle");
    unsigned moving=idle;
    for(unsigned c=0;c<animation.clips.size();++c)if(animation.clips[c].name.find("attack")!=std::string::npos){moving=c;break;}
    float maxError=0;
    for(float blend:{1.f,.35f}) {
        Playback p{moving,idle,animation.clips[moving].duration*.37f,animation.clips[idle].duration*.61f,blend};
        const auto reference=evaluate(animation,p).joints;
        const auto actual=gpu(animation,p),actual12=gpu12(animation,p);
        for(const auto& garment:garments) {
            const auto nodes=wearable_nodes(*garment,*body);
            for(const auto& v:garment->rest_vertices)for(unsigned r=0;r<3;++r) {
                float expected=0,got11=0,got12=0;
                for(unsigned k=0;k<4;++k)if(v.weights[k]) {
                    auto point=owned::skn::point(garment->geometry.joints[v.joints[k]].inverse,v.position);
                    unsigned j=nodes[v.joints[k]];float a=reference[j][r*4+3],b=actual[j].rows[r][3],c=actual12[j].rows[r][3];
                    for(unsigned col=0;col<3;++col){a+=reference[j][r*4+col]*point[col];b+=actual[j].rows[r][col]*point[col];c+=actual12[j].rows[r][col]*point[col];}
                    float weight=v.weights[k]/65535.f;expected+=a*weight;got11+=b*weight;got12+=c*weight;
                }
                check_near(expected,got11,"garment D3D11 shared pose");check_near(expected,got12,"garment D3D12 shared pose");
            }
        }
        for(const auto& named:body->sockets) {
            const unsigned index=named.second.back().node;
            const auto matrix=socket_pose(animation,named.second,p);
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c) {
                check_near(matrix[r*4+c],reference[index][r*4+c],"socket sparse/full CPU");
                check_near(matrix[r*4+c],actual[index].rows[r][c],"socket/D3D11");
                check_near(matrix[r*4+c],actual12[index].rows[r][c],"socket/D3D12");
                maxError=(std::max)(maxError,std::abs(matrix[r*4+c]-actual12[index].rows[r][c]));
            }
            for(const auto& v:item->rest_vertices)for(unsigned r=0;r<3;++r) {
                float a=matrix[r*4+3],b=actual12[index].rows[r][3];
                for(unsigned c=0;c<3;++c){a+=matrix[r*4+c]*v.position[c];b+=actual12[index].rows[r][c]*v.position[c];}
                check_near(a,b,"attached item GPU transform");
            }
        }
    }
    const auto out=std::filesystem::path(argv[0]).parent_path()/"rigid-fixture.glb";
    std::ofstream file(out,std::ios::binary);file.write(reinterpret_cast<const char*>(itemBytes.bytes->data()),itemBytes.bytes->size());
    std::cout<<"PASS five actual-body sockets plus forearm bone, weapon transforms and transition on D3D11/D3D12 WARP; max error "<<maxError<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
