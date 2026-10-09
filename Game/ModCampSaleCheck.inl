#include "ModCampSale.h"
#include "AssortmentGenerator.h"
Json CheckCampSaleG3(const std::string& directory){
 std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
 if(input["cases"].size()!=5)throw std::runtime_error("Expected five focused sale representatives");
 Json out={{"success",true},{"cases",Json::array()},{"hostCreated",false},{"worldCreated",false},
  {"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false},{"partyCreated",false}};
 for(const auto& row:input["cases"]){
  SIDBMaterial material{};CAssortmentGenerator generator(nullptr);int old=row["oldMax"];
  generator.AddMaterial(&material,old,old);
  std::vector<float> hp;std::vector<bool> alive;
  for(const auto& item:row["items"]){hp.push_back(item["hp"].get<float>());alive.push_back(true);}
  Json events=Json::array(),erased=Json::array();int money=row["money"],quote=row["quote"],shop=row["shop"];
  const std::string action=row["action"];
  if(action=="confirm"){
   for(int i=int(row["items"].size())-1;i>=0;--i){const auto& item=row["items"][i];
    auto live=[&](){if(!alive[i])throw std::runtime_error("Item accessed after treasure erase");};
    ModCampSale::Process(shop,item["metal"].get<bool>(),[&](){live();const std::string kind=item["kind"];
     auto role=kind=="weapon"?ModCampSale::Role::Weapon:kind=="armor"?ModCampSale::Role::Armor:kind=="quick"?ModCampSale::Role::Quick:kind=="instruction"?ModCampSale::Role::Instruction:kind=="treasure"?ModCampSale::Role::Treasure:kind=="material"?ModCampSale::Role::Material:ModCampSale::Role::Other;
     return ModCampSale::Info{role,item["components"].get<int>(),item["count"].get<int>()};},
     [&](bool shape){live();events.push_back(std::string(shape?"forbidShape":"forbidMaterial")+std::to_string(i));return item[shape?"shapeForbidden":"materialForbidden"].get<bool>();},
     [&](ModCampSale::Event event,int low,int high){live();switch(event){
      case ModCampSale::Event::Restore:hp[i]=item["maxhp"].get<float>();events.push_back("restore"+std::to_string(i));break;
      case ModCampSale::Event::Shape:events.push_back("shape"+std::to_string(i));break;
      case ModCampSale::Event::Material:{const int count=generator.GetMaterialAmount(&material);generator.AddMaterial(&material,count+low,count+high);events.push_back("material"+std::to_string(count+low)+"/"+std::to_string(count+high));break;}
      case ModCampSale::Event::TreasureErase:alive[i]=false;erased.push_back(i);events.push_back("erase"+std::to_string(i));break;
     }});
   }
   for(size_t i=0;i<alive.size();++i)if(alive[i])events.push_back("sellToShop"+std::to_string(i));
   money-=quote;events.push_back("charge"+std::to_string(quote));
  }else if(action=="cancel"){
   for(size_t i=0;i<alive.size();++i)events.push_back("sellToParty"+std::to_string(i));
  }
  // Real pure generator setter/getter, shared sale policy and normalized transfer
  // callbacks only. No actual CParty refcount, Lua predicate or UI execution.
  out["cases"].push_back({{"hp",hp},{"materialMax",generator.GetMaterialAmount(&material)},
   {"erased",erased},{"money",money},{"events",events}});
 }
 return out;
}
