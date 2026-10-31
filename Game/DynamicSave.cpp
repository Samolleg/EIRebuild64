#include "StdAfx.h"
#include "VoiceWaitState.h"
#include "DynamicSave.h"
#include "UnitServer.h"
#include "WaterLevels.h"
#include "FofSystem.h"
#include "Camera.h"
#include "ScenarioTracker.h"
#include "UnitStatisticsDriver.h"
#include <cstring>
#include "RPGSystem.h"
#include "GeneralUnitLogic.h"
#include "METraps.h"
#include "ServerConsts.h"
#include "Script.h"
#include "Effect.h"
#include "RandomGen.h"
#include "ModNativeCrtRandom.h"

namespace SaveSchema {
namespace {
void WriteCount(Writer &w, INT_PTR count)
{
    if (count < 0 || static_cast<std::uint64_t>(count) > UINT32_MAX)
        throw Error(ErrorCode::Limit, "save collection size overflow");
    w.Count(static_cast<std::uint32_t>(count));
}
void Text(Writer &w, const CString &v) { w.String(std::string((const char *)v, v.GetLength())); }
void Text(Reader &r, CString &v)
{
    const auto s = r.String();
    if (s.find('\0') != std::string::npos) throw Error(ErrorCode::Invalid, "embedded NUL in script text");
    v = CString(s.data(), static_cast<int>(s.size()));
}
template <class T, class A> void Array(Writer &w, const CAArray<T, A> &a)
{
    WriteCount(w,a.GetSize());
    for (int i = 0; i < a.GetSize(); ++i) w.Value(a[i]);
}
template <class T, class A> void Array(Reader &r, CAArray<T, A> &a)
{
    a.SetSize(r.Count());
    for (int i = 0; i < a.GetSize(); ++i) r.Value(a[i]);
}
}
} // namespace SaveSchema

void SRPGScienceStats::Store(SaveSchema::Writer &w) const
{
    w.UInt32(dwTools);
    w.UInt32(dwKeyNumber);
    w.UInt32(dwSkill);
}
void SRPGScienceStats::Load(SaveSchema::Reader &r)
{
    dwTools = r.UInt32();
    dwKeyNumber = r.UInt32();
    dwSkill = r.UInt32();
}

void SRPGAttackPortion::Store(SaveSchema::Writer &w) const
{
    for (const auto &field : fpAttacks) w.Float32(field);
    w.Int32(nSkill);
    w.Float32(fpToHit);
    w.Float32(fpDamage);
    w.Float32(fpDamagePercentage);
    w.Int32(static_cast<std::int32_t>(hl));
    w.Bool(bAreaEffect);
    w.Float32(fBackStabMultiplier);
    w.Bool(bCalledShot);
}
void SRPGAttackPortion::Load(SaveSchema::Reader &r)
{
    for (auto &field : fpAttacks) field = r.Float32();
    nSkill = r.Int32();
    fpToHit = r.Float32();
    fpDamage = r.Float32();
    fpDamagePercentage = r.Float32();
    hl = static_cast<ERPGHitLocations>(r.Int32());
    bAreaEffect = r.Bool();
    fBackStabMultiplier = r.Float32();
    bCalledShot = r.Bool();
}

void SRPGGeneralAttack::Store(SaveSchema::Writer &w) const
{
    w.UInt16(wToHit);
    for (const auto &field : wAttacks) w.UInt16(field);
}
void SRPGGeneralAttack::Load(SaveSchema::Reader &r)
{
    wToHit = r.UInt16();
    for (auto &field : wAttacks) field = r.UInt16();
}

void SRPGPrimaryStat::Store(SaveSchema::Writer &w) const
{
    w.Float32(fpBaseValue);
    w.Float32(fpEffectiveValue);
}
void SRPGPrimaryStat::Load(SaveSchema::Reader &r)
{
    fpBaseValue = r.Float32();
    fpEffectiveValue = r.Float32();
}

void SRPGSecondaryStat::Store(SaveSchema::Writer &w) const
{
    w.Float32(fpCurrent);
    w.Float32(fpMax);
    w.Float32(fpRegenerate);
}
void SRPGSecondaryStat::Load(SaveSchema::Reader &r)
{
    fpCurrent = r.Float32();
    fpMax = r.Float32();
    fpRegenerate = r.Float32();
}

void SRPGArmorLayer::Store(SaveSchema::Writer &w) const
{
    w.UInt32(dwCrippleMask);
    for (const auto &field : fpAbsorbtions) w.Float32(field);
    for (const auto &field : fpResistances) w.Float32(field);
    w.Int32(static_cast<std::int32_t>(state));
    for (const auto &field : lpItems) w.GameReference(field);
}
void SRPGArmorLayer::Load(SaveSchema::Reader &r)
{
    dwCrippleMask = r.UInt32();
    for (auto &field : fpAbsorbtions) field = r.Float32();
    for (auto &field : fpResistances) field = r.Float32();
    state = static_cast<ERPGArmorLayerStates>(r.Int32());
    for (auto &field : lpItems) field = r.GameReference();
}

void SRPGHitLocation::Store(SaveSchema::Writer &w) const
{
    w.Int32(static_cast<std::int32_t>(hlt));
    w.UInt32(dwCrippleMask);
    for (const auto &field : layers) w.Value(field);
    w.Int32(static_cast<std::int32_t>(state));
    w.Float32(fpHP);
    w.Float32(fpMaxHP);
    w.Float32(fpHPWeight);
    w.Float32(fpVitality);
}
void SRPGHitLocation::Load(SaveSchema::Reader &r)
{
    hlt = static_cast<ERPGHitLocationTypes>(r.Int32());
    dwCrippleMask = r.UInt32();
    for (auto &field : layers) r.Value(field);
    state = static_cast<ERPGArmorLayerStates>(r.Int32());
    fpHP = r.Float32();
    fpMaxHP = r.Float32();
    fpHPWeight = r.Float32();
    fpVitality = r.Float32();
}

void SRPGCommonStats::Store(SaveSchema::Writer &w) const
{
    w.UInt32(dwFlags);
    w.Int32(nRegenerateCounter);
    w.Value(ssHP);
    w.Value(ssMP);
    for (const auto &field : psAbsorbtions) w.Value(field);
}
void SRPGCommonStats::Load(SaveSchema::Reader &r)
{
    dwFlags = r.UInt32();
    nRegenerateCounter = r.Int32();
    r.Value(ssHP);
    r.Value(ssMP);
    for (auto &field : psAbsorbtions) r.Value(field);
}

void SRPGUnitStats::Store(SaveSchema::Writer &w) const
{
    w.UInt32(dwFlags);
    w.Float32(fpExperience);
    w.Float32(fSpentExp);
    w.Value(psStrength);
    w.Value(psDexterity);
    w.Value(psIntelligence);
    w.Value(move);
    w.Value(action);
    for (const auto &field : fpSpeeds) w.Float32(field);
    w.Float32(fpVisionArc);
    w.Float32(fpPeripheral);
    w.Float32(fpPeripheralArc);
    w.Float32(fpCurrEncumbrance);
    w.Float32(fpMaxEncumbrance);
    w.Float32(fpMoveDamageModifier);
    w.Float32(fpActionDamageModifier);
    w.UInt32(dwLocomotion);
    w.Float32(fpAttackDistance);
    w.UInt16(wAIClassStay);
    w.UInt16(wAlignProblemFixer);
    w.UInt16(wAIClassLie);
    w.UInt16(wAIClassCrawl);
    w.Float32(fpNAWRange);
    w.Float32(fpNAWToHit);
    w.Float32(fpNAWToHitRandom);
    w.Float32(fpNAWParry);
    w.Float32(fpNAWWeight);
    w.Float32(fpNAWDamageMin);
    w.Float32(fpNAWDamageRnd);
    w.Float32(fpNAWActionPoints);
    for (const auto &field : fpNAWAttacks) w.Float32(field);
    w.Int32(static_cast<std::int32_t>(rpgNAWWeaponType));
    for (const auto &field : senses) w.Value(field);
    for (const auto &field : percepts) w.Value(field);
    for (const auto &field : hls) w.Value(field);
    for (const auto &field : cManualSkills) w.UInt8(field);
    for (const auto &field : cAutoSkills) w.UInt8(field);
    for (const auto &field : cPerks) w.UInt8(field);
    w.UInt32(bFlags);
    w.Float32(fBaseHP);
    w.Float32(fExpPenalty);
}
void SRPGUnitStats::Load(SaveSchema::Reader &r)
{
    dwFlags = r.UInt32();
    fpExperience = r.Float32();
    fSpentExp = r.Float32();
    r.Value(psStrength);
    r.Value(psDexterity);
    r.Value(psIntelligence);
    r.Value(move);
    r.Value(action);
    for (auto &field : fpSpeeds) field = r.Float32();
    fpVisionArc = r.Float32();
    fpPeripheral = r.Float32();
    fpPeripheralArc = r.Float32();
    fpCurrEncumbrance = r.Float32();
    fpMaxEncumbrance = r.Float32();
    fpMoveDamageModifier = r.Float32();
    fpActionDamageModifier = r.Float32();
    dwLocomotion = r.UInt32();
    fpAttackDistance = r.Float32();
    wAIClassStay = r.UInt16();
    wAlignProblemFixer = r.UInt16();
    wAIClassLie = r.UInt16();
    wAIClassCrawl = r.UInt16();
    fpNAWRange = r.Float32();
    fpNAWToHit = r.Float32();
    fpNAWToHitRandom = r.Float32();
    fpNAWParry = r.Float32();
    fpNAWWeight = r.Float32();
    fpNAWDamageMin = r.Float32();
    fpNAWDamageRnd = r.Float32();
    fpNAWActionPoints = r.Float32();
    for (auto &field : fpNAWAttacks) field = r.Float32();
    rpgNAWWeaponType = static_cast<ERPGWeaponTypes>(r.Int32());
    for (auto &field : senses) r.Value(field);
    for (auto &field : percepts) r.Value(field);
    for (auto &field : hls) r.Value(field);
    for (auto &field : cManualSkills) field = r.UInt8();
    for (auto &field : cAutoSkills) field = r.UInt8();
    for (auto &field : cPerks) field = r.UInt8();
    bFlags = r.UInt32();
    fBaseHP = r.Float32();
    fExpPenalty = r.Float32();
}

