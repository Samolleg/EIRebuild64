#pragma once
namespace ModSpellDanger {
// Original0067EFC0's decoded stock selector, not an extended-ID remap.
inline bool StockLoadKind(int raw){return raw==6||raw==7||raw==8||raw==39;}
template<class Add> void ReconstructNativeLoad(int raw,bool mainEffect,Add add){
 if(mainEffect&&StockLoadKind(raw))add(raw);
}
inline bool ChangeNeeded(bool add,bool mapped){return add!=mapped;}
// Preserve the existing typed per-effect owner; never remove an unowned area.
template<class Apply> void Change(bool add,bool&mapped,Apply apply){
 if(!ChangeNeeded(add,mapped))return;
 apply(add);mapped=add;
}
}
