#pragma once
#include "../Navigation/Types.h"
namespace SaveSchema { class Writer; class Reader; }
/////////////////////////////////////////////////////////////////////////////////////
#include "MapObject.h"
#include "Inventory.h"
#include "PacketIDs.h"
#include "aiRoute.h"
#include "aiSensesConst.h"
#include "Spline.h"
#include "udbFormat.h"
#include "ServerConsts.h"
#include "ackFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
// classes pre-declaration
class CPlayer;
class CSpell;
class CItemQuick;
class CItemArmor;
class CItemWeapon;
/////////////////////////////////////////////////////////////////////////////////////
const DWORD QI_MAX_WEAPONS = 4; // max number of quick weapon slots
const DWORD QI_MAX_ITEMS = 4;   // max numner of quick item slots
const DWORD QI_LEGACY_SPELLS = 8; // spell slots stored by save versions up to 116
const DWORD QI_MAX_SPELLS = 16;  // physical storage/wire layout since save 117
const DWORD QI_NPC_SPELLS = 8;   // NPC logical slots; hero UI exposes all 16

const DWORD UNIT_RECALCENEMY = MAKE_FLAG(MAPOBJ_LAST_BIT, 0);
const DWORD UNIT_ALREADY_HOOVER = MAKE_FLAG(MAPOBJ_LAST_BIT, 1);
const DWORD UNIT_RECALC_DETECT = MAKE_FLAG(MAPOBJ_LAST_BIT, 2);
const DWORD UNIT_VISIBLE = MAKE_FLAG(MAPOBJ_LAST_BIT,
                                     3); // видят ли юниты клиента этот юнит (только клиент)
const DWORD UNIT_BLOCKED = MAKE_FLAG(MAPOBJ_LAST_BIT, 4);
const DWORD UNIT_STUN = MAKE_FLAG(MAPOBJ_LAST_BIT, 5);
const DWORD UNIT_LOOKATALLIES = MAKE_FLAG(MAPOBJ_LAST_BIT, 6); //
const DWORD UNIT_DONTMOVE = MAKE_FLAG(
    MAPOBJ_LAST_BIT,
    7); // не вызывать setexactposition на клиенте, на сервере влияет на getmaxattackdistance
const DWORD UNIT_GETFUTUREPOS = MAKE_FLAG(MAPOBJ_LAST_BIT,
                                          8); // GetFuturePos вернет текущую позицию
const DWORD UNIT_OLDROTATE = MAKE_FLAG(MAPOBJ_LAST_BIT,
                                       9); // use OrientInYDirection() for this unit (client)
const DWORD UNIT_FEEBLED = MAKE_FLAG(MAPOBJ_LAST_BIT, 9); // cast is forbidden (server)
const DWORD UNIT_LOOT_LOCKED = MAKE_FLAG(MAPOBJ_LAST_BIT, 10);
const DWORD UNIT_HELMET_REMOVE = MAKE_FLAG(MAPOBJ_LAST_BIT, 11);
const DWORD UNIT_ALREADY_MHOOVER = 0x80000000u; // installed addon murder/steal loot marker