void SAlarmInfo::Store(SaveSchema::Writer &w) const
{
    w.Bool(bActivated);
    w.Int32(nTime);
    w.Float32(fpX);
    w.Float32(fpY);
}
void SAlarmInfo::Load(SaveSchema::Reader &r)
{
    bActivated = r.Bool();
    nTime = r.Int32();
    fpX = r.Float32();
    fpY = r.Float32();
}

void SActionPoint::Store(SaveSchema::Writer &w) const
{
    w.Value(ptLook);
    w.Float32(fpTurnSpeed);
    w.Int16(nWaitSeg);
    w.Int16(nFlags);
}
void SActionPoint::Load(SaveSchema::Reader &r)
{
    r.Value(ptLook);
    fpTurnSpeed = r.Float32();
    nWaitSeg = r.Int16();
    nFlags = r.Int16();
}

void SMEArea::Store(SaveSchema::Writer &w) const
{
    w.Value(ptCenter);
    w.Float32(fRadius);
}
void SMEArea::Load(SaveSchema::Reader &r)
{
    r.Value(ptCenter);
    fRadius = r.Float32();
}

void SMEMagicTrap::Store(SaveSchema::Writer &w) const
{
    w.UInt32(dwDiplomacy);
    SaveSchema::Text(w, szSpell);
    w.Value(ptPosition);
    SaveSchema::Array(w, aAreas);
    SaveSchema::Array(w, aTargets);
    w.Int32(nCastingInterval);
    w.Bool(bCastOnce);
}
void SMEMagicTrap::Load(SaveSchema::Reader &r)
{
    dwDiplomacy = r.UInt32();
    SaveSchema::Text(r, szSpell);
    r.Value(ptPosition);
    SaveSchema::Array(r, aAreas);
    SaveSchema::Array(r, aTargets);
    nCastingInterval = r.Int32();
    bCastOnce = r.Bool();
}

void SMELever::Store(SaveSchema::Writer &w) const
{
    w.Value(science);
    w.Int32(nCurrentState);
    w.Int32(nTotalStates);
    w.Bool(isDoor);
    w.Bool(needGraphRecalc);
    w.Bool(isCycled);
}
void SMELever::Load(SaveSchema::Reader &r)
{
    r.Value(science);
    nCurrentState = r.Int32();
    nTotalStates = r.Int32();
    isDoor = r.Bool();
    needGraphRecalc = r.Bool();
    isCycled = r.Bool();
}

void SPartyID::Store(SaveSchema::Writer &w) const
{
    w.Int32(nSomeNumber);
}
void SPartyID::Load(SaveSchema::Reader &r)
{
    nSomeNumber = r.Int32();
}

void SPatrolPoint::Store(SaveSchema::Writer &w) const
{
    w.Value(ptPosition);
    SaveSchema::Array(w, actions);
}
void SPatrolPoint::Load(SaveSchema::Reader &r)
{
    r.Value(ptPosition);
    SaveSchema::Array(r, actions);
}

void SGeneralUnitLogic::Store(SaveSchema::Writer &w) const
{
    w.Int32(static_cast<std::int32_t>(eLogicModel));
    SaveSchema::Array(w, patrolPoints);
    w.Value(ptGuardPoint);
    w.Bool(bCyclic);
    w.UInt32(dwLogicFlags);
    w.Float32(fpGuardRadius);
    w.Float32(fpGuardStayIdleTime);
    w.Int32(nAlarmNumber);
    w.Bool(bEnabled);
    w.Float32(fCallForHelpRadius);
    w.Int32(static_cast<std::int32_t>(eAlarmCondition));
    w.Int32(nAlarmDelay);
    w.Int32(static_cast<std::int32_t>(aggressionMode));
}
void SGeneralUnitLogic::Load(SaveSchema::Reader &r)
{
    eLogicModel = static_cast<EUnitModel>(r.Int32());
    SaveSchema::Array(r, patrolPoints);
    r.Value(ptGuardPoint);
    bCyclic = r.Bool();
    dwLogicFlags = r.UInt32();
    fpGuardRadius = r.Float32();
    fpGuardStayIdleTime = r.Float32();
    nAlarmNumber = r.Int32();
    bEnabled = r.Bool();
    fCallForHelpRadius = r.Float32();
    eAlarmCondition = static_cast<EAlarmCondition>(r.Int32());
    nAlarmDelay = r.Int32();
    aggressionMode = static_cast<EUnitAggressionMode>(r.Int32());
}

void SScriptParam::Store(SaveSchema::Writer &w) const
{
    w.Int32(static_cast<std::int32_t>(eType));
    w.Int32(nNumber);
    w.Int32(nVarNumber);
}
void SScriptParam::Load(SaveSchema::Reader &r)
{
    eType = static_cast<EScriptParameterType>(r.Int32());
    nNumber = r.Int32();
    nVarNumber = r.Int32();
}

void SScriptArea::SScriptAreaElement::Store(SaveSchema::Writer &w) const
{
    w.Bool(bRect);
    if (bRect) { w.Float32(fpLowX); w.Float32(fpLowY); w.Float32(fpHighX); w.Float32(fpHighY); }
    else { w.Float32(fpCenterX); w.Float32(fpCenterY); w.Float32(fpRadius2); }
}
void SScriptArea::SScriptAreaElement::Load(SaveSchema::Reader &r)
{
    bRect = r.Bool();
    if (bRect) { fpLowX=r.Float32(); fpLowY=r.Float32(); fpHighX=r.Float32(); fpHighY=r.Float32(); }
    else { fpCenterX=r.Float32(); fpCenterY=r.Float32(); fpRadius2=r.Float32(); }
}

