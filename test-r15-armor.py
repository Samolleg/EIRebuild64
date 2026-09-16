from pathlib import Path
r=Path('work/eirebuild-local')
def edit(f,a,b):
 p=r/f;v=p.read_bytes();nl=b'\r\n' if b'\r\n' in v else b'\n';x=a.encode().replace(b'\n',nl);y=b.encode().replace(b'\n',nl);assert v.count(x)==1,(f,a[:40],v.count(x));p.write_bytes(v.replace(x,y))
edit('Game/ModRuntime.cpp','bool stunRollback=false;','bool stunRollback=false;int armorCases=0;')
a='''            actor.Power(SPELL_ID_STRENGTHEN,20);actor.Power(SPELL_ID_WEAKEN,10);
            live.RecalcStats(&result,&actor);'''
b='''            CItemArmor armorPieces[2];int slots[]={RPGAST_HELM,RPGAST_PLATE};
            struct DetachFullArmor {CUnit&u;CItemArmor*a;int count=0;~DetachFullArmor(){for(int j=0;j<count;j++){u.Remove(a[j].GetArmorType());a[j].DecRefCount();}}} detachFullArmor{actor,armorPieces};
            for(int j=0;j<2;j++){
                int index=-1;for(int k=0;k<theItemDatabase.armors.GetNumEntries();k++){auto*entry=theItemDatabase.armors.GetEntry(k);float total=0;for(float v:entry->main.fpDistributions)total+=v;if(entry->dwTypeID==slots[j]&&total>0){index=k;break;}}
                require(index>=0,"No nonempty armor location fixture");auto&a=armorPieces[j];a.SetTypeID(ITID_ITEM_ARMOR);a.SetSubtypeID(DWORD(index)<<16);a.Setup(theItemDatabase.armors.GetEntry(index));a.AddRef();actor.Wear(&a);detachFullArmor.count++;
                require(actor.GetQArmor(slots[j])==&a,"Full-recalc armor was not equipped");
            }
            SRPGCommonStats corrected={},legacy={};ModStats::Armor(corrected,actor,true);ModStats::Armor(legacy,actor,false);
            float head[RPG_NUM_ATTACK_TYPES],body[RPG_NUM_ATTACK_TYPES],hands[RPG_NUM_ATTACK_TYPES];armorPieces[0].GetAbsorbtions(head,0);armorPieces[1].GetAbsorbtions(body,0);armorPieces[1].GetAbsorbtions(hands,1);
            auto*race=theUnitDatabase.races.GetEntry(prototype->dwRaceIndex);bool distinct=false;
            for(int type=0;type<RPG_NUM_ATTACK_TYPES;type++){float base=race->defences[type]*prototype->fpAbsorbtionMultiplier;float expected=base+head[type]*.1f+body[type]*.3f+hands[type]*.3f;
                require(fabs(corrected.psAbsorbtions[type].fpBaseValue-expected)<.0001 && fabs(legacy.psAbsorbtions[type].fpBaseValue-base)<.0001,"Armor location aggregation mismatch");distinct|=fabs(expected-base)>.0001;}
            require(distinct,"Armor test failed to distinguish corrected and legacy formulas");armorCases++;
            actor.Power(SPELL_ID_STRENGTHEN,20);actor.Power(SPELL_ID_WEAKEN,10);
            live.RecalcStats(&result,&actor);'''
edit('Game/ModRuntime.cpp',a,b)
edit('Game/ModRuntime.cpp','auto oldStats=live;auto oldCommon=result;bool rejected=false;','auto oldStats=live;auto oldCommon=result;float oldArmorRate=armorPieces[0].GetManaRegenRate();bool rejected=false;')
edit('Game/ModRuntime.cpp','memcmp(&result,&oldCommon,sizeof(result))==0,"Full stats failure partially commits"','memcmp(&result,&oldCommon,sizeof(result))==0 && armorPieces[0].GetManaRegenRate()==oldArmorRate,"Full stats failure partially commits"')
edit('Game/ModRuntime.cpp','return {{"fullHeroRecalcCases",fullHeroCases}','return {{"armorLocationCases",armorCases},{"fullHeroRecalcCases",fullHeroCases}')
p=r/'check-full-stats.py';v=p.read_text().replace('assert dispatch["fullHeroRecalcCases"] == 8','assert dispatch["fullHeroRecalcCases"] == 8\nassert dispatch["armorLocationCases"] == 8');p.write_text(v)
edit('Game/ModPackage.cpp','if(!std::isfinite(staged.move.fpEffectiveValue)||!std::isfinite(stagedCommon.ssHP.fpMax)||!std::isfinite(stagedCommon.ssMP.fpMax))','if(!std::isfinite(staged.move.fpEffectiveValue)||!std::isfinite(staged.percepts[RPGS_SIGHT].fpEffectiveValue)||!std::isfinite(staged.percepts[RPGS_HEARING].fpEffectiveValue)||!std::isfinite(stagedCommon.ssHP.fpMax)||!std::isfinite(stagedCommon.ssMP.fpMax))')
