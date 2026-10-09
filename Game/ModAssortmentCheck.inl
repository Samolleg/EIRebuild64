#include "ModAssortment.h"
#include <float.h>
// Read-only inspection access to actual generator copies; no world/item generation.
struct ModAssortmentInspection {
 static Json Stored(const CAssortmentGenerator& gen){return {{"minimum",gen.fMinSkills},{"maximum",gen.fMaxSkills}};}
 static bool Same(const CAssortmentGenerator& gen,const ModAssortment::Bounds& bounds){return memcmp(gen.fMinSkills,bounds.minimum.data(),sizeof(gen.fMinSkills))==0&&memcmp(gen.fMaxSkills,bounds.maximum.data(),sizeof(gen.fMaxSkills))==0;}
};
Json CheckAssortmentG3(const std::string& directory){
 ModRuntime::Host host(directory);auto* package=ModRuntime::Current();std::string owner;
 const auto base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","perks.pdb",base,owner);if(!thePerkDatabase.LoadDatabase("",owner.c_str()))throw std::runtime_error("Assortment perk DB unavailable");
 package->ReadResource("database.res","spells.sdb",base,owner);if(!theSpellDatabase.Load("",owner.c_str()))throw std::runtime_error("Assortment spell DB unavailable");
 unsigned control=0;_controlfp_s(&control,0,0);
 Json report={{"success",true},{"luaMainExecuted",true},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},{"gameplayActivated",false},{"crtRandCalled",false},{"isaacCalled",false},
  {"enabled",package->CombatSetting("useLS_SetAssortmentParameters")},{"shopUpdate",package->CombatSetting("JMVShopUpdate")},{"randMax",RAND_MAX},{"fpuRounding",control&_MCW_RC},
  {"profiles",Json::array()},{"spells",Json::array()},{"cases",Json::array()}};
 int checks=0;auto require=[&](bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);};
 SRPGUnitStats profiles[4]={};const float intelligence[]={20.25f,31.5f,80.75f,25.f};
 const BYTE schools[4][3]={{0,0,0},{37,61,93},{120,80,40},{17,1,77}};
 for(int person=0;person<4;++person){auto& st=profiles[person];st.psIntelligence.fpBaseValue=st.psIntelligence.fpEffectiveValue=intelligence[person];
  st.cAutoSkills[RPGASKL_ELEMENTAL]=schools[person][0];st.cAutoSkills[RPGASKL_SENSE]=schools[person][1];st.cAutoSkills[RPGASKL_ASTRAL]=schools[person][2];
  for(int k=0;k<RPG_NUM_PERKS;++k)st.cPerks[k]=BYTE(person);Json modifiers=Json::array(),skills=Json::array();
  for(int k=0;k<RPG_NUM_MAGIC_SUBTYPES;++k){modifiers.push_back(st.GetPerkModifier(RPGPRK_FIRE+k));skills.push_back(st.GetMagicSkill(ERPGMagicSubtypes(k)));}
  report["profiles"].push_back({{"intelligence",intelligence[person]},{"schools",{schools[person][0],schools[person][1],schools[person][2]}},{"perks",modifiers},{"skills",skills}});
 }
 const auto original=std::vector<BYTE>((BYTE*)profiles,(BYTE*)profiles+sizeof(profiles));
 for(int i=0;i<theSpellDatabase.spells.GetNumEntries();++i){auto* p=theSpellDatabase.spells[i];report["spells"].push_back({{"id",i},{"name",std::string((LPCSTR)p->szCodeName)},{"subtype",int(p->mstSubtype)},{"complexity",p->dwComplexity}});}
 const std::vector<std::vector<int>> orders={{},{0},{1},{2},{3},{0,1,2},{2,1,0},{1,0,2},{3,3},{0,0,1,3}};
 const int sequence[]={1,17,16384,32766,32767,0,8191,24575};
 for(int shop:{-1,0,1,2,3,4,5,12})for(int order=0;order<int(orders.size());++order)for(int mode=0;mode<3;++mode){
  std::vector<const SRPGUnitStats*> borrowed;for(int person:orders[order])borrowed.push_back(&profiles[person]);
  const int limit=package->ShopConstructLimit(shop);size_t cursor=0;
  // Two setter calls share cursor: no injected/production reseed between calls.
  for(int call=0;call<2;++call){CAssortmentGenerator items(nullptr),spells(nullptr);Json draws=Json::array();
   auto bounds=ModAssortment::Apply(borrowed,limit,[&](int maximum){require(maximum>0&&maximum<=INT_MAX/RAND_MAX,"Witness outside defined native integer domain");
    int raw=mode==0?0:mode==1?32767:sequence[cursor%8];int result=(raw*maximum/RAND_MAX)%maximum;
    draws.push_back({{"index",cursor},{"bound",maximum},{"raw",raw},{"result",result}});++cursor;return result;},items,spells);
   require(ModAssortmentInspection::Same(items,bounds)&&ModAssortmentInspection::Same(spells,bounds),"Actual generator bounds not copied identically");
   require(draws.size()==borrowed.size()*RPG_NUM_MAGIC_SUBTYPES,"Assortment draw count mismatch");
   Json rounded=Json::array(),allowed=Json::array();if(!borrowed.empty()){
    for(float maximum:bounds.maximum)rounded.push_back(MINT(maximum));
    for(int i=0;i<theSpellDatabase.spells.GetNumEntries();++i){auto* p=theSpellDatabase.spells[i];allowed.push_back(MINT(bounds.maximum[p->mstSubtype])>=int(p->dwComplexity));}
   }
   report["cases"].push_back({{"shop",shop},{"limit",limit},{"order",orders[order]},{"mode",mode},{"call",call},{"draws",draws},{"item",ModAssortmentInspection::Stored(items)},{"spell",ModAssortmentInspection::Stored(spells)},{"rounded",rounded},{"baseComplexityPass",allowed}});
  }
 }
 require(memcmp(profiles,original.data(),sizeof(profiles))==0,"Assortment changed borrowed stats");
 require(!theWorldServer.IsMapLoaded(),"Assortment inspection loaded world");report["assertions"]=checks;return report;
}