namespace SaveSchema {
ValueType ValueCodec<SRPGScienceStats>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGScienceStats *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGScienceStats *>(p)->Load(r); }};
}
ValueType ValueCodec<SRPGAttackPortion>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGAttackPortion *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGAttackPortion *>(p)->Load(r); }};
}
ValueType ValueCodec<SRPGGeneralAttack>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGGeneralAttack *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGGeneralAttack *>(p)->Load(r); }};
}
ValueType ValueCodec<SRPGPrimaryStat>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGPrimaryStat *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGPrimaryStat *>(p)->Load(r); }};
}
ValueType ValueCodec<SRPGSecondaryStat>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGSecondaryStat *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGSecondaryStat *>(p)->Load(r); }};
}
ValueType ValueCodec<SRPGArmorLayer>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGArmorLayer *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGArmorLayer *>(p)->Load(r); }};
}
ValueType ValueCodec<SRPGHitLocation>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGHitLocation *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGHitLocation *>(p)->Load(r); }};
}
ValueType ValueCodec<SRPGCommonStats>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGCommonStats *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGCommonStats *>(p)->Load(r); }};
}
ValueType ValueCodec<SRPGUnitStats>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRPGUnitStats *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRPGUnitStats *>(p)->Load(r); }};
}
ValueType ValueCodec<SAlarmInfo>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SAlarmInfo *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SAlarmInfo *>(p)->Load(r); }};
}
ValueType ValueCodec<SActionPoint>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SActionPoint *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SActionPoint *>(p)->Load(r); }};
}
ValueType ValueCodec<SMEArea>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SMEArea *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SMEArea *>(p)->Load(r); }};
}
ValueType ValueCodec<SMEMagicTrap>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SMEMagicTrap *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SMEMagicTrap *>(p)->Load(r); }};
}
ValueType ValueCodec<SMELever>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SMELever *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SMELever *>(p)->Load(r); }};
}
ValueType ValueCodec<SPartyID>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SPartyID *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SPartyID *>(p)->Load(r); }};
}
ValueType ValueCodec<SPatrolPoint>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SPatrolPoint *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SPatrolPoint *>(p)->Load(r); }};
}
ValueType ValueCodec<SGeneralUnitLogic>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SGeneralUnitLogic *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SGeneralUnitLogic *>(p)->Load(r); }};
}
ValueType ValueCodec<SScriptParam>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SScriptParam *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SScriptParam *>(p)->Load(r); }};
}
ValueType ValueCodec<RandomState>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const RandomState *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<RandomState *>(p)->Load(r); }};
}
ValueType ValueCodec<SScriptArea::SScriptAreaElement>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SScriptArea::SScriptAreaElement *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SScriptArea::SScriptAreaElement *>(p)->Load(r); }};
}
ValueType ValueCodec<CPlot2D>::Get()
{
    return {
        [](Writer &w, const void *p) { auto &v=*static_cast<const CPlot2D *>(p); w.Float32(v.x); w.Float32(v.y); },
        [](Reader &r, void *p) { auto &v=*static_cast<CPlot2D *>(p); v.x=r.Float32(); v.y=r.Float32(); }};
}
ValueType ValueCodec<CPlot3D>::Get()
{
    return {
        [](Writer &w, const void *p) { auto &v=*static_cast<const CPlot3D *>(p); w.Float32(v.x); w.Float32(v.y); w.Float32(v.z); },
        [](Reader &r, void *p) { auto &v=*static_cast<CPlot3D *>(p); v.x=r.Float32(); v.y=r.Float32(); v.z=r.Float32(); }};
}
ValueType ValueCodec<CScript>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const CScript *>(p)->StoreScript(w); },
        [](Reader &r, void *p) { static_cast<CScript *>(p)->LoadScript(r); }};
}
} // namespace SaveSchema

