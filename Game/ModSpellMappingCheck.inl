#include "ModSpellRules.h"
Json CheckSpellMappingG3(const std::string& directory){
 std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
 Json out={{"success",true},{"portions",Json::array()},{"identity",Json::array()},{"point",Json::array()},{"dispatch",Json::array()},
 {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false}};
 for(const auto&r:input["portions"]){SRPGAttackPortion ap;ap.Clear();bool handled=ModSpells::ExtendedElementalPortion(r["id"],r["power"],r["duration"],ap,r["magic"]);
 Json attacks=Json::array();for(float v:ap.fpAttacks)attacks.push_back(v);
 out["portions"].push_back({{"handled",handled},{"attacks",attacks},{"damage",ap.fpDamage},{"area",ap.bAreaEffect!=FALSE},{"percentage",ap.fpDamagePercentage}});}
 for(const auto&r:input["identity"]){int id=r["id"];out["identity"].push_back({{"override",ModSpellIdentity::AttackOverride(id)},{"wall",ModSpellIdentity::Wall(id)},{"extended",ModSpellIdentity::ExtendedLifecycle(id)}});}
 for(const auto&r:input["point"])out["point"].push_back(ModSpellIdentity::PointEligible(r["release"],r["ready"],r["owner"],r["mana"],r["cost"],r["range"],r["distance2"],r["radius"]));
 for(const auto&r:input["dispatch"]){Json commands=Json::array();ModSpellIdentity::ActiveSelfCast([&](){commands.push_back("unit");});out["dispatch"].push_back(commands);}
 return out;
}
