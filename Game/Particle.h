#pragma once
/**********************************************************************
 *<
        FILE: Particle.H

        DESCRIPTION:  Particle object header

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include <cstdint>
#include "embedded_arena_particles.h"
#include "Base3D.h"
#include "Plot.h"
#include "typedefs.h"
#include "texture.h"
#include "TextureDescriptor.h"

struct SParticleUV
{
    float fpU0, fpV0, fpU1, fpV1;
};
/////////////////////////////////////////////////////////////////////////////////////
struct SParticleToDraw
{
    DWORD dwColor;
    SParticleUV *pUV;
};

class CParticle;
class CParticleManager;
class CObject3D;
class CRenderer;
class CUnit;

// --------------------------- SSingleParticle -----------------------------------------------
struct SSingleParticle
{
    SXYZW coord;      // world coordinates and size of particle
    SXYZW prevcoord;  // world coordinates and size of particle
    CPlot3D reserved; // reserved values
    CPlot3D reserved2;
    CPlot3D vSpeed;     // Speed of particle
    int nPhase;         // current phase
    int nPrevPhase;     // previous phase
    int nPhaseLifeTime; // Life time of current phase
    int nLifeTime;      // LifeTime for this particle
    int nSpecial;       // index of local source or somethig else
    DWORD dwColor;      // current color
    DWORD dwPrevColor;  // previous color
    int nDelFlag;       // this variable is set when a particle is to be erased
};
typedef CList<SSingleParticle *, SSingleParticle *> SSingleParticleList;

typedef CSimplyArray<SParticleUV, SParticleUV &> CPUVArray;

struct SLocalSource
{
    CPlot3D respectCP;
    CPlot3D vSpeed;
    CPlot3D reserved;
    CPlot3D reserved2;
    int nPhase;
    int nLifeTime;
};
typedef CSimplyArray<SLocalSource, SLocalSource &> CLocalSourceArray;

// some flags for CParticle
const int PS_GENERATE_PARTICLES = 1; // set if new particles are to be generated
const int PS_NEED_CARRIER_TO_GENERATE =
    2; // set if no new particles should be generated without carrier
const int PS_KEEP_ALIVE =
    4; // set if particle effect should be kept alive even if no particles exist
const int PS_UNPAUSABLE = 8;        // set if particle effect is to be played during pause
const int PS_BIND_TO_BODYPART = 16; // carrierCP = GetCastPoint from carrier
const int PS_PAUSE_INVISIBLE = 32;  // set to prevent segment processing when effect is invisible
const int PS_DONT_DRAW = 64;        // do not draw particle effect

// --------------------------- CParticle -----------------------------------------------------
class CParticle : public CBase3D
{
    void InitializeExtendedSpellSources();
public:
    enum EBindPlace
    {
        bpCast,
        bpHead,
        bpAboveHead,
        bpLHand,
        bpRHand,
        bpLLeg,
        bpRLeg,
        bpBody,
        bpBody00,
        bpBody01,
        bpBody02,
        bpBody03,
        bpBody04,
        bpRStaff,
        bpLStaff
    };

private:
    CObject3D *pCarrier;        // объект, к которому прив€зан данный партикл-эффект
    CPlot3D carrierCP;          // last resolved world position, including GetCP() offset
    CPlot3D vCarrierSpeed;      // carrier speed ( movement of carrier per last segment )
    CPlot3D vSpeedX1, vSpeedX2; // вектора, перпендикул€рные vCarrierSpeed единичной длины
    float fpSpeedMod;           // module of carrier speed

    int nBornProbability;    // new particle appearance probability in percent
    int nMinTrys;            // minimum number of trys to generate
    float fpLenToSingleDice; // length of motion distance to throw single dice
    int nParticlesMax;       // maximum number of particles
    int nAnimPhases;         // number of animation phases
    int nAnimPerTexSize;     // number of animation tiles per texture size
    union
    {
        int nLifeOfPhase;        // live of one phase
        std::uintptr_t powerHitWeaponToken; // PowerHit uses this slot instead of phase timing.
    };
    int nInvisibleCounter; // when this counter exceeds certain value and PS_PAUSE_INVISIBLE is set
                           // no segments are called

    int nLifeSpan;                              // life time period
    CPlot3D posRespectMinRnd, posRespectMaxRnd; // respect position vector random values
    DWORD dwFadeAlpha;                          // alpha fade out
    float fpSize;                               // camera-oriented size of particle
    float fpSpeedFactor;                        // speed multiplier ( length of vCarrierSpeed )
    float fpSizeFactor;                         // size factor
    float fpPower, fpPower2;                    // powers of effect
    float fpParam1, fpParam2;
    float fpCurrentScale; // current scale of effect
    // flags
    DWORD dwPFlags;
    // for wall effects organization
    CPlot3D ptWallDirection;
    EBindPlace bindPlace;

    // wind
    CPlot3D windDir;   // wind direction
    float fpWindForce; // wind force

    // add effect
    BOOL bUseAddEffect;

    // local sources
    CLocalSourceArray locSources;

private:
    CPlot3D bvMin, bvMax;          // bounding volume min and max points
    SBoundingVolume bv;            // bounding volume of entire particle system
    SSingleParticleList particles; // list of particles
    CPUVArray uvOfPhases;          // UV coordinates of each phase
    BOOL (CParticle::*createFunc)(SSingleParticle *, int);
    BOOL (CParticle::*segmentFunc)(SSingleParticle *);
    void (CParticle::*locSourceSegmentFunc)(SLocalSource *);

    CTextureDescriptor pTexture;    

    void CreateParticle(int nParticle);
    void AddNewParticle(SSingleParticle *pPart);
    CPlot3D GetCarrierCP(CObject3D *pOldCarrier);
    void GetObjectBV(CPlot3D &ptCenter, float &fpRadius);
#ifndef _A4
    CPlot3D GetBodyPartCP(CUnit *pUnit, const char *pszName);
#endif

public:
    // const/destr
    CParticle() { Clear(); }
    virtual ~CParticle() { Destroy(); }
    virtual BOOL Create();
    void ConfigurePowerHit(CUnit*,const CPlot3D&,const CPlot3D&,int);
    void ConfigureScriptFlow(const CPlot3D&,float length,float width);
    void ConfigureArenaWave(int kind,float radius,bool charge,int tick);
    bool IsPowerHitGlow(int hit) const {return createFunc==&CParticle::PowerHitSingleCreate&&fpParam1==0&&locSources.GetSize()==1&&locSources[0].nPhase==hit;}
    void MarkPowerHitDelivered(){if(fpParam2>=0&&fpPower2==0){fpPower2=1;locSources[0].reserved.y=6;}}
    void SetPowerHitWindup(int ticks,std::uintptr_t token){locSources[0].reserved.x=float(ticks);powerHitWeaponToken=token;}
    static void StopPowerHitLights();
    const char* ConfigureArrow(ETypeIDs type);
    bool ConfigureExtendedSpell(ETypeIDs type,char*texture); // Configuration only; texture binding is owned by Create.
    virtual void Destroy();
    void FinishEffect();
    void BindTo(EBindPlace bind) { bindPlace = bind; }
    // stop particle generation but don`t delete effect when all particles are deleted
    void HoldEffect();
    // resume particle generation, inverse to HoldEffect()
    void RunEffect();
    // stop effect and eliminate all particles
    void DeleteEffect();
    // stop/resume processing segments and visualisation for clipped out particle sources
    void SetClipState(bool bVisible);
    // Scale
    void Scale(float fpScaleFactor);
    float GetScale() const { return fpCurrentScale; }
    void SetScale(float fpVal) { Scale(fpVal / GetScale()); }
    // Set power
    void SetPower(float fpNewPower) { fpPower = fpNewPower; }
    void SetPower2(float fpNewPower) { fpPower2 = fpNewPower; }
    void SetParam1(float fpA) { fpParam1 = fpA; }
    void SetParam2(float fpA) { fpParam2 = fpA; }
    // Set wall vector for wall effects like firewall
    void SetWallVector(const CPlot3D &ptVec) { ptWallDirection = ptVec; }
    // Set/get values
    void AttachCarrier(CObject3D *pNewCarrier);
    CObject3D *GetCarrier() { return pCarrier; }
    SBoundingVolume &GetBoundingVolume() { return bv; }
    CPlot3D GetPosition();
    BOOL IsUseAddEffect() { return bUseAddEffect; }
    // Segment, returns FALSE when system is ready to destroy
    int Segment();
    // Draw
    BOOL Draw(CRenderer *pTarget);

    // class identification functions
    LPCSTR GetGameClassName() const { return "CParticle"; }
    EClassIDs WhoAmI() const { return HPC_PARTICLE; }

private:
    // Initialization
    void Clear();
    void ClearBV()
    {
        bvMin = CPlot3D(1e5f, 1e5f, 1e5f);
        bvMax = CPlot3D(-1e5f, -1e5f, -1e5f);
    }
    void UpdateBV(SSingleParticle *);
    void FinishBV();
    // Single particles processing
    owned::ArenaWaveEmitter arenaWave{};
    BOOL ArenaWaveSingleCreate(SSingleParticle*,int);
    BOOL ArenaWaveSingleSegment(SSingleParticle*);
    std::uint32_t scriptFlowRandom=0x13579bdu;
    BOOL ScriptFlowSingleCreate(SSingleParticle*,int);
    BOOL ScriptFlowSingleSegment(SSingleParticle*);
    const void* powerHitWorld=nullptr;
    BOOL PowerHitSingleCreate(SSingleParticle*,int);
    BOOL PowerHitSingleSegment(SSingleParticle*);
    void PowerHitSourceSegment(SLocalSource*);
    BOOL FireballSingleCreate(SSingleParticle *, int);
    BOOL FireballSingleSegment(SSingleParticle *);
    BOOL CampfireSingleCreate(SSingleParticle *, int);
    BOOL CampfireSingleSegment(SSingleParticle *);
    BOOL FireBlastSingleCreate(SSingleParticle *, int);
    BOOL FireBlastSingleSegment(SSingleParticle *);
    void FireBlastLocSourceSegmentFunc(SLocalSource *);
    BOOL VulcanSmokeSingleCreate(SSingleParticle *, int);
    BOOL VulcanSmokeSingleSegment(SSingleParticle *);
    BOOL HealingSingleCreate(SSingleParticle *, int);
    BOOL HealingSingleSegment(SSingleParticle *);
    void HealingLocSourceSegmentFunc(SLocalSource *);
    BOOL PoisonFogSingleCreate(SSingleParticle *, int);
    BOOL PoisonFogSingleSegment(SSingleParticle *);
    BOOL GeyserSingleCreate(SSingleParticle *, int);
    BOOL GeyserSingleSegment(SSingleParticle *);
    void GeyserLocSourceSegmentFunc(SLocalSource *);
    BOOL TornadoSingleCreate(SSingleParticle *, int);
    BOOL TornadoSingleSegment(SSingleParticle *);
    void TornadoLocSourceSegmentFunc(SLocalSource *);
    BOOL CastingSingleCreate(SSingleParticle *, int);
    BOOL CastingSingleSegment(SSingleParticle *);
    void CastingLocSourceSegmentFunc(SLocalSource *);
    BOOL NukeSingleCreate(SSingleParticle *, int);
    BOOL NukeSingleSegment(SSingleParticle *);
    void NukeLocSourceSegmentFunc(SLocalSource *);
    BOOL BansheeCastingSingleCreate(SSingleParticle *, int);
    BOOL BansheeCastingSingleSegment(SSingleParticle *);
    void BansheeCastingLocSourceSegmentFunc(SLocalSource *);
    BOOL MushroomSingleCreate(SSingleParticle *, int);
    BOOL MushroomSingleSegment(SSingleParticle *);
    void MushroomLocSourceSegmentFunc(SLocalSource *);
    BOOL BloodSingleCreate(SSingleParticle *, int);
    BOOL BloodSingleSegment(SSingleParticle *);
    void BloodLocSourceSegmentFunc(SLocalSource *);
    BOOL FireWallSingleCreate(SSingleParticle *, int);
    void FireLocSourceSegmentFunc(SLocalSource *);
    BOOL FireArrowSingleCreate(SSingleParticle *, int);
    BOOL FireArrowSingleSegment(SSingleParticle *);
    BOOL AcidRaySingleCreate(SSingleParticle *, int);
    BOOL AcidRaySingleSegment(SSingleParticle *);
    BOOL BlueGasSingleCreate(SSingleParticle *, int);
    BOOL BlueGasSingleSegment(SSingleParticle *);
    void EmptyLocSourceSegmentFunc(SLocalSource *);
    BOOL LinkSingleCreate(SSingleParticle *, int);
    BOOL LinkSingleSegment(SSingleParticle *);
    void LinkLocSourceSegmentFunc(SLocalSource *);
    BOOL SphereSingleCreate(SSingleParticle *, int);
    BOOL SphereSingleSegment(SSingleParticle *);
    BOOL ClayRingSingleCreate(SSingleParticle *, int);
    BOOL ClayRingSingleSegment(SSingleParticle *);
    void ClayRingLocSourceSegmentFunc(SLocalSource *);
    BOOL TeleportSingleCreate(SSingleParticle *, int);
    BOOL TeleportSingleSegment(SSingleParticle *);
    void TeleportLocSourceSegmentFunc(SLocalSource *);
    BOOL ModifierSingleCreate(SSingleParticle *, int);
    BOOL ModifierSingleSegment(SSingleParticle *);
    void ModifierLocSourceSegmentFunc(SLocalSource *);
    BOOL CastingStarRoundSingleCreate(SSingleParticle *, int);
    BOOL CastingStarRoundSingleSegment(SSingleParticle *);
    void CastingStarRoundLocSourceSegmentFunc(SLocalSource *);
    BOOL CastingFailedSingleCreate(SSingleParticle *, int);
    BOOL CastingFailedSingleSegment(SSingleParticle *);
    void CastingFailedLocSourceSegmentFunc(SLocalSource *);
    BOOL CastingGatherSingleCreate(SSingleParticle *, int);
    BOOL CastingGatherSingleSegment(SSingleParticle *);
    void CastingGatherLocSourceSegmentFunc(SLocalSource *);
    BOOL LightningBlastSingleCreate(SSingleParticle *, int);
    BOOL LightningBlastSingleSegment(SSingleParticle *);
    void LightningBlastLocSourceSegmentFunc(SLocalSource *);
    BOOL ZoneExitSingleCreate(SSingleParticle *, int);
    BOOL ZoneExitSingleSegment(SSingleParticle *);
    void ZoneExitLocSourceSegmentFunc(SLocalSource *);
    BOOL PathSingleCreate(SSingleParticle *, int);
    BOOL PathSingleSegment(SSingleParticle *);
    void PathLocSourceSegmentFunc(SLocalSource *);
    BOOL PathDestSingleCreate(SSingleParticle *, int);
    BOOL PathDestSingleSegment(SSingleParticle *);
    void PathDestLocSourceSegmentFunc(SLocalSource *);
    BOOL MoshkaSingleCreate(SSingleParticle *, int);
    BOOL MoshkaSingleSegment(SSingleParticle *);
    void MoshkaLocSourceSegmentFunc(SLocalSource *);
    BOOL PortalStarSingleCreate(SSingleParticle *, int);
    BOOL PortalStarSingleSegment(SSingleParticle *);
    void PortalStarLocSourceSegmentFunc(SLocalSource *);
    BOOL PortalSingleCreate(SSingleParticle *, int);
    BOOL PortalSingleSegment(SSingleParticle *);
    void PortalLocSourceSegmentFunc(SLocalSource *);
    BOOL CylinderSingleCreate(SSingleParticle *, int);
    BOOL CylinderSingleSegment(SSingleParticle *);
    void CylinderLocSourceSegmentFunc(SLocalSource *);
    BOOL StarSingleCreate(SSingleParticle *, int);
    BOOL StarSingleSegment(SSingleParticle *);
    void StarLocSourceSegmentFunc(SLocalSource *);
    BOOL SparksSingleCreate(SSingleParticle *, int);
    BOOL SparksSingleSegment(SSingleParticle *);
    void SparksLocSourceSegmentFunc(SLocalSource *);
    BOOL VStarSingleCreate(SSingleParticle *, int);
    BOOL VStarSingleSegment(SSingleParticle *);
    void VStarLocSourceSegmentFunc(SLocalSource *);
    BOOL RegenerationSingleCreate(SSingleParticle *, int);
    BOOL RegenerationSingleSegment(SSingleParticle *);
    void RegenerationLocSourceSegmentFunc(SLocalSource *);
    BOOL SilenceSingleCreate(SSingleParticle *, int);
    BOOL SilenceSingleSegment(SSingleParticle *);
    void SilenceLocSourceSegmentFunc(SLocalSource *);
    BOOL FeebleMindSingleCreate(SSingleParticle *, int);
    BOOL FeebleMindSingleSegment(SSingleParticle *);
    void FeebleMindLocSourceSegmentFunc(SLocalSource *);
    BOOL FeetCloudSingleCreate(SSingleParticle *, int);
    BOOL FeetCloudSingleSegment(SSingleParticle *);
    void FeetCloudLocSourceSegmentFunc(SLocalSource *);
    BOOL BallStarsSingleCreate(SSingleParticle *, int);
    BOOL BallStarsSingleSegment(SSingleParticle *);
    void BallStarsLocSourceSegmentFunc(SLocalSource *);
    BOOL CurseStarsSingleCreate(SSingleParticle *, int);
    BOOL CurseStarsSingleSegment(SSingleParticle *);
    void CurseStarsLocSourceSegmentFunc(SLocalSource *);
    BOOL StartTransSingleCreate(SSingleParticle *, int);
    BOOL StartTransSingleSegment(SSingleParticle *);
    void StartTransLocSourceSegmentFunc(SLocalSource *);
    BOOL A4BloodSingleCreate(SSingleParticle *, int);
    BOOL A4BloodSingleSegment(SSingleParticle *);
    void A4BloodLocSourceSegmentFunc(SLocalSource *);
};
