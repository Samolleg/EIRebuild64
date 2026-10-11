#include "StdAfx.h"
#include "GameJournal.h"
#include "ModSpellRules.h"
#include "ModSpellOwner.h"
#include "ModSpellDanger.h"
#include "ModSpellTextures.h"
#include "ModRuntime.h"
#include "ModPackage.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "EffectSpell.h"
#include "MapObject.h"
#include "world.h"
#include "Unit.h"
#include "effect.h"
#include "effectManager.h"
#include "WorldServer.h"
#include "aiMap.h"
#include "RandomGen.h"
#include "UnitServer.h"
#include "player.h"
#include "playerServer.h"
#include "PlayerManager.h"
#include "aiSensesConst.h"
#include "ackFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
// максимальное время полета стрелы
const int N_MAX_ARROW_FLY_TIME = 150;
// коэффициент увеличения радиуса источника света для конечного эффекта
const float FP_FINAL_LIGHT_RK = 1.5f;
/////////////////////////////////////////////////////////////////////////////////////
static SAIObjectPlacement dangerArea;
CWorldServer *CEffectSpell::pWSstatic = 0;
/////////////////////////////////////////////////////////////////////////////////////
CEffectSpell::CEffectSpell()
{
    nTimeLeft = 0;
    pWS = 0;
    dwFlags = 0;
    cp = CPlot3D(0, 0, 0);
    bQuiet = false;
    bDangerIsMapped = false;
}
/////////////////////////////////////////////////////////////////////////////////////
CEffectSpell::~CEffectSpell()
{
    if (bDangerIsMapped)
        AddRemoveDangerArea(spell.GetSubtypeID() >> 16, FALSE);
    RemoveEffect();
    ASSERT(!bDangerIsMapped);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectSpell::Segment()
{
    GameJournal::Actor journalActor(pWho.IsEmpty()?-1:pWho->GetID());
    const int journalPhase=castPhase;
    auto*prototype=spell.GetPrototype();
    SpellTextures::Scope textureScope(spell.GetSubtypeID()>>16,prototype?prototype->GetCodeName():nullptr);
    ASSERT(pWS);
    if (!pWS)
        return FALSE;
    BOOL bRv = TRUE;
    int nKind = spell.GetSubtypeID() >> 16;
    switch (castPhase)
    {
    case CP_START:
        StartSpell(nKind);
        break;
    case CP_DELIVERY:
        DeliverSpell(nKind);
        break;
    case CP_ACTION:
        ActionSpell(nKind);
        break;
    default:
        FinishSpell(nKind);
        RemoveEffect();
        bRv = FALSE;
        break;
    }
    if(journalPhase!=castPhase || !bRv) GameJournal::ObserveSpell(pWho, pTarget, prototype?prototype->GetCodeName():"unknown", journalPhase, int(castPhase), ptTarget.x, ptTarget.y, ptTarget.z, spell.IsOffensive());
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::Load(CSave &file)
{
    ASSERT(pWSstatic);
    pWS = pWSstatic;
    DWORD dwParam;
    file.Read(&cp, sizeof(cp));
    pWho.Load(file, *pWS);
    pTarget.Load(file, *pWS);
    file.Read(&ptTarget, sizeof(ptTarget));
    file.Read(&castPhase, sizeof(castPhase));
    spell.Load(file);
    file.Read(dwParam);
    spell.SetSubtypeID(dwParam);
    pEffect = LoadGBSmartPtr<CEffect>(file, pWS->GetEffectManager());
    pEffectSound = LoadGBSmartPtr<CEffectSound>(file, pWS->GetEffectManager());
    pEffectLight = LoadGBSmartPtr<CEffectPointLight>(file, pWS->GetEffectManager());
    file.Read(nTimeLeft);
    file.Read(dwFlags);
    file.Read(&cp, sizeof(cp));

    int nKind = spell.GetSubtypeID() >> 16;
    if(ModRuntime::Current())
        ModSpellDanger::ReconstructNativeLoad(nKind,!pEffect.IsEmpty(),[&](int stockKind){AddRemoveDangerArea(stockKind,TRUE);});
    else
        AddRemoveDangerArea(nKind, TRUE);
    if(SpellTextures::HasBindings()&&!pEffect.IsEmpty()){auto*prototype=spell.GetPrototype();if(prototype)SpellTextures::RecordOwner(pWS->GetEffectManager(),pEffect.GetPtr(),prototype->GetCodeName());}
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::Store(CSave &file)
{
    file.Write(&cp, sizeof(cp));
    pWho.Store(file);
    pTarget.Store(file);
    file.Write(&ptTarget, sizeof(ptTarget));
    file.Write(&castPhase, sizeof(castPhase));
    spell.Store(file);
    file.Write(spell.GetSubtypeID());
    StoreGBPtr(file, pEffect);
    StoreGBPtr(file, pEffectSound);
    StoreGBPtr(file, pEffectLight);
    file.Write(nTimeLeft);
    file.Write(dwFlags);
    file.Write(&cp, sizeof(cp));
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! System routines
/////////////////////////////////////////////////////////////////////////////////////
// возвращает силу эффекта в диапазоне 0.8-1.8
float CEffectSpell::GetShowPower()
{
    // return 1.8f;
    float fpS1 = spell.GetPrototype()->fpEffect;
    if (fpS1 == 0)
        return 1;
    float fpScale = spell.GetPower() / fpS1;
    fpScale = Max(1.0f, fpScale);
    fpScale = Min(8.0f, fpScale);
    return exp(log(fpScale) * 0.33f) - 0.2f;
}
/////////////////////////////////////////////////////////////////////////////////////
// возвращает увеличение площади поражения в диапозоне 0.8-1.8
float CEffectSpell::GetAreaPower()
{
    float fpS1 = spell.GetPrototype()->fpArea;
    float fpS2 = FP_PI * sqr(spell.GetRadius());
    float fpScale = fpS2 / fpS1;
    fpScale = Max(1.0f, fpScale);
    fpScale = Min(8.0f, fpScale);
    return exp(log(fpScale) * 0.33f) - 0.2f;
}
/////////////////////////////////////////////////////////////////////////////////////
const CPlot3D CEffectSpell::GetLightColor()
{
    const SSDBSpellPrototype *pProto = spell.GetPrototype();
    return CPlot3D(pProto->fLightR, pProto->fLightG, pProto->fLightB);
}
/////////////////////////////////////////////////////////////////////////////////////
const float CEffectSpell::GetLightRadius()
{
    const SSDBSpellPrototype *pProto = spell.GetPrototype();
    return pProto->fLightRadius;
}
/////////////////////////////////////////////////////////////////////////////////////
const CPlot3D CEffectSpell::GetLightSpeed()
{
    const SSDBSpellPrototype *pProto = spell.GetPrototype();
    return CPlot3D(0, 0, GetLightRadius() / Max(1.0f, pProto->fFadeoutTime));
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::TargetSpell(CWorldServer *_pWS, CSpell &_spell, CUnit *pCaster,
                               const CPlot3D &ptSrc, CMapObject *pTarg, const CPlot3D &ptTarg,
                               bool _bQuiet)
{
    _spell.Duplicate(&spell);
    pWS = _pWS;
    cp = ptSrc;
    pWho = pCaster;
    pTarget = pTarg;
    ptTarget = ptTarg;
    castPhase = CP_START;
    bQuiet = _bQuiet;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::RemoveEffect()
{
    RemoveMainEffect();
    RemoveSoundEffect();
    RemoveLightEffect();
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::RemoveMainEffect()
{
    if (!pEffect.IsEmpty())
        pWS->RemoveEffect(pEffect->GetID());
    pEffect = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::RemoveSoundEffect()
{
    if (!pEffectSound.IsEmpty())
        pWS->RemoveEffect(pEffectSound->GetID());
    pEffectSound = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::RemoveLightEffect()
{
    if (!pEffectLight.IsEmpty())
        pWS->RemoveEffect(pEffectLight->GetID());
    pEffectLight = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
CEffectWall *CEffectSpell::GetEffectWall()
{
    ASSERT(!pEffect.IsEmpty() && pEffect->WhoAmI() == HPC_EFFECTWALL);
    return (CEffectWall *)pEffect.GetPtr();
}
/////////////////////////////////////////////////////////////////////////////////////
CEffectLightning *CEffectSpell::GetEffectLightning()
{
    ASSERT(!pEffect.IsEmpty() && pEffect->WhoAmI() == HPC_EFFECTLIGHTNING);
    return (CEffectLightning *)pEffect.GetPtr();
}
/////////////////////////////////////////////////////////////////////////////////////
CEffectParticle *CEffectSpell::GetEffectParticle()
{
    ASSERT(!pEffect.IsEmpty() && pEffect->WhoAmI() == HPC_EFFECTPARTICLE);
    return (CEffectParticle *)pEffect.GetPtr();
}
/////////////////////////////////////////////////////////////////////////////////////
CEffectArrow *CEffectSpell::GetEffectArrow()
{
    ASSERT(!pEffect.IsEmpty() && pEffect->WhoAmI() == HPC_EFFECTARROW);
    return (CEffectArrow *)pEffect.GetPtr();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectSpell::RemoveReferencesOn(CMapObject *pObject)
{
    if (pTarget == pObject)
        return FALSE;
    if (pWho == pObject)
        pWho = 0;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::SetNewEffect(CEffect *pNewEffect)
{
    if (pEffect)
        pWS->RemoveEffect(pEffect->GetID());
    pEffect = pNewEffect;
    pWS->AddEffect(pEffect);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::InformFailure(DWORD dwAction)
{
    if (pWho.IsEmpty() || pWho->WhoAmI() != HPC_UNIT)
        return;
    ((CUnitServer *)((CUnit *)pWho)->GetUnitSpecific())->InformFailure(dwAction);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::AddRemoveDangerArea(int nKind, BOOL bAdd)
{
    if(ModRuntime::Current()){nKind=ModSpells::ElementalBase(nKind);if(nKind==ModSpellIdentity::NativeTestWall)nKind=SPELL_ID_FIREWALL;}
    if(!ModSpellDanger::ChangeNeeded(bAdd!=FALSE,bDangerIsMapped))return;
    CPlot3D ptDir;
    if (!pEffect)
        return;
    switch (nKind)
    {
    case SPELL_ID_POISONFOG:
        dangerArea.CreateDangerCircle(pWS->aiMap, GetCP(), GetRadius());
        break;
    case SPELL_ID_FIREWALL:
        ptDir = GetEffectWall()->ptWallDirection;
        dangerArea.CreateDangerWall(pWS->aiMap, GetCP(), 0.5f, ptDir);
        break;
    case SPELL_ID_WALL_LGHTNG:
        ptDir = (GetEffectLightning()->ptDst - GetEffectLightning()->ptSrc) * 0.5f;
        dangerArea.CreateDangerWall(pWS->aiMap, GetCP(), 0.5f, ptDir);
        break;
    case SPELL_ID_CAMPFIRE:
        dangerArea.CreateDangerCircle(pWS->aiMap, GetCP(), GetRadius());
        break;
    default:
        return;
    }
    ModSpellDanger::Change(bAdd!=FALSE,bDangerIsMapped,[&](bool add){
        if(add){
            ASSERT(!bDangerIsMapped);
            pWS->aiMap.AddDangerArea(pWS->GetSegmentTimeCounter(), dangerArea);
        }else{
            ASSERT(bDangerIsMapped);
            pWS->aiMap.RemoveDangerArea(dangerArea);
        }
    });
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Create different effects
/////////////////////////////////////////////////////////////////////////////////////
// Loaded object references can be empty after their target has disappeared.
// Use the existing FINISH path; casterless and position-targeted spells remain valid.
static bool SpellTargetMissing(CMapObject* target, DWORD id, int phase)
{
    if (target)
        return false;
    GameJournal::Event("SPELL", "target_missing", "id=%u kind=%u phase=%d", id, id >> 16, phase);
    return true;
}
void CEffectSpell::CreateArrow(const CPlot3D &ptSrc, CMapObject *pTarg, ETypeIDs tid,
                               float fpParticleScale, const char *pszSound, const CPlot3D &color,
                               float fpLightRange)
{
    if (SpellTargetMissing(pTarg, spell.GetSubtypeID(), castPhase))
    {
        castPhase = CP_FINISH;
        return;
    }
    CEffectArrow *pArrow;
    pArrow = (CEffectArrow *)((CEffectManager *)pWS->GetEffectManager())
                 ->CreateData(ETypeIDs(HPC_EFFECTARROW));
    if (!pArrow)
        return;
    pArrow->color = color;
    pArrow->fpRange = fpLightRange;
    pArrow->SetCP(ptSrc);
    pArrow->SetTarget(pTarg->GetID());
    pArrow->typeID = tid;
    pArrow->fpScale = fpParticleScale;
    pArrow->szSoundName = pszSound;
    if (!pWho.IsEmpty())
    {
        pArrow->nAttackerID = pWho->GetID();
        pArrow->bNewArrow = 1;
    }
    SetNewEffect(pArrow);
}
/////////////////////////////////////////////////////////////////////////////////////
CEffectParticle *CEffectSpell::CreateParticles(BOOL bKeepPointer, ETypeIDs tid,
                                               const CPlot3D &ptWhere, float fpScale, float fpPower,
                                               const CPlot3D &ptSpeed, CMapObject *pAttach)
{
    CEffectParticle *pParticle;
    if (tid == PTID_FIREWALL || tid == PTID_LINK || tid == PTID_FIREWALLDARK || tid == PTID_FIREWALLLIGHT)
        pParticle = (CEffectParticle *)((CEffectManager *)pWS->GetEffectManager())
                        ->CreateData(ETypeIDs(HPC_EFFECTWALL));
    else
        pParticle = (CEffectParticle *)((CEffectManager *)pWS->GetEffectManager())
                        ->CreateData(ETypeIDs(HPC_EFFECTPARTICLE));
    pParticle->typeID = tid;
    pParticle->SetCP(ptWhere);
    pParticle->fpScale = fpScale;
    pParticle->SetSpeed(ptSpeed);
    pParticle->fpPower = fpPower;
    if (pAttach)
        pParticle->Attach(pAttach->GetID());
    if (bKeepPointer)
        SetNewEffect(pParticle);
    else
        pWS->AddEffect(pParticle);
    return pParticle;
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::CreateVisionFog(int nTargetID, int nWatcherID)
{
    CEffectShowVisibility *pParticle;
    pParticle = (CEffectShowVisibility *)((CEffectManager *)pWS->GetEffectManager())
                    ->CreateData(ETypeIDs(HPC_EFFECTSHOWVISIBILITY));
    pParticle->SetNewParams(nTargetID, nWatcherID);
    pParticle->SetCP(CPlot3D(0, 0, 0));
    SetNewEffect(pParticle);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::CreateLightningToUnit(const CPlot3D &ptSrc, CMapObject *pTarg,
                                         CMapObject *pCaster, float fpPower, BYTE bColor)
{
    CPlot3D ptTarg;
    CEffectLightning *pLightning;
    if (!pTarg)
        return;
    ptTarg = pTarg->GetCP();
    ptTarg.z = pTarg->bv.ptMax.z * 1.2f + pWS->GetZValue(ptTarg.x, ptTarg.y);
    pLightning = (CEffectLightning *)((CEffectManager *)pWS->GetEffectManager())
                     ->CreateData(ETypeIDs(HPC_EFFECTLIGHTNING));
    pLightning->SetParams(ptSrc, ptTarg, 5 * fpPower);
    int nCasterID = pCaster ? pCaster->GetID() : -1;
    int nTargetID = pTarg ? pTarg->GetID() : -1;
    pLightning->Attach(nCasterID, nTargetID);
    pLightning->SetColor(bColor);
    SetNewEffect(pLightning);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::CreateLightning(const CPlot3D &ptSrc, const CPlot3D &ptTarget, float fpScale)
{
    CEffectLightning *pLightning;
    pLightning = (CEffectLightning *)((CEffectManager *)pWS->GetEffectManager())
                     ->CreateData(ETypeIDs(HPC_EFFECTLIGHTNING));
    pLightning->SetParams(ptSrc, ptTarget, fpScale);
    SetNewEffect(pLightning);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::CreateSound(BOOL bKeepPointer, const char *pszSound, const CPlot3D &ptWhere,
                               const CPlot3D &ptSpeed, float fpMinRadius, float fpMaxRadius)
{
    CEffectSound *pSound;
    pSound = (CEffectSound *)((CEffectManager *)pWS->GetEffectManager())
                 ->CreateData(ETypeIDs(HPC_EFFECTSOUND));
    pSound->SetCP(ptWhere);
    pSound->fpMinRadius = fpMinRadius;
    pSound->fpMaxRadius = fpMaxRadius;
    pSound->bHighPriority = true;
    ASSERT(strlen(pszSound) < 255);
    strcpy(pSound->szSoundName, pszSound);
    pSound->SetSpeed(ptSpeed);
    pSound->bLooping = bKeepPointer;
    if (bKeepPointer)
    {
        if (pEffectSound)
            pWS->RemoveEffect(pEffectSound->GetID());
        pEffectSound = pSound;
    }
    pWS->AddEffect(pSound);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::CreateSoundSeq(const CPlot3D &ptWhere, const CPlot3D &ptSpeed,
                                  const char *pszStartSound, const char *pszCycleSound, int nCycle,
                                  float fpMinRadius, float fpMaxRadius)
{
    CEffectSoundSeq *pSound;
    CString szTemp;
    pSound = (CEffectSoundSeq *)((CEffectManager *)pWS->GetEffectManager())
                 ->CreateData(ETypeIDs(HPC_EFFECTSOUNDSEQ));
    pSound->SetCP(ptWhere);
    pSound->SetSpeed(ptSpeed);
    pSound->fpMinRadius = fpMinRadius;
    pSound->fpMaxRadius = fpMaxRadius;
    pSound->bHighPriority = true;
    pSound->szStartSound = pszStartSound;
    if (nCycle > 0)
    {
        for (int i = 0; i < nCycle; i++)
        {
            szTemp.Format(pszCycleSound, i + 1);
            pSound->szSounds.Add(szTemp);
        }
    }
    else
        pSound->szSounds.Add(pszCycleSound);
    if (pEffectSound)
        pWS->RemoveEffect(pEffectSound->GetID());
    pEffectSound = (CEffectSound *)pSound;
    pWS->AddEffect(pSound);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::CreateLight(const CPlot3D &ptWhere, const CPlot3D &color, float fpRange,
                               const CPlot3D &ptSpeed)
{
    CEffectPointLight *pLight;
    pLight = (CEffectPointLight *)((CEffectManager *)pWS->GetEffectManager())
                 ->CreateData(ETypeIDs(HPC_EFFECTPOINTLIGHT));
    pLight->color = color;
    pLight->SetCP(ptWhere);
    pLight->fpRange = fpRange;
    pLight->SetSpeed(ptSpeed);
    pLight->dwFlags = LIT_MAGICSHADOW | LIT_SPECULAR;
    if (pEffectLight)
        pWS->RemoveEffect(pEffectLight->GetID());
    pEffectLight = pLight;
    pWS->AddEffect(pLight);
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Spell lifetime cycle
/////////////////////////////////////////////////////////////////////////////////////
#include "ModLifeSpell.inl"
// The mode-6 ASI dispatcher changes the live spell immediately before delivery.
// Keep its x87 operation order and the caller's control word, including FISTP.
static void PrepareControlMagic(int kind, CSpell& spell, CMapObject* caster, CMapObject* target)
{
    const auto* mod = ModRuntime::Current();
    if (!mod || mod->SpellAddon().Feature15() != 6)
        return;
    using ByteKey = ModSpellAddon::ByteKey;
    using ScalarKey = ModSpellAddon::ScalarKey;
    struct Rule { int kind; ByteKey flags; ScalarKey base; };
    static const Rule rules[] = {
        { SPELL_ID_PROT_FIRE, ByteKey::gameplay_prot_fire, ScalarKey::gameplay_bprot_fire },
        { SPELL_ID_PROT_ELECTRIC, ByteKey::gameplay_prot_electro, ScalarKey::gameplay_bprot_electro },
        { SPELL_ID_PROT_ACID, ByteKey::gameplay_prot_acid, ScalarKey::gameplay_bprot_acid },
        { SPELL_ID_EAGLE_SIGHT, ByteKey::gameplay_eagle_sight, ScalarKey::gameplay_beagle_sight },
        { SPELL_ID_INFRAVISION, ByteKey::gameplay_infravision, ScalarKey::gameplay_binfravision },
        { SPELL_ID_DETECT_LIFE, ByteKey::gameplay_detect_life, ScalarKey::gameplay_bdetect_life },
        { SPELL_ID_INVISIBILITY, ByteKey::gameplay_invisibility, ScalarKey::gameplay_binvisibility },
        { SPELL_ID_SILENCE, ByteKey::gameplay_silence, ScalarKey::gameplay_bsilence },
        { SPELL_ID_LICHDOM, ByteKey::gameplay_lichdom, ScalarKey::gameplay_blichdom },
        { SPELL_ID_STENCH, ByteKey::gameplay_stench, ScalarKey::gameplay_bstench },
        { SPELL_ID_STUN, ByteKey::gameplay_stun, ScalarKey::gameplay_bstun },
        { SPELL_ID_ANTIMAGIC, ByteKey::gameplay_antimagic, ScalarKey::gameplay_bantimagic },
        { SPELL_ID_STRENGTHEN, ByteKey::gameplay_strength, ScalarKey::gameplay_bstrength },
        { SPELL_ID_WEAKEN, ByteKey::gameplay_weak, ScalarKey::gameplay_bweak },
        { SPELL_ID_REGENERATION, ByteKey::gameplay_regeneration, ScalarKey::gameplay_bregeneration },
        { SPELL_ID_FEEBLEMIND, ByteKey::gameplay_feeblemind, ScalarKey::gameplay_bfeeblemind },
        { SPELL_ID_SPEED, ByteKey::gameplay_speed, ScalarKey::gameplay_bspeed },
        { SPELL_ID_SLOW, ByteKey::gameplay_slow, ScalarKey::gameplay_bslow },
        { SPELL_ID_ENLARGE, ByteKey::gameplay_enlarge, ScalarKey::gameplay_benlarge },
        { SPELL_ID_SHRINK, ByteKey::gameplay_shrink, ScalarKey::gameplay_bshrink },
    };
    for (const auto& rule : rules)
    {
        if (rule.kind != kind)
            continue;
        const auto& profile = mod->SpellAddon();
        const int flags = profile.Mode(rule.flags);
        if (flags != 11 && flags != 19)
            return; // Other dispatcher flags/modes are outside this transfer.

        // GetEffect exposes a const view; the owned spell itself is mutable.
        // Mutate its existing fields so GetShowPower/Store see the ASI result.
        auto& effect = const_cast<CSpellEffect&>(spell.GetEffect());
        if (flags == 11 && caster && target &&
            caster->WhoAmI() == HPC_UNIT && target->WhoAmI() == HPC_UNIT)
        {
            auto* source = static_cast<CUnit*>(caster);
            auto* destination = static_cast<CUnit*>(target);
            auto* sourceServer = dynamic_cast<CUnitServer*>(source->GetUnitSpecific());
            auto* destinationServer = dynamic_cast<CUnitServer*>(destination->GetUnitSpecific());
            if (sourceServer && destinationServer)
            {
                const int sourceLevel = source->GetLevel();
                const int targetLevel = destination->GetLevel();
                const float sourceInt = sourceServer->GetRPGStats()->psIntelligence.fpEffectiveValue;
                const float targetInt = destinationServer->GetRPGStats()->psIntelligence.fpEffectiveValue;
                const float levelCoeff = profile.Scalar(ScalarKey::gameplay_lvldurcoeff);
                const float intCoeff = profile.Scalar(ScalarKey::gameplay_durcoeff);
                const float one = 1.0f;
                const float zero = 0.0f;
                long duration = effect.nDuration;
                __asm
                {
                    fild sourceLevel
                    fild targetLevel
                    fsubp st(1), st(0)
                    fmul levelCoeff
                    fild duration
                    fmul st(1), st(0)
                    fld targetInt
                    fsubr sourceInt
                    fmul intCoeff
                    fmul st(0), st(1)
                    faddp st(1), st(0)
                    faddp st(1), st(0)
                    fcom one
                    fnstsw ax
                    test ah, 1
                    jz storeDuration
                    fstp st(0)
                    fld zero
                storeDuration:
                    fistp duration
                }
                effect.nDuration = duration;
            }
        }
        // Flags 19 skip duration; flags 11 with no caster do so as well.
        const float base = profile.Scalar(rule.base);
        float power = effect.fpPower;
        __asm
        {
            fld power
            fadd base
            fstp power
        }
        effect.fpPower = power;
        return;
    }
}
// Mode 3 installs the shared f07/f08/f09/f10 dispatcher. Subkeys do not
// independently change the installed ASI image (R30).
static bool NewSpellsEnabled()
{
    const auto* mod = ModRuntime::Current();
    return mod && mod->SpellAddon().Feature07() == 3;
}
static void RestoreInspirationMana(float& current, float maximum, float power)
{
    // The ASI stores the sum as float32 before the upper-bound comparison.
    // Keep the ordered comparison: NaN and negative values have no lower clamp.
    volatile float sum = current + power;
    current = sum > maximum ? maximum : sum;
}
void CEffectSpell::StartSpell(int nKind)
{
    if(ModLifeSpellStart(nKind)||ExtendedStart(nKind))return;
    const bool newSpells = NewSpellsEnabled();
    if (newSpells && (nKind == SPELL_ID_CHARM || nKind == SPELL_ID_ENSORCELL))
    {
        // Shared typed Charm36/42; do not reproduce the ASI's stack/FPU faults.
        if (!pTarget.IsEmpty() && pTarget->WhoAmI() == HPC_UNIT &&
            !pWho.IsEmpty() && pWho->WhoAmI() == HPC_UNIT)
        {
            auto* target = (CUnit*)pTarget;
            auto* caster = (CUnit*)pWho;
            bool success = target->GetOwner() == caster->GetOwner();
            if (!success)
            {
                auto* stats = ((CUnitServer*)target->GetUnitSpecific())->GetRPGStats();
                success = stats->GetManualSkillLevel(RPGMSKL_TAME, target) < GetPower();
                if (success)
                {
                    target->SetEnemyMask(0);
                    target->SetPlayerNum(0);
                }
                else
                    InformFailure(ACK_HERO_REFUSE_SPELL_CHARM);
            }
            if (success)
                CreateSound(FALSE, "magic\\Charm\\start.wav", pTarget->GetCP(), CPlot3D(0, 0, 0));
        }
        castPhase = CP_FINISH;
        AddRemoveDangerArea(nKind, TRUE);
        return;
    }
    if (newSpells && nKind >= SPELL_ID_GAMECHANGER && nKind <= SPELL_ID_MULTILIT)
    {
        CPlayerIterator iterator;
        CPlayer* first = iterator.GetFirst((CPlayerManager*)pWS->GetPlayerManager());
        if (first)
        {
            auto* server = (CPlayerServer*)first->GetPlayerSpecific();
            server->SetVar("S3x", ptTarget.x);
            server->SetVar("S4y", ptTarget.y);
            server->SetVar("S0e", GetPower());
            server->SetVar("S2t", float(short(nKind)));
            server->SetVar("S1d", float(GetDuration()));
            // Preserve the ASI reset; do not add the preceding global-time interval.
            first->nTimeWasAddedSegment = pWS->GetSegmentTimeCounter();
        }
        else
            GameJournal::Event("SPELL", "script_spell_no_player", "id=%d", nKind);
        castPhase = CP_FINISH;
        AddRemoveDangerArea(nKind, TRUE);
        return;
    }
    float fpDist;
    CPlot3D ptSpeed, ptTarg, ptSrc;
    ResetFlag(DW_SPELL_HIDE_CASTER);
    if (pWho.IsEmpty())
        ptSrc = GetCP();
    else
        ptSrc = ((CUnit *)pWho)->GetUnitHeadPosition();
    switch (nKind)
    {
    case SPELL_ID_RICKMAGIC:
    case SPELL_ID_FIREARROW:
        if (pTarget == (CMapObject *)nullptr || pTarget->WhoAmI() != HPC_UNIT)
        {
            castPhase = CP_FINISH;
            break;
        }
        nTimeLeft = N_MAX_ARROW_FLY_TIME;
        if (nKind == SPELL_ID_FIREARROW)
            CreateArrow(ptSrc, pTarget, PTID_FIREARROW, GetShowPower(),
                        "magic\\Firearrow\\circle.wav", GetLightColor(), GetLightRadius());
        else
            CreateArrow(ptSrc, pTarget, PTID_RICKARROW, GetShowPower(),
                        "magic\\Firearrow\\circle.wav", GetLightColor(), GetLightRadius());
        CreateSound(FALSE, "magic\\Firearrow\\start.wav", GetCP(), CPlot3D(0, 0, 0));
        castPhase = CP_DELIVERY;
        break;
    case SPELL_ID_ACIDRAY:
        if (pTarget == (CMapObject *)nullptr || pTarget->WhoAmI() != HPC_UNIT)
        {
            castPhase = CP_FINISH;
            break;
        }
        nTimeLeft = N_MAX_ARROW_FLY_TIME;
        CreateArrow(ptSrc, pTarget, PTID_ACIDRAY, 0.4f * GetShowPower(),
                    "magic\\Acidray\\circle.wav", GetLightColor(), GetLightRadius());
        CreateSound(FALSE, "magic\\Acidray\\start.wav", GetCP(), CPlot3D(0, 0, 0));
        castPhase = CP_DELIVERY;
        break;
    case SPELL_ID_LIGHTNING:
    case SPELL_ID_CURSEMAGIC:
        if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
        {
            castPhase = CP_FINISH;
            break;
        }
        CreateLightningToUnit(ptSrc, pTarget, pWho, GetShowPower(), nKind == SPELL_ID_CURSEMAGIC);
        CreateSound(FALSE, "magic\\lightning\\start.wav", GetCP(), CPlot3D(0, 0, 0));
        CreateLight(pTarget->GetUnitHeadPosition(), GetLightColor(), GetLightRadius(),
                    CPlot3D(0, 0, 0));
        InflictTargetedDamage();
        nTimeLeft = 7;
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_FIRERAZE:
    case SPELL_ID_FIREBALL: {
        if (nKind == SPELL_ID_FIRERAZE && !newSpells) { castPhase = CP_FINISH; break; }
        float fpSpeed = GetSpeed();
        ptSpeed = ptTarget - GetCP();
        ptSpeed.z = 0;
        fpDist = fabs(ptSpeed);
        // for reasons of safe operation with incorrect data
        if (fpSpeed <= 0)
            fpSpeed = 5;
        nTimeLeft = Float2Int(fpDist / fpSpeed * 15.0f) - 2;
        ptSpeed *= fpSpeed / 15.0f / fabs(ptSpeed);
        CreateParticles(TRUE, PTID_FIREBALL, ptSrc, 0.3f * GetShowPower(), 1,
                        ptSpeed); // create fireball effect
        if (!pWho.IsEmpty())
            GetEffectParticle()->nStartID = pWho->GetID();
        ptTarget.z = pWS->GetSurfaceValue(ptTarget.x, ptTarget.y) + 1;
        GetEffectParticle()->SetDestination(ptTarget);
        if (fpDist < 1.25f * fpSpeed)
            CreateSound(FALSE, "magic\\FireBall\\start.wav", GetCP(), ptSpeed);
        else
            CreateSoundSeq(GetCP(), ptSpeed, "magic\\Fireball\\start.wav",
                           "magic\\Fireball\\circle.wav");
        CreateLight(ptSrc, GetLightColor(), GetLightRadius(), ptSpeed);
        castPhase = CP_DELIVERY;
    }
    break;
    case SPELL_ID_PANIC:
    case SPELL_ID_INVOKE_LGHTNG:
        if (nKind == SPELL_ID_PANIC && !newSpells) { castPhase = CP_FINISH; break; }
        ptTarget.z = -3 + pWS->GetSurfaceValue(ptTarget.x, ptTarget.y);
        SetCP(ptTarget);
        ptSrc = GetCP();
        ptSrc.z += 30;
        CreateLightning(ptSrc, GetCP(), -7 * GetShowPower());
        CreateParticles(FALSE, PTID_LIGHTNINGBLAST, ptTarget, GetRadius(), GetShowPower(),
                        CPlot3D(0, 0, 0));
        CreateSound(FALSE, "magic\\InvokeLight\\start.wav", GetCP(), CPlot3D(0, 0, 0), 1000, 1000);
        ptTarget.z += 5;
        CreateLight(ptTarget, GetLightColor(), GetLightRadius(), GetLightSpeed());
        InflictDamageInRadius();
        nTimeLeft = 7;
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_STORM:
    case SPELL_ID_COLUMN_ACID:
        if (nKind == SPELL_ID_STORM && !newSpells) { castPhase = CP_FINISH; break; }
        SetCP(ptTarget);
        CreateParticles(FALSE, PTID_NUKE, ptTarget, 1, 1, CPlot3D(0, 0, 0)); // nuke explosion
        CreateSound(FALSE, "magic\\Acidcolumn\\start.wav", ptTarget, CPlot3D(0, 0, 0));
        CreateLight(ptTarget, GetLightColor(), GetLightRadius(), GetLightSpeed());
        nTimeLeft = 30;
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_FIREWALL: {
        ptTarg = ptTarget;
        CreateParticles(TRUE, PTID_FIREWALL, ptTarg, GetShowPower(), GetShowPower(),
                        CPlot3D(0, 0, 0)); // fire wall effect

        CPlot3D ptDir = ptTarg - ptSrc;
        if (ptDir.x == 0 && ptDir.y == 0)
            ptDir.x = 1;
        ptDir = CPlot3D(-ptDir.y, ptDir.x, 0);
        ptDir *= GetRadius() / fabs(ptDir);

        if (!pEffect.IsEmpty())
            GetEffectWall()->SetWallVector(ptDir);

        SetCP(ptTarg);
        nTimeLeft = GetDuration();
        castPhase = CP_ACTION;
        ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
        CreateLight(ptTarg, GetLightColor(), GetRadius() + GetLightRadius(), CPlot3D(0, 0, 0));
        CreateSoundSeq(ptTarg, CPlot3D(0, 0, 0), "magic\\Firewall\\start.wav",
                       "magic\\Firewall\\circle.wav");
    }
    break;
    case SPELL_ID_WALL_LGHTNG: {
        ptTarg = ptTarget;
        CPlot3D ptDir = ptTarg - ptSrc;
        if (ptDir.x == 0 && ptDir.y == 0)
            ptDir.x = 1;
        ptDir = CPlot3D(-ptDir.y, ptDir.x, 0);
        ptDir *= GetRadius() / fabs(ptDir);

        CPlot3D ptLSrc = ptTarget - ptDir;
        CPlot3D ptLDst = ptTarget + ptDir;
        ptLSrc.z = pWS->GetSurfaceValue(ptLSrc.x, ptLSrc.y) + 1;
        ptLDst.z = pWS->GetSurfaceValue(ptLDst.x, ptLDst.y) + 1;
        CreateLightning(ptLSrc, ptLDst, 0);

        SetCP(ptTarg);
        nTimeLeft = GetDuration();
        castPhase = CP_ACTION;
        ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
        CreateLight(ptTarg, GetLightColor(), GetRadius() + GetLightRadius(), CPlot3D(0, 0, 0));
        CreateSoundSeq(ptTarg, CPlot3D(0, 0, 0), "", "magic\\Lightwall\\circle.wav");
    }
    break;
    case SPELL_ID_POISONFOG:
        ptTarg = ptTarget;
        ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
        CreateParticles(TRUE, PTID_POISONFOG, ptTarg, GetRadius(), GetShowPower(),
                        CPlot3D(0, 0, 0)); // poison cloud
        CreateSoundSeq(ptTarg, CPlot3D(0, 0, 0), "magic\\poison\\start.wav",
                       "magic\\poison\\circle.wav");
        CreateLight(ptTarg, GetLightColor(), GetRadius() + GetLightRadius(), CPlot3D(0, 0, 0));
        SetCP(ptTarg);
        nTimeLeft = GetDuration();
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_CAMPFIRE:
        ptTarg = ptTarget;
        ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
        SetCP(ptTarg);
        nTimeLeft = GetDuration();
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_PROT_FIRE:
    case SPELL_ID_PROT_ELECTRIC:
    case SPELL_ID_PROT_ACID:
    case SPELL_ID_EAGLE_SIGHT:
    case SPELL_ID_INFRAVISION:
    case SPELL_ID_DETECT_LIFE:
    case SPELL_ID_INVISIBILITY:
    case SPELL_ID_SILENCE:
    case SPELL_ID_LICHDOM:
    case SPELL_ID_STRENGTHEN:
    case SPELL_ID_WEAKEN:
    case SPELL_ID_REGENERATION:
    case SPELL_ID_FEEBLEMIND:
    case SPELL_ID_SPEED:
    case SPELL_ID_SLOW:
    case SPELL_ID_STUN:
    case SPELL_ID_STENCH:
    case SPELL_ID_ANTIMAGIC:
    case SPELL_ID_ENLARGE:
    case SPELL_ID_SHRINK:
        if (!pTarget.IsEmpty())
        {
            PrepareControlMagic(nKind, spell, pWho, pTarget);
            CSpellEffect effect;
            spell.GetSpellEffect(effect);
            effect.nVisStrength = GetShowPower() * 0x10;
            effect.bQuiet = bQuiet;
            pTarget->AddSpellEffect(effect);
        }
        castPhase = CP_FINISH;
        break;
    case SPELL_ID_FIREWORKS:
        ptTarg = ptTarget;
        ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
        CreateParticles(TRUE, PTID_GEYSER, ptTarg, GetShowPower(), 1, CPlot3D(0, 0, 0));
        CreateLight(ptTarg, GetLightColor(), GetLightRadius(), CPlot3D(0, 0, 0));
        CreateSoundSeq(ptTarg, CPlot3D(0, 0, 0), "", "magic\\Firework\\%i.wav", 5);
        SetCP(ptTarg);
        nTimeLeft = GetDuration();
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_CLAIRVOYENCE:
        ptTarg = ptTarget;
        ptTarg.z = 1 + pWS->GetZValue(ptTarg.x, ptTarg.y);
        SetCP(ptTarg);
        CreateParticles(FALSE, PTID_CLAYRING, ptTarget, GetRadius(), 1, CPlot3D(0, 0, 0));
        CreateLight(ptTarg, GetLightColor(), GetRadius(), CPlot3D(0, 0, 0));
        CreateSound(FALSE, "magic\\Clairvoyence\\start.wav", GetCP(), CPlot3D(0, 0, 0));
        nTimeLeft = GetDuration();
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_LINK:
        if (pTarget == (CMapObject *)nullptr || pTarget->WhoAmI() != HPC_UNIT ||
            pWho == (CMapObject *)nullptr || pWho->WhoAmI() != HPC_UNIT)
        {
            castPhase = CP_FINISH;
            break;
        }
        if (pTarget->IsMoving() || pWho->IsMoving() || pTarget->IsDead() ||
            (CMapObject *)pTarget == (CMapObject *)pWho)
        {
            InformFailure(ACK_HERO_REFUSE_SPELL_LINK);
            castPhase = CP_FINISH;
            break;
        }
        ptSrc = ((CUnit *)pWho)->GetUnitHeadPosition();
        SetCP(ptSrc);
        CreateParticles(TRUE, PTID_LINK, ptSrc, 1, 1, CPlot3D(0, 0, 0)); // fire wall effect
        if (pEffect)
        {
            CPlot3D ptDir = ((CUnit *)pTarget)->GetUnitHeadPosition() - ptSrc;
            GetEffectWall()->SetWallVector(ptDir);
        }
        CreateSoundSeq(ptSrc, CPlot3D(0, 0, 0), "magic\\Link\\start.wav",
                       "magic\\Link\\circle.wav");
        nTimeLeft = GetDuration();
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_TELEPORT:
        if (pWho.IsEmpty())
            ptTarg = GetCP();
        else
            ptTarg = pWho->GetCP();
        ptTarg.z += pWS->GetZValue(ptTarg.x, ptTarg.y);
        CreateParticles(TRUE, PTID_TELEPORT, ptTarg, 1, 1, CPlot3D(0, 0, 0));
        ptTarg = ptTarget;
        ptTarg.z = pWS->GetZValue(ptTarg.x, ptTarg.y) - 0.2f;
        CreateParticles(FALSE, PTID_TELEPORT, ptTarg, -1, 1, CPlot3D(0, 0, 0));
        CreateSound(FALSE, "magic\\Teleport\\start.wav", ptTarg, CPlot3D(0, 0, 0));
        CreateLight(ptTarg, GetLightColor(), GetLightRadius(),
                    CPlot3D(0, 0, 0)); // GetLightSpeed() );
        nTimeLeft = (ModRuntime::Current() && (ModRuntime::Current()->SpellAddon().Feature22() == 3 || ModRuntime::Current()->CombatSetting("FastTeleport")) ? 1 : 30)
                    + Max(1, GetDuration());
        castPhase = CP_ACTION;
        break;
    case SPELL_ID_INSPIRATION:
        if (!newSpells || pTarget.IsEmpty() || pTarget->WhoAmI() != HPC_UNIT)
        {
            castPhase = CP_FINISH;
            break;
        }
        {
            auto* stats = ((CMapObjectServer*)pTarget->GetMapObjectSpecific())->GetRPGStats();
            RestoreInspirationMana(stats->ssMP.fpCurrent, stats->ssMP.fpMax, GetPower());
            pTarget->SetUpdateMask(CMapObject::VLD_MASK_STATS);
            CreateParticles(FALSE, PTID_HEALING, CPlot3D(0, 0, 0), 2.5f * GetShowPower() + 2.5f, 1,
                            CPlot3D(0, 0, 0), pTarget);
            // The reference retains Healing's quiet/HP predicate even for mana.
            if (!bQuiet || pTarget->GetHP() != pTarget->GetMaxHP())
            {
                CreateSound(FALSE, "magic\\Inspiration\\start.mp3", pTarget->GetCP(), CPlot3D(0, 0, 0));
                CreateLight(ptTarget, GetLightColor(), -GetLightRadius(), GetLightSpeed());
            }
            nTimeLeft = 30;
            castPhase = CP_ACTION;
        }
        break;
    case SPELL_ID_HEALING:
        if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
        {
            castPhase = CP_FINISH;
            break;
        }
        if (pTarget->WhoAmI() == HPC_UNIT)
        {
            ((CUnitServer *)((CUnit *)pTarget)->GetUnitSpecific())->Heal(GetPower());
            CreateParticles(FALSE, PTID_HEALING, CPlot3D(0, 0, 0), GetShowPower(), 1,
                            CPlot3D(0, 0, 0), pTarget); // healing
            if (!bQuiet || pTarget->GetHP() != pTarget->GetMaxHP())
            {
                CreateSound(FALSE, "magic\\Healing\\start.wav", pTarget->GetCP(), CPlot3D(0, 0, 0));
                CreateLight(ptTarget, GetLightColor(), -GetLightRadius(), GetLightSpeed());
            }
            nTimeLeft = 30;
            castPhase = CP_ACTION;
        }
        else
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_CHARM:
        if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
        {
            castPhase = CP_FINISH;
            break;
        }
        if (pTarget->WhoAmI() == HPC_UNIT && !pWho.IsEmpty() && pWho->WhoAmI() == HPC_UNIT)
        {
            CUnitServer *pTargServer = (CUnitServer *)((CUnit *)pTarget)->GetUnitSpecific();
            CUnitServer *pSrcServer = (CUnitServer *)((CUnit *)pWho)->GetUnitSpecific();
            SRPGUnitStats *pStats = pSrcServer->GetRPGStats();
            float fpSkill = pStats->psIntelligence.fpEffectiveValue;
            fpSkill += GetPower();
            bool bFail = false;
            for (;;)
            {
                if (pTargServer->GetUnit()->GetOwner() == pSrcServer->GetUnit()->GetOwner())
                    break;
                if (!pTargServer->Tame(pWho, fpSkill))
                {
                    bFail = true;
                    InformFailure(ACK_HERO_REFUSE_SPELL_CHARM);
                    break;
                }
            }
            if (!bFail)
                CreateSound(FALSE, "magic\\Charm\\start.wav", pTarget->GetCP(), CPlot3D(0, 0, 0));
            castPhase = CP_FINISH;
        }
        break;
    case SPELL_ID_POSSESSION:
        if (!pTarget.IsEmpty())
        {
            CreateSound(FALSE, "magic\\Possession\\start.wav", pTarget->GetUnitHeadPosition(),
                        CPlot3D(0, 0, 0));
            nTimeLeft = GetDuration();
            castPhase = CP_ACTION;
        }
        else
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_VISIONFOG:
        if (pWho.IsEmpty() || pTarget.IsEmpty())
        {
            castPhase = CP_FINISH;
            break;
        }
        CreateVisionFog(pWho->GetID(), pTarget->GetID());
        CreateSound(FALSE, "magic\\VisionFog\\start.wav", pTarget->GetUnitHeadPosition(),
                    CPlot3D(0, 0, 0));
        nTimeLeft = GetDuration();
        castPhase = CP_ACTION;
        break;
    default:
        castPhase = CP_FINISH;
        break;
    }
    if(!ModRuntime::Current()||ModSpellDanger::StockLoadKind(nKind))AddRemoveDangerArea(nKind, TRUE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::DeliverSpell(int nKind)
{
    if(ExtendedDeliver(nKind))return;
    CPlot3D ptExplosion;
    switch (nKind)
    {
    case SPELL_ID_RICKMAGIC:
    case SPELL_ID_FIREARROW:
        if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
        {
            castPhase = CP_FINISH;
            break;
        }
        if (pEffect)
        {
            SetCP(pEffect->GetCP());
            if (GetEffectArrow()->IsDelivered())
            {
                InflictTargetedDamage();
                RemoveEffect();
                ptTarget = pTarget->GetCP();
                ptExplosion = ptTarget;
                ptExplosion.z = ((CUnit *)pTarget)->GetUnitHeadHeight();
                ptExplosion.z += pWS->GetZValue(ptTarget.x, ptTarget.y);
                // CreateParticles( FALSE, PTID_FIREBLAST, ptExplosion, 0.2f, CPlot3D( 0, 0, 0 ) );
                CreateParticles(FALSE, PTID_FIRESTAR, CPlot3D(0, 0, 0), GetShowPower() * 0.7f, 1,
                                CPlot3D(0, 0, 0), pTarget);
                CreateSound(FALSE, "magic\\Firearrow\\end.wav", ptExplosion, CPlot3D(0, 0, 0));
                CreateLight(ptExplosion, GetLightColor(), GetLightRadius() * FP_FINAL_LIGHT_RK,
                            GetLightSpeed() * FP_FINAL_LIGHT_RK);
                nTimeLeft = 35;
                castPhase = CP_ACTION;
            }
        }
        if (nTimeLeft-- < 0)
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_ACIDRAY:
        if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
        {
            castPhase = CP_FINISH;
            break;
        }
        if (pEffect)
        {
            SetCP(pEffect->GetCP());
            if (GetEffectArrow()->IsDelivered())
            {
                InflictTargetedDamage();
                RemoveEffect();
                ptTarget = pTarget->GetCP();
                ptExplosion = ptTarget;
                ptExplosion.z = ((CUnit *)pTarget)->GetUnitHeadHeight();
                ptExplosion.z += pWS->GetZValue(ptTarget.x, ptTarget.y);
                CreateParticles(FALSE, PTID_ACIDSTAR, CPlot3D(0, 0, 0), GetShowPower() * 0.7f, 1,
                                CPlot3D(0, 0, 0), pTarget);
                CreateSound(FALSE, "magic\\Acidray\\end.wav", ptExplosion, CPlot3D(0, 0, 0));
                CreateLight(ptExplosion, GetLightColor(), GetLightRadius() * FP_FINAL_LIGHT_RK,
                            GetLightSpeed() * FP_FINAL_LIGHT_RK);
                nTimeLeft = 35;
                castPhase = CP_ACTION;
            }
        }
        if (nTimeLeft-- < 0)
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_FIRERAZE:
    case SPELL_ID_FIREBALL:
        if (nKind == SPELL_ID_FIRERAZE && !NewSpellsEnabled())
        {
            ASSERT(0 && "Deliver phase not implemented for this spell");
            break;
        }
        if (nTimeLeft-- < 0)
        {
            SetCP(ptTarget);
            // inflict some fire damage
            InflictDamageInRadius();
            RemoveEffect();
            ptExplosion = ptTarget;
            // ptExplosion.z += 1;//1.5f;
            CEffectParticle *pP;
            pP = CreateParticles(FALSE, PTID_FIREBLAST, ptExplosion,
                                 1 * Max(GetAreaPower(), GetShowPower()), 1,
                                 CPlot3D(0, 0, 0)); // fireball explosion
            pP->SetPower(GetShowPower(), GetRadius());
            CreateSound(FALSE, "magic\\fireball\\end.wav", ptExplosion, CPlot3D(0, 0, 0));
            CreateLight(ptExplosion, GetLightColor(), GetLightRadius() * FP_FINAL_LIGHT_RK,
                        GetLightSpeed() * FP_FINAL_LIGHT_RK);
            if (!pWho.IsEmpty() && pWho->WhoAmI() == HPC_UNIT)
            {
                CUnitServer *pUS = (CUnitServer *)((CUnit *)pWho)->GetUnitSpecific();
                const auto* soundMod = ModRuntime::Current();
                const float strength = soundMod && soundMod->SpellAddon().Feature38() == 1
                    ? soundMod->SpellAddon().Scalar(ModSpellAddon::ScalarKey::gameplay_fireboom) : 5.0f;
                pWS->aiMap.marks.ProduceSoundFootstep(pUS, strength, AI_MAGIC_EFFECT_SOUND, &ptTarget);
            }
            nTimeLeft = 35;
            castPhase = CP_ACTION;
        }
        if (nTimeLeft < 0)
            RemoveMainEffect();
        break;
    default:
        ASSERT(0 && "Deliver phase not implemented for this spell");
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// Match the reachable ASI mana branch, including x87 comparisons and rounding.
// Typed arguments avoid retail object offsets; no HP branch or extra clamping.
static void ExchangeLinkMana(float& casterMP, float casterMax,
                             float& targetMP, float targetMax, float power)
{
    float* caster = &casterMP;
    float* target = &targetMP;
    __asm
    {
        mov ecx, caster
        mov edx, target
        fld targetMax
        fdivr casterMax
        fld dword ptr [edx]
        fadd dword ptr [ecx]
        fld1
        fadd st(0), st(2)
        fdiv st(1), st(0)
        fstp st(0)
        fmul st(1), st(0)
        fld dword ptr [ecx]
        fdiv casterMax
        fld dword ptr [edx]
        fdiv targetMax
        fcomp st(1)
        fstp st(0)
        fnstsw ax
        test ah, 1
        je casterReceives
        fld dword ptr [edx]
        fadd power
        fcomp st(1)
        fnstsw ax
        test ah, 1
        jne targetLimited
        jmp balanced
    casterReceives:
        fld dword ptr [ecx]
        fadd power
        fcomp st(2)
        fnstsw ax
        test ah, 1
        jne casterLimited
    balanced:
        fstp dword ptr [edx]
        fstp dword ptr [ecx]
        jmp done
    targetLimited:
        fld dword ptr [ecx]
        fsub power
        fstp dword ptr [ecx]
        fld dword ptr [edx]
        fadd power
        fstp dword ptr [edx]
        jmp discardBalances
    casterLimited:
        fld dword ptr [edx]
        fsub power
        fstp dword ptr [edx]
        fld dword ptr [ecx]
        fadd power
        fstp dword ptr [ecx]
    discardBalances:
        fstp st(0)
        fstp st(0)
    done:
    }
}
void CEffectSpell::ActionSpell(int nKind)
{
    if(ExtendedAction(nKind))return;
    const auto* wallMod = ModRuntime::Current();
    const int wallMask = wallMod && wallMod->SpellAddon().Feature25() == 1
        ? wallMod->SpellAddon().Byte(ModSpellAddon::ByteKey::gameplay_frequencyvalue) : 3;
    POSITION pos;
    CUnitList unitList;
    switch (nKind)
    {
    case SPELL_ID_INSPIRATION:
    case SPELL_ID_FIRERAZE:
        if (NewSpellsEnabled() && nTimeLeft-- < 0)
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_PANIC:
    case SPELL_ID_STORM:
        // R30 REVIEW: retain Start, finish instead of invalid/frozen Action.
        if (NewSpellsEnabled())
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_INVOKE_LGHTNG:
        RemoveMainEffect();
        if (nTimeLeft == 5)
        {
            CPlot3D ptSrc = GetCP();
            ptSrc.z += 30;
            CreateLightning(ptSrc, GetCP(), -7);
        }
        if (nTimeLeft-- < 0)
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_RICKMAGIC:
    case SPELL_ID_FIREARROW:
    case SPELL_ID_ACIDRAY:
    case SPELL_ID_HEALING:
    case SPELL_ID_FIREBALL:
        if (nTimeLeft-- < 0)
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_LIGHTNING:
    case SPELL_ID_CURSEMAGIC:
        if (nTimeLeft == 0)
        {
            RemoveMainEffect();
            RemoveSoundEffect();
            pEffectLight->SetSpeed(GetLightSpeed());
        }
        if (nTimeLeft < -80)
            castPhase = CP_FINISH;
        nTimeLeft--;
        break;
    case SPELL_ID_COLUMN_ACID:
        if (nTimeLeft-- < 0)
            castPhase = CP_FINISH;
        if (nTimeLeft == 15)
            InflictDamageInRadius();
        break;
    case SPELL_ID_POISONFOG:
        // inflict some damage to all near units
        if (nTimeLeft >= 0)
        {
            if ((nTimeLeft & wallMask) == 0)
            {
                InflictDamageInRadius();
                SetFlag(DW_SPELL_HIDE_CASTER); // forbid knowledge about author of poisonfog
            }
            nTimeLeft--;
        }
        else
        {
            if (nTimeLeft == -1)
            {
                CreateSound(FALSE, "magic\\poison\\end.wav", ptTarget, CPlot3D(0, 0, 0));
                pEffectLight->SetSpeed(GetLightSpeed());
                AddRemoveDangerArea(nKind, FALSE);
                RemoveSoundEffect();
                RemoveMainEffect();
            }
            if (nTimeLeft < -80)
                castPhase = CP_FINISH;
            nTimeLeft--;
        }
        break;
    case SPELL_ID_FIREWALL:
        // inflict some damage to near units in the wall
        if (nTimeLeft >= 0)
        {
            if ((nTimeLeft & wallMask) == 0 && pEffect)
            {
                InflictWallDamage(GetEffectWall()->ptWallDirection);
                SetFlag(DW_SPELL_HIDE_CASTER); // forbid knowledge about author of firewall
            }
            nTimeLeft--;
        }
        else
        {
            if (nTimeLeft == -1)
            {
                CPlot3D ptTarg = GetCP();
                if (pEffectSound)
                    ptTarg = pEffectSound->GetCP();
                AddRemoveDangerArea(nKind, FALSE);
                RemoveSoundEffect();
                RemoveMainEffect();
                CreateSound(FALSE, "magic\\Firewall\\end.wav", ptTarg, CPlot3D(0, 0, 0));
                pEffectLight->SetSpeed(GetLightSpeed());
            }
            if (nTimeLeft < -80)
                castPhase = CP_FINISH;
            --nTimeLeft;
        }
        break;
    case SPELL_ID_CAMPFIRE:
        if ((nTimeLeft++ & wallMask) == 0)
            InflictDamageInRadius();
        break;
    case SPELL_ID_WALL_LGHTNG:
        // inflict some damage to near units in the wall
        if (nTimeLeft >= 0)
        {
            if ((nTimeLeft & wallMask) == 0 && pEffect)
            {
                InflictWallDamage(GetEffectLightning()->ptDst - GetEffectLightning()->ptSrc);
                SetFlag(DW_SPELL_HIDE_CASTER); // forbid knowledge about author of lightning wall
            }
            nTimeLeft--;
        }
        else
        {
            if (nTimeLeft == -1)
            {
                pEffectLight->SetSpeed(GetLightSpeed());
                AddRemoveDangerArea(nKind, FALSE);
                RemoveSoundEffect();
                RemoveMainEffect();
            }
            if (nTimeLeft < -80)
                castPhase = CP_FINISH;
            nTimeLeft--;
        }
        break;
    case SPELL_ID_LINK:
    {
        if (pTarget == (CMapObject *)nullptr || pWho == (CMapObject *)nullptr ||
            pTarget->IsMoving() || pWho->IsMoving() || pTarget->IsDead() || pWho->IsDead())
        {
            CreateSound(FALSE, "magic\\Link\\end.wav", GetCP(), CPlot3D(0, 0, 0));
            InformFailure(ACK_HERO_REFUSE_SPELL_LINK_ABORTED);
            castPhase = CP_FINISH;
            break;
        }
        const auto* linkMod = ModRuntime::Current();
        if (linkMod && linkMod->SpellAddon().Feature34() == 1 && (nTimeLeft & 31) == 0)
        {
            auto* casterStats = ((CMapObjectServer*)pWho->GetMapObjectSpecific())->GetRPGStats();
            auto* targetStats = ((CMapObjectServer*)pTarget->GetMapObjectSpecific())->GetRPGStats();
            ExchangeLinkMana(casterStats->ssMP.fpCurrent, casterStats->ssMP.fpMax,
                             targetStats->ssMP.fpCurrent, targetStats->ssMP.fpMax, GetPower());
            pWho->SetUpdateMask(CMapObject::VLD_MASK_STATS);
            pTarget->SetUpdateMask(CMapObject::VLD_MASK_STATS);
        }
        if (--nTimeLeft < 0)
        {
            CreateSound(FALSE, "magic\\Link\\end.wav", GetCP(), CPlot3D(0, 0, 0));
            castPhase = CP_FINISH;
        }
        break;
    }
    case SPELL_ID_TELEPORT:
        if (nTimeLeft-- < 0 || pWho == (CMapObject *)nullptr || pWho->IsDead())
            castPhase = CP_FINISH;
        else if (nTimeLeft == Max(1, GetDuration()))
        {
            ptTarget.z = pWho->GetCP().z;
            if (!pWS->aiMap.CanPutObject(pWho, ptTarget))
            {
                InformFailure(ACK_HERO_REFUSE_CANT_TELEPORT);
                break;
            }
            BOOL bRv = pWS->aiMap.MoveObject(pWho, ptTarget);
            GameJournal::Event("WORLD", "teleport", "reason=teleport actor=%d x=%g y=%g z=%g result=%d", pWho->GetID(), ptTarget.x, ptTarget.y, ptTarget.z, bRv);
            ASSERT(bRv);
            RemoveMainEffect();
            RemoveSoundEffect();
            if (!pWho.IsEmpty() && pWho->WhoAmI() == HPC_UNIT)
            {
                CSpellEffect effect;
                spell.GetSpellEffect(effect);
                pWho->AddSpellEffect(effect);
                ((CUnitServer *)((CUnit *)pWho)->GetUnitSpecific())->InitializeUnit();
                pEffectLight->SetSpeed(GetLightSpeed());
                // here was StopCurrentCommand() from unitserver, but now it should not be required
            }
        }
        break;
    case SPELL_ID_FIREWORKS:
        --nTimeLeft;
        if (nTimeLeft >= 0)
        {
            pWS->AddActiveSpot(GetCP(), GetRadius() + 5);
            pWS->aiMap.GetUnitsNear(GetCP(), unitList, GetRadius());
            for (pos = unitList.GetHeadPosition(); pos;)
            {
                CUnit *pUnit = unitList.GetNext(pos);
                ASSERT(AI_PPOWER_FIREWORKS > 100);
                int nPower = (AI_PPOWER_FIREWORKS - 50) * (float(nTimeLeft) / GetDuration()) + 50;
                ((CUnitServer *)pUnit->GetUnitSpecific())
                    ->logic.environment.AddPoI(GetCP(), nPower, AI_PPDEC_FIREWORKS);
            }
        }
        else
        {
            if (nTimeLeft == -1)
            {
                CreateSound(FALSE, "magic\\Firework\\end.wav", GetCP(), CPlot3D(0, 0, 0));
                RemoveMainEffect();
                RemoveSoundEffect();
                pEffectLight->SetSpeed(GetLightSpeed());
            }
            if (nTimeLeft < -80)
                castPhase = CP_FINISH;
        }
        break;
    case SPELL_ID_CLAIRVOYENCE:
        if (!pWho.IsEmpty() && pWho->WhoAmI() == HPC_UNIT)
        {
            CPlayer *pPlayer = ((CUnit *)pWho)->GetOwner();
            if (pPlayer)
            {
                CScriptObjectGroup &group = ((CPlayerServer *)pPlayer->GetPlayerSpecific())
                                                ->addToVisible;
                pWS->aiMap.GetUnitsNear(GetCP(), unitList, GetRadius());
                for (POSITION pos = unitList.GetHeadPosition(); pos;)
                    group.Add(unitList.GetNext(pos));
            }
            pWS->AddActiveSpot(GetCP(), GetRadius() + 5);
        }
        if (--nTimeLeft < 0 || pWho.IsEmpty())
            castPhase = CP_FINISH;
        if (nTimeLeft == 10)
        {
            CreateParticles(FALSE, PTID_CLAYRING, ptTarget, -GetRadius(), 1, CPlot3D(0, 0, 0));
            CreateSound(FALSE, "magic\\Clairvoyence\\end.wav", GetCP(), CPlot3D(0, 0, 0));
        }
        break;
    case SPELL_ID_POSSESSION:
        if (SpellTargetMissing(pTarget, spell.GetSubtypeID(), castPhase))
        {
            castPhase = CP_FINISH;
            break;
        }
        if (!pWho.IsEmpty() && pWho->WhoAmI() == HPC_UNIT)
        {
            CPlayer *pPlayer = ((CUnit *)pWho)->GetOwner();
            if (pPlayer && pTarget->WhoAmI() == HPC_UNIT)
            {
                CUnit *pUnit = (CUnit *)pTarget;
                CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
                CScriptObjectGroup &group = ((CPlayerServer *)pPlayer->GetPlayerSpecific())
                                                ->addToVisible;
                group.Add(pUnit);
                group.AddList(*pUS->logic.GetVisibleList());
                group.AddList(pUS->logic.environment.seenCorpses);
                ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SetCameraOn(pUnit->GetID());
                pUnit->SetFlag(UNIT_LOOKATALLIES);
                pWS->AddActiveSpot(pUnit->GetCP(),
                                   pUS->GetMaxVisionRange() * AI_MAX_VISION_BONUS + 20);
            }
            if (--nTimeLeft < 0)
                castPhase = CP_FINISH;
        }
        else
            castPhase = CP_FINISH;
        break;
    case SPELL_ID_VISIONFOG:
        if (nTimeLeft-- < 0 || pTarget.IsEmpty() || pTarget->IsDead())
            castPhase = CP_FINISH;
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::FinishSpell(int nKind)
{
    AddRemoveDangerArea(nKind, FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Inflicting damage
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::InflictDamage(SRPGAttackPortion &ap, CUnit *pTarget)
{
    const auto* mod = ModRuntime::Current();
    const int filterMode = mod ? mod->SpellAddon().Feature36() : 0;
    // LS replaces this body under JMVParticles; the ASI f36 patch is then unreachable.
    const bool lsDamage = mod && mod->CombatSetting("JMVParticles");
    if ((pWho.IsEmpty() && (lsDamage || (filterMode != 1 && filterMode != 2))) ||
        spell.IsValidTarget(pWho, pTarget))
    {
        if (CheckFlag(DW_SPELL_HIDE_CASTER))
            pTarget->ProcessAttackPortion(&ap, 0, pWho);
        else
            pTarget->ProcessAttackPortion(&ap, pWho, pWho);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::InflictDamageInRadius()
{
    POSITION pos;
    CUnitList units;
    SRPGAttackPortion ap;
    CreateAttackPortion(ap);
    // for now damage on mapobjects is absent
    pWS->aiMap.GetUnitsNear(GetCP(), units, GetRadius());
    pos = units.GetHeadPosition();
    while (pos)
    {
        CUnit *pUnit = units.GetNext(pos);
        if (!pUnit->CanBeDamagedByAreaSpell())
            continue;
        InflictDamage(ap, pUnit);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// для ptWallDir важно только направление
void CEffectSpell::InflictWallDamage(const CPlot3D &ptWallDir)
{
    CUnitList units;
    SRPGAttackPortion ap;
    CreateAttackPortion(ap);
    // for now damage on mapobjects is absent
    CPlot3D ptNormal;
    ptNormal.x = -ptWallDir.y;
    ptNormal.y = ptWallDir.x;
    ptNormal.z = 0;
    // ширину стены считаем 1 метер ( 0.5 метра в обе стороны )
    ptNormal *= 2 / fabs(ptNormal);
    pWS->aiMap.GetUnitsNear(GetCP(), units, GetRadius());
    for (POSITION pos = units.GetHeadPosition(); pos;)
    {
        CUnit *pUnit = units.GetNext(pos);
        if (fabs((pUnit->GetCP() - GetCP()) * ptNormal) > 1)
            continue;
        if (!pUnit->CanBeDamagedByAreaSpell())
            continue;
        InflictDamage(ap, pUnit);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CEffectSpell::InflictTargetedDamage()
{
    CUnit *pUnit;
    SRPGAttackPortion ap;
    CreateAttackPortion(ap);
    pUnit = (CUnit *)pTarget;
    if (pUnit && pUnit->WhoAmI() == HPC_UNIT)
        InflictDamage(ap, pUnit);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CEffectSpell::CreateAttackPortion(SRPGAttackPortion &ap)
{
    if(ModRuntime::Current()&&ModSpells::ExtendedElementalPortion(spell.GetSubtypeID()>>16,GetPower(),GetDuration(),ap,spell.GetMagicSubtype()))return TRUE;
    ap.Clear();
    ap.hl = RPGHL_ALL;
    ap.fpToHit = FLT_MAX;
    ap.fBackStabMultiplier = 1.0f;
    ap.bCalledShot = false;

    const int attackKind = spell.GetSubtypeID() >> 16;
    if (NewSpellsEnabled() && attackKind >= SPELL_ID_ENSORCELL && attackKind <= SPELL_ID_STORM)
    {
        ap.fpAttacks[RPGAT_TERMICAL] = 1;
        ap.fpDamage = GetPower();
        ap.bAreaEffect = FALSE;
    }
    else switch (attackKind)
    {
    case SPELL_ID_INVOKE_LGHTNG:
    case SPELL_ID_LIGHTNING:
    case SPELL_ID_CURSEMAGIC:
        ap.fpAttacks[RPGAT_ELECTRICAL] = 1;
        ap.fpDamage = GetPower();
        ap.bAreaEffect = FALSE;
        break;
    case SPELL_ID_WALL_LGHTNG:
        ap.fpAttacks[RPGAT_ELECTRICAL] = 1;
        ap.fpDamage = GetPower();
        ap.bAreaEffect = TRUE;
        break;
    case SPELL_ID_FIREBALL:
    case SPELL_ID_FIREARROW:
    case SPELL_ID_RICKMAGIC:
        ap.fpAttacks[RPGAT_TERMICAL] = 1;
        ap.fpDamage = GetPower();
        ap.bAreaEffect = FALSE;
        break;
    case SPELL_ID_FIREWALL:
        ap.fpAttacks[RPGAT_TERMICAL] = 1;
        ap.fpDamage = GetPower();
        ap.bAreaEffect = TRUE;
        break;
    case SPELL_ID_CAMPFIRE:
        ap.fpAttacks[RPGAT_TERMICAL] = 1;
        ap.fpDamage = GetPower();
        ap.bAreaEffect = TRUE;
        break;
    case SPELL_ID_ACIDRAY:
        ap.fpAttacks[RPGAT_CHEMICAL] = 1;
        ap.fpDamage = GetPower();
        ap.bAreaEffect = FALSE;
        break;
    case SPELL_ID_COLUMN_ACID:
    case SPELL_ID_POISONFOG:
        ap.fpAttacks[RPGAT_CHEMICAL] = 1;
        ap.fpDamage = GetPower();
        ap.bAreaEffect = TRUE;
        break;
    default:
        ASSERT(0);
        break;
    }
    //
    if (GetDuration() > 1) // continuous effect
    {
        const auto* wallMod = ModRuntime::Current();
        float fraction = 4.0f / float(GetDuration());
        if (wallMod && wallMod->SpellAddon().Feature26() == 2)
        {
            const int kind = spell.GetSubtypeID() >> 16;
            const auto key = kind == SPELL_ID_FIREWALL ? ModSpellAddon::ScalarKey::gameplay_firepercentagevalue
                : kind == SPELL_ID_WALL_LGHTNG ? ModSpellAddon::ScalarKey::gameplay_lightningpercentagevalue
                : ModSpellAddon::ScalarKey::gameplay_acidpercentagevalue;
            fraction = wallMod->SpellAddon().Scalar(key);
        }
        ap.fpDamagePercentage = fraction;
        ap.fpDamage *= fraction;
    }

    //

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////

#include "ModExtendedSpell.inl"
