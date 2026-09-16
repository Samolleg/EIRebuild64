exec(open(__file__.replace('integrate-initialization-r20.py','implement-regeneration-r20.py'),encoding='utf-8').read().split("edit('Game/ModPackage.h'")[0])
edit('Game/ModPackage.h','struct lua_State;', 'struct lua_State;\nnamespace ModInitialization {struct Ledger;}')
edit('Game/ModPackage.h','    void SyncRuntimeState();','    void SyncRuntimeState();\n    std::unique_ptr<ModInitialization::Ledger> initialization;')
edit('Game/ModPackage.cpp','#include "ModStatRules.h"','#include "ModStatRules.h"\n#include "ModStatInitialization.h"')
edit('Game/ModPackage.cpp','CModPackage::CModPackage(){}','CModPackage::CModPackage():initialization(std::make_unique<ModInitialization::Ledger>()){}')
edit('Game/ModPackage.cpp',' if(!unit)throw std::runtime_error("Mod regeneration requires a native unit");',' if(!unit||!unit->GetUnitSpecific())throw std::runtime_error("Mod regeneration requires a native unit-specific component");')
edit('Game/ModPackage.cpp',' CallbackFrame frame(state);SRPGCommonStats stagedCommon=common;SRPGUnitStats staged=stats;',' SyncRuntimeState();const unsigned epoch=initialization->epoch;bool ready=initialization->Ready(*unit,common,stats);\n ModInitialization::Inputs inputs(common,stats,*unit);\n CallbackFrame frame(state);SRPGCommonStats stagedCommon=common;SRPGUnitStats staged=stats;')
edit('Game/ModPackage.cpp',' // Retain the native full-recalc caller until initialization proof/observers are ported.\n return changed;', ''' // Publish both native masks when only MP changed; missing/changed proof uses full recalc.
 bool mpOnly=changed&&epoch==initialization->epoch&&ready&&inputs.MpOnly(common,stats,*unit);
 if(mpOnly){unit->SetUpdateMask(CMapObject::VLD_MASK_STATS);unit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);}
 return changed&&!mpOnly;''')
edit('Game/ModPackage.cpp','void CModPackage::ResetRuntimeState(){npcArmorCache.clear();','void CModPackage::ResetRuntimeState(){initialization->Reset();npcArmorCache.clear();')
edit('Game/ModPackage.cpp','if(world!=cacheWorld||zone!=cacheZone||(cacheSegment>=0&&segment<cacheSegment))npcArmorCache.clear();','if(world!=cacheWorld||zone!=cacheZone||(cacheSegment>=0&&segment<cacheSegment)){npcArmorCache.clear();initialization->Reset();}')
edit('Game/ModPackage.cpp',' CallbackFrame frame(state);StunEntry stun(state,unit.GetID());SyncRuntimeState();',' CallbackFrame frame(state);StunEntry stun(state,unit.GetID());SyncRuntimeState();\n initialization->Invalidate(unit.GetID());const unsigned epoch=initialization->epoch;')
edit('Game/ModPackage.cpp','stun.committed=true;return npc&&npcTrickster&&staged.cPerks[11]>0;','stun.committed=true;if(epoch==initialization->epoch)initialization->Finish(unit,common,stats);return npc&&npcTrickster&&staged.cPerks[11]>0;')
edit('Game/ModRuntime.cpp','#include "ModStatRules.h"','#include "ModStatRules.h"\n#include "ModStatInitialization.h"')
edit('Game/ModRuntime.cpp','require(result.Regenerate(live,&actor,true)&&result.nRegenerateCounter==0,"Serialized counter boundary mismatch");','result.Regenerate(live,&actor,true);require(result.ssMP.fpCurrent>0&&result.nRegenerateCounter==0,"Serialized counter boundary mismatch");')
edit('Game/ModRuntime.cpp','                // Failure after both resource and equipment writes must publish nothing.', '''                result=savedCommon;live=savedStats;result.ssMP.fpCurrent=0;result.nRegenerateCounter=14;
                require(!result.Regenerate(live,&actor,false)&&result.ssMP.fpCurrent>0,"MP-only regeneration did not skip full recalc");
                ModInitialization::Ledger proof;proof.Finish(actor,result,live);require(proof.Ready(actor,result,live),"Completed stats proof missing");
                float hp=result.ssHP.fpCurrent;result.ssHP.fpCurrent-=1;require(!proof.Ready(actor,result,live),"Changed HP retained proof");result.ssHP.fpCurrent=hp;
                auto changedStats=live;require(!proof.Ready(actor,result,changedStats),"Different stats identity retained proof");
                result.ssMP.fpCurrent-=1;result.nRegenerateCounter=7;require(proof.Ready(actor,result,live),"MP/counter invalidated completed proof");proof.Reset();require(!proof.Ready(actor,result,live),"Reset retained completed proof");
                ModInitialization::Inputs captured(result,live,actor);result.ssMP.fpCurrent+=1;require(captured.MpOnly(result,live,actor),"Transient MP-only comparison failed");live.move.fpEffectiveValue+=1;require(!captured.MpOnly(result,live,actor),"Changed stats bypassed rebuild");live=savedStats;
                // Failure after both resource and equipment writes must publish nothing.''')
print('R20 initialization ledger integrated at both real native consumers')
