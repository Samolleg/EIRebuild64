#pragma once
namespace SaveSchema { class Writer; class Reader; }

/*==========================================================================================================

        Effects classes header

==========================================================================================================*/

#include "Base3D.h"
#include "SoundTypes.h"
#include "ServerConsts.h"
#include "OwnedRender/E2EffectDistance.h"
#include "SaveLoader.h"
#include "NetUpdate.h"
#include "Lightning.h"
#include "MapObject.h"
#include "Light.h"
#include "RpgSystem.h"
#include "VisFog.h"
#include "aiSensesConst.h"
#include "Particle.h"

class CScriptObjectGroup;

// --------------------------------------------------------------------------------------------
// Effect base
// --------------------------------------------------------------------------------------------
class CEffect : public CBase3D
{
protected:
    int nStartSegment;
    CPlot3D vecSpeed; // speed of effect`s moving
    BOOL bMoving;
    void SetMoving();

public:
    CUpdateSingle update;
    int nUpdatedSegment;

    CEffect()
        : vecSpeed(0, 0, 0)
    {
        nStartSegment = 0;
        bMoving = FALSE;
    }
    virtual void Clear();
    virtual BOOL Create();
    virtual void Destroy();
    void SetUpdateMask();
    // Additional range
    virtual float GetRange() const { return OwnedRender::E2EffectDistance(); }
    virtual const CPlot3D &GetEffectPlace(CScriptObjectGroup &currentVisible) { return cp; }
    // Segment
    virtual int Segment() { return TRUE; }
    // Updates
    void UpdateInit();
    void UpdateFrame();
    BOOL UpdateFrom(EUpdateType tip, CPacket &pkt);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt) = 0;
    virtual void UpdateTo(CUpdateHistory &pkt) = 0;
    virtual void ApplyParams(EUpdateType tip) = 0;
    void StartEffect(int nSegment) { nStartSegment = nSegment; }
    void SetSpeed(const CPlot3D &newSpeed);
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// Sound effect
// --------------------------------------------------------------------------------------------
class CEffectSound : public CEffect
{
public:
    char szSoundName[256];          // Name of the sound file
    float fpMinRadius, fpMaxRadius; // Hearing radiuses of the sound
    BOOL bLooping; // if sound is not looping then it is played once and effect is deleted
    bool bHighPriority;
    HSOUND hSnd; // Sound handler ( client only )
    bool bFresh;

public:
    CEffectSound();
    virtual ~CEffectSound()
    {
        CEffectSound::Clear();
        Destroy();
    }
    virtual void Clear();
    virtual float GetRange() const { return fpMaxRadius; }
    // erasing non looping sounds
    virtual int Segment();
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectSound"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTSOUND; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// Sound effect
// --------------------------------------------------------------------------------------------
class CEffectSoundSeq : public CEffect
{
public:
    CString szStartSound;
    CStringArray szSounds;
    float fpMinRadius, fpMaxRadius; // Hearing radiuses of the sound
    bool bHighPriority;
    HSNDLIST hSndList; // Sounds handler ( client only )
public:
    CEffectSoundSeq();
    virtual ~CEffectSoundSeq()
    {
        CEffectSoundSeq::Clear();
        Destroy();
    }
    virtual void Clear();
    virtual float GetRange() const { return fpMaxRadius; }
    //
    virtual int Segment();
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectSoundSeq"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTSOUNDSEQ; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// Ambient sound
// --------------------------------------------------------------------------------------------
class CEffectAmbientSound : public CEffect
{
public:
    CStringArray szSoundNames;      // List of the sound file names
    float fpMinRadius, fpMaxRadius; // Radiuses of the sound
    int nMsToPlayMin, nMsToPlayMax; // Playing frequences
    HSNDAMBIENT hSndAmb;            // Sound handler ( client only )
public:
    CEffectAmbientSound();
    virtual ~CEffectAmbientSound()
    {
        CEffectAmbientSound::Clear();
        Destroy();
    }
    virtual void Clear();
    virtual float GetRange() const { return fpMaxRadius; }
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectAmbientSound"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTAMBIENTSOUND; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// Point light effect
// --------------------------------------------------------------------------------------------
class CEffectPointLight : public CEffect
{
public:
    CPlot3D color;            // Light color
    float fpRange;            // Light range
    CSmartPtr<CLight> pLight; // Pointer to light ( client only )
    DWORD dwFlags;

public:
    CEffectPointLight();
    virtual ~CEffectPointLight() { Destroy(); }
    virtual void Clear();
    virtual float GetRange() { return OwnedRender::E2EffectDistance() + fpRange; }
    //
    virtual int Segment();
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectPointLight"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTPOINTLIGHT; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// Particle source
// --------------------------------------------------------------------------------------------
class CEffectParticle : public CEffect
{
    CPlot3D ptTemp; // for GetEffectPlace()
protected:
    void AttachCarrierOnClient();
    int GetParticleEffectLength();
    void UpdateParticleParams();

public:
    ETypeIDs typeID;                // type ID of particle source
    float fpScale;                  // Scale value of effect
    float fpPower, fpPower2;        // Power of effect defaults to 1
    int nAttachedID;                // object to which particle is attached
    CPlot3D ptDest;                 // destination of moving particles
    CSmartPtr<CParticle> pParticle; // Pointer to particle source ( client only )
    int nStartID;                   // set start effect position to this object
public:
    CEffectParticle();
    virtual ~CEffectParticle() { Destroy(); }
    virtual void Clear();
    virtual const CPlot3D &GetEffectPlace(CScriptObjectGroup &currentVisible);
    // Segment
    virtual int Segment();
    // Server side commands
    void Attach(int nObjectID);
    void SetDestination(const CPlot3D &_ptDest);
    void SetScale(float _fpScale);
    void SetPower(float _fpPower, float fpPower2 = 1);
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectParticle"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTPARTICLE; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// Wall effects ( firewall etc )
// --------------------------------------------------------------------------------------------
class CEffectWall : public CEffectParticle
{
public:
    CPlot3D
        ptWallDirection; // Вектор от центра эффекта, показывающий где должен присутствовать эффект
public:
    CEffectWall();
    virtual ~CEffectWall() { Destroy(); }
    virtual void Clear();
    //
    void SetWallVector(const CPlot3D &vec);
    // Update
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectWall"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTWALL; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// Lightning
// --------------------------------------------------------------------------------------------
class CEffectLightning : public CEffect
{
public:
    CPlot3D ptSrc, ptDst;             // source and destination of lightning
    float fpWidth;                    // width of lightning
    CSmartPtr<CLightning> pLightning; // pointer to lightning
    int nAttachedID;                  // object to which second point of lightning is attached
    int nCasterID;                    // who is casting (if casting)
    BYTE bColor;

