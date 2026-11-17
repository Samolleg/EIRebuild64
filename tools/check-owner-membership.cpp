// Native regression for inventory refresh followed by a purchase. No game,
// renderer, databases, user saves or UI are initialized by this executable.
#include "stdafx.h"
#include "Game/Party.h"
#include "Game/GameSave.h"
#include "Game/Item.h"
#include "Game/ItemManager.h"
#include "Game/SpellManager.h"
#include <cstdio>
#include <stdexcept>

namespace {
void Check(bool value,const char *message) { if(!value)throw std::runtime_error(message); }
using Party=std::shared_ptr<CParty>;
Party NewParty() { auto p=std::make_shared<CParty>();p->Create();return p; }
CItem *Add(const Party &p,int id=-1) {
    auto *item=static_cast<CItem *>(p->GetItemManager()->CreateData(ITID_ITEM_SPELL,id));
    item->SetCount(7);item->AddFreeSpell(p->GetSpellManager()->CreateSpell());
    p->allWagonItems.Add(item);item->AddRef();return item;
}
std::shared_ptr<SaveSchema::SavedOwners> Owners(const Party &p) {
    auto o=std::make_shared<SaveSchema::SavedOwners>();
    o->Add(SaveSchema::OwnerKind::Item,{p,p->GetItemManager()});
    o->Add(SaveSchema::OwnerKind::Spell,{p,p->GetSpellManager()});return o;
}
void Membership(CBaseManager *manager,CGameBase *expected) {
    int count=0;bool found=false;
    manager->ForEach([&](CGameBase *object){
        ++count;found|=object==expected;
        Check(manager->Find(object->GetID())==object,"enumeration disagrees with lookup");
    });
    Check(count==manager->GetNumElements(),"enumeration count disagrees with lookup");
    Check(!expected||found,"live item omitted from owner membership");
}
void RoundTrip(const Party &source) {
    auto owners=Owners(source);SaveSchema::Registry registry;SaveSchema::RegisterGameTypes(registry,owners);
    CMemorySave file;
    SaveSchema::Store(file,registry,[&](SaveSchema::Writer &w){owners->StoreMembers(w);source->Store(w);});
    ObjectIdentity::RestoreNamespace allocation(ObjectIdentity::GetNext());
    auto dest=std::make_shared<CParty>();
    dest->CreateSavedOwners(source->GetItemManager()->GetRTID(),source->GetSpellManager()->GetRTID());
    auto target=Owners(dest);SaveSchema::Registry types;SaveSchema::RegisterGameTypes(types,target);file.Seek(0);
    auto graph=SaveSchema::Load(file,types,[&](SaveSchema::Reader &r){target->LoadMembers(r);dest->Load(r);});
    Check(dest->allWagonItems.GetSize()==source->allWagonItems.GetSize(),"wagon count changed");
    for(INT_PTR i=0;i<source->allWagonItems.GetSize();++i){
        auto *a=source->allWagonItems[i],*b=dest->allWagonItems[i];
        Check(a->GetRTID()==b->GetRTID()&&a->GetCount()==b->GetCount(),"item identity/quantity changed");
        Check(b->GetSpell(0)&&a->GetSpell(0)->GetRTID()==b->GetSpell(0)->GetRTID(),"item spell binding changed");
    }
    Membership(dest->GetItemManager(),nullptr);Membership(dest->GetSpellManager(),nullptr);
}
void Cases() {
    {
        auto p=NewParty();Add(p);Add(p);RoundTrip(p);
        std::puts("PASS ordinary inventory/spell round-trip");
    }
    {
        auto p=NewParty();CSmartPtr<CItem> retired=Add(p);const int id=retired->GetID();
        // Clear removes the registration, but an external consumer still owns
        // the old item. ClearHashTable makes the numeric ID available again.
        p->Clear();auto *purchased=Add(p);
        Check(purchased->GetID()==id,"fixture did not reuse the retired item ID");
        retired=nullptr;
        Membership(p->GetItemManager(),purchased);RoundTrip(p);
        std::puts("PASS delayed item release after party refresh and purchase");
    }
    {
        auto p=NewParty();auto *manager=p->GetSpellManager();
        CSmartPtr<CSpell> retired=manager->CreateSpell();const int id=retired->GetID();
        manager->RemoveAll();auto *item=Add(p);
        Check(item->GetSpell(0)->GetID()==id,"fixture did not reuse the retired spell ID");
        retired=nullptr;Membership(manager,item->GetSpell(0));RoundTrip(p);
        std::puts("PASS delayed spell release after manager refresh");
    }
    {
        auto p=NewParty();auto *manager=p->GetItemManager();
        CSmartPtr<CItem> retired=static_cast<CItem *>(manager->CreateData(ITID_ITEM_SPELL,41));
        auto *replacement=Add(p,41);
        Membership(manager,replacement);retired=nullptr;
        Membership(manager,replacement);RoundTrip(p);
        p->RemoveItemFromWagon(replacement,true);
        Check(manager->GetNumElements()==0,"normal release retained a live registration");
        Membership(manager,nullptr);
        std::puts("PASS replacement at an occupied ID and normal release");
    }
}
}
int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    try {Cases();std::fflush(stdout);ExitProcess(0);}
    catch(const std::exception &e){std::printf("FAIL %s\n",e.what());std::fflush(stdout);ExitProcess(1);}
}