const DWORD UNIT_LAST_BIT = MAKE_LAST_BIT(MAPOBJ_LAST_BIT, 10);
/////////////////////////////////////////////////////////////////////////////////////
const DWORD HLDAMAGE_POTION = 512;
const DWORD HLDAMAGE_BACKSTAB = 1;
const DWORD HLDAMAGE_CRITICAL = 2;
const DWORD HLDAMAGE_ELECTRIC = 4;
const DWORD HLDAMAGE_CRIT = 32;
const DWORD HLDAMAGE_PARRY = 64;
const DWORD HLDAMAGE_BLOCK = 128;
const DWORD HLDAMAGE_CHEST = 256;
const DWORD HLDAMAGE_AVOID = 1024;
const DWORD HLDAMAGE_EXPERIENCE = 8; // not actually a damage, but exp points :)
/////////////////////////////////////////////////////////////////////////////////////
const float FP_SHOW_PATH_SPEED = 0.5f;
/////////////////////////////////////////////////////////////////////////////////////
/*enum EUnitPhase
{
        UF_IDLE = 0,                       // non attacking idle
        UF_MOVE,                           // unit is moving by route
        UF_MOVEIDLE,                       // unit is moving, but at the moment he waits
someone/something UF_ATTACK,                         // unit is attacking UF_CAST, // unit is
casting ( similar to attacking ) UF_SKILL,                          // unit is using some skill (
similar to attacking ) UF_ROTATE,                         // rotate to target UF_SUFFER, // unit is
suffering damage UF_SUFFERMOVE,                     // unit is suffering damage during run/walk
        UF_DYING,                          // unit is dying
        UF_DEAD,                           // unit is dead однако
        UF_PLAYANIM,                       // unit is playing special animation
        UF_STUN,                           // unit is stun someway
        UF_UNDEFINED                       // initialization only
};
*/
/////////////////////////////////////////////////////////////////////////////////////
struct SAttackParams
{
    ERPGWeaponTypes rpgWeaponType; // current weapon type
    ERPGAttackTypes rpgAttackType; // current weapon attack type
    float fWeaponWeight;           // current weapon weight
    float fDamageMin, fDamageMax;  // actual damage range
    float fToHitRandom;
    int nAttack, nDefense; // actual attack/defense
    //
    void ApplyDifficultyLevel(BOOL bHero);
};
/////////////////////////////////////////////////////////////////////////////////////
class CUnitSpecific : public CGameBase
{
protected:
    CUnit *lpUnit;

public:
    CUnitSpecific();
    virtual ~CUnitSpecific();
    //
    virtual BOOL Create();
    virtual void Destroy();
    virtual BOOL FinalDataLoading() { return TRUE; }
    //
    void RegisterUnit(CUnit *lpNewUnit) { lpUnit = lpNewUnit; }
    // commands
    virtual void NewAnimationWasReceived(float fSegmentTimeCounter) {}
    virtual void PoseChanged() {}
    // вызывается в случае передвижения юнита
    virtual CAIFullRoute *GetRoute() = 0;
    // hook on update inventory
    virtual void InventoryUpdated() = 0;
    // hook on set owner
    virtual void SetOwner(CPlayer *lpNewOwner) {}
    // RPG system functions
    virtual BOOL RecalcRPGStats(bool bStrengthChanged = false) { return FALSE; }
    virtual BOOL ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion, CUnit *pAttacker,
                                      CUnit *pExpDest)
    {
        return FALSE;
    };
    virtual BOOL Heal(float fpPower) { return FALSE; }
    virtual void ExportStats() {}
    virtual void RecalcAIClass() {}
    virtual int GetHeroLevel() const = 0;
    //
    virtual BOOL ChangeWeapon(DWORD dwToWeapon) = 0;
    virtual bool Wear(DWORD dwArmor) = 0;
    virtual bool Remove(ERPGArmorTypes type) = 0;
    virtual void RemoveAllArmors() {}
    virtual BOOL NotifyItemsUpdated() { return FALSE; }
    virtual BOOL NotifyWeaponUpdated() { return FALSE; }
    virtual BOOL NotifyHitPointsUpdated() { return FALSE; }
    virtual void SetArmors(DWORD *pArmors, DWORD dwNumArmors) {}
    virtual void SetWeapons(DWORD *pWeapons, DWORD dwNumWeapons) {}
    virtual bool AddWeapon(DWORD dwWeapon) { return false; }
    virtual bool RemoveWeapon(DWORD dwWeapon) { return false; }
    //
    virtual bool HaveLowShapes() const = 0;
    virtual bool HaveHighShapes() const = 0;
    //
    virtual void RemovePendingDiplomacy() {}
    //
    virtual ETameLevel GetCurrentTameLevel() const { return UTL_NONE; }
    // Segment
    virtual void SetExactPosition(float fpTime) {}
    virtual void NewRouteReceived() {}
    virtual void RemoveReferencesOn(CMapObject *pTarget) {}
    virtual void StartFadeOut() {};
    // call back on unit changing state
    virtual void ComplectionChanged() {}
    virtual void RecalcAIParams() {}
    virtual void HLDamaged(ERPGHitLocations hl, float fpDamage, BYTE cFlags) {}
    virtual void ElectricDamage(float fpStrength) {}
    virtual void Dead() {}
    virtual void ChangeSpeedWithSpell() {}
    //
    virtual void GetAttackParams(SAttackParams &ap) const = 0;
    // acknowledgements
    virtual void AddAck(const SAckEntry *pAck, DWORD dwType) {}
    virtual void ClearAcks() {}

    //
    virtual void TimeOfDayChanged() {}
    //
    CUnit *GetUnit() { return lpUnit; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
inline CUnitSpecific::CUnitSpecific()
{
    lpUnit = NULL;
}
inline CUnitSpecific::~CUnitSpecific()
{
    RegisterUnit(NULL);
}
inline BOOL CUnitSpecific::Create()
{
    lpUnit = NULL;
    return TRUE;
}
inline void CUnitSpecific::Destroy()
{
    RegisterUnit(NULL);
}
/////////////////////////////////////////////////////////////////////////////////////
class CUnit : public CMapObject
{
public:
    Navigation::Location navLocation;
private:
    struct SItemIDs
    {
        DWORD dwWeaponsID[QI_MAX_WEAPONS];
        DWORD dwArmorsID[RPG_NUM_ARMOR_TYPES];
    };
    friend class CUnitSpecific;
    friend class CUnitClientSpecific;
    friend class CUnitServer;
    friend class NavigationWorld;
    friend struct ModNpcMigrationAccess;
    friend struct OwnedPreviewSourceAccess;

protected:
    // just a name :)
    CString szName;         // unit's name
    DWORD dwVoicePrototype; // индекс прототипа, из которого берётся голос
    // specific
    CUnitSpecific *lpUnitSpecific; // unit specific object (client/server)
    // diplomacy (and relative) information
    DWORD dwEnemyMask;       // маска, показывающая враждебных плееров для этого юнита
    DWORD dwPossibleEnemies; // маска, показывающая возможных враждебных плееров для этого юнита
    CPlayer
        *lpOwner; // pointer to player that own this unit or NULL if no info about player available
    int nPlayerNum; // number of player to handle diplomacy
    // Lowest level of commands execution - animations and movement description
    BYTE nStartAnimation,
        nCycleAnimation;  // startAnimation should not by cycle, 0xff - no animation
    DWORD fullStartAnimation, fullCycleAnimation; // companion IDs; not serialized here
    bool bExtendedAnimationId;
    BYTE nAnimationSound; // set of sounds used for the animation (0=default)
    int nAnimationStartSegment;
    EUnitAnimationSpeed animationSpeed;   // determines speed of animation
    EUnitLocomotionSpeed locomotionSpeed; // if locomotion frozen or not
    // unit state information
    EUnitPose currentPose, wishPose;     // pose of unit and pose to which unit wants to go
    DWORD dwCurrentWeapon, dwWishWeapon; // weapon unit holding and one it wants to hold
    BOOL bDead, bMoving;                 // some state as flags
    // unit's movement information
    CRouteWalker walker; // route walker - unit's movement engine
    float fpBaseZ;       // base coordinate over terrain
    int nAIClass;        // ai class cache variable
    float fpAttackDistance, fpCollisionDistance;
    // RPG information
    SRPGUserUnitStats rpgUserUnitStats; // user unit's RPG stats
    EUnitAggression aggression;         // combat mode
    // temp data
    BOOL bNeedFastPath; // this flag is used in aiMap during pathfinding
private:
    // quick stuff, BE CAREFUL WHEN CHANGE NUMBER OF ITEMS - CHANGE EVERYWHERE
    CItemWeapon *lpQWeapons[QI_MAX_WEAPONS]; // quick weapons
    CItemQuick *lpQItems[QI_MAX_ITEMS];      // quick items
    CItemSpell *lpQSpells[QI_MAX_SPELLS];    // quick spells
    // unit's equipment (armors)
    CItemArmor *lpArmors[RPG_NUM_ARMOR_TYPES];     // weared armors (by slots)
    CArray<CItemQuest *, CItemQuest *> questItems; // questItems unit got
    // animation coeffs
    float fAnimationCoeffs[6];
    //
    void ConvertItemsIDs(SItemIDs &res);
    virtual void ChangeComplectionWithSpell();
    virtual void ChangeSpeedWithSpell();

public:
    // BOOL bAggressive
    SUnitVisibilityInfo sensesDetection;

public:
    CUnit();
    virtual ~CUnit();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    void Clear();
    virtual void FreeReferences();
    virtual bool IsDynamic() { return true; }
    virtual BOOL FinalDataLoading();
    // specific data
    virtual void SetSpecific(LPGAMEBASE lpNewSpecific);
    void SetUnitSpecific(LPGAMEBASE pNewUnitSpecific);
    CUnitSpecific *GetUnitSpecific() { return lpUnitSpecific; }
    // animation coeffs functions
    void SetRaceAnimationCoeffs(const float *fNewAnimationCoeffs)
    {
        memcpy(fAnimationCoeffs, fNewAnimationCoeffs, sizeof(fAnimationCoeffs));
    }
    float GetAnimationCoeff(const SADEntry *pADEntry);
    // LOWEST LEVEL interface
    //  animation & route functions
    void LowNewRoute();
    void LowStartAnimation(BYTE start, BYTE cycle, int nStartSegment, BYTE nSoundSet);
    void LowStartAnimationExtended(DWORD start, DWORD cycle, int nStartSegment, BYTE nSoundSet);
    void RestoreExtendedAnimationIds(DWORD start, DWORD cycle);
    DWORD GetStartAnimationId() const { return fullStartAnimation; }
    DWORD GetCycleAnimationId() const { return fullCycleAnimation; }
    bool HasExtendedAnimationId() const { return bExtendedAnimationId; }
    BYTE GetAnimationStartByte() const { return nStartAnimation; }
    BYTE GetAnimationCycleByte() const { return nCycleAnimation; }
    void LowAnimationSpeed(EUnitAnimationSpeed speed);
    void LowLocomotionSpeed(EUnitLocomotionSpeed speed);
    void LowSetPoses(EUnitPose cur, EUnitPose wish);
    void
    LowSetRouteSpeeds(float fpMove,
                      float fpRotate); // initializes walker with specified speeds (for showpath)

    // get type of the current weapon. NOTE: valid only on the client. because require
    // rpgUserUnitStats
    ERPGWeaponTypes GetCurrentWeaponType() const;
    // check for current weapon bow or crossbow
    bool IsCurrentWeaponABow() const
    {
        const CItemWeapon *pWeapon = GetCurrentQWeapon();
        if (pWeapon)
            return (pWeapon->GetWeaponType() == RPGWST_BOW) ||
                   (pWeapon->GetWeaponType() == RPGWST_CROSSBOW);
        return false;
    }
    // RPG system processing
    void SetupCampUnitWithUnitStats(SRPGUnitStats *pUnitStats);
    const SRPGUserUnitStats *GetUserUnitStats() const { return &rpgUserUnitStats; }
    const SUDBUnitPrototype *GetPrototype() const;
    const SUDBNPCPrototype *GetNPCPrototype() const;
    const SUDBRaceModel *GetRaceModel() const;
    const SUDBUnitPrototype *GetVoicePrototype() const;
    void SetVoicePrototype(const SUDBUnitPrototype *pPrototype);
    void SetVoicePrototype(const char *pszPrototype);
    void SetVoicePrototype(DWORD dwIndex);
    DWORD GetVoicePrototypeIdx() const { return dwVoicePrototype; }
    virtual void SetRPGStats(SRPGUnitStats *lpRPGNewUnitStats);
    virtual void RecalcAIClass();
    virtual BOOL ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion, CUnit *pAttacker,
                                      CUnit *pExpDest)
    {
        return lpUnitSpecific->ProcessAttackPortion(lpAttackPortion, pAttacker, pExpDest);
    }
    virtual BOOL ExportStats();
    virtual BOOL RecalcRPGStats(bool bStrengthChanged = false);
    virtual void LoadDefaultClientStats(const char *pszTemplate);
    void GetAttackParams(SAttackParams &ap) const { lpUnitSpecific->GetAttackParams(ap); }
    EUnitAggression GetAggression() const { return aggression; }
    void SetAggression(EUnitAggression _aggression);
    int GetLevel() const;
    virtual void RestoreComplection();
    //
    float GetHeightComplectionScale() const;
    //
    bool IsBackstab(CMapObject *pTarget) const;
    float GetDefense(CUnit *pUnit, bool bBackstab = false) const;
    float GetToHit(CMapObject *pMO, bool bBackstab = false, bool bCalcDistance = true) const;
    float GetToHitProbability(CMapObject *pTarget, ERPGHitLocations hl = RPGHL_ALL) const;
    // armor
    CItemArmor *Wear(CItemArmor *lpArmor);
    CItemArmor *GetQArmor(DWORD dwSlotIndex);
    CItemArmor *Remove(ERPGArmorTypes type);
    void RemoveCrippledItems();
    // weapon
    DWORD GetCurrentWeaponIndex() { return dwWishWeapon; }
    // instead of this function use playerclient::changeweapon
    void WantWeapon(DWORD dwQWeaponIndex) { dwWishWeapon = dwQWeaponIndex; }
    BOOL ChangeWeapon(DWORD dwQWeaponIndex) { return lpUnitSpecific->ChangeWeapon(dwQWeaponIndex); }
    int AddWeapon(CItemWeapon *lpWeapon);
    void RemoveWeapon(DWORD dwQWeaponIndex);
    CItemWeapon *GetQWeapon(DWORD dwQWeaponIndex) const;
    CItemWeapon *GetStoredQWeapon(DWORD index) const { return index < QI_MAX_WEAPONS ? lpQWeapons[index] : nullptr; }
    CItemWeapon *GetCurrentQWeapon() const { return GetQWeapon(dwCurrentWeapon); }
    DWORD GetCurrentWeaponSlotIndex() const { return dwCurrentWeapon; }
    void ConvertWeaponsOnClient();
    // items
    int AddQItem(CItemQuick *lpQItem);
    void RemoveQItem(DWORD dwQItemIndex);
    int AddCampQItem(CItemQuick *item);
    void RemoveCampQItem(int index);
    CItemQuick *GetQItem(DWORD dwQItemIndex);
    CItemQuick *GetStoredQItem(DWORD index) const { return index < QI_MAX_ITEMS ? lpQItems[index] : nullptr; }
    CItem *RemoveRandomItem(); // don`t release resulting item, for stealing purposes, quick or
                               // quest, quest have priority
    BOOL HasQuickSlot();
    void ShowHelmet(bool bShow);
    void RefreshPartyVisualArmor();
    // spells
    DWORD GetSpellSlotCount() const;
    int AddQSpell(CItemSpell *lpSpell);
    CItemSpell *RemoveQSpell(DWORD dwQSpellIndex);
    CItemSpell *GetQSpellItem(DWORD dwQSpellIndex);
    CSpell *GetQSpell(DWORD dwQSpellIndex);
    CPlot3D GetCastPointPosition();
    CPlot3D GetWeaponCastPoint(const CItemWeapon*,bool left);
    // quest items
    void AddQuestItem(CItemQuest *pItem);
    int GetQuestItemsNum() const { return questItems.GetSize(); }
    CItemQuest *GetQuestItem(int i) { return questItems[i]; }
    void RemoveQuestItem(int i);
    //
    void GetAllItemsList(CGameBaseList &items, CGameBaseList &spells);
    void RemoveAllItems();
    EUnitCastFailure CheckIsCastPossible(DWORD dwQSpellIndex, CPlot3D &ptTarget);
    EUnitCastFailure CheckIsWandPossible(DWORD dwQItemIndex, CPlot3D &ptTarget);
    //
    float GetItemsWeight() const;
    float GetItemsWeight(DWORD spellSlots) const;
    //
    virtual void NotifyHitPointsUpdated() { lpUnitSpecific->NotifyHitPointsUpdated(); }
    // player functions
    void SetOwner(CPlayer *lpNewOwner);
    CPlayer *GetOwner() const { return lpOwner; }
    int GetOwnerNum() const { return nPlayerNum; }
    //
    BOOL IsHero() const;
    // return TRUE if pUnit is enemy of this[0]
    BOOL IsEnemy(CUnit *pUnit);
    BOOL IsEnemy(int nPlayer) { return (dwEnemyMask & dwPossibleEnemies & (1 << nPlayer)) != 0; }
    void SetPlayerNum(int nNewPlayer);
    int GetPlayerNum() { return nPlayerNum; }
    void AddEnemyMask(DWORD dwNewEnemies);
    void SetEnemyMask(DWORD dwNewEnemies);
    DWORD GetEnemyMask() { return dwEnemyMask; }
    void AddEnemy(int nPlayer) { AddEnemyMask(dwEnemyMask | (1 << nPlayer)); }
    void SetPossibleEnemiesMask(DWORD dwPosEnemies);
    DWORD GetPossibleEnemiesMask() const { return dwPossibleEnemies; }
    // calc position of unit head for looking
    float GetUnitHeadHeight() const;
    float GetStandUnitHeadHeight() const;
    virtual CPlot3D GetUnitHeadPosition(); // works correctly only on server
    // значения рассчитываются по параметрам bound volumes, используется body part bd
    // для корректной работы bv & fbvs should be loaded
    float GetUnitRadius();
    float GetUnitHeight();
    bool CanBeDamagedByAreaSpell() { return GetAIClass() != 0 || GetPrototype()->fAltitude < 0.5f; }
    // name functins
    virtual LPCSTR GetName() const { return szName; }
    virtual void SetName(LPCSTR pszNewName) { szName = pszNewName; }
    // Update
    virtual void UpdateTo();
    virtual BOOL UpdateFrom(EUpdateType tip);
    virtual void NewVerticalPos(float fpZ);
    // Segment
    virtual void SetExactPosition(float fpTime);
    virtual int Segment();
    virtual void RemoveReferencesOn(CMapObject *pTarget)
    {
        lpUnitSpecific->RemoveReferencesOn(pTarget);
    }
    virtual void TimeOfDayChanged() { lpUnitSpecific->TimeOfDayChanged(); }
    virtual void SetStunFlag(BOOL bStun);
    // position prediction, server only
    virtual CPlot3D GetFuturePosition(int nSegment);
    bool GetRoutePosition(float time,CPlot3D &position);
    virtual CPlot3D GetTargetCP();

    // рассчитать высоту юнита над землей в точке (x,y)
    float CalcZ(float fpX, float fpY);

    // get state
    virtual BOOL IsDead() const { return bDead; }     // phase == UF_DEAD || phase == UF_DYING; }
    virtual BOOL IsMoving() const { return bMoving; } // phase == UF_MOVE; }
    // BOOL IsAttacking() const { return ?????????????;}//phase == UF_ATTACK || phase == UF_CAST ||
    // phase == UF_SKILL; };
    BOOL IsCrawling() const { return currentPose == UP_CRAWL; }
    BOOL IsLaying() const { return currentPose == UP_LAY; }
    BOOL IsStanding() const { return currentPose == UP_STAND; }
    BOOL IsRunning() const { return currentPose == UP_RUN; }
    // BOOL WishRun() const { return (bWishRun || bAggressive); }
    EUnitPose GetShowPose() const { return wishPose; }
    EUnitPose GetCurrentPose() const { return currentPose; }
    void SetWishPose(EUnitPose pose) { wishPose = pose; } // use from PlayerClient only
    // EUnitPhase GetPhase() const { return phase; }
    // call back on phase change
    // Get unit parameters
    void RecalcAIParams();
    virtual float GetAttackDistance() { return fpAttackDistance; }
    float GetCollisionDistance() { return fpCollisionDistance; }
    virtual float GetSpeed()
    {
        float m, r;
        GetSpeeds(m, r);
        return m;
    }
    void GetSpeeds(float &fpMoveSpeed,
                   float &fpRotateSpeed); // move speed in ai tiles per segment, radians per segment
    float GetShowSpeed();
    float GetShowSpeed(EUnitPose pose);
    int GetAIClass() { return nAIClass; }
    BOOL NeedFastPath() { return bNeedFastPath; }
    float GetMaxAttackPathLength(float fpToTarget);
    //
    virtual bool CanPlayAnimation(const SADEntry *pEntry) const;
    //
    LPCSTR GetSFXPath() const;
    int GetNumSFXHits() const;
    int GetNumSFXAttacks() const;
    int GetNumSFXDeathes() const;
    // load&store
    virtual void Load(CSave &file) = delete;
    virtual void Store(CSave &file) = delete;
    // Animation entry
    bool HasRestAnimations() const;
    const SADEntry *GetAnimation(DWORD dwAnimationFlags, bool bWeapon = true,
                                bool reportMissing = true);
    const SADEntry *GetAnimationByIndex(DWORD nIndex);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CUnit"; };
    virtual EClassIDs WhoAmI() const { return HPC_UNIT; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
typedef CUnit *LPUNIT;
