#pragma once
namespace SaveSchema { class Writer; class Reader; }
/////////////////////////////////////////////////////////////////////////////////////
#include "MapObject.h"
#include "SmartPtr.h"
#include "SpellSystem.h"
#include "METraps.h"
/////////////////////////////////////////////////////////////////////////////////////
struct SMEBuildingCaster;
struct SMEMagicTrap;
struct SMELever;
/////////////////////////////////////////////////////////////////////////////////////
class CTree : public CMapObject
{
private:
public:
    CTree();
    virtual ~CTree();
    //
    //
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CTree"; };
    virtual EClassIDs WhoAmI() const { return HPC_TREE; };
};
/////////////////////////////////////////////////////////////////////////////////////
class CBuilding : public CMapObject
{
private:
public:
    CBuilding();
    virtual ~CBuilding();
    //
    //
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CBuilding"; };
    virtual EClassIDs WhoAmI() const { return HPC_BUILDING; };
};
/////////////////////////////////////////////////////////////////////////////////////
class CStone : public CMapObject
{
private:
public:
    CStone();
    virtual ~CStone();
    //
    //
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CStone"; };
    virtual EClassIDs WhoAmI() const { return HPC_STONE; };
};
/////////////////////////////////////////////////////////////////////////////////////
class CStuffObject : public CMapObject
{
private:
public:
    CStuffObject();
    virtual ~CStuffObject();
    //
    //
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CStuffObject"; };
    virtual EClassIDs WhoAmI() const { return HPC_STUFF_OBJECT; };
};
/////////////////////////////////////////////////////////////////////////////////////
// флажки, используемые ловушками
const DWORD TRAP_ACTIVE = MAKE_FLAG(MAPOBJ_LAST_BIT, 0);
const DWORD TRAP_STARTUP = MAKE_FLAG(MAPOBJ_LAST_BIT, 1);

