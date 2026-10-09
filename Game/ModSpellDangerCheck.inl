#include "ModSpellDanger.h"
Json CheckSpellLoadG3(const std::string&directory){
 std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
 Json out={{"success",true},{"load",Json::array()},{"lifecycle",Json::array()},
 {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false}};
 auto shape=[](int raw){return raw==6?"wall":raw==7?"lightning_wall":"circle";};
 for(const auto&r:input["load"]){
  DWORD subtype=r["subtype"];int raw=int(subtype>>16);bool mapped=false;Json calls=Json::array();
  ModSpellDanger::ReconstructNativeLoad(raw,r["main_effect"].get<bool>(),[&](int kind){
   ModSpellDanger::Change(true,mapped,[&](bool){calls.push_back({{"op","add"},{"kind",kind},{"shape",shape(kind)}});});
  });
  out["load"].push_back({{"calls",calls},{"mapped",mapped},{"phase",r["phase"]}});
 }
 for(const auto&r:input["lifecycle"]){
  int raw=r["raw"];bool mapped=false;Json calls=Json::array();
  auto change=[&](bool add){ModSpellDanger::Change(add,mapped,[&](bool value){calls.push_back({{"op",value?"add":"remove"},{"kind",raw},{"shape",shape(raw)}});});};
  ModSpellDanger::ReconstructNativeLoad(raw,r["main_effect"].get<bool>(),[&](int){change(true);});
  std::string order=r["order"];
  if(order=="repeat_add_then_finish")if(mapped)change(true);
  // Normalized ownership events; no CEffectSpell, world, map or references created.
  if(order!="destroy_only"&&order!="target_removed")change(false); // Finish after Action or caster-removal continuation.
  if(mapped)change(false); // Existing destructor removes only what it owns.
  out["lifecycle"].push_back({{"calls",calls},{"mapped",mapped}});
 }
 return out;
}
