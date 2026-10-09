#include "ModIdleTooltips.h"
// Explicit inspection entry before gameplay startup; actual Lua/data, borrowed stats.
Json CheckIdleTooltipsG3(const std::string& directory){
 ModRuntime::Host host(directory);auto* package=ModRuntime::Current();std::string owner;
 const auto base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","spells.sdb",base,owner);if(!theSpellDatabase.Load("",owner.c_str()))throw std::runtime_error("Idle spell database unavailable");
 package->ReadResource("database.res","perks.pdb",base,owner);if(!thePerkDatabase.LoadDatabase("",owner.c_str()))throw std::runtime_error("Idle perk database unavailable");
 Json report={{"success",true},{"luaMainExecuted",true},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},{"gameplayActivated",false},
  {"itemIdle",package->CombatSetting("useLS_ObjectItemsOnIdle")},{"weaponIdle",package->CombatSetting("useLS_ObjectWeaponsOnIdle")},{"hd",package->CombatSetting("JMVGraphics2")},
  {"profiles",Json::array()},{"spells",Json::array()},{"durability",Json::array()}};
 int checks=0;auto require=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
 const CString labels[ModIdleTooltips::QUICK_LABELS]={"mana","effect","speed","range","area","duration","targets","m","s","instant","unit","fixed"};
 SRPGUserUnitStats profiles[4]={};
 for(int p=0;p<4;++p){auto& st=profiles[p];st.cIntelligence=BYTE(25+p*15);st.cAutoSkills[RPGASKL_ELEMENTAL]=BYTE(1+p*30);st.cAutoSkills[RPGASKL_SENSE]=BYTE(120-p*20);st.cAutoSkills[RPGASKL_ASTRAL]=BYTE(17+p*20);
  for(int k=0;k<RPG_NUM_PERKS;++k)st.cPerks[k]=BYTE(p);Json perks=Json::array();for(int k=0;k<RPG_NUM_MAGIC_SUBTYPES;++k)perks.push_back(st.GetPerkModifier(RPGPRK_FIRE+k));
  report["profiles"].push_back({{"intelligence",st.cIntelligence},{"schools",{st.cAutoSkills[RPGASKL_ELEMENTAL],st.cAutoSkills[RPGASKL_SENSE],st.cAutoSkills[RPGASKL_ASTRAL]}},{"perks",perks}});
 }
 const auto original=std::vector<BYTE>((BYTE*)profiles,(BYTE*)profiles+sizeof(profiles));
 for(int i=0;i<theSpellDatabase.spells.GetNumEntries();++i){auto* prototype=theSpellDatabase.spells[i];CSpell spell;spell.SetSubtypeID(DWORD(i)<<16);require(spell.Setup(prototype)!=0,"Idle spell setup failed");
  const float addition=package->InfoProperty("GetSpellAdds",prototype->szCodeName,2);Json cases=Json::array();
  for(int profile=0;profile<4;++profile)for(int charge=0;charge<3;++charge){const auto& st=profiles[profile];float mp=charge==0?0:charge==1?13.9f:83.5f,maxMP=83.5f;
   Json weapons=Json::array();
   for(int kind=0;kind<3;++kind)for(int difficulty:{0,2}){
    ModIdleTooltips::WeaponInput in;in.name="weapon";in.damageType="piercing";in.damage="damage";in.actionsLabel="actions";in.rangeLabel="range";in.spellLabel="spell";in.blockPowerLabel="power";in.blockLabel="block";in.parryLabel="parry";in.hpLabel="hp";in.effectLabel="effect";in.manaLabel="mana";
    in.shield=kind==1;in.broken=kind==2;in.minimum=12.6f;in.maximum=23.4f;in.actions=17;in.range=3.9f;in.hp=31.9f;in.maxHP=83.5f;in.blockPower=24.6f;in.block=45.3f;in.parry=18.7f;
    in.mp=mp;in.maxMP=maxMP;in.spell=&spell;in.stats=&st;in.spellName="spellname";in.spellDamageType=std::to_string(ModIdleTooltips::DamageType(spell)).c_str();in.difficulty=difficulty;
    weapons.push_back({{"kind",kind},{"difficulty",difficulty},{"text",std::string((LPCSTR)ModIdleTooltips::Weapon(in))}});
   }
   cases.push_back({{"profile",profile},{"charge",charge},{"mp",mp},{"maxMP",maxMP},{"skill",ModIdleTooltips::Skill(st,spell)},
    {"wand",std::string((LPCSTR)ModIdleTooltips::Quick("quick",IDB_WAND,&spell,st,mp,maxMP,addition,labels))},
    {"scroll",std::string((LPCSTR)ModIdleTooltips::Quick("quick",IDB_SCROLL,&spell,st,mp,maxMP,addition,labels))},{"weapons",weapons}});
  }
  report["spells"].push_back({{"id",i},{"name",std::string((LPCSTR)prototype->szCodeName)},{"subtype",int(spell.GetMagicSubtype())},{"power",spell.GetPower()},{"addition",addition},
   {"mana",spell.GetManaCost()},{"speed",spell.GetSpeed()},{"range",spell.GetRange()},{"area",spell.GetArea()},{"duration",spell.GetDuration()},{"targets",spell.GetNumTargets()},
   {"unit",spell.IsOnUnit()!=0},{"rune",prototype->bRuneAllowed[SDBAR_EFFECT]!=0},{"cases",cases}});
 }
 for(auto values:std::vector<std::pair<float,float>>{{31.9f,83.5f},{0,100},{-1,100},{101,100},{1,0},{1,-1}}){int percent=0;bool valid=ModIdleTooltips::Durability(values.first,values.second,percent);report["durability"].push_back({{"hp",values.first},{"maximum",values.second},{"valid",valid},{"percent",percent}});}
 int percent=0;require(!ModIdleTooltips::Durability(std::numeric_limits<float>::infinity(),100,percent),"Nonfinite durability accepted");
 require(ModIdleTooltips::Quick("quick",IDB_WAND,nullptr,profiles[0],0,100,0,labels)=="quick","Empty spell changed quick text");
 require(ModIdleTooltips::Quick("quick",-1,nullptr,profiles[0],0,100,0,labels)=="quick","Other quick changed text");
 require(memcmp(profiles,original.data(),sizeof(profiles))==0,"Idle tooltip mutated shown stats");
 require(!theWorldServer.IsMapLoaded(),"Idle component loaded server map");report["assertions"]=checks;return report;
}
