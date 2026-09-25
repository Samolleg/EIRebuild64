#pragma once

#include "Object.h"
#include "RPGSystem.h"
#include "SpellEffect.h"
#include "SaveLoader.h"
#include "NetUpdate.h"
// --------------------------------------------------------------------------------------------
const DWORD MAPOBJ_MOVED = MAKE_FLAG(OBJ_LAST_BIT, 0);
const DWORD MAPOBJ_TRANSPARENT = MAKE_FLAG(OBJ_LAST_BIT, 1);
const DWORD MAPOBJ_DYNAMIC = MAKE_FLAG(OBJ_LAST_BIT, 2);
const DWORD MAPOBJ_STUNED = MAKE_FLAG(OBJ_LAST_BIT, 3);

const DWORD MAPOBJ_LAST_BIT = MAKE_LAST_BIT(OBJ_LAST_BIT, 3);
// const DWORD MAPOBJ_MOVED  = 0x1000;

// --------------------------------------------------------------------------------------------
// quest states
// --------------------------------------------------------------------------------------------
enum EQuestState
{
    QS_NOT_AVAILABLE = 0,
    QS_ACTIVE = 1,
    QS_COMPLETED = 2,
};
/////////////////////////////////////////////////////////////////////////////////////
class CMapObject;
class CUnit;
class CMapObjectSpecific : public CGameBase
{
protected:
    CMapObject *lpMapObject;

public:
    CMapObjectSpecific();
    virtual ~CMapObjectSpecific();
    //
    virtual BOOL Create();
    virtual void Destroy();
    //
    virtual void UpdateSpellEffectVisualization(bool bStart) {}
    //
    virtual int Segment() { return 0; }
    virtual void LeaveMap() {}
    //
    virtual BOOL ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion, CUnit *pAttacker)
    {
        return FALSE;
    }
    //
    virtual void ReceiveDamage(int nAmount, DWORD dwFlags) {}
    //
    virtual BOOL IsDead() const = 0;
    virtual BOOL IsCorpse() const = 0;
    //
    virtual void SetQuestState(EQuestState qs) {}
    //
    void RegisterMapObject(CMapObject *lpNewMapObject) { lpMapObject = lpNewMapObject; }
    CMapObject *GetMapObject() { return lpMapObject; };
};
/////////////////////////////////////////////////////////////////////////////////////
inline CMapObjectSpecific::CMapObjectSpecific()
{
    lpMapObject = NULL;
}
inline CMapObjectSpecific::~CMapObjectSpecific()
{
    RegisterMapObject(NULL);
}
inline BOOL CMapObjectSpecific::Create()
{
    lpMapObject = NULL;
    return TRUE;
}
inline void CMapObjectSpecific::Destroy()
{
    RegisterMapObject(NULL);
}
/////////////////////////////////////////////////////////////////////////////////////
class CMapObject : public CObject3D
{
    friend class CMapObjectClientSpecific;
    friend class CMapObjectServer;
    friend class CMapObjectSpecific;

protected:
    CMapObjectSpecific *lpMapObjectSpecific; // map object specific data
    int nActiveObjectCounter;                // счетчик для определения активности объекта
    float fpAnimationCoeff;                  // animation speed coeff (default = 1)
    // RPG system: common stats
    SRPGUserCommonStats rpgUserCommonStats; // user common RPG stats
    // attached spells
    CSpellEffectArray spellEffects; // spell effects, attached to this mapobject
    CString szAttachedQuest;        // name of attached quest in form like q.asdf.r3
    //
    SComplection baseComplection;

protected:
    void RemoveAllSpellEffects();
    virtual void ChangeComplectionWithSpell();
    virtual void ChangeSpeedWithSpell() {}

public:
    // for AI tile Map, changed by AIMap->Add/Change/Remove MapObject
    int nTileMapX; // coordinate X on tile map
    int nTileMapY; // coordinate Y on tile map
    // masks for wValidMask
    enum
    {
        VLD_MASK_STATS = 0,       // common stats
        VLD_MASK_COMPLECTION = 1, // comlpection ( if it differs from default )
        VLD_MASK_POSROT = 2,      // position and rotation
        VLD_MASK_SPELLEFFECTS = 3,
        VLD_MASK_MAPOBJMAX = 3,
        // torch`s part
        VLD_MASK_TORCH = 4,
        VLD_MASK_TORCHMAX = 4,
        // lever`s part
        VLD_MASK_LEVER = 5,
        VLD_MASK_LEVERMAX = 5,
        // unit's part
        VLD_MASK_UNIT_STATS = 4,  // common stats
        VLD_MASK_INVENTORY = 5,   // inventory: quick weapons, items, spells; armor
        VLD_MASK_POSE = 6,        // current pose, current weapon
        VLD_MASK_FFLAGS = 7,      // animation speed, movement speed, dead flag
        VLD_MASK_ANIM = 8,        // animations start time & animations itself
        VLD_MASK_NAME = 9,        // szName
        VLD_MASK_ROUTE = 10,      // route
        VLD_MASK_DIPLOMACY = 11,  // diplomacy or owner for CUnit
        VLD_MASK_DETECTIONS = 12, // units probabilities of be detected
        VLD_MASK_AGGRESSION = 13, // units aggression
        VLD_MASK_UNITMAX = 13,
    };

public:
    int nUpdatedSegment;    // number of segment when object was last updated
    CUpdateMultiple update; // network updates information
public:
    CMapObject();
    virtual ~CMapObject();
    // create/destroy
    virtual void FreeReferences();
    virtual BOOL Create();
    virtual void Destroy();
    virtual bool IsDynamic() { return CheckFlag(MAPOBJ_DYNAMIC) != 0; }
    //
    void SetBaseComplection(const SComplection &comp) { baseComplection = comp; }
    const SComplection &GetBaseComplection() const { return baseComplection; }
    virtual void RestoreComplection() { complection = baseComplection; }
    // RPG system processing
    virtual BOOL ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion, CUnit *pAttacker,
                                      CUnit *pExpDest)
    {
        return lpMapObjectSpecific->ProcessAttackPortion(lpAttackPortion, pAttacker);
    }
    virtual BOOL ExportStats();
    virtual BOOL RecalcRPGStats(bool bStrengthChanged = false);
    virtual void LoadDefaultClientStats(const char *pszTemplate);
    virtual void SetRPGStats(SRPGCommonStats *lpRPGNewCommonStats);
    virtual void SetRPGStats(SRPGUnitStats *lpRPGNewUnitStats) {}
    virtual void RecalcAIClass() {}
    void ClearRPGAbsorbtions();
    // can science be applied on this object ? and what key number is required for that
    virtual BOOL IsScientific();
    virtual BOOL IsScientificDoor();
    virtual DWORD GetScienceKeyNumber();
    virtual bool ApplyScience(const SRPGScienceStats &tool, CUnit *pWho);
    virtual BOOL CanApplyScience(const SRPGScienceStats &tool);
    virtual bool NeedGraphRecalc() { return true; }
    virtual void GetUsePoints(CArray<CPlot3D, CPlot3D &> &points, float fpUnitRadius);
    // spell-effects-in-mapobject management
    virtual void AddSpellEffect(CSpellEffect &effect);
    bool RefreshWeaponStatus(CSpellEffect &effect);
    const CSpellEffectArray &GetSpellEffects() const { return spellEffects; }
    virtual void SetStunFlag(BOOL bStun) {}
    BOOL HasSpellEffect(int nSpellID) const;
    float GetEffectValue(int nSpellID) const;
    // attached quest
    const char *GetAttachedQuest() const { return szAttachedQuest; }
    void SetAttachedQuest(const char *pszQuest);
    virtual void SetQuestState(EQuestState qs) { lpMapObjectSpecific->SetQuestState(qs); }
    void SetQuestVarValue(float fpVal);
    //
    virtual CPlot3D GetUnitHeadPosition();
    // Update
    void SetUpdateMask(int nBit = -1);
    void UpdateInit();
    void UpdateFrame();
    BOOL UpdateFrom(EUpdateType tip, CPacket &pkt);
    virtual void UpdateTo();
    virtual BOOL UpdateFrom(EUpdateType tip);
    virtual void NewVerticalPos(float fpZ);
    // определить положение юнита в момент nSegment
    virtual CPlot3D GetFuturePosition(int nSegment) { return GetCP(); }
    virtual CPlot3D GetTargetCP() { return GetCP(); }
    virtual BOOL IsMoving() const { return FALSE; }
    virtual BOOL IsDead() const { return lpMapObjectSpecific->IsDead(); }
    virtual bool IsInvisible() const;
    bool IsStuned() const;
    // Segment
    virtual int Segment();
    bool CountActivity() { return --nActiveObjectCounter > 0; }
    bool TickInactiveSpellEffects();
    void SetActivityCounter(int nValue) { nActiveObjectCounter = nValue; }
    virtual void RemoveReferencesOn(CMapObject *pTarget) {}
    virtual void TimeOfDayChanged() {}
    // work with specific
    virtual void SetSpecific(LPGAMEBASE lpNewSpecific);
    void SetMapObjectSpecific(LPGAMEBASE lpNewSpecific);
    CMapObjectSpecific *GetMapObjectSpecific() { return lpMapObjectSpecific; }
    // RPG system stats access
    int GetHP() const { return rpgUserCommonStats.nHP; }
    int GetMaxHP() const { return rpgUserCommonStats.nMaxHP; }
    int GetMP() const { return rpgUserCommonStats.wMP; }
    int GetMaxMP() const { return rpgUserCommonStats.wMaxMP; }
    const SRPGUserCommonStats &GetUserCommonStats() const { return rpgUserCommonStats; }
    virtual void NotifyHitPointsUpdated() {}
    // get speed in tiles/segment
    // virtual float GetHeight() { return 1.0f; }  // Get unit's Height
    virtual float GetSpeed() { return 0; }
    virtual float GetAttackDistance() { return 0.75f; }
    //
    void SetAnimationCoeff(float fpNewCoeff) { fpAnimationCoeff = fpNewCoeff; }
    // functions for work with map
    BOOL LeaveMap();
    BOOL ReturnOnMap();
    // load & store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
