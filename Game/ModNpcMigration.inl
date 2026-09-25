// Installed-addon NPC equipment migration, adapted to the EIRebuild object API.
// Included only by ModPackage.cpp after engine class definitions.
struct ModNpcMigrationAccess {
    static CItemSpell* Spell(CUnit* unit,int slot) { return unit->lpQSpells[slot]; }
    static void Restore(CUnit* unit,int slot,CItemSpell* item) {
        unit->lpQSpells[slot]=item;
        if(item)item->AddRef();
        unit->SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);
    }
};
namespace ModNpcMigration {
struct Counts {int scanned=0,updated=0,unchanged=0,failed=0,dead=0,owned=0;};
static bool Has(const CString& text){return !text.IsEmpty()&&text.CompareNoCase("none")!=0;}
template<class T,size_t N> struct Prepared {
 CItemManager*manager;T* items[N]={};
 explicit Prepared(CItemManager*m):manager(m){}
 ~Prepared(){for(auto*&item:items)if(item&&item->IsUnreferenced())manager->Release(item);}
};
struct Snapshot {
 DWORD active;CItemWeapon*weapons[QI_MAX_WEAPONS]={};
 CItemArmor*armors[RPG_NUM_ARMOR_TYPES]={};CItemSpell*spells[QI_MAX_SPELLS]={};
 explicit Snapshot(CUnit*u):active(u->GetCurrentWeaponSlotIndex()){
  for(int i=0;i<QI_MAX_WEAPONS;++i){weapons[i]=u->GetQWeapon(i);if(weapons[i])weapons[i]->AddRef();}
  for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i){armors[i]=u->GetQArmor(i);if(armors[i])armors[i]->AddRef();}
  for(int i=0;i<QI_MAX_SPELLS;++i){spells[i]=ModNpcMigrationAccess::Spell(u,i);if(spells[i])spells[i]->AddRef();}
 }
 ~Snapshot(){for(auto*p:weapons)if(p)p->Release();for(auto*p:armors)if(p)p->Release();for(auto*p:spells)if(p)p->Release();}
};
static bool Same(CItem*a,CItem*b){return (a==nullptr)==(b==nullptr)&&(!a||b->Compare(a));}
static bool SameSpell(CItemSpell*a,CItemSpell*b){
 if(!Same(a,b))return false;if(!a)return true;
 CSpell*x=a->GetSpell(),*y=b->GetSpell();if((x==nullptr)!=(y==nullptr))return false;if(!x)return true;
 const CSpellEffect&l=x->GetEffect(),&r=y->GetEffect();
 return x->GetSubtypeID()==y->GetSubtypeID()&&l.nSpellID==r.nSpellID&&
 l.nVisStrength==r.nVisStrength&&l.nDuration==r.nDuration&&l.fpPower==r.fpPower&&
 l.fpRadius==r.fpRadius&&l.bQuiet==r.bQuiet&&x->GetSpeed()==y->GetSpeed()&&
 x->GetRange()==y->GetRange()&&x->GetNumTargets()==y->GetNumTargets()&&
 x->GetResolveFlags()==y->GetResolveFlags()&&x->GetManaCost()==y->GetManaCost()&&
 x->GetNumSlots()==y->GetNumSlots()&&x->GetPrice()==y->GetPrice()&&
 x->GetActionPoints()==y->GetActionPoints()&&x->GetComplexity()==y->GetComplexity()&&
 memcmp(x->GetRunes(),y->GetRunes(),8)==0;
}
template<class T>static T*Make(CItemManager*m,const CString&spec,EClassIDs kind){
 CItem*item=m->CreateItem(spec);if(!item)return nullptr;
 if(item->WhoAmI()==kind)return static_cast<T*>(item);
 if(item->IsUnreferenced())m->Release(item);return nullptr;
}
static bool Rollback(CUnit*u,const Snapshot&s,bool weapons,bool armors,bool spells){
 bool okay=true;
 if(weapons){
  u->ChangeWeapon(DWORD(-1));u->RemoveWeapon(0);u->RemoveWeapon(1);
  for(int i=0;i<2;++i)if(s.weapons[i])okay=(u->AddWeapon(s.weapons[i])==i)&&okay;
  if(s.active==DWORD(-1))u->ChangeWeapon(DWORD(-1));
  else if(s.active<QI_MAX_WEAPONS&&u->GetQWeapon(s.active))okay=(u->ChangeWeapon(s.active)!=FALSE)&&okay;
  else okay=false;
 }
 if(armors){
  for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)if(u->GetQArmor(i))u->Remove(static_cast<ERPGArmorTypes>(i));
  for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)if(s.armors[i]){u->Wear(s.armors[i]);okay=(u->GetQArmor(i)==s.armors[i])&&okay;}
 }
 if(spells){
  for(int i=0;i<QI_MAX_SPELLS;++i)u->RemoveQSpell(i);
  for(int i=0;i<QI_MAX_SPELLS;++i)ModNpcMigrationAccess::Restore(u,i,s.spells[i]);
 }
 return okay;
}
static bool Migrate(CUnit*u,CItemManager*m,Counts&c){
 ++c.scanned;if(u->IsDead()){++c.dead;return true;}if(u->GetOwner()){++c.owned;return true;}
 CUnitServer*server=dynamic_cast<CUnitServer*>(u->GetUnitSpecific());if(!server)return true;
 DWORD id=u->GetSubtypeID();if(id>=static_cast<DWORD>(theUnitDatabase.prototypes.GetNumEntries())){++c.failed;return false;}
 const SUDBUnitPrototype*p=theUnitDatabase.prototypes.GetEntry(id);if(!p){++c.failed;return false;}
 const bool human=u->GetTypeID()==MOTID_HUMAN;
 Prepared<CItemWeapon,2>w(m);Prepared<CItemArmor,RPG_NUM_ARMOR_TYPES>a(m);
 Prepared<CItemSpell,QI_MAX_SPELLS>s(m);int nw=0,ns=0;
 if(human){
  const CString*specs[2]={&p->szWeapon,&p->szWeapon2};
  for(int i=0;i<2;++i)if(Has(*specs[i])){
   w.items[nw]=Make<CItemWeapon>(m,*specs[i],HPC_ITEM_WEAPON);
   if(!w.items[nw]){++c.failed;return false;}++nw;
  }
  for(const CString&spec:p->szWears)if(Has(spec)){
   CItemArmor*item=Make<CItemArmor>(m,spec,HPC_ITEM_ARMOR);
   if(!item){++c.failed;return false;}
   int slot=static_cast<int>(item->GetArmorType());
   if(slot<0||slot>=RPG_NUM_ARMOR_TYPES){if(item->IsUnreferenced())m->Release(item);++c.failed;return false;}
   if(a.items[slot]&&a.items[slot]->IsUnreferenced())m->Release(a.items[slot]);
   a.items[slot]=item; // Last duplicate armor slot wins, as in WearAll.
  }
 }
 if(p->szSpells.size()>u->GetSpellSlotCount()){++c.failed;return false;}
 ns=static_cast<int>(p->szSpells.size());
 for(int i=0;i<ns;++i){
  CString spec;spec.Format("spell container [%s]",(LPCSTR)p->szSpells[i]);
  s.items[i]=Make<CItemSpell>(m,spec,HPC_ITEM_SPELL);
  if(!s.items[i]){++c.failed;return false;}
 }
 bool sameW=true,sameA=true,sameS=true;
 if(human){
  for(int i=0;i<2;++i)if(!Same(u->GetQWeapon(i),w.items[i]))sameW=false;
  for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)if(!Same(u->GetQArmor(i),a.items[i]))sameA=false;
 }
 for(int i=0;i<QI_MAX_SPELLS;++i)if(!SameSpell(ModNpcMigrationAccess::Spell(u,i),s.items[i]))sameS=false;
 if(sameW&&sameA&&sameS){++c.unchanged;return true;}
 Snapshot before(u);bool changedW=false,changedA=false,changedS=false;
 if(human&&!sameW){
  changedW=true;u->ChangeWeapon(DWORD(-1));u->RemoveWeapon(0);u->RemoveWeapon(1);
  for(int i=0;i<nw;++i){if(u->AddWeapon(w.items[i])!=i)goto failure;w.items[i]=nullptr;}
  u->ChangeWeapon(nw?0:DWORD(-1));
 }
 if(human&&!sameA){
  changedA=true;for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)if(u->GetQArmor(i))u->Remove(static_cast<ERPGArmorTypes>(i));
  for(int i=0;i<RPG_NUM_ARMOR_TYPES;++i)if(a.items[i]){
   CItemArmor*item=a.items[i];u->Wear(item);if(u->GetQArmor(i)!=item)goto failure;a.items[i]=nullptr;
  }
 }
 if(!sameS){
  changedS=true;for(int i=0;i<QI_MAX_SPELLS;++i)u->RemoveQSpell(i);
  for(int i=0;i<ns;++i){if(u->AddQSpell(s.items[i])!=i)goto failure;s.items[i]=nullptr;}
 }
 if(human&&(changedW||changedA))server->RecalcRPGStats(false);
 server->NotifyItemsUpdated();if(changedW)server->NotifyWeaponUpdated();++c.updated;return true;
failure:
 if(!Rollback(u,before,changedW,changedA,changedS))OutputDebugStringA("NPC equipment rollback incomplete\n");
 if(human&&(changedW||changedA))server->RecalcRPGStats(false);
 server->NotifyItemsUpdated();if(changedW)server->NotifyWeaponUpdated();++c.failed;return false;
}
} // namespace ModNpcMigration
