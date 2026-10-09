#include "ModSpellOwner.h"
Json CheckSpellOwnerG3(const std::string&directory){
 std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
 Json out={{"success",true},{"native54",Json::array()},{"invalid54",Json::array()},{"explosive_audio",Json::array()},{"danger",Json::array()},
 {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false}};
 auto trace54=[&](const Json&r,bool valid){
  Json trace=Json::array();float power=valid?r["power"].get<float>():0;
  ModSpellOwner::Native54Start(valid,power,valid?r["quiet"].get<bool>():false,
   [&](float value){trace.push_back({{"op","heal"},{"actor","caster"},{"power",value}});},
   [&](SRPGAttackPortion&ap){
    if(!r["self_filter_allows"].get<bool>())return;
    int attack=-1;for(int i=0;i<int(RPG_NUM_ATTACK_TYPES);++i)if(ap.fpAttacks[i]!=0){if(attack!=-1||ap.fpAttacks[i]!=1)throw std::runtime_error("Invalid native54 shares");attack=i;}
    trace.push_back({{"op","damage"},{"actor","caster"},{"author","caster"},{"power",ap.fpDamage},{"attack",attack},{"area",ap.bAreaEffect!=FALSE},
     {"hl",ap.hl==RPGHL_ALL?"ALL":"OTHER"},{"toHit",ap.fpToHit==FLT_MAX?"FLT_MAX":"OTHER"},{"backstab",ap.fBackStabMultiplier},{"calledShot",ap.bCalledShot!=FALSE},{"percentage",ap.fpDamagePercentage}});
   },
   [&](){trace.push_back({{"op","particle"},{"actor","target"},{"resource","PTID_VAMP"},{"showPower","actual GetShowPower"},{"keep",false}});},
   [&](){return !r["target_full_after_prior_ops"].get<bool>();},
   [&](const char*sound,float scale){trace.push_back({{"op","sound"},{"actor","target"},{"resource",sound}});trace.push_back({{"op","light"},{"actor","ptTarget"},{"radius",scale==-1?"negative actual GetLightRadius":"OTHER"}});},
   [&](bool action,int timer){Json phase={{"op","phase"},{"phase",action?"ACTION":"FINISH"}};if(action)phase["timer"]=timer;trace.push_back(phase);});
  return trace;
 };
 for(const auto&r:input["native54"])out["native54"].push_back(trace54(r,true));
 for(const auto&r:input["invalid54"])out["invalid54"].push_back(trace54(r,false));
 for(const auto&r:input["explosive_audio"]){
  Json trace=Json::array();int owner=0;int*manager=r["client"].get<bool>()&&r["manager"].get<bool>()?&owner:nullptr;
  // A descriptor spy, never a CSoundManager or a device.
  ModSpellOwner::ExplosiveSound(r["world_matches_current"].get<bool>(),manager,[&](int*,const ModSpellOwner::SoundRequest&request){
   trace.push_back({{"op","BeginFX"},{"resource",request.resource},{"priority",request.priority},{"position","impact"},{"near",request.nearDistance},{"far",request.farDistance},{"loop",request.loop},{"speech",false}});
  });out["explosive_audio"].push_back(trace);
 }
 for(const auto&r:input["danger"])out["danger"].push_back(ModSpellOwner::AdmitExtendedDangerAfterStart(r["id"],r["action"]));
 int timer=30,ticks=0;while(++ticks<100&&!ModSpellOwner::ActionExpired(timer)){}
 out["action"]={{"ticks",ticks},{"timerAfter",timer},{"finished",ticks<100}};
 return out;
}