const DWORD TRAP_LAST_BIT = MAKE_LAST_BIT(MAPOBJ_LAST_BIT, 1);
/////////////////////////////////////////////////////////////////////////////////////
class CGeneralTrapObject : public CMapObject
{
protected:
    DWORD dwEnemyMask; // маска для определения враждебных плееров
public:
    virtual BOOL Create();
    virtual bool IsDynamic() { return true; }
};
/////////////////////////////////////////////////////////////////////////////////////
// механические ловушки
class CTrapObject : public CGeneralTrapObject
{
public:
    CTrapObject();
    virtual BOOL Create();
    //
    virtual int Segment();
    void ActivateTrap();
    //
    virtual BOOL IsScientific() { return TRUE; }
    virtual bool ApplyScience(const SRPGScienceStats &tool, CUnit *pWho);
    virtual BOOL CanApplyScience(const SRPGScienceStats &tool);
    //
    virtual void Load(CSave &file); // if trap will use diplomacy save dwEnemyMask here
    virtual void Store(CSave &file);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CTrapObject"; };
    virtual EClassIDs WhoAmI() const { return HPC_TRAP; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
// объект, который кастует спелл в указанные точки или на юнита(юнитов если bCastOnce == FALSE),
// если точки не указаны при попадании некоторого юнита в область trapAreas (или кастует
// постоянно пока есть юниты в TrapAreas - если nCastingInterval != 0)
class CMagicTrapObject : public CGeneralTrapObject
{
private:
    CSmartPtr<CSpell> pSpell;              // чем собственно будет пыхать этот trap
    CAArray<SMEArea, SMEArea &> trapAreas; // множество тригеров
    CAArray<CPlot2D, CPlot2D &> ptTargets; // точки, в которые будет нанесен удар при
                                           // попадании в область активации ловушки, если этих точек
                                           // нет, то удар будет нанесен в юнита
    CMapObjectPtrList trappedPersons;      // те, кто уже наступил, но еще не сошел
    int nCastingInterval;                  // сколько сегментов между кастованиями
    int nTimeToCast;                       // сколько времени осталось до возможности катования
    BOOL bCastOnce;                        // поражается спелом только ближайший юнит или все сразу

    BOOL IsUnitOnTrap(CUnit *pUnit);
    void CastSpell(CList<CUnit *, CUnit *> &targets);
    void WorkAsTrigger();
    void DamageValidTargets();

public:
    CMagicTrapObject();
    virtual ~CMagicTrapObject() { Destroy(); }
    virtual BOOL Create();
    virtual void Destroy();
    void SetParams(SMEMagicTrap &data);
    //
    virtual int Segment();
    virtual void RemoveReferencesOn(CMapObject *pTarget);
    //
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CMagicTrapObject"; };
    virtual EClassIDs WhoAmI() const { return HPC_MAGICTRAP; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
// флажки, используемые lever`ами
const DWORD LEVER_DISABLED = MAKE_FLAG(MAPOBJ_LAST_BIT, 0);

const DWORD LEVER_LAST_BIT = MAKE_LAST_BIT(MAPOBJ_LAST_BIT, 0);
/////////////////////////////////////////////////////////////////////////////////////
// объект типа "рычаг" - может быть переведен в следующее состояние путем применения
// скила science
class CLeverObject : public CMapObject
{
private:
    int nCurrentState, nTotalStates; // текущее состояние и общее количество состояний
    BOOL bCycle;
    SRPGScienceStats science; // для определения того, какие научные воздействия корректны
    // начальное значение положения рычага, конечное значение положения рычага, время завершения
    // движения и 1/время движения
    float fpStartVal, fpFinalVal, fpFinalTime, fpTimePeriod1;
    bool bIsDoor, bGraphRecalc;

public:
    bool UsesAuthoredInteraction() const;
    bool ScriptActivationAllowed(CUnit* who);
    CLeverObject();
    virtual ~CLeverObject();
    void SetParams(SMELever &data);
    virtual BOOL Create();
    virtual bool IsDynamic() { return true; }
    virtual bool NeedGraphRecalc() { return bGraphRecalc; }
    //
    virtual void UpdateTo();
    virtual BOOL UpdateFrom(EUpdateType tip);
    //
    virtual int Segment();
    //
    void SetNewScience(const SRPGScienceStats &_science);
    virtual BOOL IsScientific();
    BOOL IsScientificDoor() { return bIsDoor; }
    virtual bool ApplyScience(const SRPGScienceStats &tool, CUnit *pWho);
    virtual DWORD GetScienceKeyNumber();
    virtual BOOL CanApplyScience(const SRPGScienceStats &tool);
    virtual void GetUsePoints(CArray<CPlot3D, CPlot3D &> &points, float fpUnitRadius);
    //
    int GetCurrentState() const { return nCurrentState; }
    virtual float GetAttackDistance();
    // новое состояние (-1)-следующее и время, за которое рычаг должен переключится в это состояние
    void SwitchState(int nState, float fpTime = -10);
    void EnableLever(bool bEnable);
    //
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CLeverObject"; };
    virtual EClassIDs WhoAmI() const { return HPC_LEVER; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
class CTorchObjectSpecific : public CGameBase
{
protected:
    CMapObject *lpTorch;

    ETypeIDs tidParticlesType;

    CString szSFXName;
    float fpParticleScale;
    float fpLightRange;
    D3DCOLORVALUE dcvLightColor;

    CPlot3D ptParticlesCenter;
    CPlot3D ptLightCenter;

public:
    CTorchObjectSpecific()
    {
        lpTorch = 0;
        Create();
    }
    virtual ~CTorchObjectSpecific() { Destroy(); }
    //
    void SetObject(CMapObject *lpNewObject) { lpTorch = lpNewObject; }
    CMapObject *GetObject() const { return lpTorch; }
    //
    virtual BOOL Create();
    virtual void Destroy();
    //
    void UpdateTo(CUpdateHistory &pkt);
    void UpdateFrom(CUpdateHistory &pkt);
    //
    virtual void TurnOn() {}
    virtual void TurnOff() {}
    virtual void StartupEffect() {}
    //
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    //
    virtual void SetParticles(ETypeIDs tidParticlesTypeID, const CPlot3D &position,
                              const float fpScale);
    virtual void SetLight(const CPlot3D &position, const float range, const D3DCOLORVALUE &color);
    virtual void SetSFX(LPCSTR pszSFXName);
    //
    virtual int Segment() { return 1; }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CTorchObjectSpecific"; };
    virtual EClassIDs WhoAmI() const { return HPC_TORCHSERVERSPECIFIC; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
class CTorchObject : public CMapObject
{
private:
    CSmartPtr<CTorchObjectSpecific> pTorchSpecific;

private:
    void SetTorchSpecific(CTorchObjectSpecific *pNewSpecific)
    {
        pTorchSpecific = pNewSpecific;
        if (pTorchSpecific)
            pTorchSpecific->SetObject(this);
    }

public:
    CTorchObject();
    virtual ~CTorchObject();
    //
    virtual BOOL Create();
    virtual void Destroy();
    virtual bool IsDynamic() { return true; } // CRAP
    //
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // Update
    virtual void UpdateTo();
    virtual BOOL UpdateFrom(EUpdateType tip);
    //
    virtual void SetSpecific(LPGAMEBASE lpNewSpecific);
    //
    CTorchObjectSpecific *GetTorchSpecific() const { return pTorchSpecific; }
    //
    virtual int Segment() { return 1; }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CTorchObject"; };
    virtual EClassIDs WhoAmI() const { return HPC_TORCH; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
/////////////////////////////////////////////////////////////////////////////////////
