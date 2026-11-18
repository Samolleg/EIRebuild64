#define wmain animation_regression_main
#include "check-gltf-animation.cpp"
#undef wmain
#include "../Game/OwnedRender/EquipmentSelection.h"
#include "gltf_equipment_registry.h"
#include "gltf_weapon_constraints.h"
#include "gltf_breast.h"
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
    check(argc==3 || argc==4 || argc==6 || argc==7 || argc==8 || argc==10,"usage: check-gltf-equipment body.glb profile.json [clothing-directory [wolf.png leopard.png [models-directory [helmet-models-directory [original-body.glb hair-directory]]]]]");
    using W=OwnedRender::EquipmentWeapon;
    {
        using namespace owned::gltf;
        const Affine initial{0,-2,0,3, 2,0,0,4, 0,0,2,5, 0,0,0,1};
        const Point3 outside{0,0,0},inside{1,0,0},tip{1,2,3},nock{-1,3,4};
        Affine corrected{};
        const auto close=[](const Point3& a,const Point3& b){return weapon_length(weapon_subtract(a,b))<1e-4f;};
        WeaponTwoHandSupport timed;timed.clip_prefixes={"uattack14"};
        timed.clip_windows["uattack14"]={.4f,.5f,.7f,.8f};
        check(weapon_support_weight(timed,"uattack14",.2f,1.f)==0 &&
              weapon_support_weight(timed,"uattack14",.6f,1.f)==1 &&
              weapon_support_weight(timed,"uattack14",.9f,1.f)==0 &&
              weapon_support_weight(timed,"other",.6f,1.f)==0,"timed support keeps reload free");
        check_near(weapon_support_weight(timed,"uattack14",.45f,1.f),.5f,"support fade in");
        check_near(weapon_support_weight(timed,"uattack14",.75f,1.f),.5f,"support fade out");
        check(weapon_segment(corrected,initial,outside,inside,tip,nock),"bow string correction");
        check(close(weapon_point(corrected,outside),tip) && close(weapon_point(corrected,inside),nock),
              "bow string joins actual limb and drawing hand");
        const Point3 hand{2,3,4},grip{-3,4,6},forward{1,0,0};
        check(weapon_aim(corrected,initial,outside,forward,hand,grip),"projectile aim");
        check(close(weapon_point(corrected,outside),hand),"projectile nock follows drawing hand");
        const auto direction=weapon_direction(corrected,forward),target=weapon_subtract(grip,hand);
        const auto factor=weapon_length(direction)/weapon_length(target);
        check(close(direction,{target[0]*factor,target[1]*factor,target[2]*factor}) &&
              std::abs(weapon_length(direction)-2.f)<1e-4f,"projectile aims at bow without rescaling");
        check(weapon_aim(corrected,initial,outside,forward,hand,{2,-2,4}),"antiparallel aim remains finite");
        check(!weapon_aim(corrected,initial,outside,forward,hand,hand),"degenerate aim is rejected");
        check(weapon_draw_weight(20,59,20,45)==0 && weapon_draw_weight(25,59,20,45)==1 &&
              weapon_draw_weight(44,59,20,45)==1 && weapon_draw_weight(49,59,20,45)==0 &&
              weapon_draw_weight(24,59,20,45)==.5f,"arrow pickup, draw and smooth release");
        check(weapon_draw_weight(58,59,55,5)==0 && weapon_draw_weight(2,59,55,5)==1 &&
              weapon_draw_weight(20,59,55,5)==0,"wrapped arrow visibility");
    }
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
    auto offhand=registry;
    offhand["items"]["sword01"]["materials"]["iron"]["hand_r"][0]["offset"]={.19,-.002,0};
    const auto offsetBinding=item_bindings(offhand,selected);
    check(offsetBinding.size()==1 && std::abs(offsetBinding[0].offset[0]-.19f)<1e-6f,
          "hand-specific offset retained with common resource");
    offhand["items"]["sword01"]["materials"]["iron"]["hand_r"][0]["offset"]={0,0};
    must_reject([&]{item_bindings(offhand,selected);});
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
    auto withOrm=shared;
    withOrm["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"][0]["metallicRoughness"]="models/items/textures/orm.png";
    hood.material="wolf hide";
    check(item_bindings(withOrm,hood)[0].textures[0].metallic_roughness=="models/items/textures/orm.png",
          "metallic/roughness selection");
    auto invalidOrm=withOrm;
    invalidOrm["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"][0]["metallicRoughness"]="models/items/textures/../orm.png";
    must_reject([&]{item_bindings(invalidOrm,hood);});
    hood.material="special";const auto special=item_bindings(shared,hood);
    check(special.size()==1 && special[0].resource=="models/items/vlfh-special.glb" &&
          special[0].textures.empty(),"material-specific model takes priority");
    hood.material="hidden";check(item_bindings(shared,hood).empty(),"explicit empty overrides common model");
    {
     // Conditions remove different source entries depending on the outfit.
     // Texture binding indices still identify the original default list.
     auto conditional=shared;
     auto& entries=conditional["items"]["vlfh"]["defaults"]["armor_0"];
     auto second=entries[0];second["resource"]="models/items/vlfh-alt.glb";
     const Json condition={{"slot","armor_4"},{"shape","pants"}};
     entries[0]["requiresEquipment"]=condition;second["unlessEquipment"]=condition;
     entries.push_back(second);
     auto& maps=conditional["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"];
     auto secondMap=maps[0];secondMap["binding"]=1u;secondMap["baseColor"]="models/items/textures/leopard.png";
     maps.push_back(secondMap);
     hood.material="wolf hide";
     owned::VisualEquipmentItem pants;pants.kind=owned::VisualEquipmentItem::Kind::armor;
     pants.slot="armor_4";pants.shape="pants";
     const std::vector<owned::VisualEquipmentItem> without{hood},with{hood,pants};
     const auto absent=item_bindings(conditional,hood,&without),present=item_bindings(conditional,hood,&with);
     check(absent.size()==1 && absent[0].resource=="models/items/vlfh-alt.glb" &&
           absent[0].textures.size()==1 && absent[0].textures[0].resource=="models/items/textures/leopard.png",
           "conditional common binding lost its texture");
     check(present.size()==1 && present[0].resource=="models/items/vlfh.glb" &&
           present[0].textures.size()==1 && present[0].textures[0].resource=="models/items/textures/wolf.png",
           "conditional common binding selected wrong texture");
    }
    {
     auto conditional=shared;
     auto& entries=conditional["items"]["vlfh"]["defaults"]["armor_0"];
     const Json condition={{"slot","armor_1"},{"shape","*"},{"hasSelection",true}};
     auto armorHair=entries[0];armorHair["resource"]="models/items/braid-armor.glb";
     armorHair["requiresEquipment"]=condition;
     entries[0]["unlessEquipment"]=condition;entries.push_back(armorHair);
     const Json plate={{"profile","human.v1"},{"resource","models/items/plate.glb"},{"binding","skin"}};
     conditional["items"]["plate"]["defaults"]["armor_1"]=Json::array({plate});
     conditional["items"]["plate"]["materials"]["common"]["armor_1"]={{"textures",Json::array()}};
     conditional["items"]["plate"]["materials"]["hidden"]["armor_1"]=Json::array();
     conditional["items"]["plate"]["materials"]["explicit"]["armor_1"]=Json::array({plate});
     owned::VisualEquipmentItem armor;armor.kind=owned::VisualEquipmentItem::Kind::armor;
     armor.slot="armor_1";armor.shape="plate";armor.material="common";
     hood.material="wolf hide";
     const auto selectedHair=[&](const owned::VisualEquipmentItem& value) {
       const std::vector<owned::VisualEquipmentItem> outfit{hood,value};
       return item_bindings(conditional,hood,&outfit).at(0).resource;
     };
     check(selectedHair(armor)=="models/items/braid-armor.glb","common armor selection");
     armor.material="explicit";check(selectedHair(armor)=="models/items/braid-armor.glb","explicit armor selection");
     armor.material="hidden";check(selectedHair(armor)=="models/items/vlfh.glb","empty armor selection");
     armor.material="unknown";check(selectedHair(armor)=="models/items/vlfh.glb","unknown armor material");
     armor.shape="unregistered";check(selectedHair(armor)=="models/items/vlfh.glb","unregistered armor");
     armor.shape="plate";armor.material="common";armor.slot="armor_2";
     check(selectedHair(armor)=="models/items/vlfh.glb","other armor slot");
     const std::vector<owned::VisualEquipmentItem> none{hood};
     check(item_bindings(conditional,hood,&none).at(0).resource=="models/items/vlfh.glb","removed armor");
     auto invalid=conditional;invalid["items"]["vlfh"]["defaults"]["armor_0"][0]["unlessEquipment"]["hasSelection"]=false;
     must_reject([&]{item_bindings(invalid,hood,&none);});
     invalid=conditional;invalid["items"]["vlfh"]["defaults"]["armor_0"][0]["unlessEquipment"]["shape"]="plate";
     must_reject([&]{item_bindings(invalid,hood,&none);});
     std::cout<<"PASS any armor slot with configured visual and empty/removed variants\n";
    }
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
    {
     const std::string bodyKey="models/characters/control.glb";
     auto skin=[](const char* file){return Json{{"profile","human.v1"},{"resource",std::string("models/items/")+file+".glb"},{"binding","skin"},{"visibility","always"}};};
     Json layers;auto free=skin("shirt-free"),tucked=skin("shirt-tucked");
     const Json condition={{"slot","armor_4"},{"shape","pants"}};
     free["unlessEquipment"]=condition;tucked["requiresEquipment"]=condition;
     layers["items"]["shirt"]["materials"]["wool"]["armor_3"]=Json::array({free,tucked});
     layers["items"]["pants"]["materials"]["wool"]["armor_4"]=Json::array({skin("pants")});
     layers["items"]["pants"]["materials"]["missing"]["armor_4"]=Json::array();
     auto upper=skin("base-upper"),lower=skin("base-lower");
     upper["slot"]="base_upper";upper["hiddenBySlots"]=Json::array({"armor_3"});
     lower["slot"]="base_lower";lower["hiddenBySlots"]=Json::array({"armor_4"});
     layers["baseLayers"][bodyKey]=Json::array({upper,lower});
     owned::VisualEquipmentItem shirt,pants;shirt.kind=pants.kind=owned::VisualEquipmentItem::Kind::armor;
     shirt.shape="shirt";pants.shape="pants";shirt.slot="armor_3";pants.slot="armor_4";shirt.material=pants.material="wool";
     for(unsigned mask=0;mask<4;++mask){
      std::vector<owned::VisualEquipmentItem> outfit;if(mask&1)outfit.push_back(shirt);if(mask&2)outfit.push_back(pants);
      const auto selected=outfit_bindings(layers,outfit,bodyKey);check(selected.size()==2,"base layer equip/remove atomic selection");
      if(mask&1)check(selected[0].resource==(mask&2?"models/items/shirt-tucked.glb":"models/items/shirt-free.glb"),"whole-outfit condition");
     }
     pants.material="missing";auto empty=outfit_bindings(layers,{pants},bodyKey);check(empty.size()==2 && empty[0].slot=="base_upper" && empty[1].slot=="base_lower","empty registered visual must not hide base layer");
     check(outfit_bindings(layers,{},"models/characters/other.glb").empty(),"base layer body identity isolation");
     auto broken=layers;broken["baseLayers"][bodyKey].push_back(upper);must_reject([&]{outfit_bindings(broken,{},bodyKey);});
     broken=layers;broken["items"]["shirt"]["materials"]["wool"]["armor_3"][0]["unlessEquipment"]=nullptr;
     must_reject([&]{outfit_bindings(broken,{shirt},bodyKey);});
     std::cout<<"PASS conditional outfit variants, base layer equip/remove, empty visual and body isolation\n";
    }
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
    auto ormA=std::make_shared<const owned::skn::Dds>();
    auto ormB=std::make_shared<const owned::skn::Dds>();
    auto idA=std::make_shared<const std::uint8_t>(0),idB=std::make_shared<const std::uint8_t>(0);
    std::map<unsigned,std::shared_ptr<const owned::skn::Dds>> overridesA{{0,textureA}},overridesB{{0,textureB}};
    std::map<unsigned,std::shared_ptr<const owned::skn::Dds>> ormOverridesA{{0,ormA}},ormOverridesB{{0,ormB}};
    std::map<unsigned,std::shared_ptr<const std::uint8_t>> idsA{{0,idA}},idsB{{0,idB}};
    owned::build_skn_groups(visual,runtime,&socketNodes,true,false,&overridesA,&ormOverridesA,&idsA);
    const auto firstKey=owned::geometry_keys.back();
    owned::build_skn_groups(visual,runtime,&socketNodes,true,false,&overridesB,&ormOverridesB,&idsB);
    check(owned::geometry_keys.back()==firstKey,"texture variant changed geometry cache key");
    const auto& firstVariant=visual.prepared->merged_groups[2];
    const auto& secondVariant=visual.prepared->merged_groups[3];
    check(firstVariant.skn_asset==secondVariant.skn_asset && firstVariant.skn_base_color_override==textureA &&
          secondVariant.skn_base_color_override==textureB && firstVariant.skn_metallic_roughness_override==ormA &&
          secondVariant.skn_metallic_roughness_override==ormB && firstVariant.skn_material_identity==idA &&
          secondVariant.skn_material_identity==idB,"texture variants failed to share geometry or remain distinct");
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
    changed=doc;
    changed["nodes"]=Json::array({{{"mesh",0}}});changed["scenes"][0]["nodes"]=Json::array({0});
    changed["asset"]["extras"]["ei"]["grip"]["translation"]={0,0,0};
    changed["asset"]["extras"]["ei"]["projectileAim"]={{"version",1},{"nock",{0,0,0}},{"forward",{1,0,0}}};
    check(bool(decode(fixture(changed),profile,publish)->projectile_aim),"valid projectile metadata");
    changed["asset"]["extras"]["ei"]["projectileAim"]["forward"]={0,0,0};
    must_reject([&]{decode(fixture(changed),profile,publish);});
    changed=doc;changed["nodes"][0]["scale"]={-1,1,1};
    must_reject([&]{decode(fixture(changed),profile,publish);});
    changed=doc;changed["nodes"][0]["scale"]={2,3,4};
    const auto scaled=decode(fixture(changed),profile,publish);
    check_near(scaled->rest_vertices[0].position[0],1,"scaled grip x");
    check_near(scaled->rest_vertices[0].position[1],9,"scaled hierarchy y");
    std::cout<<"PASS host selection/removal/private preview, registry rejection, rigid GLB hierarchy/grip/material/anchors\n";
    const auto body=decode(read_file(argv[1]),profile,publish);const auto& animation=*body->animation;
    if(const auto bowPath=_wgetenv(L"EI_TEST_WEAPON_BOW")) {
     const auto arrowPath=_wgetenv(L"EI_TEST_WEAPON_ARROW");check(arrowPath,"weapon arrow fixture missing");
     const auto bow=decode(read_file(bowPath),profile,publish);
     const auto arrow=decode(read_file(arrowPath),profile,publish);
     check(bow->rigid && bow->bow_constraint && bow->mechanism.size()==4 && bow->mechanism_animation &&
           bow->mechanism_animation->clips.size()==3 && !bow->gpu_animation,"saved bow mechanism contract");
     check(arrow->rigid && arrow->projectile_aim && !arrow->animation && !arrow->gpu_animation,
           "saved projectile aim contract");
     for(const auto& name:bow->bow_constraint->drawing_bones)check(!body->attachment_chain(name).empty(),"drawing hand marker");
     for(const auto& name:bow->bow_constraint->grip_bones)check(!body->attachment_chain(name).empty(),"bow hand marker");
     const std::array<owned::gltf::PhysiqueInput,3> shapes{{owned::gltf::neutral_physique,
          {19,31,.55f,.23f,.94f},{45,5,.9f,.1f,.6f}}};
     struct Shot { const char* clip;float frames;std::array<float,3> samples; };
     const Shot shots[]={{"uattack01",59,{25,35,44}},{"uattack02",55,{22,32,40}},
                         {"uattack03",76,{27,37,48}}};
     const owned::gltf::Affine root{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
     for(const auto& input:shapes) {
      const auto shape=body->physique.resolve(input,unsigned(animation.nodes.size()),body->cache_key);
      for(const auto& shot:shots) {
       const auto b=owned::gltf::named_clip(animation,shot.clip);
       const auto w=owned::gltf::named_clip(*bow->mechanism_animation,shot.clip);
       check(b!=UINT32_MAX && w!=UINT32_MAX,"saved shooting clip");
       for(float frame:shot.samples) {
        owned::gltf::Playback bp{b,UINT32_MAX,animation.clips[b].duration*frame/shot.frames,0,1};
        owned::gltf::Playback wp{w,UINT32_MAX,bow->mechanism_animation->clips[w].duration*frame/shot.frames,0,1};
        const auto socket=owned::gltf::socket_pose(animation,body->attachment_chain(bow->socket),bp,root,shape.get());
        const auto arrowSocket=owned::gltf::socket_pose(animation,body->attachment_chain(arrow->socket),bp,root,shape.get());
        const auto palm=[&](const std::array<std::string,4>& markers) {
         owned::gltf::Point3 result{};
         for(const auto& name:markers){const auto p=owned::gltf::socket_pose(animation,body->attachment_chain(name),bp,root,shape.get());
          for(unsigned i=0;i<3;++i)result[i]+=.25f*p[i*4+3];}
         return result;
        };
        const auto drawing=palm(bow->bow_constraint->drawing_bones),grip=palm(bow->bow_constraint->grip_bones);
        std::array<owned::gltf::Affine,16> parts{};
        for(const auto& node:bow->mechanism)
         parts[node.node]=owned::gltf::multiply(socket,owned::gltf::socket_local(*bow->mechanism_animation,node,wp));
        check(owned::gltf::weapon_bow(parts,*bow->bow_constraint,drawing,1),"real bow string solver");
        for(const auto& string:bow->bow_constraint->strings) {
         check(owned::gltf::weapon_length(owned::gltf::weapon_subtract(
             owned::gltf::weapon_point(parts[string.cord],string.inside),drawing))<.001f,
             "real bow nock follows drawing hand");
         check(owned::gltf::weapon_length(owned::gltf::weapon_subtract(
             owned::gltf::weapon_point(parts[string.cord],string.outside),
             owned::gltf::weapon_point(parts[string.limb],string.tip)))<.001f,"real bow string follows limb");
        }
        auto aimed=arrowSocket;
        check(owned::gltf::weapon_projectile(aimed,arrowSocket,*arrow->projectile_aim,drawing,grip,1),"real projectile solver");
        check(owned::gltf::weapon_length(owned::gltf::weapon_subtract(
            owned::gltf::weapon_point(aimed,arrow->projectile_aim->nock),drawing))<.001f,"real arrow nock follows hand");
       }
      }
     }
     std::cout<<"PASS saved bow/projectile metadata, shared hand markers and mechanism clips\n";
     std::cout<<"PASS three physique variants and three shooting actions: both string ends and arrow follow hands\n";
    }
    if(const auto weaponPath=_wgetenv(L"EI_TEST_TWO_HAND")) {
     using namespace owned::gltf;
     const auto weapon=decode(read_file(weaponPath),profile,publish);
     check(weapon->rigid && weapon->two_hand_support,"saved two-hand support metadata");
     const auto& support=*weapon->two_hand_support;
     const auto plan=weapon_arm_plan(*body,support);
     check(plan==weapon_arm_plan(*body,support) && !plan->nodes.empty() && plan->nodes.size()<=256,
           "support arm plan must be cached on the shared body");
     const Affine root{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
     const std::array<PhysiqueInput,3> shapes{{neutral_physique,{19,31,.55f,.23f,.94f},{45,5,.9f,.1f,.6f}}};
     unsigned corrected=0;
     for(const auto& input:shapes) {
      const auto shape=body->physique.resolve(input,unsigned(animation.nodes.size()),body->cache_key);
      for(const char* clip:{"2hscidle01","2hsuattack04","2hacidle01","2hauattack04"}) {
       const auto c=named_clip(animation,clip);check(c!=UINT32_MAX,"two-hand action exists");
       for(float fraction:{.1f,.37f,.7f}) {
        Playback pose{c,UINT32_MAX,animation.clips[c].duration*fraction,0,1};
        std::array<Affine,256> arm{};
        for(unsigned i=0;i<plan->nodes.size();++i) {
         const auto& node=plan->nodes[i];
         arm[i]=node.parent<0 ? socket_pose(animation,body->attachment_chain(support.upper),pose,root,shape.get()) :
             multiply(arm[unsigned(node.parent)],socket_local(animation,node.joint,pose,shape.get()));
        }
        for(unsigned marker:plan->markers) {
         const auto index=plan->nodes[marker].joint.node;
         const auto sparse=socket_pose(animation,body->attachment_chain(body->geometry.joints[index].name),pose,root,shape.get());
         for(unsigned k=0;k<12;++k)check_near(arm[marker][k],sparse[k],"cached arm plan and sparse pose");
        }
        const auto origin=[](const Affine& x){return Point3{x[3],x[7],x[11]};};
        const auto shoulder=origin(arm[plan->upper]),elbow=origin(arm[plan->forearm]),wrist=origin(arm[plan->hand]);
        Point3 palm{};for(unsigned marker:plan->markers)for(unsigned k=0;k<3;++k)palm[k]+=.25f*arm[marker][k*4+3];
        const auto socket=socket_pose(animation,body->attachment_chain(weapon->socket),pose,root,shape.get());
        WeaponArmSolution solved;
        if(!weapon_support_ik(solved,socket,support,shoulder,elbow,wrist,palm,1))continue;
        ++corrected;
        check_near(weapon_length(weapon_subtract(solved.new_elbow,shoulder)),weapon_length(weapon_subtract(elbow,shoulder)),
                   "two-hand upper arm length");
        check_near(weapon_length(weapon_subtract(solved.new_wrist,solved.new_elbow)),weapon_length(weapon_subtract(wrist,elbow)),
                   "two-hand forearm length");
        auto palmMatrix=root;palmMatrix[3]=palm[0];palmMatrix[7]=palm[1];palmMatrix[11]=palm[2];
        const auto moved=weapon_rotate_around(palmMatrix,solved.forearm_rotation,elbow,solved.new_elbow);
        const auto newPalm=origin(moved),grip=weapon_point(socket,support.grip),axis=weapon_direction(socket,support.axis);
        const auto gap=[&](const Point3& p) {const auto d=weapon_subtract(p,grip);const auto len=weapon_length(axis);
         const float dot=(d[0]*axis[0]+d[1]*axis[1]+d[2]*axis[2])/(len*len);
         return weapon_length({d[0]-dot*axis[0],d[1]-dot*axis[1],d[2]-dot*axis[2]});};
        check(gap(newPalm)<=gap(palm)+1e-4f,"two-hand solver must move the palm toward the grip");
       }
      }
     }
     check(corrected>=20,"two-hand support exercised across clips and body shapes");
     std::cout<<"PASS cached arm plan, two-hand contact and arm lengths across three physique variants\n";
    }
    if(const auto weaponPath=_wgetenv(L"EI_TEST_CROSSBOW")) {
     using namespace owned::gltf;
     const auto weapon=decode(read_file(weaponPath),profile,publish);
     check(weapon->two_hand_support && !weapon->mechanism.empty(),"crossbow mechanism and support metadata");
     const auto& support=*weapon->two_hand_support;
     const auto plan=weapon_arm_plan(*body,support);
     check(plan && support.clip_windows.size()==3,"crossbow cached arm plan and timed clips");
     const Affine root{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
     const std::array<PhysiqueInput,3> shapes{{neutral_physique,{19,31,.55f,.23f,.94f},{45,5,.9f,.1f,.6f}}};
     unsigned corrected=0;
     for(const auto& input:shapes) {
      const auto shape=body->physique.resolve(input,unsigned(animation.nodes.size()),body->cache_key);
      for(const char* clip:{"uattack14","uattack15","uattack16"}) {
       const auto c=named_clip(animation,clip);check(c!=UINT32_MAX,"crossbow action exists");
       const auto window=support.clip_windows.at(clip);
       check(weapon_support_weight(support,clip,window[0]*.5f,1)==0 &&
             weapon_support_weight(support,clip,(window[1]+window[2])*.5f,1)==1 &&
             weapon_support_weight(support,clip,(window[3]+1)*.5f,1)==0,
             "crossbow support releases hand during reloading");
       const float fraction=(window[1]+window[2])*.5f;
       Playback pose{c,UINT32_MAX,animation.clips[c].duration*fraction,0,1};
       const auto origin=[](const Affine& x){return Point3{x[3],x[7],x[11]};};
       const auto bone=[&](const std::string& name){return origin(socket_pose(animation,body->attachment_chain(name),pose,root,shape.get()));};
       const auto shoulder=bone(support.upper),elbow=bone(support.forearm),wrist=bone(support.hand);
       Point3 palm{};for(const auto& marker:support.markers) {const auto point=bone(marker);for(unsigned k=0;k<3;++k)palm[k]+=.25f*point[k];}
       const auto socket=socket_pose(animation,body->attachment_chain(weapon->socket),pose,root,shape.get());
       WeaponArmSolution solved;
       if(weapon_support_ik(solved,socket,support,shoulder,elbow,wrist,palm,1)) {
        ++corrected;
        check_near(weapon_length(weapon_subtract(solved.new_elbow,shoulder)),weapon_length(weapon_subtract(elbow,shoulder)),"crossbow upper arm length");
        check_near(weapon_length(weapon_subtract(solved.new_wrist,solved.new_elbow)),weapon_length(weapon_subtract(wrist,elbow)),"crossbow forearm length");
       }
      }
     }
     check(corrected>=5,"crossbow support exercised in firing poses across physiques");
     std::cout<<"PASS timed crossbow firing support and unmodified reload across three actions and physiques\n";
    }
    if(const auto directory=_wgetenv(L"EI_TEST_WEAPON_DIRECTORY")) {
     unsigned count=0,animated=0,bows=0,projectiles=0,twohand=0,crossbow_support=0;
     for(const auto& file:std::filesystem::directory_iterator(directory)) {
      if(!file.is_regular_file() || file.path().extension()!=L".glb")continue;
      const auto weapon=decode(read_file(file.path().c_str()),profile,publish);
      check(weapon->rigid && !weapon->gpu_animation && std::isfinite(weapon->radius) && weapon->radius>0,
            "saved weapon geometry contract");
      ++count;animated+=!weapon->mechanism.empty();bows+=bool(weapon->bow_constraint);
      projectiles+=bool(weapon->projectile_aim);twohand+=bool(weapon->two_hand_support);
      if(!weapon->mechanism.empty() && weapon->two_hand_support) {
       ++crossbow_support;check(weapon->two_hand_support->clip_windows.size()==3,"crossbow support windows");
      }
     }
     check(count==90 && animated==11 && bows==6 && projectiles==1 && twohand==24 && crossbow_support==5,"saved weapon catalog coverage");
     std::cout<<"PASS 90 GLBs, 11 mechanisms, 6 constrained bows, one projectile and 24 supported two-hand weapons\n";
    }
    if(const auto catalogPath=_wgetenv(L"EI_TEST_WEAPON_CATALOG")) {
     const auto modelsPath=_wgetenv(L"EI_TEST_WEAPON_MODELS");check(modelsPath,"weapon models path missing");
     const std::filesystem::path root=modelsPath;
     const auto data=read_file(catalogPath);const auto catalog=Json::parse(data.bytes->begin(),data.bytes->end());
     ++owned::fixture_generation;owned::fixture_files.clear();owned::fixture_reads.clear();
     const auto put=[&](const std::string& key) {
      if(owned::fixture_files.count(key))return;
      check(key.rfind("models/",0)==0,"weapon resource prefix");
      auto value=read_file((root/std::filesystem::path(key.substr(7))).c_str());
      value.identity.canonical_name=key;owned::fixture_files.emplace(key,std::move(value));
     };
     put("models/registry.v1.json");auto profileFixture=profile;profileFixture.identity.canonical_name="profiles/human.v1.json";
     owned::fixture_files.emplace("profiles/human.v1.json",std::move(profileFixture));
     const auto& bytes=*owned::fixture_files.at("models/registry.v1.json").bytes;
     const auto catalogRegistry=Json::parse(bytes.begin(),bytes.end());
     std::map<std::string,std::shared_ptr<const owned::gltf::Asset>> shared;
     unsigned selections=0,visible=0,empty=0;
     for(const auto& form:catalog.at("items").items()) {
      const auto family=form.value().at("family").get<std::string>();
      for(const auto& material:form.value().at("materials").items())for(const char* hand:{"hand_r","hand_l"}) {
       owned::VisualEquipmentItem value;value.kind=owned::VisualEquipmentItem::Kind::weapon;
       value.shape=form.key();value.material=material.key();value.slot=hand;
       const auto bindings=item_bindings(catalogRegistry,value);
       for(const auto& binding:bindings) {
        put(binding.resource);
        for(const auto& texture:binding.textures){put(texture.resource);if(!texture.metallic_roughness.empty())put(texture.metallic_roughness);}
       }
       const auto selected=select_equipment({value},"models/characters/profile-test.glb");
       const bool expected=!material.value().is_null() && !(value.slot=="hand_l" && (family=="bow" || family=="crossbow"));
       check(selected.size()==bindings.size() && expected==!selected.empty(),"weapon material/slot selection");
       for(const auto& part:selected) {
        check(part.asset->rigid && !part.asset->gpu_animation && !part.asset->animation,"weapon shares body program");
        const auto existing=shared.emplace(part.asset->resource_key,part.asset).first;
        check(existing->second==part.asset,"weapon geometry must be shared across materials and hands");
       }
       ++selections;if(selected.empty())++empty;else ++visible;
      }
     }
     check(select_equipment({},"models/characters/profile-test.glb").empty(),"weapon unequip");
     check(selections==1710 && visible==1194 && empty==516 && shared.size()==90,"weapon selection coverage");
     std::cout<<"PASS "<<selections<<" real weapon selections, "<<visible<<" visible, "<<empty
              <<" empty, "<<shared.size()<<" shared geometry resources and unequip\n";
    }
    if(argc==10) {
     const auto originalBody=decode(read_file(argv[8]),profile,publish);
     owned::gltf::BreastState originalPhysics,updatedPhysics;
     check(originalPhysics.bind(originalBody) && updatedPhysics.bind(body),"Nala physics bones missing after hair separation");
     std::set<std::pair<std::string,unsigned>> originalNodes,updatedNodes;
     for(const auto& value:originalPhysics.affected)
      originalNodes.emplace(originalBody->geometry.joints.at(value.node).name,value.side);
     for(const auto& value:updatedPhysics.affected)
      updatedNodes.emplace(body->geometry.joints.at(value.node).name,value.side);
     check(!originalNodes.empty() && originalNodes==updatedNodes,"Nala physics affected-bone set changed");
     for(unsigned side=0;side<2;++side)
      check(originalBody->geometry.joints.at(originalPhysics.targets[side]).name==
            body->geometry.joints.at(updatedPhysics.targets[side]).name,"Nala physics target changed");
     for(const wchar_t* name:{L"hair-full.glb",L"hair-helmet.glb",L"hair-mask.glb"}) {
      const auto hair=decode(read_file((std::filesystem::path(argv[9])/name).c_str()),profile,publish);
      check(hair->wearable && !hair->animation && !hair->gpu_animation,"hair must use body pose");
      const auto nodes=wearable_nodes(*hair,*body);
      check(wearable_radius(*hair,*body,nodes)>0,"hair shared pose bounds");
     }
     std::cout<<"PASS Nala physics target and affected bones preserved; three hair skins share the body palette\n";
    }
    // Physique-enabled bodies may already cache additional sole chains.
    const auto previousChains=body->sockets.size();
    const bool forearmPresent=body->sockets.count("CC_Base_Forearm.L")!=0;
    const auto& forearmChain=body->attachment_chain("CC_Base_Forearm.L");
    check(&forearmChain==&body->attachment_chain("CC_Base_Forearm.L") &&
        body->sockets.size()==previousChains+(forearmPresent ? 0u : 1u),
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
     const auto registryBlob=read_file((std::filesystem::path(argv[3]).parent_path()/"registry.v1.json").c_str());
     const auto actualRegistry=Json::parse(registryBlob.bytes->begin(),registryBlob.bytes->end());
     std::set<std::string> resources;
     for(const auto& shape:actualRegistry.at("items"))for(const auto& material:shape.at("materials"))for(const auto& slot:material)
      if(slot.is_array())for(const auto& entry:slot)
       if(entry.value("binding","")=="skin")resources.insert(entry.at("resource").get<std::string>());
     for(const auto& layers:actualRegistry.at("baseLayers"))for(const auto& entry:layers)resources.insert(entry.at("resource").get<std::string>());
     check(resources.size()==66,"complete author wardrobe resources");
     for(const auto& name:resources) {
      auto garment=decode(read_file((std::filesystem::path(argv[3])/std::filesystem::path(name).filename()).c_str()),profile,publish);
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
     const char* shapes[]={"vlcsh","vlcpt","vlcgl","vlcbt"};
     const char* armorSlots[]={"armor_3","armor_4","armor_6","armor_5"};
     unsigned variants=0;
     for(unsigned family=0;family<3;++family)for(const char* material:{"wool","cotton","silk red","silk blue","astral thread"})for(unsigned i=0;i<4;++i){
      owned::VisualEquipmentItem selected;selected.kind=owned::VisualEquipmentItem::Kind::armor;
      selected.shape=std::string(shapes[i])+(family?std::to_string(family):"");selected.material=material;selected.slot=armorSlots[i];
      const std::vector<owned::VisualEquipmentItem> outfit{selected};const auto list=item_bindings(actualRegistry,selected,&outfit);
      if(family==2 && selected.material!="wool")check(list.empty(),"unauthored simple material substituted");
      else {check(!list.empty(),"authored variant absent");++variants;}
     }
     check(variants==44,"complete selectable wardrobe");
     for(unsigned family=0;family<3;++family)for(unsigned mask=0;mask<16;++mask) {
      std::vector<owned::VisualEquipmentItem> outfit;
      for(unsigned i=0;i<4;++i)if(mask&(1u<<i)) {
       owned::VisualEquipmentItem selected;selected.kind=owned::VisualEquipmentItem::Kind::armor;
       selected.shape=std::string(shapes[i])+(family?std::to_string(family):"");selected.material=family==2?"wool":(i%2?"silk red":"cotton");selected.slot=armorSlots[i];outfit.push_back(selected);
      }
      const auto list=outfit_bindings(actualRegistry,outfit,"models/characters/nala.glb");
      unsigned expected=0;for(unsigned i=0;i<4;++i)expected+=(mask>>i)&1;
      expected+=!(mask&1);expected+=!(mask&2);if(family==0 && (mask&2))++expected;if(family==1 && (mask&1))++expected;
      check(list.size()==expected,"partial/removed outfit layer count");
      bool upper=false,lower=false;
      for(const auto& binding:list){
       check(resources.count(binding.resource)==1 && binding.skinned,"unknown wardrobe layer");
       upper|=binding.slot=="base_upper";lower|=binding.slot=="base_lower";
       if(family==0 && binding.slot=="armor_3")check((binding.resource.find("-tucked")!=std::string::npos)==bool(mask&2),"starter shirt tuck condition");
       if(family==0 && binding.resource.find("-wrap-")!=std::string::npos)check((binding.resource.find("-wrap-over")!=std::string::npos)==bool(mask&1),"starter wrap condition");
      }
      check(upper==!(mask&1) && lower==!(mask&2),"underwear equip/remove");
     }
     std::cout<<"PASS 66 clip-free layers, 44 variants, 48 equip/remove combinations with mixed materials, shared palette, bind rejection and body/weapon preservation\n";
    }
    if(argc>=6) {
     const std::uint8_t ormPng[]={0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x1a,0x49,0x44,0x41,0x54,0x78,0xda,0x63,0x14,0x61,0x60,0xf8,0xcf,0x90,0x62,0xc4,0xc0,0x20,0x72,0x22,0xe5,0xbf,0xc8,0xff,0xff,0xff,0x01,0x29,0xf5,0x06,0xfb,0x23,0x9d,0x41,0xaf,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
     auto linearOrm=decode_metallic_roughness(blob(ormPng,sizeof(ormPng)),publish);
     check(linearOrm.mips.size()==2 && linearOrm.mips[0].data[2]==20 &&
           linearOrm.mips[0].data[5]==100 && linearOrm.mips[0].data[4]==50 &&
           linearOrm.mips[1].data[0]==101 && linearOrm.mips[1].data[1]==139 &&
           linearOrm.mips[1].data[2]==20,"ORM must use linear channels and mip averaging");
     must_reject([&]{auto invalid=blob("not a PNG",9);decode_metallic_roughness(invalid,publish);});
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
     productionRegistry["items"]["vlfh"]["materials"]["wolf hide"]["armor_0"]["textures"][0]["metallicRoughness"]="models/items/textures/orm-a.png";
     productionRegistry["items"]["vlfh"]["materials"]["leopard hide"]["armor_0"]["textures"][0]["metallicRoughness"]="models/items/textures/orm-b.png";
     productionRegistry["items"]["vlfh"]["materials"]["same-color"]=productionRegistry["items"]["vlfh"]["materials"]["wolf hide"];
     productionRegistry["items"]["vlfh"]["materials"]["same-color"]["armor_0"]["textures"][0]["metallicRoughness"]="models/items/textures/orm-b.png";
     productionRegistry["items"]["vlfh"]["defaults"]["armor_0"][0]["binding"]="skin";
     productionRegistry["items"]["vlfh"]["defaults"]["armor_0"][0].erase("socket");
     productionRegistry["items"]["vlfh"]["materials"]["special"]["armor_0"][0]["binding"]="skin";
     productionRegistry["items"]["vlfh"]["materials"]["special"]["armor_0"][0].erase("socket");
     productionRegistry["items"]["vlfh"]["materials"]["bad"]["armor_0"]=Json::parse(R"({"textures":[{"binding":0,"material":0,"baseColor":"models/items/textures/bad.png"}]})");
     productionRegistry["items"]["vlfh"]["materials"]["bad-index"]["armor_0"]=Json::parse(R"({"textures":[{"binding":0,"material":999,"baseColor":"models/items/textures/wolf.png"}]})");
     const auto registryText=productionRegistry.dump();put("models/registry.v1.json",blob(registryText.data(),registryText.size()));
     put("profiles/human.v1.json",profile);
     put("models/items/vlfh.glb",read_file((std::filesystem::path(argv[3])/L"vlcgl-wool.glb").c_str()));
     put("models/items/vlfh-special.glb",read_file((std::filesystem::path(argv[3])/L"vlcpt-wool.glb").c_str()));
     put("models/items/textures/wolf.png",read_file(argv[4]));
     put("models/items/textures/leopard.png",read_file(argv[5]));
     put("models/items/textures/orm-a.png",blob(ormPng,sizeof(ormPng)));
     put("models/items/textures/orm-b.png",read_file(argv[5]));
     put("models/items/textures/bad.png",blob("not a PNG",9));
     hood.material="wolf hide";const auto first=select_equipment({hood}, "");
     hood.material="leopard hide";const auto second=select_equipment({hood}, "");
     check(first.size()==1 && second.size()==1 && first[0].asset==second[0].asset,
           "production asset cache duplicated common GLB");
     check(owned::fixture_reads["models/items/vlfh.glb"]==1,"common GLB loaded more than once");
     check(first[0].base_color_overrides.at(0)!=second[0].base_color_overrides.at(0),
           "production texture cache aliased different materials");
     check(first[0].metallic_roughness_overrides.at(0)!=second[0].metallic_roughness_overrides.at(0) &&
           first[0].material_identities.at(0)!=second[0].material_identities.at(0),
           "different ORM maps require different D3D11 material identities");
     check(owned::skn::from_gltf(first[0].asset)==owned::skn::from_gltf(second[0].asset),
           "GPU geometry runtime asset not shared");
     hood.material="wolf hide";const auto again=select_equipment({hood}, "");
     check(again[0].base_color_overrides.at(0)==first[0].base_color_overrides.at(0),
           "selected PNG was decoded more than once while referenced");
     check(again[0].metallic_roughness_overrides.at(0)==first[0].metallic_roughness_overrides.at(0) &&
           again[0].material_identities.at(0)==first[0].material_identities.at(0),
           "same ORM material did not share its cache and batching identity");
     hood.material="same-color";const auto sameColor=select_equipment({hood}, "");
     check(sameColor[0].base_color_overrides.at(0)==first[0].base_color_overrides.at(0) &&
           sameColor[0].metallic_roughness_overrides.at(0)!=first[0].metallic_roughness_overrides.at(0) &&
           sameColor[0].material_identities.at(0)!=first[0].material_identities.at(0),
           "same color with different ORM must not share the D3D11 material");
     hood.material="special";const auto alternative=select_equipment({hood}, "");
     check(alternative.size()==1 && alternative[0].asset!=first[0].asset &&
           alternative[0].base_color_overrides.empty(),"production alternative model priority");
     hood.material="bad";must_reject([&]{select_equipment({hood}, "");});
     hood.material="bad-index";must_reject([&]{select_equipment({hood}, "");});
     hood.material="wolf hide";check(select_equipment({hood}, "")[0].asset==first[0].asset,
           "bad texture poisoned common model cache");
     std::cout<<"PASS common geometry identity, separate hood textures, model override priority and external PNG decode\n";
     std::cout<<"PASS production equipment selection, shared GLB and texture caches, bad texture isolation\n";
     if(argc>=7) {
      const auto dir=std::filesystem::path(argv[6]);++owned::fixture_generation;owned::fixture_reads.clear();
      put("models/registry.v1.json",read_file((dir/L"registry.v1.json").c_str()));
      put("models/items/vlfh.glb",read_file((dir/L"items/vlfh.glb").c_str()));
      for(const char* code:{"uq","uw","wh","ww","ub","wo","lh","pt"}) {
       const std::string resource=std::string("models/items/textures/vlfh-")+code+".png";
       put(resource,read_file((dir/"items/textures"/(std::string("vlfh-")+code+".png")).c_str()));
      }
      const char* names[]={"tiger hide","tiger hide white","wolf hide","white wolf hide", "black wolf hide","warg hide","leopard hide","black panter hide"};
      std::vector<std::vector<owned::gltf::EquipmentAsset>> actual;
      for(const char* name:names){hood.material=name;actual.push_back(select_equipment({hood}, ""));
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
      const auto& head=body->attachment_chain("socket_head");const auto index=head.back().node;
#ifndef EI_TEST_NO_SHADER_COMPILE
      const auto realGpu11=gpu(animation,realPose),realGpu12=gpu12(animation,realPose);
      for(const auto& v:actual.front()[0].asset->rest_vertices)for(unsigned row=0;row<3;++row) {
       float expected=realReference[index][row*4+3],d11=realGpu11[index].rows[row][3],d12=realGpu12[index].rows[row][3];
       for(unsigned c=0;c<3;++c){expected+=realReference[index][row*4+c]*v.position[c];
        d11+=realGpu11[index].rows[row][c]*v.position[c];d12+=realGpu12[index].rows[row][c]*v.position[c];}
       check_near(expected,d11,"real hood D3D11 socket");check_near(expected,d12,"real hood D3D12 socket");
      }
      std::cout<<"PASS actual vlfh registry: 8 materials, one rigid GLB, separate PNGs, D3D11/D3D12 head attachment\n";
#else
      const auto headPose=socket_pose(animation,head,realPose);
      for(unsigned i=0;i<12;++i)check_near(headPose[i],realReference[index][i],"real hood CPU socket");
      std::cout<<"PASS actual vlfh registry: 8 materials, one rigid GLB, separate PNGs, CPU head attachment\n";
#endif
     }
     if(argc>=8) {
      const auto dir=std::filesystem::path(argv[7]);++owned::fixture_generation;owned::fixture_reads.clear();
      const auto registryBlob=read_file((dir/L"registry.v1.json").c_str());
      const auto registry=Json::parse(registryBlob.bytes->begin(),registryBlob.bytes->end());
      put("models/registry.v1.json",registryBlob);
      unsigned shapes=0,available=0,missing=0,sampled=0;
      for(const auto& shape:registry.at("items").items()) {
       if(!shape.value().contains("defaults") || !shape.value().at("defaults").contains("armor_0"))continue;
       const auto& defaults=shape.value().at("defaults").at("armor_0");
       if(!defaults.is_array() || defaults.size()!=1)continue;
       const auto resource=defaults[0].at("resource").get<std::string>();
       if(resource.rfind("models/items/helmet-",0)!=0)continue;
       ++shapes;put(resource,read_file((dir/std::filesystem::path(resource.substr(7))).c_str()));
       std::vector<std::string> choices;
       for(const auto& variant:shape.value().at("materials").items()) {
        owned::VisualEquipmentItem item;item.kind=owned::VisualEquipmentItem::Kind::armor;
        item.shape=shape.key();item.material=variant.key();item.slot="armor_0";
        const auto parsed=item_bindings(registry,item);
        if(variant.value().at("armor_0").is_array()) {
         check(parsed.empty(),"missing helmet material must have an empty visual");++missing;continue;
        }
        check(parsed.size()==1 && parsed[0].resource==resource && parsed[0].textures.size()==1 &&
              !parsed[0].textures[0].metallic_roughness.empty(),"helmet color/ORM registry selection");
        choices.push_back(variant.key());++available;
       }
       check(!choices.empty(),"helmet has no available materials");
       std::shared_ptr<const owned::gltf::Asset> sharedAsset;
       for(const auto& choice:{choices.front(),choices.back()}) {
        owned::VisualEquipmentItem item;item.kind=owned::VisualEquipmentItem::Kind::armor;
        item.shape=shape.key();item.material=choice;item.slot="armor_0";
        const auto binding=item_bindings(registry,item)[0];
        for(const auto& name:{binding.textures[0].resource,binding.textures[0].metallic_roughness})
         put(name,read_file((dir/std::filesystem::path(name.substr(7))).c_str()));
        const auto selected=select_equipment({item},"");
        check(selected.size()==1 && selected[0].asset->rigid && selected[0].socket=="socket_head" &&
              selected[0].base_color_overrides.count(0)==1 && selected[0].metallic_roughness_overrides.count(0)==1,
              "saved helmet resource selection");
        if(sharedAsset)check(sharedAsset==selected[0].asset,"helmet materials did not share GLB geometry");
        sharedAsset=selected[0].asset;++sampled;
       }
       check(owned::fixture_reads[resource]==1,"helmet GLB loaded more than once");
      }
      check(shapes==40 && available==225 && missing==76,"helmet inventory coverage");
      std::cout<<"PASS "<<shapes<<" helmets, "<<available<<" available and "<<missing
               <<" missing material selections; "<<sampled<<" saved color/ORM pairs loaded\n";
     }
    }
    const unsigned idle=named_clip(animation,"idle");check(idle!=UINT32_MAX,"body idle");
    unsigned moving=idle;
    for(unsigned c=0;c<animation.clips.size();++c)if(animation.clips[c].name.find("attack")!=std::string::npos){moving=c;break;}
#ifdef EI_TEST_NO_SHADER_COMPILE
    for(float blend:{1.f,.35f}) {
        Playback p{moving,idle,animation.clips[moving].duration*.37f,animation.clips[idle].duration*.61f,blend};
        const auto reference=evaluate(animation,p).joints;
        for(const auto& named:body->sockets) {
            const auto matrix=socket_pose(animation,named.second,p);
            const unsigned index=named.second.back().node;
            for(unsigned i=0;i<12;++i)check_near(matrix[i],reference[index][i],"socket sparse/full CPU");
        }
    }
    std::cout<<"PASS socket and transition CPU checks; GPU tests skipped to prohibit shader compilation\n";
#else
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
#endif
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
