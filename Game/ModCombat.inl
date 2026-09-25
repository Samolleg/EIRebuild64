// Included by RPGSystem.cpp: the existing durability owner is shared with the vanilla path.
#include "ModCombat.h"
#include "ModStatRules.h"
#include "SoundManager.h"
#include "WorldClient.h"
namespace {
struct NativeCombatFeedback:ModCombat::Feedback {int Random(int maximum)override{return random.Get(maximum);}void Sound(CUnit*unit,const char*name)override{auto*manager=theWorld.GetSoundManager();if(manager)manager->BeginFX(name,0,unit->GetCP(),15,20,FALSE);}};
void ModBlockSound(CUnit*unit,CItem*weapon,int type,float block,bool parry,ModCombat::Feedback&fx){
 int variant=fx.Random(2)+1;if(!(block>1||parry))return;CString name,material;
 if(block>1){for(int i=0;i<4;i++){auto*w=unit->GetQWeapon(i);if(w&&w->GetWeaponType()==RPGWST_HANDS1){material=w->GetShapeEntry()->szMaterialType;break;}}
 const char*format="weapons\\Blocks\\block blunt%d.wav";
 if(material=="Bone"||material=="Stone"||material=="Leather")format="weapons\\Blocks\\blow heavy%d.wav";
 else if(weapon){int wt=weapon->GetShapeEntry()->dwTypeID;if(wt==RPGWST_BOW||wt==RPGWST_CROSSBOW)format="weapons\\Blocks\\block arrow%d.wav";else if(type==RPGAT_CRUSHING)format="weapons\\Blocks\\shatter%d.wav";else if(type!=RPGAT_SLASHING)format="weapons\\Blocks\\shield_block%d.wav";}
 name.Format(format,variant);
 }else {static const char*names[]={"weapons\\Blocks\\Parry1.wav","weapons\\Blocks\\Parry2.wav","weapons\\Blocks\\Parry3.wav"};name=names[fx.Random(2)];}
 fx.Sound(unit,name);
}
void ModCriticalSound(CUnit*unit,ERPGHitLocations location,ModCombat::Feedback&fx){
 bool metal=false;
 if(unit->GetTypeID()==MOTID_HUMAN&&int(location)>=0&&int(location)<RPG_NUM_HIT_LOCATIONS){const int layers[]={1,0,0},slots[]={0,0,1};for(int i=0;i<3;i++){auto type=hlArmors[location][layers[i]][slots[i]];if(type==RPGAST_UNKNOWN)continue;auto*armor=unit->GetQArmor(type);if(!armor)continue;DWORD material=armor->GetMaterial();if(material>=DWORD(theItemDatabase.materials.GetNumEntries()))continue;const auto*entry=theItemDatabase.materials.GetEntry(material);if(!entry->szType.IsEmpty()){metal=entry->szType.CompareNoCase("Metal")==0;break;}}}
 CString name="weapons\\pierce\\leather\\1.wav";if(metal)name.Format("weapons\\Blocks\\block blade%d.wav",(fx.Random(2)+1));fx.Sound(unit,name);
}
void ValidateModPortion(const SRPGAttackPortion&ap){if(int(ap.hl)<0||ap.hl>=RPGHL_UNKNOWN||!std::isfinite(ap.fpDamage)||!std::isfinite(ap.fpDamagePercentage)||!std::isfinite(ap.fBackStabMultiplier))throw std::runtime_error("Invalid incoming attack portion");for(float part:ap.fpAttacks)if(!std::isfinite(part))throw std::runtime_error("Invalid attack distribution");}
}
BOOL SRPGUnitStats::ProcessModAttackSingle(CUnit*unit,SRPGCommonStats*common,const SRPGAttackPortion*portion,CItem*weapon,CUnit*attacker,const ModCombat::Defense&defense,ModCombat::Feedback&fx){
 ERPGHitLocations location=portion->hl;if(hls[location].state<RPGALS_ACTIVE&&portion->fpDamage>0)location=RPGHL_BODY;auto&hl=hls[location];if(hl.state<RPGALS_DEAD)return FALSE;
 bool damaged=false;float total=0,backstab=portion->fBackStabMultiplier<=0?1:portion->fBackStabMultiplier;
 for(int type=0;type<RPG_NUM_ATTACK_TYPES;type++){
 float passed=portion->fpAttacks[type]*portion->fpDamage;if(passed<=0)continue;
 int left=ModStats::LeftHand(*unit);auto*shield=left>=0?unit->GetQWeapon(left):nullptr;
 if(shield&&shield->GetWeaponType()==RPGWST_HANDS1&&defense.block>1){float wear=Max(0.f,passed)-Max(0.f,defense.block);DamageItem(shield,unit,wear*2,ACK_HERO_NOTIFY_WEAPON_CRIPPLED,ACK_HERO_NOTIFY_WEAPON_CRITICAL);}
 passed=ModCombat::Passed(passed,type,defense.block,defense.parry,attacker!=nullptr);
 if(defense.parry||defense.block>.01f)ModBlockSound(unit,weapon,type,defense.block,defense.parry,fx);
 // Reference computes noLayers but still processes hero layers; preserve executed behavior.
 for(int layer=IsIDofPersonage(unit->GetID())?RPG_NUM_ARMOR_LAYERS-1:-1;layer>=0;layer--){if(hl.layers[layer].state<RPGALS_DEAD)continue;passed-=hl.layers[layer].fpAbsorbtions[type]*portion->fpDamagePercentage;ApplyDamageToArmors(unit,portion->hl,layer,Max(0.f,passed*2));if(passed<=0)break;}
 if(passed<=0)continue;damaged=true;passed*=backstab;total+=passed;hl.fpHP-=passed;
 if(((passed>=hl.fpMaxHP)&&(hl.dwCrippleMask&(1<<type)))||hl.fpHP<-10*hl.fpMaxHP){if(hl.state>RPGALS_CRIPPLED){DWORD ack=hl.hlt==RPGHLT_HAND?ACK_HERO_NOTIFY_ARM_CRIPPLED:hl.hlt==RPGHLT_LEG?ACK_HERO_NOTIFY_LEG_CRIPPLED:0;auto*spec=unit->GetUnitSpecific();if(ack&&spec&&spec->WhoAmI()==HPC_UNITSERVERSPECIFIC)static_cast<CUnitServer*>(spec)->InformFailure(ack);}hl.state=RPGALS_CRIPPLED;hl.fpHP=-10*hl.fpMaxHP;}
 else if(hl.fpHP<=0&&!ModRuntime::Current()->CombatSetting("DisableCrippledHL"))hl.state=RPGALS_DEAD;
 }
 if(ModCombat::CriticalFeedback(defense.critical,damaged,total))bFlags|=RPGSTAT_CRIT;
 if(backstab!=1)bFlags|=RPGSTAT_BACKSTAB;if(defense.parry)bFlags|=RPGSTAT_PARRY;else if(defense.block>.01f)bFlags|=RPGSTAT_BLOCK;
 common->ssHP.fpCurrent=CollectHP(common->ssHP.fpMax);return damaged;
}
BOOL SRPGUnitStats::ProcessModAttackPortion(CUnit*unit,SRPGCommonStats*common,const SRPGAttackPortion*portion,CItem*weapon,CUnit*attacker,const ModCombat::Defense&input,ModCombat::Feedback*feedback){
 NativeCombatFeedback native;auto&fx=feedback?*feedback:native;
 ValidateModPortion(*portion);ModCombat::Defense defense=input;SRPGAttackPortion ap=*portion;if(ap.fpDamage==0)return FALSE;
 ERPGHitLocations resolved=ap.hl;if(resolved!=RPGHL_ALL&&hls[resolved].state<RPGALS_ACTIVE)resolved=RPGHL_BODY;
 if(resolved==RPGHL_HEAD)bFlags|=RPGSTAT_CRITICAL;if(ap.fpAttacks[RPGAT_ELECTRICAL])bFlags|=RPGSTAT_ELECTRIC;
 if(ap.hl==RPGHL_ALL)ap.fpDamage*=defense.damageMultiplier;
 bool heavy=false,pierce=false;if(weapon){int type=weapon->GetShapeEntry()->dwTypeID;heavy=type==RPGWST_BOW||type==RPGWST_CROSSBOW||type==RPGWST_HANDS4||type==RPGWST_HANDS5||type==RPGWST_HANDS2||type==RPGWST_HANDS6||type==RPGWST_HANDS7||type==RPGWST_HANDS3;pierce=type==RPGWST_SPEAR||type==RPGWST_DAGGER||type==RPGWST_HANDS2;}
 if(ap.fBackStabMultiplier>1&&!heavy&&pierce){ap.fpDamage*=ap.fBackStabMultiplier;bFlags|=RPGSTAT_BACKSTAB;ap.fBackStabMultiplier=1;}if(heavy)ap.fBackStabMultiplier=1;
 if(!std::isfinite(ap.fpDamage))throw std::runtime_error("Incoming damage overflow");if(ap.fpDamage==0)return FALSE;
 float total=0;float absorption=IsIDofPersonage(unit->GetID())?1:theAIGeneralSettings.fAbsorptionLevels[theSettings.nDifficultyLevel];
 for(int type=0;type<RPG_NUM_ATTACK_TYPES;type++){float passed=Max(0.f,ap.fpDamage*ap.fpAttacks[type]-common->psAbsorbtions[type].fpEffectiveValue*ap.fpDamagePercentage*absorption);total+=passed;ap.fpAttacks[type]=passed/ap.fpDamage;
 if(portion->hl!=RPGHL_ALL&&weapon){float wear=Min(1000.f,Max(0.f,portion->fpAttacks[type]*portion->fpDamage));DamageItem(weapon,unit,wear,ACK_HERO_NOTIFY_WEAPON_CRIPPLED,ACK_HERO_NOTIFY_WEAPON_CRITICAL);if(weapon->IsBroken()&&attacker){attacker->RemoveCrippledItems();if(attacker->GetCurrentWeaponSlotIndex()>=QI_MAX_WEAPONS&&attacker->GetQWeapon(0)){attacker->ChangeWeapon(DWORD(-1));attacker->ChangeWeapon(0);}}}
 }
 if(!(total>0)){unit->RemoveCrippledItems();if(unit->GetCurrentWeaponSlotIndex()==DWORD(-1)&&unit->GetQWeapon(0)){unit->ChangeWeapon(DWORD(-1));unit->ChangeWeapon(0);}return FALSE;}
 defense.critical=defense.critical&&total>=1;BOOL damaged=FALSE;
 if(ap.hl==RPGHL_ALL){for(int type=3;type<RPG_NUM_ATTACK_TYPES;type++)if(defense.block>1&&portion->fpAttacks[type]>0)ap.fpDamage-=defense.block*.5f;
 ModBlockSound(unit,weapon,0,defense.block,defense.parry,fx);int left=ModStats::LeftHand(*unit);auto*shield=left>=0?unit->GetQWeapon(left):nullptr;if(shield&&shield->GetWeaponType()==RPGWST_HANDS1&&defense.block>1)DamageItem(shield,unit,defense.block,ACK_HERO_NOTIFY_WEAPON_CRIPPLED,ACK_HERO_NOTIFY_WEAPON_CRITICAL);
 float vitality=0;for(int i=0;i<RPG_NUM_HIT_LOCATIONS;i++)if(hls[i].state>RPGALS_CRIPPLED)vitality+=hls[i].fpVitality;
 if(!(common->ssHP.fpMax*vitality>0))return FALSE;float percent=ap.fpDamage/(common->ssHP.fpMax*vitality);ModCombat::Defense magic=defense;magic.block=0;magic.parry=false;magic.critical=false;
 for(int i=0;i<RPG_NUM_HIT_LOCATIONS;i++){ap.hl=ERPGHitLocations(i);ap.fpDamagePercentage=Max(0.f,portion->fpDamagePercentage*hls[i].fpMaxHP*percent/portion->fpDamage);ap.fpDamage=hls[i].fpMaxHP*percent;damaged=ProcessModAttackSingle(unit,common,&ap,weapon,attacker,magic,fx)||damaged;}
 }else {ap.fpDamage*=defense.damageMultiplier;damaged=ProcessModAttackSingle(unit,common,&ap,weapon,attacker,defense,fx);}
 if(defense.critical&&damaged&&ModRuntime::Current()->CombatSetting("JMVFixCriticalHitSoundTrigger"))ModCriticalSound(unit,resolved,fx);
 unit->RemoveCrippledItems();return damaged;
}
