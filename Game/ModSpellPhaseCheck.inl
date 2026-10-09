#include "ModSpellPhase.h"
#include "ModSpellOwner.h"
#include "ModSpellDanger.h"
Json CheckSpellPhaseG3(const std::string& directory){
 std::ifstream file(fs::path(directory)/"oracle.json");Json input;file>>input;
 Json out={{"success",true},{"phase_ticks",Json::array()},{"phase_runs",Json::array()},{"routes",Json::array()},{"start_particles",Json::array()},{"owner_traces",Json::array()},
 {"hostCreated",false},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},{"saveRead",false}};
 auto eventName=[](ModSpellPhase::Event e){switch(e){
 case ModSpellPhase::Event::WallDamage:return "wall_damage";case ModSpellPhase::Event::RadiusDamage:return "radius_damage";
 case ModSpellPhase::Event::HideCaster:return "hide_caster";case ModSpellPhase::Event::CaptureFirewallSoundCP:return "capture_firewall_sound_cp";
 case ModSpellPhase::Event::DangerRemove:return "danger_remove";case ModSpellPhase::Event::SoundRemove:return "sound_remove";
 case ModSpellPhase::Event::MainRemove:return "main_remove";case ModSpellPhase::Event::FirewallEndSound:return "firewall_end_sound";
 case ModSpellPhase::Event::PoisonEndSound:return "poison_end_sound";case ModSpellPhase::Event::LightFade:return "light_fade";
 case ModSpellPhase::Event::InvokeStrike:return "invoke_strike";case ModSpellPhase::Event::Finish:return "finish";
 }throw std::runtime_error("Unknown phase event");};
 for(const auto& r:input["phase_ticks"]){
  int time=r["timer"];Json events=Json::array();ModSpellPhase::Action(r["raw"],time,r["main"],[&](ModSpellPhase::Event e){events.push_back(eventName(e));});
  out["phase_ticks"].push_back({{"timer",time},{"events",events}});
 }
 for(const auto& r:input["phase_runs"]){
  int time=r["timer"];bool main=r["main"],finish=false;Json trace=Json::array();
  for(int i=0;i<300&&!finish;++i){Json events=Json::array();ModSpellPhase::Action(r["raw"],time,main,[&](ModSpellPhase::Event e){
   events.push_back(eventName(e));if(e==ModSpellPhase::Event::MainRemove)main=false;if(e==ModSpellPhase::Event::Finish)finish=true;
  });trace.push_back({{"timer",time},{"events",events}});}
  if(!finish)throw std::runtime_error("Phase did not finish");out["phase_runs"].push_back(trace);
 }
 for(const auto&r:input["routes"])out["routes"].push_back(ModSpellPhase::Enabled(r["current"],r["particles"],r["raw"]));
 for(const auto&r:input["start_particles"]){auto particle=ModSpellPhase::StartParticle(r["raw"]);
  out["start_particles"].push_back(particle==PTID_FIREBALLLIGHT?"PTID_FIREBALLLIGHT":particle==PTID_NUKELIGHT?"PTID_NUKELIGHT":particle==PTID_POISONFOGLIGHT?"PTID_POISONFOGLIGHT":"OTHER");
 }
 for(const auto&r:input["owner_traces"]){
  int raw=r["raw"];bool mapped=r["owned_before"],main=r["main"];Json calls=Json::array();
  bool shape=ModSpellDanger::StockLoadKind(raw)||raw==61||raw==62||raw==67||raw==68||raw==73||raw==74;
  auto change=[&](bool add){if(!main||!shape)return;ModSpellDanger::Change(add,mapped,[&](bool value){calls.push_back(value?"add":"remove");});};
  if(r["begin"]=="load")ModSpellDanger::ReconstructNativeLoad(raw,main,[&](int){change(true);});
  else if(ModSpellPhase::Enabled(true,r["particles"],raw)){if(ModSpellOwner::AdmitExtendedDangerAfterStart(raw,true))change(true);}
  else if(ModSpellDanger::StockLoadKind(raw))change(true); // actual current-mod retail Start selector
  std::string cleanup=r["cleanup"];
  if(cleanup=="action_finish_destroy"){change(false);main=false;change(false);if(mapped)change(false);}
  else if(cleanup=="target_removed"){if(mapped)change(false);}
  else if(cleanup=="caster_removed_finish"){change(false);if(mapped)change(false);}
  else throw std::runtime_error("Unknown owner trace");
  out["owner_traces"].push_back({{"calls",calls},{"mapped",mapped}});
 }
 return out;
}
