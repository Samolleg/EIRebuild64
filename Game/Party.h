#pragma once
namespace SaveSchema { class Writer; class Reader; }

#include "World.h"
#include "rpgSystem.h"
#include "SaveLoader.h"
#include "GSVars.h"
#include "RPGSystem.h"
#include "Object.h"
#include <map>
#include <string>
#include "ServerConsts.h"

/////////////////////////////////////////////////////////////////////////////////////
class CItem;
class CItemWeapon;
class CItemArmor;
class CItemQuick;
class CItemSpell;

class CSpellConstructor;
/////////////////////////////////////////////////////////////////////////////////////
class CPersonage
{
public:
    CString szName;         // имя в локальной кодировке, "" если имя стандартное
    CString szInternalName; // внутреннее имя

    CString szTemplate;
    CString szPrimTexture;
    CString szSecondTexture;
    CString szPrototype;
    CString szVoicePrototype;

    // unit ID, вычисляется из нелокализованного(главного) имени персонажа, под этим ID
    // этот персонаж деплоится на любую зону
    int nID;
    SRPGUnitStats stats;
    SComplection complection;

    CSimplyArray<CItemWeapon *, CItemWeapon *> weapons;
    CSimplyArray<CItemQuick *, CItemQuick *> items;
    CSimplyArray<CItemArmor *, CItemArmor *> armors;
    CSimplyArray<CItemSpell *, CItemSpell *> spells;

    EUnitPose pose;
    DWORD dwCurrentWeapon;
    bool bIsSelected;
    int nFollowToID;
    EUnitAggression aggression;

public:
    CPersonage();
    ~CPersonage();
    void Clear();
    void ClearItems();
    void CopyFrom(CPersonage &src, CWorld *pMyWorld);
    void CopyItemsFrom(CPersonage &src, CWorld *pMyWorld);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
    // функциям add/remove в качестве аргументов передаются items принадлежащие itemmanager`y
    // CParty этого персонажа, при передаче в Add застеканого itema с него снимается копия, иначе
    // добавляется сам item, если функция AddXXX возвращает ее аргумент, значит item был перенесен,
    // иначе item был застекан и в personage была помещена его копия аргументом функций RemoveXXX
    // должны быть item`a из weapons[] etc этого персонажа НЕ ДОБАВЛЯТЬ ITEMS ИЗ ДРУГОГО CWORLD!!!
    CItemWeapon *AddWeapon(CItemWeapon *pItem);
    void RemoveWeapon(CItemWeapon *pItem);
    CItemQuick *AddQuick(CItemQuick *pItem);
    void RemoveQuick(CItemQuick *pItem);
    CItemArmor *AddArmor(CItemArmor *pItem);
    void RemoveArmor(CItemArmor *pItem);
    CItemSpell *AddSpell(CItemSpell *pItem);
    void RemoveSpell(CItemSpell *pItem);
    //
    float GetItemsWeight() const;
public:
    void Store(SaveSchema::Writer &) const;
    void Load(SaveSchema::Reader &);
    void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
// typedef CArray< CPersonage*, CPersonage* > CPersonages;
/////////////////////////////////////////////////////////////////////////////////////
// Ownership exchange used only after a complete staged slot has been prepared.
template<class T, class Base = CArray<T>> class CPartySaveArray : public Base {
public:
    void Swap(CPartySaveArray &other) noexcept {
        std::swap(this->m_pData,other.m_pData);std::swap(this->m_nSize,other.m_nSize);
        std::swap(this->m_nMaxSize,other.m_nMaxSize);std::swap(this->m_nGrowBy,other.m_nGrowBy);
    }
};
class CParty : public CWorld
{
    CItemManager *pItemManager;
    CItemConstructor *pItemConstructor; // item constructor
    CSpellManager *pSpellManager;       // local spell manager
    CSpellConstructor
        *pSpellConstructor; // local spell constructor (spell manager use t to create spells)
public:
    CPartySaveArray<CPersonage> people;
    CPartySaveArray<CItem *, CSimplyArray<CItem *, CItem *>> allWagonItems;
    int nMoney; // типа общие деньги
    CGSVariableSet vars;
    // Campaign-wide qualified local IDs, including absent and dead actors.
    std::map<std::string, int> settlementIds;
    CString szCurrentZone; // имя текущей зоны
    int nEntrance;         // номер входа, через который группа должна попасть на зону

