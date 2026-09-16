from pathlib import Path
import shutil
r=Path(__file__).resolve().parent
def edit(name,old,new):
 p=r/name;b=p.read_bytes();old=old.replace('\n','\r\n').encode();new=new.replace('\n','\r\n').encode()
 if old not in b:old=old.replace(b'\r\n',b'\n');new=new.replace(b'\r\n',b'\n')
 assert b.count(old)==1,(name,old[:70],b.count(old))
 backup=r/'evidence/addon-integration/pre-r21'/name
 if not backup.exists():backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,backup)
 p.write_bytes(b.replace(old,new))
edit('Game/ModPackage.h','struct lua_State;','struct lua_State;\nstruct ModNpcRefreshQueue;')
edit('Game/ModPackage.h','    void ResetRuntimeState();','    void ResetRuntimeState();\n    void TickStatRefresh();\n    void SuccessfulWorldLoad();\n    bool SuppressLegacyRefreshSpell(const char*) const;')
edit('Game/ModPackage.h','    bool npcArmorCalculation=true','    std::unique_ptr<ModNpcRefreshQueue> refreshQueue;\n    bool npcStatRefresh=true;\n    void InitializeLoadedHeroes();\n    bool npcArmorCalculation=true')
edit('Game/ModPackage.cpp','#include "ModStatInitialization.h"','#include "ModStatInitialization.h"\n#include "ModNpcRefresh.h"\n#include "MapObjectServer.h"')
edit('Game/ModPackage.cpp','CModPackage::CModPackage():initialization(std::make_unique<ModInitialization::Ledger>()){}','CModPackage::CModPackage():initialization(std::make_unique<ModInitialization::Ledger>()),refreshQueue(std::make_unique<ModNpcRefreshQueue>()){}')
edit('Game/ModPackage.cpp','npcArmorCalculation=settings.GetInt','npcStatRefresh=settings.GetInt("Addon","JMVNpcStatRefresh",1)!=0;npcArmorCalculation=settings.GetInt')
edit('Game/ModPackage.cpp','void CModPackage::ResetRuntimeState(){initialization->Reset();','void CModPackage::ResetRuntimeState(){refreshQueue->Reset();initialization->Reset();')
edit('Game/ModPackage.cpp',' if(theWorldServer.GetPlayerManager()){auto*p=theWorldServer.GetPlayer(0);if(p)zone=(LPCSTR)p->party.szCurrentZone;}',' zone=(LPCSTR)theWorldServer.szMobFileName;')
edit('Game/ModPackage.cpp','{npcArmorCache.clear();initialization->Reset();}','{npcArmorCache.clear();initialization->Reset();refreshQueue->Reset();}')
edit('Game/ModPackage.cpp','bool CModPackage::CollectArmor(','''namespace {
CUnitServer* AuthoritativeServer(CUnit*unit){
 if(!unit||unit->IsInvalid()||unit->IsUnreferenced()||!theWorldServer.GetMapObjectManager())return nullptr;
 if(theWorldServer.GetUnit(unit->GetID())!=unit||unit->WhoAmI()!=HPC_UNIT)return nullptr;
 return dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
}
}
void CModPackage::InitializeLoadedHeroes(){
 if(!refreshQueue->heroesPending||!theWorldServer.IsMapLoaded())return;
 refreshQueue->heroesPending=false;CMapObjPtrHashIterator iter;
 for(auto*object=iter.GetFirst(&theWorldServer.dynamicObjs);object;object=iter.GetNext()){
  if(object->WhoAmI()!=HPC_UNIT)continue;auto*unit=static_cast<CUnit*>(object);auto*server=AuthoritativeServer(unit);
  if(!server||!unit->GetOwner()||unit->IsDead())continue;
  auto*common=server->GetMapObjectServer()->GetRPGStats();auto*stats=server->GetRPGStats();
  if(!std::isfinite(common->ssHP.fpCurrent)||!std::isfinite(common->ssHP.fpMax)||!std::isfinite(common->ssMP.fpCurrent)||!std::isfinite(common->ssMP.fpMax))continue;
  if(!initialization->Ready(*unit,*common,*stats))server->RecalcRPGStats(false);
 }
}
void CModPackage::TickStatRefresh(){
 SyncRuntimeState();InitializeLoadedHeroes();if(!npcStatRefresh||!theWorldServer.IsMapLoaded())return;
 if(refreshQueue->scanPending){
  refreshQueue->scanPending=false;CMapObjPtrHashIterator iter;
  for(auto*object=iter.GetFirst(&theWorldServer.dynamicObjs);object;object=iter.GetNext()){
   if(object->WhoAmI()!=HPC_UNIT)continue;auto*unit=static_cast<CUnit*>(object);auto*server=AuthoritativeServer(unit);
   if(server&&server->GetUnit()&&!unit->GetOwner()&&!unit->IsDead())refreshQueue->Add(unit->GetID());
  }
 }
 refreshQueue->Process([&](int id){
  auto*unit=theWorldServer.GetMapObjectManager()?theWorldServer.GetUnit(id):nullptr;
  if(auto*server=AuthoritativeServer(unit))server->RecalcRPGStats(false);
 });
}
void CModPackage::SuccessfulWorldLoad(){ResetRuntimeState();SyncRuntimeState();InitializeLoadedHeroes();}
bool CModPackage::SuppressLegacyRefreshSpell(const char*name)const{return npcStatRefresh&&name&&_stricmp(name,"invisibility{d05;d05}")==0;}
bool CModPackage::CollectArmor(''')
edit('Game/WorldServer.cpp','    SetSegmentTimeCounter(GetSegmentTimeCounter() + 1);','    SetSegmentTimeCounter(GetSegmentTimeCounter() + 1);\n    if(auto*mod=ModRuntime::Current())mod->TickStatRefresh();')
edit('Game/WorldServer.cpp','    pItemManager->CheckReferences();\n\n    return TRUE;','    pItemManager->CheckReferences();\n\n    if(auto*mod=ModRuntime::Current())mod->SuccessfulWorldLoad();\n    return TRUE;')
edit('Game/WorldServer.cpp','    bLoadDynamic = true;\n    bLoadStatic = true;\n    return TRUE;','    bLoadDynamic = true;\n    bLoadStatic = true;\n    if(auto*mod=ModRuntime::Current())mod->ResetRuntimeState();\n    return TRUE;')
edit('Game/SpellManager.cpp','#include "SpellManager.h"','#include "SpellManager.h"\n#include "ModPackage.h"\n#include "ModRuntime.h"')
edit('Game/SpellManager.cpp','CSpell *CSpellManager::CreateSpell(LPCSTR pszCodeName)\n{','CSpell *CSpellManager::CreateSpell(LPCSTR pszCodeName)\n{\n    if(auto*mod=ModRuntime::Current())if(mod->SuppressLegacyRefreshSpell(pszCodeName))return nullptr;')
edit('Game/ModRuntime.cpp','#include "ModStatInitialization.h"','#include "ModStatInitialization.h"\n#include "ModNpcRefresh.h"\n#include "SpellManager.h"')
edit('Game/ModRuntime.cpp','    auto run=[&](const char *code)', '''    {
        ModNpcRefreshQueue queue;for(int id=0;id<29;id++)require(queue.Add(id),"Refresh enqueue failed");
        require(!queue.Add(0),"Refresh queue accepts duplicate");std::vector<int> seen;
        auto record=[&](int id){seen.push_back(id);};
        require(queue.Process(record)==12&&queue.Process(record)==12&&queue.Process(record)==5&&queue.Process(record)==0,"Refresh batch size mismatch");
        require(seen.size()==29&&!queue.Add(28),"Completed batch lost world deduplication");
        for(int id=0;id<29;id++)require(seen[id]==id,"Refresh order mismatch");
        queue.scanPending=false;queue.heroesPending=false;queue.Add(90);queue.Reset();
        require(queue.ids.empty()&&queue.scheduled.empty()&&queue.scanPending&&queue.heroesPending&&queue.Add(0),"Refresh reset retained prior world state");
        require(package.SuppressLegacyRefreshSpell("INVISIBILITY{D05;D05}")&&!package.SuppressLegacyRefreshSpell("invisibility")&&!package.SuppressLegacyRefreshSpell(nullptr),"Legacy refresh filter mismatch");
    }
    auto run=[&](const char *code)''')
edit('Game/ModRuntime.cpp','return {{"regenerationCadenceHpMpRollback",true}', 'return {{"refreshQueueBatchDedupReset",true},{"regenerationCadenceHpMpRollback",true}')
print('R21 lifecycle and queue source integration written')
