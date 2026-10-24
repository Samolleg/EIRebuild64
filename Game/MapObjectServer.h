#pragma once
namespace SaveSchema { class Writer; class Reader; }

#include "RPGSystem.h"
#include "MapObject.h"

// struct SMOScriptEvent;
class CUnit;
class CMapObjectServer : public CMapObjectSpecific
{
    friend class CMapObject;

private:
    SRPGCommonStats rpgCommonStats; // common server stats
public:
    // Crap, must be killed without mercy
    CString szPrimTexture;
    CString szSecTexture;
    CStringArray szNames;

public:
    CMapObjectServer();
    virtual ~CMapObjectServer();
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    // access to RPG stats
    void SetRPGStats(SRPGCommonStats *lpRPGNewCommonStats);
    SRPGCommonStats *GetRPGStats() { return &rpgCommonStats; }
    float GetCurrentHP() const { return rpgCommonStats.ssHP.fpCurrent; }
    float GetMaxHP() const { return rpgCommonStats.ssHP.fpMax; }
    float ChangeCurrentHP(float fpChange);
    float GetCurrentMP() const { return rpgCommonStats.ssMP.fpCurrent; }
    float GetMaxMP() const { return rpgCommonStats.ssMP.fpMax; }
    float ChangeCurrentMP(float fpChange);
    virtual BOOL ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion);
    // RPG system information
    virtual BOOL IsDead() const { return (rpgCommonStats.IsDead()); };
    virtual BOOL IsCorpse() const
    {
        return (rpgCommonStats.ssHP.fpCurrent < -5.0f * rpgCommonStats.ssHP.fpMax);
    }
    //
    virtual int Segment();
    //
    virtual void LeaveMap();
    // load & store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CMapObjectServer"; };
    virtual EClassIDs WhoAmI() const { return HPC_MAPOBJECTSERVERSPECIFIC; };
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// change current HP and put it in bounds [-inf...fpMaxHP]
// also, set an update flag CMapObject::VLD_MASK_STATS
// --------------------------------------------------------------------------------------------
inline float CMapObjectServer::ChangeCurrentHP(float fpChange)
{
    rpgCommonStats.ssHP.fpCurrent += fpChange;
    rpgCommonStats.ssHP.fpCurrent = Min(rpgCommonStats.ssHP.fpMax, rpgCommonStats.ssHP.fpCurrent);
    lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_STATS);
    return rpgCommonStats.ssHP.fpCurrent;
}

// --------------------------------------------------------------------------------------------
// change current MP and put it in bounds [0...fpMaxMP]
// also, set an update flag CMapObject::VLD_MASK_STATS
// --------------------------------------------------------------------------------------------
inline float CMapObjectServer::ChangeCurrentMP(float fpChange)
{
    rpgCommonStats.ssMP.fpCurrent += fpChange;
    rpgCommonStats.ssMP.fpCurrent = Max(0.0f, Min(rpgCommonStats.ssMP.fpMax,
                                                  rpgCommonStats.ssMP.fpCurrent));
    lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_STATS);
    return rpgCommonStats.ssMP.fpCurrent;
}