    void RecalcPosition();

public:
    CEffectLightning();
    virtual ~CEffectLightning() { Destroy(); }
    virtual void Clear();
    void SetParams(const CPlot3D &ptNSrc, const CPlot3D &ptNDst, float fpNWidth);
    // Server side commands
    void Attach(int nSrcID, int nTargetID);
    void SetColor(BYTE _bColor);
    // Segment
    virtual int Segment();
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectLightning"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTLIGHTNING; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
class CEffectArrowBase : public CEffect
{
protected:
    BOOL bDelivered;
    int nTargetID;

    virtual BOOL CalcSpeed();
    virtual void ApplyNewArrowPos() = 0;

public:
    bool bNewArrow;

public:
    void SetTarget(int nID);
    virtual void Clear();
    virtual int Segment();
    BOOL IsDelivered() { return bDelivered; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
class CEffectArrow : public CEffectArrowBase
{
    CEffectParticle particle;
    CEffectPointLight pointLight;
    CEffectSound sound;
    SRPGAttackPortion weaponAttack;
    bool weaponAttackPending;
    void DiscardWeaponAttack();

    virtual void ApplyNewArrowPos();

public:
    CString szSoundName;
    CPlot3D color;   // Light color
    float fpRange;   // Light range
    ETypeIDs typeID; // type ID of particle source
    float fpScale;   // Scale value of effect
    int nAttackerID; // arrow`s author

public:
    CEffectArrow();
    virtual ~CEffectArrow() { Clear(); Destroy(); }
    void SetWeaponAttack(const SRPGAttackPortion&);
    BOOL DeliverWeaponAttack(CUnit*target,CUnit*attacker);
    virtual int Segment();
    virtual void Clear();
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectArrow"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTARROW; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
class CEffectArrowFigure : public CEffectArrowBase
{
    CMapObjectPtr pMapObj;
    bool attackProcessed;
    bool explosivePending=false;
    CPlot3D explosivePoint;
    float explosiveRadius=0;
    int explosiveStart=0;
    BOOL CalcSpeed() override;
    void Explode(const CPlot3D&);

    virtual void ApplyNewArrowPos();

public:
    int nAttackerID; // arrow`s author
    SRPGAttackPortion attackPortion;

    CEffectArrowFigure();
    BOOL DeliverAttack(CMapObject*target,CUnit*attacker);
    void SetExplosivePoint(const CPlot3D& point,float radius,int tick){explosivePoint=point;explosiveRadius=radius;explosiveStart=tick;explosivePending=true;}
    virtual ~CEffectArrowFigure()
    {
        CEffectArrowFigure::Clear();
        Destroy();
    }
    virtual void Clear();
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    virtual int Segment();
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectArrowFigure"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTARROWFIGURE; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CEffectShowVisibility : public CEffect
{
    int nTargetID, nWatcherID;
    CSmartPtr<CVisibilityFog> pFog;
    SVisibilityMask mask;

public:
    CEffectShowVisibility();
    virtual ~CEffectShowVisibility()
    {
        CEffectShowVisibility::Clear();
        Destroy();
    }
    virtual void Clear();
    //
    void SetNewParams(int nTarget, int nWatcher);
    // Update
    virtual void ApplyParams(EUpdateType tip);
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    virtual int Segment();
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectShowVisibility"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTSHOWVISIBILITY; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
// client side effect
// --------------------------------------------------------------------------------------------
class CEffectMoshka : public CEffect
{
    enum EState
    {
        Start,
        Round,
        Finish
    };
    CMapObjectPtr pMapObj;
    CSmartPtr<CParticle> pParticle;
    int nUnitID;
    float fpPrevTime = 0, fpStateTime = 0, fpBaseAngle = 0, fpRoundAwayH = 0;
    EState state = Start;

    CPlot3D ChooseFarPoint(const CPlot3D &ptBase);
    CPlot3D ChooseFarPoint(const CPlot3D &ptBase, float fpAngle);
    void DoFinish(const CPlot3D &ptFinish);

public:
    CEffectMoshka();
    virtual ~CEffectMoshka()
    {
        CEffectMoshka::Clear();
        Destroy();
    }
    virtual void Clear();
    void SetTime(float fpTime);
    void Create(int nBindTo);
    virtual int Segment();
    // keep effect happy
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    virtual void ApplyParams(EUpdateType tip);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectMoshka"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTMOSHKA; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};

// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CEffectTornado : public CEffect
{
    CSmartPtr<CParticle> pParticle;
    HSOUND hSnd; // Sound handler ( client only )
    int nTimeLeft, nStartSegment;

public:
    CEffectTornado();
    virtual ~CEffectTornado()
    {
        CEffectTornado::Clear();
        Destroy();
    }
    virtual void Clear();
    void SetParams(const CPlot3D &ptSrc, const CPlot3D &ptDst);
    virtual int Segment();
    // updates
    virtual BOOL UpdateFrom(EUpdateType tip, CUpdateHistory &pkt);
    virtual void UpdateTo(CUpdateHistory &pkt);
    virtual void ApplyParams(EUpdateType tip);
    // load&store
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
    // class identification information
    virtual LPCSTR GetGameClassName() const { return "CEffectTornado"; }
    virtual EClassIDs WhoAmI() const { return HPC_EFFECTTORNADO; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
};
