from pathlib import Path
import re
r=Path(__file__).resolve().parent;ref=r.parent/'src_addon_world_background_data_20260916/src/addon/Patches'
def edit(name,fn):
 p=r/name;b=p.read_bytes();dest=r/'evidence/addon-integration/pre-r26'/name;dest.parent.mkdir(parents=True,exist_ok=True)
 if not dest.exists():dest.write_bytes(b)
 p.write_bytes(fn(b.decode('cp1251')).encode('cp1251'))
header=(ref/'RegisteredLoot.h').read_text().replace('namespace EIClasses {','').replace('\n}\n','\n').replace('EIClasses::','')
(r/'Game/ModRegisteredLoot.h').write_text(header,encoding='utf-8')
s=(ref/'RegisteredLoot.cpp').read_text()
s=s.replace('#include "main.h"','#include "StdAfx.h"').replace('#include "RegisteredLoot.h"','#include "ModRegisteredLoot.h"').replace('#include "Config.h"','#include "ModPackage.h"\n#include "ModRuntime.h"').replace('#include "EIClasses/EIClasses.h"','#include "WorldServer.h"\n#include "Player.h"\n#include "StuffMapObjects.h"\n#include "Item.h"')
s=s.replace('using namespace EIClasses;','#define WorldServer (&theWorldServer)')
s=s.replace('g_addonCfg.JMVRegisterLoot','(ModRuntime::Current() && ModRuntime::Current()->RegisteredLootEnabled())')
s=s.replace('WorldServer->GetMobFileName()','&theWorldServer.szMobFileName').replace('mobName->GetString()','(LPCSTR)*mobName').replace('GetClassID()','WhoAmI()').replace('CItemsList items;','CList<CItem*,CItem*> items;')
a='''    CGSVariableSetHashIter iterator(vars->GetVarsHash());
    for (CGSVariableSetHashItem *variable = iterator.First();
         variable;
         variable = iterator.Next())'''
assert a in s;s=s.replace(a,'    for (const auto& variable : *vars)').replace('variable->name.GetString()','variable.first').replace('variable->value','variable.second')
s+='\n#undef WorldServer\n'
(r/'Game/ModRegisteredLoot.inl').write_text(s,encoding='utf-8')
edit('Game/ModPackage.h',lambda s:s.replace('    const std::string& Directory()', '    bool RegisteredLootEnabled() const { return registeredLoot; }\r\n    const std::string& Directory()').replace('    std::string root;', '    bool registeredLoot=true;\r\n    std::string root;'))
edit('Game/ModPackage.cpp',lambda s: s.replace('npcStatRefresh=settings.GetInt','registeredLoot=settings.GetInt("Addon","JMVRegisterLoot",1)!=0;npcStatRefresh=settings.GetInt').replace('void CModPackage::ResetRuntimeState(){','void CModPackage::ResetRuntimeState(){JMV_RegisteredLootReset("package-reset");').replace('npcArmorCache.clear();initialization->Reset();refreshQueue->Reset();','JMV_RegisteredLootReset("world-epoch");npcArmorCache.clear();initialization->Reset();refreshQueue->Reset();').replace(' SyncRuntimeState();InitializeLoadedHeroes();if(!npcStatRefresh',' SyncRuntimeState();if(theWorldServer.IsMapLoaded())JMV_RegisteredLootTick();InitializeLoadedHeroes();if(!npcStatRefresh').replace('void CModPackage::SuccessfulWorldLoad(){ResetRuntimeState();SyncRuntimeState();InitializeLoadedHeroes();}','void CModPackage::SuccessfulWorldLoad(){ResetRuntimeState();SyncRuntimeState();JMV_RegisteredLootOnSuccessfulSaveLoad();InitializeLoadedHeroes();}').replace('#include "ModPackage.h"','#include "ModPackage.h"\r\n#include "ModRegisteredLoot.inl"'))
edit('Game/StuffMapObjects.cpp',lambda s:s.replace('#include "StdAfx.h"','#include "StdAfx.h"\r\n#include "ModRegisteredLoot.h"').replace('        EnableLever(false);\r\n        return true;', '        EnableLever(false);\r\n        JMV_RegisteredLootOnLeverActivated(this,pWho);\r\n        return true;'))
def mob(s):
 s=s.replace('#include "ModRuntime.h"','#include "ModRuntime.h"\r\n#include "ModRegisteredLoot.h"')
 for n in ['JMVRegisterLoot','RegisterLoot']:
  s,count=re.subn(r'("'+n+r'","[^"]*",)false',r'\1true',s);assert count==1
 a=' if(cmd.command==300)';assert a in s;s=s.replace(a,' if(cmd.command==386){result=JMV_RegisterLoot(ObjectArg(cmd),cmd.szParams[0])?1.f:0.f;return true;}\r\n'+a)
 a=' case 382:';assert a in s;s=s.replace(a,' case 388:if(!JMV_RegisterLoot(ObjectArg(cmd),cmd.szParams[0]))throw std::runtime_error("RegisterLoot registration rejected");break;\r\n'+a);return s
edit('Game/ModMobCommands.h',mob)
edit('Game/ModRuntime.cpp',lambda s:s.replace('pending.command=386','pending.command=359'))
