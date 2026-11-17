#define wmain animation_regression_main
#include "check-gltf-animation.cpp"
#undef wmain
#include "../Game/OwnedRender/EquipmentSelection.h"
#include "gltf_equipment_registry.h"
#include "retained_static_scene.h"
#include "figure_service_nodes.h"
namespace owned {
void bridge_append_probe_event(const char*,const char*) noexcept {}
// Test-only resource boundary. Selection, decoding and both production caches
// are compiled unchanged from gltf_asset.cpp; files are immutable fixture blobs.
static std::map<std::string,ResourceBlob> fixture_files;
static std::map<std::string,unsigned> fixture_reads;
static std::uint64_t fixture_generation=1,fixture_incarnation=100;
struct ResourceStore::Impl {};
ResourceStore::ResourceStore(std::uint64_t):impl_(new Impl) {}
ResourceStore::~ResourceStore() = default;
bool ResourceStore::open(const ResourceStoreConfig&,ResourceFailure& failure) { failure={};return true; }
void ResourceStore::close() noexcept {}
bool ResourceStore::resolve(const std::string& name,ResourceBlob& out,ResourceFailure& failure) const {
    ++fixture_reads[name];auto it=fixture_files.find(name);
    if(it==fixture_files.end()){failure.code=ResourceFailureCode::not_found;failure.message="fixture missing: "+name;return false;}
    out=it->second;failure={};return true;
}
bool ResourceStore::copy_selected_memory(const std::string& name,const void* data,std::uint32_t size,
                                         ResourceBlob& out,ResourceFailure& failure) const {
    const auto* bytes=static_cast<const std::uint8_t*>(data);
    out.identity.canonical_name=name;out.identity.container_incarnation=++fixture_incarnation;
    out.bytes=std::make_shared<std::vector<std::uint8_t>>(bytes,bytes+size);failure={};return true;
}
bool owned_resource_runtime_mounts(ResourceStoreConfig&,std::uint64_t* generation) noexcept {
    if(generation)*generation=fixture_generation;return true;
}
std::string ResourceIdentity::key() const { return canonical_name; }
namespace {
std::vector<std::string> geometry_keys;
template<class Build>
std::shared_ptr<const VisualPreparedResources::MergedGeometry> merged_geometry(const std::string& key,Build&& build) {
    geometry_keys.push_back(key);
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
    check(argc==3 || argc==4 || argc==6 || argc==7,"usage: check-gltf-equipment body.glb profile.json [clothing-directory [wolf.png leopard.png [models-directory]]]");
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
    const auto shared=Json::parse(R"({"items":{"vlfh":{
      "defaults":{"armor_0":[{"profile":"human.v1","resource":"models/items/vlfh.glb","socket":"socket_head"}]},
      "materials":{
        "wolf hide":{"armor_0":{"textures":[{"binding":0,"material":0,"baseColor":"models/items/textures/wolf.png"}]}},
        "leopard hide":{"armor_0":{"textures":[{"binding":0,"material":0,"baseColor":"models/items/textures/leopard.png"}]}},
        "special":{"armor_0":[{"profile":"human.v1","resource":"models/items/vlfh-special.glb","socket":"socket_head"}]},
        "hidden":{"armor_0":[]}
      }}}})");
    owned::VisualEquipmentItem hood;hood.kind=owned::VisualEquipmentItem::Kind::armor;
    hood.shape="vlfh";hood.slot="armor_0";hood.material="wolf hide";
    const auto wolf=item_bindings(shared,hood);
    hood.material="leopard hide";const auto leopard=item_bindings(shared,hood);
    check(wolf.size()==1 && leopard.size()==1 && wolf[0].resource==leopard[0].resource &&
          wolf[0].textures.size()==1 && leopard[0].textures.size()==1 &&
          wolf[0].textures[0].resource!=leopard[0].textures[0].resource,"common model with distinct textures");
    hood.material="special";const auto special=item_bindings(shared,hood);
    check(special.size()==1 && special[0].resource=="models/items/vlfh-special.glb" &&
          special[0].textures.empty(),"material-specific model takes priority");
    hood.material="hidden";check(item_bindings(shared,hood).empty(),"explicit empty overrides common model");
    hood.material="missing";must_reject([&]{item_bindings(shared,hood);});
    hood.material="wolf hide";
    auto wrong=shared;wrong["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"][0]["baseColor"]="models/items/textures/../secret.png";
    must_reject([&]{item_bindings(wrong,hood);});
    wrong=shared;wrong["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"][0]["binding"]=2u;
    must_reject([&]{item_bindings(wrong,hood);});
    wrong=shared;wrong["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"].push_back(wrong["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"][0]);
    must_reject([&]{item_bindings(wrong,hood);});
    wrong=shared;wrong["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["resource"]="models/items/ignored.glb";
    must_reject([&]{item_bindings(wrong,hood);});
    wrong=shared;wrong["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"][0]["material"]=std::uint64_t(UINT32_MAX)+1;
    must_reject([&]{item_bindings(wrong,hood);});
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
    auto textureA=std::make_shared<const owned::skn::Dds>();
    auto textureB=std::make_shared<const owned::skn::Dds>();
    std::map<unsigned,std::shared_ptr<const owned::skn::Dds>> overridesA{{0,textureA}},overridesB{{0,textureB}};
    owned::build_skn_groups(visual,runtime,&socketNodes,true,false,&overridesA);
    const auto firstKey=owned::geometry_keys.back();
    owned::build_skn_groups(visual,runtime,&socketNodes,true,false,&overridesB);
    check(owned::geometry_keys.back()==firstKey,"texture variant changed geometry cache key");
    const auto& firstVariant=visual.prepared->merged_groups[2];
    const auto& secondVariant=visual.prepared->merged_groups[3];
    check(firstVariant.skn_asset==secondVariant.skn_asset && firstVariant.skn_base_color_override==textureA &&
          secondVariant.skn_base_color_override==textureB,"texture variants failed to share geometry or remain distinct");
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
    if(argc>=4) {
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
    if(argc>=6) {
     const std::array<float,4> factor{{1,1,1,1}};
     auto a=decode_base_color(read_file(argv[4]),publish,factor);
     auto b=decode_base_color(read_file(argv[5]),publish,factor);
     check(a.mips.size()>1 && b.mips.size()>1 && a.mips[0].width==1024 && a.mips[0].height==1024 &&
           b.mips[0].width==1024 && b.mips[0].height==1024,"real hood texture dimensions/mips");
     check(*a.blob.bytes!=*b.blob.bytes,"different source textures decoded identically");
     must_reject([&]{auto invalid=blob("not a PNG",9);decode_base_color(invalid,publish,factor);});
     // Call the actual registry -> Asset cache -> external texture selection.
     // The virtual filesystem is the only stub, so changing a material cannot
     // quietly reload a GLB or alias two different texture byte owners.
     auto put=[](const std::string& name,owned::ResourceBlob value) {
       value.identity.canonical_name=name;owned::fixture_files[name]=std::move(value);
     };
     // Selection always requires the characters object, even for an item.
     auto productionRegistry=shared;productionRegistry["version"]=1;productionRegistry["characters"]=Json::object();
     productionRegistry["items"]["vlfh"]["defaults"]["armor_0"][0]["binding"]="skin";
     productionRegistry["items"]["vlfh"]["defaults"]["armor_0"][0].erase("socket");
     productionRegistry["items"]["vlfh"]["materials"]["special"]["armor_0"][0]["binding"]="skin";
     productionRegistry["items"]["vlfh"]["materials"]["special"]["armor_0"][0].erase("socket");
     productionRegistry["items"]["vlfh"]["materials"]["bad"]["armor_0"]=Json::parse(R"({"textures":[{"binding":0,"material":0,"baseColor":"models/items/textures/bad.png"}]})");
     productionRegistry["items"]["vlfh"]["materials"]["bad-index"]["armor_0"]=Json::parse(R"({"textures":[{"binding":0,"material":999,"baseColor":"models/items/textures/wolf.png"}]})");
     const auto registryText=productionRegistry.dump();put("models/registry.v1.json",blob(registryText.data(),registryText.size()));
     put("profiles/human.v1.json",profile);
     put("models/items/vlfh.glb",read_file((std::filesystem::path(argv[3])/L"vlcsh-wool.glb").c_str()));
     put("models/items/vlfh-special.glb",read_file((std::filesystem::path(argv[3])/L"vlcpt-wool.glb").c_str()));
     put("models/items/textures/wolf.png",read_file(argv[4]));
     put("models/items/textures/leopard.png",read_file(argv[5]));
     put("models/items/textures/bad.png",blob("not a PNG",9));
     hood.material="wolf hide";const auto first=select_equipment({hood});
     hood.material="leopard hide";const auto second=select_equipment({hood});
     check(first.size()==1 && second.size()==1 && first[0].asset==second[0].asset,
           "production asset cache duplicated common GLB");
     check(owned::fixture_reads["models/items/vlfh.glb"]==1,"common GLB loaded more than once");
     check(first[0].base_color_overrides.at(0)!=second[0].base_color_overrides.at(0),
           "production texture cache aliased different materials");
     check(owned::skn::from_gltf(first[0].asset)==owned::skn::from_gltf(second[0].asset),
           "GPU geometry runtime asset not shared");
     hood.material="wolf hide";const auto again=select_equipment({hood});
     check(again[0].base_color_overrides.at(0)==first[0].base_color_overrides.at(0),
           "selected PNG was decoded more than once while referenced");
     hood.material="special";const auto alternative=select_equipment({hood});
     check(alternative.size()==1 && alternative[0].asset!=first[0].asset &&
           alternative[0].base_color_overrides.empty(),"production alternative model priority");
     hood.material="bad";must_reject([&]{select_equipment({hood});});
     hood.material="bad-index";must_reject([&]{select_equipment({hood});});
     hood.material="wolf hide";check(select_equipment({hood})[0].asset==first[0].asset,
           "bad texture poisoned common model cache");
     std::cout<<"PASS common geometry identity, separate hood textures, model override priority and external PNG decode\n";
     std::cout<<"PASS production equipment selection, shared GLB and texture caches, bad texture isolation\n";
     if(argc==7) {
      const auto dir=std::filesystem::path(argv[6]);++owned::fixture_generation;owned::fixture_reads.clear();
      put("models/registry.v1.json",read_file((dir/L"registry.v1.json").c_str()));
      put("models/items/vlfh.glb",read_file((dir/L"items/vlfh.glb").c_str()));
      for(const char* code:{"uq","uw","wh","ww","ub","wo","lh","pt"}) {
       const std::string resource=std::string("models/items/textures/vlfh-")+code+".png";
       put(resource,read_file((dir/"items/textures"/(std::string("vlfh-")+code+".png")).c_str()));
      }
      const char* names[]={"tiger hide","tiger hide white","wolf hide","white wolf hide", "black wolf hide","warg hide","leopard hide","black panter hide"};
      std::vector<std::vector<owned::gltf::EquipmentAsset>> actual;
      for(const char* name:names){hood.material=name;actual.push_back(select_equipment({hood}));
       check(actual.back().size()==1 && actual.back()[0].asset->rigid &&
             actual.back()[0].socket=="socket_head" && actual.back()[0].base_color_overrides.count(0)==1,
             "real vlfh registry selection");
       check(actual.back()[0].asset==actual.front()[0].asset,"real vlfh geometry not shared");
      }
      check(owned::fixture_reads["models/items/vlfh.glb"]==1,"real vlfh GLB loaded more than once");
      check(actual.front()[0].asset->rest_vertices.size()==233 &&
            actual.front()[0].asset->geometry.materials[0].alpha==1,"real hood geometry or shadow coverage");
      const unsigned realIdle=named_clip(animation,"idle");
      Playback realPose{realIdle,realIdle,animation.clips[realIdle].duration*.37f,0,1.f};
      const auto realReference=evaluate(animation,realPose).joints;
      const auto realGpu11=gpu(animation,realPose),realGpu12=gpu12(animation,realPose);
      const auto& head=body->attachment_chain("socket_head");const auto index=head.back().node;
      for(const auto& v:actual.front()[0].asset->rest_vertices)for(unsigned row=0;row<3;++row) {
       float expected=realReference[index][row*4+3],d11=realGpu11[index].rows[row][3],d12=realGpu12[index].rows[row][3];
       for(unsigned c=0;c<3;++c){expected+=realReference[index][row*4+c]*v.position[c];
        d11+=realGpu11[index].rows[row][c]*v.position[c];d12+=realGpu12[index].rows[row][c]*v.position[c];}
       check_near(expected,d11,"real hood D3D11 socket");check_near(expected,d12,"real hood D3D12 socket");
      }
      std::cout<<"PASS actual vlfh registry: 8 materials, one rigid GLB, separate PNGs, D3D11/D3D12 head attachment\n";
     }
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
