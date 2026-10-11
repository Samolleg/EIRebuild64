// Direct source-derived elemental lifecycle. Keep original IDs for damage/FX.
#include "ModSpellPhase.h"
static bool ExtendedSpellEnabled(int kind){auto*mod=ModRuntime::Current();return ModSpellPhase::Enabled(mod!=nullptr,mod&&mod->CombatSetting("JMVParticles"),kind);}
bool CEffectSpell::ExtendedStart(int nKind){
 if(!ExtendedSpellEnabled(nKind))return false;
 float fpDist;
		CPlot3D ptSpeed, ptTarg, ptSrc;
		ResetFlag(DW_SPELL_HIDE_CASTER);
		if (pWho.IsEmpty())
			ptSrc = cp;
		else{
			CUnit* pUnit = (CUnit*)pWho;
			ptSrc = pUnit->GetUnitHeadPosition();
		}
		switch (nKind)
		{
		case ModSpellIdentity::NativeTestArrow:
		case SPELL_ID_FIREARROWDARK:
		case SPELL_ID_FIREARROWLIGHT: {
			if (pTarget.IsEmpty() || pTarget->WhoAmI() != HPC_UNIT)
			{
				castPhase = CP_FINISH;
				break;
			}
			nTimeLeft = N_MAX_ARROW_FLY_TIME;
			float power = GetShowPower();
			const CPlot3D light = GetLightColor();
			const float lightrad = GetLightRadius();
			if (nKind == SPELL_ID_FIREARROWDARK)
				CreateArrow(ptSrc, pTarget, PTID_FIREARROWDARK, power, "magic\\Firearrow\\circle.wav", light, lightrad);
			else if(nKind == SPELL_ID_FIREARROWLIGHT)
			CreateArrow(ptSrc, pTarget, PTID_FIREARROWLIGHT, power, "magic\\Firearrow\\circle.wav", light, lightrad);
			else
				CreateArrow(ptSrc, pTarget, PTID_FIREARROW, power, "magic\\Firearrow\\circle.wav", light, lightrad);
			CreateSound(FALSE, "magic\\Firearrow\\start.wav", cp, CPlot3D(0, 0, 0));
			castPhase = CP_DELIVERY;
			break; }
		case SPELL_ID_ACIDRAY:
		case SPELL_ID_ACIDRAYDARK:
		case SPELL_ID_ACIDRAYLIGHT: {
			if (pTarget.IsEmpty() || pTarget->WhoAmI() != HPC_UNIT)
			{
				castPhase = CP_FINISH;
				break;
			}
			nTimeLeft = N_MAX_ARROW_FLY_TIME;
			CreateArrow(ptSrc, pTarget, PTID_ACIDRAY, 0.4f * GetShowPower(), "magic\\Acidray\\circle.wav", GetLightColor(), GetLightRadius());
			CreateSound(FALSE, "magic\\Acidray\\start.wav", GetCP(), CPlot3D(0, 0, 0));
			castPhase = CP_DELIVERY;
			break; }
		case ModSpellIdentity::NativeTestWall:
		case SPELL_ID_FIREWALLDARK:
		case SPELL_ID_FIREWALLLIGHT: {
			ptTarg = ptTarget;
			if (nKind == SPELL_ID_FIREWALLDARK)
				CreateParticles(TRUE, PTID_FIREWALLDARK, ptTarg, GetShowPower(), GetShowPower(), CPlot3D(0, 0, 0)); 
			else if (nKind == SPELL_ID_FIREWALLLIGHT)
				CreateParticles(TRUE, PTID_FIREWALLLIGHT, ptTarg, GetShowPower(), GetShowPower(), CPlot3D(0, 0, 0)); 
			else
				CreateParticles(TRUE, PTID_FIREWALL, ptTarg, GetShowPower(), GetShowPower(), CPlot3D(0, 0, 0)); 
			CPlot3D ptDir = ptTarg - ptSrc;
			if (ptDir.x == 0 && ptDir.y == 0)
				ptDir.x = 1;
			ptDir = CPlot3D(-ptDir.y, ptDir.x, 0);
			ptDir *= GetRadius() / fabs(ptDir);
			if (!pEffect.IsEmpty())
				GetEffectWall()->SetWallVector(ptDir);
			SetCP(ptTarg);
			nTimeLeft = GetDuration();
			castPhase = CP_ACTION;
			ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
			CreateLight(ptTarg, GetLightColor(), GetRadius() + GetLightRadius(), CPlot3D(0, 0, 0));
			CreateSoundSeq(ptTarg, CPlot3D(0, 0, 0), "magic\\Firewall\\start.wav", "magic\\Firewall\\circle.wav");
			break;
		}
		case SPELL_ID_FIREBALLDARK:
		case SPELL_ID_FIREBALLLIGHT: {
			float fpSpeed = GetSpeed();
			ptSpeed = ptTarget - GetCP();
			ptSpeed.z = 0;
			fpDist = fabs(ptSpeed);
			if (fpSpeed <= 0)
				fpSpeed = 5;
			nTimeLeft = Float2Int(fpDist / fpSpeed * 15.0f) - 2;
			ptSpeed *= fpSpeed / 15.0f / fabs(ptSpeed);
			CreateParticles(TRUE, ModSpellPhase::StartParticle(nKind), ptSrc, 0.3f * GetShowPower(), 1, ptSpeed); 
			if (!pWho.IsEmpty())
				GetEffectParticle()->nStartID = pWho->GetID();
			ptTarget.z = pWS->GetSurfaceValue(ptTarget.x, ptTarget.y) + 1;
			GetEffectParticle()->SetDestination(ptTarget);
			if (fpDist < 1.25f * fpSpeed)
				CreateSound(FALSE, "magic\\FireBall\\start.wav", GetCP(), ptSpeed);
			else
				CreateSoundSeq(GetCP(), ptSpeed, "magic\\Fireball\\start.wav", "magic\\Fireball\\circle.wav");
			CreateLight(ptSrc, GetLightColor(), GetLightRadius(), ptSpeed);
			castPhase = CP_DELIVERY;
			break;
		}
		case SPELL_ID_LIGHTNINGDARK:
		case SPELL_ID_LIGHTNINGLIGHT: {
			if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
			{
			    castPhase = CP_FINISH;
			    break;
			}
			
			
			
			
			
			
			
			
			int color = 2;
			if(nKind == SPELL_ID_LIGHTNINGLIGHT)
				color = 3;
			CreateLightningToUnit(ptSrc, pTarget, pWho, GetShowPower(), /*nKind == SPELL_ID_CURSEMAGIC, */color);
			CreateSound(FALSE, "magic\\lightning\\start.wav", GetCP(), CPlot3D(0, 0, 0));
			CreateLight(ptSrc, GetLightColor(), GetLightRadius(), CPlot3D(0, 0, 0));
			InflictTargetedDamage();
			nTimeLeft = 7;
			castPhase = CP_ACTION;
			break;
		}
		case SPELL_ID_INVOKE_LGHTNGDARK:
		case SPELL_ID_INVOKE_LGHTNGLIGHT: {
			ptTarget.z = -3 + pWS->GetSurfaceValue(ptTarget.x, ptTarget.y);
			SetCP(ptTarget);
			ptSrc = GetCP();
			ptSrc.z += 30;
			int color = 1;
			if (nKind == SPELL_ID_INVOKE_LGHTNGLIGHT)
				color = 2;
			CreateLightningWithColor(ptSrc, GetCP(), -7 * GetShowPower(), color);
			if (nKind == SPELL_ID_INVOKE_LGHTNGDARK)
				CreateParticles(FALSE, PTID_LIGHTNINGBLASTDARK, ptTarget, GetRadius(), GetShowPower(), CPlot3D(0, 0, 0));
			else
				CreateParticles(FALSE, PTID_LIGHTNINGBLASTLIGHT, ptTarget, GetRadius(), GetShowPower(), CPlot3D(0, 0, 0));
			CreateSound(FALSE, "magic\\InvokeLight\\start.wav", GetCP(), CPlot3D(0, 0, 0), 1000, 1000);
			ptTarget.z += 5;
			CreateLight(ptTarget, GetLightColor(), GetLightRadius(), GetLightSpeed());
			InflictDamageInRadius();
			nTimeLeft = 7;
			castPhase = CP_ACTION;
			break;
		}
		case SPELL_ID_COLUMN_ACIDDARK:
		case SPELL_ID_COLUMN_ACIDLIGHT: {
			SetCP(ptTarget);
			CreateParticles(FALSE, ModSpellPhase::StartParticle(nKind), ptTarget, 1, 1, CPlot3D(0, 0, 0)); 
			CreateSound(FALSE, "magic\\Acidcolumn\\start.wav", ptTarget, CPlot3D(0, 0, 0));
			CreateLight(ptTarget, GetLightColor(), GetLightRadius(), GetLightSpeed());
			nTimeLeft = 30;
			castPhase = CP_ACTION;
			break;
		}
		case SPELL_ID_WALL_LGHTNGDARK:
		case SPELL_ID_WALL_LGHTNGLIGHT: {
			ptTarg = ptTarget;
			CPlot3D ptDir = ptTarg - ptSrc;
			if (ptDir.x == 0 && ptDir.y == 0)
				ptDir.x = 1;
			ptDir = CPlot3D(-ptDir.y, ptDir.x, 0);
			ptDir *= GetRadius() / fabs(ptDir);
			CPlot3D ptLSrc = ptTarget - ptDir;
			CPlot3D ptLDst = ptTarget + ptDir;
			ptLSrc.z = pWS->GetSurfaceValue(ptLSrc.x, ptLSrc.y) + 1;
			ptLDst.z = pWS->GetSurfaceValue(ptLDst.x, ptLDst.y) + 1;
			CreateLightning(ptLSrc, ptLDst, 0);
			SetCP(ptTarg);
			nTimeLeft = GetDuration();
			castPhase = CP_ACTION;
			ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
			CreateLight(ptTarg, GetLightColor(), GetRadius() + GetLightRadius(), CPlot3D(0, 0, 0));
			CreateSoundSeq(ptTarg, CPlot3D(0, 0, 0), "", "magic\\Lightwall\\circle.wav");
		break;
		}
		case SPELL_ID_POISONFOGDARK: 
		case SPELL_ID_POISONFOGLIGHT: {
			ptTarg = ptTarget;
			ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
			CreateParticles(TRUE, ModSpellPhase::StartParticle(nKind), ptTarg, GetRadius(), GetShowPower(), CPlot3D(0, 0, 0)); 
			CreateSoundSeq(ptTarg, CPlot3D(0, 0, 0), "magic\\poison\\start.wav",
				"magic\\poison\\circle.wav");
			CreateLight(ptTarg, GetLightColor(), GetRadius() + GetLightRadius(), CPlot3D(0, 0, 0));
			SetCP(ptTarg);
			nTimeLeft = GetDuration();
			castPhase = CP_ACTION;
			break;
		}


		}
 // Registered native extended Start bypasses original danger admission for all IDs.
 if(ModSpellOwner::AdmitExtendedDangerAfterStart(nKind,castPhase==CP_ACTION))AddRemoveDangerArea(nKind,TRUE);
 return true;
}
bool CEffectSpell::ExtendedDeliver(int nKind){
 if(!ExtendedSpellEnabled(nKind))return false;
CPlot3D ptExplosion;
switch(nKind){
		case ModSpellIdentity::NativeTestArrow:
		case SPELL_ID_FIREARROWDARK:
		case SPELL_ID_FIREARROWLIGHT: {
			if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
			{
			    castPhase = CP_FINISH;
			    break;
			}
			if (pEffect)
			{
				SetCP(pEffect->GetCP());
				if (GetEffectArrow()->IsDelivered())
				{
					InflictTargetedDamage();
					RemoveEffect();
					ptTarget = pTarget->GetCP();
					ptExplosion = ptTarget;
					ptExplosion.z = ((CUnit*)pTarget)->GetUnitHeadHeight();
					ptExplosion.z += pWS->GetZValue(ptTarget.x, ptTarget.y);
					CreateParticles(FALSE, PTID_FIRESTAR, CPlot3D(0, 0, 0), GetShowPower() * 0.7f, 1,
						CPlot3D(0, 0, 0), pTarget);
					CreateSound(FALSE, "magic\\Firearrow\\end.wav", ptExplosion, CPlot3D(0, 0, 0));
					const CPlot3D light = GetLightColor();
					const float lightrad = GetLightRadius();
					const CPlot3D lightspeed = GetLightSpeed();
					CreateLight(ptExplosion, light, lightrad * FP_FINAL_LIGHT_RK, lightspeed * FP_FINAL_LIGHT_RK);
					nTimeLeft = 35;
					castPhase = CP_ACTION;
				}
			}
			if (nTimeLeft-- < 0)
				castPhase = CP_FINISH;
			break; 
		}
		case SPELL_ID_FIREBALLDARK:
		case SPELL_ID_FIREBALLLIGHT: {
			if (nTimeLeft-- < 0)
			{
				SetCP(ptTarget);
				InflictDamageInRadius();
				RemoveEffect();
				ptExplosion = ptTarget;
				CEffectParticle* pP;
				if (nKind == SPELL_ID_FIREBALLDARK)
					pP = CreateParticles(FALSE, PTID_FIREBLASTDARK, ptExplosion,
					1 * Max(GetAreaPower(), GetShowPower()), 1,
					CPlot3D(0, 0, 0)); 
				else if (nKind == SPELL_ID_FIREBALLLIGHT)
					pP = CreateParticles(FALSE, PTID_FIREBLASTLIGHT, ptExplosion,
					1 * Max(GetAreaPower(), GetShowPower()), 1,
					CPlot3D(0, 0, 0)); 
				else
					pP = CreateParticles(FALSE, PTID_FIREBLAST, ptExplosion,
						1 * Max(GetAreaPower(), GetShowPower()), 1,
						CPlot3D(0, 0, 0)); 
				pP->SetPower(GetShowPower(), GetRadius());
				CreateSound(FALSE, "magic\\fireball\\end.wav", ptExplosion, CPlot3D(0, 0, 0));
				CreateLight(ptExplosion, GetLightColor(), GetLightRadius() * FP_FINAL_LIGHT_RK,
					GetLightSpeed() * FP_FINAL_LIGHT_RK);
				if (!pWho.IsEmpty() && pWho->WhoAmI() == HPC_UNIT)
				{
					CUnitServer* pUS = (CUnitServer*)((CUnit*)pWho)->GetUnitSpecific();
					pWS->aiMap.marks.ProduceSoundFootstep(pUS, 5, AI_MAGIC_EFFECT_SOUND, &ptTarget);
				}
				nTimeLeft = 35;
				castPhase = CP_ACTION;
			}
			if (nTimeLeft < 0)
				RemoveMainEffect();
			break;
		}
		case SPELL_ID_ACIDRAYDARK: 
		case SPELL_ID_ACIDRAYLIGHT: {
			if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
			{
			    castPhase = CP_FINISH;
			    break;
			}
			if (pEffect)
			{
				SetCP(pEffect->GetCP());
				if (GetEffectArrow()->IsDelivered())
				{
					InflictTargetedDamage();
					RemoveEffect();
					ptTarget = pTarget->GetCP();
					ptExplosion = ptTarget;
					ptExplosion.z = ((CUnit*)pTarget)->GetUnitHeadHeight();
					ptExplosion.z += pWS->GetZValue(ptTarget.x, ptTarget.y);
					if(nKind == SPELL_ID_ACIDRAYDARK)
					CreateParticles(FALSE, PTID_ACIDSTARDARK, CPlot3D(0, 0, 0), GetShowPower() * 0.7f, 1, CPlot3D(0, 0, 0), pTarget);
					if (nKind == SPELL_ID_ACIDRAYLIGHT)
						CreateParticles(FALSE, PTID_ACIDSTARLIGHT, CPlot3D(0, 0, 0), GetShowPower() * 0.7f, 1, CPlot3D(0, 0, 0), pTarget);
					CreateSound(FALSE, "magic\\Acidray\\end.wav", ptExplosion, CPlot3D(0, 0, 0));
					CreateLight(ptExplosion, GetLightColor(), GetLightRadius() * FP_FINAL_LIGHT_RK,
						GetLightSpeed() * FP_FINAL_LIGHT_RK);
					nTimeLeft = 35;
					castPhase = CP_ACTION;
				}
			}
			if (nTimeLeft-- < 0)
				castPhase = CP_FINISH;
			break;
		}
}
 return true;
}
bool CEffectSpell::ExtendedAction(int nKind){
 if(nKind==ModSpellIdentity::NativeTest){auto*mod=ModRuntime::Current();if(mod&&mod->CombatSetting("JMVParticles")){if(ModSpellOwner::ActionExpired(nTimeLeft))castPhase=CP_FINISH;return true;}}
 if(nKind==SPELL_ID_VAMP&&ModRuntime::Current()){if(nTimeLeft--<0)castPhase=CP_FINISH;return true;}
 if(!ExtendedSpellEnabled(nKind))return false;
 CPlot3D firewallSoundCP;
 ModSpellPhase::Action(nKind,nTimeLeft,!pEffect.IsEmpty(),[&](ModSpellPhase::Event event){
  switch(event){
  case ModSpellPhase::Event::WallDamage:
   if(ModSpellIdentity::Wall(nKind))InflictWallDamage(GetEffectWall()->ptWallDirection);
   else InflictWallDamage(GetEffectLightning()->ptDst-GetEffectLightning()->ptSrc);
   break;
  case ModSpellPhase::Event::RadiusDamage:InflictDamageInRadius();break;
  case ModSpellPhase::Event::HideCaster:SetFlag(DW_SPELL_HIDE_CASTER);break;
  case ModSpellPhase::Event::CaptureFirewallSoundCP:
   firewallSoundCP=GetCP();if(pEffectSound)firewallSoundCP=pEffectSound->GetCP();break;
  case ModSpellPhase::Event::DangerRemove:AddRemoveDangerArea(nKind,FALSE);break;
  case ModSpellPhase::Event::SoundRemove:RemoveSoundEffect();break;
  case ModSpellPhase::Event::MainRemove:RemoveMainEffect();break;
  case ModSpellPhase::Event::FirewallEndSound:CreateSound(FALSE,"magic\\Firewall\\end.wav",firewallSoundCP,CPlot3D(0,0,0));break;
  case ModSpellPhase::Event::PoisonEndSound:CreateSound(FALSE,"magic\\poison\\end.wav",ptTarget,CPlot3D(0,0,0));break;
  case ModSpellPhase::Event::LightFade:pEffectLight->SetSpeed(GetLightSpeed());break;
  case ModSpellPhase::Event::InvokeStrike:{
   CPlot3D ptSrc=GetCP();ptSrc.z+=30;int color=nKind==SPELL_ID_INVOKE_LGHTNGLIGHT?3:2;
   CreateLightningWithColor(ptSrc,GetCP(),-7,color);break;
  }
  case ModSpellPhase::Event::Finish:castPhase=CP_FINISH;break;
  }
 });
 return true;
}
void CEffectSpell::CreateLightningWithColor(const CPlot3D&src,const CPlot3D&dst,float scale,int color){
 auto*lightning=static_cast<CEffectLightning*>(pWS->GetEffectManager()->CreateData(ETypeIDs(HPC_EFFECTLIGHTNING)));
 lightning->SetParams(src,dst,scale);lightning->SetColor(BYTE(color));SetNewEffect(lightning);
}