namespace SaveSchema {
// Private VM state is accessed only here, never through the global live world.
// Object/group command slots are scratch bindings recalculated by CalcParameters
// before every command/function; their arity is saved, their addresses are not.
struct ScriptCodec
{
    CScript &vm;
    std::vector<SScriptThread *> threadTable;
    std::map<const SScriptThread *, std::uint32_t> threadIds;
    std::vector<SOneScript *> scriptTable;
    std::map<const SOneScript *, std::uint32_t> scriptIds;
    explicit ScriptCodec(CScript &vm) : vm(vm) {}
    static void Invalid(const char *why) { throw Error(ErrorCode::Invalid, why); }
    template <class T> static void Pin(T *&target, T *object)
    {
        target = object;
        if (object) object->AddRef();
    }
    template <class T> static void Unpin(T *&object)
    {
        T *old = object;
        object = nullptr;
        if (old) old->Release();
    }
    static void Reset(CScript &v)
    {
        // Detached rollback must never RemoveEffect from the current world.
        for (int i=0; i<v.lights.GetSize(); ++i) Unpin(v.lights[i].pLight);
        POSITION p=v.particles.GetHeadPosition();
        while (p) Unpin(v.particles.GetNext(p).pParticle);
        p=v.sfxes.GetHeadPosition(); while(p) Unpin(v.sfxes.GetNext(p).pSound);
        p=v.lightnings.GetHeadPosition(); while(p) Unpin(v.lightnings.GetNext(p).pLightning);
        v.lights.RemoveAll(); v.particles.RemoveAll(); v.sfxes.RemoveAll(); v.lightnings.RemoveAll();
        v.runningThreads.RemoveAll(); v.runningScripts.RemoveAll(); v.scripts.RemoveAll();
        v.threads.RemoveAll(); v.worldActions.RemoveAll(); v.globalVars.Clear();
        v.tempGroups.RemoveAll(); v.tempObjects.RemoveAll(); v.scriptAreas.RemoveAll();
        v.bLoaded=FALSE; v.nForCount=0; v.nSecondaryCount=0;
        v.nPrevNegativeParticleSourceID=-2; v.nPrevNegativeSoundID=-2;
    }
    template <class T, class A, class F> static void Items(Writer &w, const CAArray<T,A> &a, F field)
    {
        WriteCount(w,a.GetSize());
        for (int i=0; i<a.GetSize(); ++i) field(a[i]);
    }
    template <class T, class A, class F> static void Items(Reader &r, CAArray<T,A> &a, F field)
    {
        a.SetSize(r.Count());
        for (int i=0; i<a.GetSize(); ++i) field(a[i]);
    }
    template <class T, class A, class F> static void List(Writer &w, const CList<T,A> &a, F field)
    {
        WriteCount(w,a.GetCount());
        POSITION p=a.GetHeadPosition(); while(p) field(a.GetNext(p));
    }
    template <class T, class A, class F> static void List(Reader &r, CList<T,A> &a, F field)
    {
        const auto count=r.Count();
        for (std::uint32_t i=0; i<count; ++i) field(a.GetAt(a.AddTail(T{})));
    }
    static void Object(Writer &w, const CScriptObject &v) {
        w.GameReference(v.GetObject()); w.Int32(v.travellingId);
    }
    static void Object(Reader &r, CScriptObject &v) {
        v.SetObject(r.GameReferenceAs<CMapObject>()); v.travellingId=r.Int32();
        if(v.travellingId < -1 || (v.travellingId>=0 && (v.GetObject() || !v.travellingId)))
            Invalid("invalid travelling party reference");
    }
    static void Group(Writer &w, const CScriptObjectGroup &v)
    {
        Items(w,v.members,[&](const CScriptObject &o){Object(w,o);});
    }
    static void Group(Reader &r, CScriptObjectGroup &v)
    {
        // Keep the exact order/null slots and refcounts, without Add() deduplication.
        Items(r,v.members,[&](CScriptObject &o){Object(r,o);});
    }
    template <class T, class F> static void Vars(Writer &w, const CScriptVars<T> &v, F field)
    {
        Items(w,v.vars,field);
        Items(w,v.varNames,[&](const CString &name){Text(w,name);});
    }
    template <class T, class F> static void Vars(Reader &r, CScriptVars<T> &v, F field)
    {
        Items(r,v.vars,field);
        Items(r,v.varNames,[&](CString &name){Text(r,name);});
        if(v.varNames.GetSize()>v.vars.GetSize()) Invalid("script variable metadata length");
    }
    static void Variables(Writer &w, const SScriptVariablesSet &v)
    {
        Vars(w,v.fVars,[&](float x){w.Float32(x);});
        Vars(w,v.szVars,[&](const CString &x){Text(w,x);});
        Vars(w,v.oVars,[&](const CScriptObject &x){Object(w,x);});
        Vars(w,v.gVars,[&](const CScriptObjectGroup &x){Group(w,x);});
    }
    static void Variables(Reader &r, SScriptVariablesSet &v)
    {
        Vars(r,v.fVars,[&](float &x){x=r.Float32();});
        Vars(r,v.szVars,[&](CString &x){Text(r,x);});
        Vars(r,v.oVars,[&](CScriptObject &x){Object(r,x);});
        Vars(r,v.gVars,[&](CScriptObjectGroup &x){Group(r,x);});
    }
    void AddThread(SScriptThread &t)
    {
        threadTable.push_back(&t); threadIds.emplace(&t,static_cast<std::uint32_t>(threadTable.size()));
    }
    void AddScript(SOneScript &s)
    {
        scriptTable.push_back(&s); scriptIds.emplace(&s,static_cast<std::uint32_t>(scriptTable.size()));
    }
    void ThreadRef(Writer &w, const SScriptThread *t)
    {
        if (!t) {w.UInt32(0); return;}
        auto it=threadIds.find(t); if(it==threadIds.end()) Invalid("foreign script thread");
        w.UInt32(it->second);
    }
    SScriptThread *ThreadRef(Reader &r)
    {
        const auto id=r.UInt32();
        if(id>threadTable.size()) Invalid("dangling script thread");
        return id ? threadTable[id-1] : nullptr;
    }
    void ScriptRef(Writer &w, const SOneScript *s)
    {
        if(!s){w.UInt32(0);return;}
        auto it=scriptIds.find(s); if(it==scriptIds.end()) Invalid("foreign running script");
        w.UInt32(it->second);
    }
    SOneScript *ScriptRef(Reader &r)
    {
        const auto id=r.UInt32();
        if(id>scriptTable.size()) Invalid("dangling running script");
        return id ? scriptTable[id-1] : nullptr;
    }
    static void Param(Writer &w, const SScriptParam &p) { w.Value(p); }
    static void Param(Reader &r, SScriptParam &p)
    {
        r.Value(p);
        if(p.eType<ptNone || p.eType>ptGroup || p.nNumber<0 || p.nVarNumber<0)
            Invalid("invalid script parameter");
    }
    static void CheckSlot(const SSCCmd &c, EScriptParameterType type, int slot)
    {
        int count=0;
        switch(type){
        case ptFloat: count=c.params.GetSize();break;
        case ptString: count=c.szParams.GetSize();break;
        case ptObject: count=c.oParams.GetSize();break;
        case ptGroup: count=c.gParams.GetSize();break;
        default: Invalid("invalid script parameter kind");
        }
        if(slot<0 || slot>=count) Invalid("script parameter slot outside command");
    }
    void Command(Writer &w, const SSCCmd &c, unsigned depth=0)
    {
        if(depth>=128) Invalid("script command nesting limit");
        w.Int32(c.command);
        Items(w,c.params,[&](float x){w.Float32(x);});
        Items(w,c.szParams,[&](const CString &s){Text(w,s);});
        WriteCount(w,c.oParams.GetSize()); WriteCount(w,c.gParams.GetSize());
        Items(w,c.localParameters,[&](const SScriptParam &p){Param(w,p);});
        Items(w,c.globalParameters,[&](const SScriptParam &p){Param(w,p);});
        Items(w,c.funcParameters,[&](const SScriptFuncParam &p){
            w.Int32(p.eType); w.Int32(p.nNumber);
            if(!p.cmd) Invalid("null script function");
            Command(w,*p.cmd,depth+1);
        });
        ThreadRef(w,c.pLoopBody);
    }
    void Command(Reader &r, SSCCmd &c, unsigned depth=0)
    {
        if(depth>=128) Invalid("script command nesting limit");
        c.command=r.Int32();
        Items(r,c.params,[&](float &x){x=r.Float32();});
        Items(r,c.szParams,[&](CString &s){Text(r,s);});
        c.oParams.SetSize(r.Count()); c.gParams.SetSize(r.Count());
        for(int i=0;i<c.oParams.GetSize();++i)c.oParams[i]=nullptr;
        for(int i=0;i<c.gParams.GetSize();++i)c.gParams[i]=nullptr;
        Items(r,c.localParameters,[&](SScriptParam &p){Param(r,p);CheckSlot(c,p.eType,p.nNumber);});
        Items(r,c.globalParameters,[&](SScriptParam &p){Param(r,p);CheckSlot(c,p.eType,p.nNumber);});
        Items(r,c.funcParameters,[&](SScriptFuncParam &p){
            p.eType=static_cast<EScriptParameterType>(r.Int32()); p.nNumber=r.Int32();
            CheckSlot(c,p.eType,p.nNumber); Command(r,*p.cmd,depth+1);
        });
        c.pLoopBody=ThreadRef(r);
    }
    void Thread(Writer &w, const SScriptThread &t)
    {
        w.Int32(t.nIdx);w.UInt32(t.settlement);
        Items(w,t.commands,[&](const SSCCmd &c){Command(w,c);});
    }
    void Thread(Reader &r, SScriptThread &t)
    {
        t.nIdx=r.Int32();t.settlement=r.UInt32();
        Items(r,t.commands,[&](SSCCmd &c){Command(r,c);});
    }
    void One(Writer &w, const SOneScript &s)
    {
        w.Int32(s.nDelFlag); Text(w,s.szScriptName); Text(w,s.szScriptParams);
        Items(w,s.conditions,[&](const SScriptCondition &c){ThreadRef(w,c.pConditions);ThreadRef(w,c.pActions);});
        Variables(w,s.vars);
    }
    void One(Reader &r, SOneScript &s)
    {
        s.nDelFlag=r.Int32(); Text(r,s.szScriptName); Text(r,s.szScriptParams);
        Items(r,s.conditions,[&](SScriptCondition &c){
            c.pConditions=ThreadRef(r); c.pActions=ThreadRef(r);
            if(!c.pConditions || !c.pActions) Invalid("null script condition/action");
        });
        Variables(r,s.vars);
    }
    void State(Writer &w, const CScriptThreadState &s)
    {
        ThreadRef(w,s.pThread); w.Int32(s.nCurCommand); w.Int32(s.nSegment); w.Int32(s.nSleepStart);
        ScriptRef(w,s.pLinkedOneScript); Variables(w,s.variables);
    }
    void State(Reader &r, CScriptThreadState &s)
    {
        s.pThread=ThreadRef(r); s.nCurCommand=r.Int32(); s.nSegment=r.Int32(); s.nSleepStart=r.Int32();
        if(!s.pThread || s.nCurCommand<0 || s.nCurCommand>s.pThread->commands.GetSize())
            Invalid("invalid running script instruction");
        s.pLinkedOneScript=ScriptRef(r); Variables(r,s.variables);
        // No VoiceWait::AfterLoad here: saved waits are not rewritten while staging.
    }
    static void Light(Writer &w, const SScriptLightInfo &v)
    {
        w.GameReference(v.pLight);w.Int32(v.nScriptID);w.Bool(v.bAutoDelete!=FALSE);w.Bool(v.bIsMoving!=FALSE);
        w.Int32(v.nSegmentsToMove);w.Value(v.moveVector);
    }
    static void Light(Reader &r, SScriptLightInfo &v)
    {
        Pin(v.pLight,r.GameReferenceAs<CEffectPointLight>());v.nScriptID=r.Int32();v.bAutoDelete=r.Bool();v.bIsMoving=r.Bool();
        v.nSegmentsToMove=r.Int32();r.Value(v.moveVector);
    }
    static void Particle(Writer &w, const SParticleInfo &v)
    {
        w.GameReference(v.pParticle);w.Int32(v.nScriptID);w.Bool(v.bAutoDelete!=FALSE);w.Bool(v.bIsMoving!=FALSE);
        w.Int32(v.nSegmentsToMove);w.Value(v.moveVector);w.Int32(v.nLifeTime);
    }
    static void Particle(Reader &r, SParticleInfo &v)
    {
        Pin(v.pParticle,r.GameReferenceAs<CEffectParticle>());v.nScriptID=r.Int32();v.bAutoDelete=r.Bool();v.bIsMoving=r.Bool();
        v.nSegmentsToMove=r.Int32();r.Value(v.moveVector);v.nLifeTime=r.Int32();
    }
    static void Sound(Writer &w, const SFXInfo &v)
    {
        w.GameReference(v.pSound);w.Int32(v.nScriptID);Text(w,v.szSoundName);w.Value(v.cp);
        w.Float32(v.fpMinRadius);w.Float32(v.fpMaxRadius);w.Bool(v.bIsRandomized!=FALSE);w.Float32(v.fpRandRadius);
        w.Int32(v.nMinSegment);w.Int32(v.nMaxSegment);w.Int32(v.nDelay);
    }
    static void Sound(Reader &r, SFXInfo &v)
    {
        Pin(v.pSound,r.GameReferenceAs<CEffectSound>());v.nScriptID=r.Int32();Text(r,v.szSoundName);r.Value(v.cp);
        v.fpMinRadius=r.Float32();v.fpMaxRadius=r.Float32();v.bIsRandomized=r.Bool();v.fpRandRadius=r.Float32();
        v.nMinSegment=r.Int32();v.nMaxSegment=r.Int32();v.nDelay=r.Int32();
    }
    static void Lightning(Writer &w, const SLightningInfo &v){w.GameReference(v.pLightning);w.Int32(v.nScriptID);}
    static void Lightning(Reader &r, SLightningInfo &v){Pin(v.pLightning,r.GameReferenceAs<CEffectLightning>());v.nScriptID=r.Int32();}
    void Write(Writer &w)
    {
        // Save is taken between segments; stack-local for/function execution cannot be resumed.
        if(vm.nForCount) Invalid("script save requires segment boundary");
        POSITION p=vm.threads.GetHeadPosition();while(p)AddThread(vm.threads.GetNext(p));
        p=vm.worldActions.GetHeadPosition();while(p)AddThread(vm.worldActions.GetNext(p));
        for(int i=0;i<vm.scripts.GetSize();++i)AddScript(vm.scripts[i]);
        p=vm.runningScripts.GetHeadPosition();while(p)AddScript(vm.runningScripts.GetNext(p));
        WriteCount(w,vm.threads.GetCount()); WriteCount(w,vm.worldActions.GetCount());
        WriteCount(w,vm.scripts.GetSize()); WriteCount(w,vm.runningScripts.GetCount());
        for(auto *t:threadTable)Thread(w,*t);
        for(auto *s:scriptTable)One(w,*s);
        Variables(w,vm.globalVars);
        List(w,vm.runningThreads,[&](const CScriptThreadState &s){State(w,s);});
        Items(w,vm.lights,[&](const SScriptLightInfo &v){Light(w,v);});
        List(w,vm.particles,[&](const SParticleInfo &v){Particle(w,v);});
        List(w,vm.sfxes,[&](const SFXInfo &v){Sound(w,v);});
        List(w,vm.lightnings,[&](const SLightningInfo &v){Lightning(w,v);});
        w.Int32(vm.nPrevNegativeParticleSourceID);w.Int32(vm.nPrevNegativeSoundID);
        w.Bool(vm.bLoaded!=FALSE);w.Int32(vm.nSecondaryCount);w.Int32(vm.nForCount);
        Items(w,vm.scriptAreas,[&](const SScriptArea &a){w.Int32(a.nID);Array(w,a.areas);});
        List(w,vm.tempObjects,[&](const CScriptObject &o){Object(w,o);});
        List(w,vm.tempGroups,[&](const CScriptObjectGroup &g){Group(w,g);});
        Text(w,vm.scriptError.text);w.Int32(vm.scriptError.eError);w.Int32(vm.scriptError.nLine);
    }
    void Read(Reader &r)
    {
        if(vm.bLoaded || !vm.threads.IsEmpty() || !vm.worldActions.IsEmpty() || !vm.runningScripts.IsEmpty() ||
           !vm.runningThreads.IsEmpty() || vm.scripts.GetSize() || !vm.particles.IsEmpty() ||
           !vm.sfxes.IsEmpty() || !vm.lightnings.IsEmpty() || vm.scriptAreas.GetSize() ||
           vm.globalVars.fVars.GetSize() || vm.globalVars.szVars.GetSize() ||
           vm.globalVars.oVars.GetSize() || vm.globalVars.gVars.GetSize()) Invalid("script load target must be fresh");
        for(int i=0;i<vm.lights.GetSize();++i)if(vm.lights[i].pLight)Invalid("script load target owns effects");
        vm.detachedSaveState=true;
        Reset(vm);
        try {
            const auto normal=r.Count(), world=r.Count(), definitions=r.Count(), running=r.Count();
            for(std::uint32_t i=0;i<normal;++i)AddThread(vm.threads.GetAt(vm.threads.AddTail(SScriptThread{})));
            for(std::uint32_t i=0;i<world;++i)AddThread(vm.worldActions.GetAt(vm.worldActions.AddTail(SScriptThread{})));
            vm.scripts.SetSize(definitions);
            for(int i=0;i<vm.scripts.GetSize();++i)AddScript(vm.scripts[i]);
            for(std::uint32_t i=0;i<running;++i)AddScript(vm.runningScripts.GetAt(vm.runningScripts.AddTail(SOneScript{})));
            for(auto *t:threadTable)Thread(r,*t);
            for(auto *s:scriptTable)One(r,*s);
            Variables(r,vm.globalVars);
            List(r,vm.runningThreads,[&](CScriptThreadState &s){State(r,s);});
            Items(r,vm.lights,[&](SScriptLightInfo &v){Light(r,v);});
            List(r,vm.particles,[&](SParticleInfo &v){Particle(r,v);});
            List(r,vm.sfxes,[&](SFXInfo &v){Sound(r,v);});
            List(r,vm.lightnings,[&](SLightningInfo &v){Lightning(r,v);});
            vm.nPrevNegativeParticleSourceID=r.Int32();vm.nPrevNegativeSoundID=r.Int32();
            vm.bLoaded=r.Bool();vm.nSecondaryCount=r.Int32();vm.nForCount=r.Int32();
            if(vm.nForCount)Invalid("script save is not at segment boundary");
            Items(r,vm.scriptAreas,[&](SScriptArea &a){a.nID=r.Int32();Array(r,a.areas);});
            List(r,vm.tempObjects,[&](CScriptObject &o){Object(r,o);});
            List(r,vm.tempGroups,[&](CScriptObjectGroup &g){Group(r,g);});
            if(vm.tempObjects.IsEmpty() || vm.tempGroups.IsEmpty())Invalid("missing script scratch sentinel");
            Text(r,vm.scriptError.text);vm.scriptError.eError=static_cast<EScriptParserError>(r.Int32());vm.scriptError.nLine=r.Int32();
        } catch(...) {Reset(vm);throw;}
    }
};
} // namespace SaveSchema

