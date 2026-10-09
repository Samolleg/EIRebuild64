#include "ModCampRepair.h"
Json CheckCampRepairG3(const std::string& directory){
 std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
 if(input["cases"].size()!=5)throw std::runtime_error("Expected five focused repair representatives");
 Json out={{"success",true},{"cases",Json::array()},{"worldCreated",false},{"unitCreated",false},
  {"screenCreated",false},{"hostCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false}};
 for(const auto& row:input["cases"]){
  const bool selected=row["mode"]=="selected";const int shop=row["shop"],size=int(row["items"].size());
  const float coefficient=row["coefficient"].get<float>();int money=row["money"];
  std::vector<float> hp;for(const auto& item:row["items"])hp.push_back(item["hp"].get<float>());
  auto metal=[&](int i){return row["items"][i]["metal"].get<bool>();};
  int price=ModCampRepair::Quote(true,shop,size,metal,[&](int i){const auto& item=row["items"][i];
   return ModCampRepair::Price(item["price"].get<float>(),coefficient,hp[i],item["maxhp"].get<float>())*(selected?item["count"].get<int>():1);});
  auto valid=selected?ModCampRepair::SelectedValidity(true,size,price):ModCampRepair::Validity{price>0,false};
  if(money<price)valid.confirm=false;
  Json events=Json::array(),returned=Json::array();const std::string action=row["action"];
  if(action=="confirm"&&valid.confirm){
   ModCampRepair::RestoreItems(true,selected?ModCampRepair::Mode::Selected:ModCampRepair::Mode::Equipped,shop,size,metal,[&](int i){hp[i]=row["items"][i]["maxhp"].get<float>();events.push_back("restore"+std::to_string(i));});
   if(selected)for(int i=0;i<size;++i){returned.push_back(i);events.push_back("return"+std::to_string(i));}
   money-=price;events.push_back("charge"+std::to_string(price));
  }else if(action=="cancel"&&valid.cancel){
   for(int i=0;i<size;++i){returned.push_back(i);events.push_back("return"+std::to_string(i));}
  }
  // Transfers/money are normalized callbacks for unchanged camp ownership code;
  // this component does not instantiate CParty or exercise live UI/refcounts.
  out["cases"].push_back({{"price",price},{"validOK",valid.confirm},{"validCancel",valid.cancel},
   {"hp",hp},{"money",money},{"returned",returned},{"events",events}});
 }
 return out;
}