    CParty();
    virtual ~CParty();
    CParty &operator=(const CParty &src);
    CParty(const CParty &src);
    // create/destroy
    virtual BOOL Create();
    BOOL CreateSavedOwners(ObjectId item, ObjectId spell);
    void SwapSavedState(CParty &) noexcept;
private:
    BOOL CreateOwners(ObjectId item, ObjectId spell);
public:
    virtual void Destroy();
    void Clear(BOOL bDelVars, BOOL bDestructor);
    void Clear() { Clear(TRUE, FALSE); }

    virtual CSpellManager *GetSpellManager() { return pSpellManager; }
    virtual CItemManager *GetItemManager() { return pItemManager; }
    virtual CBaseManager *GetEffectManager() { return NULL; }
    virtual CItemConstructor *GetItemConstructor() { return pItemConstructor; }
    virtual CSpellConstructor *GetSpellConstructor() { return pSpellConstructor; }
    // store/load whole object
    void Serialize(CStructureSaver &file);
    void Load(CSave &file, BOOL bVars = TRUE);
    void Store(CSave &file, BOOL bVars = TRUE);
    // store/load people/items/wagon
    void LoadItems(CSave &file) { Load(file, FALSE); }
    void StoreItems(CSave &file) { Store(file, FALSE); }
    // people management
    // allows to copy personage from this or other party
    void AddPers(CPersonage &man);
    // remove personage with specified ID
    BOOL RemovePersonage(int nID);
    // this function gives items to person as specified in template, use CreatePersonage()
    BOOL InitPersFromPrototype(CPersonage &man);
    /// <summary>
    /// Создаёт персонажа
    /// </summary>
    /// <param name="pszIntName">Внутреннее имя персонажа латинскими буквами, ID персонажа
    /// рассчитывается по этому имени. При использовании пустого pszIntName этому персонажу
    /// присваивается nID = -1. Использование идентификатора -1 не позволит узнать под каким nID
    // данный объект находится на клиенте/сервере!</param>
    /// <param name="pszPrototype">Имя прототипа из Датабазы</param>
    /// <param name="pszName">Иное имя, "" если стандартное</param>
    /// <returns>the reference returned by this function will be valid until some personage is
    /// added/created/removed</returns>
    CPersonage &CreatePersonage(const char *pszIntName, const char *pszPrototype,
                                const char *pszName);
    CPersonage *GetPersonage(int nID);
    // wagon management
    // return index of item in allWagonItems, -1 if not found
    int GetWagonItemIdx(CItem *pItem);
    // return item to which pItem was stacked or pItem if no stacking occured
    // NOTE: in few cases we don't need to stack item even if the same item is already there (
    // constructors ) MOTE: then bNeedStack must be false
    CItem *AddItemToWagon(CItem *pItem, bool bMoveAllStack = false, bool bNeedStack = true);
    // return item whose count decreased after pItem removal or NULL if item was removed itself
    CItem *RemoveItemFromWagon(CItem *pItem, bool bMoveAllStack = false);
    void RemoveAllItems();
    void RemoveAllQuestItems();
    //
    void AutoAvailableZones();
    //
    bool VisitedZone(const char *pszZone) { return vars.GetVar(CString("zs.") + pszZone) != 0; }
    void SetVisitedZone(const char *pszZone) { vars.SetVar(CString("zs.") + pszZone, 1); }
    void ResetVisitedZone(const char *pszZone) { vars.SetVar(CString("zs.") + pszZone, 0); }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CParty"; };
    virtual EClassIDs WhoAmI() const { return HPC_PARTY; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
// поддерживает считывание и запись в отдельные файлы