void CScript::StoreScript(SaveSchema::Writer &w) const
{
    SaveSchema::ScriptCodec codec(const_cast<CScript &>(*this));
    codec.Write(w);
}
void CScript::LoadScript(SaveSchema::Reader &r) { SaveSchema::ScriptCodec(*this).Read(r); }
void CScript::DiscardSaveState()
{
    if (!detachedSaveState)
        throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid, "script is not detached save state");
    SaveSchema::ScriptCodec::Reset(*this);
}

namespace SaveSchema {
RandomState RandomState::Capture()
{
    RandomState state;
    state.ready = random.bIsReady != FALSE;
    if(state.ready) {
        state.count=random.randcnt;
        for(unsigned i=0;i<256;++i){state.results[i]=random.randrsl[i];state.memory[i]=random.randmem[i];}
        state.a=random.randa;state.b=random.randb;state.c=random.randc;
    }
    state.nativeCrt=ModNativeCrtRandom::Detail::State();
    return state;
}
void RandomState::Apply() const
{
    if(count>256)throw Error(ErrorCode::Invalid,"invalid ISAAC cursor");
    random.bIsReady=ready;
    random.randcnt=count;
    for(unsigned i=0;i<256;++i){random.randrsl[i]=results[i];random.randmem[i]=memory[i];}
    random.randa=a;random.randb=b;random.randc=c;
    ModNativeCrtRandom::Detail::State()=nativeCrt;
}
void RandomState::Store(Writer &w) const
{
    if(count>256)throw Error(ErrorCode::Invalid,"invalid ISAAC cursor");
    w.Bool(ready);w.UInt32(count);
    for(auto x:results)w.UInt32(x);
    for(auto x:memory)w.UInt32(x);
    w.UInt32(a);w.UInt32(b);w.UInt32(c);w.UInt32(nativeCrt);
}
void RandomState::Load(Reader &r)
{
    ready=r.Bool();count=r.UInt32();
    if(count>256)throw Error(ErrorCode::Invalid,"invalid ISAAC cursor");
    for(auto &x:results)x=r.UInt32();
    for(auto &x:memory)x=r.UInt32();
    a=r.UInt32();b=r.UInt32();c=r.UInt32();nativeCrt=r.UInt32();
}
} // namespace SaveSchema

// P16b payloads. World/slot publication and graph ownership belong to P17.
namespace SaveSchema { namespace {
template <class T, class A> void ValueList(Writer &w, const CList<T,A> &a)
{
    WriteCount(w,a.GetCount());
    POSITION pos=a.GetHeadPosition();
    while(pos) w.Value(a.GetNext(pos));
}
template <class T, class A> void ValueList(Reader &r, CList<T,A> &a)
{
    a.RemoveAll();
    const auto count=r.Count();
    for(std::uint32_t i=0;i<count;++i) r.Value(a.GetAt(a.AddTail(T{})));
}
} }

