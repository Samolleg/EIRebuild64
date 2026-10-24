#include "StdAfx.h"
#include "SaveSlot.h"
#include "SaveSchema.h"
#include <memory>
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Party.h"
// due to design mistake this file has to include worldclient - file not related to
// this one any how - mapobjectmanager for some strange reason handles database of units
// and is stored in worldclient
#include "WorldClient.h"
#include "WorldServer.h"
#include "MapObjectManager.h"
#include "SpellConstructor.h"
#include "ModConstructorLifetime.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "Item.h"
#include "ItemManager.h"
#include "ItemConstructor.h"
#include "GlobalMap.h"
#include "WorldInterface.h"
#include "RandomGen.h"
/////////////////////////////////////////////////////////////////////////////////////
static CParty *pCurrentParty = 0;
CLMPPartySet LMPPartySet;
/////////////////////////////////////////////////////////////////////////////////////
// CPersonage
/////////////////////////////////////////////////////////////////////////////////////
CPersonage::CPersonage()
{
    complection.strength = 0.8f;
    complection.grace = 0.2f;
    complection.tall = 1.0f;
    pose = UP_STAND;
    dwCurrentWeapon = 0;
    bIsSelected = false;
    nFollowToID = -1;
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
CPersonage::~CPersonage()
{
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::ClearItems()
{
    int i;
    for (i = 0; i < weapons.GetSize(); i++)
        if (weapons[i]) weapons[i]->Release();
    for (i = 0; i < items.GetSize(); i++)
        if (items[i]) items[i]->Release();
    for (i = 0; i < armors.GetSize(); i++)
        if (armors[i]) armors[i]->Release();
    for (i = 0; i < spells.GetSize(); i++)
        if (spells[i]) spells[i]->Release();
    weapons.SetSize(0);
    items.SetSize(0);
    armors.SetSize(0);
    spells.SetSize(0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::Clear()
{
    ClearItems();
    pose = UP_STAND;
    dwCurrentWeapon = 0;
    bIsSelected = false;
    nFollowToID = -1;
    aggression = PAS_AGGRESSIVE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::Load(CSave &file)
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::Store(CSave &file)
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
typedef CSimplyArray<CItem *, CItem *> CItemArray;
/////////////////////////////////////////////////////////////////////////////////////
static void CopyItemArray(CItemArray &dst, CItemArray &src, CWorld *pWorld)
{
    ASSERT(dst.GetSize() == 0);
    dst.SetSize(src.GetSize());
    for (int i = 0; i < src.GetSize(); i++)
    {
        CItem *pSrc = src[i];
        dst[i] = pWorld->DuplicateItem(pSrc);
        dst[i]->SetCount(pSrc->GetCount());
        dst[i]->AddRef();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::CopyFrom(CPersonage &src, CWorld *pMyWorld)
{
    Clear();
    szName = src.szName;
    szTemplate = src.szTemplate;
    szPrimTexture = src.szPrimTexture;
    szSecondTexture = src.szSecondTexture;
    szPrototype = src.szPrototype;
    nID = src.nID;
    stats = src.stats;
    complection = src.complection;
    CopyItemArray(*(CItemArray *)&weapons, *(CItemArray *)&src.weapons, pMyWorld);
    CopyItemArray(*(CItemArray *)&items, *(CItemArray *)&src.items, pMyWorld);
    CopyItemArray(*(CItemArray *)&armors, *(CItemArray *)&src.armors, pMyWorld);
    CopyItemArray(*(CItemArray *)&spells, *(CItemArray *)&src.spells, pMyWorld);
    pose = src.pose;
    aggression = src.aggression;

    dwCurrentWeapon = src.dwCurrentWeapon;
    bIsSelected = src.bIsSelected;
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::CopyItemsFrom(CPersonage &src, CWorld *pMyWorld)
{
    ClearItems();
    CopyItemArray(*(CItemArray *)&weapons, *(CItemArray *)&src.weapons, pMyWorld);
    CopyItemArray(*(CItemArray *)&items, *(CItemArray *)&src.items, pMyWorld);
    CopyItemArray(*(CItemArray *)&armors, *(CItemArray *)&src.armors, pMyWorld);
    CopyItemArray(*(CItemArray *)&spells, *(CItemArray *)&src.spells, pMyWorld);
}
/////////////////////////////////////////////////////////////////////////////////////
float CPersonage::GetItemsWeight() const
{
    float fItemsWeight = 0;
    int i;
    // weapons
    for (i = 0; i < weapons.GetSize(); ++i)
    {
        if (weapons[i])
            fItemsWeight += weapons[i]->CItem::GetWeight();
    }
    // quick items
    for (i = 0; i < items.GetSize(); ++i)
    {
        if (items[i])
            fItemsWeight += items[i]->GetWeight();
    }
    // spells
    for (i = 0; i < spells.GetSize(); ++i)
    {
        if (spells[i])
            fItemsWeight += spells[i]->GetWeight();
    }
    // armors
    for (i = 0; i < armors.GetSize(); ++i)
    {
        if (armors[i])
            fItemsWeight += armors[i]->GetWeight();
    }
    //
    return fItemsWeight;
}
/////////////////////////////////////////////////////////////////////////////////////
CItemWeapon *CPersonage::AddWeapon(CItemWeapon *pItem)
{
    ASSERT(pItem);
    pItem = pItem->Duplicate(pItem->GetWorld()->GetSpellManager());
    pItem->AddRef();
    weapons.Add(pItem);
    return pItem;
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::RemoveWeapon(CItemWeapon *pItem)
{
    for (int i = 0; i < weapons.GetSize(); i++)
    {
        if (weapons[i] == pItem)
        {
            pItem->Release();
            weapons.RemoveAt(i);
            return;
        }
    }
    ASSERT(0 && "Trying to remove weapon personage does not have");
}
/////////////////////////////////////////////////////////////////////////////////////
CItemQuick *CPersonage::AddQuick(CItemQuick *pItem)
{
    ASSERT(pItem);
    pItem = pItem->Duplicate(pItem->GetWorld()->GetSpellManager());
    pItem->AddRef();
    items.Add(pItem);
    return pItem;
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::RemoveQuick(CItemQuick *pItem)
{
    for (int i = 0; i < items.GetSize(); i++)
    {
        if (items[i] == pItem)
        {
            pItem->Release();
            items.RemoveAt(i);
            return;
        }
    }
    ASSERT(0 && "Trying to remove item personage does not have");
}
/////////////////////////////////////////////////////////////////////////////////////
CItemArmor *CPersonage::AddArmor(CItemArmor *pItem)
{
    ASSERT(pItem);
    pItem = pItem->Duplicate(pItem->GetWorld()->GetSpellManager());
    pItem->AddRef();
    armors.Add(pItem);
    return pItem;
    // here its possible to add Wear() function...
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::RemoveArmor(CItemArmor *pItem)
{
    for (int i = 0; i < armors.GetSize(); i++)
    {
        if (armors[i] == pItem)
        {
            // and here is suitable place for Unwear
            pItem->Release();
            armors.RemoveAt(i);
            return;
        }
    }
    ASSERT(0 && "Trying to remove armor personage does not have");
}
/////////////////////////////////////////////////////////////////////////////////////
CItemSpell *CPersonage::AddSpell(CItemSpell *pItem)
{
    ASSERT(pItem);
    pItem = pItem->Duplicate(pItem->GetWorld()->GetSpellManager());
    pItem->AddRef();
    spells.Add(pItem);
    return pItem;
    // here its possible to add Wear() function...
}
/////////////////////////////////////////////////////////////////////////////////////
void CPersonage::RemoveSpell(CItemSpell *pItem)
{
    for (int i = 0; i < spells.GetSize(); i++)
    {
        if (spells[i] == pItem)
        {
            pItem->Release();
            spells.RemoveAt(i);
            return;
        }
    }
    ASSERT(0 && "Trying to remove armor personage does not have");
}
/////////////////////////////////////////////////////////////////////////////////////
// CParty
/////////////////////////////////////////////////////////////////////////////////////
CParty::CParty()
{
    pItemManager = 0;
    pSpellManager = 0;
    pItemConstructor = 0;
    pSpellConstructor = 0;
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CParty::~CParty()
{
    Clear(TRUE, TRUE);
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
CParty &CParty::operator=(const CParty &src)
{
    if (this != &src) SaveSchema::Slot::CopyParty(*this, src);
    return *this;
}
/////////////////////////////////////////////////////////////////////////////////////
CParty::CParty(const CParty &src)
{
    pItemManager = 0;
    pSpellManager = 0;
    pItemConstructor = 0;
    pSpellConstructor = 0;
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    SaveSchema::Slot::CopyParty(*this, src);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CParty::Create()
{
    return CreateOwners(0, 0);
}
BOOL CParty::CreateSavedOwners(ObjectId item, ObjectId spell)
{
    if (!ObjectIdentity::RestoreNamespace::Active() || !item || !spell || item == spell || pItemManager || pSpellManager)
        throw SaveSchema::Error(SaveSchema::ErrorCode::Identity, "party requires fresh saved owners");
    return CreateOwners(item, spell);
}
BOOL CParty::CreateOwners(ObjectId item, ObjectId spell)
{
    CWorld::Create();
    ASSERT(!pItemManager);
    // ItemManager
    pItemManager = item ? ObjectIdentity::CreateWith<std::unique_ptr<CItemManager>>(item, [] { return new CItemManager; }).release() : dbgnew CItemManager;
    pItemManager->AddRef();
    ((CBaseManager *)pItemManager)->Create(this);
    // SpellManager
    pSpellManager = spell ? ObjectIdentity::CreateWith<std::unique_ptr<CSpellManager>>(spell, [] { return new CSpellManager; }).release() : dbgnew CSpellManager;
    pSpellManager->AddRef();
    ((CBaseManager *)pSpellManager)->Create(this);
    // ItemConstructor
    pItemConstructor = dbgnew CItemConstructor;
    pItemConstructor->AddRef();
    pItemConstructor->Create();
    // Spell constructor
    pSpellConstructor = dbgnew CSpellConstructor;
    pSpellConstructor->AddRef();
    pSpellConstructor->Create();
    Clear(TRUE, FALSE);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CParty::Destroy()
{
    people.RemoveAll();
    for (int i = 0; i < allWagonItems.GetSize(); i++)
        if (allWagonItems[i]) allWagonItems[i]->Release();
    allWagonItems.RemoveAll();
    //
    if (pItemConstructor)
    {
        pItemConstructor->DecRefCount();
        delete pItemConstructor;
        pItemConstructor = NULL;
    }
    //
    if (pSpellConstructor)
    {
        if (ModConstructorLifetime::PolicyBound())
            ModConstructorLifetime::ReleaseOwned(pSpellConstructor,
                [](const void* object, const void* vtable) {
                    ModConstructorLifetime::NoteInvalid(ModConstructorLifetime::Party, object, vtable);
                });
        else
        {
            pSpellConstructor->DecRefCount();
            delete pSpellConstructor;
        }
        pSpellConstructor = 0;
    }
    //
    if (pItemManager)
    {
        pItemManager->DecRefCount();
        delete pItemManager;
        pItemManager = NULL;
    }
    //
    if (pSpellManager)
    {
        pSpellManager->DecRefCount();
        delete pSpellManager;
        pSpellManager = NULL;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CParty::Clear(BOOL bDelVars, BOOL bDestructor)
{
    int i;
    for (i = 0; i < allWagonItems.GetSize(); i++)
        if (allWagonItems[i]) allWagonItems[i]->Release();
    allWagonItems.RemoveAll();
    if (pItemManager)
        pItemManager->RemoveAll();
    if (pSpellManager)
    {
        pSpellManager->Destroy();
        if (!bDestructor)
            ((CBaseManager *)pSpellManager)->Create(this);
    }
    people.RemoveAll();
    if (bDelVars)
        vars.clear();
    szCurrentZone = "";
    nEntrance = 0;
    nMoney = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CParty::Serialize(CStructureSaver &file)
{
    CMemorySave f;
    if (file.IsReading())
    {
        file.Add(1, f);
        f.SetVersion(file.GetVersion());
        Load(f);
    }
    else
    {
        Store(f);
        file.Add(1, f);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CParty::Load(CSave &file, BOOL bVars)
{
    CGSVariableSet held=vars;
    SaveSchema::Slot::LoadParty(file,*this);
    if (!bVars) vars=held;
}
/////////////////////////////////////////////////////////////////////////////////////
void CParty::Store(CSave &file, BOOL bVars)
{
    SaveSchema::Slot::StoreParty(file,*this);
}
/////////////////////////////////////////////////////////////////////////////////////
// return index of item in allWagonItems, -1 if not found
int CParty::GetWagonItemIdx(CItem *pItem)
{
    for (int i = 0; i < allWagonItems.GetSize(); i++)
    {
        if (pItem == allWagonItems[i])
            return i;
    }
    return -1;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CParty::AddItemToWagon(CItem *pItem, bool bMoveAllStack, bool bNeedStack)
{
    int i, num = 1;
    ASSERT(pItem);
    if (!pItem)
        return NULL;
    if (bMoveAllStack)
        num = pItem->GetCount();
    if (bNeedStack)
    {
        for (i = 0; i < allWagonItems.GetSize(); i++)
        {
            // if its possible to stack item - do it
            if (allWagonItems[i]->Compare(pItem))
            {
                allWagonItems[i]->SetCount(allWagonItems[i]->GetCount() + num);
                return allWagonItems[i];
            }
        }
    }
    CItem *pNewItem = DuplicateItem(pItem);
    pNewItem->SetCount(num);
    allWagonItems.Add(pNewItem);
    pNewItem->AddRef();
    return pNewItem;
}
/////////////////////////////////////////////////////////////////////////////////////
CItem *CParty::RemoveItemFromWagon(CItem *pItem, bool bMoveAllStack)
{
    int nIdx = GetWagonItemIdx(pItem);
    ASSERT(nIdx >= 0 && "Trying to remove item not in wagon");
    if (nIdx >= 0)
    {
        if (allWagonItems[nIdx]->GetCount() > 1 && !bMoveAllStack)
        {
            allWagonItems[nIdx]->SetCount(allWagonItems[nIdx]->GetCount() - 1);
            return allWagonItems[nIdx];
        }
        else
        {
            allWagonItems[nIdx]->Release();
            allWagonItems.RemoveAt(nIdx);
            return 0;
        }
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CParty::RemoveAllQuestItems()
{
    for (int i = allWagonItems.GetSize() - 1; i >= 0; i--)
    {
        CItem *pItem = allWagonItems[i];
        if (pItem->WhoAmI() == HPC_ITEM_QUEST)
            RemoveItemFromWagon(pItem, true);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CParty::RemoveAllItems()
{
    for (int i = 0; i < allWagonItems.GetSize(); i++)
        if (allWagonItems[i]) allWagonItems[i]->Release();
    allWagonItems.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
// добавить персонаж из другой CParty
void CParty::AddPers(CPersonage &man)
{
    people.Add(CPersonage());
    CPersonage &our = people[people.GetSize() - 1];
    our.CopyFrom(man, this);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CParty::RemovePersonage(int nID)
{
    for (int i = 0; i < people.GetSize(); i++)
    {
        if (people[i].nID == nID)
        {
            people.RemoveAt(i);
            return TRUE;
        }
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
CPersonage *CParty::GetPersonage(int nID)
{
    for (int i = 0; i < people.GetSize(); i++)
    {
        if (people[i].nID == nID)
            return &people[i];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CParty::InitPersFromPrototype(CPersonage &man)
{
    int i;
    CStringArray szWears, szSpells;
    CString szWeapon, szWeapon2;
    // determine unit template by its protoype
    CMapObjectManager *pMOManager = static_cast<CMapObjectManager *>(
        theWorld.GetMapObjectManager());
    const SUDBUnitPrototype *pUnitPrototype = pMOManager->GetPrototype(man.szPrototype);
    const SUDBRaceModel *pRaceModel = pMOManager->GetRace(pUnitPrototype->dwRaceIndex);
    man.szTemplate = pRaceModel->szMaskName;
    pMOManager->CreateNewHero(&man.stats, man.szPrototype);

    man.complection.grace = pUnitPrototype->fGrace;
    man.complection.strength = pUnitPrototype->fStrength;
    man.complection.tall = pUnitPrototype->fTall;

    // get unit items from prototype
    if (!((CMapObjectManager *)theWorld.GetMapObjectManager())
             ->GetItemsByPrototype(man.szPrototype, szWears, szWeapon, szWeapon2, szSpells))
        return FALSE;

    for (i = 0; i < szWears.GetSize(); i++)
        man.AddArmor(CSmartPtr<CItemArmor>((CItemArmor *)GetItemManager()->CreateItem(szWears[i])));

    if (!szWeapon.IsEmpty() && szWeapon.CompareNoCase("none") != 0)
        man.AddWeapon(
            CSmartPtr<CItemWeapon>((CItemWeapon *)GetItemManager()->CreateItem(szWeapon)));
    if (!szWeapon2.IsEmpty() && szWeapon2.CompareNoCase("none") != 0)
        man.AddWeapon(
            CSmartPtr<CItemWeapon>((CItemWeapon *)GetItemManager()->CreateItem(szWeapon2)));

    for (i = 0; i < szSpells.GetSize(); i++)
        man.AddSpell(CSmartPtr<CItemSpell>(
            GetItemManager()->CreateItemSpell(szSpells[i], GetSpellManager())));

    man.szVoicePrototype = man.szPrototype;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CParty::InitLMPPers(CPersonage &man)
{
    int i;
    // разденем man`a
    while (man.spells.GetSize())
        man.RemoveSpell(man.spells[0]);
    while (man.weapons.GetSize())
        man.RemoveWeapon(man.weapons[0]);
    const SUDBNPCPrototype *pUP = theUnitDatabase.npcs.GetEntry(man.szPrototype);
    if (!pUP)
        return FALSE;
    for (i = 0; i < pUP->szSpells.size(); i++)
        man.AddSpell(CSmartPtr<CItemSpell>(
            GetItemManager()->CreateItemSpell(pUP->szSpells[i], GetSpellManager())));
    for (i = 0; i < pUP->szQItems.size(); i++)
        man.AddQuick(
            CSmartPtr<CItemQuick>((CItemQuick *)GetItemManager()->CreateItem(pUP->szQItems[i])));
    for (i = 0; i < pUP->szWeapons.size(); i++)
        man.AddWeapon(
            CSmartPtr<CItemWeapon>((CItemWeapon *)GetItemManager()->CreateItem(pUP->szWeapons[i])));
    nMoney += pUP->dwMoney;
    man.stats.AddExperience(pUP->fExpToDistribute);
    man.szVoicePrototype = man.szPrototype;
    // CRAP - this should be used to modify man.stats
    /*	DWORD dwCharacterPoints;
            float fExpirience;
            // primary stats
            float fStrength;
            float fDexterity;
            float fIntelligence;
            // skills & perks
            BYTE cManualSkills[RPG_NUM_MANUAL_SKILLS];
            BYTE cAutoSkills[RPG_NUM_AUTO_SKILLS];
            std::vector<CString> szPerks;*/
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
CPersonage &CParty::CreatePersonage(const char *pszIntName, const char *pszPrototype,
                                    const char *pszName)
{
    if (!pItemManager)
        Create();
    people.Add(CPersonage());
    CPersonage &pers = people[people.GetSize() - 1];
    pers.szInternalName = pszIntName;
    if (pszIntName[0] == 0)
        pers.nID = -1;
    else
        pers.nID = GetPersonageIDByName(pszIntName);
    pers.szPrototype = pszPrototype;
    pers.szName = pszName;
    InitPersFromPrototype(pers);
    return pers;
}
/////////////////////////////////////////////////////////////////////////////////////
void CParty::AutoAvailableZones()
{
    // automatic available of neighbour zones
    CString szVarName;
    SScenarioZone *pZ = theMap.GetZoneByName(szCurrentZone);
    if (pZ && pZ->eType == GM_BETWEENZONES)
    {
        for (int i = 0; i < pZ->exits.GetSize(); i++)
        {
            SScenarioZone *pZN = theMap.GetZoneByName(pZ->exits[i].szZone);
            if (pZN && pZN->eType == GM_REALZONE)
            {
                szVarName.Format("z.%s", pZN->szName);
                if (vars.GetVar(szVarName) == 0)
                    vars.SetVar(szVarName, 1);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CLMPPartySet
/////////////////////////////////////////////////////////////////////////////////////
void CLMPPartySet::SOneParty::Store(const char *pszDir)
{
    // проверить, является ли имя перса строкой типа <номер>.mp
    {
        int nPos = szFileName.ReverseFind('.');
        if (nPos == -1)
        {
            DeleteFile(CString(pszDir) + szFileName);
            return;
        }
        for (int i = 0; i < nPos; ++i)
        {
            if (isdigit(szFileName[i]) == 0)
            {
                DeleteFile(CString(pszDir) + szFileName);
                return;
            }
        }
    }
    // работаем с этим именем дальше - оно правильное...
    MoveFile(CString(pszDir) + szFileName, CString(pszDir) + "temp.mp");
    {
        CFileSave file;
        CMemorySave mem;
        file.OpenWrite(CString(pszDir) + szFileName);
        party.Store(mem);
        CompressTo(file, 0x114, N_CURRENT_SAVE_VERSION, mem);
    }
    DeleteFile(CString(pszDir) + "temp.mp");
}
/////////////////////////////////////////////////////////////////////////////////////
bool CLMPPartySet::SOneParty::Load(const char *pszDir)
{
    CFileSave file;
    CMemorySave mem;
    file.OpenRead(CString(pszDir) + szFileName);
    if (!DecompressTo(mem, 0x114, N_CURRENT_SAVE_VERSION, N_CURRENT_MINIMAL_SAVE_VERSION, file))
        return FALSE;
    party.Load(mem);
    return mem.IsOk();
}
/////////////////////////////////////////////////////////////////////////////////////
void CLMPPartySet::SetCurrent(SOneParty &party)
{
    currentParty = party;
}
/////////////////////////////////////////////////////////////////////////////////////
void CLMPPartySet::Load()
{
    ASSERT(szDir != "");
    parties.clear();
    WIN32_FIND_DATA fd;
    HANDLE h = FindFirstFile(szDir + "\\*.mp", &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            SOneParty p;
            p.szFileName = fd.cFileName;
            if (!p.Load(szDir))
                continue;
            ClearLMPPartyVariables(p.party.vars);
            if (p.party.people.GetSize() == 1)
                parties.push_back(p);
        } while (FindNextFile(h, &fd));
        FindClose(h);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
extern void ClearDirectory(LPCTSTR pszPath);
void CLMPPartySet::Store()
{
    ASSERT(szDir != "");
    if (szDir == "")
        return;
    ClearDirectory(szDir);
    for (int i = 0; i < parties.size(); i++)
        parties[i].Store(szDir);
}
/////////////////////////////////////////////////////////////////////////////////////
