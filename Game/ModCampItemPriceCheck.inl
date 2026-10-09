#include "ModCampItemPrice.h"
Json CheckCampItemPriceG3(const std::string& directory){
 std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
 if(input["cases"].size()!=5)throw std::runtime_error("Expected five focused trade representatives");
 Json out={{"success",true},{"cases",Json::array()},{"hostCreated",false},{"worldCreated",false},
  {"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false}};
 for(const auto& row:input["cases"]){
  Json prices=Json::array(),events=Json::array();int price=0,money=row["money"];
  for(const auto& item:row["items"]){
   const float base=item["price"].get<float>(),coefficient=item["coefficient"].get<float>();
   const std::string kind=item["kind"];
   int unit=(kind=="weapon"||kind=="armor")?ModCampItemPrice::WeaponArmor(base,coefficient,row["repair"].get<float>(),item["hp"].get<float>(),item["maxhp"].get<float>()):int(base*coefficient);
   prices.push_back(unit);int total=unit*item["count"].get<int>();price+=item["buy"].get<bool>()?total:-total;
  }
  const bool cancel=!row["items"].empty(),confirm=cancel&&money>=price;const std::string action=row["action"];
  if(action=="confirm"&&confirm){
   for(size_t i=0;i<row["items"].size();++i)if(!row["items"][i]["buy"].get<bool>())events.push_back("sellToShop"+std::to_string(i));
   for(size_t i=0;i<row["items"].size();++i)if(row["items"][i]["buy"].get<bool>())events.push_back("buyToParty"+std::to_string(i));
   money-=price;events.push_back("charge"+std::to_string(price));
  }else if(action=="cancel"&&cancel){
   for(size_t i=0;i<row["items"].size();++i)if(row["items"][i]["buy"].get<bool>())events.push_back("buyToShop"+std::to_string(i));
   for(size_t i=0;i<row["items"].size();++i)if(!row["items"][i]["buy"].get<bool>())events.push_back("sellToParty"+std::to_string(i));
  }
  // Transfers are normalized source callbacks; no actual party/refcount/sale
  // preprocessing or UI virtual dispatch is executed by this small component.
  out["cases"].push_back({{"unitPrices",prices},{"price",price},{"validOK",confirm},{"validCancel",cancel},{"money",money},{"events",events}});
 }
 return out;
}