typedef CSmartPtr<CMapObject> CMapObjectPtr1;

class CUnit;
// --------------------------------------------------------------------------------------------
// special class, containing pointer to mapobject and maintaining its correctness with inc/dec
// refcount
// --------------------------------------------------------------------------------------------
class CMapObjectPtr
{
    typedef CMapObject *PMO;
    CMapObject *pObject;

public:
    void Clear()
    {
        if (pObject)
            pObject->Release();
        pObject = 0;
    }
    void Set(PMO pMO)
    {
        if (pMO == pObject)
            return;
        Clear();
        if (pMO)
            pMO->AddRef();
        pObject = pMO;
    }
    CMapObjectPtr() { pObject = NULL; }
    CMapObjectPtr(CMapObject *pMO)
    {
        pObject = 0;
        Set(pMO);
    }
    CMapObjectPtr(const CMapObjectPtr &a)
    {
        pObject = 0;
        Set(a.pObject);
    }
    CMapObjectPtr &operator=(const CMapObjectPtr &a)
    {
        Set(a.pObject);
        return *this;
    }
    CMapObjectPtr &operator=(CMapObject *pMO)
    {
        Set(pMO);
        return *this;
    }
    ~CMapObjectPtr() { Clear(); }

    operator CMapObject *() const { return pObject; }
    operator CUnit *() const { return (CUnit *)pObject; }
    CMapObject *operator->() const { return pObject; }
    // int operator==( const CMapObjectPtr &a ) const { return pObject == a.pObject; }
    // int operator==( const CMapObject *pA ) const { return pObject == pA; }
    // int operator!=( const CMapObject *pA ) const { return pObject != pA; }
    // int operator!=( const CUnit *pA ) const { return pObject != (CMapObject*) pA; }
    BOOL IsEmpty() const { return pObject == NULL; }

    void Check()
    {
        if (pObject->IsInvalid())
            Clear();
    }
    void Load(CSave &file, CWorld &world);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
typedef CList<CMapObjectPtr, CMapObject *> CMapObjectPtrList;

// --------------------------------------------------------------------------------------------
// load&store for mapobjectptrs lists
// --------------------------------------------------------------------------------------------
inline void StoreMapObjectPtrList(CMapObjectPtrList &arr, CSave &file)
{
    int i = arr.GetCount();
    file.Write(&i, sizeof(i));
    POSITION pos = arr.GetHeadPosition();
    while (pos)
        arr.GetNext(pos).Store(file);
}

/////////////////////////////////////////////////////////////////////////////////////
inline void LoadMapObjectPtrList(CMapObjectPtrList &arr, CSave &file, CWorld &world)
{
    int i;
    file.Read(&i, sizeof(i));
    while (i-- > 0)
        arr.GetAt(arr.AddTail(CMapObjectPtr())).Load(file, world);
}
/////////////////////////////////////////////////////////////////////////////////////
typedef CMapObject *LPMAPOBJECT;
