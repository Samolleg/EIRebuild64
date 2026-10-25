#pragma once
namespace SaveSchema { class Writer; class Reader; }

#include "Effect.h"
#include "SpellSystem.h"
#include "Basic.h"
/////////////////////////////////////////////////////////////////////////////////////
class CUnit;
class CWorldServer;
/////////////////////////////////////////////////////////////////////////////////////
enum ESpellCastPhase
{
    CP_START,
    CP_DELIVERY,
    CP_ACTION,
    CP_FINISH
};
/////////////////////////////////////////////////////////////////////////////////////
// возможные флажки dwFlags спела (параметры времени исполнения)
const int DW_SPELL_HIDE_CASTER = 1; // подписыватся ли под повреждениями

// --------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------
class CEffectSpell : public CFundament //: public CBase3D
{
    CWorldServer *pWS;
    static CWorldServer *pWSstatic;
    DWORD dwFlags;
    CPlot3D cp;
    // variables characterizing casted spell
    CMapObjectPtr pTarget, pWho;
    CPlot3D ptTarget;
    ESpellCastPhase castPhase;
    // effect visualizing the spell
    CSpell spell;
    CSmartPtr<CEffect> pEffect;
    CSmartPtr<CEffectSound> pEffectSound;
    CSmartPtr<CEffectPointLight> pEffectLight;
    bool bDangerIsMapped; // true if danger area was set on aiMap
    // time to flip spell to next phase
    int nTimeLeft;
    bool bQuiet;

private:
    void SetFlag(DWORD dwF) { dwFlags |= dwF; }
    void ResetFlag(DWORD dwF) { dwFlags &= ~dwF; }
    DWORD CheckFlag(DWORD dwF) { return dwFlags & dwF; }
    void SetCP(const CPlot3D &ptSrc) { cp = ptSrc; }
    const CPlot3D &GetCP() { return cp; }
    //
    float GetRadius() const { return spell.GetRadius(); }
    int GetDuration() const { return spell.GetDuration(); }
    float GetPower() const { return spell.GetPower(); }
    float GetSpeed() const { return spell.GetSpeed(); }
    float GetShowPower();
    float GetAreaPower();
    //
    void RemoveEffect();
    void RemoveMainEffect();
    void RemoveSoundEffect();
    void RemoveLightEffect();
    CEffectWall *GetEffectWall();
    CEffectLightning *GetEffectLightning();
    CEffectParticle *GetEffectParticle();
    CEffectArrow *GetEffectArrow();
    void SetNewEffect(CEffect *pNewEffect);
    void CreateArrow(const CPlot3D &ptSrc, CMapObject *pTarg, ETypeIDs tid, float fpParticleScale,
                     const char *pszSound, const CPlot3D &color, float fpLightRange);
    void CreateLightningToUnit(const CPlot3D &ptSrc, CMapObject *pTarg, CMapObject *pCaster,
                               float fpPower, BYTE bColor);
    void CreateLightning(const CPlot3D &ptSrc, const CPlot3D &ptTarget, float fpScale);
    CEffectParticle *CreateParticles(BOOL bKeepPointer, ETypeIDs tid, const CPlot3D &ptWhere,
                                     float fpScale, float fpPower, const CPlot3D &ptSpeed,
                                     CMapObject *pAttach = NULL);
    void CreateVisionFog(int nTargetID, int nWatcherID);
    void CreateSound(BOOL bKeepPointer, const char *pszSound, const CPlot3D &ptWhere,
                     const CPlot3D &ptSpeed, float fpMinRadius = 10, float fpMaxRadius = 50);
    void CreateSoundSeq(const CPlot3D &ptWhere, const CPlot3D &ptSpeed, const char *pszStartSound,
                        const char *pszCycleSound, int nCycle = -1, float fpMinRadius = 10,
                        float fpMaxRadius = 50);
    void CreateLight(const CPlot3D &ptWhere, const CPlot3D &color, float fpRange,
                     const CPlot3D &ptSpeed);
    BOOL CreateAttackPortion(SRPGAttackPortion &ap);
    const CPlot3D GetLightColor();
    const float GetLightRadius();
    const CPlot3D GetLightSpeed();
    void InflictDamage(SRPGAttackPortion &ap, CUnit *pTarget);
    void InflictWallDamage(const CPlot3D &ptWallDir);
    void InflictDamageInRadius();
    void InflictTargetedDamage();
    bool ModLifeSpellStart(int kind);
    bool ExtendedStart(int nKind);
    bool ExtendedDeliver(int nKind);
    bool ExtendedAction(int nKind);
    void CreateLightningWithColor(const CPlot3D&src,const CPlot3D&dst,float scale,int color);
    friend bool JMV_CheckExtendedSpellLifecycle();
    void StartSpell(int nKind);
    void DeliverSpell(int nKind);
    void ActionSpell(int nKind);
    void FinishSpell(int nKind);
    void AddRemoveDangerArea(int nKind, BOOL bAdd);
    void InformFailure(DWORD dwAction);

public:
    //
    CEffectSpell();
    ~CEffectSpell();
    // returns FALSE if impossible to remove reference on pObject and spell should be deleted
    BOOL RemoveReferencesOn(CMapObject *pObject);
    void TargetSpell(CWorldServer *_pWS, CSpell &_spell, CUnit *pCaster, const CPlot3D &ptSrc,
                     CMapObject *pTarg, const CPlot3D &ptTarg, bool _bQuiet);
    //
    BOOL Segment();
    // load&store
    static void SetCurrentWorld(CWorldServer *_pWS) { pWSstatic = _pWS; }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
public:
    void Store(SaveSchema::Writer &) const;
    void Load(SaveSchema::Reader &);
    void DiscardSave();
public:
    void AdoptSavedState(CWorldServer &);
private:
    bool savedStatePending = false;
    bool savedDangerMapping = false;
};