void SUnitBasicCmd::Store(SaveSchema::Writer &w) const
{
    w.Int32(cmd); w.Int32(nQSpellNum);
    w.GameReference(static_cast<CMapObject *>(pTarget));
    w.Value(ptTarget); w.Bool(bUsePoint);
}
void SUnitBasicCmd::Load(SaveSchema::Reader &r)
{
    cmd=static_cast<EUnitCommand>(r.Int32()); nQSpellNum=r.Int32();
    pTarget=r.GameReferenceAs<CMapObject>(); r.Value(ptTarget); bUsePoint=r.Bool();
    if(cmd<UC_IDLE || cmd>UC_REST) throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid,"invalid unit command");
}
void SUnitCommand::Store(SaveSchema::Writer &w) const
{
    SUnitBasicCmd::Store(w);
    // The parameter is an untagged float/DWORD union. Preserve its 32 bits,
    // including integer masks and NaNs, without accessing the inactive member.
    std::uint32_t bits; std::memcpy(&bits,&fpParam,4);
    w.UInt32(bits); w.Bool(bDoItFast);
}
void SUnitCommand::Load(SaveSchema::Reader &r)
{
    SUnitBasicCmd::Load(r);
    const auto bits=r.UInt32(); std::memcpy(&fpParam,&bits,4); bDoItFast=r.Bool();
}
void SL0Animation::Store(SaveSchema::Writer &w) const
{
    w.UInt8(bStart); w.UInt8(bCyclic); w.UInt8(bSoundSet);
    w.Int32(nLength); w.Int32(animationSpeed);
}
void SL0Animation::Load(SaveSchema::Reader &r)
{
    bStart=r.UInt8(); bCyclic=r.UInt8(); bSoundSet=r.UInt8();
    nLength=r.Int32(); animationSpeed=static_cast<EUnitAnimationSpeed>(r.Int32());
}
void SL0AnimationSeq::Store(SaveSchema::Writer &w) const
{
    // Full indices and the pump marker must survive independently of byte IDs.
    SaveSchema::WriteCount(w,animations.GetSize());
    for(int i=0;i<animations.GetSize();++i) {
        w.Value(animations[i]); w.UInt32(GetStartId(i)); w.UInt32(GetCycleId(i));
    }
    w.Int32(nStartSegment);
}
void SL0AnimationSeq::Load(SaveSchema::Reader &r)
{
    Clear(); const auto count=r.Count();
    for(std::uint32_t i=0;i<count;++i) {
        SL0Animation animation; r.Value(animation);
        const auto start=r.UInt32(), cycle=r.UInt32(); Append(animation,start,cycle);
    }
    nStartSegment=r.Int32();
}
void SL1Pose::Store(SaveSchema::Writer &w) const
{
    w.Int32(pose); w.Bool(bAggressive); w.UInt8(bRun);
}
void SL1Pose::Load(SaveSchema::Reader &r)
{
    pose=static_cast<EUnitPose>(r.Int32()); bAggressive=r.Bool(); bRun=r.UInt8();
}
void SL1Command::Store(SaveSchema::Writer &w) const
{
    w.Value(attackAnimation); w.Int32(nTimeStrikeMoment); w.Int32(nHitTime);
    w.MapReference(nTargetID); w.Float32(fpHitDist2); w.Bool(bCast);
    w.Int32(eAttackLocation); w.Bool(bCalledShot); w.Int32(castType); w.Value(ptDir);
}
void SL1Command::Load(SaveSchema::Reader &r)
{
    r.Value(attackAnimation); nTimeStrikeMoment=r.Int32(); nHitTime=r.Int32();
    nTargetID=r.MapReference(); fpHitDist2=r.Float32(); bCast=r.Bool();
    eAttackLocation=static_cast<ERPGHitLocations>(r.Int32()); bCalledShot=r.Bool();
    castType=static_cast<ERPGMagicSubtypes>(r.Int32()); r.Value(ptDir);
}
void SContSpellInfo::Store(SaveSchema::Writer &w) const
{
    w.GameReference(pItem); w.Int32(nDelay); w.Bool(bIsUsed);
}
void SContSpellInfo::Load(SaveSchema::Reader &r)
{
    pItem=r.GameReferenceAs<CItem>(); nDelay=r.Int32(); bIsUsed=r.Bool();
}
void STrainInfo::Store(SaveSchema::Writer &w) const
{
    w.Bool(bIsTrain); w.GameReference(master);
}
void STrainInfo::Load(SaveSchema::Reader &r)
{
    bIsTrain=r.Bool(); master=r.GameReferenceAs<CUnit>();
}
void SRectRegion::Store(SaveSchema::Writer &w) const
{
    w.Int32(nLowX); w.Int32(nHighX); w.Int32(nLowY); w.Int32(nHighY);
}
void SRectRegion::Load(SaveSchema::Reader &r)
{
    nLowX=r.Int32(); nHighX=r.Int32(); nLowY=r.Int32(); nHighY=r.Int32();
}
void SRouteUnit::Store(SaveSchema::Writer &w) const
{
    w.UInt16(wX); w.UInt16(wY); w.UInt16(wSpeed256); w.UInt16(wFlags);
}
void SRouteUnit::Load(SaveSchema::Reader &r)
{
    wX=r.UInt16(); wY=r.UInt16(); wSpeed256=r.UInt16(); wFlags=r.UInt16();
}
void CAIRoute::Store(SaveSchema::Writer &w) const { SaveSchema::ValueList(w,route); }
void CAIRoute::Load(SaveSchema::Reader &r) { SaveSchema::ValueList(r,route); }
void CAIFullRoute::Store(SaveSchema::Writer &w) const
{
    w.Int32(moveType); w.Value(start); w.Float32(fpStartAngle); w.Int32(nStartSegment);
    // Only the active movement branch, never the largest union image.
    switch(moveType) {
    case Stand: break;
    case Walk: w.Float32(walk.fpFinishX); w.Float32(walk.fpFinishY); break;
    case Rotate: w.Float32(rotate.fpFinalAngle); w.Float32(rotate.fpAngleSpeed); break;
    case Attack: w.MapReference(attack.nTargetID); w.Int32(attack.nHitSegment); w.Int32(attack.nTurnFinishSegment); break;
    case AttackPoint: w.Float32(attack1.fpFinalAngle); w.Int32(attack1.nHitSegment); w.Int32(attack1.nTurnFinishSegment); break;
    default: throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid,"invalid movement branch");
    }
    w.Value(route);
}
void CAIFullRoute::Load(SaveSchema::Reader &r)
{
    Clear(); moveType=static_cast<EMovement>(r.Int32()); r.Value(start);
    fpStartAngle=r.Float32(); nStartSegment=r.Int32();
    switch(moveType) {
    case Stand: break;
    case Walk: walk.fpFinishX=r.Float32(); walk.fpFinishY=r.Float32(); break;
    case Rotate: rotate.fpFinalAngle=r.Float32(); rotate.fpAngleSpeed=r.Float32(); break;
    case Attack: attack.nTargetID=r.MapReference(); attack.nHitSegment=r.Int32(); attack.nTurnFinishSegment=r.Int32(); break;
    case AttackPoint: attack1.fpFinalAngle=r.Float32(); attack1.nHitSegment=r.Int32(); attack1.nTurnFinishSegment=r.Int32(); break;
    default: throw SaveSchema::Error(SaveSchema::ErrorCode::Invalid,"invalid movement branch");
    }
    r.Value(route);
}
void SRouteSegment::Store(SaveSchema::Writer &w) const
{
    w.Value(start); w.Value(startV); w.Value(finish); w.Value(finishV);
    w.Float32(fpAlpha); w.Float32(fpAlphaInc);
}
void SRouteSegment::Load(SaveSchema::Reader &r)
{
    r.Value(start); r.Value(startV); r.Value(finish); r.Value(finishV);
    fpAlpha=r.Float32(); fpAlphaInc=r.Float32();
}
void SWalkerWaypoint::Store(SaveSchema::Writer &w) const
{
    w.Value(point); w.Value(speed); w.Float32(fpSpeed); w.Int32(nTileX); w.Int32(nTileY);
}
void SWalkerWaypoint::Load(SaveSchema::Reader &r)
{
    r.Value(point); r.Value(speed); fpSpeed=r.Float32(); nTileX=r.Int32(); nTileY=r.Int32();
}
void SWalkerTempPoint::Store(SaveSchema::Writer &w) const { w.Value(point); w.Int32(nNumber); }
void SWalkerTempPoint::Load(SaveSchema::Reader &r) { r.Value(point); nNumber=r.Int32(); }
void CRouteWalker::Store(SaveSchema::Writer &w) const
{
    SaveSchema::Array(w,points); SaveSchema::Array(w,tpoints);
    SaveSchema::WriteCount(w,speeds.GetSize());
    for(int i=0;i<speeds.GetSize();++i) w.Float32(speeds[i]);
    w.Float32(fpBaseSpeed); w.Float32(fpAngularSpeed); w.Float32(fpActiveAngularSpeed);
    w.Float32(fpStartAngle); w.Float32(fpDeltaAngle);
    // cache is derived from points and deliberately rebuilt, like the old Load.
}
void CRouteWalker::Load(SaveSchema::Reader &r)
{
    cache.Clear(); SaveSchema::Array(r,points); SaveSchema::Array(r,tpoints);
    speeds.SetSize(r.Count()); for(int i=0;i<speeds.GetSize();++i) speeds[i]=r.Float32();
    fpBaseSpeed=r.Float32(); fpAngularSpeed=r.Float32(); fpActiveAngularSpeed=r.Float32();
    fpStartAngle=r.Float32(); fpDeltaAngle=r.Float32();
}
void SAIPath::Store(SaveSchema::Writer &w) const
{
    w.Value(toGo); w.Value(curSegment); w.Value(ptFinish); w.Value(ptDestination);
    w.Bool(bFinished); w.Bool(bRouteFailed); w.Bool(bTryAnyPath); w.Bool(bFirstTry);
    SaveSchema::WriteCount(w,excludeVertices.GetSize());
    for(int i=0;i<excludeVertices.GetSize();++i) {
        w.Int32(excludeVertices[i].nX); w.Int32(excludeVertices[i].nY);
    }
    w.Int32(nPathCost);
}
void SAIPath::Load(SaveSchema::Reader &r)
{
    r.Value(toGo); r.Value(curSegment); r.Value(ptFinish); r.Value(ptDestination);
    bFinished=r.Bool(); bRouteFailed=r.Bool(); bTryAnyPath=r.Bool(); bFirstTry=r.Bool();
    excludeVertices.SetSize(r.Count());
    for(int i=0;i<excludeVertices.GetSize();++i) {
        excludeVertices[i].nX=r.Int32(); excludeVertices[i].nY=r.Int32();
    }
    nPathCost=r.Int32();
}
void CWaterLevels::Store(SaveSchema::Writer &w) const
{
    SaveSchema::WriteCount(w,levels.GetSize());
    for(int i=0;i<levels.GetSize();++i) {
        const auto &v=levels[i];
        // nObjectID is a terrain/placement index, not a CGameBase reference.
        w.Int32(v.nObjectID); w.Int32(v.nSegments); w.Float32(v.fpH); w.Float32(v.fpDH);
    }
}
void CWaterLevels::Load(SaveSchema::Reader &r)
{
    levels.SetSize(r.Count());
    for(int i=0;i<levels.GetSize();++i) {
        auto &v=levels[i]; v.nObjectID=r.Int32(); v.nSegments=r.Int32();
        v.fpH=r.Float32(); v.fpDH=r.Float32();
    }
}
void CFofTracker::Store(SaveSchema::Writer &w) const
{
    for(auto v:allyMatrix) w.UInt32(v);
    for(auto v:enemyMatrix) w.UInt32(v);
}
void CFofTracker::Load(SaveSchema::Reader &r)
{
    for(auto &v:allyMatrix) v=r.UInt32();
    for(auto &v:enemyMatrix) v=r.UInt32();
}
void CAIAcknowledgements::Store(SaveSchema::Writer &w) const
{
    w.Int32(curSegmentNeighborAck); w.Int32(wantAck); w.Int32(curAck); w.Int32(nCurAckTimeLeft);
}
void CAIAcknowledgements::Load(SaveSchema::Reader &r)
{
    curSegmentNeighborAck=static_cast<EAIAcks>(r.Int32()); wantAck=static_cast<EAIAcks>(r.Int32());
    curAck=static_cast<EAIAcks>(r.Int32()); nCurAckTimeLeft=r.Int32();
}
void SAITurnLookOrder::Store(SaveSchema::Writer &w) const
{
    w.Float32(fpAngle); w.Float32(fpTurnSpeed); w.Float32(fpTime); w.Bool(bRest);
}
void SAITurnLookOrder::Load(SaveSchema::Reader &r)
{
    fpAngle=r.Float32(); fpTurnSpeed=r.Float32(); fpTime=r.Float32(); bRest=r.Bool();
}
void SAIRallyPoint::Store(SaveSchema::Writer &w) const { w.Value(ptPoint); SaveSchema::Array(w,lookOrders); }
void SAIRallyPoint::Load(SaveSchema::Reader &r) { r.Value(ptPoint); SaveSchema::Array(r,lookOrders); }
void SCameraPlacement::Store(SaveSchema::Writer &w) const
{
    w.Float32(fpTime); w.Float32(step); w.Value(plot); w.Value(quat);
}
void SCameraPlacement::Load(SaveSchema::Reader &r)
{
    fpTime=r.Float32(); step=r.Float32(); r.Value(plot); r.Value(quat);
}
void SCameraShortcut::Store(SaveSchema::Writer &w) const
{
    w.Float32(fDistance); w.Value(qRotate); w.Float32(fPitch);
}
void SCameraShortcut::Load(SaveSchema::Reader &r)
{
    fDistance=r.Float32(); r.Value(qRotate); fPitch=r.Float32();
}
namespace SaveSchema {
void ShopCoefficients::Store(Writer &w) const { for(float v:values) w.Float32(v); }
void ShopCoefficients::Load(Reader &r) { for(float &v:values) v=r.Float32(); }
    static_assert(NUM_SHOP_KOEFFS==10,"shop payload width changed");
ValueType ValueCodec<SUnitBasicCmd>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SUnitBasicCmd *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SUnitBasicCmd *>(p)->Load(r); }};
}
ValueType ValueCodec<SUnitCommand>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SUnitCommand *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SUnitCommand *>(p)->Load(r); }};
}
ValueType ValueCodec<SL0Animation>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SL0Animation *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SL0Animation *>(p)->Load(r); }};
}
ValueType ValueCodec<SL0AnimationSeq>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SL0AnimationSeq *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SL0AnimationSeq *>(p)->Load(r); }};
}
ValueType ValueCodec<SL1Pose>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SL1Pose *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SL1Pose *>(p)->Load(r); }};
}
ValueType ValueCodec<SL1Command>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SL1Command *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SL1Command *>(p)->Load(r); }};
}
ValueType ValueCodec<SContSpellInfo>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SContSpellInfo *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SContSpellInfo *>(p)->Load(r); }};
}
ValueType ValueCodec<STrainInfo>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const STrainInfo *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<STrainInfo *>(p)->Load(r); }};
}
ValueType ValueCodec<SRectRegion>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRectRegion *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRectRegion *>(p)->Load(r); }};
}
ValueType ValueCodec<SRouteUnit>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRouteUnit *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRouteUnit *>(p)->Load(r); }};
}
ValueType ValueCodec<CAIRoute>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const CAIRoute *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<CAIRoute *>(p)->Load(r); }};
}
ValueType ValueCodec<CAIFullRoute>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const CAIFullRoute *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<CAIFullRoute *>(p)->Load(r); }};
}
ValueType ValueCodec<SRouteSegment>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SRouteSegment *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SRouteSegment *>(p)->Load(r); }};
}
ValueType ValueCodec<SWalkerWaypoint>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SWalkerWaypoint *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SWalkerWaypoint *>(p)->Load(r); }};
}
ValueType ValueCodec<SWalkerTempPoint>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SWalkerTempPoint *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SWalkerTempPoint *>(p)->Load(r); }};
}
ValueType ValueCodec<CRouteWalker>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const CRouteWalker *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<CRouteWalker *>(p)->Load(r); }};
}
ValueType ValueCodec<SAIPath>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SAIPath *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SAIPath *>(p)->Load(r); }};
}
ValueType ValueCodec<CWaterLevels>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const CWaterLevels *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<CWaterLevels *>(p)->Load(r); }};
}
ValueType ValueCodec<CFofTracker>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const CFofTracker *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<CFofTracker *>(p)->Load(r); }};
}
ValueType ValueCodec<CAIAcknowledgements>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const CAIAcknowledgements *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<CAIAcknowledgements *>(p)->Load(r); }};
}
ValueType ValueCodec<SAITurnLookOrder>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SAITurnLookOrder *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SAITurnLookOrder *>(p)->Load(r); }};
}
ValueType ValueCodec<SAIRallyPoint>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SAIRallyPoint *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SAIRallyPoint *>(p)->Load(r); }};
}
ValueType ValueCodec<ShopCoefficients>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const ShopCoefficients *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<ShopCoefficients *>(p)->Load(r); }};
}
ValueType ValueCodec<SCameraPlacement>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SCameraPlacement *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SCameraPlacement *>(p)->Load(r); }};
}
ValueType ValueCodec<SCameraShortcut>::Get()
{
    return {
        [](Writer &w, const void *p) { static_cast<const SCameraShortcut *>(p)->Store(w); },
        [](Reader &r, void *p) { static_cast<SCameraShortcut *>(p)->Load(r); }};
}
ValueType ValueCodec<CQuaternion>::Get()
{
    return {
        [](Writer &w,const void *p) {
            float qw,qx,qy,qz; static_cast<const CQuaternion *>(p)->GetComponents(qw,qx,qy,qz);
            w.Float32(qw); w.Float32(qx); w.Float32(qy); w.Float32(qz);
        },
        [](Reader &r,void *p) {
            const auto qw=r.Float32(),qx=r.Float32(),qy=r.Float32(),qz=r.Float32();
            *static_cast<CQuaternion *>(p)=CQuaternion(qw,qx,qy,qz);
        }};
}
ValueType ValueCodec<SCameraMovement<float>>::Get()
{
    return {
        [](Writer &w,const void *p) {
            const auto &v=*static_cast<const SCameraMovement<float> *>(p);
            w.Float32(v.value); w.Float32(v.desired); w.Float32(v.fpAccelerationCoeff);
        },
        [](Reader &r,void *p) {
            auto &v=*static_cast<SCameraMovement<float> *>(p);
            v.value=r.Float32(); v.desired=r.Float32(); v.fpAccelerationCoeff=r.Float32();
        }};
}
ValueType ValueCodec<SCameraMovement<CPlot3D>>::Get()
{
    return {
        [](Writer &w,const void *p) {
            const auto &v=*static_cast<const SCameraMovement<CPlot3D> *>(p);
            w.Value(v.value); w.Value(v.desired); w.Float32(v.fpAccelerationCoeff);
        },
        [](Reader &r,void *p) {
            auto &v=*static_cast<SCameraMovement<CPlot3D> *>(p);
            r.Value(v.value); r.Value(v.desired); v.fpAccelerationCoeff=r.Float32();
        }};
}
ValueType ValueCodec<SComplection>::Get()
{
    return {
        [](Writer &w,const void *p) {
            const auto &v=*static_cast<const SComplection *>(p);
            w.Float32(v.strength); w.Float32(v.grace); w.Float32(v.tall);
        },
        [](Reader &r,void *p) {
            auto &v=*static_cast<SComplection *>(p);
            v.strength=r.Float32(); v.grace=r.Float32(); v.tall=r.Float32();
        }};
}
ValueType ValueCodec<SUnitVisibilityInfo>::Get()
{
    return {
        [](Writer &w,const void *p) {
            const auto &v=*static_cast<const SUnitVisibilityInfo *>(p);
            w.Float32(v.fpSight); w.Float32(v.fpSmell); w.Float32(v.fpSound);
            w.Float32(v.fpTracking); w.Float32(v.fpSenseLife);
        },
        [](Reader &r,void *p) {
            auto &v=*static_cast<SUnitVisibilityInfo *>(p);
            v.fpSight=r.Float32(); v.fpSmell=r.Float32(); v.fpSound=r.Float32();
            v.fpTracking=r.Float32(); v.fpSenseLife=r.Float32();
        }};
}
ValueType ValueCodec<SVisibilityMask>::Get()
{
    return {
        [](Writer &w,const void *p) {
            const auto &v=*static_cast<const SVisibilityMask *>(p);
            const auto x=v.data.GetXSize(), y=v.data.GetYSize();
            const auto cells=static_cast<std::uint64_t>(x)*y;
            if(x<=0 || y<=0 || cells>INT32_MAX) throw Error(ErrorCode::Limit,"visibility dimensions");
            w.Int32(v.nShiftX); w.Int32(v.nShiftY); w.Count(x); w.Count(y); w.Count(static_cast<std::uint32_t>(cells));
            for(std::uint32_t i=0;i<cells;++i) w.UInt8(static_cast<unsigned char>(v.data.GetBuffer()[i]));
        },
        [](Reader &r,void *p) {
            auto &v=*static_cast<SVisibilityMask *>(p);
            v.nShiftX=r.Int32(); v.nShiftY=r.Int32();
            const auto x=r.Count(), y=r.Count(), cells=r.Count();
            if(!x || !y || static_cast<std::uint64_t>(x)*y!=cells || cells>INT32_MAX)
                throw Error(ErrorCode::Invalid,"visibility dimensions");
            v.data.SetSizes(x,y);
            for(std::uint32_t i=0;i<cells;++i) {
                const auto bits=r.UInt8(); std::memcpy(v.data.GetBuffer()+i,&bits,1);
            }
        }};
}
ValueType ValueCodec<SAIMark>::Get()
{
    return {
        [](Writer &w,const void *p) {
            const auto &v=*static_cast<const SAIMark *>(p);
            w.Value(v.ptPosition); w.Value(v.ptPointPosition); w.Int32(v.nTime);
            for(float f:v.fpStrength) w.Float32(f);
            w.MapReference(v.nUnitID); w.Int32(v.nPlayer);
        },
        [](Reader &r,void *p) {
            auto &v=*static_cast<SAIMark *>(p);
            // P17 owns the mark lists. Link only after every value is loaded.
            v.pNext=v.pPrev=v.pNextDel=nullptr;
            r.Value(v.ptPosition); r.Value(v.ptPointPosition); v.nTime=r.Int32();
            for(float &f:v.fpStrength) f=r.Float32();
            v.nUnitID=r.MapReference(); v.nPlayer=r.Int32();
        }};
}
ValueType ValueCodec<SUnitWearsStatistics>::Get()
{
    return {
        [](Writer &w,const void *p) {
            const auto &v=*static_cast<const SUnitWearsStatistics *>(p);
            w.UInt16(v.wPrototype);
            WriteCount(w,v.armors.size()); for(auto id:v.armors) w.UInt32(id);
            WriteCount(w,v.weapons.size()); for(auto id:v.weapons) w.UInt32(id);
            w.UInt8(static_cast<unsigned char>(v.cCurrentWeaponIndex)); w.MapReference(v.nID);
        },
        [](Reader &r,void *p) {
            auto &v=*static_cast<SUnitWearsStatistics *>(p);
            v.wPrototype=r.UInt16();
            v.armors.resize(r.Count()); for(auto &id:v.armors) id=r.UInt32();
            v.weapons.resize(r.Count()); for(auto &id:v.weapons) id=r.UInt32();
            const auto bits=r.UInt8(); std::memcpy(&v.cCurrentWeaponIndex,&bits,1); v.nID=r.MapReference();
        }};
}
ValueType ValueCodec<SUnitStatistics>::Get()
{
    return {
        [](Writer &w,const void *p) {
            const auto &v=*static_cast<const SUnitStatistics *>(p);
            w.Value(static_cast<const SUnitWearsStatistics &>(v));
            w.Float32(v.fStrength); w.Float32(v.fGrace); w.Float32(v.fTall);
            WriteCount(w,v.szBPs.size()); for(const auto &name:v.szBPs) Text(w,name);
        },
        [](Reader &r,void *p) {
            auto &v=*static_cast<SUnitStatistics *>(p);
            r.Value(static_cast<SUnitWearsStatistics &>(v));
            v.fStrength=r.Float32(); v.fGrace=r.Float32(); v.fTall=r.Float32();
            v.szBPs.resize(r.Count()); for(auto &name:v.szBPs) Text(r,name);
        }};
}
} // namespace SaveSchema

void CScript::AdoptSaveState()
{
    for (POSITION p=runningThreads.GetHeadPosition();p;) VoiceWait::AfterLoad(runningThreads.GetNext(p).nSleepStart);
    detachedSaveState=false;
}
