// LS StartSpell policy; native ownership replaces fixed-address hooks.
bool CEffectSpell::ModLifeSpellStart(int kind){
 auto*mod=ModRuntime::Current();if(!mod||!mod->CombatSetting("JMVParticles"))return false;
 if(kind==ModSpellIdentity::NativeTest){
  CUnit*target=!pTarget.IsEmpty()&&pTarget->WhoAmI()==HPC_UNIT?static_cast<CUnit*>(pTarget):nullptr;
  CUnit*caster=!pWho.IsEmpty()&&pWho->WhoAmI()==HPC_UNIT?static_cast<CUnit*>(pWho):nullptr;
  auto*casterServer=caster?dynamic_cast<CUnitServer*>(caster->GetUnitSpecific()):nullptr;
  bool valid=pWS&&pWS->GetEffectManager()&&spell.GetPrototype()&&target&&target->GetPrototype()&&caster&&caster->GetPrototype()&&casterServer;
  ResetFlag(DW_SPELL_HIDE_CASTER);
  ModSpellOwner::Native54Start(valid,GetPower(),bQuiet,
   [&](float power){casterServer->Heal(power);},
   [&](SRPGAttackPortion&ap){InflictDamage(ap,caster);},
   [&](){CreateParticles(FALSE,PTID_VAMP,CPlot3D(0,0,0),GetShowPower(),1,CPlot3D(0,0,0),target);},
   [&](){return target->GetHP()!=target->GetMaxHP();},
   [&](const char*sound,float radiusScale){CreateSound(FALSE,sound,target->GetCP(),CPlot3D(0,0,0));CreateLight(ptTarget,GetLightColor(),radiusScale*GetLightRadius(),GetLightSpeed());},
   [&](bool action,int timer){if(action)nTimeLeft=timer;castPhase=action?CP_ACTION:CP_FINISH;});
  return true;
 }
 if(kind!=SPELL_ID_VAMP&&kind!=SPELL_ID_HEALING&&kind!=SPELL_ID_STRENGTHEN&&kind!=SPELL_ID_WEAKEN)return false;
 CUnit*target=!pTarget.IsEmpty()&&pTarget->WhoAmI()==HPC_UNIT?static_cast<CUnit*>(pTarget):nullptr;
 if(!target){if(kind==SPELL_ID_VAMP||kind==SPELL_ID_HEALING){castPhase=CP_FINISH;return true;}return false;}
 int targetKind=int(mod->VampSpellValue(target->GetPrototype()->szName,2));
 if(kind==SPELL_ID_STRENGTHEN||kind==SPELL_ID_WEAKEN){if(!targetKind)return false;castPhase=CP_FINISH;return true;}
 auto*targetServer=dynamic_cast<CUnitServer*>(target->GetUnitSpecific());
 int attack=3+spell.GetMagicSubtype();if(!targetServer||((kind==SPELL_ID_VAMP||targetKind==1)&&(attack<0||attack>=int(RPG_NUM_ATTACK_TYPES)))){castPhase=CP_FINISH;return true;}
 SRPGAttackPortion ap;ap.Clear();ap.hl=RPGHL_ALL;ap.fpToHit=FLT_MAX;ap.fBackStabMultiplier=1;ap.bCalledShot=false;if(attack>=0&&attack<int(RPG_NUM_ATTACK_TYPES))ap.fpAttacks[attack]=1;ap.fpDamage=GetPower();
 auto healingSound=[&](){CreateSound(FALSE,"magic\\Healing\\start.wav",target->GetCP(),CPlot3D(0,0,0));CreateLight(ptTarget,GetLightColor(),GetLightRadius(),GetLightSpeed());};
 if(kind==SPELL_ID_HEALING){
  if(targetKind==1){targetServer->ProcessCustomAttackPortion(&ap,target,target);CreateParticles(FALSE,PTID_VAMP,CPlot3D(0,0,0),GetShowPower(),1,CPlot3D(0,0,0),target);if(!bQuiet)healingSound();}
  else{targetServer->Heal(GetPower());CreateParticles(FALSE,PTID_HEALING,CPlot3D(0,0,0),GetShowPower(),1,CPlot3D(0,0,0),target);if(!bQuiet||target->GetHP()!=target->GetMaxHP())healingSound();}
 }else{
  CUnit*caster=!pWho.IsEmpty()&&pWho->WhoAmI()==HPC_UNIT?static_cast<CUnit*>(pWho):nullptr;
  auto*casterServer=caster?dynamic_cast<CUnitServer*>(caster->GetUnitSpecific()):nullptr;
  if(!casterServer){castPhase=CP_FINISH;return true;}
  bool casterUndead=mod->VampSpellValue(caster->GetPrototype()->szName,2)==1;
  // The reference reads columns 3/4 but never applies them. Keep its 0.5 multiplier.
  mod->VampSpellValue(caster->GetPrototype()->szName,4);mod->VampSpellValue(target->GetPrototype()->szName,3);
  float absorb=target->GetUserCommonStats().wAbsorbtions[attack];
  if(absorb<ap.fpDamage){
   if(targetKind!=2){casterServer->Heal((GetPower()-absorb)*.5f);CreateParticles(FALSE,PTID_HEALING,CPlot3D(0,0,0),GetShowPower(),1,CPlot3D(0,0,0),caster);if(!bQuiet||target->GetHP()!=target->GetMaxHP())healingSound();}
   if(!casterUndead){targetServer->ProcessCustomAttackPortion(&ap,caster,caster);CreateParticles(FALSE,PTID_VAMP,CPlot3D(0,0,0),GetShowPower(),1,CPlot3D(0,0,0),target);}
  }
 }
 nTimeLeft=30;castPhase=CP_ACTION;return true;
}
