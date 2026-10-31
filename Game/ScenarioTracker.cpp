#include "StdAfx.h"
#include "SaveSlot.h"
#include "GameJournal.h"
#include "ModNativeCrtRandom.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "ModAssortment.h"
#include "WorldInterface.h"
#include "Tools.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ScenarioTracker.h"
#include "Item.h"
#include "ItemManager.h"
#include "SpellManager.h"
/////////////////////////////////////////////////////////////////////////////////////
namespace { CGlobalHat initialHat; CGlobalHat *publishedHat=&initialHat; }
CGlobalHat &ActiveGlobalHat() { return *publishedHat; }
void PublishGlobalHat(CGlobalHat *hat) noexcept { publishedHat=hat ? hat : &initialHat; }
/////////////////////////////////////////////////////////////////////////////////////
// CSingleParty
/////////////////////////////////////////////////////////////////////////////////////
void CSingleParty::Serialize(CStructureSaver &file)
{
    CMemorySave f;
    file.Add(1, szName);
    if (file.IsReading())
    {
        file.Add(2, f);
        f.SetVersion(file.GetVersion());
        heroes.Load(f);
    }
    else
    {
        heroes.Store(f);
        file.Add(2, f);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// SPartySet
/////////////////////////////////////////////////////////////////////////////////////
void SPartySet::NewGame()
{
    Clear();
    parties.push_back(dbgnew CSingleParty);
    CParty &heroes = parties[0]->heroes;
    //
    // now initialize party itself
    // TODO: ATM: fix this hardcode of singleplayer start, move to lua?
    heroes.CreatePersonage("Hero", "Human Hero", "");
    heroes.szCurrentZone = "gz1g";
    heroes.nEntrance = 0;

    // heroes.people[0].AddQuick((CItemQuick*)heroes.GetItemManager()->CreateItem( "Potion Enlarge"
    // )); heroes.people[0].AddQuick((CItemQuick*)heroes.GetItemManager()->CreateItem( "Potion
    // Shrink" )); heroes.people[0].AddQuick((CItemQuick*)heroes.GetItemManager()->CreateItem(
    // "wand 1.rock[prot_fire{ic}]" ));
    // heroes.people[0].AddSpell(heroes.GetItemManager()->CreateItemSpell( "fireball",
    // heroes.GetSpellManager()));
    // heroes.people[0].AddSpell(heroes.GetItemManager()->CreateItemSpell( "charm",
    // heroes.GetSpellManager()));
    // heroes.people[0].AddSpell(heroes.GetItemManager()->CreateItemSpell( "strength",
    // heroes.GetSpellManager()));
    // heroes.people[0].AddSpell(heroes.GetItemManager()->CreateItemSpell( "weak",
    // heroes.GetSpellManager()));
    /*
    heroes.people[0].AddWeapon( (CItemWeapon*)heroes.GetItemManager()->CreateItem(
    "Hammer.Meteoric[inv_lit{ic}]" )); while ( heroes.people[0].armors.GetSize() )
        heroes.people[0].RemoveArmor( heroes.people[0].armors[0] );
    heroes.people[0].AddArmor((CItemArmor*)heroes.GetItemManager()->CreateItem( "Hadagan Low
    Shirt.Wool[fireball{ic}]" ));
    heroes.people[0].AddArmor((CItemArmor*)heroes.GetItemManager()->CreateItem( "Hadagan Low
    Pants.Wool[healing{ic}]" ));
    heroes.people[0].AddArmor((CItemArmor*)heroes.GetItemManager()->CreateItem( "Hadagan Low
    Boots.Wool[prot_acid{ic}]" ));
    heroes.people[0].AddArmor((CItemArmor*)heroes.GetItemManager()->CreateItem( "Hadagan Low
    Gloves.Wool[eagle_sight{ic}]" ));
    */
    heroes.vars.SetVar(szPartyGlobalTimeVarName, 6);
}
/////////////////////////////////////////////////////////////////////////////////////
void SPartySet::Clear()
{
    parties.clear();
    szCurrentParty = "";
    szPartyOnServer = "";
}
/////////////////////////////////////////////////////////////////////////////////////
CParty *SPartySet::GetParty(const char *pszName)
{
    CParty *pRes = 0;
    for (int i = 0; i < parties.size(); i++)
    {
        if (parties[i]->szName == pszName)
            return &parties[i]->heroes;
        // if wrong name is specified use party with "" name
        if (parties[i]->szName == "")
            pRes = &parties[i]->heroes;
    }
    return pRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void SPartySet::CreateParty(const char *pszName)
{
    CSingleParty *pNew = dbgnew CSingleParty;
    pNew->szName = pszName;
    parties.push_back(pNew);
}
/////////////////////////////////////////////////////////////////////////////////////
void SPartySet::RemoveParty(const char *pszName)
{
    for (int i = 0; i < parties.size(); i++)
    {
        if (parties[i]->szName == pszName)
            parties.erase(parties.begin() + i);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SPartySet::SetPartyFromServer(CParty &res)
{
    CParty *pRes = GetParty(szPartyOnServer);
    ASSERT(pRes);
    if (pRes)
    {
        *pRes = res;
    }
    // sync global vars with other parties
    CGSVariableSet vars = res.vars;
    CString szZone = res.szCurrentZone;
    for (int i = 0; i < parties.size(); i++)
    {
        parties[i]->heroes.vars = vars;
        parties[i]->heroes.settlementIds = res.settlementIds;
        parties[i]->heroes.szCurrentZone = szZone;
        parties[i]->heroes.nEntrance = res.nEntrance;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SPartySet::GetCurrentParty(CParty &res)
{
    CParty *pRes = GetParty(szCurrentParty);
    if (pRes)
    {
        res = *pRes;
    }
    else
        res.Clear();
    szPartyOnServer = szCurrentParty;
}
/////////////////////////////////////////////////////////////////////////////////////
void SPartySet::Serialize(CStructureSaver &file)
{
    file.AddTT(1, parties);
    file.Add(2, szCurrentParty);
    file.Add(3, szPartyOnServer);
}
/////////////////////////////////////////////////////////////////////////////////////
// CShop + SShopSet
/////////////////////////////////////////////////////////////////////////////////////
CShop::CShop()
    : itemGenerator(&itemAssort)
    , spellGenerator(&spellAssort)
{
    itemAssort.Create();
    spellAssort.Create();
    nID = -1;
    bItem = false;
    bSpell = false;
    bNeedUpdate = true;
}
/////////////////////////////////////////////////////////////////////////////////////
CShop::~CShop()
{
    itemAssort.Destroy();
    spellAssort.Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
void CShop::Serialize(CStructureSaver &)
{
    throw SaveSchema::Error(SaveSchema::ErrorCode::Incompatible, "CShop is a dynamic slot object, not a resource");
}
void CShop::SetAssortmentParameters(float *fMinLevels, float *fMaxLevels)
{
    SetAssortmentParameters(IWorldClient.GetParty(), fMinLevels, fMaxLevels);
}
void CShop::SetAssortmentParameters(CParty &party, float *fMinLevels, float *fMaxLevels)
{
    if(auto* mod=ModRuntime::Current();mod&&mod->AdvancedUI("useLS_SetAssortmentParameters")){
        const int limit=mod->ShopConstructLimit(nID);
        auto& people=party.people;std::vector<const SRPGUnitStats*> stats;
        stats.reserve(people.GetSize());for(int person=0;person<people.GetSize();++person)stats.push_back(&people[person].stats);
        ModAssortment::Apply(stats,limit,[](int maximum){return ModNativeCrtRandom::Random(maximum);},itemGenerator,spellGenerator);return;
    }
    itemGenerator.SetParameters(fMinLevels, fMaxLevels);
    spellGenerator.SetParameters(fMinLevels, fMaxLevels);
}
/////////////////////////////////////////////////////////////////////////////////////
void CShop::GenerateItems()
{
    GameJournal::Event("SHOP", "generate_items", "shop=%d enabled=%d", nID, bItem);
    if (bItem)
    {
        std::list<CItem *> items;
        std::list<CItem *>::iterator it;
        itemAssort.Clear();
        itemGenerator.SetShopID(nID);
        itemGenerator.ImportSpells(spellGenerator);
        itemGenerator.RestoreMaterials();
        itemGenerator.GenerateWeapons(40, 50, items);
        itemGenerator.GenerateArmors(40, 50, items);
        itemGenerator.GenerateQuickItems(40, 50, items);
        itemGenerator.GenerateWeaponInstructions(1, 3, items);
        itemGenerator.GenerateArmorInstructions(1, 3, items);
        itemGenerator.GenerateQuickItemInstructions(1, 3, items);
        itemGenerator.GenerateMaterials(20, 50, items);
        for (it = items.begin(); it != items.end(); ++it)
        {
            itemAssort.AddItemToWagon((*it), true);
            (*it)->Release();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CShop::GenerateSpells()
{
    GameJournal::Event("SHOP", "generate_spells", "shop=%d enabled=%d", nID, bSpell);
    if (bSpell)
    {
        std::list<CItem *> items;
        std::list<CItem *>::iterator it;
        spellAssort.Clear();
        this->spellGenerator.SetShopID(nID);
        spellGenerator.GenerateSpells(40, 50, items);
        spellGenerator.GenerateSpellInstructions(1, 3, items);
        spellGenerator.GenerateModifierInstructions(20, 50, items);
        for (it = items.begin(); it != items.end(); ++it)
        {
            spellAssort.AddItemToWagon((*it), true);
            (*it)->Release();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CShop::FillStartAssortment()
{
    GameJournal::Event("SHOP", "assortment_start", "shop=%d", nID);
    int i;
    CStringArray szTmpArray;
    CItemManager *pIM = itemAssort.GetItemManager();
    CSpellManager *pSM = spellAssort.GetSpellManager();

    pIM->GetWeaponShapeNames(szTmpArray);
    for (i = 0; i < szTmpArray.GetSize(); i++)
    {
        const SIDBWeaponShape *pShape = pIM->GetWeaponShape(szTmpArray[i]);
        if (pShape && pShape->IsAvailableInShop(nID - 1))
            itemGenerator.AddWeaponShape(pShape);
    }

    pIM->GetArmorShapeNames(szTmpArray);
    for (i = 0; i < szTmpArray.GetSize(); i++)
    {
        const SIDBArmorShape *pShape = pIM->GetArmorShape(szTmpArray[i]);
        if (pShape && pShape->IsAvailableInShop(nID - 1))
            itemGenerator.AddArmorShape(pShape);
    }

    pIM->GetQuickItemShapeNames(szTmpArray);
    for (i = 0; i < szTmpArray.GetSize(); i++)
    {
        const SIDBQuickItemShape *pShape = pIM->GetQuickItemShape(szTmpArray[i]);
        if (pShape && pShape->IsAvailableInShop(nID - 1))
            itemGenerator.AddQuickItemShape(pShape);
    }

    pIM->GetMaterialNames(szTmpArray);
    for (i = 0; i < szTmpArray.GetSize(); i++)
    {
        const SIDBMaterial *pMat = pIM->GetMaterial(szTmpArray[i]);
        if (pMat && pMat->IsAvailableInShop(nID - 1))
            itemGenerator.AddMaterial(pMat, 20, 50);
    }

    pSM->GetPrototypeNames(szTmpArray);
    for (i = 0; i < szTmpArray.GetSize(); i++)
    {
        const SSDBSpellPrototype *pProt = pSM->GetPrototype(
            pSM->GetPrototypeCodeName(szTmpArray[i]));
        if (pProt && pProt->IsAvailableInShop(nID - 1))
        {
            spellGenerator.AddSpellPrototype(pProt);
            itemGenerator.AddSpellPrototype(pProt);
        }
    }

    pSM->GetRuneNames(szTmpArray);
    for (i = 0; i < szTmpArray.GetSize(); i++)
    {
        const SSDBModifier *pMod = pSM->GetModifier(pSM->GetRuneCodeName(szTmpArray[i]));
        if (pMod && pMod->IsAvailableInShop(nID - 1))
        {
            spellGenerator.AddSpellModifier(pMod);
            itemGenerator.AddSpellModifier(pMod);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SShopSet::Clear()
{
    shops.clear();
}
/////////////////////////////////////////////////////////////////////////////////////
CShop *SShopSet::GetShop(int nID)
{
    for (int i = 0; i < shops.size(); i++)
    {
        if (shops[i]->nID == nID)
            return shops[i];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void SShopSet::Serialize(CStructureSaver &file)
{
    file.AddTT(1, shops);
}
/////////////////////////////////////////////////////////////////////////////////////
void SShopSet::NewGame()
{
    Clear();
    CShop *pShop;

    {
        // GIPAT ITEMS
        pShop = dbgnew CShop;
        pShop->nID = 1;
        pShop->bItem = true;
        pShop->bSpell = false;
#ifdef __DEMO__
        pShop->bSpell = true;
#endif // __DEMO__
        pShop->fKoeffs[0] = 1.f;
        pShop->fKoeffs[1] = 0.5f;
        pShop->fKoeffs[2] = 0.2f;
        pShop->fKoeffs[3] = 0.1f;
        pShop->fKoeffs[4] = 1.f;
        pShop->fKoeffs[5] = 0.5f;
        pShop->fKoeffs[6] = 0.2f;
        pShop->fKoeffs[7] = 0.1f;
        pShop->fKoeffs[8] = 0.2f;
        pShop->FillStartAssortment();
        shops.push_back(pShop);

        // GIPAT SPELLS
        pShop = dbgnew CShop;
        pShop->nID = 2;
        pShop->bItem = false;
        pShop->bSpell = true;
        pShop->fKoeffs[0] = 1.f;
        pShop->fKoeffs[1] = 0.5f;
        pShop->fKoeffs[2] = 0.2f;
        pShop->fKoeffs[3] = 0.1f;
        pShop->fKoeffs[4] = 1.f;
        pShop->fKoeffs[5] = 0.5f;
        pShop->fKoeffs[6] = 0.2f;
        pShop->fKoeffs[7] = 0.1f;
        pShop->fKoeffs[8] = 0.2f;
        pShop->FillStartAssortment();
        shops.push_back(pShop);

        // KANIA ITEMS & SPELLS
        pShop = dbgnew CShop;
        pShop->nID = 3;
        pShop->bItem = true;
        pShop->bSpell = true;
        pShop->fKoeffs[0] = 1.f;
        pShop->fKoeffs[1] = 0.5f;
        pShop->fKoeffs[2] = 0.2f;
        pShop->fKoeffs[3] = 0.1f;
        pShop->fKoeffs[4] = 1.f;
        pShop->fKoeffs[5] = 0.5f;
        pShop->fKoeffs[6] = 0.2f;
        pShop->fKoeffs[7] = 0.1f;
        pShop->fKoeffs[8] = 0.2f;
        pShop->FillStartAssortment();
        shops.push_back(pShop);

        // HADAGAN ITEMS AND SPELLS
        pShop = dbgnew CShop;
        pShop->nID = 4;
        pShop->bItem = true;
        pShop->bSpell = true;
        pShop->fKoeffs[0] = 1.f;
        pShop->fKoeffs[1] = 0.5f;
        pShop->fKoeffs[2] = 0.2f;
        pShop->fKoeffs[3] = 0.1f;
        pShop->fKoeffs[4] = 1.f;
        pShop->fKoeffs[5] = 0.5f;
        pShop->fKoeffs[6] = 0.2f;
        pShop->fKoeffs[7] = 0.1f;
        pShop->fKoeffs[8] = 0.2f;
        pShop->FillStartAssortment();
        shops.push_back(pShop);

        // JUN ITEMS
        pShop = dbgnew CShop;
        pShop->nID = 5;
        pShop->bItem = true;
        pShop->bSpell = true;
        pShop->fKoeffs[0] = 0;
        pShop->fKoeffs[1] = 0;
        pShop->fKoeffs[2] = 0;
        pShop->fKoeffs[3] = 0;
        pShop->fKoeffs[4] = 0;
        pShop->fKoeffs[5] = 0;
        pShop->fKoeffs[6] = 0;
        pShop->fKoeffs[7] = 0;
        pShop->fKoeffs[8] = 0;
        pShop->FillStartAssortment();
        shops.push_back(pShop);
    }
    if(auto*mod=ModRuntime::Current())mod->RecalcShops(*this,false);
}
/////////////////////////////////////////////////////////////////////////////////////
// SHatUser
/////////////////////////////////////////////////////////////////////////////////////
void SHatUser::Serialize(CStructureSaver &file)
{
    file.Add(1, szName);
    file.Add(2, szPassword);
    file.AddT(3, parties);
}
/////////////////////////////////////////////////////////////////////////////////////
// CGlobalHat
/////////////////////////////////////////////////////////////////////////////////////
void CGlobalHat::Clear()
{
    parties.clear();
    users.clear();
    singleParty.Clear();
    nLastPartyID = 1;
    shops.Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CGlobalHat::Serialize(CStructureSaver &file)
{
    file.AddT(1, parties);
    file.AddT(2, users);
    file.Add(3, nLastPartyID);
    file.Add(4, shops);
    file.Add(5, singleParty);
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// Get party from hat if partyID is not initialized then use first party

/////////////////////////////////////////////////////////////////////////////////////
// Set modified party to hat from game

/////////////////////////////////////////////////////////////////////////////////////
void CGlobalHat::GetSinglePlayerParty(CParty &res)
{
    singleParty.GetCurrentParty(res);
}
/////////////////////////////////////////////////////////////////////////////////////
void CGlobalHat::SetSinglePlayerParty(CParty &res)
{
    singleParty.SetPartyFromServer(res);
}
/////////////////////////////////////////////////////////////////////////////////////
void CGlobalHat::NewSingleGame()
{
    singleParty.NewGame();
    shops.NewGame();
}
/////////////////////////////////////////////////////////////////////////////////////
void CGlobalHat::UpdateAssortment()
{
    for (int i = 0; i < shops.shops.size(); i++)
        shops.shops[i]->bNeedUpdate = true;
}
/////////////////////////////////////////////////////////////////////////////////////
