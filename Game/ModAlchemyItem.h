#pragma once
#include "ModAlchemy.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "Item.h"
namespace ModAlchemy {
inline bool PipelineEnabled(){
 auto* mod=ModRuntime::Current();
 return mod&&Enabled(mod->CombatSetting("JMVPerks")!=0,mod->CombatSetting("JMVAlchemy")!=0);
}
// Admission hook32630 reads Alchemy independently; UI hooks45000-03 also require Perks.
inline bool Scroll(CItem* item,bool pipeline=true){
 auto* mod=ModRuntime::Current();
 const bool allowed=mod&&mod->CombatSetting("JMVAlchemy")&&(!pipeline||mod->CombatSetting("JMVPerks"));
 const bool quick=item&&item->GetTypeID()==ITID_ITEM_QUICK;
 return ScrollAllowed(allowed,quick,quick&&static_cast<CItemQuick*>(item)->GetQuickItemType()==IDB_SCROLL);
}
}
