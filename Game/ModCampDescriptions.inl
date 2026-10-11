ModCampDescriptions::Result CModPackage::EvaluateCampDescriptions(const ModCampDescriptions::Input& in){
 using namespace ModCampDescriptions;
 if(!in.stats||!in.shown)throw std::runtime_error("Camp display requires selected stats");
 CallbackFrame frame(state);SRPGUnitStats stats=*in.stats;const auto& shown=*in.shown;Result out;
 auto scalar=[&](const char* name,std::initializer_list<float> args){BeginCallback(name);for(float value:args)lua_pushnumber(state,value);Call(int(args.size()),1);float result=float(Number(state,name));lua_pop(state,1);return result;};
 auto perk=[&](int group){return stats.GetPerkModifier(group);};
 auto level=[&](int group){return int(stats.cPerks[group]);};
 auto overload=[&](const char* name){return scalar(name,{float(in.carried),float(in.capacity)});};
 auto add=[](std::vector<Row>& rows,const char* label,float value,const char* format="%.0f",float second=0){if(value<=0&&strcmp(format,"%+.0f%%")==0)format="%.0f%%";rows.push_back({label,value,second,format,-1,false});};
 auto heading=[](std::vector<Row>& rows,const char* label){rows.push_back({label,0,0,"",-1,true});};
 auto attr=[&](int type,float modifier){return (scalar("GetDamageModAttr",{float(type),float(int(stats.psStrength.fpEffectiveValue)),float(int(stats.psDexterity.fpEffectiveValue)),float(int(stats.psIntelligence.fpEffectiveValue)),modifier})-1)*100;};
 BeginCallback("GetHitLocDecr");ModLua::Push(state,&stats,frame.scope);lua_pushnil(state);lua_pushinteger(state,1);Call(3,1);const float wound=float(Number(state,"Display hit location"));lua_pop(state,1);
 const float attackOverload=overload("GetOverloadAttackOnServer"),defenseOverload=overload("GetOverloadDefenceOnServer"),damageOverload=overload("GetOverloadDamageOnServer");
 const float blockOverload=overload("GetOverloadBlockOnServer"),parryOverload=overload("GetOverloadParryOnServer");
 const bool dual=in.left.present,shield=dual&&in.left.type==RPGWST_HANDS1,strong=level(29)!=0,master=level(26)!=0,duelist=level(28)!=0;
 const bool rightTwo=TwoHanded(in.right.type),leftTwo=TwoHanded(in.left.type);
 const int weightPerk=in.right.weightClass==1?16:in.right.weightClass==2?24:in.right.weightClass==3?6:-1;
 const int weightBonus=weightPerk<0?0:int(perk(weightPerk));
 const int skill=int(stats.cAutoSkills[Ranged(in.right.type)?RPGASKL_ARCHERY:RPGASKL_MELEE]);
 const int dex=int(shown.cDexterity)-25;
 const int armorAttack=Armor(in.armorAttack,int(scalar("GetArmoryAttackDebuff",{float(level(5))}))),armorDefense=Armor(in.armorDefense,int(scalar("GetArmoryDefenceDebuff",{float(level(5))})));
 int weaponAttack=in.right.attack,weaponDefense=in.right.defense,strongBonus=0,masterBonus=0;
 if(dual){
  if(shield){weaponAttack+=in.left.attack;weaponDefense+=in.left.defense;if(strong&&rightTwo)strongBonus=int(scalar("GetStrongMod",{}));}
  else{weaponAttack=int(float(in.right.attack+in.left.attack)*.5f);weaponDefense=int(float(in.right.defense+in.left.defense)*.5f);if(strong&&(rightTwo||leftTwo))strongBonus=int(scalar("GetStrongMod",{}));masterBonus=int(scalar(master?"GetMasterMod":"GetMasterMod2",{}));}
 }
 out.attack=dex+skill+int(attackOverload)+weightBonus;out.defense=dex+skill+int(defenseOverload)+weightBonus;
 out.attack+=in.right.shapeSkill;out.defense+=in.right.shapeSkill;
 out.attack+=weaponAttack+strongBonus+masterBonus+armorAttack;out.defense+=weaponDefense+(!dual&&duelist&&!rightTwo?3:0)+armorDefense;
 auto attackDetails=[&](bool defense,std::vector<Row>& rows){
  add(rows,"$WSLDEXT",dex);add(rows,Ranged(in.right.type)?"infoitem_56":"infoitem_55",skill);
  add(rows,"infoitem_54",defense?int(defenseOverload):int(attackOverload));add(rows,"infoitem_57",defense?armorDefense:armorAttack);
  const char* label=duelist&&!rightTwo?"infoitem_61":in.right.weightClass==1?"infoskill_38":in.right.weightClass==2?"infoskill_37":in.right.weightClass==3?"infoskill_36":"$WSLPERK";
  add(rows,label,weightBonus+(defense&&weightPerk>=0&&duelist&&!rightTwo?3:0));
  if(dual&&shield){add(rows,"$WSLWEAPON",defense?in.right.defense:in.right.attack);add(rows,"infoskill_39",defense?in.left.defense:in.left.attack);}
  else add(rows,dual?"infoskill_40":"$WSLWEAPON",defense?weaponDefense:weaponAttack);
  if(!defense){if(dual&&!shield)add(rows,master?"infoskill_41":"infoskill_41_2",masterBonus);if(strongBonus)add(rows,"infoskill_42",strongBonus);}
  int sum=0;for(const auto& row:rows)sum+=int(row.value);add(rows,"$WSLSUM",sum);
 };
 if(in.right.present){attackDetails(false,out.attackRows);attackDetails(true,out.defenseRows);}
 const float shownBlock=in.block*blockOverload;
 auto parry=[&](){float a=in.right.parry*parryOverload*.01f,b=in.left.parry*parryOverload*.01f;if(!dual)return a*100;if(a<.0001f){if(b<.0001f)b=a*100;a=b*100;}else if(b<.0001f)b=a*100;else a=(a+b-2*a*b)*100;return a;};
 if(in.right.type==RPGWST_HANDS1||shield){add(out.defenseRows,"infoskill_26",in.blockPower*blockOverload);add(out.defenseRows,"infoskill_31",shownBlock,"%.0f%%");}
 add(out.defenseRows,"infoskill_32",in.right.present?parry():0,"%.0f%%");
 const float stealth=level(15)==1?.05f:level(15)==2?.1f:level(15)==3?.2f:0;
 add(out.defenseRows,"infoitem_51",(1+in.armorHearing-stealth)*100,"%.0f%%");add(out.defenseRows,"infoitem_53",(1+in.armorSight-stealth)*100,"%.0f%%");
 auto critical=[&](const Weapon& weapon,int hand){heading(out.attackRows,hand==1?"infoitem_58":"infoitem_58#2");add(out.attackRows,"infoitem_48",(weapon.criticalChance+in.armorCritChance*in.armorCritMaterial+in.jewelryCritChance+shown.GetPerkModifier(12)*.01f)*100,"%.0f%%");add(out.attackRows,"infoitem_49",(weapon.criticalPower+in.jewelryCritPower+shown.GetPerkModifier(4)*.01f)*100,"%.0f%%");};
 if(in.right.present)critical(in.right,1);if(dual&&!shield)critical(in.left,2);
 add(out.overloadRows,"infoskill_1",float(in.carried),"%.0f/%.0f",float(in.capacity));add(out.overloadRows,"infoitem_39",int(defenseOverload));add(out.overloadRows,"infoitem_38",int(attackOverload));
 add(out.overloadRows,"infoitem_15",(damageOverload-1)*100,"%+.0f%%");add(out.overloadRows,"infoitem_45",shownBlock-in.block,"%+.0f%%");
 add(out.overloadRows,"infoitem_47",-OverloadParry(in.overloadParryRight,in.overloadParryLeft,parryOverload,dual),shownBlock-in.block>0?"%+.1f%%":"%.1f%%");
 out.mpRate=in.raceMP*(1+in.jewelryRestore+in.armorRestore+perk(18)*.01f)*MpOverload(in.carried,in.capacity);
 const float mp=30*powf(1.10f,static_cast<float>(log(stats.fpExperience/500.0f)/log(1.61f)))-41.9f;
 out.mpBase=(float(shown.cDexterity)/25)*(1+perk(23)*.01f)*mp*1.2f+47;out.mpPerSecond=out.mpBase*out.mpRate;out.mpSeconds=out.mpBase/out.mpPerSecond;
 heading(out.mpRows,"infoskill_52");add(out.mpRows,"infoskill_45",out.mpRate*100,"%.4f/100");add(out.mpRows,"infoskill_46",out.mpPerSecond,"%.2f");out.mpRows.back().label+="|infoskill_48";add(out.mpRows,"infoskill_51",out.mpSeconds,"%.1f");out.mpRows.back().label+="|infoskill_49";
 out.backstab=theAIGeneralSettings.fpBackstabAdd+perk(CombatSetting("JMVPerks")?15:RPGPRK_BACKSTAB)*.01f+(CombatSetting("JMVPerks")?0:in.complexBackstab*.01f);
 float rightSum=0;
 auto damage=[&](const Weapon& weapon,bool left){
  if(!weapon.present||int(weapon.maximum)==0)return;
  const float skillMod=(scalar("GetSkillMod",{float(stats.cAutoSkills[Ranged(weapon.type)?RPGASKL_ARCHERY:RPGASKL_MELEE])})-1)*100;
  const float attrMod=attr(weapon.type,.5f),perkMod=attr(weapon.type,.25f*level(30)),enc=(damageOverload-1)*100;
  float wperk=0;if(CombatSetting("JMV136")){if(left)wperk=float(level(6));else if(weapon.weightClass!=3)wperk=perk(weapon.weightClass==1?16:24)*(weapon.weightClass==1?3:1);}
  const float sum=skillMod+attrMod+perkMod+enc+wperk+in.complexDamage;if(!left)rightSum=sum;
  for(int t=0;t<RPG_NUM_ATTACK_TYPES;++t)if(weapon.distribution[t]!=0){float factor=(100+sum+wound)*.01f*(1+in.difficulty*.25f);out.damages.push_back({t,left,weapon.minimum,weapon.maximum,weapon.minimum*factor,weapon.maximum*factor,skillMod,attrMod,perkMod+wperk,enc,in.complexDamage,sum,left?skillMod+attrMod+attr(in.right.type,.25f*level(30))+enc:sum});}
 };
 damage(in.right,false);damage(in.left,true);
 auto detailedDamage=[&](bool left){
  const Weapon& weapon=left?in.left:in.right;if(!weapon.present||(left&&shield)||int(weapon.maximum)==0)return;
  heading(out.damageRows,left?"infoitem_58#2":"infoitem_58");
  for(const auto& d:out.damages)if(d.left==left)out.damageRows.push_back({"damage",d.rawMinimum,d.rawMaximum,"%.0f-%.0f",d.type,false});
  heading(out.damageRows,"infoitem_59");const auto it=std::find_if(out.damages.begin(),out.damages.end(),[&](const Damage& d){return d.left==left;});if(it==out.damages.end())return;const auto& d=*it;
  add(out.damageRows,"infoitem_62",d.skill,"%+.0f%%");add(out.damageRows,"infoitem_60",d.attribute,"%+.0f%%");add(out.damageRows,left?"PERK dexter0":"infoitem_61",left?attr(in.right.type,.25f*level(30)):d.perk,"%+.0f%%");add(out.damageRows,left?"infoitem_63":"infoitem_57",d.overload+(left?0:d.complex),"%+.0f%%");
  add(out.damageRows,"infoitem_64",d.shownSum+in.difficulty*25,"%+.0f%%");if(in.difficulty>0){out.damageRows.back().label+="|difficulty";out.damageRows.back().second=in.difficulty==1?2:1;}
  for(const auto& range:out.damages)if(range.left==left){float factor=(100+rightSum+wound)*.01f*(1+in.difficulty*.25f);out.damageRows.push_back({"damage",weapon.minimum*factor,weapon.maximum*factor,"%.0f-%.0f",range.type,false});}
 };
 detailedDamage(false);detailedDamage(true);return out;
}