// Headless contract: real private phase owners, without world/renderer substitutes.
bool JMV_CheckExtendedSpellLifecycle(){
 if(!ExtendedSpellEnabled(SPELL_ID_FIREBALLDARK))return false;
 for(int id=57;id<=74;++id){
  
  CEffectSpell effect;effect.pWS=&theWorldServer;effect.spell.SetSubtypeID(DWORD(id)<<16);effect.castPhase=CP_ACTION;effect.nTimeLeft=-2;
  for(int tick=0;tick<100&&effect.castPhase!=CP_FINISH;++tick)effect.Segment();
  if(effect.castPhase!=CP_FINISH)return false;
 }
 
 for(int id:{SPELL_ID_FIREARROWDARK,SPELL_ID_FIREARROWLIGHT,SPELL_ID_ACIDRAYDARK,SPELL_ID_ACIDRAYLIGHT}){
  CEffectSpell effect;effect.pWS=&theWorldServer;effect.spell.SetSubtypeID(DWORD(id)<<16);effect.castPhase=CP_START;
  effect.Segment();if(effect.castPhase!=CP_FINISH)return false;
  effect.castPhase=CP_DELIVERY;effect.nTimeLeft=-1;effect.Segment();if(effect.castPhase!=CP_FINISH)return false;
 }
 
 for(int id:{SPELL_ID_FIREBALLDARK,SPELL_ID_FIREBALLLIGHT}){
  CEffectSpell effect;effect.pWS=&theWorldServer;effect.spell.SetSubtypeID(DWORD(id)<<16);effect.castPhase=CP_ACTION;effect.nTimeLeft=35;
  for(int tick=0;tick<36;++tick){effect.Segment();if(effect.castPhase!=CP_ACTION)return false;}
  effect.Segment();if(effect.castPhase!=CP_FINISH)return false;
 }
 return true;
}
