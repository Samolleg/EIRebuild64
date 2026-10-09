// Isolated component entry: actual installed Lua/perks, borrowed stats; no Unit/world/UI.
Json CheckFullDescriptionsG3(const std::string& directory){
 ModRuntime::Host host(directory);auto* package=ModRuntime::Current();std::string owner;
 const auto base=(fs::path(directory).parent_path().parent_path()/"Base/res/database.res").string();
 package->ReadResource("database.res","perks.pdb",base,owner);
 if(!thePerkDatabase.LoadDatabase("",owner.c_str()))throw std::runtime_error("Full display perks unavailable");
 Json report={{"success",true},{"luaMainExecuted",true},{"worldCreated",false},{"unitCreated",false},{"screenCreated",false},{"deviceCreated",false},{"audioStarted",false},{"gameplayActivated",false},{"cases",Json::array()},{"requirements",Json::array()},{"layout",Json::array()},{"absorption",Json::array()},{"perks",Json::array()},{"backstabBase",theAIGeneralSettings.fpBackstabAdd},{"jmv136",package->CombatSetting("JMV136")},{"jmvPerks",package->CombatSetting("JMVPerks")},{"jmvAttacks",package->CombatSetting("JMVAttacks")}};
 size_t checks=0;auto require=[&](bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);};
 for(int i=0;i<thePerkDatabase.GetNumPerks();++i){const auto* p=thePerkDatabase.GetPerk(i);report["perks"].push_back({{"index",i},{"code",std::string((LPCSTR)p->szCode)},{"modifier",p->dwModifier}});}
 auto rows=[](const std::vector<ModCampDescriptions::Row>& src){Json a=Json::array();for(const auto& r:src)a.push_back({{"label",r.label},{"value",r.value},{"second",r.second},{"format",r.format},{"damageType",r.damageType},{"heading",r.heading}});return a;};
 using namespace ModCampDescriptions;
 for(int profile=0;profile<4;++profile){
  SRPGUnitStats st={};SRPGUserUnitStats shown={};st.fpExperience=profile==0?1500.0f:200000.0f;
  st.psStrength.fpBaseValue=st.psStrength.fpEffectiveValue=profile==0?25:80;
  st.psDexterity.fpBaseValue=st.psDexterity.fpEffectiveValue=profile==0?80:30;
  st.psIntelligence.fpBaseValue=st.psIntelligence.fpEffectiveValue=profile==0?25:60;
  shown.cStrength=25;shown.cDexterity=BYTE(25+profile*15);shown.cIntelligence=25;
  st.cAutoSkills[RPGASKL_MELEE]=BYTE(20+profile*20);st.cAutoSkills[RPGASKL_ARCHERY]=BYTE(120-profile*20);
  for(int k=0;k<RPG_NUM_PERKS;++k){st.cPerks[k]=BYTE(profile);shown.cPerks[k]=BYTE(profile);}
  for(int h=0;h<RPG_NUM_HIT_LOCATIONS;++h){st.hls[h].hlt=h>=RPGHL_LLEG?RPGHLT_LEG:RPGHLT_BODY;st.hls[h].fpHP=profile==3?50:100;st.hls[h].fpMaxHP=100;st.hls[h].state=RPGALS_ACTIVE;}
  const auto original=st;const auto originalShown=shown;
  Json mods=Json::array(),shownMods=Json::array();for(int k=0;k<RPG_NUM_PERKS;++k){mods.push_back(st.GetPerkModifier(k));shownMods.push_back(shown.GetPerkModifier(k));}
  for(int type:{RPGWST_SWORD,RPGWST_DAGGER,RPGWST_HANDS2,RPGWST_HANDS3,RPGWST_HANDS6,RPGWST_HANDS7})for(int hand=0;hand<3;++hand)for(int weight:{0,194,195,240,300,375,450})for(int difficulty:{0,2}){
   Input in;in.stats=&st;in.shown=&shown;in.carried=weight;in.capacity=300;in.difficulty=difficulty;
   in.armorAttack=-12;in.armorDefense=-9;in.complexDamage=7;in.complexBackstab=9;in.jewelryRestore=.03f;in.armorRestore=.02f;in.raceMP=.01f;
   in.block=45;in.blockPower=24;in.armorHearing=.12f;in.armorSight=.09f;in.armorCritChance=.03f;in.armorCritMaterial=2;in.jewelryCritChance=.04f;in.jewelryCritPower=.07f;
   in.right.present=true;in.right.type=type;in.right.weightClass=1+profile%3;in.right.attack=5;in.right.defense=7;in.right.shapeSkill=2;
   in.right.minimum=12;in.right.maximum=22;in.right.distribution[0]=.6f;in.right.distribution[4]=.4f;in.right.parry=30;in.right.criticalChance=.11f;in.right.criticalPower=.2f;in.overloadParryRight=40;
   if(hand){in.left=in.right;in.left.type=hand==1?RPGWST_HANDS1:RPGWST_AXE;in.left.weightClass=3;in.left.attack=2;in.left.defense=4;in.left.minimum=hand==1?0:9;in.left.maximum=hand==1?0:18;in.left.parry=hand==1?0:25;in.overloadParryLeft=hand==1?0:35;}
   const auto out=package->EvaluateCampDescriptions(in);Json damages=Json::array();
   for(const auto& d:out.damages)damages.push_back({{"type",d.type},{"left",d.left},{"rawMinimum",d.rawMinimum},{"rawMaximum",d.rawMaximum},{"minimum",d.minimum},{"maximum",d.maximum},{"skill",d.skill},{"attribute",d.attribute},{"perk",d.perk},{"overload",d.overload},{"complex",d.complex},{"sum",d.sum},{"shownSum",d.shownSum}});
   report["cases"].push_back({{"profile",profile},{"type",type},{"hand",hand},{"weight",weight},{"capacity",300},{"difficulty",difficulty},{"experience",st.fpExperience},{"partyStrength",st.psStrength.fpEffectiveValue},{"partyDexterity",st.psDexterity.fpEffectiveValue},{"partyIntelligence",st.psIntelligence.fpEffectiveValue},{"shownDexterity",shown.cDexterity},{"melee",st.cAutoSkills[RPGASKL_MELEE]},{"archery",st.cAutoSkills[RPGASKL_ARCHERY]},{"modifiers",mods},{"shownModifiers",shownMods},{"attack",out.attack},{"defense",out.defense},{"backstab",out.backstab},{"mpRate",out.mpRate},{"mpBase",out.mpBase},{"mpPerSecond",out.mpPerSecond},{"mpSeconds",out.mpSeconds},{"attackRows",rows(out.attackRows)},{"defenseRows",rows(out.defenseRows)},{"overloadRows",rows(out.overloadRows)},{"mpRows",rows(out.mpRows)},{"damageRows",rows(out.damageRows)},{"damages",damages}});
  }
  for(int i=0;i<thePerkDatabase.GetNumPerks();++i){const auto* p=thePerkDatabase.GetPerk(i);report["requirements"].push_back({{"profile",profile},{"index",i},{"code",std::string((LPCSTR)p->szCode)},{"missing",package->CampMissingRequirement(st,p->szCode)}});}
  require(memcmp(&original,&st,sizeof(st))==0,"Full description mutated party stats");require(memcmp(&originalShown,&shown,sizeof(shown))==0,"Full description mutated displayed stats");
 }
 for(bool weapon:{false,true})for(int damageCount:{0,1,2,4,12})for(int row=0;row<28;++row)report["layout"].push_back({{"weapon",weapon},{"damageCount",damageCount},{"row",row},{"type",WeaponHover(row,weapon,damageCount)}});
 for(int t=0;t<RPG_NUM_ATTACK_TYPES;++t)for(float raw:{0.0f,10.0f,23.4f})report["absorption"].push_back({{"type",t},{"raw",raw},{"armory",20},{"flow",35},{"value",Absorption(raw,t,20,35)}});
 require(!theWorldServer.IsMapLoaded(),"Full display component loaded server world");report["assertions"]=checks;return report;
}