ModCampDescriptions::Result CModPackage::CampDescriptions(CPersonage& pers,CUnit& unit,bool skillCapacity){
 using namespace ModCampDescriptions;Input in;in.stats=&pers.stats;in.shown=unit.GetUserUnitStats();in.carried=int(pers.GetItemsWeight());in.difficulty=theSettings.nDifficultyLevel;
 if(skillCapacity)in.capacity=CampSkillCapacity(pers,&unit);else{float seconds=0;CampInfo(pers,&unit,in.capacity,seconds);}
 in.complexDamage=InfoComplex(unit,11);if(!CombatSetting("JMVPerks"))in.complexBackstab=InfoComplex(unit,20);
 const auto* race=unit.GetRaceModel();in.raceMP=race?race->fpRegenerateMP:0;
 auto readWeapon=[&](int slot,Weapon& result){if(slot<0||slot>=pers.weapons.GetSize())return;auto* w=pers.weapons[slot];if(!w)return;result.present=true;result.type=w->GetWeaponType();const char* name=w->GetShapeEntry()->szName;result.weightClass=int(InfoProperty("GetWeaponPropsInside",name,7));result.attack=int(w->GetAtackBonus());result.defense=int(w->GetDefenceBonus());result.minimum=w->GetDamageMin();result.maximum=w->GetDamageMax();w->GetDamageDistribution(result.distribution.data());result.criticalChance=InfoProperty("GetWeaponPropsInside",name,4)*.01f;result.criticalPower=InfoProperty("GetWeaponPropsInside",name,5)*.01f;
  // Native LS_GetWSLWeaponModifier7781, registered44813 under existing JMVAttacks.
  if(CombatSetting("JMVAttacks"))result.shapeSkill=float(w->GetSkillModifier(Ranged(result.type)?RPGASKL_ARCHERY:RPGASKL_MELEE));
  else if(result.type<=RPGWST_CROSSBOW||result.type==RPGWST_HANDS)result.shapeSkill=pers.stats.GetWSLWeaponModifier(w->GetSubtypeID());
 };
 int current=int(pers.dwCurrentWeapon),left=ModStats::LeftHand(unit,[&](int slot)->CItemWeapon*{return slot>=0&&slot<pers.weapons.GetSize()?pers.weapons[slot]:nullptr;});readWeapon(current,in.right);readWeapon(left,in.left);
 for(int i=0;i<pers.armors.GetSize();++i)if(auto* a=pers.armors[i]){const char* name=a->GetShapeEntry()->szName;in.armorDefense+=int(InfoProperty("GetArmorPropsInside",name,2));in.armorAttack+=InfoProperty("GetArmorPropsInside",name,3);in.armorRestore+=InfoProperty("GetMaterialPropsInside",a->GetMaterial()<unsigned(theItemDatabase.materials.GetNumEntries())?(LPCSTR)theItemDatabase.materials.GetEntry(a->GetMaterial())->szName:"",3)*.01f;}
 for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)if(auto* a=unit.GetQArmor(i)){const char* name=a->GetShapeEntry()->szName;in.armorHearing+=InfoProperty("GetArmorPropsInside",name,6);in.armorSight+=InfoProperty("GetArmorPropsInside",name,5);in.armorCritChance+=InfoProperty("GetArmorPropsInside",name,8)*.01f;in.armorCritMaterial+=InfoProperty("GetMaterialPropsInside",name,6);}
 for(int i=0;i<pers.items.GetSize();++i)if(auto* q=pers.items[i]){const char* name=q->GetShapeEntry()->szName;in.jewelryRestore+=InfoProperty("GetJewelryPropsInside",name,15)*.01f;in.jewelryCritChance+=InfoProperty("GetJewelryPropsInside",name,7)*.01f;in.jewelryCritPower+=InfoProperty("GetJewelryPropsInside",name,8)*.01f;}
 CallbackFrame frame(state);auto value=[&](const char* name,CUnit& actor,int slot){BeginCallback(name);ModLua::Push(state,actor.GetUserUnitStats(),frame.scope);ModLua::Push(state,&actor,frame.scope);if(slot>=0)lua_pushinteger(state,slot);Call(slot>=0?3:2,1);float v=float(Number(state,name));lua_pop(state,1);return v;};
 in.block=value("GetShieldBlockChance",unit,-1);in.blockPower=value("GetShieldBlock",unit,-1);
 CUnit* server=skillCapacity&&theWorldServer.GetMapObjectManager()?theWorldServer.GetUnit(unit.GetID()):nullptr;CUnit& overloadActor=server?*server:unit;
 if(in.right.present){in.right.parry=value("GetWeaponParryChance",unit,current);in.overloadParryRight=server?value("GetWeaponParryChance",overloadActor,current):in.right.parry;}
 if(in.left.present){in.left.parry=value("GetWeaponParryChance",unit,left);in.overloadParryLeft=server?value("GetWeaponParryChance",overloadActor,left):in.left.parry;}
 return EvaluateCampDescriptions(in);
}

std::string CModPackage::CampMissingRequirement(const SRPGUnitStats& stats,const char* code){
 CallbackFrame frame(state);std::string first;bool missing=false;
 for(int field=3;field<=7;++field){BeginCallback("GetPerkRequiredJmv");lua_pushstring(state,code);lua_pushinteger(state,field);Call(2,1);const char* text=lua_tostring(state,-1);std::string required=text?text:"";lua_pop(state,1);if(field==3)first=required;
  // Lua-returned strings do not have the address of C++ sentinel literals.
  // Native therefore reaches the failed lookup and breaks on an empty/sentinel code.
  int index=thePerkDatabase.GetPerkIndex(required.c_str());if(index<0)break;if(stats.cPerks[index/3]<index%3+1)missing=true;
 }
 return missing?first:std::string();
}
