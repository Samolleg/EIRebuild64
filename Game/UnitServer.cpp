#include "StdAfx.h"
#include "SaveSchema.h"
#include "CorePerf.h"
#include "GameJournal.h"
#include "ModNativeCrtRandom.h"
#include "NumericPolicy.h"
#include "EngineDiagnostics.h"
#include "ModPackage.h"
#include "ModSpellRules.h"
#include "ModMonsterProjectiles.h"
#include "ModTacticalSpells.h"
#include "ModCombat.h"
#include "ModConditionalCombat.h"
#include "ModEquipmentSwitch.h"
#include "ModAttackTiming.h"
#include "ModAttackEvents.h"
#include "ModInvincible.h"
#include "ModPreparedAttack.h"
#include "ModItemSpellPolicy.h"
#include "ModStealingPolicy.h"
#include "ModStatRules.h"
#include "ModRuntime.h"
#include "ModOutOfCombatRegeneration.h"
#include "StuffMapObjects.h"
#include <cctype>
#include <cmath>
#include <string>
#include <sstream>
#include "ModRuntime.h"
static const char LOCAL_FILE[] = __FILE__;
namespace {
// Additional unit melee separation, shared by approach and attack planning.
constexpr float FP_MELEE_SPACING = 0.15f;

bool IsEquippedRangedWeapon(CItemWeapon *weapon)
{
    if (!weapon)
        return false;
    const ERPGWeaponTypes type = weapon->GetWeaponType();
    if (type == RPGWST_BOW || type == RPGWST_CROSSBOW)
        return true;
    const CModPackage *mod = ModRuntime::Current();
    return mod && mod->SpellAddon().Feature23() == 1 &&
           (type == RPGWST_HANDS3 || type == RPGWST_HANDS7);
}
}
/////////////////////////////////////////////////////////////////////////////////////
/*==========================================================================================================

        Server specific unit class implementation

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/

/////////////////////////////////////////////////////////////////////////////////////
// радиус, на котором юниты слышат друг друга
const float FP_FOF_HEARRADIUS = 20;
// коэффициент прозрачности, начианая с которого возможна атака
const float FP_MIN_ATTACK_OPACITY = 0.1f;
/////////////////////////////////////////////////////////////////////////////////////
#include "Hoover.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "UnitServer.h"
#include "MapObjectServer.h"
#include "mapFormat.h"
#include "ServerConsts.h"
#include "player.h"
#include "aiMap.h"
#include "events.h"
#include "PlayerServer.h"
#include "randomGen.h"
#include "mapObjectManager.h"
#include "console.h"
#include "effect.h"
#include "EffectManager.h"
#include "ParticleManager.h"
#include "ackFormat.h"
#include "ItemManager.h"
#include "aiSettings.h"
#include "WorldInterface.h"
#include "ModMessages.h"
#include "ModAnimationLoop.h"
/////////////////////////////////////////////////////////////////////////////////////
#ifndef _DEBUG
#pragma optimize("agw", on)
#endif
/////////////////////////////////////////////////////////////////////////////////////
static ostream &aiLog = NConsole::ai;
/////////////////////////////////////////////////////////////////////////////////////
// N_MAXIMUM_HIT_FRAME - максимальное значение кадра в анимации на котором наносится удар
const int N_MAXIMUM_HIT_FRAME = 16;
/////////////////////////////////////////////////////////////////////////////////////
const float FP_CHECK_VISIBILITY_HEIGHT = 1; // высота точки над террейном в которую
const float FP_MINIMAL_DISTANCE_TO_POU =
    0.2f; // максимально допустимое расстояние до точки использования
// трейсится луч в случае необходимости прямой видимости цели для спела
/////////////////////////////////////////////////////////////////////////////////////
// SUnitCommand

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// SL0AnimationSeq
SL0AnimationSeq &SL0AnimationSeq::operator+=(const SL0AnimationSeq &add)
{
    for (int i = 0; i < add.animations.GetSize(); i++)
    {
        auto &a = add.animations[i];
        if (a.bStart != 0xff)
        {
            if (a.bCyclic == 0xff)
                AddSimple(add.GetStartId(i), a.nLength);
            else
                Append(a, add.GetStartId(i), add.GetCycleId(i));
        }
        else if (a.bCyclic == 0xfe && add.GetCycleId(i) == 0xffffffffu)
            AddPumpCycle();
        else
            AddCycle(add.GetCycleId(i));
    }
    return *this;
}
/////////////////////////////////////////////////////////////////////////////////////
EUnitAnimationSpeed SL0AnimationSeq::GetAnimationSpeed()
{
    if (animations.GetSize() == 0)
        return UAS_NORMAL_;
    return animations[0].animationSpeed;
}
/////////////////////////////////////////////////////////////////////////////////////
void SL0AnimationSeq::EnsureIds()
{
    while (startIds.GetSize() < animations.GetSize())
    {
        const int index = startIds.GetSize();
        startIds.Add(animations[index].bStart == 0xff ? 0xffffffffu : animations[index].bStart);
        cycleIds.Add(animations[index].bCyclic == 0xff ? 0xffffffffu : animations[index].bCyclic);
    }
}
DWORD SL0AnimationSeq::GetStartId(int index) const
{
    return index < startIds.GetSize() ? startIds[index] : (animations[index].bStart == 0xff ? 0xffffffffu : animations[index].bStart);
}
DWORD SL0AnimationSeq::GetCycleId(int index) const
{
    return index < cycleIds.GetSize() ? cycleIds[index] : (animations[index].bCyclic == 0xff ? 0xffffffffu : animations[index].bCyclic);
}
void SL0AnimationSeq::SetStartId(int index, DWORD id)
{
    EnsureIds();
    startIds[index] = id;
    animations[index].bStart = id == 0xffffffffu ? 0xff :
        (id > 253 ? 0xfe : BYTE(id));
}
void SL0AnimationSeq::SetCycleId(int index, DWORD id)
{
    EnsureIds();
    cycleIds[index] = id;
    animations[index].bCyclic = id == 0xffffffffu ? 0xff :
        (id > 253 ? 0xfe : BYTE(id));
}
void SL0AnimationSeq::Append(const SL0Animation &animation, DWORD startId, DWORD cycleId)
{
    EnsureIds();
    animations.Add(animation);
    startIds.Add(startId);
    cycleIds.Add(cycleId);
}
void SL0AnimationSeq::AddSimple(DWORD bStart, int nLength)
{
    SL0Animation animation;
    animation.nLength = nLength;
    Append(animation, bStart, 0xffffffffu);
    SetStartId(animations.GetSize() - 1, bStart);
}
/////////////////////////////////////////////////////////////////////////////////////
void SL0AnimationSeq::AddCycle(DWORD bCycle)
{
    if (animations.GetSize() == 0 ||
        GetCycleId(animations.GetSize() - 1) != 0xffffffffu)
        Append(SL0Animation(), 0xffffffffu, 0xffffffffu);
    SL0Animation &a = animations[animations.GetSize() - 1];
    SetCycleId(animations.GetSize() - 1, bCycle);
    a.nLength = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void SL0AnimationSeq::AddPumpCycle()
{
    AddCycle(0xfe);
    cycleIds[animations.GetSize() - 1] = 0xffffffffu;
}
/////////////////////////////////////////////////////////////////////////////////////
void SL0AnimationSeq::SetSpeed(EUnitAnimationSpeed speed)
{
    for (int i = 0; i < animations.GetSize(); i++)
        animations[i].animationSpeed = speed;
}
/////////////////////////////////////////////////////////////////////////////////////
void SL0AnimationSeq::SetSoundSet(BYTE bSoundSet)
{
    for (int i = 0; i < animations.GetSize(); i++)
        animations[i].bSoundSet = bSoundSet;
}
/////////////////////////////////////////////////////////////////////////////////////
bool SL0AnimationSeq::IsEndless()
{
    for (int i = 0; i < animations.GetSize(); i++)
    {
        if (animations[i].nLength < 0)
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// returns -1 if animation is endless
int SL0AnimationSeq::GetLength()
{
    int nLeng = 0;
    for (int i = 0; i < animations.GetSize(); i++)
    {
        if (animations[i].nLength < 0)
            return -1;
        nLeng += animations[i].nLength;
    }
    return nLeng;
}
/////////////////////////////////////////////////////////////////////////////////////
bool SL0AnimationSeq::IsSame(SL0AnimationSeq &cmp)
{
    if (animations.GetSize() != cmp.animations.GetSize())
        return false;
    for (int i = 0; i < animations.GetSize(); i++)
    {
        if (animations[i] != cmp.animations[i] ||
            GetStartId(i) != cmp.GetStartId(i) || GetCycleId(i) != cmp.GetCycleId(i))
            return false;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////
// SL0Command
void SL1Command::Clear()
{
    attackAnimation.Clear();
    nTargetID = -1;
    nTimeStrikeMoment = nHitTime = 0;
    eAttackLocation = RPGHL_BODY;
    fpHitDist2 = 1e38f;
    bCast = false;
    bCalledShot = false;
    castType = RPGMST_UNKNOWN;
}
/////////////////////////////////////////////////////////////////////////////////////
void SL1Command::CreateCastAttack(SL0AnimationSeq animation, int nTime, int nHit,
                                  ERPGMagicSubtypes magicType)
{
    Clear();
    nTimeStrikeMoment = nTime;
    nHitTime = nHit + nTime;
    attackAnimation = animation;
    bCast = true;
    castType = magicType;
}
/////////////////////////////////////////////////////////////////////////////////////
void SL1Command::CreateAimlessAttack(SL0AnimationSeq animation, int nTime, int nHit)
{
    Clear();
    nTimeStrikeMoment = nTime;
    nHitTime = nHit + nTime;
    attackAnimation = animation;
    bCast = false;
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// SContSpellInfo
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// STrainInfo
/////////////////////////////////////////////////////////////////////////////////////
void STrainInfo::RemoveReferencesOn(CMapObject *pTarget)
{
    if (master == pTarget)
    {
        bIsTrain = false;
        master = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// CUnitServer
/////////////////////////////////////////////////////////////////////////////////////
// ----------------------------- Create/destroy
// ------------------------------------------------------------
CUnitServer::CUnitServer()
{
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
    Clear();
}
CUnitServer::~CUnitServer()
{
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitServer::Create()
{
    Clear();
    logic.Set(this);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::Destroy()
{
    Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::Clear()
{
    ModOutOfCombatRegeneration::Forget(this);
    modLeverTarget=nullptr;modLeverId=0;modLeverRing=false;
    if(auto*mod=ModRuntime::Current())mod->ForgetAttack(level1AP);
    curCommand.Clear();
    nextCommand.Clear();
    // level0 clear
    bL0Frozen = false;
    bL0MovementFrozen = false;
    level0Animation.Clear();
    // level1 clear
    path.Clear();
    level1Command.Clear();
    level1AP.Clear();
    sufferAnimationMem.Clear();
    eL1State = UL1_NONE;
    nL1SufferDirection = -1;
    bL1PlayingSuffer = false;
    bL1MovementFailed = false;
    bL1SpeedChanged = false;
    bL1MoveAnimationSet = false;
    nL1RunFailCounter = 0;
    currentPose.Clear();
    wishPose.Clear();
    pCastParticle = 0;
    nAttackParamsSetTime = -1;
    // command level clear
    curCommand.cmd = UC_IDLE;
    nextCommand.cmd = UC_NONE;
    nCanAttackTime = 0;
    nMoveToTargetTime = 0;
    bLcCommandFail = false;
    bLcCompletelyDead = false;
    train.Clear();
    // oldPose = UP_STAND;
    bLcChangingPoseToAttack = false;
    // general data clear
    logic.Clear();
    // logic.SetPlayer();
    acknowledgements.Clear();
    lastFootstep = NULL;
    fpModSound = 1;
    fpModVision = 1;
    fpSModVision = 1;
    fpSModSound = 1;
    // A new/recycled server may be recalculated before prototype stats are assigned.
    rpgUnitStats = SRPGUnitStats{};
    rpgUnitStats.Clear();
    dwSpellAddonLostSight = 0;
    tameLevel = UTL_NONE;
    nOriginalPlayerNum = -1;
    nPlayerWhoTamed = -1;
    nFootstepGenInterval = 15;
    dwFutureEnemies = 0;
    fpZSpeed = 0;
    animMoveCache.Clear();
    animIdleCache.Clear();
    nBoredCounter = 0;
    activeContSpells.RemoveAll();
    animCallback = 0;
    nCommandExecStart = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Lowest level interface funtions
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::LowSetDead(BOOL bDead)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    if (lpUnit->bDead == bDead)
        return;
    pWS->aiMap.RemoveObject(lpUnit);
    lpUnit->bDead = bDead;
    BOOL bRv = pWS->aiMap.AddObject(lpUnit);
    ASSERT(bRv);
    if (bDead)
    {
        if (!pCastParticle.IsEmpty())
        {
            pWS->RemoveEffect(pCastParticle->GetID());
            pCastParticle = 0;
        }
    }
    AddUpdateMask(CMapObject::VLD_MASK_FFLAGS);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::LowSetMoving(BOOL bMoving)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    if (lpUnit->bMoving == bMoving)
        return;
    pWS->aiMap.RemoveObject(lpUnit);
    lpUnit->bMoving = bMoving;
    BOOL bRv = pWS->aiMap.AddObject(lpUnit);
    ASSERT(bRv);
    AddUpdateMask(CMapObject::VLD_MASK_FFLAGS);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::LowNewPose(EUnitPose pose)
{
    if (pose == lpUnit->currentPose)
        return;
    lpUnit->currentPose = pose;
    AddUpdateMask(CMapObject::VLD_MASK_POSE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::LowNewWishPose(EUnitPose pose)
{
    if (pose == lpUnit->wishPose)
        return;
    lpUnit->wishPose = pose;
    AddUpdateMask(CMapObject::VLD_MASK_POSE);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::LowFindPath(const CPlot3D &ptDest, SAIPath &res, BOOL bFastPath,
                              float fpMaxPathLength, Navigation::GoalPolicy goalPolicy,const Navigation::Location* goalSupport)
{
    CorePerf::Scope coreTiming(CorePerf::FindPath);
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CPlayer *pPlayer = lpUnit->GetOwner();
    if (pPlayer)
        res.bTryAnyPath = false;
    else
        res.bTryAnyPath = true;
    //
    BOOL bSaveFastPath = lpUnit->bNeedFastPath;
    lpUnit->bNeedFastPath = bFastPath;
    const bool journal=lpUnit->IsHero() && &res==&path;
    Navigation::Route detail;
    Navigation::Location targetSupport;
    if(!goalSupport&& &res==&path&&curCommand.IsMove()) {
        if(curCommand.navGoalValid&&std::hypot(curCommand.navGoal.position.x-ptDest.x,curCommand.navGoal.position.y-ptDest.y)<.01f)goalSupport=&curCommand.navGoal;
        else if(auto* player=logic.GetPlayer())if(const auto* point=player->MoveGoal())if(std::hypot(point->x-ptDest.x,point->y-ptDest.y)<.01f) {
            targetSupport.position={point->x,point->y,point->z};goalSupport=&targetSupport;
        }
    }
    if(!goalSupport&& &res==&path&&!curCommand.pTarget.IsEmpty()) {
        if(auto* target=dynamic_cast<CUnit*>(static_cast<CMapObject*>(curCommand.pTarget))){targetSupport=target->navLocation;targetSupport.position=pWS->aiMap.Navigation().Position(target,pWS->aiMap);goalSupport=&targetSupport;}
    }
    bool bFound = pWS->aiMap.NewPath(lpUnit, ptDest, res, fpMaxPathLength,0,goalPolicy,journal?&detail:nullptr,goalSupport);
    lpUnit->bNeedFastPath = bSaveFastPath;
    if(bFound&&journal&&!detail.points.empty()) {
        std::ostringstream points;unsigned turns=0,printed=0;
        for(size_t i=0;i<detail.points.size();++i) {
            const auto& current=detail.points[i];
            if(i&&i+1<detail.points.size()) {
                const auto& a=detail.points[i-1];const auto& b=detail.points[i+1];
                const float ax=current.position.x-a.position.x,ay=current.position.y-a.position.y,
                    bx=b.position.x-current.position.x,by=b.position.y-current.position.y;
                const float lengths=std::hypot(ax,ay)*std::hypot(bx,by);
                if(lengths>1.e-10f&&ax*bx+ay*by>0&&std::abs(ax*by-ay*bx)<.052336f*lengths&&
                    a.area==current.area&&current.area==b.area&&a.surface==current.surface&&current.surface==b.surface)continue;
                ++turns;
            }
            if(printed>=23&&i+1<detail.points.size())continue;
            if(printed++)points<<';';
            points<<current.position.x<<','<<current.position.y<<','<<current.position.z<<'/'<<current.area<<'/'<<current.surface;
        }
        const auto& from=detail.points.front();const auto& to=detail.points.back();
        GameJournal::Event("NAV","route_ready","unit=%d mode=%s pose=%d requested=(%.3f,%.3f) from=(%.3f,%.3f,%.3f) accepted=(%.3f,%.3f,%.3f) length=%.3f planned_cost=%.3f selected_cost=%.3f straight_cost=%.3f straightened=%d polygons=%zu points=%zu bends=%u printed=%u corners=%s",
            lpUnit->GetID(),bFastPath?"flat":"material",int(lpUnit->GetCurrentPose()),ptDest.x,ptDest.y,
            from.position.x,from.position.y,from.position.z,to.position.x,to.position.y,to.position.z,
            detail.length,detail.plannedCost,detail.cost,detail.straightCost,int(detail.straightened),
            detail.corridor.size(),detail.points.size(),turns,printed,GameJournal::Quote(points.str().c_str()).c_str());
    }
    return bFound;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::LowRemoveUnitsForPathFind(CUnitList &units, const CPlot3D &ptDest, bool bGroup)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CPlayer *pPlayer = lpUnit->GetOwner();
    POSITION pos, oldPos;
    units.RemoveAll();
    if (pPlayer && pPlayer->curInterface == IFACE_MISSION)
    {
        CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
        if (fabs2(ptDest, lpUnit->GetCP()) > 16)
        {
            pWS->aiMap.GetUnitsNear(ptDest, units, 4);
            for (pos = units.GetHeadPosition(); pos;)
            {
                oldPos = pos;
                CUnit *pUnit = units.GetNext(pos);
                if (!pPS->currentVisible.HasMapObject(pUnit))
                    pWS->aiMap.RemoveObject(pUnit);
                else
                    units.RemoveAt(oldPos);
            }
        }
        // remove allies
        CUnitPtrHashIterator unitIter;
        CUnit *pAlly = unitIter.GetFirst(&pPlayer->unitsUnderControl);
        for (; pAlly; pAlly = unitIter.GetNext())
        {
            if (pAlly == lpUnit)
                continue;
            CUnitServer *pUS = (CUnitServer *)pAlly->GetUnitSpecific();
            bool bRemove = false;
            if (bGroup && pUS->HasMove())
                bRemove = true;
            else if (pUS->logic.GetPlayerLeader() == lpUnit)
                bRemove = true;
            //
            if (bRemove)
            {
                units.AddTail(pAlly);
                theWorldServer.aiMap.RemoveObject(pAlly);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Level 0 of commands execution
/////////////////////////////////////////////////////////////////////////////////////
// здесь проверяется не пришло ли время сменить текущий фрагмент анимации на следующий
bool CUnitServer::L0ShiftAnimations()
{
    UpdateAnimationLoop();
    bool bRv = false;
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    int nTime = pWS->GetSegmentTimeCounter();
    for (;;)
    {
        if (level0Animation.animations.GetSize() == 0)
            return bRv;
        SL0Animation &a = level0Animation.animations[0];
        if (a.nLength == -1)
            return bRv;
        if (a.nLength + level0Animation.nStartSegment > nTime)
            return bRv;
        bRv = true;
        // перейдем к следующей анимации
        bool bShiftStartTime = true;
        if (level0Animation.animations.GetSize() > 1)
        {
            if (level0Animation.animations[1].IsEmpty())
            {
                level0Animation.SetStartId(1, level0Animation.GetStartId(0));
                level0Animation.SetCycleId(1, level0Animation.GetCycleId(0));
                level0Animation.animations[1].nLength += a.nLength;
                if (level0Animation.animations[1].animationSpeed == UAS_FROZEN)
                {
                    int nSomeTime = nTime - level0Animation.nStartSegment - a.nLength;
                    nSomeTime = Min(nSomeTime, a.nLength);
                    if (nSomeTime > 0)
                        level0Animation.nStartSegment += nSomeTime;
                }
                bShiftStartTime = false;
            }
        }
        if (bShiftStartTime)
            level0Animation.nStartSegment += a.nLength;
        level0Animation.animations.RemoveAt(0);
        level0Animation.startIds.RemoveAt(0);
        level0Animation.cycleIds.RemoveAt(0);
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L0StartNextAnimationSegment()
{
    if (level0Animation.animations.GetSize() != 0)
    {
        SL0Animation &a = level0Animation.animations[0];
        if (!a.IsEmpty())
        {
            if (a.bCyclic == 0xfe && level0Animation.GetCycleId(0) == 0xffffffffu)
                lpUnit->LowStartAnimation(a.bStart, a.bCyclic,
                    level0Animation.nStartSegment, a.bSoundSet);
            else
                lpUnit->LowStartAnimationExtended(level0Animation.GetStartId(0),
                    level0Animation.GetCycleId(0), level0Animation.nStartSegment, a.bSoundSet);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L0StartAnimation(SL0AnimationSeq &anim)
{
    level0Animation = anim;
    level0Animation = anim; // CRAP - compiler generates wrong code without this line
    if (level0Animation.nStartSegment == -1)
        level0Animation.nStartSegment = GetWorldBase()->GetSegmentTimeCounter();

    L0ShiftAnimations();
    L0StartNextAnimationSegment();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L0GetAnimation(SL0AnimationSeq &curAnimation)
{
    curAnimation = level0Animation;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L0Freeze(bool bFreeze)
{
    bL0Frozen = bFreeze;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L0FreezeMovement(bool bFreeze)
{
    bL0MovementFrozen = bFreeze;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::L0HasAnimation()
{
    return !level0Animation.IsEmpty();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::L0IsPlayingSuffer()
{
    if (level0Animation.animations.GetSize() > 0)
    {
        const DWORD startId = level0Animation.GetStartId(0);
        if (startId == 0xffffffffu) return false;
        const SADEntry *pEntry = lpUnit->GetAnimationByIndex(startId);
        if (pEntry && (pEntry->dwFlags & ADB_ACTIONS) == ADB_ACTION_SUFFER)
        {
            if (level0Animation.nStartSegment + pEntry->dwAnimationLength >
                GetWorldBase()->GetSegmentTimeCounter())
                return true;
        }
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L0Segment()
{
    CorePerf::Scope coreTiming(CorePerf::L0);
    if (bL0Frozen)
    {
        lpUnit->LowLocomotionSpeed(ULS_FROZEN);
        lpUnit->LowAnimationSpeed(UAS_FROZEN);
        path.curSegment.Freeze(1);
        level0Animation.Freeze(1);
        sufferAnimationMem.Freeze(1);
        level1Command.Freeze(1);
        lpUnit->nAnimationStartSegment++;
        return;
    }
    if (bL0MovementFrozen)
    {
        path.curSegment.Freeze(1);
        sufferAnimationMem.Freeze(1);
        level1Command.Freeze(1);
        lpUnit->LowLocomotionSpeed(ULS_FROZEN);
    }
    else
        lpUnit->LowLocomotionSpeed(ULS_NORMAL);

    if (L0ShiftAnimations())
        L0StartNextAnimationSegment();

    EUnitAnimationSpeed animSpeed = level0Animation.GetAnimationSpeed();
    if (animSpeed == UAS_FROZEN)
    {
        level0Animation.Freeze(1);
        if (level0Animation.animations.GetSize())
            level0Animation.animations[0].nLength--;
        else
            ASSERT(0);
    }
    lpUnit->LowAnimationSpeed(animSpeed);
}

/////////////////////////////////////////////////////////////////////////////////////
// !!! Level I of commands execution
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1ChangeWeapon()
{
    if (lpUnit->dwWishWeapon == lpUnit->dwCurrentWeapon)
        return;
    lpUnit->ChangeWeapon(lpUnit->dwWishWeapon);
    // InformFailure( ACK_HERO_REFUSE_CHANGE_WEAPON ); // never called
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1SetPhase(EUnitL1State phase)
{
    /*
            NConsole::game << "unit = " << lpUnit->GetID() << " phase = ";
            switch ( phase )
            {
                    case UL1_MOVE:    NConsole::game << "move" << endl; break;
                    case UL1_ROTATE:  NConsole::game << "rotate" << endl; break;
                    case UL1_IDLE:    NConsole::game << "idle" << endl; break;
                    case UL1_SPECIAL: NConsole::game << "spec" << endl; break;
                    case UL1_SPECIAL_CYCLE: NConsole::game << "spec_cycle" << endl; break;
                    case UL1_ATTACK:  NConsole::game << "attack" << endl; break;
                    case UL1_NONE:    NConsole::game << "none" << endl; break;
                    default:          NConsole::game << "crap" << endl; break;
            }*/
    eL1State = phase;
    if (phase != UL1_ATTACK)
        lpUnit->RecalcAIParams();
    else
        nAttackParamsSetTime = ((CWorldServer *)GetWorld())->GetSegmentTimeCounter() + 10;
    //
    if (phase != UL1_ATTACK) //&& phase != UL1_SPECIAL )
    {
        if (!pCastParticle.IsEmpty())
        {
            theWorldServer.RemoveEffect(pCastParticle->GetID());
            pCastParticle = 0;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1StartSuffer()
{
    SL0AnimationSeq suffer;
    ASSERT(!bL1PlayingSuffer);
    if (currentPose.pose == UP_LAY && L1IsMoving())
        return;
    GetSufferAnimation(suffer, nL1SufferDirection, false, false);
    if(suffer.IsEmpty()&&powerHitSuffer)suffer.AddSimple(powerHitSuffer->dwEntryIndex,GetAnimationLength(powerHitSuffer->dwEntryIndex));
    if (!LcCanPlaySuffer(suffer.GetLength()))
        return;
    bL1PlayingSuffer = true;
    L0GetAnimation(sufferAnimationMem);
    L0StartAnimation(suffer);
    L0FreezeMovement(true);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1StandStill()
{
    path.Clear();
    if (path.curSegment.SetStartPos(lpUnit->GetCP(), -lpUnit->GetYAxis(),
                                    GetWorldBase()->GetSegmentTimeCounter()))
        lpUnit->LowNewRoute();
    LowSetMoving(FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1StopMovement(bool bFailed)
{
    navigationRejects=navigationWait=0;
    static_cast<CWorldServer*>(GetWorld())->aiMap.Navigation().Stop(lpUnit->GetID());
    if(path.curSegment.GetMoveType()==CAIFullRoute::Rotate) {
        const auto facing=-lpUnit->GetYAxis();
        GameJournal::Event("UNIT","turn_finished","unit=%d failed=%d requested=%.6f actual=%.6f",
                           lpUnit->GetID(),int(bFailed),std::atan2(curCommand.ptTarget.y,curCommand.ptTarget.x),std::atan2(facing.y,facing.x));
    }
    L1StandStill();
    L1SetPhase(UL1_NONE);
    bL1MovementFailed = bFailed;
}
/////////////////////////////////////////////////////////////////////////////////////
// start rotate animation - depends on previous L1 state
void CUnitServer::L1StartRotateAnimation()
{
    switch (eL1State)
    {
    case UL1_MOVE:
    case UL1_ROTATE:
        break;
    case UL1_IDLE:
        break;
    default: {
        // здесь по идее должна использоватся анимация поворота
        L0StartAnimation(*GetIdleAnimation());
    }
    break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// начался новый сегмент пути, выставляется начальная анимация поворота для него
void CUnitServer::L1NewPathSegment()
{
    lpUnit->LowNewRoute();
    L1StartRotateAnimation();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::RestoreNavigation(bool invalidate)
{
    if(invalidate){navigationRejects=navigationWait=0;path.excludeVertices.RemoveAll();}
    if(!path.curSegment.IsMoving()&&!path.navigationPending)return true;
    auto* world=static_cast<CWorldServer*>(GetWorld());auto goal=path.ptDestination;
    if(distanceXY2(lpUnit->GetCP(),goal)<.0009f) {
        path.CreateStand(lpUnit);L1SetPhase(UL1_NONE);lpUnit->LowNewRoute();return true;
    }
    if(!lpUnit->navLocation.surface||!world->aiMap.NewPath(lpUnit,goal,path)) {
        // A derived route may become unavailable after world reconstruction.
        // Keep the loaded unit, stop this move; a new command can request a path.
        path.CreateStand(lpUnit);L1SetPhase(UL1_NONE);
    }
    lpUnit->LowNewRoute();return true;
}
void CUnitServer::L1ExecMove()
{
    CorePerf::Scope coreTiming(CorePerf::MoveCollisions);
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CPlot3D pos, vec;
    float fpSpeed;
    bool bMoved, bSegmentFinished, bRotating;
    EAILock lock;
    int nCheckTileX, nCheckTileY;
    bool bConvertLocks = false, bPathFound;
    int nTime = pWS->GetSegmentTimeCounter();
    CAIFullRoute &segment = path.curSegment;

    NavigationWorld::Movement movement;
    const auto from=pWS->aiMap.Navigation().Position(lpUnit,pWS->aiMap);
    const bool accepted=pWS->aiMap.Navigation().Move(lpUnit,path,float(nTime),navigationWait,movement);
    pos=lpUnit->GetCP();pos.x=movement.support.position.x;pos.y=movement.support.position.y;
    vec=CPlot3D(movement.direction.x,movement.direction.y,0);
    bRotating=movement.rotating;bSegmentFinished=movement.finished;fpSpeed=movement.speed;

    if (!bRotating && !bL1MoveAnimationSet)
    {
        L0StartAnimation(*GetMoveAnimation());
        bL1MoveAnimationSet = true;
    }
    if (nL1SufferDirection != -1)
    {
        if (bRotating)
        {
            L1StartSuffer();
            return;
        }
        else
        {
            // check if current animation is suffer then ignore new suffer
            if (!L0IsPlayingSuffer())
            {
                // set animation to suffermove + move
                SL0AnimationSeq move, suffer;
                GetMoveAnimation(move);
                GetSufferAnimation(suffer, nL1SufferDirection, false, true);
                suffer += move;
                L0StartAnimation(suffer);
            }
        }
    }

    auto support=movement.support;
    if(!accepted) {
        if(movement.blocker>=0) {
            GameJournal::Event("NAV","queue_stopped","unit=%d blocker=%d reason=no_detour",lpUnit->GetID(),movement.blocker);
            L1StopMovement(true);return;
        }
        path.ClearCalcedPath();
        if(++navigationRejects>1) {
            GameJournal::Event("NAV","step_replan","unit=%d result=stopped reason=repeated_rejection",lpUnit->GetID());
            L1StopMovement(true);return;
        }
        bool replanned=LowFindPath(path.ptDestination,path,lpUnit->bNeedFastPath,path.ptDestination.z);
        // Reject an unchanged first movement immediately; rebuilding the same
        // path every tick otherwise leaves a permanently running animation.
        if(replanned)for(POSITION i=path.curSegment.route.route.GetHeadPosition();i;) {
            const auto& node=path.curSegment.route.route.GetNext(i);
            Navigation::Point next{AITile2Meter(node.point.x),AITile2Meter(node.point.y),from.z};
            const float length=std::hypot(next.x-from.x,next.y-from.y);if(length<.001f)continue;
            const float fraction=(std::min)(1.f,.05f/length);next.x=from.x+(next.x-from.x)*fraction;next.y=from.y+(next.y-from.y)*fraction;
            Navigation::Location probe;replanned=pWS->aiMap.Navigation().Step(lpUnit,from,next,probe,&path.curSegment.route);break;
        }
        GameJournal::Event("NAV","step_replan","unit=%d result=%s",lpUnit->GetID(),replanned?"ready":"stopped");
        if(replanned)L1NewPathSegment();else L1StopMovement(true);
        return;
    }
    if(movement.replanned){L1NewPathSegment();return;}
    if(movement.waiting) {
        segment.Freeze(1);lpUnit->SetUpdateMask(CMapObject::VLD_MASK_ROUTE);
        L0Freeze(true);LowSetMoving(FALSE);
        if(navigationWait==1)GameJournal::Event("NAV","queue_wait","unit=%d blocker=%d",lpUnit->GetID(),movement.blocker);
        return;
    }
    L0Freeze(false);bMoved=true;
    lock=bMoved?AI_LOCK_NO:AI_LOCK_DYNAMIC;
    // юнит вступил в дерьмо, а может он мог бы это избежать ??! - repath in this case
    if (eL1State == UL1_MOVE && pWS->aiMap.IsDangerous(pos) &&
        segment.GetStartSegment() < pWS->aiMap.GetLastDangerAddTime())
    {
        if (!IsAggressive())
        {
            // simulate static lock
            bMoved = false;
            lock = AI_LOCK_STATIC;
        }
    }
    if (bMoved)
    {
        bMoved = pWS->aiMap.MoveObject(
            lpUnit,
            pos); // SetCP( pos ); setcp нельзя использовать, теперь только aimap::moveobject
        if(bMoved) {
            lpUnit->navLocation=support;
            if(std::hypot(support.position.x-from.x,support.position.y-from.y)>.001f)navigationRejects=0;
        }
        else lock=AI_LOCK_STATIC;
        ASSERT(bMoved);
    }
    if (!bMoved)
    {
        // юнит не может двигатся дальше по различным причинам
        switch (lock)
        {
        case AI_LOCK_NO:
            ASSERT(0 && "Incorrect failure of CAIMap::MoveObject");
            break;
        case AI_LOCK_STATIC:
            // создадим новый путь, чтобы обойти возникшее вдруг препятствие
            // если нам помешал динамический блок от более медленного юнита обойдем его
            if (bConvertLocks)
            {
                pWS->aiMap.RemoveObject(lpUnit);
                pWS->aiMap.ConvertLocks(pos, TRUE);
                pWS->aiMap.AddObject(lpUnit);
            }
            path.ClearCalcedPath();
            bPathFound = LowFindPath(path.ptDestination, path, lpUnit->bNeedFastPath,
                                     path.ptDestination.z);
            if (bConvertLocks)
            {
                pWS->aiMap.RemoveObject(lpUnit);
                pWS->aiMap.ConvertLocks(pos, FALSE);
                pWS->aiMap.AddObject(lpUnit);
            }
            if (bPathFound)
                L1NewPathSegment();
            else
                L1StopMovement(path.bRouteFailed == TRUE);
            break;
        case AI_LOCK_DYNAMIC:
            // подождем, пока динамический лок от движущегося юнита снимут
            if ((rpgUnitStats.dwLocomotion == 2 || rpgUnitStats.dwLocomotion == 3))
                L0FreezeMovement(true);
            else
                L0Freeze(true);
            LowSetMoving(FALSE);
            break;
        }
    }
    else
    {
        // юнит продолжает путь
        LowSetMoving(eL1State == UL1_MOVE);
        lpUnit->SetOrientationXY(vec);
        if (bSegmentFinished)
        {
            if (path.IsPathFinished())
            {
                L1StopMovement(path.bRouteFailed == TRUE);
            }
            else
            {
                if (!pWS->aiMap.ContinuePath(lpUnit, path))
                    L1StopMovement(false);
                else
                    L1NewPathSegment();
            }
        }
    }
    // run state processing (here we determine if unit is running or not)
    if (!lpUnit->bMoving)
        return;
    if (currentPose.pose != UP_STAND || wishPose.pose != UP_STAND)
        return;
    if (currentPose.bRun && lpUnit->GetOwner() && !lpUnit->GetOwner()->bGod &&
        theWorldServer.curInterface != IFACE_BASE)
    {
        // вычтем ману за трудоемкие усилия по передвижению бегом у юнитов под управлением игрока :)
        SRPGCommonStats &stats = *GetMapObjectServer()->GetRPGStats();
        if (stats.ssMP.fpCurrent == 0)
        {
            wishPose.bRun = 0;
            LowNewWishPose(wishPose.Get());
        }
        else
        {
            ModRuntime::SpendRunningMana(rpgUnitStats, *lpUnit, stats);
            if (stats.ssMP.fpCurrent < 0)
            {
                InformFailure(ACK_HERO_NOTIFY_OUT_OF_STAMINA);
                stats.ssMP.fpCurrent = 0;
                wishPose.bRun = 0;
                LowNewWishPose(wishPose.Get());
            }
            AddUpdateMask(CMapObject::VLD_MASK_STATS);
        }
    }
    L1CheckRun();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1CheckRun()
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();

    ECanRun ecr = rpgUnitStats.CanRun(true);
    if (ecr != ECR_CAN_RUN && wishPose.WantRun())
    {
        InformFailure(ecr == ECR_INJURED ? ACK_HERO_REFUSE_CANT_RUN_INJURED
                                         : ACK_HERO_REFUSE_CANT_RUN_OVERLOADED);
        if (curCommand.bDoItFast)
            curCommand.bDoItFast = false;
        wishPose.bRun = 0;
        LowNewWishPose(wishPose.Get());
    }
    if (currentPose.bRun)
    {
        if (!wishPose.WantRun() || !pWS->aiMap.CanRun(lpUnit) || (ecr != ECR_CAN_RUN))
        {
            nL1RunFailCounter++;
            if (nL1RunFailCounter > 5 || !wishPose.WantRun())
            {
                currentPose.SetRun(0);
                // currentPose.bRun = 0;
                LowNewPose(currentPose.Get());
                lpUnit->RecalcAIParams();
                L1SpeedChanged();
            }
        }
        else
            nL1RunFailCounter = 0;
    }
    else
    {
        SRPGCommonStats &stats = *GetMapObjectServer()->GetRPGStats();
        CPlayer *pOwner = lpUnit->GetOwner();
        BOOL bGod = pOwner ? pOwner->bGod : 1;
        if (currentPose.pose == UP_STAND && wishPose.WantRun() &&
            (bGod || stats.ssMP.fpCurrent > 0) && pWS->aiMap.CanRun(lpUnit) && (ecr == ECR_CAN_RUN))
        {
            nL1RunFailCounter = 0;
            currentPose.SetRun(1);
            // currentPose.bRun = 1;
            LowNewPose(currentPose.Get());
            lpUnit->RecalcAIParams();
            L1SpeedChanged();
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::L1StartMove(const CPlot3D &ptTarget, float fpMaxPathLength, const Navigation::Location *goalSupport)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();

    L1CheckRun();
    // if ( eL1State != UL1_MOVE )
    // bL1MoveAnimationSet = false;
    bL1SpeedChanged = false;
    bL1MovementFailed = false;
    path.Clear();
    const auto goalPolicy=curCommand.cmd==UC_MOVETO&&lpUnit->IsHero()?Navigation::GoalPolicy::ClosestReachable:Navigation::GoalPolicy::Exact;
    if (!LowFindPath(ptTarget, path, lpUnit->bNeedFastPath, fpMaxPathLength,goalPolicy,goalSupport))
    {
        bL1MoveAnimationSet = false;
        L1StopMovement(path.bRouteFailed == TRUE);
        return false;
    }
    if(!path.curSegment.IsMoving()&&!path.navigationPending) {
        // Already at the nearest reachable point: successful no-op, not a
        // refusal and not a standing segment left in the movement phase.
        const auto accepted=path.ptDestination;
        L1StopMovement(false);path.CreateStand(lpUnit);path.ptDestination=accepted;return true;
    }
    path.ptDestination.z = fpMaxPathLength;
    nL1RunFailCounter = 0;
    L1NewPathSegment();
    L1SetPhase(UL1_MOVE);
    if (bL1MoveAnimationSet)
    {
        SL0AnimationSeq move(*GetMoveAnimation()), tek;
        L0GetAnimation(tek);
        if (tek.animations.GetSize() != 1 || move.animations.GetSize() != 1 ||
            tek.GetCycleId(0) != move.GetCycleId(0))
            L0StartAnimation(move);
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1SpeedChanged()
{
    bL1SpeedChanged = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1StartRotate(const CPlot3D &vecDest, float fpAngSpeed)
{
    ASSERT(L1IsInterruptable());
    CWorldServer *pWS = (CWorldServer *)GetWorld();

    path.CreateRotation(pWS->GetSegmentTimeCounter(), lpUnit, vecDest, fpAngSpeed);
    lpUnit->LowNewRoute();
    L1StartRotateAnimation();
    L1SetPhase(UL1_ROTATE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1StartIdle()
{
    ASSERT(L1IsInterruptable());
    if (eL1State == UL1_IDLE)
        return;
    SL0AnimationSeq *pIdle = GetIdleAnimation();
    if (!level0Animation.IsSame(*pIdle))
    {
        // художники запретили это делать: make idle start from random position
        // теперь должно стартовать со случайной позиции только во время старта игры
        if (level0Animation.IsEmpty() && level0Animation.nStartSegment == -1)
            pIdle->nStartSegment = GetWorldBase()->GetSegmentTimeCounter() - random.Get(0, 50);
        L0StartAnimation(*pIdle);
    }

    L1StandStill();
    L1SetPhase(UL1_IDLE);
}
/////////////////////////////////////////////////////////////////////////////////////
// сбросить текущее состояние L1 для смены позы (для правильного выбора анимации поворота)
void CUnitServer::L1ResetState()
{
    L1StandStill();
    L1SetPhase(UL1_NONE);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1StartSpecial(SL0AnimationSeq animation)
{
    L1SetPhase(UL1_SPECIAL);
    L0StartAnimation(animation);
    L1StandStill();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1StartSpecialCycle(SL0AnimationSeq animation)
{
    ASSERT(L1IsInterruptable());
    L1SetPhase(UL1_SPECIAL_CYCLE);
    L0StartAnimation(animation);
    L1StandStill();
}
/////////////////////////////////////////////////////////////////////////////////////
static const char* AttackAnimationName(CUnit* unit, SL0AnimationSeq& animation)
{
    if (animation.IsEmpty()) return nullptr;
    const DWORD startId = animation.GetStartId(0);
    if (startId == 0xffffffffu) return nullptr;
    const SADEntry* entry = unit->GetAnimationByIndex(startId);
    return entry ? entry->pszAnimationName : nullptr;
}

static bool UsesNativeAttackPhase(const SUnitCommand& current)
{
    auto* mod = ModRuntime::Current();
    return mod && mod->CombatSetting("JMVAttacks") != 0 && current.cmd == UC_ATTACK;
}

static bool UsesNativeOrdinaryImpact(CUnit* unit, const SUnitCommand& current, SL0AnimationSeq& animation)
{
    auto* mod = ModRuntime::Current();
    if (!mod || animation.IsEmpty()) return false;
    const DWORD startId = animation.GetStartId(0);
    if (startId == 0xffffffffu) return false;
    const SADEntry* entry = unit->GetAnimationByIndex(startId);
    return ModAttackTiming::UsesImpactFrame(mod->CombatSetting("JMVAttacks") != 0,
        current.cmd == UC_ATTACK, ModSkills::Slot(current.dwParam) >= 0,
        entry ? entry->pszAnimationName : nullptr);
}

void CUnitServer::L1ExecAttack()
{
    const CMapObjectPtr attackTarget = curCommand.pTarget;
    const bool nativeAttackPhase = UsesNativeAttackPhase(curCommand);
    StartPowerHitGlow(GetWorldBase()->GetSegmentTimeCounter());
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    int nTime = pWS->GetSegmentTimeCounter();
    CAIFullRoute &segment = path.curSegment;
    if (nTime == nAttackParamsSetTime)
        lpUnit->RecalcAIParams();
    if (nTime == level1Command.nTimeStrikeMoment)
    {
        L0StartAnimation(level1Command.attackAnimation);
        // start casting effect
        if (level1Command.bCast && lpUnit->GetRaceModel()->dwCastingType != 0)
        {
            CEffectParticle *pParticle;
            pParticle = (CEffectParticle *)((CEffectManager *)theWorldServer.GetEffectManager())
                            ->CreateData(ETypeIDs(HPC_EFFECTPARTICLE));
            switch (level1Command.castType)
            {
            case RPGMST_FIRE:
                pParticle->typeID = PTID_CASTINGFIRE;
                break;
            case RPGMST_LIGHTNING:
                pParticle->typeID = PTID_CASTINGELECTRICITY;
                break;
            case RPGMST_ACID:
                pParticle->typeID = PTID_CASTINGACID;
                break;
            case RPGMST_ILLUSION:
                pParticle->typeID = PTID_CASTINGILLUSION;
                break;
            case RPGMST_DIVINATION:
                pParticle->typeID = PTID_CASTINGDIVINATION;
                break;
            case RPGMST_ENCHANTMENTS:
                pParticle->typeID = PTID_CASTINGENCHANTMENT;
                break;
            case RPGMST_HEALING:
                pParticle->typeID = PTID_CASTINGHEALING;
                break;
            case RPGMST_DOMINATION:
                pParticle->typeID = PTID_CASTINGDOMINATION;
                break;
            default:
                pParticle->typeID = PTID_CASTING;
                break;
            }
            pParticle->fpScale = 1;
            pParticle->Attach(lpUnit->GetID());
            pWS->AddEffect(pParticle);
            pCastParticle = pParticle;
        }
    }
    const bool ordinaryImpact = UsesNativeOrdinaryImpact(lpUnit, curCommand, level1Command.attackAnimation);
    int finishTime = level1Command.nHitTime;
    if (nativeAttackPhase && !level1Command.attackAnimation.IsEmpty())
        if (auto* mod = ModRuntime::Current())
        {
            const auto& animation = level1Command.attackAnimation.animations[0];
            const char* plannedName = AttackAnimationName(lpUnit, level1Command.attackAnimation);
            if (ModAttackTiming::IsDatName(plannedName))
            {
                // Planned strikes own lifetime only; delivery queries the live animation after the phase.
                const auto lifetimeStrikes = mod->DualStrikes(plannedName, animation.nLength);
                for (const auto& strike : lifetimeStrikes)
                {
                    int when = level1Command.nTimeStrikeMoment + strike.first;
                    finishTime = (std::max)(finishTime, when + 1);
                }
            }
        }
    if (nTime >= finishTime)
    {
        if (nativeAttackPhase)
            LcAttackCallback(level1Command.eAttackLocation, level1Command.bCalledShot, false, AttackCallbackOrigin::Original);
        else if(!ordinaryImpact){
            ASSERT(nTime == level1Command.nHitTime);
            LcAttackCallback(level1Command.eAttackLocation, level1Command.bCalledShot);
        }
        // convert current state to simple non interruptable idle
        level1Command.Clear();
        if (eL1State == UL1_ATTACK)
            L1SetPhase(UL1_SPECIAL);
        L1StandStill();
        if (!nativeAttackPhase) return;
    }
    else // set new position and orientation
    {
        CWorldServer *pWS = (CWorldServer *)GetWorld();
        CPlot3D pos, vec;
        float fpSpeed;
        bool bRotating;
        int nCheckTileX, nCheckTileY;

        pos = lpUnit->GetCP();
        lpUnit->walker.GetPosition(pWS, segment, pWS->GetSegmentTimeCounter(), bRotating, pos, vec,
                                   fpSpeed, nCheckTileX, nCheckTileY);
        LowSetMoving(eL1State == UL1_MOVE);
        lpUnit->OrientInYDirection(vec, FALSE);
    }
    if (nativeAttackPhase)
    {
        const char* liveName = AttackAnimationName(lpUnit, level0Animation);
        if (ModAttackTiming::IsDatName(liveName))
        {
            if (attackTarget.IsEmpty() || attackTarget->IsDead())
            {
                L1CancelAttack(true);
                nCanAttackTime = 0;
                return;
            }
            if (level1Command.attackAnimation.IsEmpty()) return;
            if (auto* mod = ModRuntime::Current())
            {
                // Do not reuse the prephase result: the Lua provider may observe changed state.
                const auto deliveryStrikes = mod->DualStrikes(liveName, level1Command.attackAnimation.animations[0].nLength);
                for (const auto& strike : deliveryStrikes)
                {
                    if (level1Command.attackAnimation.IsEmpty()) break;
                    int when = level1Command.nTimeStrikeMoment + strike.first;
                    if (nTime == when)
                    {
                        UseAttackStart(attackTarget, level1Command.eAttackLocation, level1Command.bCalledShot, strike.second);
                        LcAttackCallback(level1Command.eAttackLocation, level1Command.bCalledShot, strike.second, AttackCallbackOrigin::Dual);
                        if (attackTarget.IsEmpty() || attackTarget->IsDead())
                        {
                            L1CancelAttack(true);
                            nCanAttackTime = 0;
                            return;
                        }
                    }
                }
            }
        }
        else if (ModAttackTiming::IsOrdinaryName(liveName))
            ModAttackTiming::ExecuteImpact(true,
                !attackTarget.IsEmpty() && !attackTarget->IsDead(), nTime, level1Command.nHitTime,
                [&] { UseAttackStart(attackTarget, level1Command.eAttackLocation, level1Command.bCalledShot); },
                [&] { LcAttackCallback(level1Command.eAttackLocation, level1Command.bCalledShot, false, AttackCallbackOrigin::Ordinary); });
    }
    else
    ModAttackTiming::ExecuteImpact(ordinaryImpact && UsesNativeOrdinaryImpact(lpUnit, curCommand, level0Animation),
        !curCommand.pTarget.IsEmpty() && !curCommand.pTarget->IsDead(), nTime, level1Command.nHitTime,
        [&] { UseAttackStart(curCommand.pTarget, level1Command.eAttackLocation, level1Command.bCalledShot); },
        [&] { LcAttackCallback(level1Command.eAttackLocation, level1Command.bCalledShot); });
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1StartAttack(bool bPredictTarget, BYTE nSoundSet)
{
    ASSERT(L1IsInterruptable());
    level1Command.attackAnimation.SetSoundSet(nSoundSet);
    L1StartRotateAnimation();
    L1SetPhase(UL1_ATTACK);
    // если это не вырожденная атака (не каст в точку)
    if (level1Command.nTargetID != -1)
    {
        int nHitTime = bPredictTarget ? level1Command.nHitTime : 0;
        // set motion to attacking
        path.CreateAttack(GetWorldBase()->GetSegmentTimeCounter(), lpUnit, level1Command.nTargetID,
                          nHitTime, level1Command.nTimeStrikeMoment);
        lpUnit->LowNewRoute();
        LowSetMoving(FALSE);
    }
    else
    {
        path.CreatePointAttack(GetWorldBase()->GetSegmentTimeCounter(), lpUnit, level1Command.ptDir,
                               level1Command.nHitTime, level1Command.nTimeStrikeMoment);
        lpUnit->LowNewRoute();
        LowSetMoving(FALSE);
        // L1StandStill();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// should clear pointer to target
void CUnitServer::L1CancelAttack(bool bAbortAnimation)
{
    if(auto*mod=ModRuntime::Current())mod->ForgetAttack(level1AP);
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    int nTime = pWS->GetSegmentTimeCounter();
    // if attack animation has begun then wait to its end, else stop everything
    if (!bAbortAnimation &&
        (nTime >= level1Command.nTimeStrikeMoment && !level0Animation.IsEndless()))
    {
        if (level1Command.attackAnimation.animations.GetSize() == 3)
        {
            // for complex animations with idling in the middle in this case we should
            // proceed to final stage of animation
            if (level0Animation.animations.GetSize() == 2)
            {
                SL0AnimationSeq anim = level1Command.attackAnimation;
                if (anim.animations[1].animationSpeed == UAS_FROZEN)
                {
                    anim.nStartSegment = nTime - anim.animations[0].nLength;
                    anim.animations[0].nLength += anim.animations[2].nLength;
                    anim.animations.SetSize(1);
                    anim.startIds.SetSize(1);
                    anim.cycleIds.SetSize(1);
                    L0StartAnimation(anim);
                }
            }
            else if (level0Animation.animations.GetSize() == 3)
            {
                SL0AnimationSeq anim = level1Command.attackAnimation;
                if (anim.animations[1].animationSpeed == UAS_FROZEN)
                {
                    anim.animations[0].nLength += anim.animations[2].nLength;
                    anim.nStartSegment = level0Animation.nStartSegment;
                    anim.animations.SetSize(1);
                    anim.startIds.SetSize(1);
                    anim.cycleIds.SetSize(1);
                    L0StartAnimation(anim);
                }
                else
                {
                    anim.nStartSegment = level0Animation.nStartSegment;
                    anim.animations.RemoveAt(1);
                    anim.startIds.RemoveAt(1);
                    anim.cycleIds.RemoveAt(1);
                    L0StartAnimation(anim);
                }
            }
        }
        L1SetPhase(UL1_SPECIAL);
    }
    else
        L1SetPhase(UL1_NONE);
    level1Command.Clear();
    L1StandStill();
}
/////////////////////////////////////////////////////////////////////////////////////
// current L1 command can be interrupted with new L1 command
bool CUnitServer::L1IsInterruptable()
{
    if (lpUnit->CheckFlag(UNIT_STUN))
        return false;
    if (bL1PlayingSuffer)
        return false;
    switch (eL1State)
    {
    case UL1_IDLE:
    case UL1_MOVE:
    case UL1_ROTATE:
    case UL1_NONE:
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1NeedPlaySuffer(int nSufferDirection)
{
    if (bL1PlayingSuffer)
        return;
    nL1SufferDirection = nSufferDirection;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::L1Segment()
{
    CorePerf::Scope coreTiming(CorePerf::L1);
    L0FreezeMovement(false);
    L0Freeze(false);
    // перепроверим атаку
    if (eL1State == UL1_ATTACK && !LcRecheckAttack()) {
        auto* mod = ModRuntime::Current();
        if (ModAttackTiming::CancelFailedRecheck(mod && mod->CombatSetting("DisableRunaway") != 0))
            L1CancelAttack();
    }
    // а не застанили ли нас ?
    if (lpUnit->CheckFlag(UNIT_STUN))
    {
        L0Freeze(true);
        nL1SufferDirection = -1;
        return;
    }
    if (bL1PlayingSuffer)
    {
        if (L0HasAnimation())
        {
            L0FreezeMovement(true);
            nL1SufferDirection = -1;
            return;
        }
        // suffer is over
        L0StartAnimation(sufferAnimationMem);
        bL1PlayingSuffer = false;
    }
    switch (eL1State)
    {
    case UL1_ROTATE:
        L1ChangeWeapon();
        L1ExecMove();
        break;
    case UL1_MOVE:
        L1ChangeWeapon();
        if (bL1SpeedChanged)
        {
            if (!L1StartMove(path.ptDestination, path.ptDestination.z))
                break;
        }
        L1ExecMove();
        break;
    case UL1_SPECIAL:
        if (!L0HasAnimation())
        {
            L1SetPhase(UL1_NONE);
            if (animCallback)
            {
                animCallback(lpUnit);
                LcSegment(); // CRAP for continous animations
            }
        }
        break;
    case UL1_SPECIAL_CYCLE:
    case UL1_IDLE:
        L1ChangeWeapon();
        if (nL1SufferDirection != -1)
            L1StartSuffer();
        break;
    case UL1_ATTACK:
        L1ExecAttack();
        break;
    }
    nL1SufferDirection = -1;
}

/////////////////////////////////////////////////////////////////////////////////////
// !!! Command level of unit operation
/////////////////////////////////////////////////////////////////////////////////////
// проверить подходит ли цель под команду
bool CUnitServer::LcTargetIsValid(const SUnitCommand &cmd)
{
    bool bCast = cmd.IsCast();
    CMapObject *pTarget = cmd.pTarget;
    if (bCast)
    {
        if (lpUnit->CheckFlag(UNIT_FEEBLED))
            return false;
    }
    if (!pTarget)
    {
        if (bCast)
        {
            CSpell *pSpell = GetSpell(cmd);
            if (pSpell && pSpell->IsOnTile())
                return true;
        }
        return false;
    }
    if (pTarget->IsDead() && !cmd.IsLoot() && !cmd.IsScience())
        return false;
    return true;
}
// проверка возможности в текущий момент проиграть анимацию хита длиной nSufferLeng
bool CUnitServer::LcCanPlaySuffer(int nSufferLeng)
{
    if (curCommand.IsAttack() && eL1State == UL1_IDLE)
    {
        if (GetWorldBase()->GetSegmentTimeCounter() + nSufferLeng > nCanAttackTime)
            return false;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// R158: observe attack authority and displayed positions without changing the decision.
void CUnitServer::TraceMeleeReach(int phase, CMapObject *target, CItemWeapon *weapon,
                                 float predictedDistance2, float hpBefore, float hpAfter)
{
    if (phase < 0 || phase > 2 || curCommand.cmd != UC_ATTACK || !target ||
        target->WhoAmI() != HPC_UNIT)
        return;
    CUnit *victim = static_cast<CUnit *>(target);
    if (!lpUnit->GetOwner() && !victim->GetOwner())
        return;
    if (!weapon) weapon = lpUnit->GetCurrentQWeapon();
    const int type = weapon ? int(weapon->GetWeaponType()) : -1;
    if (type == RPGWST_BOW || type == RPGWST_CROSSBOW ||
        type == RPGWST_HANDS3 || type == RPGWST_HANDS7)
        return;

    const CPlot3D a = lpUnit->GetCP(), b = victim->GetCP();
    const float distance2 = distanceXY2(a, b), reach = GetHitDist(curCommand);
    CUnit *viewA = theWorld.GetMapObjectManager() ? theWorld.GetUnit(lpUnit->GetID()) : nullptr;
    CUnit *viewB = theWorld.GetMapObjectManager() ? theWorld.GetUnit(victim->GetID()) : nullptr;
    if (viewA && viewA->GetSubtypeID() != lpUnit->GetSubtypeID()) viewA = nullptr;
    if (viewB && viewB->GetSubtypeID() != victim->GetSubtypeID()) viewB = nullptr;
    const CPlot3D va = viewA ? viewA->GetCP() : CPlot3D(0, 0, 0);
    const CPlot3D vb = viewB ? viewB->GetCP() : CPlot3D(0, 0, 0);
    const float gapA = viewA ? sqrtf(distanceXY2(a, va)) : -1;
    const float gapB = viewB ? sqrtf(distanceXY2(b, vb)) : -1;
    // These thresholds select diagnostic samples only; they never gate a hit.
    const bool unusual = distance2 > sqr(reach + 1) || gapA > .75f || gapB > .75f;
    static unsigned normal[3] = {}, unusualCount[3] = {}, serial = 0;
    unsigned &count = unusual ? unusualCount[phase] : normal[phase];
    if (count >= 16) return;
    ++count;
    const unsigned sample = ++serial;
    const char *stages[] = {"plan", "recheck", "damage"};
    EngineDiagnostics::Event("MELEE_REACH R158 sample=%u phase=%s a=%d b=%d tick=%d client=%.3f type=%d range=%.3f reach=%.3f limit2=%.3f dist2=%.3f predicted2=%.3f hp=%.3f/%.3f usepoint=%d hit=%d start=%d unusual=%d",
        sample, stages[phase], lpUnit->GetID(), victim->GetID(),
        GetWorldBase()->GetSegmentTimeCounter(), theWorld.GetPreciseTime(), type,
        weapon ? weapon->GetRange() : rpgUnitStats.fpNAWRange, reach,
        level1Command.fpHitDist2, distance2, predictedDistance2, hpBefore, hpAfter,
        curCommand.HasPointOfUse(), level1Command.nHitTime,
        level1Command.nTimeStrikeMoment, unusual);
    EngineDiagnostics::Event("MELEE_POS R158 sample=%u serverA=%.3f/%.3f/%.3f serverB=%.3f/%.3f/%.3f viewA=%d:%.3f/%.3f/%.3f viewB=%d:%.3f/%.3f/%.3f gap=%.3f/%.3f collision=%.3f/%.3f attack=%.3f/%.3f route=%d/%d",
        sample, a.x, a.y, a.z, b.x, b.y, b.z, viewA != nullptr, va.x, va.y, va.z,
        viewB != nullptr, vb.x, vb.y, vb.z, gapA, gapB, lpUnit->GetCollisionDistance(),
        victim->GetCollisionDistance(), lpUnit->GetAttackDistance(),
        victim->GetAttackDistance(), int(path.curSegment.GetMoveType()),
        int(victim->GetUnitSpecific()->GetRoute()->GetMoveType()));
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::LcRecheckAttack()
{
    if(curCommand.cmd==UC_CASTPOINT&&ModSkills::Slot(curCommand.dwParam)>=0)return CanRequestPointSkill(ModSkills::Slot(curCommand.dwParam),curCommand.ptTarget);
    if (curCommand.cmd == UC_CASTPOINT || curCommand.cmd == UC_CASTUNIT)
    {
        if (lpUnit->CheckFlag(UNIT_FEEBLED))
            return false;
    }
    if (curCommand.cmd == UC_CASTPOINT || curCommand.cmd == UC_TRAP)
        return true;
    if (!curCommand.IsAttack() && curCommand.cmd != UC_SKILL && curCommand.cmd != UC_CASTUNIT)
        return false;
    CMapObject *pTarget = curCommand.pTarget;
    CPlot3D ptTargetPos;
    // может цель уже подохла ?
    if (!LcTargetIsValid(curCommand))
        return false;
    // цель находится в пределах досягаемости?
    ptTargetPos = pTarget->GetFuturePosition(level1Command.nHitTime);
    CPlot3D ptOurPos = lpUnit->GetCP();
    float fpDist2 = sqr(ptTargetPos.x - ptOurPos.x) + sqr(ptTargetPos.y - ptOurPos.y);
    TraceMeleeReach(1, pTarget, nullptr, fpDist2);
    return fpDist2 < level1Command.fpHitDist2;
}
/////////////////////////////////////////////////////////////////////////////////////
// this function is called when attack is to be performed
struct PreparedAttackSnapshot {
    CModPackage* mod;
    SRPGAttackPortion ap;
    PreparedAttackSnapshot(CModPackage* owner, const SRPGAttackPortion& source) : mod(owner), ap(source) {
        if (mod) mod->CopyAttack(source, ap);
    }
    ~PreparedAttackSnapshot() { if (mod) mod->ForgetAttack(ap); }
    PreparedAttackSnapshot(const PreparedAttackSnapshot&) = delete;
    PreparedAttackSnapshot& operator=(const PreparedAttackSnapshot&) = delete;
};

void CUnitServer::LcAttackCallback(ERPGHitLocations eAttackLocation, bool bCalledShot, bool leftHand, AttackCallbackOrigin origin)
{
    int pointSkill=PrepareExplosiveShot();if(pointSkill){LcCancelCommand();return;}
    const CMapObjectPtr target = curCommand.pTarget;
    auto* mod = ModRuntime::Current();
    const int activeSkillSlot=ModSkills::Slot(curCommand.dwParam);
    int skill=PrepareWeaponSkill(leftHand);
    if (ModPreparedAttack::RejectPreparation(skill, [&] { LcCancelCommand(); })) return;
    const char* activeSkillCode=nullptr;
    if(skill>0&&activeSkillSlot>=0){auto*activeSpell=GetSpell(activeSkillSlot);if(activeSpell&&activeSpell->GetPrototype())activeSkillCode=activeSpell->GetPrototype()->szCodeName;}
    switch (curCommand.cmd)
    {
    case UC_CASTPOINT:
    case UC_CASTUNIT:
        UseSpell();
        LcCancelCommand();
        break;
    case UC_SKILL:
        UseSkill(target);
        LcCancelCommand();
        break;
    case UC_ATTACK:
        if (origin == AttackCallbackOrigin::Original && skill == 0) break;
        ModPreparedAttack::DeliverProjectiles(skill ? skill : 1,
            [&] { return std::make_unique<PreparedAttackSnapshot>(mod, level1AP); },
            [&](int index, int count, bool weaponSpell) {
                UseAttack(target, level1AP, weaponSpell, index, count, activeSkillCode, origin);
            },
            [&](const std::unique_ptr<PreparedAttackSnapshot>& retained) {
                mod->ScheduleDoubleShot(*lpUnit, target, retained->ap);
            });
        if (!target.IsEmpty() && target->WhoAmI() == HPC_UNIT && eAttackLocation < RPGHL_ALL)
        {
            CUnit *pTarget = (CUnit *)(CMapObject*)target;
            CUnitServer *pUS = (CUnitServer *)pTarget->GetUnitSpecific();
            if (pUS->rpgUnitStats.hls[eAttackLocation].state != RPGALS_ACTIVE)
            {
                if (eAttackLocation == (ERPGHitLocations)curCommand.nQSpellNum)
                    curCommand.nQSpellNum = (int)RPGHL_ALL;
            }
        }
        break;
    case UC_TRAP:
        UseTrap();
        LcCancelCommand();
        break;
    }
    ModPreparedAttack::CancelReady(skill > 0, [&] { LcCancelCommand(); });
}
/////////////////////////////////////////////////////////////////////////////////////
// перезапустить текущую команду
void CUnitServer::LcRestartCommand()
{
    if (nextCommand.cmd == UC_NONE)
        nextCommand = curCommand;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::LcChangePose()
{
    bool bRv;
    if (currentPose.pose == wishPose.pose && currentPose.bAggressive == wishPose.bAggressive)
        return false;
    bool bChangePose = currentPose.pose != wishPose.pose;
    // пытаемся менять текущую позу на новую
    SL1Pose prevPose = currentPose;
    theWorldServer.aiMap.RemoveObject(lpUnit);
    // bTest - был ли исходный тайл проходимым
    bool bTest = theWorldServer.aiMap.CanPutUnit(lpUnit), bCanChange = false;
    // хитрый способ определения возможности смены позы
    if (currentPose.pose == wishPose.pose)
        bCanChange = true;
    currentPose = wishPose;
    lpUnit->currentPose = currentPose.Get();
    lpUnit->RecalcAIParams();
    //
    if (!bCanChange)
    {
        if (bTest)
            bCanChange = theWorldServer.aiMap.CanPutUnit(lpUnit);
        else
            bCanChange = theWorldServer.aiMap.CanPutUnitRelaxed(lpUnit);
    }
    //
    if (bCanChange)
        bRv = theWorldServer.aiMap.AddObject(lpUnit);
    else
        bRv = false;

    AddUpdateMask(CMapObject::VLD_MASK_POSE);
    if (!bRv)
    {
        currentPose = prevPose;
        lpUnit->currentPose = currentPose.Get();
        wishPose.pose = currentPose.pose;
        if (wishPose.pose != UP_STAND)
            wishPose.bRun = 0;
        lpUnit->wishPose = wishPose.Get();
        lpUnit->RecalcAIParams();
        bRv = theWorldServer.aiMap.AddObject(lpUnit);
        InformFailure(ACK_HERO_REFUSE_CANT_CHANGE_POSE);
        ASSERT(bRv);
        return false;
    }
    // if ( lpUnit->GetTypeID() == MOTID_HUMAN )
    // !IsMoveAnimation( level0Animation )
    if (L1IsMoving() && bChangePose)
        L1ResetState();
    if ((!L1IsMoving() || bChangePose) && (curCommand.cmd != UC_ATTACK))
    {
        // here we start transition animation with L1StartSpecial
        SL0AnimationSeq res;
        if (GetTransAnimation(res, prevPose, currentPose))
        {
            L1ResetState();
            L1StartSpecial(res);
        }
    }
    lpUnit->RecalcAIParams();
    if (bChangePose && lpUnit->GetOwner())
        GameJournal::Event("UNIT", "pose_changed", "unit=%d previous=%d current=%d at=(%.3f,%.3f)",
                           lpUnit->GetID(), int(prevPose.Get()), int(currentPose.Get()),
                           lpUnit->GetCPX(), lpUnit->GetCPY());
    LcRestartCommand();
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::LcCanChangePose(EUnitPose p)
{
    SL1Pose pp;
    pp.Convert(p);
    if (pp.pose == currentPose.pose)
        return true;
    //
    EUnitPose prevUPose = lpUnit->currentPose;
    theWorldServer.aiMap.RemoveObject(lpUnit);
    lpUnit->currentPose = p;
    lpUnit->RecalcAIParams();
    //
    bool bCanChange = theWorldServer.aiMap.CanPutUnit(lpUnit);
    lpUnit->currentPose = prevUPose;
    lpUnit->RecalcAIParams();
    bool bRv = theWorldServer.aiMap.AddObject(lpUnit);
    ASSERT(bRv);
    return bCanChange;
}
/////////////////////////////////////////////////////////////////////////////////////
// конвертировать позы - текущую и желаемую из нескольких источников
void CUnitServer::LcConvertPoses()
{
    if (curCommand.IsAttack()) //|| curCommand.cmd == UC_SKILL )
        SetAggression(true);
    else
    {
        // if unit is under player control
        if (lpUnit->GetOwner())
            SetAggression(false);
    }
    currentPose.Convert(lpUnit->currentPose);
    wishPose.Convert(lpUnit->wishPose);
    if (curCommand.bDoItFast && currentPose.pose == UP_STAND)
    {
        const CModPackage *mod = ModRuntime::Current();
        const CMapObjectServer *specific = GetMapObjectServer();
        const float mana = specific ? specific->GetCurrentMP() : 1.0f;
        // The ASI x87 equality also treats unordered (NaN) as zero.
        if (mod && mod->SpellAddon().Feature40() == 1 &&
            specific && (mana == 0.0f || mana != mana))
            curCommand.bDoItFast = false;
        else
            wishPose.bRun |= 2;
    }
    // животные бегают в агрессивном состоянии
    if (!lpUnit->GetOwner() && wishPose.bAggressive)
        wishPose.bRun |= 1;
}
/////////////////////////////////////////////////////////////////////////////////////
// nextCommand заменияет curCommand
void CUnitServer::LcStartNextCommand()
{
    curCommand = nextCommand;
    nextCommand.Clear();
    if (curCommand.bDoItFast && currentPose.pose != UP_STAND)
    {
        if (!(curCommand.IsAttack() || curCommand.IsCast() || curCommand.IsSkill()) ||
            !CanAttack(curCommand))
            LowNewWishPose(UP_STAND);
    }
    LcConvertPoses();
    nCommandExecStart = theWorldServer.GetSegmentTimeCounter();
}
/////////////////////////////////////////////////////////////////////////////////////
// stop execution of current command
void CUnitServer::LcCancelCommand()
{
    if(auto*mod=ModRuntime::Current())mod->ForgetAttack(level1AP);
    // if ( curCommand.cmd == UC_SKILL )
    // LowNewWishPose( oldPose );
    curCommand.Clear();
    if (nextCommand.cmd == UC_NONE)
        CmdWait();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::LcProcessNewCommand()
{
    if (nextCommand.cmd == UC_NONE)
        return false;
    //
    /*
            NConsole::game << "unit = " << lpUnit->GetID() << " cmd = ";
            switch ( nextCommand.cmd )
            {
                    case UC_IDLE: NConsole::game << "idle" << endl; break;
                    case UC_MOVETO: NConsole::game << "moveto" << endl; break;
                    case UC_ROTATE: NConsole::game << "rotate" << endl; break;
                    case UC_ATTACK: NConsole::game << "attack" << endl; break;
                    case UC_CASTPOINT: NConsole::game << "castp" << endl; break;
                    case UC_CASTUNIT: NConsole::game << "castu" << endl; break;
                    case UC_SKILL: NConsole::game << "skill" << endl; break;
                    case UC_PLAYANIM: NConsole::game << "playanim" << endl; break;
                    case UC_PLAYANIMCYCLE: NConsole::game << "playanimc" << endl; break;
                    case UC_NONE: NConsole::game << "none" << endl; break;
                    case UC_TRAP: NConsole::game << "trap" << endl; break;
                    case UC_REST: NConsole::game << "rest" << endl; break;
                    default: NConsole::game << "crap" << endl; break;
            }*/
    // из состояния rest юнит способен выполнить только команду rest или умереть :)
    if (nextCommand.cmd != UC_REST && currentPose.pose == UP_REST)
    {
        lpUnit->wishPose = wishPose.pose = UP_STAND;
        if (LcChangePose())
            return true;
        ASSERT(0);
        // даже не знаю что делать в этом случае - выход из реста всегда должен происходить успешно
        // иначе юнит попадает в беспомощное положение
        // так как условия проходимости для положения stand и rest идентичны (см
        // CUnit::RecalcAIParams()) сюда программа доходить не должна
    }
    switch (nextCommand.cmd)
    {
    case UC_IDLE:
        LcStartNextCommand();
        L1StartIdle();
        return true;
    case UC_REST:
        if (currentPose.pose != UP_REST)
        {
            wishPose.pose = UP_REST;
            lpUnit->wishPose = UP_REST;
            if (LcChangePose())
                return true;
            InformFailure(ACK_HERO_REFUSE_CANT_START_REST);
            LcCancelCommand();
            return false;
        }
        LcStartNextCommand();
        L1StartIdle();
        return true;
    case UC_MOVETO: {
        CWorldServer *pWS = (CWorldServer *)GetWorld();
        bool bRemoveAllies = false;
        CList<CUnit *, CUnit *> removed;

        bLcCommandFail = false;
        LcStartNextCommand();
        // change pose if requested before pathfinding
        if (LcChangePose())
            return TRUE;
        //
        bool bFound;
        if (fabs2(lpUnit->GetCP() - curCommand.ptTarget) < 0.02)
        {
            LcCancelCommand();
            return LcProcessNewCommand();
        }
        LowRemoveUnitsForPathFind(removed, curCommand.ptTarget, curCommand.nQSpellNum != 0);
        lpUnit->bNeedFastPath = ((CWorldServer *)GetWorld())->aiMap.IsWater(lpUnit->GetCP());
        bFound = L1StartMove(curCommand.ptTarget, curCommand.ptTarget.z);
        //
        for (POSITION pos = removed.GetHeadPosition(); pos;)
            pWS->aiMap.AddObject(removed.GetNext(pos));
        // if there is flag to show found path
        if (curCommand.dwParam)
        {
            curCommand.dwParam = 0;
            if (bFound || distanceXY2(curCommand.ptTarget, lpUnit->GetCP()) >= 0.001f)
                ShowFoundMovePath(path, bFound);
        }
        // check for failure
        if (!bFound)
        {
            bLcCommandFail = true;
            LcCancelCommand();
            LcProcessNewCommand();
            return false;
        }
        return true;
    }
    break;
    case UC_ROTATE:
        LcStartNextCommand();
        L1StartRotate(curCommand.ptTarget, curCommand.fpParam);
        return true;
    case UC_ATTACK:
    case UC_CASTUNIT:
    case UC_SKILL:
    case UC_CASTPOINT:
        bLcCommandFail = false;
        LcStartNextCommand();
        L1ResetMoveFailed();
        StartAttack();
        return true;
    case UC_PLAYANIM: {
        LcStartNextCommand();
        SL0AnimationSeq anim;
        anim.Clear();
        anim.AddSimple(curCommand.dwParam, GetAnimationLength(curCommand.dwParam));
        L1StartSpecial(anim);
    }
        return true;
    case UC_PLAYANIMCYCLE: {
        LcStartNextCommand();
        SL0AnimationSeq anim;
        anim.Clear();
        anim.AddCycle(curCommand.dwParam);
        L1StartSpecialCycle(anim);
    }
        return true;
    case UC_TRAP: {
        LcStartNextCommand();
        int nHit;
        SL0AnimationSeq anim;
        GetAttackAnimation(anim, nHit, UC_SKILL, 0);
        level1Command.CreateAimlessAttack(anim, GetWorldBase()->GetSegmentTimeCounter(), nHit);
        L1StartAttack(true, 0);
    }
        return true;
        break;
    }
    ASSERT(0 && "No such command found");
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::LcSegment()
{
    if (LcProcessNewCommand())
    {
        nBoredCounter = 0;
        return;
    }

    LcConvertPoses();

    if (LcChangePose())
        return;

    switch (curCommand.cmd)
    {
    case UC_PLAYANIMCYCLE:
    case UC_IDLE:
        break;
    case UC_MOVETO:
    case UC_ROTATE:
        if (L1IsFree())
        {
            if (L1IsMoveFailed())
            {
                bLcCommandFail = true;
                // в follow не информировать о траблах
                if (!logic.GetPlayerLeader())
                    InformFailure(ACK_HERO_REFUSE_PATH_NOT_FOUND);
            }
            CmdWait();
            LcProcessNewCommand();
        }
        break;
    case UC_ATTACK:
    case UC_CASTUNIT:
    case UC_SKILL:
    case UC_CASTPOINT:
        ExecAttack();
        break;
    case UC_PLAYANIM:
        CmdWait();
        LcProcessNewCommand();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::GetPathDestination(CPlot3D &ptRes)
{
    if (curCommand.IsMove())
    {
        ptRes = curCommand.ptTarget;
        return true;
    }
    if (nextCommand.IsMove())
    {
        ptRes = nextCommand.ptTarget;
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::IsDanger()
{
    if (curCommand.IsCast())
    {
        CSpell *pSpell = GetSpell(curCommand);
        if (pSpell)
        {
            switch (pSpell->GetSubtypeID() >> 16)
            {
            case SPELL_ID_FIREARROW:
            case SPELL_ID_LIGHTNING:
            case SPELL_ID_ACIDRAY:
            case SPELL_ID_FIREBALL:
            case SPELL_ID_INVOKE_LGHTNG:
            case SPELL_ID_COLUMN_ACID:
            case SPELL_ID_FIREWALL:
            case SPELL_ID_WALL_LGHTNG:
            case SPELL_ID_POISONFOG:
            case SPELL_ID_ANTIMAGIC:
                return true;
                /*
                case SPELL_ID_PROT_FIRE:
                case SPELL_ID_PROT_ELECTRIC:
                case SPELL_ID_PROT_ACID:
                case SPELL_ID_EAGLE_SIGHT:
                case SPELL_ID_INFRAVISION:
                case SPELL_ID_DETECT_LIFE:
                case SPELL_ID_INVISIBILITY:
                case SPELL_ID_SILENCE:
                case SPELL_ID_LICHDOM:
                case SPELL_ID_FIREWORKS:
                case SPELL_ID_STENCH:
                case SPELL_ID_CLAIRVOYENCE:
                case SPELL_ID_POSSESSION:
                case SPELL_ID_SHAPECHANGE:
                case SPELL_ID_VISIONFOG:
                case SPELL_ID_HEALING:
                case SPELL_ID_LINK:
                case SPELL_ID_TELEPORT:
                case SPELL_ID_STRENGTHEN:
                case SPELL_ID_WEAKEN:
                case SPELL_ID_REGENERATION:
                case SPELL_ID_FEEBLEMIND:
                case SPELL_ID_SPEED:
                case SPELL_ID_SLOW:
                case SPELL_ID_COMPREHEND:
                case SPELL_ID_STUN:
                case SPELL_ID_CHARM:
                case SPELL_ID_ENLARGE:
                case SPELL_ID_SHRINK:
                case SPELL_ID_CAMPFIRE:*/
            }
        }
    }
    return curCommand.IsAttack();
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! SOME WEAPON PARAMS
/////////////////////////////////////////////////////////////////////////////////////
// get attack range of the current weapon. If there is no weapon, get Native Attack Weapon range
float CUnitServer::GetCurrentWeaponRange()
{
    CItemWeapon *pWeapon = lpUnit->GetCurrentQWeapon();
    if (pWeapon)
        return pWeapon->GetRange();
    else
        return rpgUnitStats.fpNAWRange;
}
/////////////////////////////////////////////////////////////////////////////////////
ERPGWeaponTypes CUnitServer::GetCurrentWeaponType() const
{
    CItemWeapon *pWeapon = lpUnit->GetCurrentQWeapon();
    if (pWeapon)
        return pWeapon->GetWeaponType();
    return rpgUnitStats.rpgNAWWeaponType;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::IsCurrentWeaponABow()
{
    return IsEquippedRangedWeapon(lpUnit->GetCurrentQWeapon());
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::HasRangedAttack()
{
    CItemWeapon *pWeapon = lpUnit->GetCurrentQWeapon();
    if (pWeapon)
        return IsCurrentWeaponABow();
    return rpgUnitStats.rpgNAWWeaponType != RPGWST_MAGIC && rpgUnitStats.fpNAWRange > 1;
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnitServer::GetSpellRange(CSpell *pSpell)
{
    ASSERT(pSpell);
    if (pSpell)
        return pSpell->GetRange();
    return 10;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::CancelFollowOnLostSight(CUnit* target)
{
    if (!target || !lpUnit || curCommand.pTarget != target ||
        !lpUnit->GetOwner() || !target->GetOwner())
        return;
    curCommand.cmd = UC_IDLE;
    curCommand.nQSpellNum = 0;
    curCommand.pTarget.Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
int CUnitServer::GetSpellCastTime(CSpell *pSpell)
{
    if(auto*mod=ModRuntime::Current())if(mod->CombatSetting("UsePrototypeActionsForMobCast"))return ModSpells::CastTime(rpgUnitStats,pSpell,lpUnit&&lpUnit->GetOwner(),theSettings.nDifficultyLevel);
    if (!pSpell)
        return 0;
    float fpAction = rpgUnitStats.action.fpEffectiveValue;
    if (fpAction != 0)
        fpAction = 15 / fpAction;
    return Float2Int(pSpell->GetActionPoints() * fpAction);
}
/////////////////////////////////////////////////////////////////////////////////////
// получить итем со спелом по номеру
CItemQuick *CUnitServer::GetSpellQuickItem(int nQSpellNum)
{
    CItemQuick *pItem = lpUnit->GetQItem(nQSpellNum & 0x7fffffff);
    if (!pItem)
        return 0;
    if (pItem->WhoAmI() != HPC_ITEM_QUICK)
        return 0;
    if (pItem->GetQuickItemType() != IDB_WAND && pItem->GetQuickItemType() != IDB_SCROLL)
        return 0;
    return pItem;
}
/////////////////////////////////////////////////////////////////////////////////////
// проверить является ли итем под номером nQSpellNum поушеном
bool CUnitServer::IsPotion(int nQSpellNum)
{
    if ((nQSpellNum & 0x80000000) == 0)
        return false;
    CItemQuick *pItem = GetSpellQuickItem(nQSpellNum);
    if (!pItem || pItem->GetQuickItemType() != IDB_SCROLL)
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// получить спелл по его номеру - либо из quickspells либо из wands или scrolls
CSpell *CUnitServer::GetSpell(int nQSpellNum)
{
    if (nQSpellNum & 0x80000000)
    {
        // wand
        CItemQuick *pItem = GetSpellQuickItem(nQSpellNum);
        if (!pItem)
            return 0;
        return pItem->GetSpell(0);
    }
    // simple quick spell
    return lpUnit->GetQSpell(nQSpellNum);
}
/////////////////////////////////////////////////////////////////////////////////////
CSpell *CUnitServer::GetSpell(const SUnitBasicCmd &cmd)
{
    return GetSpell(cmd.nQSpellNum);
}
/////////////////////////////////////////////////////////////////////////////////////
// check cast possibility with rpg system
void CUnitServer::SyncNpcMagicState()
{
    auto* mod = ModRuntime::Current();
    if (!lpUnit)
        return;
    if (mod && mod->SpellAddon().Feature29() == 1)
    {
        // ASI uses the actual HasSpellEffect return value as the quick-spell slot.
        const int slot = lpUnit->HasSpellEffect(SPELL_ID_FEEBLEMIND);
        bool feebled = slot && rpgUnitStats.rpgNAWWeaponType != RPGWST_MAGIC;
        if (!feebled)
        {
            CSpell* spell = lpUnit->GetQSpell(slot);
            feebled = spell && !lpUnit->IsHero() &&
                double(spell->GetManaCost()) * rpgUnitStats.GetSpellAddonManaBonusMultiplier() >
                    GetMapObjectServer()->GetCurrentMP();
        }
        if (feebled)
            lpUnit->SetFlag(UNIT_FEEBLED);
        else
            lpUnit->ResetFlag(UNIT_FEEBLED);
        return;
    }
    // Keep the existing NPC synchronization when f29 is inactive.
    if (mod && !lpUnit->IsHero())
    {
        if (lpUnit->HasSpellEffect(SPELL_ID_FEEBLEMIND) &&
            rpgUnitStats.rpgNAWWeaponType != RPGWST_MAGIC)
            lpUnit->SetFlag(UNIT_FEEBLED);
        else
            lpUnit->ResetFlag(UNIT_FEEBLED);
    }
}
bool CUnitServer::HasAISpellMana(int slot)
{
    auto*mod=ModRuntime::Current();
    if(!mod||!lpUnit||slot<0||slot>=8)return true;
    const bool naturalMana = mod->SpellAddon().Feature18() == 4;
    if(!naturalMana&&lpUnit->IsHero()&&!mod->CompanionSkillAI(*lpUnit))return true;
    auto*spell=GetSpell(slot);auto*owner=GetMapObjectServer();if(!spell||!owner)return true;
    ModSkills::Config config;
    if(mod->SkillConfig(spell,config))
        return (lpUnit->IsHero()&&!mod->CompanionSkillAI(*lpUnit)) ||
            ModSpells::HasMana(owner->GetCurrentMP(),config.Cost(spell->GetManaCost()));
    if(naturalMana)
        return rpgUnitStats.rpgNAWWeaponType==RPGWST_MAGIC ||
            !(double(spell->GetManaCost())*rpgUnitStats.GetSpellAddonManaBonusMultiplier()>owner->GetCurrentMP());
    if(lpUnit->IsHero()&&!mod->CompanionSkillAI(*lpUnit))return true;
    return ModSpells::HasMana(owner->GetCurrentMP(),ModSpells::ManaCost(spell->GetManaCost(),rpgUnitStats.GetManaBonusMultiplier()));
}
bool CUnitServer::CanCastSpell(CSpell *pSpell, bool bCheckRPG)
{
    if(auto*mod=ModRuntime::Current()){ModSkills::Config config;if(bCheckRPG&&mod->SkillConfig(pSpell,config))return config.allowNpc&&mod->SkillReady(*lpUnit,pSpell,config,true);}
    SyncNpcMagicState();
    // lets check if this unit can cast the spell
    if (pSpell && lpUnit->IsHero() && bCheckRPG &&
        !rpgUnitStats.CanCastSpell(pSpell->GetMagicSubtype(), pSpell->GetComplexity()))
        return false;
    if (lpUnit->CheckFlag(UNIT_FEEBLED))
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// вычисляет продолжительность одного периода атаки - от начала атакующего движения до
// возможности начать следующее в сегментах
int CUnitServer::GetAttackCycleTime(EUnitCommand cmd, CItemWeapon *pWeapon)
{
    if(auto*mod=ModRuntime::Current())if(mod->CombatSetting("UsePrototypeActionsForMobAttack"))return mod->AttackCycle(rpgUnitStats,*lpUnit,cmd==UC_ATTACK);
    // calc delay to next attack
    float fpAction = rpgUnitStats.action.fpEffectiveValue;
    if (fpAction != 0)
        fpAction = 15 / fpAction;
    int nAttackCycle = 0;
    if (cmd == UC_ATTACK)
    {
        int nActions = 60;
        if (pWeapon)
            nActions = pWeapon->GetActionPoints();
        else
            nActions = rpgUnitStats.fpNAWActionPoints;
        nAttackCycle = Float2Int(nActions * fpAction);
    }
    return nAttackCycle;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::GetWeaponAttack(SRPGGeneralAttack &res) const
{
    int i;
    bool bHero = lpUnit->GetOwner() != 0;
    const auto *mod = ModRuntime::Current();
    const bool addonAttack = mod && mod->CombatSetting("JMVAttacks");
    if ((!bHero && !addonAttack) || (lpUnit->dwCurrentWeapon >= QI_MAX_WEAPONS) ||
        (lpUnit->lpQWeapons[lpUnit->dwCurrentWeapon] == NULL))
    {
        float fpDamage = rpgUnitStats.fpNAWDamageMin + rpgUnitStats.fpNAWDamageRnd;
        fpDamage = rpgUnitStats.GetModifiedDamage(lpUnit, fpDamage);
        for (i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
            res.wAttacks[i] = Float2Int(rpgUnitStats.fpNAWAttacks[i] * fpDamage);
    }
    else
    {
        float fpAttacks[RPG_NUM_ATTACK_TYPES];
        CItemWeapon *pWeapon = lpUnit->lpQWeapons[lpUnit->dwCurrentWeapon];
        float fpDamage = pWeapon->GetDamageMax();
        fpDamage = rpgUnitStats.GetModifiedDamage(lpUnit, fpDamage);
        pWeapon->GetDamageDistribution(fpAttacks);
        for (i = 0; i < RPG_NUM_ATTACK_TYPES; i++)
            res.wAttacks[i] = Float2Int(fpDamage * fpAttacks[i]);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::GetSpellAttack(int nSpellNum, SRPGGeneralAttack &res)
{
    CSpell *pSpell = GetSpell(nSpellNum);
    ASSERT(pSpell);
    memset(&res, 0, sizeof(res));
    if (pSpell && !(ModRuntime::Current()&&(pSpell->GetSubtypeID()>>16)==SPELL_ID_VAMP&&3+pSpell->GetMagicSubtype()>=int(RPG_NUM_ATTACK_TYPES)))
        res.wAttacks[pSpell->GetAttackType()] = pSpell->GetPower();
    res.wToHit = 0x7fff;
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! ATTACK CHECKS
/////////////////////////////////////////////////////////////////////////////////////
bool IsSpellCastingDirect(CWorldServer *pWS, CSpell *pSpell, CUnit *pWho, const CPlot3D &ptFrom,
                          CUnit *pTarget, const CPlot3D &ptTarget)
{
    if (!pSpell->IsRequireTrace())
        return true;
    CPlot3D ptDest;
    if (!pTarget)
    {
        ptDest = ptTarget;
        ptDest.z = FP_CHECK_VISIBILITY_HEIGHT + pWS->GetBridgeSurfaceZValue(ptDest.x, ptDest.y);
    }
    else
    {
        ptDest = pTarget->GetCP();
        ptDest.z = pTarget->GetUnitHeadHeight() + pTarget->CalcZ(ptDest.x, ptDest.y);
    }
    CPlot3D ptTest = ptFrom;
    if (pWho)
        ptTest.z = pWho->GetStandUnitHeadHeight() + pWho->CalcZ(ptTest.x, ptTest.y);
    CAIMap &aiMap = pWS->aiMap;
    if (aiMap.TraceRelief(ptTest, ptDest) < FP_MIN_ATTACK_OPACITY)
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// определить нужно ли для команды cmd трассировать террейн и если да заполнить структуру
// info
bool CUnitServer::NeedReliefTrace(const SUnitBasicCmd &cmd, SUCTraceInfo &info)
{
    if (cmd.HasPointOfUse())
        return false;
    //
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CMapObject *pTarget = cmd.pTarget;
    if (cmd.IsCast())
    {
        CSpell *pSpell = GetSpell(cmd);
        ASSERT(pSpell);
        if (!pSpell || !pSpell->IsRequireTrace())
            return false;
        info.Clear();
        // trace to target point
        if (!pTarget || pTarget->WhoAmI() != HPC_UNIT) // pSpell->IsOnTile() ||
        {
            info.ptDest = cmd.ptTarget;
            info.ptDest.z = FP_CHECK_VISIBILITY_HEIGHT +
                            pWS->GetBridgeSurfaceZValue(info.ptDest.x, info.ptDest.y);
        }
        else
        {
            info.pTarget = (CUnit *)pTarget;
            info.fpMinDist2 = sqr(lpUnit->GetCollisionDistance() +
                                  ((CUnit *)pTarget)->GetCollisionDistance() + 0.3f);
        }
    }
    else
    {
        if (!pTarget || pTarget->WhoAmI() != HPC_UNIT)
        {
            // use science or something like that
            return false;
        }
        info.Clear();
        info.fpMinDist2 = 10;
        info.pTarget = (CUnit *)pTarget;
    }
    info.fpOurHeadH = lpUnit->GetStandUnitHeadHeight();
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::SetReliefTraceDest(SUCTraceInfo &info, const CPlot3D &ptUnitPos)
{
    if (info.pTarget)
    {
        info.ptDest = ptUnitPos;
        info.ptDest.z = info.pTarget->GetUnitHeadHeight() +
                        info.pTarget->CalcZ(ptUnitPos.x, ptUnitPos.y);
        info.pTarget = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// осуществить проверку видимости цели из позиции ptFrom
bool CUnitServer::CheckRelief(SUCTraceInfo &info, const CPlot3D &ptFrom)
{
    if (info.pTarget)
    {
        info.ptDest = info.pTarget->GetUnitHeadPosition();
        info.pTarget = 0;
    }
    if (distanceXY2(info.ptDest, ptFrom) < info.fpMinDist2)
        return true;
    //
    CPlot3D ptTest = ptFrom;
    ptTest.z = info.fpOurHeadH + lpUnit->CalcZ(ptTest.x, ptTest.y);
    CAIMap &aiMap = ((CWorldServer *)GetWorld())->aiMap;
    if (aiMap.TraceRelief(ptTest, info.ptDest) < FP_MIN_ATTACK_OPACITY)
        return false;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// check up mand // and distance to target for specified spell/wand
bool CUnitServer::IsCastPossible(const SUnitBasicCmd &cmd)
{
    SyncNpcMagicState();
    auto*mod=ModRuntime::Current();auto*skill=cmd.nQSpellNum>=0&&cmd.nQSpellNum<int(lpUnit->GetSpellSlotCount())?GetSpell(cmd.nQSpellNum):nullptr;
    ModSkills::Config manaSkill;
    const bool spellAddonMana = mod && mod->SpellAddon().Feature18() == 4 &&
        !(mod->SkillConfig(skill,manaSkill));
    if(!spellAddonMana&&!HasAISpellMana(cmd.nQSpellNum))return false;
    if(cmd.cmd==UC_CASTPOINT&&mod&&mod->PointSkill(skill)){ModSkills::Config config;if(mod->SkillConfig(skill,config))return CanRequestPointSkill(cmd.nQSpellNum,cmd.ptTarget);}
    bool nativeResult=true;
    CSpell *pSpell;
    int nQSpellNum = cmd.nQSpellNum;
    // when updating this code don`t forget about unit::checkiscastpossible
    if (nQSpellNum & 0x80000000)
    {
        // wand casting
        CItemQuick *pItem = GetSpellQuickItem(nQSpellNum);
        if (!pItem)
            return false;
        pSpell = pItem->GetSpell(0);
        if (!pSpell)
            return false;
        if (pItem->GetQuickItemType() == IDB_WAND && pSpell->GetManaCost() > pItem->GetMP())
            return false;
    }
    else
    {
        // spell casting
        pSpell = lpUnit->GetQSpell(nQSpellNum);
        if (!pSpell)
            return false;
        const bool checkMana = spellAddonMana
            ? rpgUnitStats.rpgNAWWeaponType != RPGWST_MAGIC : lpUnit->IsHero();
        const bool insufficient = spellAddonMana
            ? checkMana && double(pSpell->GetManaCost()) * rpgUnitStats.GetSpellAddonManaBonusMultiplier() >
                GetMapObjectServer()->GetCurrentMP()
            : pSpell->GetManaCost() * rpgUnitStats.GetManaBonusMultiplier() >
                GetMapObjectServer()->GetCurrentMP();
        if (checkMana && insufficient)
        {
            if (mod && mod->SpellAddon().Feature29() == 1 &&
                !IsIDofPersonage(lpUnit->GetID()))
                lpUnit->SetFlag(UNIT_FEEBLED);
            nativeResult=false;
        }
    }
    // LS applies an additional ordered mana check after the native ASI predicate.
    // Natural MAGIC bypasses f18 only; wands and non-NPC heroes keep their owners.
    if (mod && (!lpUnit->IsHero() || mod->CompanionSkillAI(*lpUnit)) &&
        cmd.IsCast() && nQSpellNum >= 0 && nQSpellNum < 8)
    {
        auto* owner = GetMapObjectServer();
        if (owner)
        {
            ModSkills::Config config;
            // Addon's Lua reader accepts IEEE values. Do not use the strict R01 cache.
            float cost = mod->SkillConfig(pSpell, config) ? config.Cost(pSpell->GetManaCost())
                : pSpell->GetManaCost() * rpgUnitStats.GetSpellAddonManaBonusMultiplier();
            if (cost < 0.0f) cost = 0.0f;
            if (!(cost <= owner->GetCurrentMP() + 0.0001f)) return false;
        }
    }
    // range checking is disabled
    /*if ( fabs2( ptTarget - lpUnit->GetCP() ) > sqr( pSpell->GetRange() * 0.98f ) )
    {
            if ( !bFixTarget )
                    return false;
            ptTarget = ptTarget - lpUnit->GetCP();
            ptTarget *= pSpell->GetRange() / fabs( ptTarget );
            ptTarget = lpUnit->GetCP() + ptTarget;
    }*/
    return nativeResult;
}
/////////////////////////////////////////////////////////////////////////////////////
// calc distance for action for command
float CUnitServer::GetHitDist(const SUnitBasicCmd &cmd)
{
    float fpHitDist = 0; //, fpMinDist;
    if(ModRuntime::Current()&&cmd.HasPointOfUse()&&cmd.IsScience()&&!cmd.pTarget.IsEmpty()&&cmd.pTarget->WhoAmI()==HPC_LEVER)
        return 0.5f;
    if (cmd.HasPointOfUse())
        return FP_MINIMAL_DISTANCE_TO_POU;
    if(cmd.cmd==UC_CASTPOINT){auto*mod=ModRuntime::Current();auto*spell=GetSpell(cmd);if(mod&&mod->CombatSetting("JMVExplosiveShot")&&mod->PointSkill(spell))return (std::max)(0.f,GetCurrentWeaponRange());}
    CMapObject *pTarget = cmd.pTarget;
    switch (cmd.cmd)
    {
    case UC_CASTUNIT:
    case UC_CASTPOINT:
        if (!lpUnit->GetOwner() && rpgUnitStats.rpgNAWWeaponType == RPGWST_MAGIC)
            fpHitDist = rpgUnitStats.fpNAWRange;
        else
            fpHitDist = GetSpellRange(GetSpell(cmd));
        break;
    case UC_SKILL:
        if (cmd.IsLoot())
            fpHitDist = lpUnit->GetAttackDistance() + 0.3f * pTarget->GetAttackDistance();
        else
        {
            if (pTarget->WhoAmI() == HPC_UNIT)
                fpHitDist = lpUnit->GetCollisionDistance() +
                            ((CUnit *)pTarget)->GetCollisionDistance() + 0.6f;
            else
                fpHitDist = lpUnit->GetAttackDistance() + pTarget->GetAttackDistance() - 0.1f;
        }
        break;
    case UC_ATTACK:
    default:
        //
        if (pTarget->WhoAmI() == HPC_UNIT)
        {
            float fpWeaponRange = 0.6f;
            if (((CUnit *)pTarget)->GetOwner() && lpUnit->GetOwner())
                fpWeaponRange -= 0.4f;
            fpWeaponRange = Max(GetCurrentWeaponRange(), fpWeaponRange);
            // Retain melee reach, with room for the original body circles and
            // the approved spacing when the transverse radius was too small.
            const bool bodyMelee = cmd.cmd == UC_ATTACK && !HasRangedAttack() &&
                                   GetCurrentWeaponType() != RPGWST_MAGIC;
            auto& navigation = ((CWorldServer *)GetWorld())->aiMap.Navigation();
            fpHitDist = (bodyMelee ? navigation.Radius(lpUnit) : lpUnit->GetCollisionDistance()) +
                        (bodyMelee ? navigation.Radius((CUnit *)pTarget) :
                                     ((CUnit *)pTarget)->GetCollisionDistance()) + fpWeaponRange;
            if (bodyMelee) fpHitDist = Max(fpHitDist + FP_MELEE_SPACING,
                navigation.CollisionRadius(lpUnit) + navigation.CollisionRadius((CUnit *)pTarget) + FP_MELEE_SPACING);
        }
        else
        {
            const bool bodyMelee = cmd.cmd == UC_ATTACK && !HasRangedAttack() &&
                                   GetCurrentWeaponType() != RPGWST_MAGIC;
            const float body = bodyMelee ? ((CWorldServer *)GetWorld())->aiMap.Navigation().Radius(lpUnit) + 0.05f
                                        : lpUnit->GetAttackDistance();
            fpHitDist = body + pTarget->GetAttackDistance() + GetCurrentWeaponRange();
        }
        break;
    }
    return fpHitDist;
}
/////////////////////////////////////////////////////////////////////////////////////
// расстояние атаки для выбора между "короткой" и "длинной" атаками
float CUnitServer::GetCloseAttackDist(const SUnitBasicCmd &cmd)
{
    ASSERT(cmd.cmd == UC_ATTACK);
    float fpHitDist = 0;
    CMapObject *pTarget = cmd.pTarget;
    float fpAttackDistance = lpUnit->GetAttackDistance();
    fpHitDist = fpAttackDistance + pTarget->GetAttackDistance(); // + GetCurrentWeaponRange();
    if (pTarget && pTarget->WhoAmI() == HPC_UNIT)
    {
        // насколько монстр выпрыгивает во время атаки в единицах собственной длины ( 1 - монстр
        // выпрыгивает на половину собственной длины)
        float fpAttackKoef = rpgUnitStats.fpAttackDistance;
        // fpAttackKoef = ( 1 - complection.tall ) + complection.tall * fpAttackKoef;
        const bool bodyMelee = !HasRangedAttack() && GetCurrentWeaponType() != RPGWST_MAGIC;
        auto& navigation = ((CWorldServer *)GetWorld())->aiMap.Navigation();
        if (bodyMelee) fpAttackDistance = navigation.Radius(lpUnit) + 0.05f;
        fpAttackDistance *= (1 + fpAttackKoef);
        fpHitDist = fpAttackDistance + (bodyMelee ? navigation.Radius((CUnit *)pTarget) :
                                                 ((CUnit *)pTarget)->GetCollisionDistance());
        if (bodyMelee) fpHitDist = Max(fpHitDist + FP_MELEE_SPACING,
            navigation.CollisionRadius(lpUnit) + navigation.CollisionRadius((CUnit *)pTarget) + FP_MELEE_SPACING);
    }
    return fpHitDist;
}
/////////////////////////////////////////////////////////////////////////////////////
// вернет TRUE, если в момент nSegment никакие юниты не будут мешать нанесению удара
// по pTarget в точке ptTarget из текущего положения юнита
// можно использовать список потенциально видимых юнитов для проверки на столкновения так как
// атаковать можно только видимого юнита ( по крайней мере способом для которого необходима
// проверка AttackIsDirect()
bool CUnitServer::AttackIsDirect(const SUnitBasicCmd &cmd, const CPlot3D &ptTarget, int nSegment)
{
    float fpDist2;
    if (((CUnit *)cmd.pTarget) == lpUnit)
        return true;

    if (cmd.HasPointOfUse())
        return true;
    // CRAP (actually this should account vision) ai units can not attack from too far
    if (!lpUnit->GetOwner() && !cmd.pTarget.IsEmpty())
    {
        float fpSight = Max(GetVisionSense(), GetLifeSense()) * 1.05f;
        if (distanceXY2(lpUnit->GetCP(), ptTarget) > sqr(fpSight))
            return false;
    }
    //
    if (!cmd.IsCast())
    {
        // here we get an attack/skill, check if we are not using a bow or a long range weapon
        // then no units should be on the path
        if (cmd.cmd == UC_SKILL ||
            (cmd.IsAttack() && (!IsCurrentWeaponABow() && GetCurrentWeaponRange() < 3)))
        {
            CPlot3D ptVec1, ptVec2;
            ptVec1 = ptTarget - lpUnit->GetCP();
            fpDist2 = fabs2(ptVec1);
            ptVec1 /= fpDist2;
            ptVec2.x = ptVec1.y;
            ptVec2.y = -ptVec1.x;
            ptVec2.z = 0;
            ptVec2 /= fabs(ptVec2);
            CMapObjectPtrList &list = *logic.environment.GetUnitsList();
            for (POSITION pos = list.GetHeadPosition(); pos;)
            {
                CUnit *pUnit = (CUnit *)list.GetNext(pos);
                if (pUnit == cmd.pTarget)
                    continue;
                if (pUnit->IsDead())
                    continue;
                float fpC1, fpC2;
                CPlot3D ptToUnit = pUnit->GetFuturePosition(nSegment) - lpUnit->GetCP();
                fpC1 = ptToUnit * ptVec1;
                fpC2 = ptToUnit * ptVec2;
                if (fpC1 > 0.1f && fpC1 < 0.9f && fpC2 > -0.3f && fpC2 < 0.3f)
                    return false;
            }
        }
    }
    else
    {
        if (lpUnit->CheckFlag(UNIT_FEEBLED))
            return false;
    }
    SUCTraceInfo info;
    if (NeedReliefTrace(cmd, info))
    {
        SetReliefTraceDest(info, ptTarget);
        if (!CheckRelief(info, lpUnit->GetCP()))
            return false;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// отличается от простого CreateAttackPlan тем, что не возвращает результат и фиксирует
// цель на одном месте
bool CUnitServer::CreateTestAttackPlan(const SUnitBasicCmd &cmd)
{
    SL1Command tmp;
    CMapObject *pTarget = cmd.pTarget;
    if (pTarget && pTarget->WhoAmI() == HPC_UNIT)
        pTarget->SetFlag(UNIT_GETFUTUREPOS);
    bool bRv = CreateAttackPlan(cmd, tmp);
    if (pTarget && pTarget->WhoAmI() == HPC_UNIT)
        pTarget->ResetFlag(UNIT_GETFUTUREPOS);
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
// а не лутит ли уже кто pTarget?
bool CUnitServer::IsLootLocked(CMapObject *pTarget)
{
    if (pTarget->CheckFlag(UNIT_LOOT_LOCKED))
        return true;

    CMapObjectPtrList &list = *logic.environment.GetUnitsList();
    for (POSITION pos = list.GetHeadPosition(); pos;)
    {
        CUnit *pUnit = (CUnit *)list.GetNext(pos);
        if (!pUnit || pUnit->IsDead())
            continue;
        CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
        if (pUS->curCommand.IsLoot() && pUS->curCommand.pTarget == pTarget &&
            (pUS->L1IsAttacking() || pUS->bLcChangingPoseToAttack))
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// проверить, достаем ли мы цель по высоте
bool CUnitServer::IsTargetReachable(const SUnitBasicCmd &cmd)
{
    if (cmd.cmd != UC_ATTACK && cmd.cmd != UC_SKILL)
        return true;
    CUnit *pTarget = (CUnit *)cmd.pTarget;
    if (!pTarget || pTarget->WhoAmI() != HPC_UNIT)
        return true;
    if (pTarget->IsDead())
        return true;
    if(lpUnit->GetUserUnitStats()->cLocomotion==3||pTarget->GetUserUnitStats()->cLocomotion==3) {
        if(cmd.cmd==UC_ATTACK&&IsCurrentWeaponABow())return true;
        const auto a=lpUnit->GetCP(),b=pTarget->GetCP();
        return std::abs(lpUnit->CalcZ(a.x,a.y)-pTarget->CalcZ(b.x,b.y))<1.5f;
    }
    if (pTarget->GetAIClass() != 0)
        return true;
    // now we have to check heights
    if (cmd.cmd == UC_ATTACK && IsCurrentWeaponABow())
        return true;
    return pTarget->GetPrototype()->fAltitude + pTarget->GetCP().z < 1.5f;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::CanAttack(const SUnitBasicCmd &cmd)
{
    if (!IsTargetReachable(cmd))
        return false;
    return CreateTestAttackPlan(cmd);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::CanAttackFrom(const SUnitBasicCmd &cmd, const CPlot3D &ptPos)
{
    if (!IsTargetReachable(cmd))
        return false;
    CPlot3D prevCP = lpUnit->GetCP();

    lpUnit->cp = ptPos;
    bool bRv = CreateTestAttackPlan(cmd);
    lpUnit->cp = prevCP;
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Attack planning system
/////////////////////////////////////////////////////////////////////////////////////
// function to create attack plans return TRUE if attack plan creation succeded
// attack should be made from point unit standing on
// calcs attackInfo
bool CUnitServer::CreateAttackPlan(const SUnitBasicCmd &atk, SL1Command &cmd)
{
    int nHit;
    SL0AnimationSeq animation;
    CPlot3D unitPos = lpUnit->GetCP(), unitOrientation;
    int nCurSegment = GetWorldBase()->GetSegmentTimeCounter();
    float fpRotationSpeed, fpOriginalAngle, fpTargetAngle, fpAngle, fpMoveSpeed;
    float fpDist, fpPrevDist, fpHitDist, fpHitDistRun;
    int nRotationTimeMax;
    CSpell *pSpell = 0;

    // Existing overlaps (including loaded ones) must clear before melee can
    // stop movement and leave both bodies standing inside one another.
    if (atk.IsAttack() && !atk.HasPointOfUse() && !HasRangedAttack() && GetCurrentWeaponType() != RPGWST_MAGIC &&
        !atk.pTarget.IsEmpty() && atk.pTarget->WhoAmI() == HPC_UNIT &&
        ((CWorldServer *)GetWorld())->aiMap.CheckCircleCollision(lpUnit, lpUnit->GetCP()))
        return false;
    // check if we arrived to use point, if one is specified
    if (atk.HasPointOfUse())
    {
        if (distanceXY2(atk.ptTarget, lpUnit->GetCP()) > sqr(GetHitDist(atk)))
            return false;
    }
    cmd.Clear();
    // choose animation
    switch (atk.cmd)
    {
    case UC_ATTACK:
        CalcAttackDirection(cmd, animation, nHit, atk.pTarget);
        break;
    case UC_CASTPOINT:
    case UC_CASTUNIT: {
        pSpell = GetSpell(atk);
        auto*mod=ModRuntime::Current();ModSkills::Config config;
        if(atk.cmd==UC_CASTPOINT&&mod&&mod->PointSkill(pSpell)&&mod->SkillConfig(pSpell,config)){cmd.eAttackLocation=RPGHL_BODY;GetAttackAnimation(animation,nHit,UC_ATTACK,ADB_MODIFIER_HDIRECTION1_MIDDLE|ADB_MODIFIER_VDIRECTION1_MIDDLE);}
        else GetCastAnimation(animation, nHit, atk.cmd, GetSpellCastTime(pSpell));
    }
    break;
    default:
        GetAttackAnimation(animation, nHit, atk.cmd, atk.nQSpellNum);
        break;
    }
    if (nHit <= 0)
    {
#ifdef _DEBUG
        CString szTemp;
        szTemp.Format("Attack has zero nHit\n");
        OutputDebugString(szTemp);
#endif
        nHit = 1;
    }
    // Only the two target predictions use the ASI's fixed hit offset.
    const CModPackage *predictionMod = ModRuntime::Current();
    const int nPredictionHit = predictionMod && predictionMod->SpellAddon().Feature28() == 1
                                   ? 5 : nHit;
    // calc distance from which hit is possible
    fpHitDist = GetHitDist(atk);
    if (atk.HasPointOfUse())
        fpHitDist = 1e10f;
    // determine start orientation and acquire some rotation parameters
    unitOrientation = -lpUnit->GetYAxis();
    fpOriginalAngle = float(atan2(unitOrientation.y, unitOrientation.x));
    lpUnit->GetSpeeds(fpMoveSpeed, fpRotationSpeed);
    nRotationTimeMax = Float2Int(FP_PI / fpRotationSpeed) + 3;

    // по убегающему юниту расстояние больше, эквивалентно атаке из бега
    bool bPredictTargetPos = false;
    int nPredictShift = 0;
    CUnit *pTargetUnit = 0;
    CPlot3D ptTargetDestPos = atk.GetTargetCP(); // положение цели
    CPlot3D ptDir = ptTargetDestPos - unitPos;   // направление на цель
    float fpStartDist2 = distanceXY2(unitPos, ptTargetDestPos);
    float fpTargetMove = 0;
    if (!atk.pTarget.IsEmpty() && atk.pTarget->WhoAmI() == HPC_UNIT)
        pTargetUnit = (CUnit *)atk.pTarget;
    if (pTargetUnit)
    {
        fpTargetMove = atk.GetTargetSpeed();
        // если юнит находится в пределах досягаемости в начале атаки, то атака возможна
        // и ее расстояние учитывает перемещение
        if (fpStartDist2 <= sqr(fpHitDist) && (unitOrientation * ptDir) > 0.5f * sqrt(fpStartDist2))
        {
            fpHitDistRun = sqr(fpHitDist +
                               (nPredictionHit + nRotationTimeMax / 2) * AITile2Meter(fpTargetMove) + 0.5f);
        }
        else
        {
            // в обычной ситуации атака производится по юниту на расстоянии атаки
            fpHitDistRun = sqr(fpHitDist + 0.5f);
            if (pTargetUnit->IsMoving())
                bPredictTargetPos = true;
        }
        if (atk.IsCast() && !atk.HasPointOfUse() && pTargetUnit->IsMoving())
        {
            bPredictTargetPos = true;
            float fpSpeed;
            CSpell *pSpell = GetSpell(atk);
            if (pSpell && pSpell->IsOnTile() && (fpSpeed = pSpell->GetSpeed()) > 0)
                nPredictShift = 15 * sqrt(fpStartDist2) / fpSpeed - 3;
        }
    }
    else
    {
        fpHitDistRun = sqr(fpHitDist);
    }
    cmd.fpHitDist2 = fpHitDistRun; // допустимое расстояние атаки

    // test attack possibility in i segments
    fpHitDist = sqr(fpHitDist); // square distance beforehand to avoid multiple sqrt
    fpPrevDist = 1e30f;

    for (int i = 0; i < nRotationTimeMax; i++, fpPrevDist = fpDist)
    {
        if (bPredictTargetPos)
        {
            ptTargetDestPos = pTargetUnit->GetFuturePosition(nCurSegment + i + nPredictionHit +
                                                             nPredictShift);
            ptDir = ptTargetDestPos - unitPos;
        }
        // check if distance is ok to hit
        fpDist = ptDir.x * ptDir.x + ptDir.y * ptDir.y;
        if (fpDist > fpHitDist)
        {
            if (fpDist <= fpPrevDist)
                continue;
            // если цель удаляется
            if (fpDist > fpPrevDist)
            {
                // цель слишком далеко - дальше проверять бесполезно
                if (fpDist >= fpHitDistRun)
                    return FALSE;
            }
        }
        // check time to rotate is enough
        if (ptDir.x == 0 && ptDir.y == 0)
            fpAngle = 0;
        else
        {
            if (pTargetUnit && pTargetUnit->GetID() ==
                                   lpUnit->GetID()) // не надо поворачиваться, чтобы атаковать себя
                fpAngle = 0;
            else
            {
                fpTargetAngle = float(atan2(ptDir.y, ptDir.x));
                fpAngle = fabs(fpTargetAngle - fpOriginalAngle);
                if (fpAngle > FP_PI)
                    fpAngle = 2 * FP_PI - fpAngle;
                if (fpAngle > 0.01f && int(fpAngle / fpRotationSpeed) + 1 > i)
                    continue;
            }
        }
        // all tests are ok, create attack plan
        if (!AttackIsDirect(atk, ptTargetDestPos, nCurSegment + i + nHit))
            return false;
        // fill rest of cmd
        cmd.nTargetID = atk.GetTargetID();
        cmd.nTimeStrikeMoment = nCurSegment + i;
        cmd.nHitTime = cmd.nTimeStrikeMoment + nHit;
        cmd.attackAnimation = animation;
        cmd.bCalledShot = ERPGHitLocations(atk.nQSpellNum) != RPGHL_ALL;
        cmd.ptDir = ptDir;
        if (atk.IsCast())
        {
            ASSERT(pSpell);
            cmd.bCast = true;
            if (pSpell)
            {
                if (pSpell->IsOnTile())
                    cmd.nTargetID = -1;
                auto *animationMod = ModRuntime::Current();
                if (animationMod && animationMod->CombatSetting("LoadSpellAnimationFromSlotsField"))
                    cmd.castType = static_cast<ERPGMagicSubtypes>(pSpell->GetPrototype()->dwSlots);
                else switch (pSpell->GetSubtypeID() >> 16)
                {
                case SPELL_ID_RICKMAGIC:
                    cmd.castType = RPGMST_LIGHTNING;
                    break;
                case SPELL_ID_CURSEMAGIC:
                    cmd.castType = RPGMST_FIRE;
                    break;
                default:
                    cmd.castType = pSpell->GetMagicSubtype();
                    break;
                }
            }
            else
                cmd.castType = RPGMST_UNKNOWN;
        }
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// move to target during attack - rather difficult function for ranged attacks :)
// failures to move to target currently are untracked
void CUnitServer::MoveToTarget()
{
    CPlot3D ptDest;
    float fpDistance, fpSpeed, fpChaseTime, fpHitDist;
    CWorldServer *pWS = (CWorldServer *)GetWorldBase();
    int nCurSegment = pWS->GetSegmentTimeCounter(), nTimeToHit, nForwardTime;

    lpUnit->bNeedFastPath = TRUE;
    // if we need to move to the point of use or point of cast destination
    if (curCommand.HasPointOfUse() || curCommand.pTarget.IsEmpty())
    {
        ptDest = curCommand.ptTarget;
        if (L1IsMoving() && distanceXY2(ptDest, path.ptDestination) <= sqr(0.3f))
            return;
        bool bFound = L1StartMove(ptDest);
        if (bFound)
        {
            // а туда ли путь-то нашли? :)
            if (distanceXY2(path.GetFinalPoint(), ptDest) > sqr(0.3f))
            {
                if (!CanAttackFrom(curCommand, path.GetFinalPoint()))
                    bFound = false;
            }
        }
        if (!bFound)
        {
            InformAttackPathNotFound(curCommand);
            LcCancelCommand();
            bLcCommandFail = true;
        }
        return;
    }
    // more conventional path - we have some target and move to it
    CMapObject *pTarget = curCommand.pTarget;
    const bool bodyMelee = curCommand.IsAttack() && pTarget->WhoAmI() == HPC_UNIT &&
                           !HasRangedAttack() && GetCurrentWeaponType() != RPGWST_MAGIC;
    const bool overlapping = bodyMelee && pWS->aiMap.CheckCircleCollision(lpUnit, lpUnit->GetCP());
    fpHitDist = GetHitDist(curCommand);
    CPlot3D ptTargetPos = pTarget->GetTargetCP();
    if (!overlapping && !pTarget->IsMoving() && distanceXY2(ptTargetPos, lpUnit->GetCP()) <= sqr(fpHitDist) &&
        AttackIsDirect(curCommand, ptTargetPos, nCurSegment))
    {
        L1StartIdle();
        return;
    }
    // проверим, надо ли бежать за движущейсся целью или мы и так успеем и можно отдохнуть
    if (pTarget->IsMoving() && !overlapping)
    {
        nTimeToHit = nCanAttackTime + N_MAXIMUM_HIT_FRAME - nCurSegment;
        if (nTimeToHit > 0)
        {
            fpDistance = fabs(pTarget->GetFuturePosition(nTimeToHit + nCurSegment) -
                              lpUnit->GetCP());
            fpSpeed = AITile2Meter(lpUnit->GetSpeed());
            if (fpSpeed > 0)
            {
                fpChaseTime = (fpDistance - fpHitDist) / fpSpeed;
                if (fpChaseTime < nTimeToHit)
                {
                    L1StartIdle();
                    return;
                }
            }
        }
    }
    // move to target
    if (L1IsFree() || L1IsIdle() || nMoveToTargetTime - nCurSegment < 16)
    {
        if (train.bIsTrain)
        {
            CUnitServer *pMaster = (CUnitServer *)train.master->GetUnitSpecific();
            if (pMaster->curCommand.pTarget == pTarget && pMaster->IsAttacking() &&
                !pMaster->L1IsAttacking())
                pTarget = train.master;
            else
                train.Clear();
        }
        float fpTargetDist = fabs(pTarget->GetTargetCP() - lpUnit->GetCP());
        float fpMaxSpeed = lpUnit->GetSpeed();
        if (pTarget->IsMoving())
        {
            CPlot3D ptDir = pTarget->GetCP() - lpUnit->GetCP();
            float fpDirLeng = fabs(ptDir);
            if (fpDirLeng > 0)
                ptDir /= fpDirLeng;
            fpMaxSpeed += pTarget->GetSpeed() * (ptDir * pTarget->GetYAxis());
        }
        fpMaxSpeed = Max(0.1f, fpMaxSpeed);
        nForwardTime = Min(64, Float2Int((fpTargetDist - fpHitDist) / fpMaxSpeed)); // 0.1f ) );
        nForwardTime = Max(2, nForwardTime);
        ptDest = pTarget->GetFuturePosition(nCurSegment + nForwardTime);
        if (L1IsMoving() &&
            distanceXY2(ptDest, path.ptDestination) < sqr(0.1f + fpTargetDist * 0.1f + (bodyMelee ? fpHitDist : 0.f)))
        {
            // set maximal path length
            path.ptDestination.z = lpUnit->GetMaxAttackPathLength(fpTargetDist) + 5;
            return;
        }
        // если это юнит под управлением ai, то в атаку в обход он не пойдет
        float fpMaxPathLength = lpUnit->GetMaxAttackPathLength(fpTargetDist) + 5;
        // если до цели дойти нельзя, то облом
        bool bGraphRebuild = theWorldServer.aiMap.SetGraphRebuild(false);
        if (bodyMelee || distanceXY2(lpUnit->cp, ptDest) > 0.1f)
        {
            CPlot3D ptPathDest(lpUnit->GetCP() * 0.001f + ptDest * 0.999f);
            ptPathDest.z = ptDest.z;
            Navigation::Location approach;
            bool found = true;
            if (bodyMelee)
            {
                found = pWS->aiMap.Navigation().AttackPosition(lpUnit, (CUnit *)pTarget,
                    {ptDest.x, ptDest.y, 0}, fpHitDist, fpMaxPathLength, approach);
                if (found)
                {
                    ptPathDest.x = approach.position.x;
                    ptPathDest.y = approach.position.y;
                }
            }
            else
                theWorldServer.aiMap.RemoveObject(pTarget);
            if (!found)
                L1StopMovement(true);
            else if (!L1StartMove(ptPathDest, fpMaxPathLength, bodyMelee ? &approach : nullptr))
                L1SetMoveFailed();
            if (!bodyMelee)theWorldServer.aiMap.AddObject(pTarget);
            theWorldServer.aiMap.SetGraphRebuild(bGraphRebuild);
            // если к цели нельзя подойти достаточно близко, то облом
            if (distanceXY2(ptDest, path.GetFinalPoint()) > sqr(fpHitDist))
                L1StopMovement(true); //( L1SetMoveFailed();
            nMoveToTargetTime = nCurSegment + nForwardTime;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::StartAttack()
{
    CPlot3D ptFuture;
    int nCurSegment = GetWorldBase()->GetSegmentTimeCounter();
    bLcChangingPoseToAttack = false;
    if (nCanAttackTime > nCurSegment && curCommand.IsAttack())
    {
        MoveToTarget(); // including ResetAttackPlan();
        return;
    }
    // allow weapon change in case of attacking without pauses
    if (auto* mod=ModRuntime::Current();mod && mod->CombatSetting("JMVAttacks")) {
        if (ModEquipmentSwitch::BeforeAttack(ModEquipmentSwitch::RuntimeCache(),int(lpUnit->GetID()),
                lpUnit->dwCurrentWeapon,lpUnit->dwWishWeapon,true,mod->CombatSetting("JMVNoWeaponSwitchShit")!=0))
            lpUnit->ChangeWeapon(lpUnit->dwWishWeapon);
    } else L1ChangeWeapon();
    if (CreateAttackPlan(curCommand, level1Command))
    {
        /*
        if ( curCommand.cmd == UC_SKILL && pTarget && pTarget->IsMoving() )
        {
                InformFailure( UAF_CANT_USE_SKILL_ON_MOVE );
                bLcCommandFail = true;
                if ( nextCommand.cmd != UC_IDLE )
                        CmdWait();
                return;
        }*/
        // если кто-то уже начал лутить этого товарища, то мы не успели
        if (curCommand.IsLoot() && IsLootLocked(curCommand.pTarget))
        {
            LcCancelCommand();
            return;
        }
        // примем позу, удобную для выбранного действия
        bool bNeedPose = false;
        if (curCommand.IsSkill())
        {
            /*if ( currentPose.pose != UP_CRAWL )
            {
                    LowNewWishPose( UP_CRAWL );
                    bNeedPose = true;
            }*/
        }
        else if (curCommand.IsCast() && lpUnit->GetTypeID() == MOTID_HUMAN)
        {
            if (currentPose.pose == UP_LAY)
            {
                LowNewWishPose(UP_CRAWL);
                bNeedPose = true;
            }
        }
        else
        {
            if (currentPose.pose != UP_STAND)
            {
                LowNewWishPose(UP_STAND);
                bNeedPose = true;
            }
        }
        //
        LcConvertPoses();
        if (!LcChangePose())
        {
            if (bNeedPose)
            {
                bLcCommandFail = true;
                LcCancelCommand();
                return;
            }
        }
        else
        {
            bLcChangingPoseToAttack = true;
            return;
        }
        // а доступна ли цель для текущего оружия
        if (!IsTargetReachable(curCommand))
        {
            InformFailure(ACK_HERO_REFUSE_TARGET_TOO_HIGH);
            LcCancelCommand();
            return;
        }
        // последние проверки перед атакой
        if (nCanAttackTime > level1Command.nTimeStrikeMoment && curCommand.IsAttack())
            MoveToTarget(); // including ResetAttackPlan();
        else
        {
            // calc delay to next attack
            if (curCommand.IsAttack())
            {
                int nAttackCycle = GetAttackCycleTime(curCommand.cmd, lpUnit->GetCurrentQWeapon());
                nCanAttackTime = Max(nCanAttackTime, nCurSegment + nAttackCycle);
            }
            if (curCommand.IsCast() && !IsCastPossible(curCommand))
            {
                if ((curCommand.nQSpellNum & 0x80000000) == 0)
                    InformFailure(ACK_HERO_REFUSE_CANT_CAST);
                else
                    InformFailure(ACK_HERO_REFUSE_SCIENCE_FAILED);
                bLcCommandFail = true;
                LcCancelCommand();
                return;
            }
            // determine if we need to predict target position
            bool bPredictTarget = true;
            if (!curCommand.pTarget.IsEmpty() && curCommand.pTarget->WhoAmI() == HPC_UNIT)
            {
                CUnit *pTarget = (CUnit *)curCommand.pTarget;
                CUnitServer *pUS = (CUnitServer *)pTarget->GetUnitSpecific();
                if (((CUnit *)pUS->curCommand.pTarget) == lpUnit)
                    bPredictTarget = false;
            }
            // start attack itself
            BYTE nSoundSet = 0;
            if (curCommand.cmd == UC_ATTACK)
            {
                CUnit *pTarget = (CUnit *)curCommand.pTarget;
                if (pTarget->WhoAmI() == HPC_UNIT)
                {
                    CUnitServer *pUS = (CUnitServer *)pTarget->GetUnitSpecific();
                    // Native JMV attack start is a no-op; its original DetermineHit wrapper returns false.
                    // Build the real portion only at the impact frame, avoiding an early RNG/critical owner.
                    if (UsesNativeAttackPhase(curCommand) ||
                        UsesNativeOrdinaryImpact(lpUnit, curCommand, level1Command.attackAnimation)) nSoundSet = 1;
                    else {
                        UseAttackStart(curCommand.pTarget, level1Command.eAttackLocation,
                                       level1Command.bCalledShot);
                        if (ModSkills::Slot(curCommand.dwParam)<0 && !pUS->DetermineHit(&level1AP, GetUnit()))
                            nSoundSet = 1;
                    }
                }
            }
            L1StartAttack(bPredictTarget, nSoundSet);
            TraceMeleeReach(0, curCommand.pTarget);
        }
    }
    else
    {
        // атака не удалась - сообщим об этом
        /*
        if ( curCommand.cmd == UC_CASTUNIT && lpUnit->GetOwner() )
        {
                InformFailure( UAF_CANT_CAST );
                bLcCommandFail = true;
                LcCancelCommand();
                return;
        }*/
        MoveToTarget();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// выполнить атаку, включая подход на расстояние атаки
void CUnitServer::ExecAttack()
{
    CPlayer *pPlayer;
    bool bTargetVisible = true;
    pPlayer = lpUnit->GetOwner();
    // check if command is canceled due to out of sight
    CUnit *pTarget = (CUnit *)curCommand.pTarget;
    if (pPlayer && pTarget && pTarget->WhoAmI() == HPC_UNIT)
    {
        if (!logic.environment.IsUnitVisible(lpUnit, pTarget))
            bTargetVisible = ((CPlayerServer *)pPlayer->GetPlayerSpecific())
                                 ->IsUnitVisibleToParty(pTarget);
    }
    // не пора ли прекратить выполнение команды по тем или иным причинам
    if (!LcTargetIsValid(curCommand) || L1IsMoveFailed() || !bTargetVisible)
    {
        if (L1IsMoveFailed())
        {
            bLcCommandFail = true;
            InformAttackPathNotFound(curCommand);
        }
        if (!bTargetVisible)
        {
            bLcCommandFail = true;
            InformFailure(ACK_HERO_REFUSE_CANT_SEE_TARGET);
        }
        LcCancelCommand();
        L1CancelAttack();
        return;
    }
    StartAttack();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::CalcAttackDest(SUnitCommand &cmd, CAIFullRoute *pRes)
{
    CWorldServer *pWS = (CWorldServer *)GetWorldBase();
    CMapObject *pTarget = cmd.pTarget;

    cmd.ptTarget.z = -1;
    if (cmd.cmd == UC_SKILL && !cmd.pTarget.IsEmpty())
    {
        switch (cmd.nQSpellNum)
        {
        case RPGMSKL_SCIENCE: {
            auto* lever=ModRuntime::Current()&&pTarget->WhoAmI()==HPC_LEVER?static_cast<CLeverObject*>(pTarget):nullptr;
            const bool missingPoints=lever&&!lever->UsesAuthoredInteraction();
            bool ring=missingPoints;
            if(lever){cmd.bUsePoint=false;modLeverTarget=nullptr;modLeverId=0;modLeverRing=false;}
            CArray<CPlot3D, CPlot3D &> points;
            pTarget->GetUsePoints(points,lpUnit->GetAttackDistance());
            if(missingPoints&&points.IsEmpty())return false;
            if(!points.IsEmpty()){
                SAIPath pathFake;
                int bestCost=0x7fffffff;CPlot3D bestPoint;
                for(int attempt=0;attempt<2;++attempt){
                    for(int i=0;i<points.GetSize();++i){
                        CPlot3D destination=points[i];pathFake.Clear();
                        bool found=pWS->aiMap.NewPath(lpUnit,destination,pathFake);
                        const float tolerance=lever?0.5f:FP_MINIMAL_DISTANCE_TO_POU;
                        if(found)found=distanceXY2(pathFake.GetFinalPoint(),destination)<sqr(tolerance);
                        if(found&&pathFake.nPathCost<bestCost){
                            bestCost=pathFake.nPathCost;bestPoint=destination;
                            if(pRes)pathFake.GetWholeRoute(*pRes);
                        }
                    }
                    if(bestCost!=0x7fffffff||!lever||ring)break;
                    // Retry rejected authored/world-XY points on the close ring.
                    points.SetSize(0);lever->GetFallbackUsePoints(points,lpUnit->GetAttackDistance());ring=true;
                }
                static unsigned leverPathSamples=0;
                if(ModRuntime::Current()&&leverPathSamples++<24)EngineDiagnostics::Event("Lever route object=%d candidates=%d fallback=%d found=%d radius=%g",pTarget->GetID(),points.GetSize(),ring,bestCost!=0x7fffffff,lpUnit->GetAttackDistance());
                if(bestCost==0x7fffffff){if(lever)cmd.bUsePoint=false;return false;}
                cmd.ptTarget=bestPoint;cmd.bUsePoint=true;
                if(lever){modLeverTarget=lever;modLeverId=lever->GetRTID();modLeverPoint=bestPoint;modLeverRing=ring;}
            }
        }
        break;
        }
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
float CUnitServer::CalcAttackPathLeng(const SUnitCommand &cmd, CAIFullRoute &move)
{
    if (!move.IsMoving())
        return 0;
    //
    CWorldServer *pWS = (CWorldServer *)GetWorldBase();
    bool bNeedTrace;
    CPlot3D ptTraceSrc, ptPrevPos, ptTarget, ptFrom;
    SUCTraceInfo trace;
    CRouteWalker walk;
    float fpTime, fpDist2;
    //
    walk.SetSpeeds(FP_SHOW_PATH_SPEED, 1e10f);
    walk.CreateWalkerRoute(move);
    bNeedTrace = NeedReliefTrace(cmd, trace);
    ptPrevPos = CPlot3D(0, 0, -1000);
    fpDist2 = sqr(GetHitDist(cmd));
    ptTarget = cmd.GetTargetCP();
    //
    for (fpTime = 0;; fpTime += 5)
    {
        CPlot3D vec;
        float fpSpeed;
        bool bRotate;
        int nTileX, nTileY;

        walk.GetPosition(0, move, fpTime + move.GetStartSegment(), bRotate, ptTraceSrc, vec,
                         fpSpeed, nTileX, nTileY);
        if (ptTraceSrc == ptPrevPos)
            break;
        ptPrevPos = ptTraceSrc;
        ptTraceSrc.x = AITile2Meter(ptTraceSrc.x);
        ptTraceSrc.y = AITile2Meter(ptTraceSrc.y);
        // расстояние
        if (distanceXY2(ptTraceSrc, ptTarget) > fpDist2)
            continue;
        if (!bNeedTrace)
            break;
        // террейн
        if (CheckRelief(trace, ptTraceSrc))
            break;
    }
    return fpTime;
}
/////////////////////////////////////////////////////////////////////////////////////
// найти точку использования, если нужно. Для юнитов игрока найти путь использования и
// показать его
bool CUnitServer::FindAttackPath(SUnitCommand &cmd, DWORD dwPositiveAck)
{
    bool bRv = true;
    CPlayer *pPlayer = lpUnit->GetOwner();
    if (cmd.cmd != UC_SKILL && !pPlayer)
        return true;
    float fpPathLeng = 1e6f;
    CAIFullRoute showPath;
    CPlayerServer *pPS = (CPlayerServer *)(lpUnit->GetOwner()->GetPlayerSpecific());
    CPlot3D ptTargetCP = cmd.GetTargetCP();

    if (CalcAttackDest(cmd, &showPath))
    {
        if (!pPlayer)
            return true;
        if (CanAttack(cmd))
        {
            ShowFoundPath(showPath, ptTargetCP, CPlayer::PPT_NOPATH, CPlayer::PPR_OK);
            InformFailure(dwPositiveAck);
            return true;
        }
        // определим тип атаки
        CPlayer::EPathType type = CPlayer::PPT_USE;
        if (cmd.cmd == UC_ATTACK)
            type = CPlayer::PPT_ATTACK;
        if (cmd.IsCast())
        {
            CSpell *pSpell = GetSpell(curCommand.nQSpellNum);
            if (pSpell && pSpell->GetMagicSchool() == RPGMS_ELEMENTAL)
                type = CPlayer::PPT_ATTACK;
        }
        bool bCanAttack = false;
        // если это обычная атака движущегося юнита нарисум просто путь до юнита
        if (!cmd.HasPointOfUse())
        {
            SAIPath pathFake;
            bool bGraphRebuild;
            CMapObject *pTarget = cmd.pTarget;
            // если путь найти не удалось, то все равно надо показать, что все Ok
            // потому что может быть можно атаковать с текущей позиции
            if (pTarget)
            {
                bGraphRebuild = theWorldServer.aiMap.SetGraphRebuild(false);
                theWorldServer.aiMap.RemoveObject(pTarget);
            }
            if (!LowFindPath(ptTargetCP, pathFake, TRUE))
                pathFake.CreateStand(lpUnit);
            //
            if (pTarget)
            {
                theWorldServer.aiMap.AddObject(pTarget);
                theWorldServer.aiMap.SetGraphRebuild(bGraphRebuild);
            }
            //
            pathFake.GetWholeRoute(showPath);
            bCanAttack = CanAttackFrom(cmd, showPath.GetFinalPoint());
            if (bCanAttack)
                fpPathLeng = CalcAttackPathLeng(cmd, showPath);
        }
        else
            bCanAttack = CanAttackFrom(cmd, showPath.GetFinalPoint());
        // visualize found info
        CPlayer::EPathResult res = bCanAttack ? CPlayer::PPR_OK : CPlayer::PPR_FAILED;
        ShowFoundPath(showPath, ptTargetCP, type, res, fpPathLeng);
        bRv = bCanAttack;
    }
    else
    {
        ShowFoundPath(showPath, ptTargetCP, CPlayer::PPT_NOPATH, CPlayer::PPR_FAILED);
        bRv = false;
    }
    if (bRv)
        InformFailure(dwPositiveAck);
    else
        InformAttackPathNotFound(cmd);
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::ShowFoundPath(CAIFullRoute &move, const CPlot3D &ptDest, int pathType,
                                int pathResult, float fpShowLeng)
{
    CPlayer *pPlayer = lpUnit->GetOwner();
    if (!pPlayer)
        return;
    CPlayer::EPathType type = (CPlayer::EPathType)pathType;
    CPlayer::EPathResult res = (CPlayer::EPathResult)pathResult;
    CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
    // show found path
    pPS->SendShowPath(lpUnit, move, ptDest, type, res, fpShowLeng);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::ShowFoundMovePath(SAIPath &move, bool bFound)
{
    if (logic.IsFollowing())
        return;
    CAIFullRoute full;
    if (!bFound)
    {
        ShowFoundPath(full, move.ptDestination, CPlayer::PPT_NOPATH, CPlayer::PPR_FAILED);
        if (!logic.GetPlayerLeader())
            InformFailure(ACK_HERO_REFUSE_PATH_NOT_FOUND);
    }
    else
    {
        move.GetWholeRoute(full);
        float fpLength = distanceXY2(lpUnit->GetCP(), move.ptDestination);
        if (distanceXY2(move.GetFinalPoint(), move.ptDestination) <= (std::max)(sqr(.001f), sqr(0.1f) * fpLength))
        {
            ShowFoundPath(full, move.ptDestination, CPlayer::PPT_MOVE, CPlayer::PPR_OK);
            if (!logic.GetPlayerLeader())
                InformFailure(ACK_HERO_ACK_MOVE);
        }
        else
        {
            ShowFoundPath(full, move.ptDestination, CPlayer::PPT_MOVE, CPlayer::PPR_FAILED);
            if (!logic.GetPlayerLeader())
                InformFailure(ACK_HERO_REFUSE_PATH_NOT_FOUND);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Attack execution code
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::UseTrap()
{
    int nQItemIdx = curCommand.nQSpellNum;
    CItemQuick *pTrap = lpUnit->GetQItem(nQItemIdx);
    if (!pTrap || pTrap->GetQuickItemType() != IDB_TRAP)
    {
        ASSERT(0);
        return;
    }
    pTrap->AddRef();
    lpUnit->RemoveQItem(nQItemIdx);
    theWorldServer.SetupTrap(pTrap, lpUnit);
    pTrap->Release();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::UsePotion(int nQSpellNum, CMapObject *pTarget, const CPlot3D &ptCastTo)
{
    CSpell *pSpell;
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CItemQuick *pItem = lpUnit->GetQItem(nQSpellNum & 0x7fffffff);
    ASSERT(pItem && pItem->GetCount() > 0);
    if (!pItem || pItem->GetCount() == 0)
        return;
    pSpell = GetSpell(nQSpellNum);
    ASSERT(pSpell);
    if (!pSpell)
        return;
    if (CModPackage *mod = ModRuntime::Current())
        if (mod->CombatSetting("usePotionAcks"))
        {
            InformFailure(ACK_HERO_ACK_USE_POTION);
            theWorld.ReceiveDamage(lpUnit->GetID(), 0, HLDAMAGE_POTION);
        }
    pWS->CastSpell(pSpell, lpUnit, lpUnit->GetCP(), pTarget, ptCastTo);
    // reduce items count and remove if everything is spent
    pWS->ReduceQItemCount(lpUnit, pItem);
}
/////////////////////////////////////////////////////////////////////////////////////
namespace {
class ScopedRandomSpellPower {
public:
    ScopedRandomSpellPower(CSpell *spell, float limit)
        : spell_(spell), original_(spell->GetPower()), active_(limit >= 0.0f)
    {
        if (active_)
        {
            const float roll = float(rand());
            spell_->SetPower(original_ +
                (roll - 16384.0f) * original_ * limit / 16384.0f);
        }
    }
    ~ScopedRandomSpellPower()
    {
        if (active_)
            spell_->SetPower(original_);
    }
private:
    CSpell *spell_;
    float original_;
    bool active_;
};
}

void CUnitServer::UseSpell()
{
    CPlot3D ptCastTo;
    SUnitCommand &cmd = curCommand;
    CMapObjectServer *pMOS = GetMapObjectServer();
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CSpell *pSpell;
    CItem *pItem = 0;
    int nQSpellNum = cmd.nQSpellNum;

    // take spell either from wand or from spell
    if (nQSpellNum & 0x80000000)
    {
        pItem = GetSpellQuickItem(nQSpellNum);
        if (!pItem)
            return;
        pSpell = pItem->GetSpell(0);
    }
    else
        pSpell = lpUnit->GetQSpell(nQSpellNum);
    if (!pSpell)
        return;
    if(ModRuntime::Current()&&!pItem&&ModTactical::ObsoleteHeal(this,pSpell,cmd.pTarget)){cmd.nQSpellNum=-1;return;}
    // determine cast target
    if (cmd.pTarget.IsEmpty()) // cmd == UC_CASTPOINT )
        ptCastTo = cmd.ptTarget;
    else
        ptCastTo = cmd.pTarget->GetTargetCP();
    // Mana may have changed while the casting animation was running.
    if(ModRuntime::Current()&&!IsCastPossible(cmd)){cmd.nQSpellNum=-1;return;}
    // perform cast
    ASSERT(IsCastPossible(cmd)); // nQSpellNum, ptCastTo, cmd.cmd == UC_CASTPOINT ) )
    bool bDoCast = true;
    if (pItem)
    {
        if (pItem->GetMP() < pSpell->GetManaCost())
            bDoCast = false;
        else
            SpendItemMana(pItem, pSpell->GetManaCost());
    }
    else
    {
        auto* mod = ModRuntime::Current();
        ModSkills::Config manaSkill;
        const bool naturalMana = mod && mod->SpellAddon().Feature18() == 4 &&
            !mod->SkillConfig(pSpell,manaSkill);
        if (naturalMana ? rpgUnitStats.rpgNAWWeaponType != RPGWST_MAGIC
                        : (lpUnit->IsHero() || mod))
        {
            const float cost = pSpell->GetManaCost();
            const float multiplier = naturalMana
                ? rpgUnitStats.GetSpellAddonManaBonusMultiplier()
                : rpgUnitStats.GetManaBonusMultiplier();
            const float change = -cost * multiplier;
            if (naturalMana && !std::isfinite(change))
            {
                // Retail writes NaN for 0 * infinity. Preserve MP by the R36
                // decision, and bound the passive journal event to once / 5s.
                static bool logged = false;
                static DWORD lastLog = 0;
                const DWORD now = GetTickCount();
                if (!logged || DWORD(now - lastLog) >= 5000)
                {
                    logged = true;
                    lastLog = now;
                    GameJournal::Event("SPELL", "mana_nonfinite",
                        "unit=%d cost=%g mult=%g", lpUnit->GetID(),
                        double(cost), double(multiplier));
                }
            }
            else
            {
                GetMapObjectServer()->ChangeCurrentMP(change);
                AddUpdateMask(CMapObject::VLD_MASK_STATS);
            }
        }
    }
    if (bDoCast)
    {
        {
            const CModPackage *mod = ModRuntime::Current();
            ScopedRandomSpellPower randomPower(
                pSpell, mod ? mod->MagicRandomLimit() : -1.0f);
            pWS->CastSpell(pSpell, lpUnit, lpUnit->GetCP(), cmd.pTarget, ptCastTo);
        }
        // produce some sound so enemies should know that you are doing something
        if (pSpell->GetMagicSchool() != RPGMS_SENSES)
            pWS->aiMap.marks.ProduceSoundFootstep(this, GetFSSoundNativeDetect() * AI_ATTACK_SOUND);
    }
    // mark spell as casted
    cmd.nQSpellNum = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::UseSkill(CMapObject *pTarget)
{
    if (!pTarget)
        return;
    SUnitCommand &cmd = curCommand;
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    GameJournal::ObserveObject(pTarget, cmd.IsLoot()?"take":cmd.nQSpellNum==RPGMSKL_STEALING?"steal":"use");
    if (cmd.IsLoot())
    {
        if (pTarget->WhoAmI() != HPC_UNIT)
            return;
        if (lpUnit->GetOwner())
            theWorldServer.HooverUnit((CUnit *)pTarget, lpUnit->GetOwner());
        theWorldServer.RemoveMapObject(pTarget->GetID());
        // note, here pTarget does not exist anymore
        const auto *lootMod = ModRuntime::Current();
        if (!lootMod || !lootMod->CombatSetting("JMVSteals"))
            pWS->aiMap.marks.ProduceSoundFootstep(this, GetFSSoundNativeDetect() * AI_LOOT_SOUND);
        return;
    }
    // check chances to steal something
    float fpOurSkill, fpTargetSkill;
    ERPGManualSkills eSkill;
    if (cmd.nQSpellNum == RPGMSKL_STEALING)
        eSkill = RPGMSKL_SCIENCE; // RPGMSKL_STEALING;
    else if (cmd.nQSpellNum == RPGMSKL_SCIENCE)
        eSkill = RPGMSKL_SCIENCE;
    else if (cmd.nQSpellNum == RPGMSKL_TAME)
        return; // eSkill = RPGMSKL_TAME;
    else
        return;
    CUnit *pTargetUnit = 0;
    if (pTarget->WhoAmI() == HPC_UNIT)
        pTargetUnit = (CUnit *)pTarget;
    CModPackage *modPackage = ModRuntime::Current();
    const bool addonSteals = modPackage && modPackage->CombatSetting("JMVSteals");
    const bool onlyQuest = addonSteals && pTargetUnit &&
        pTargetUnit->GetOwner() == 0 && pTargetUnit->GetQuestItemsNum() > 0;
    const SRPGUserUnitStats& userStats = *GetUnit()->GetUserUnitStats();
    fpOurSkill = addonSteals ? float(ModStealing::ActorSkill(int(userStats.cDexterity),int(userStats.cManualSkills[eSkill])))
                            : rpgUnitStats.GetManualSkillLevel(eSkill, GetUnit());
    fpTargetSkill = 0;
    if (pTargetUnit)
        fpTargetSkill = ((CUnitServer *)pTargetUnit->GetUnitSpecific())
                            ->rpgUnitStats.GetManualSkillLevel(eSkill, pTargetUnit);
    aiLog << "Unit" << lpUnit->GetID() << " is using skill ";
    switch (cmd.nQSpellNum) // use this var instead of eSkill due to stealing=>science redefinition
    {
    case RPGMSKL_STEALING:
        aiLog << "stealing";
        break;
    case RPGMSKL_SCIENCE:
        aiLog << "science";
        break;
    case RPGMSKL_TAME:
        aiLog << "tame";
        break;
    default:
        aiLog << "unknown";
        break;
    }
    aiLog << " his_skill = " << fpOurSkill << " target_skill = " << fpTargetSkill << endl;

    if (cmd.nQSpellNum != RPGMSKL_SCIENCE && !onlyQuest && fpTargetSkill >= fpOurSkill)
    {
        // skill failed - not a fact in case
        pWS->fof.MakeDamage(GetUnit(), pTargetUnit, 10);
    }
    else
    {
        // skill succeded
        switch (
            cmd.nQSpellNum) // use this var instead of eSkill due to stealing=>science redefinition
        {
        case RPGMSKL_STEALING:
            if (pTargetUnit && pTargetUnit->GetOwner() == 0)
            {
                if (addonSteals)
                {
                    CPlayer* owner = GetUnit()->GetOwner();
                    CPlayerServer* playerServer = owner ? (CPlayerServer*)owner->GetPlayerSpecific() : nullptr;
                    if (!ModStealing::CanSteal(playerServer!=nullptr,playerServer&&playerServer->dangerState==WDS_COMBAT))
                    {
                        static unsigned refusalNotes=0;
                        if(refusalNotes++<16)EngineDiagnostics::Event("P02 steal refused actor=%d target=%d player=%d combat=%d",GetUnit()->GetID(),pTargetUnit->GetID(),playerServer?1:0,playerServer&&playerServer->dangerState==WDS_COMBAT?1:0);
                        InformFailure(ACK_HERO_REFUSE_NO_WAY_TO_ATTACK);
                        break;
                    }
                    // Transfer quest inventory separately, as the addon does.
                    if (pTargetUnit->GetQuestItemsNum() > 0)
                    {
                        if (CPlayer *pPlayer = GetUnit()->GetOwner())
                        {
                            CList<CItem *, CItem *> allItems;
                            CList<CItem *, CItem *> newItems;
                            CList<CSpell *, CSpell *> newSpells;
                            int ignoredMoney = 0;
                            pWS->pHoover->HooverAddon(
                                pWS, pTargetUnit, allItems, newItems,
                                newSpells, ignoredMoney,
                                GetRPGStats()->GetManualSkillLevel(
                                    RPGMSKL_SCIENCE, GetUnit()),
                                GetRPGStats()->psDexterity.fpEffectiveValue,
                                true);
                            CList<CItem *, CItem *> questItems;
                            for (POSITION pos = allItems.GetHeadPosition(); pos;)
                            {
                                POSITION current = pos;
                                CItem *item = allItems.GetNext(pos);
                                if (item && item->WhoAmI() == HPC_ITEM_QUEST)
                                {
                                    questItems.AddTail(item);
                                    allItems.RemoveAt(current);
                                }
                            }
                            if (!questItems.IsEmpty())
                            {
                                static unsigned questNotes=0;
                                if(questNotes++<16)EngineDiagnostics::Event("P02 quest transfer actor=%d target=%d count=%d remaining=%d skill=%g",GetUnit()->GetID(),pTargetUnit->GetID(),int(questItems.GetCount()),pTargetUnit->GetQuestItemsNum(),fpOurSkill);
                                pPlayer->AddItemsToHoover(questItems);
                            }
                            // Hoover already retained/detached every quest. No second random removal:
                            // that would steal an unrelated quick item after the quest list is empty.
                            for (POSITION pos = questItems.GetHeadPosition(); pos;)
                                questItems.GetNext(pos)->Release();
                            for (POSITION pos = allItems.GetHeadPosition(); pos;)
                                allItems.GetNext(pos)->Release();
                        }
                    }
                    bool bSuccess = false;
                    if (!onlyQuest && GetUnit()->GetOwner())
                        bSuccess = pWS->HooverUnitAddon(
                            pTargetUnit, GetUnit()->GetOwner(), this, false);
                    if (!bSuccess && !onlyQuest && GetUnit()->GetOwner())
                        InformFailure(ACK_HERO_NOTIFY_STEAL_EMPTY);
                    break;
                }

                // stealing success - some item transmission
                if (CItem *pSubj = pTargetUnit->RemoveRandomItem())
                {
                    aiLog << "Item has been stolen from" << endl;
                    if (CPlayer *pPlayer = GetUnit()->GetOwner())
                    {
                        CList<CItem *, CItem *> items;
                        items.AddHead(pSubj);
                        pPlayer->AddItemsToHoover(items);
                    }
                    pSubj->Release();
                }
                else
                {
                    bool bSuccess = false;
                    if (GetUnit()->GetOwner())
                    { CModPackage *mod=ModRuntime::Current(); if(mod&&mod->CombatSetting("JMVSteals"))bSuccess=pWS->HooverUnitAddon(pTargetUnit,GetUnit()->GetOwner(),this,false); else bSuccess=pWS->HooverUnit(pTargetUnit,GetUnit()->GetOwner()); }
                    if (!bSuccess && GetUnit()->GetOwner())
                        InformFailure(ACK_HERO_NOTIFY_STEAL_EMPTY);
                }
            }
            else
                ASSERT(0);
            break;
        case RPGMSKL_SCIENCE: {
            SRPGScienceStats tool;
            // item ID of tool is stored at nQSpellNum
            CItem *pTool = (CItem *)pWS->GetItemManager()->Find(cmd.dwParam);
            tool.dwKeyNumber = 0;
            tool.dwTools = RPGTOOL_HAND;
            tool.dwSkill = 0;
            if (pTool && pTool->IsTool())
                tool = pTool->GetTool();
            int quickScience = 0;
            if (addonSteals)
                for (DWORD slot = 0; slot < QI_MAX_ITEMS; ++slot)
                    if (CItemQuick *item = lpUnit->GetQItem(slot))
                        quickScience = Max(quickScience, int(item->GetSkillModifier(RPGMSKL_SCIENCE)));
            tool.dwSkill += Float2Int(fpOurSkill) + quickScience;
            const int journalActor = lpUnit->GetID(), journalTarget = pTarget->GetID();
            const bool journalApplied=pTarget->ApplyScience(tool, GetUnit());
            GameJournal::Event("COMBAT", "skill", "actor=%d target=%d skill=science result=%d", journalActor, journalTarget, journalApplied);
            if (!journalApplied)
                InformFailure(ACK_HERO_REFUSE_SCIENCE_FAILED);
        }
        break;
        case RPGMSKL_TAME:
            if (pTargetUnit)
            {
                CUnitServer *pTargetServer = (CUnitServer *)pTargetUnit->GetUnitSpecific();
                if (!pTargetServer->Tame(lpUnit, fpOurSkill))
                    InformFailure(ACK_HERO_REFUSE_TAME);
            }
            else
                ASSERT(0);
            break;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// create damage directly or create an arrow
void CUnitServer::UseAttack(CMapObject *pTarget, SRPGAttackPortion &attackPortion,bool applyWeaponSpell,int projectileIndex,int projectileCount,const char* activeSkillCode,AttackCallbackOrigin origin)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    auto*mod=ModRuntime::Current();
    auto weapon=mod?mod->AttackWeapon(attackPortion):std::shared_ptr<CItemWeapon>();
    CItemWeapon*usedWeapon=weapon?weapon.get():lpUnit->GetCurrentQWeapon();
    const bool spellLeftHand=mod&&mod->AttackIsLeft(attackPortion);
    GameJournal::SpellObservation journalSpell;
    CPlot3D journalPosition;
    if (activeSkillCode) {
        journalSpell = GameJournal::CaptureSpell(lpUnit, pTarget);
        journalPosition = pTarget->GetCP();
    }
    // Keep the launch weapon alive through delivery and its trigger spell.
    struct Cleanup{CModPackage*mod;SRPGAttackPortion&ap;~Cleanup(){if(mod)mod->ForgetAttack(ap);}} cleanup{mod,attackPortion};
    // Keep JMV staff projectiles; the retail/ASI path uses an arrow figure.
    const bool staffProjectile = mod && mod->CombatSetting("JMVAttacks") && usedWeapon &&
        (usedWeapon->GetWeaponType() == RPGWST_HANDS3 || usedWeapon->GetWeaponType() == RPGWST_HANDS7);
    bool bow = IsEquippedRangedWeapon(usedWeapon) && !staffProjectile;
    if (bow)
    {
        CEffectArrowFigure *pArrow;
        pArrow = (CEffectArrowFigure *)(pWS->GetEffectManager())
                     ->CreateData(ETypeIDs(HPC_EFFECTARROWFIGURE));
        if (!pArrow)
            return;
        CPlot3D launch=GetUnit()->GetUnitHeadPosition();if(projectileCount>1)launch.x+=projectileIndex==0?-.08f:.08f;
        pArrow->SetCP(launch);
        pArrow->SetTarget(pTarget->GetID());
        pArrow->nAttackerID = GetUnit()->GetID();
        pArrow->bNewArrow = true;
        pArrow->attackPortion = attackPortion;
        if(auto*mod=ModRuntime::Current())mod->MoveAttack(attackPortion,pArrow->attackPortion);
        pWS->AddEffect(pArrow);
        if(projectileIndex==0&&activeSkillCode){
            const char* resource=!_stricmp(activeSkillCode,"doubleshot")?"skills\\doubleshot\\1.wav":
                !_stricmp(activeSkillCode,"preciseshot")?"skills\\preciseshot\\1.wav":nullptr;
            if(resource)if(auto*sound=theWorld.GetSoundManager())sound->BeginFX(resource,8000,launch,5,25,FALSE);
        }
    }
    else if(mod&&mod->CombatSetting("JMVAttacks")&&usedWeapon&&
            (usedWeapon->GetWeaponType()==RPGWST_HANDS3||usedWeapon->GetWeaponType()==RPGWST_HANDS7))
    {
        auto*arrow=static_cast<CEffectArrow*>(pWS->GetEffectManager()->CreateData(ETypeIDs(HPC_EFFECTARROW)));
        if(!arrow)return;
        bool left=mod->AttackIsLeft(attackPortion);
        CUnit*model=theWorld.GetMapObjectManager()?theWorld.GetUnit(lpUnit->GetID()):nullptr;
        arrow->SetCP((model?model:lpUnit)->GetWeaponCastPoint(usedWeapon,left));
        arrow->SetTarget(pTarget->GetID());arrow->nAttackerID=lpUnit->GetID();
        arrow->color=CPlot3D(1,1,1);arrow->fpRange=1;arrow->fpScale=.25f;
        auto type=CItemWeapon::GetAttackType(usedWeapon->GetSubtypeID());
        arrow->typeID=type==RPGAT_TERMICAL?PTID_STAFFARROWFIRE:type==RPGAT_ELECTRICAL?PTID_STAFFARROWLIGHT:PTID_STAFFARROWWATER;
        arrow->szSoundName="magic\\Firearrow\\circle.wav";
        arrow->SetWeaponAttack(attackPortion);
        pWS->AddEffect(arrow);
    }
    else if(mod && mod->CombatSetting("JMVAttacks") &&
            ModMonsterProjectiles::Classify(lpUnit->GetPrototype()->szName).ranged)
    {
        const auto kind=ModMonsterProjectiles::Classify(lpUnit->GetPrototype()->szName);
        auto*arrow=static_cast<CEffectArrow*>(pWS->GetEffectManager()->CreateData(ETypeIDs(HPC_EFFECTARROW)));
        if(!arrow)return;
        CUnit*model=theWorld.GetMapObjectManager()?theWorld.GetUnit(lpUnit->GetID()):nullptr;
        arrow->SetCP((model?model:lpUnit)->GetWeaponCastPoint(nullptr,false));
        arrow->SetTarget(pTarget->GetID());arrow->nAttackerID=lpUnit->GetID();
        SAttackParams params;lpUnit->GetAttackParams(params);
        arrow->typeID=kind.acid?PTID_STAFFARROWACID:
            params.rpgAttackType==RPGAT_TERMICAL?PTID_STAFFARROWFIRE:
            params.rpgAttackType==RPGAT_ELECTRICAL?PTID_STAFFARROWLIGHT:PTID_STAFFARROWWATER;
        arrow->color=CPlot3D(1,1,1);arrow->fpRange=kind.acid?.01f:1.f;arrow->fpScale=.25f;
        arrow->szSoundName="magic\\Firearrow\\circle.wav";
        // Existing engine-owned payload is retained through delivery, not a stack pointer.
        arrow->SetWeaponAttack(attackPortion);
        pWS->AddEffect(arrow);
        static unsigned notes=0;
        if(notes++<48)EngineDiagnostics::Event("Monster projectile attacker=%d target=%d prototype=%s type=%d acid=%d",
            lpUnit->GetID(),pTarget->GetID(),lpUnit->GetPrototype()->szName,int(arrow->typeID),kind.acid?1:0);
    }
    else if (origin != AttackCallbackOrigin::Original)
    {
        CMapObjectServer *victimStats = pTarget->WhoAmI() == HPC_UNIT ?
            static_cast<CUnitServer *>(static_cast<CUnit *>(pTarget)->GetUnitSpecific())->GetMapObjectServer() : nullptr;
        const float hpBefore = victimStats ? victimStats->GetCurrentHP() : -1;
        pTarget->ProcessAttackPortion(&attackPortion, GetUnit(), GetUnit());
        TraceMeleeReach(2, pTarget, usedWeapon, -1, hpBefore,
                        victimStats ? victimStats->GetCurrentHP() : -1);
    }
    if(activeSkillCode) GameJournal::ObserveSpell(journalSpell, activeSkillCode, -1, 1, journalPosition.x, journalPosition.y, journalPosition.z);
    if(origin != AttackCallbackOrigin::Original && applyWeaponSpell)UseCurrentWeaponSpell(pTarget,usedWeapon,spellLeftHand);
    // produce some sound so enemies should know that you are doing something
    if (attackPortion.fBackStabMultiplier == 1) // not a backstab
        pWS->aiMap.marks.ProduceSoundFootstep(this, GetFSSoundNativeDetect() * AI_ATTACK_SOUND);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::UseAttackStart(CMapObject *pTarget, ERPGHitLocations eAttackLocation,
                                 bool bCalledShot, bool leftHand)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    BOOL bBackstab = FALSE;
    //
    ASSERT(eAttackLocation < RPGHL_UNKNOWN);
    CModPackage *creationMod = ModRuntime::Current();
    if (creationMod && ModConditionalCombat::OrdinaryCreation(
            creationMod->CombatSetting("JMVShit")!=0, creationMod->CombatSetting("JMVAttacks")!=0,
            ModSkills::Slot(curCommand.dwParam)>=0, leftHand))
    {
        CPlot3D toTarget = pTarget->GetCP() - GetUnit()->GetCP();
        float length = fabs(toTarget);
        if (length > 0) toTarget /= length;
        bBackstab = ModConditionalCombat::RetailRear(GetUnit()->GetOwner()!=nullptr,
            IsCurrentWeaponABow(), pTarget->GetYAxis()*toTarget);
        if (pTarget->WhoAmI()==HPC_UNIT &&
            ((CUnitServer *)((CUnit *)pTarget)->GetUnitSpecific())->IsAggressive()) bBackstab=FALSE;
        CreateConditionalRetailAttackPortion(level1AP,bBackstab,eAttackLocation,bCalledShot);
        return;
    }
    bBackstab = GetUnit()->IsBackstab(pTarget);
    // backstab по агрессивным не проходит
    if (pTarget->WhoAmI() == HPC_UNIT &&
        ((CUnitServer *)((CUnit *)pTarget)->GetUnitSpecific())->IsAggressive())
        bBackstab = FALSE;
    // check if we are using a bow then create arrow else process damage directly
    if (IsCurrentWeaponABow())
    {
        CModPackage *mod = ModRuntime::Current();
        const BOOL rangedBackstab = mod &&
            (mod->CombatSetting("JMVRangedBackstab") || mod->CombatSetting("JMV136"));
        CreateCurrentWeaponAttackPortion(level1AP, rangedBackstab ? bBackstab : FALSE,
                                         eAttackLocation, bCalledShot, leftHand);
    }
    else
        CreateCurrentWeaponAttackPortion(level1AP, bBackstab, eAttackLocation, bCalledShot,leftHand);
}
/////////////////////////////////////////////////////////////////////////////////////
// create attack portion with current weapon.
// take in account skills in weapon proficiency class
BOOL CUnitServer::CreateConditionalRetailAttackPortion(SRPGAttackPortion &ap, BOOL bBackstab,
                                                       ERPGHitLocations eAttackLocation, bool bCalledShot)
{
    auto *mod=ModRuntime::Current();
    const bool personage=IsIDofPersonage(lpUnit->GetID());
    const int slot=int(lpUnit->GetCurrentWeaponSlotIndex());
    CItemWeapon *weapon=slot>=0&&slot<QI_MAX_WEAPONS?lpUnit->GetQWeapon(slot):nullptr;
    ModConditionalCombat::CreationBackend b;
    b.clear=[&](){mod->ForgetAttack(ap);ap.Clear();};
    b.native=[&](){
        memcpy(ap.fpAttacks,rpgUnitStats.fpNAWAttacks,sizeof(float)*RPG_NUM_ATTACK_TYPES);
        ap.nSkill=0;
        ap.fpToHit=rpgUnitStats.fpNAWToHit*(personage?1.f:theAIGeneralSettings.fAttackLevels[theSettings.nDifficultyLevel]);
        float jitter=rpgUnitStats.fpNAWDamageRnd?float(random.Get(MINT(rpgUnitStats.fpNAWDamageRnd*100.f)))*.01f:0.f;
        ap.fpDamage=rpgUnitStats.fpNAWDamageMin+jitter;
    };
    b.weapon=[&](){
        weapon->GetDamageDistribution(ap.fpAttacks);ap.nSkill=weapon->GetProficiencyClass();
        ap.fpToHit=ModConditionalCombat::WeaponToHit(Dice(RPG_CONST_X),RPG_CONST_X);
        ap.fpDamage=weapon->GetRandomDamage();
    };
    b.modified=[&](){ap.fpDamage=mod->ConditionalRetailDamage(rpgUnitStats,*lpUnit,ap.fpDamage);};
    b.metadata=[&](){
        ap.hl=eAttackLocation;ap.bAreaEffect=FALSE;
        ap.fBackStabMultiplier=bBackstab?rpgUnitStats.GetBackstabDamageMultiplier(GetUnit()):1.f;
        if(lpUnit->GetOwner()&&lpUnit->GetOwner()->bGod>=2)ap.MakeDeadlyAttack();
        ap.bCalledShot=bCalledShot;
    };
    ModConditionalCombat::CreateRetail(ModConditionalCombat::RetailWeapon(personage,slot,weapon!=nullptr),b);
    return TRUE;
}

BOOL CUnitServer::CreateCurrentWeaponAttackPortion(SRPGAttackPortion &ap, BOOL bBackstab,
                                                   ERPGHitLocations eAttackLocation,
                                                   bool bCalledShot, bool leftHand)
{
    bool bHero = IsIDofPersonage(lpUnit->GetID());
    float fpDamageRandom = 0;
    auto*mod=ModRuntime::Current();float roll=mod?float(ModNativeCrtRandom::Random(100)):0;
    int weaponSlot=mod?mod->AttackSlot(*lpUnit,leftHand):int(lpUnit->dwCurrentWeapon);
    CItemWeapon*selectedWeapon=weaponSlot>=0&&weaponSlot<QI_MAX_WEAPONS?lpUnit->GetQWeapon(weaponSlot):nullptr;
    if(mod){mod->ForgetAttack(ap);if(leftHand)bBackstab=FALSE;}
    if(mod&&bBackstab){auto*w=lpUnit->GetCurrentQWeapon();if(mod->CombatSetting("JMVDaggerCritical"))roll=w&&w->GetWeaponType()==RPGWST_DAGGER?-2.f:0.f;else if(mod->CombatSetting("JMV136"))roll=0;}

    ap.Clear();

    if ((!mod && !bHero) || !selectedWeapon)
    {
        // create attack portion with native weapon
        memcpy(ap.fpAttacks, rpgUnitStats.fpNAWAttacks, sizeof(float) * RPG_NUM_ATTACK_TYPES);
        ap.nSkill = 0;
        ap.fpToHit = rpgUnitStats.fpNAWToHit *
                     (bHero ? 1.0f
                            : theAIGeneralSettings.fAttackLevels[theSettings.nDifficultyLevel]);
        if (rpgUnitStats.fpNAWDamageRnd)
            fpDamageRandom = float(random.Get(MINT(rpgUnitStats.fpNAWDamageRnd * 100.0f))) * 0.01f;
        ap.fpDamage = rpgUnitStats.fpNAWDamageMin + fpDamageRandom;
        // ap.hl = ERPGHitLocations( Random(RPG_NUM_HIT_LOCATIONS) );
    }
    else
    {
        CItemWeapon *lpWeapon = selectedWeapon;

        lpWeapon->GetDamageDistribution(ap.fpAttacks);
        ap.nSkill = lpWeapon->GetProficiencyClass();
        ap.fpToHit = 2 * (mod ? ModNativeCrtRandom::Dice(RPG_CONST_X) : Dice(RPG_CONST_X)) - (RPG_CONST_X + 1);
        ap.fpDamage = lpWeapon->GetRandomDamage();
        if(mod){int leftSlot=mod->AttackSlot(*lpUnit,true);float lo=mod->ModifiedDamage(rpgUnitStats,*lpUnit,lpWeapon->GetDamageMin(),roll,leftSlot,leftHand);float hi=mod->ModifiedDamage(rpgUnitStats,*lpUnit,lpWeapon->GetDamageMax(),roll,leftSlot,leftHand);ap.fpDamage=random.GetFloat(lo,hi);}
        // ap.hl = ERPGHitLocations( Random(RPG_NUM_HIT_LOCATIONS) );
    }
    //
    if(mod){if(!selectedWeapon)ap.fpDamage=mod->ModifiedDamage(rpgUnitStats,*lpUnit,ap.fpDamage,roll);}
    else ap.fpDamage = rpgUnitStats.GetModifiedDamage(lpUnit, ap.fpDamage);
    //
    ap.hl = eAttackLocation;
    ap.bAreaEffect = FALSE;
    if (bBackstab)
        ap.fBackStabMultiplier = rpgUnitStats.GetBackstabDamageMultiplier(GetUnit());
    else
        ap.fBackStabMultiplier = 1.0f;

    if (lpUnit->GetOwner() && lpUnit->GetOwner()->bGod >= 2)
        ap.MakeDeadlyAttack();
    ap.bCalledShot = bCalledShot;
    if(mod&&mod->CombatSetting("JMVAttacks"))mod->RememberAttack(rpgUnitStats,*lpUnit,ap,roll,leftHand);

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::UseCurrentWeaponSpell(CMapObject *pTarget, CItemWeapon *attackWeapon, bool leftHand)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CItemWeapon *pWeapon;
    CSpell *pSpell;
    pWeapon = attackWeapon?attackWeapon:lpUnit->GetCurrentQWeapon();
    if (!pWeapon)
        return;
    pSpell = pWeapon->GetSpell(0);
    if (pSpell && pSpell->IsItemTrigger() && pWeapon->GetMP() >= pSpell->GetManaCost())
    {
        SpendItemMana(pWeapon, pSpell->GetManaCost());
        static unsigned triggerNotes=0;
        const bool self=ModItemSpells::WeaponHealingTargetsSelf(pSpell->GetPrototype()->szName);
        if(triggerNotes++<32)EngineDiagnostics::Event("P01 weapon spell item=%d spell=%u left=%d self=%d cost=%g",pWeapon->GetID(),pSpell->GetSubtypeID()>>16,leftHand?1:0,self?1:0,pSpell->GetManaCost());
        if(self)
            theWorldServer.CastSpell(pSpell, GetUnit(), GetUnit()->GetCP(), GetUnit(), GetUnit()->GetCP());
        else
            theWorldServer.CastSpell(pSpell, GetUnit(), GetUnit()->GetWeaponCastPoint(pWeapon,leftHand),
                                     pTarget, pTarget->GetCP());
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::UseArmorSpells(bool bBeforeAttack)
{
    auto useItemSpell = [&](CItem* item, bool shield)
    {
        CSpell* spell = item->GetSpell(0);
        if (!spell || !spell->IsItemTrigger()) return;
        const int spellID = spell->GetSubtypeID() >> 16;
        const int filterID = shield ? spellID : BYTE(spellID);
        const bool suppressed = ModItemSpells::Suppressed(filterID, spell->GetPower(), shield,
            lpUnit->spellEffects.GetSize(), [&](int i)->const CSpellEffect& { return lpUnit->spellEffects[i]; });
        static unsigned suppressedNotes=0;
        if(suppressed&&suppressedNotes++<16)EngineDiagnostics::Event("P01 existing effect skips item=%d spell=%d shield=%d power=%g",item->GetID(),filterID,shield?1:0,spell->GetPower());
        if (!ModItemSpells::CanTrigger(true, item->GetMP(), spell->GetManaCost(), suppressed)) return;
        auto* mod = ModRuntime::Current();
        const int healAfterHit = mod ? mod->CombatSetting("JMVHealAfterHit") : 0;
        if (!ModItemSpells::TriggerPhase(shield?spellID:BYTE(spellID), bBeforeAttack, shield, healAfterHit)) return;
        static unsigned triggerNotes=0;
        if(triggerNotes++<32)EngineDiagnostics::Event("P01 armor/shield spell item=%d spell=%d shield=%d before=%d mana=%g cost=%g",item->GetID(),spellID,shield?1:0,bBeforeAttack?1:0,item->GetMP(),spell->GetManaCost());
        SpendItemMana(item, spell->GetManaCost());
        theWorldServer.CastSpell(spell, GetUnit(), GetUnit()->GetCP(), GetUnit(), GetUnit()->GetCP());
    };
    CItemWeapon* current = lpUnit->GetCurrentQWeapon();
    const int leftSlot = current ? ModStats::LeftHand(*lpUnit) : -1;
    // Native shield slots precede armor; only the equipped left shield is eligible.
    for (int i = 0; i < 4; ++i)
    {
        CItemWeapon* weapon = lpUnit->GetQWeapon(i);
        if (weapon && ModItemSpells::IsActiveShield(current!=nullptr,leftSlot,i,weapon->GetWeaponType()))
            useItemSpell(weapon,true);
    }
    for (int i = 0; i < RPG_NUM_ARMOR_TYPES; ++i)
        if (CItemArmor* armor = lpUnit->GetQArmor(i)) useItemSpell(armor,false);
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Different animations handling code
/////////////////////////////////////////////////////////////////////////////////////
// works for UC_ATTACK only
bool CUnitServer::CalcAttackDirection(SL1Command &cmd, SL0AnimationSeq &res, int &nHit,
                                      CMapObject *pMO)
{
    DWORD dwAttackFlags;
    if (pMO->WhoAmI() != HPC_UNIT) // this is non-unit map object
    {
        cmd.eAttackLocation = RPGHL_BODY;
        dwAttackFlags = ADB_MODIFIER_HDIRECTION1_MIDDLE | ADB_MODIFIER_VDIRECTION1_MIDDLE;
    }
    else // unit
    {
        SUnitBasicCmd atkCmd;
        atkCmd.MakeAttack(pMO);
        CUnit *pVictim = static_cast<CUnit *>(pMO);
        CPlot3D ptWe = lpUnit->GetCP();
        CPlot3D ptVictim = pVictim->GetTargetCP();
        float fVictimDistance = fabs(ptWe - ptVictim);
        float fAttackDistance = Min(GetHitDist(atkCmd), fVictimDistance);

        const SRPGUnitStats &rpgVictimUnitStats =
            static_cast<CUnitServer *>(pVictim->GetUnitSpecific())->rpgUnitStats;
        float fAttackerZ = ptWe.z + lpUnit->CalcZ(ptWe.x, ptWe.y);
        float fAttackerHeight = lpUnit->GetUnitHeight();
        float fVictimZ = ptVictim.z + pVictim->CalcZ(ptVictim.x, ptVictim.y);
        float fVictimHeight = pVictim->GetUnitHeight();
        ERPGHitLocations hl;
        ERPGHitLocations targetHL = (ERPGHitLocations)curCommand.nQSpellNum;
        DWORD dir = ADB_MODIFIER_VDIRECTION1_MIDDLE;
        // выбираем HitLocation, куда будет нанесён удар
        // по новым веяниям (24.05.2000) он (HL) не должен зависеть от взаимного расположения юнитов
        if (targetHL == RPGHL_ALL) // если targetHL не определён, то выбираем рандомно
        {
            ERPGHitLocations hls[20] = { RPGHL_HEAD,  RPGHL_HEAD,  RPGHL_BODY,  RPGHL_BODY,
                                         RPGHL_BODY,  RPGHL_BODY,  RPGHL_BODY,  RPGHL_BODY,
                                         RPGHL_LHAND, RPGHL_LHAND, RPGHL_LHAND, RPGHL_RHAND,
                                         RPGHL_RHAND, RPGHL_RHAND, RPGHL_LLEG,  RPGHL_LLEG,
                                         RPGHL_LLEG,  RPGHL_RLEG,  RPGHL_RLEG,  RPGHL_RLEG };
            ERPGHitLocations addonHls[20] = { RPGHL_HEAD,  RPGHL_HEAD,  RPGHL_BODY,  RPGHL_BODY,
                                         RPGHL_BODY,  RPGHL_BODY,  RPGHL_BODY,  RPGHL_BODY,
                                         RPGHL_BODY,  RPGHL_LHAND, RPGHL_LHAND, RPGHL_RHAND,
                                         RPGHL_RHAND, RPGHL_LLEG,  RPGHL_LLEG,  RPGHL_LLEG,
                                         RPGHL_LLEG,  RPGHL_RLEG,  RPGHL_RLEG,  RPGHL_RLEG };
            const auto *hitMod = ModRuntime::Current();
            const ERPGHitLocations *locations = hitMod && hitMod->CombatSetting("JMVAttacks") ? addonHls : hls;
            DWORD dwProbability = random.Get(0, 19);
            hl = rpgVictimUnitStats.GetNearestHitLocation(locations[dwProbability]);
        }
        else if ((targetHL >= 0) &&
                 (targetHL <
                  RPGHL_ALL)) // если targetHL определён, то берём его (с validation check)
            hl = targetHL;
        else // в случае непоняток бьём в тело
            hl = rpgVictimUnitStats.GetNearestHitLocation(RPGHL_BODY);
        // после того, как мы выбрали HL, выбираем направление анимации удара, исходя из hl
        float fTgValue = 0;
        if (fAttackerZ + fAttackerHeight < fVictimZ)
        {
            dir = ADB_MODIFIER_VDIRECTION1_UP;
            // tangent value to correct attack direction
            fTgValue = (fVictimZ - (fAttackerZ + fAttackerHeight)) / fAttackDistance;
        }
        else if (fVictimZ + fVictimHeight < fAttackerZ)
        {
            dir = ADB_MODIFIER_VDIRECTION1_DOWN;
            // tangent value to correct attack direction
            fTgValue = (fAttackerZ - (fVictimZ + fVictimHeight)) / fAttackDistance;
        }
        else
        {
            // выбираем высоту удара, исходя из пересечения отрезков
            float fMax = Min(fAttackerZ + fAttackerHeight, fVictimZ + fVictimHeight);
            float fMin = Max(fAttackerZ, fVictimZ);
            float fValue = fMin;
            float fMin2, fMax2;
            switch (hl)
            {
            case RPGHL_LLEG:
            case RPGHL_RLEG:
                fMin2 = Max(fMin, fVictimZ);
                fMax2 = Min(fMax, fVictimZ + fVictimHeight * 0.4f);
                break;
            case RPGHL_LHAND:
            case RPGHL_RHAND:
            case RPGHL_BODY:
                fMin2 = Max(fMin, fVictimZ + fVictimHeight * 0.4f);
                fMax2 = Min(fMax, fVictimZ + fVictimHeight * 0.8f);
                break;
            case RPGHL_HEAD:
                fMin2 = Max(fMin, fVictimZ + fVictimHeight * 0.8f);
                fMax2 = Min(fMax, fVictimZ + fVictimHeight);
                break;
            default:
                ASSERT(0); // этого не должно случаться
            }
            if (MINT((fMax2 - fMin2) * 100.0f) > 0)
                fValue = fMin2 + random.Get(MINT((fMax2 - fMin2) * 100.0f) - 1) * 0.01f;
            else if (MINT((fMax - fMin) * 100.0f) > 0)
                fValue = fMin + random.Get(MINT((fMax - fMin) * 100.0f) - 1) * 0.01f;
            // attacker range (vertical attack direction modifier)
            if (IsValInRange<bool>(fValue, fAttackerZ,
                                   fAttackerZ + fAttackerHeight * 0.4f)) // range 0 - 0.4
                dir = ADB_MODIFIER_VDIRECTION1_DOWN;
            else if (IsValInRange<bool>(fValue, fAttackerZ + fAttackerHeight * 0.4f,
                                        fAttackerZ + fAttackerHeight * 0.8f)) // range 0.4 - 0.8
                dir = ADB_MODIFIER_VDIRECTION1_MIDDLE;
            else if (IsValInRange<bool>(fValue, fAttackerZ + fAttackerHeight * 0.8f,
                                        fAttackerZ + fAttackerHeight)) // range 0.8 - 1
                dir = ADB_MODIFIER_VDIRECTION1_UP;
            // tangent value to correct attack direction
            fTgValue = fabs(fValue - fAttackerZ + fAttackerHeight * 0.7f) / fAttackDistance;
        }
        // angle correction for ranged attack (attack distance > 5)
        if (fAttackDistance > 5)
        {
            if (fTgValue > 1.73205f)
                dir = ADB_MODIFIER_VDIRECTION1_UP;
            else if (fTgValue < -1.73205f)
                dir = ADB_MODIFIER_VDIRECTION1_DOWN;
            else
                dir = ADB_MODIFIER_VDIRECTION1_MIDDLE;
        }
        // выбираем горизонтальное направление удара
        dir |= ADB_MODIFIER_HDIRECTION1_MIDDLE;
        // 'long' and 'short' attacks for non-humans
        if (lpUnit->GetTypeID() != MOTID_HUMAN)
        {
            float fCheckHitDistance = GetCloseAttackDist(atkCmd);
            if (fVictimDistance < fCheckHitDistance - 0.15f)
                dir = ADB_MODIFIER_HDIRECTION1_MIDDLE | ADB_MODIFIER_VDIRECTION1_DOWN;
            else
                dir = ADB_MODIFIER_HDIRECTION1_MIDDLE | ADB_MODIFIER_VDIRECTION1_MIDDLE;
        }

        cmd.eAttackLocation = hl;
        dwAttackFlags = dir;
    }
    // assert for correct Hit Location
    ASSERT(cmd.eAttackLocation < RPGHL_UNKNOWN);
    //
    GetAttackAnimation(res, nHit, UC_ATTACK, dwAttackFlags);
    /*
    if ( IsCurrentWeaponABow() && !res.IsEmpty() )
    {
            int nCycle = GetAttackCycleTime( UC_ATTACK, lpUnit->GetCurrentQWeapon() );
            nCycle -= 10;
            if ( nCycle > res.animations[0].nLength )
            {
                    SL0Animation anim = res.animations[0];
                    res.Clear();
                    // make animation with freezing at the middle
                    int nPause = nCycle - anim.nLength;
                    nHit = Max( 2, nHit );
                    res.AddSimple( anim.bStart, nHit - 2 );
                    res.AddSimple( 0xffffffffu, nPause );
                    res.AddSimple( 0xffffffffu, anim.nLength - nHit + 2 );
                    res.SetSpeed( UAS_NORMAL_ );
                    res.animations[1].animationSpeed = UAS_FROZEN;
                    nHit += nPause;
            }
    }
    */

    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// Animation flags related functions
DWORD CUnitServer::GetAnimationWeaponMask()
{
    CItemWeapon *pWeapon = lpUnit->GetCurrentQWeapon();
    if (pWeapon)
        return (1 << pWeapon->GetWeaponType());
    else if (rpgUnitStats.rpgNAWWeaponType < RPGWST_UNKNOWN)
        return (1 << rpgUnitStats.rpgNAWWeaponType);

    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// only humans have attack/neutral (i.e. aggressive) states
DWORD CUnitServer::GetPoseAnimationMask(const SL1Pose &pose)
{
    switch (pose.pose)
    {
    case UP_LAY:
        return ADB_STATE_LIE;
    case UP_CRAWL:
        return ADB_STATE_WARRY;
    case UP_STAND:
        if (pose.bAggressive || (lpUnit->GetTypeID() != MOTID_HUMAN))
            return ADB_STATE_ATTACK;
        else
            return ADB_STATE_NEUTRAL;
    case UP_REST:
        return ADB_STATE_REST;
    }
    ASSERT(0);
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// uc_attack/uc_skill
void CUnitServer::GetAttackAnimation(SL0AnimationSeq &res, int &nHit, EUnitCommand cmd,
                                     DWORD dwAttackFlags)
{
    bool bAddPoseChange = false;
    EUnitPose actionPose = UP_CRAWL;
    const SADEntry *pEntry;
    DWORD dwAnimationFlags, dwWeaponFlags = 0;
    res.Clear();
    if (cmd == UC_SKILL)
    {
        switch (dwAttackFlags)
        {
        case RPGMSKL_TAME:
            dwAnimationFlags = ADB_MODIFIER_SPECIAL_1 * 19;
            bAddPoseChange = currentPose.pose != UP_CRAWL;
            break;
        case RPGMSKL_STEALING:
            dwAnimationFlags = ADB_MODIFIER_SPECIAL_1 * 20;
            bAddPoseChange = currentPose.pose != UP_CRAWL;
            break;
        case RPGMSKL_SCIENCE:
            dwAnimationFlags = ADB_MODIFIER_SPECIAL_1 * 21;
            bAddPoseChange = currentPose.pose != UP_CRAWL;
            break;
        case RPGMSKL_LOOT:
            if (currentPose.pose == UP_STAND)
            {
                if (IsCurrentWeaponABow())
                    dwAnimationFlags = ADB_MODIFIER_SPECIAL_1 * 52;
                else
                    dwAnimationFlags = ADB_MODIFIER_SPECIAL_1 * 48;
                bAddPoseChange = false;
            }
            else
            {
                if (IsCurrentWeaponABow())
                    dwAnimationFlags = ADB_MODIFIER_SPECIAL_1 * 22;
                else
                    dwAnimationFlags = ADB_MODIFIER_SPECIAL_1 * 53;
                bAddPoseChange = currentPose.pose != UP_CRAWL;
            }
            if (GetAnimationWeaponMask() == 0)
                dwAnimationFlags |= ADB_WEAPON_BOW;
            else
                dwAnimationFlags |= GetAnimationWeaponMask();
            break;
        }
        dwAnimationFlags |= ADB_ACTION_SPECIAL | ADB_STAGE_UNIQUE | ADB_STATE_NEUTRAL;
    }
    else
    {
        dwWeaponFlags = GetAnimationWeaponMask();
        dwAnimationFlags = dwAttackFlags | dwWeaponFlags | ADB_ACTION_ATTACK | ADB_STAGE_UNIQUE |
                           ADB_STATE_ATTACK;
    }
    /*
    if ( (dwAnimationFlags & ADB_ACTIONS) != ADB_ACTION_SPECIAL )
            dwAnimationFlags |= ADB_MODIFIER_HDIRECTION1_MIDDLE | ADB_MODIFIER_VDIRECTION1_MIDDLE;
    */
    const DWORD requestedFlags = dwAnimationFlags;
    const bool directedAttack = cmd == UC_ATTACK &&
        (dwAttackFlags & ADB_MODIFIERS) !=
            (ADB_MODIFIER_HDIRECTION1_MIDDLE | ADB_MODIFIER_VDIRECTION1_MIDDLE);
    pEntry = lpUnit->GetAnimation(dwAnimationFlags, false, !directedAttack);
    const bool missingDirection = directedAttack && !pEntry;
    if ((pEntry == 0 || (pEntry->dwFlags & dwWeaponFlags) != dwWeaponFlags) && cmd == UC_ATTACK)
    {
        // OutputDebugString( "no attack animation found, using mm\n" );
        dwAttackFlags = ADB_MODIFIER_HDIRECTION1_MIDDLE | ADB_MODIFIER_VDIRECTION1_MIDDLE;
        dwAnimationFlags = dwAttackFlags | dwWeaponFlags | ADB_ACTION_ATTACK | ADB_STAGE_UNIQUE |
                           ADB_STATE_ATTACK;
        pEntry = lpUnit->GetAnimation(dwAnimationFlags);
        if (missingDirection)
            GameJournal::MissingAnimation(lpUnit->GetMaskName(), requestedFlags,
                pEntry ? GameJournal::AnimationGapResult::Ordinary : GameJournal::AnimationGapResult::Missing);
        if (pEntry == 0)
        {
            OutputDebugString(
                "Fucking ShITTT!!! ArtIsTs MuSt diE!!! CaN'T FiNd pRoPER ANiMatioN!!!\n");
            //			dwAttackFlags = 0;
            //			dwAnimationFlags = GetAnimationWeaponMask() | ADB_ACTION_ATTACK |
            // ADB_STAGE_CYCLE | ADB_STATE_ATTACK;
            pEntry = lpUnit->GetAnimationByIndex(0); // GetAnimation( dwAnimationFlags );
        }
    }
    if(cmd==UC_ATTACK)if(auto*mod=ModRuntime::Current()){
        std::string name=mod->DualAnimation(*lpUnit);
        if(!name.empty())if(const SADEntry*dual=lpUnit->GetAnimationEntry(name.c_str()))
            if(!mod->DualStrikes(name.c_str(),GetAnimationLength(dual->dwEntryIndex)).empty())pEntry=dual;
    }
    ASSERT(pEntry);
    nHit = 0;
    if (pEntry)
    {
        if (bAddPoseChange)
        {
            SL0AnimationSeq t;
            SL1Pose srcPose, dstPose;
            dstPose.Convert(actionPose);
            srcPose = currentPose;
            if (GetTransAnimation(t, srcPose, dstPose))
            {
                res.Append(t.animations[0], t.GetStartId(0), t.GetCycleId(0));
                nHit += t.animations[0].nLength;
            }
        }
        res.AddSimple(pEntry->dwEntryIndex, GetAnimationLength(pEntry->dwEntryIndex));
        nHit += GetAnimationHit(pEntry->dwEntryIndex);
        if (bAddPoseChange)
        {
            SL0AnimationSeq t;
            SL1Pose srcPose, dstPose;
            srcPose.Convert(actionPose);
            dstPose = currentPose;
            if (GetTransAnimation(t, srcPose, dstPose))
                res.Append(t.animations[0], t.GetStartId(0), t.GetCycleId(0));
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// uc_castpoint, uc_castunit, length in segments
void CUnitServer::GetCastAnimation(SL0AnimationSeq &res, int &nHit, EUnitCommand cmd, int nLength)
{
    const SADEntry *pEntry;
    DWORD dwAnimationFlags;
    res.Clear();
    if (lpUnit->GetTypeID() == MOTID_HUMAN)
    {
        dwAnimationFlags = GetAnimationWeaponMask() | ADB_ACTION_CAST | ADB_STAGE_UNIQUE;
        if (currentPose.pose == UP_CRAWL)
            dwAnimationFlags |= ADB_STATE_WARRY | ADB_MODIFIER_HDIRECTION1_MIDDLE |
                                ADB_MODIFIER_VDIRECTION1_MIDDLE;
        else
        {
            if (currentPose.bAggressive)
                dwAnimationFlags |= ADB_STATE_ATTACK;
            else
                dwAnimationFlags |= ADB_STATE_NEUTRAL;
            if (random.Get(0, 1) == 0)
                dwAnimationFlags |= ADB_MODIFIER_HDIRECTION1_MIDDLE |
                                    ADB_MODIFIER_VDIRECTION1_MIDDLE;
            else
                dwAnimationFlags |= ADB_MODIFIER_HDIRECTION1_MIDDLE | ADB_MODIFIER_VDIRECTION1_UP;
        }
    }
    else
    {
        dwAnimationFlags = ADB_ACTION_ATTACK | ADB_STAGE_UNIQUE | ADB_STATE_ATTACK;
        dwAnimationFlags |= ADB_MODIFIER_HDIRECTION1_MIDDLE | ADB_MODIFIER_VDIRECTION1_MIDDLE;
    }
    pEntry = lpUnit->GetAnimation(dwAnimationFlags);
    ASSERT(pEntry);
    nHit = 0;
    if (pEntry)
    {
        if (auto *mod = ModRuntime::Current(); mod && mod->CombatSetting("AttackAnimSpeed") &&
            !mod->CombatSetting("JMVAttacks"))
            lpUnit->fpAnimationCoeff = float(pEntry->dwAnimationLength) / float(nLength);

        if (nLength > GetAnimationLength(pEntry->dwEntryIndex))
        {
            // if this is a levitating unit then it should continue previous animation (rotate)
            // until its possible to play cast animation
            if ((rpgUnitStats.dwLocomotion == 2 || rpgUnitStats.dwLocomotion == 3))
            {
                nHit = GetAnimationHit(pEntry->dwEntryIndex);
                int nPause = nLength - GetAnimationLength(pEntry->dwEntryIndex);
                res.AddSimple(0xffffffffu, nPause);
                res.AddSimple(pEntry->dwEntryIndex, GetAnimationLength(pEntry->dwEntryIndex));
                nHit += nPause;
            }
            else
            {
                // make animation with freezing at the middle
                nHit = GetAnimationHit(pEntry->dwEntryIndex);
                int nPause = nLength - GetAnimationLength(pEntry->dwEntryIndex);
                res.AddSimple(pEntry->dwEntryIndex, nHit);
                res.AddSimple(0xffffffffu, nPause);
                res.AddSimple(0xffffffffu, GetAnimationLength(pEntry->dwEntryIndex) - nHit);
                res.SetSpeed(UAS_NORMAL_);
                res.animations[1].animationSpeed = UAS_FROZEN;
                nHit += nPause;
            }
        }
        else
        {
            res.AddSimple(pEntry->dwEntryIndex, GetAnimationLength(pEntry->dwEntryIndex));
            nHit = GetAnimationHit(pEntry->dwEntryIndex);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// юнит получил по репе или другому месту и должен изобразить что ему хреново
void CUnitServer::GetSufferAnimation(SL0AnimationSeq &res, int nSufferDirection, bool bDiyng,
                                     bool bMoving)
{
    const SADEntry *pEntry;
    DWORD dwAnimationFlags;

    res.Clear();
    dwAnimationFlags = GetAnimationWeaponMask();
    dwAnimationFlags |= GetPoseAnimationMask(currentPose);
    if (lpUnit->GetTypeID() == MOTID_HUMAN)
    {
        if (bDiyng) // suffer direction are valid only for humans
        {
            if (currentPose.pose == UP_REST)
                dwAnimationFlags |= ADB_MODIFIER_DIRECTION2_MM;
            else if ((currentPose.pose == UP_LAY) || (currentPose.pose == UP_CRAWL))
            {
                if (currentPose.pose == UP_CRAWL && bMoving)
                {
                    dwAnimationFlags &= ~ADB_MODIFIERS;
                    dwAnimationFlags |= random.Get(0, 1) == 0 ? ADB_MODIFIER_DIRECTION2_FM
                                                              : ADB_MODIFIER_DIRECTION2_BM;
                }
                else
                    dwAnimationFlags |= ADB_MODIFIER_DIRECTION2_MM;
            }
            else
            {
                dwAnimationFlags |= nSufferDirection;
                // if moving only forward/backward directions
                if (bMoving)
                {
                    // TODO: похоже здесь потерялись скобки, вернул на место
                    if (((dwAnimationFlags & ADB_MODIFIERS) != ADB_MODIFIER_DIRECTION2_FM) ||
                        ((dwAnimationFlags & ADB_MODIFIERS) != ADB_MODIFIER_DIRECTION2_BM))
                    {
                        dwAnimationFlags &= ~ADB_MODIFIERS;
                        dwAnimationFlags |= random.Get(0, 1) == 0 ? ADB_MODIFIER_DIRECTION2_FM
                                                                  : ADB_MODIFIER_DIRECTION2_BM;
                    }
                }
            }
        }
        else // suffer
            dwAnimationFlags |= ADB_MODIFIER_DIRECTION2_MM;
        dwAnimationFlags &= ~(ADB_STATE_ATTACK | ADB_STATE_NEUTRAL);
        if (bMoving && (currentPose.pose != UP_LAY))
        {
            if (currentPose.bRun && currentPose.pose == UP_STAND)
                dwAnimationFlags |= ADB_MODIFIER_ACTION_RUN | ADB_STATE_ATTACK;
            else
                dwAnimationFlags |= ADB_MODIFIER_ACTION_WALK | ADB_STATE_NEUTRAL;
        }
        else if (currentPose.pose == UP_LAY)
            dwAnimationFlags |= ADB_MODIFIER_ACTION_STAY | ADB_STATE_LIE;
        else
            dwAnimationFlags |= ADB_MODIFIER_ACTION_STAY | ADB_STATE_NEUTRAL;
    }
    else
    {
        if (bMoving && !bDiyng)
            return;
        if ((nSufferDirection == ADB_MODIFIER_DIRECTION2_ML) ||
            (nSufferDirection == ADB_MODIFIER_DIRECTION2_MR))
            dwAnimationFlags |= ADB_MODIFIER_DIRECTION2_MM | ADB_MODIFIER_ACTION_STAY;
        else
            dwAnimationFlags |= nSufferDirection | ADB_MODIFIER_ACTION_STAY;
    }

    if (bDiyng)
        dwAnimationFlags |= ADB_ACTION_DEATH | ADB_STAGE_UNIQUE;
    else
        dwAnimationFlags |= ADB_ACTION_SUFFER | ADB_STAGE_UNIQUE;
    const DWORD requestedFlags = dwAnimationFlags;
    const bool directedDeath = bDiyng && (dwAnimationFlags & ADB_MODIFIER_DIRECTION2_MASK) != 0;
    pEntry = lpUnit->GetAnimation(dwAnimationFlags, true, !directedDeath);

    // try to find 'MM' animation for dying
    if (bDiyng && (pEntry == 0))
    {
        dwAnimationFlags &= ~ADB_MODIFIER_DIRECTION2_MASK;
        dwAnimationFlags |= ADB_MODIFIER_DIRECTION2_MM;
        pEntry = lpUnit->GetAnimation(dwAnimationFlags);
        if (directedDeath)
            GameJournal::MissingAnimation(lpUnit->GetMaskName(), requestedFlags,
                pEntry ? GameJournal::AnimationGapResult::Ordinary : GameJournal::AnimationGapResult::Missing);
    }

    // TODO: Эта проверка в текущей версии довольно часто фейлится, что приводит к раздражающе
    // частым вылетам. После ее выключения визуально ничего неестественного в анимациях не произошло
    // ASSERT(pEntry);

    // в положении лёжа нет анимаций ранения
    if (!bDiyng && (currentPose.pose == UP_LAY))
        pEntry = 0;
    //
    if (pEntry)
        res.AddSimple(pEntry->dwEntryIndex, GetAnimationLength(pEntry->dwEntryIndex));
    if (bMoving)
        res.SetSpeed(UAS_MOVEMENT);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::SelectStandingIdle(SL0AnimationSeq &sequence)
{
    if (!lpUnit || currentPose.pose != UP_STAND || currentPose.bAggressive ||
        sequence.animations.GetSize() != 1)
        return;

    const SL0Animation &stock = sequence.animations[0];
    if (stock.bStart != 0xff || stock.bCyclic != 0xfe)
        return;
    const DWORD cycle = sequence.GetCycleId(0);
    if (cycle > 253 && cycle != 0xffffffffu)
        return;

    const DWORD flags = GetAnimationWeaponMask() | ADB_STATE_NEUTRAL |
        ADB_ACTION_IDLE | ADB_STAGE_CYCLE | ADB_MODIFIER_IDLE_STAY;
    if (!lpUnit->GetAnimationEntryByIndex(254) || !lpUnit->pADFileHdr)
        return;

    const SADEntry *entry = nullptr;
    unsigned eligible = 0;
    const unsigned threshold = random.Get(0, 99);
    for (DWORD index = 254; index < lpUnit->pADFileHdr->dwCount; ++index)
    {
        const SADEntry *candidate = lpUnit->GetAnimationByIndex(index);
        if (!candidate || candidate->dwEntryIndex != index ||
            candidate->dwProbability < threshold || !candidate->Match(flags) ||
            !lpUnit->CanPlayAnimation(candidate))
            continue;
        // This particular ADB entry lacks its referenced ANM clip.
        if (index == 256 && strcmp(candidate->pszAnimationName, "2hscidle22") == 0)
            continue;
        ++eligible;
        if (!entry || random.Get(0, eligible - 1) == 0)
            entry = candidate;
    }
    if (!entry || entry->dwEntryIndex <= 253)
        return;

    sequence.Clear();
    sequence.AddCycle(entry->dwEntryIndex);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::GetIdleAnimation(SL0AnimationSeq &res)
{
    const SADEntry *pEntry;
    res.Clear();
    if (currentPose.pose == UP_STAND && currentPose.bAggressive == false)
    {
        res.AddPumpCycle();
        SelectStandingIdle(res);
        return;
    }
    DWORD dwAnimationFlags = GetAnimationWeaponMask();
    dwAnimationFlags |= GetPoseAnimationMask(currentPose);
    dwAnimationFlags |= ADB_ACTION_IDLE | ADB_STAGE_CYCLE | ADB_MODIFIER_IDLE_STAY;
    pEntry = lpUnit->GetAnimation(dwAnimationFlags);
    if (pEntry)
        res.AddCycle(pEntry->dwEntryIndex);
}
/////////////////////////////////////////////////////////////////////////////////////
SL0AnimationSeq *CUnitServer::GetIdleAnimation()
{
    DWORD dwAnimationFlags = GetAnimationWeaponMask();
    dwAnimationFlags |= GetPoseAnimationMask(currentPose);
    if (animIdleCache.dwAnimationFlags != dwAnimationFlags)
    {
        animIdleCache.dwAnimationFlags = dwAnimationFlags;
        GetIdleAnimation(animIdleCache.anim);
    }
    else
    {
        animIdleCache.anim.nStartSegment = -1;
        // Native GetIdleCached inlines the miss path: one selection per call.
        SelectStandingIdle(animIdleCache.anim);
    }
    return &animIdleCache.anim;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::GetMoveAnimation(SL0AnimationSeq &res, bool bStart)
{
    const SADEntry *pEntry;
    res.Clear();
    DWORD dwAnimationFlags = GetAnimationWeaponMask(), dwPose;
    dwPose = GetPoseAnimationMask(currentPose);
    if (dwPose == ADB_STATE_NEUTRAL && !bStart && lpUnit->GetTypeID() == MOTID_HUMAN)
        dwPose = ADB_STATE_ATTACK;
    dwAnimationFlags |= dwPose;
    if (lpUnit->IsRunning())
        dwAnimationFlags |= ADB_ACTION_RUN;
    else
        dwAnimationFlags |= ADB_ACTION_WALK;
    if (bStart)
        dwAnimationFlags |= ADB_STAGE_START;
    else
    {
        dwAnimationFlags |= ADB_STAGE_CYCLE;
        if (lpUnit->GetTypeID() == MOTID_HUMAN)
        {
            if (rpgUnitStats.GetMoveDamageModifier() <= theAIGeneralSettings.fDamageLevel2Value)
                dwAnimationFlags |= ADB_MODIFIER_WALK_LAME2;
            else if (rpgUnitStats.GetMoveDamageModifier() <=
                     theAIGeneralSettings.fDamageLevel1Value)
                dwAnimationFlags |= ADB_MODIFIER_WALK_LAME1;
        }
    }
    pEntry = lpUnit->GetAnimation(dwAnimationFlags);
    if (pEntry)
    {
        if (bStart)
            res.AddSimple(pEntry->dwEntryIndex, GetAnimationLength(pEntry->dwEntryIndex));
        else
            res.AddCycle(pEntry->dwEntryIndex);
    }
    res.SetSpeed(UAS_MOVEMENT);
}
/////////////////////////////////////////////////////////////////////////////////////
SL0AnimationSeq *CUnitServer::GetMoveAnimation()
{
    DWORD dwAnimationFlags = GetAnimationWeaponMask();
    dwAnimationFlags |= GetPoseAnimationMask(currentPose);
    if (lpUnit->IsRunning())
        dwAnimationFlags |= ADB_ACTION_RUN;
    else
    {
        dwAnimationFlags |= ADB_ACTION_WALK;
        if (lpUnit->GetTypeID() == MOTID_HUMAN)
        {
            if (rpgUnitStats.GetMoveDamageModifier() <= theAIGeneralSettings.fDamageLevel2Value)
                dwAnimationFlags |= ADB_MODIFIER_WALK_LAME2;
            else if (rpgUnitStats.GetMoveDamageModifier() <=
                     theAIGeneralSettings.fDamageLevel1Value)
                dwAnimationFlags |= ADB_MODIFIER_WALK_LAME1;
        }
    }
    if (animMoveCache.dwAnimationFlags != dwAnimationFlags)
    {
        animMoveCache.dwAnimationFlags = dwAnimationFlags;
        SL0AnimationSeq &move = animMoveCache.anim;
        if (lpUnit->GetTypeID() == MOTID_HUMAN)
        {
            // we are using startmove + cyclemove for humans
            SL0AnimationSeq moveCyc;
            GetMoveAnimation(moveCyc);
            GetMoveAnimation(move, true);
            move += moveCyc;
        }
        else
            GetMoveAnimation(move);
    }
    return &animMoveCache.anim;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::IsMoveAnimation(SL0AnimationSeq &res)
{
    return animMoveCache.anim.IsSame(res);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::GetTransAnimation(SL0AnimationSeq &res, const SL1Pose &srcPose,
                                    const SL1Pose &dstPose)
{
    const SADEntry *pEntry;
    res.Clear();
    if ((lpUnit->GetTypeID() != MOTID_HUMAN) && (dstPose.pose != UP_REST) &&
        (srcPose.pose != UP_REST))
        return false;
    if (srcPose.pose == dstPose.pose &&
        (srcPose.pose != UP_STAND || srcPose.bAggressive == dstPose.bAggressive))
        return false;
    DWORD dwAnimationFlags = GetAnimationWeaponMask();
    dwAnimationFlags |= GetPoseAnimationMask(srcPose);
    dwAnimationFlags |= ADB_ACTION_CROSS;
    switch (dstPose.pose)
    {
    case UP_STAND:
        if (dstPose.bAggressive || (lpUnit->GetTypeID() != MOTID_HUMAN))
            dwAnimationFlags |= ADB_MODIFIER_CROSS_TO_ATTACK;
        else
            dwAnimationFlags |= ADB_MODIFIER_CROSS_TO_NEUTRAL;
        break;
    case UP_CRAWL:
        dwAnimationFlags |= ADB_MODIFIER_CROSS_TO_WARRY;
        break;
    case UP_LAY:
        dwAnimationFlags |= ADB_MODIFIER_CROSS_TO_LIE;
        break;
    case UP_REST:
        dwAnimationFlags |= ADB_MODIFIER_CROSS_TO_REST;
        break;
    }
    if ((srcPose.pose == UP_REST || dstPose.pose == UP_REST) && !lpUnit->HasRestAnimations())
    {
        GameJournal::MissingAnimation(lpUnit->GetMaskName(), dwAnimationFlags,
            GameJournal::AnimationGapResult::SkipTransition);
        return false;
    }
    pEntry = lpUnit->GetAnimation(dwAnimationFlags);
    // ASSERT( pEntry );
    if (pEntry)
    {
        res.AddSimple(pEntry->dwEntryIndex, GetAnimationLength(pEntry->dwEntryIndex));
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
int CUnitServer::CalcSufferDirection(CUnit *pAttacker, BOOL bAreaSpell)
{
    int nSufferDirection;
    CPlot3D ptDamageFrom;
    float fpForward, fpRight;
    // определим direction в котором колбаснет геро
    nSufferDirection = 0;
    nSufferDirection = ADB_MODIFIER_DIRECTION2_MM;
    if (pAttacker)
    {
        if (bAreaSpell)
            nSufferDirection = ADB_MODIFIER_DIRECTION2_MM;
        else
        {
            ptDamageFrom = pAttacker->GetCP() - lpUnit->GetCP();
            fpForward = -(ptDamageFrom * lpUnit->GetYAxis());
            fpRight = -(ptDamageFrom * lpUnit->GetXAxis());
            if (fabs(fpForward) > fabs(fpRight))
            {
                if (fpForward > 0)
                    nSufferDirection = ADB_MODIFIER_DIRECTION2_BM;
                else
                    nSufferDirection = ADB_MODIFIER_DIRECTION2_FM;
            }
            else
            {
                if (fpRight > 0)
                    nSufferDirection = ADB_MODIFIER_DIRECTION2_ML;
                else
                    nSufferDirection = ADB_MODIFIER_DIRECTION2_MR;
            }
        }
    }
    return nSufferDirection;
}
/////////////////////////////////////////////////////////////////////////////////////
int CUnitServer::GetAnimationLength(DWORD bAnim)
{
    const SADEntry *pEntry = lpUnit->GetAnimationByIndex(bAnim);
    if (pEntry)
        return pEntry->dwAnimationLength / lpUnit->GetAnimationCoeff(pEntry);
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
int CUnitServer::GetAnimationHit(DWORD bAnim)
{
    const SADEntry *pEntry = lpUnit->GetAnimationByIndex(bAnim);
    if (pEntry)
        return pEntry->dwHit / lpUnit->GetAnimationCoeff(pEntry);
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! TAME
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitServer::Tame(CUnit *pWho, float fpWhoSkill)
{
    CPlayer *pPlayer;
    float fpOurSkill;

    fpOurSkill = rpgUnitStats.GetManualSkillLevel(RPGMSKL_TAME, GetUnit());
    fpOurSkill += GetCurrentTameLevel() * 10;
    if (GetUnit()->GetOwner())
        return TameFailed(pWho);
    // any tries to tame already maximally tamed monster should succeed
    if (GetUnit()->GetOwner() == pWho->GetOwner() && pWho->GetOwner() != 0)
        return TRUE;
    if (fpOurSkill >= fpWhoSkill)
        return TameFailed(pWho);
    switch (tameLevel)
    {
    case UTL_NONE:
        lpUnit->SetPossibleEnemiesMask(~(1 << pWho->GetPlayerNum()));
        tameLevel = UTL_NOTENEMY;
        break;
    case UTL_NOTENEMY:
        nOriginalPlayerNum = lpUnit->GetPlayerNum();
        lpUnit->SetPlayerNum(pWho->GetPlayerNum());
        tameLevel = UTL_ALLY;
        break;
    case UTL_ALLY:
        pPlayer = pWho->GetOwner();
        if (pPlayer)
        {
            pPlayer->RemoveTamedFromControl();
            lpUnit->SetPlayerNum(pWho->GetPlayerNum());
            lpUnit->SetOwner(pPlayer);
            logic.AddMotivation(dbgnew CUMTamed);
            logic.environment.seenCorpses.RemoveAll();
            logic.environment.freshCorpses.RemoveAll();
            tameLevel = UTL_CONTROL;
        }
        break;
    }
    nPlayerWhoTamed = pWho->GetPlayerNum();
    logic.StartIdle();
    CmdWait();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// попытка приручить провалилась
BOOL CUnitServer::TameFailed(CUnit *pWho)
{
    // if tame on next level failed remove all tame
    if (GetPlayerWhoTamed() == pWho->GetPlayerNum())
        RemoveTame();
    ((CWorldServer *)GetWorld())->fof.MakeDamage(pWho, GetUnit(), 10);
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::RemoveTame()
{
    if (tameLevel == UTL_NONE)
        return;
    logic.RemoveMotivation(AI_UM_TAMED);
    lpUnit->SetOwner(0);
    lpUnit->SetPossibleEnemiesMask(0xFFFFFFFF);
    if (tameLevel > UTL_NOTENEMY)
        lpUnit->SetPlayerNum(nOriginalPlayerNum);
    tameLevel = UTL_NONE;
}
/////////////////////////////////////////////////////////////////////////////////////
int CUnitServer::GetPlayerWhoTamed()
{
    if (tameLevel == UTL_NONE)
        return -1;
    return nPlayerWhoTamed;
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! Supporting code
/////////////////////////////////////////////////////////////////////////////////////
// main function, processing units low level commands and actions
int CUnitServer::Segment()
{
    CorePerf::Scope coreTiming(CorePerf::UnitSegment);
    // Observe loaded status without serializing cached speed.
    if(auto*mod=ModRuntime::Current()){float factor=mod->CrippledSpeed(*lpUnit);if(factor!=lastCrippledFactor){lastCrippledFactor=factor;L1SpeedChanged();}}
    if (!lpUnit->bDead)
    {
#ifdef __DEMO__
        if (curCommand.cmd == UC_IDLE)
        {
#endif // __DEMO__
       // mana and health regeneration
            if (((CMapObjectServer *)lpUnit->GetMapObjectSpecific())
                    ->GetRPGStats()
                    ->Regenerate(rpgUnitStats, lpUnit, curCommand.cmd != UC_IDLE))
            {
                rpgUnitStats.RecalcStats(GetMapObjectServer()->GetRPGStats(), lpUnit);
                AddUpdateMask(CMapObject::VLD_MASK_STATS);
                AddUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
            }
#ifdef __DEMO__
        }
#endif // __DEMO__
       //
        CreateFootstep();
        UseContinousSpells();
        // CRAP перерасчет заметности объекта
        if (lpUnit->IsHero()) //>CheckFlag( UNIT_RECALC_DETECT ) )
        {
            logic.environment.CalcNotability(this, lpUnit->sensesDetection);
            AddUpdateMask(CMapObject::VLD_MASK_DETECTIONS);
            // bored processing
            nBoredCounter++;
            if (nBoredCounter > 500 && random.Get(0, 1000) == 0)
            {
                if (lpUnit->GetOwner() &&
                    ((CPlayerServer *)lpUnit->GetOwner()->GetPlayerSpecific())->dangerState ==
                        WDS_NORMAL)
                {
                    InformFailure(ACK_HERO_BORED);
                    nBoredCounter = 400;
                }
                else
                    nBoredCounter = 0;
            }
        }
        // process acknowledgements
        acknowledgements.Segment(this);
        // process logic
        { CorePerf::Scope coreTiming(CorePerf::UnitAI); logic.Segment(); }
        lpUnit->ResetFlag(MAPOBJ_MOVED);
        RecalcEnemy();
        // commands execution
        if (L1IsInterruptable())
            LcSegment();
        else
        {
            if (L1IsAttacking() && nextCommand.IsMove())
                L1CancelAttack(true);
        }
        L1Segment();
    }
    else
    {
        CWorldServer *pWS = (CWorldServer *)GetWorld();
        if (lpUnit->cp.z > 0)
        {
            fpZSpeed += 0.01f;
            lpUnit->cp.z -= fpZSpeed;
            lpUnit->cp.z = Max(0.0f, lpUnit->cp.z);
            AddUpdateMask(CMapObject::VLD_MASK_POSROT);
        }
        if (!bLcCompletelyDead && level0Animation.animations.GetSize() == 0)
        {
            bLcCompletelyDead = true;
            // find suitable rot for corpse to be lootable
            /*
            for ( int nTry = 0; nTry < 10; nTry++ )
            {
                    if ( !pWS->aiMap.CanPutUnit( 3, lpUnit->GetTargetCP() ) )
                    {
                            CPlot3D ptDir(0,0,0);
                            float fpAlpha = random.GetFloat( 0, 6.28f );
                            ptDir.x = cos( fpAlpha );
                            ptDir.y = sin( fpAlpha );
                            lpUnit->OrientInYDirection( ptDir );
                            path.curSegment.SetStartPos( lpUnit->GetCP(), ptDir,
            pWS->GetSegmentTimeCounter() ); lpUnit->LowNewRoute();
                    }
                    else
                            break;
            }*/
            // inform everybody about this event
            pWS->InformAboutUnitDeath(lpUnit);
            // remove from owner, and maybe this is gameover
            CPlayer *pPlayer = lpUnit->GetOwner();
            lpUnit->SetOwner(0);
            if (pPlayer && pPlayer->IsMainHero(lpUnit))
                pPlayer->GetPlayerSpecific()->GameOver();
            else
            {
                logic.ClearRefs();
                acknowledgements.Clear();
            }
            curCommand.Clear();
            nextCommand.Clear();
        }
    }
    L0Segment();
    return 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::RemoveReferencesOn(CMapObject *pTarget)
{
    if (pTarget == 0)
        return;
    if (curCommand.pTarget == pTarget)
        LcCancelCommand();
    if (nextCommand.pTarget == pTarget)
        CmdWait();
    if (level1Command.nTargetID == pTarget->GetID())
        L1CancelAttack();
    logic.RemoveReferencesOn(pTarget);
    train.RemoveReferencesOn(pTarget);
}
/////////////////////////////////////////////////////////////////////////////////////
// послать сообщение пользователю, что какое-либо действие окончилось неудачей
void CUnitServer::InformFailure(DWORD dwAction)
{
    if (!IsHeroAck(dwAction) && dwAction != ACK_UNIT_BECOME_KILLER)
        return;
    if (dwAction == ACK_NOTHING)
        return;
    CPlayer *pPlayer = lpUnit->GetOwner();
    if (pPlayer)
        ((CPlayerServer *)pPlayer->GetPlayerSpecific())->SendFailure(lpUnit, dwAction);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::InformAttackPathNotFound(const SUnitCommand &cmd)
{
    if (cmd.cmd == UC_ATTACK)
        InformFailure(ACK_HERO_REFUSE_NO_WAY_TO_ATTACK);
    else
        InformFailure(ACK_HERO_REFUSE_PATH_NOT_FOUND);
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! FOOTSTEPs
/////////////////////////////////////////////////////////////////////////////////////
// расстояние между соседними следами не должно превосходить 1.5 метра, из этих
// соображений выбирается nFootstepGenInterval
void CUnitServer::CreateFootstep()
{
    if ((theWorldServer.GetSegmentTimeCounter() & nFootstepGenInterval) == 0)
    {
        if (lastFootstep)
        {
            lastFootstep->ptPointPosition.x = lpUnit->GetCP().x;
            lastFootstep->ptPointPosition.y = lpUnit->GetCP().y;
        }
        lastFootstep = theWorldServer.aiMap.marks.AddFootStep(lpUnit->GetCP(), this);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// числа указанные здесь зависят от расстояния на котором распознаются следы ( здесь
// считается, что это расстояние равно 3-м ai тайлам) и количества сегментов в секунду
// считается, что 15
void CUnitServer::CalcNewFootstepGenInterval()
{
    if (L1IsMoving())
    {
        float fpSpeed = lpUnit->GetSpeed();
        if (fpSpeed > 1.2f) // 1.5f ) is exact value
            nFootstepGenInterval = 0;
        else if (fpSpeed > 0.6f) // 75f )
            nFootstepGenInterval = 1;
        else if (fpSpeed > 0.3f) // 375f )
            nFootstepGenInterval = 3;
        else if (fpSpeed > 0.15f) // 19f )
            nFootstepGenInterval = 7;
        else
            nFootstepGenInterval = 15;
    }
    else
        nFootstepGenInterval = 15;
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! CONTINOUS spells in items
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::RebuildContinousSpellsList()
{
    POSITION pos, pos1;
    CList<CItem *, CItem *> items;
    CList<CSpell *, CSpell *> spells;

    lpUnit->GetAllItemsList((CGameBaseList &)items, (CGameBaseList &)spells);
    // remove inactive weapons from list
    CItemWeapon* current = lpUnit->GetCurrentQWeapon();
    const bool twoHanded = current && ModStats::IsTwohandedWeapon(current->GetWeaponType(),lpUnit);
    const int leftSlot = current && !twoHanded ? ModStats::LeftHand(*lpUnit) : -1;
    CItemWeapon* left = leftSlot >= 0 ? lpUnit->GetQWeapon(leftSlot) : nullptr;
    for (pos = items.GetHeadPosition(); pos;)
    {
        pos1 = pos;
        CItemWeapon *pWeapon = (CItemWeapon *)items.GetNext(pos);
        if (pWeapon->WhoAmI() == HPC_ITEM_WEAPON &&
            !ModItemSpells::KeepContinuousWeapon(pWeapon==current,current!=nullptr,twoHanded,pWeapon==left))
            items.RemoveAt(pos1);
    }
    // mark current spells as inactive
    for (pos = activeContSpells.GetHeadPosition(); pos;)
        activeContSpells.GetNext(pos).bIsUsed = false;
    // walk through items and mark already used items and create new
    for (pos = items.GetHeadPosition(); pos;)
    {
        CItem *pItem = items.GetNext(pos);
        CSpell *pSpell = nullptr;
        if (pItem)
            pSpell = pItem->GetSpell(0);
        if (!pSpell || !pSpell->IsItemContinuous())
            continue;
        bool bFound = false;
        for (pos1 = activeContSpells.GetHeadPosition(); pos1;)
        {
            SContSpellInfo &info = activeContSpells.GetNext(pos1);
            if (info.bIsUsed || info.pItem != pItem)
                continue;
            info.bIsUsed = true;
            bFound = true;
            break;
        }
        if (bFound)
            continue;
        SContSpellInfo &newInfo = activeContSpells.GetAt(
            activeContSpells.AddTail(SContSpellInfo()));
        newInfo.pItem = pItem;
        newInfo.bIsUsed = true;
    }
    // remove unused items with continous effect
    for (pos = activeContSpells.GetHeadPosition(); pos;)
    {
        pos1 = pos;
        if (!activeContSpells.GetNext(pos).bIsUsed)
            activeContSpells.RemoveAt(pos1);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::UseContinousSpells()
{
    CWorldServer *pWS = ((CWorldServer *)GetWorld());
    bool bWasHealing = false;
    for (POSITION pos = activeContSpells.GetHeadPosition(); pos;)
    {
        SContSpellInfo &info = activeContSpells.GetNext(pos);
        if (info.nDelay > 0)
            info.nDelay--;
        else if (!info.pItem.IsEmpty())
        {
            CItem *pItem = info.pItem;
            CSpell *pSpell = pItem->GetSpell(0);
            if (ModRuntime::Current()?ModSpells::ItemCanCast(pItem->GetMP(),pSpell->GetManaCost()):pItem->GetMP()>pSpell->GetManaCost())
            {
                if ((pSpell->GetSubtypeID() >> 16) == SPELL_ID_HEALING)
                {
                    if (bWasHealing || lpUnit->GetHP() == lpUnit->GetMaxHP())
                        continue;
                    bWasHealing = true;
                }
                SpendItemMana(pItem, pSpell->GetManaCost());
                pWS->CastSpell(pSpell, lpUnit, lpUnit->GetCP(), lpUnit, lpUnit->GetCP(), true);
                info.nDelay = pSpell->GetDuration() - 3;
                if (info.nDelay <= 0)
                    info.nDelay = 8;
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::SpendItemMana(CItem *pItem, float fpMana)
{
    ASSERT(pItem);
    if (!pItem)
        return;
    CWorldServer *pWS = ((CWorldServer *)GetWorld());

    pItem->ChangeCurrentMP(-fpMana);
    pWS->AddActiveItem(pItem);
    CPlayer *pPlayer = lpUnit->GetOwner();
    CItemSrcInfo *pInfo = (CItemSrcInfo *)pItem->GetUserData();
    if (pPlayer && pInfo)
    {
        CItem *pPItem = (CItem *)pPlayer->party.GetItemManager()->Find(pInfo->nSrcItemID);
        ASSERT(pPItem);
        if (pPItem)
        {
            pPItem->SetMP(pItem->GetMP());
            pPlayer->AddActiveItem(pPItem);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! DIPLOMACY spreading
/////////////////////////////////////////////////////////////////////////////////////
union UFloatInt
{
    float fp;
    int n;
};
static UFloatInt maxRange = { 0 }; // FP_FOF_HEARRADIUS * FP_FOF_HEARRADIUS };

static inline int CanHear(const CUnit *pA, const CUnit *pB)
{
    UFloatInt range;
    float fpx = pA->GetCP().x - pB->GetCP().x, fpy = pA->GetCP().y - pB->GetCP().y;
    range.fp = fpx * fpx + fpy * fpy;
    return range.n < maxRange.n;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::RemovePendingDiplomacy()
{
    dwFutureEnemies = 0;
    logic.environment.RemoveDiplomacyMsgs();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::HearNewEnemiesMsg(DWORD dwNewEnemies)
{
    lpUnit->AddEnemyMask(dwNewEnemies);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::InformAboutEnemy(CUnit *pWho, DWORD dwNewEnemiesMask)
{
    dwFutureEnemies |= dwNewEnemiesMask;
    CString szInfo;
    for (int i = 0; i < 32; i++)
    {
        if (dwNewEnemiesMask & (1 << i))
        {
            CString szCur;
            szCur.Format("Unit %i claims that player %i are enemies! ", pWho->GetID(), i);
            szInfo = szInfo + szCur;
        }
    }
    logic.environment.HearNewEnemies(dwNewEnemiesMask, 30, szInfo);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::RecalcEnemy()
{
    if (!lpUnit->CheckFlag(UNIT_RECALCENEMY))
        return;
    lpUnit->ResetFlag(UNIT_RECALCENEMY);

    dwFutureEnemies |= lpUnit->GetEnemyMask();

    CFofTracker &fof = ((CWorldServer *)lpUnit->GetWorld())->fof;
    CMapObjectPtrList &nearUnits =
        *logic.GetUnitsList(); // using nearUnits from logic, this is reasonable and fast
    POSITION pos;
    int nCurrentPlayer, nOtherPlayer;
    nCurrentPlayer = lpUnit->GetPlayerNum();
    ASSERT("unit has invalid playernum" && nCurrentPlayer >= 0 && nCurrentPlayer < N_MAX_PLAYERS);
    pos = nearUnits.GetHeadPosition();
    maxRange.fp = sqr(logic.GetCallForHelpRadius());
    while (pos)
    {
        CUnit *pOther = (CUnit *)nearUnits.GetNext(pos);
        nOtherPlayer = pOther->GetPlayerNum();
        ASSERT("unit has invalid playernum" && nOtherPlayer >= 0 && nOtherPlayer < N_MAX_PLAYERS);
        if (fof.IsPlayerAlly(nCurrentPlayer, nOtherPlayer))
        {
            if (CanHear(pOther, lpUnit))
            {
                DWORD dwOtherFutureEnemies = ((CUnitServer *)(pOther->GetUnitSpecific()))
                                                 ->dwFutureEnemies;
                if ((lpUnit->GetEnemyMask() ^ dwOtherFutureEnemies) != 0)
                {
                    int nEnemyHasChanged = lpUnit->GetEnemyMask() & ~dwOtherFutureEnemies;
                    int nReverseInfo = pOther->GetEnemyMask() & ~dwFutureEnemies;
                    if (nEnemyHasChanged)
                        ((CUnitServer *)pOther->GetUnitSpecific())
                            ->InformAboutEnemy(lpUnit, nEnemyHasChanged);
                    if (nReverseInfo)
                        InformAboutEnemy(pOther, nReverseInfo);
                }
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! RPG system work
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::TimeOfDayChanged()
{
    // recalc bonuses to vision/ detection
    RecalcAIParams();
}
/////////////////////////////////////////////////////////////////////////////////////
// определить AI класс юнита по его радиусу и высоте
static DWORD CalcUnitClass(float fpR, float fpH)
{
    if (fpR > 1.5f)
        return 7;
    if (fpR > 0.85f)
    {
        if (fpH > 1.7f)
            return 6;
        else
            return 5;
    }
    if (fpH < 0.6f)
        return 1;
    if (fpH < 1.2f)
        return 2;
    if (fpH < 1.9f)
        return 3;
    return 4;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::SetRPGStats(SRPGUnitStats *lpRPGNewUnitStats)
{
    rpgUnitStats = *lpRPGNewUnitStats;
    lpUnit->SetUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
    //
    RecalcRPGStats();       // perform first stage recalc with health distribution
    rpgUnitStats.HealAll(); // heal all hit locations
    RecalcRPGStats();       // perform recalc damage modifiers with distributed health
    // restore HP and MP
    GetMapObjectServer()->GetRPGStats()->ssHP.fpCurrent =
        GetMapObjectServer()->GetRPGStats()->ssHP.fpMax;
    GetMapObjectServer()->GetRPGStats()->ssMP.fpCurrent =
        GetMapObjectServer()->GetRPGStats()->ssMP.fpMax;
}
/////////////////////////////////////////////////////////////////////////////////////
// calc AI class for this unit, assuming bounding volume has been loaded here
void CUnitServer::RecalcAIClass()
{
    ASSERT(lpUnit->bv.fpRadius > 0);
    if ((rpgUnitStats.dwLocomotion == 2 || rpgUnitStats.dwLocomotion == 3))
    {
        rpgUnitStats.wAIClassLie = 0;
        rpgUnitStats.wAIClassStay = 0;
    }
    else
    {
        // CRAP - ai class calc is incorrect, radius is incorrect
        float fpRadius = lpUnit->GetUnitRadius(); // * 2;
        float fpHeight = lpUnit->GetUnitHeight() * GetUnit()->GetRaceModel()->fHeadHeight;
        if (lpUnit->GetTypeID() == MOTID_HUMAN)
            fpHeight = Min(fpHeight, 1.89f);
        rpgUnitStats.wAIClassStay = CalcUnitClass(fpRadius, fpHeight);
        rpgUnitStats.wAIClassLie = CalcUnitClass(fpRadius, fpHeight * 0.2f);
        rpgUnitStats.wAIClassCrawl = CalcUnitClass(fpRadius, fpHeight * 0.6f);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::ChangeSpeedWithSpell()
{
    float factor=ModRuntime::Current()?ModRuntime::Current()->CrippledSpeed(*lpUnit):1;
    if(factor!=lastCrippledFactor){lastCrippledFactor=factor;L1SpeedChanged();}
    if (auto* mod = ModRuntime::Current(); mod && mod->SpellAddon().Feature29() == 1)
        SyncNpcMagicState();
    else
    {
        if (lpUnit->HasSpellEffect(SPELL_ID_FEEBLEMIND) &&
            rpgUnitStats.rpgNAWWeaponType != RPGWST_MAGIC)
            lpUnit->SetFlag(UNIT_FEEBLED);
        else
            lpUnit->ResetFlag(UNIT_FEEBLED);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::ComplectionChanged()
{
    bool bRv;
    if (lpUnit->IsDead())
        return;
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    pWS->aiMap.RemoveObject(lpUnit);
    // reload bounding volumes
    lpUnit->LoadFigureBoundingVolumes(lpUnit->GetMaskName());
    //
    lpUnit->RecalcAIClass();
    lpUnit->RecalcAIParams();
    if (!pWS->aiMap.CanPutUnitRelaxed(lpUnit))
    {
        // оказывается юнит теперь нельзя поставить на старое место - перенесем его на новое
        CPlot3D ptNewPlace = lpUnit->GetCP();
        if (!pWS->aiMap.FindUnitPosition(lpUnit, lpUnit->GetCP(), ptNewPlace))
        {
            // this unit is unlucky :)
            SRPGAttackPortion overDose;
            overDose.MakeDeadlyAttack();
            ProcessAttackPortion(&overDose, 0, 0);
            ASSERT(lpUnit->IsDead());
            ptNewPlace = lpUnit->GetCP();
        }
        else
            lpUnit->SetCP(ptNewPlace);
    }
    //
    bRv = theWorldServer.aiMap.AddObject(lpUnit);
    ASSERT(bRv);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::GetRPGStats(SRPGUnitStats *pRPGNewUnitStats)
{
    *pRPGNewUnitStats = rpgUnitStats;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::GainExperience(float fpExp)
{
    rpgUnitStats.GainExperienceInCombat(lpUnit, fpExp);
    RecalcRPGStats();
    //	rpgUnitStats.Heal( GetMapObjectServer()->GetRPGStats(), 10000 );
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitServer::RecalcRPGStats(bool bStrengthChanged)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    int nTime = pWS->GetSegmentTimeCounter();
    float fpOldAction = rpgUnitStats.action.fpEffectiveValue;
    //
    if (bStrengthChanged)
        rpgUnitStats.StrengthChanged(GetMapObjectServer()->GetRPGStats(), lpUnit);
    rpgUnitStats.RecalcStats(GetMapObjectServer()->GetRPGStats(), lpUnit);
    if (nCanAttackTime > nTime)
        nCanAttackTime = nTime + Float2Int((nCanAttackTime - nTime) * fpOldAction /
                                           rpgUnitStats.action.fpEffectiveValue);
    AddUpdateMask(CMapObject::VLD_MASK_STATS); // this will force exportstats
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::CalcUnifiedDefend()
{
    SRPGCommonStats &stats = *GetMapObjectServer()->GetRPGStats();
    rpgUnitStats.ExportAbsorbs(GetUnit(), &stats, rpgDefend);
    // calc to hit for defender
    float fDefenceModifier = lpUnit->IsHero() ? 1.0f
                                              : theAIGeneralSettings
                                                    .fDefenceLevels[theSettings.nDifficultyLevel];
    CItemWeapon *pWeapon = lpUnit->GetCurrentQWeapon();
    if (!lpUnit->GetOwner() || pWeapon == 0)
        rpgDefend.wToHit = rpgUnitStats.fpNAWParry * fDefenceModifier;
    else
        rpgDefend.wToHit = rpgUnitStats.GetWeaponSkillLevel(pWeapon->GetWeaponType(), lpUnit) *
                           fDefenceModifier;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::ExportStats()
{
    BYTE bSpeeds[4];
    memcpy(bSpeeds, lpUnit->rpgUserUnitStats.cSpeeds, 4);
    rpgUnitStats.Export(lpUnit, &lpUnit->rpgUserUnitStats);
    rpgUnitStats.bFlags = 0;
    CalcUnifiedDefend();
    AddUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
    if (memcmp(bSpeeds, lpUnit->rpgUserUnitStats.cSpeeds, 4) != 0)
        L1SpeedChanged();
}
/////////////////////////////////////////////////////////////////////////////////////
// wear new armor
bool CUnitServer::Wear(DWORD dwArmor)
{
    DWORD dwArmorType = theItemDatabase.armors[CItem::GetShape(dwArmor)]->dwTypeID;
    rpgUnitStats.Wear(lpUnit->lpArmors[dwArmorType]);
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::Remove(ERPGArmorTypes type)
{
    rpgUnitStats.RecalcHitLocations(lpUnit);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::RemoveAllArmors()
{
    rpgUnitStats.RemoveAllArmors();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitServer::NotifyItemsUpdated()
{
    RebuildContinousSpellsList();
    logic.caster.Initialize(lpUnit);
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// select quick weapon
BOOL CUnitServer::ChangeWeapon(DWORD dwQWeaponIndex)
{
    DWORD dwNewWeaponIndex = dwQWeaponIndex;
    CItem *lpNewWeapon = lpUnit->GetQWeapon(dwQWeaponIndex);

    if (lpNewWeapon && !lpNewWeapon->IsBroken())
        dwNewWeaponIndex = dwQWeaponIndex;
    else
        dwNewWeaponIndex = DWORD(-1);
    // assign new weapon
    AddUpdateMask(CMapObject::VLD_MASK_POSE);
    lpUnit->dwWishWeapon = lpUnit->dwCurrentWeapon = dwNewWeaponIndex;
    //
    if (lpUnit->GetOwner())
        logic.caster.Initialize(lpUnit);
    RebuildContinousSpellsList();

    if (!lpNewWeapon || lpNewWeapon->IsBroken())
        return FALSE;
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::RecalcAIParams()
{
    CWorldServer *pWorld = (CWorldServer *)GetWorld();
    float fpNight = pWorld->GetNightPower();
    fpModSound = 1;
    fpModVision = 1;
    fpSModSound = 1;
    fpSModVision = rpgUnitStats.senses[RPGS_INFRA].fpEffectiveValue * fpNight / 100 + (1 - fpNight);
    fpSModVision *= pWorld->GetVisionBonus();

    switch (lpUnit->currentPose)
    {
    case UP_LAY:
        fpModVision *= AI_LAY_VISION;
        fpSModVision *= AI_LAY_SENSE_VISION;
        break;
    case UP_CRAWL:
        fpModVision *= AI_CRAWL_VISION;
        break;
    }

    if (L1IsMoving())
    {
        switch (lpUnit->currentPose)
        {
        case UP_LAY:
            fpModSound *= AI_LAY_SOUND;
            break;
        case UP_CRAWL:
            fpModSound *= AI_CRAWL_SOUND;
            break;
        }
        if (lpUnit->IsRunning())
        {
            fpModSound *= AI_RUN_SOUND;
            fpSModSound *= AI_RUN_SENSE_SOUND;
        }
    }
    else
        fpModSound = 0;
    if (L1IsAttacking())
    {
        // для кастования сферы senses увеличивать заметность не надо
        if (curCommand.cmd == UC_CASTPOINT || curCommand.cmd == UC_CASTUNIT)
        {
            CSpell *pSpell = GetSpell(curCommand.nQSpellNum);
            ASSERT(pSpell);
            if (pSpell && pSpell->GetMagicSchool() != RPGMS_SENSES)
                fpModVision *= AI_ATTACK_VISION;
        }
        //
    }
    CalcNewFootstepGenInterval();
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! DAMAGE, processing of some actions concerning health of unit
/////////////////////////////////////////////////////////////////////////////////////
// перебрать всех друзей из units и если они видят pTest, то pEnemy становится также им
// виден и к тому же становится их врагом
void CUnitServer::MakeNewEnemies(CMapObjectPtrList &units, CUnit *pTest, CUnit *pEnemy)
{
    int nCurrentPlayer, nOtherPlayer;
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CFofTracker &fof = ((CWorldServer *)lpUnit->GetWorld())->fof;
    nCurrentPlayer = lpUnit->GetPlayerNum();

    for (POSITION pos = units.GetHeadPosition(); pos;)
    {
        CUnit *pBrother = units.GetNext(pos);
        nOtherPlayer = pBrother->GetPlayerNum();
        if (fof.IsPlayerAlly(nOtherPlayer, nCurrentPlayer))
        {
            // прочувствуем кого видит наш брат
            CUnitServer *pBr = (CUnitServer *)pBrother->lpUnitSpecific;
            pBrother->SetFlag(UNIT_LOOKATALLIES);
            CAIUnitEnvironment &brotherEnv = pBr->logic.environment;
            pWS->aiMap.marks.Feel(pBr, brotherEnv);
            // если он видел pTest, то он поимел нового врага
            if (brotherEnv.visibleUnits.Find(pTest))
            {
                if (!brotherEnv.visibleUnits.Find(pEnemy))
                {
                    brotherEnv.visibleUnits.AddTail(pEnemy);
                    aiLog << "Unit " << pBrother->GetID()
                          << " has seen murder and is forced to see unit " << pEnemy->GetID()
                          << endl;
                }
                pWS->fof.MakeDamage(pEnemy, pBrother, 10);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// переберем всех ближайших друзей, если кто-либо из них видел в этот момент того кого
// атаковали или того кто атаковал, то они становятся врагами pAttacker`a
void CUnitServer::AlarmEverybodyWhoSawDeath(CUnit *pAttacker)
{
    if (!pAttacker || pAttacker->WhoAmI() != HPC_UNIT)
        return;
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CMapObjectPtrList &nearUnits =
        *logic.GetUnitsList(); // using nearUnits from logic, this is reasonable and fast
    CUnitServer *pAttackerS = (CUnitServer *)pAttacker->GetUnitSpecific();

    // все кто видел жертву
    MakeNewEnemies(logic.GetUnitsList()[0], GetUnit(), pAttacker);
    // все кто видел атакера
    MakeNewEnemies(pAttackerS->logic.GetUnitsList()[0], pAttacker, pAttacker);
}
/////////////////////////////////////////////////////////////////////////////////////
namespace {
std::string GenocideMobName(const CString& mobFile){
 std::string name=(LPCSTR)mobFile;
 const auto slash=name.find_last_of("\\/");
 if(slash!=std::string::npos)name.erase(0,slash+1);
 const auto extension=name.find_last_of('.');
 if(extension!=std::string::npos)name.erase(extension);
 for(char& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
 return name;
}
void TrackGenocidePlayerKill(CWorldServer& world,CUnit& victim,CUnit* attacker){
 auto* mod=ModRuntime::Current();
 if(!mod||!mod->CombatSetting("JMVGenocidePlayerKillTracking")||!attacker)return;
 CPlayer* player=world.GetPlayer(0);
 if(!player||attacker->GetOwner()!=player||victim.GetOwner())return;
 const std::string mobName=GenocideMobName(world.szMobFileName);
 ModGenocideConfig config;
 if(mobName.empty()||!mod->GenocideMapConfig(mobName.c_str(),config))return;
 if(victim.IsInvalid())return;
 const std::string counter="jmv.genocide.kills."+config.resultVar;
 float stored=player->party.vars.GetVar(counter.c_str());
 if(!(stored>=0))stored=0;
 if(stored>config.population)stored=float(config.population);
 const int oldKills=int(stored);
 const int kills=oldKills<config.population?oldKills+1:config.population;
 player->party.vars.SetVar(counter.c_str(),float(kills));
 const float previous=player->party.vars.GetVar(config.resultVar.c_str());
 const int oldStage=previous>=2?2:previous>=1?1:0;
 const int stage=kills*100>=config.population*config.stageTwoPercent?2:
   kills*100>=config.population*config.stageOnePercent?1:0;
 if(stage>oldStage)player->party.vars.SetVar(config.resultVar.c_str(),float(stage));
 const int newStage=(std::max)(stage,oldStage);
 static const char* const messages[]={
  "\xC2\xE5\xEB\xE8\xEA\xE8\xE5\x20\xF1\xE8\xEB\xFB\x20\xF7\xF3\xFE\xF2\x20\xEA\xF0\xEE\xE2\xFC\x20\xED\xE0\x20\xE2\xE0\xF8\xE8\xF5\x20\xF0\xF3\xEA\xE0\xF5",
  "\xC2\xE5\xEB\xE8\xEA\xE8\xE5\x20\xF1\xE8\xEB\xFB\x20\xE2\xEE\xE7\xEC\xF3\xF9\xE5\xED\xFB\x20\xE2\xE0\xF8\xE5\xE9\x20\xEA\xF0\xEE\xE2\xEE\xE6\xE0\xE4\xED\xEE\xF1\xF2\xFC\xFE"
 };
 const std::string messageKey="jmv.genocide.msg."+config.resultVar;
 const float announced=player->party.vars.GetVar(messageKey.c_str());
 const int announcedStage=announced>=2?2:announced>=1?1:0;
 for(int next=announcedStage+1;next<=newStage;++next){
  if(ModMessages::Show(messages[next-1]))break;
  player->party.vars.SetVar(messageKey.c_str(),float(next));
 }
}
}
void CUnitServer::TakeDamage(CUnit *pAttacker, BOOL bAreaSpell)
{
    StopAnimationLoop();
    const bool wasDead = lpUnit->IsDead() != FALSE;
    CPlot3D ptDamageFrom;
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    // float fpForward, fpRight;
    switch (lpUnit->GetTypeID())
    {
    case MOTID_HUMAN:
    case MOTID_MONSTER:
    case MOTID_ANIMAL:
    case MOTID_TREE:
    case MOTID_BUILDING:
        break;
    default:
        ASSERT(0);
        lpUnit->Invalidate();
        return;
    }
    // определим степень повреждения героя
    if (((CMapObjectServer *)lpUnit->GetMapObjectSpecific())->IsDead())
    {
        // say ack
        if (pAttacker)
        {
            if (pAttacker->IsHero())
                ((CUnitServer *)pAttacker->GetUnitSpecific())
                    ->InformFailure(ACK_UNIT_BECOME_KILLER);
            else if (lpUnit->IsHero())
                ((CUnitServer *)pAttacker->GetUnitSpecific())
                    ->acknowledgements.SayAck(AI_ACK_KILLED);
        }
        //
        AlarmEverybodyWhoSawDeath(pAttacker);
        // this boy is done
        lpUnit->RemoveAllSpellEffects();

        LowSetDead(true);
        //
        aiLog << "Unit " << lpUnit->GetID() << " damaged to death" << endl;
        // play last animation
        SL0AnimationSeq suffer;
        GetSufferAnimation(suffer, CalcSufferDirection(pAttacker, bAreaSpell), true, false);
        L0FreezeMovement(false);
        L0Freeze(false);
        L1StartSpecial(suffer);
        pWS->aiMap.marks.RemoveFootSteps(this);

        CPlayer *pPlayer = lpUnit->GetOwner();
        if (pPlayer)
        {
            // some lpUnit allies should see his corpse
            CUnitPtrHashIterator unitIter;
            CUnit *pAlly = unitIter.GetFirst(&pPlayer->unitsUnderControl);
            while (pAlly && pAlly == lpUnit)
                pAlly = unitIter.GetNext();
            if (pAlly)
                ((CUnitServer *)pAlly->GetUnitSpecific())
                    ->logic.environment.SeeNewCorpse(pAlly, lpUnit);

        }
        if (!wasDead)
            TrackGenocidePlayerKill(*pWS, *lpUnit, pAttacker);
        return;
    }
    UseArmorSpells(false);
    // modify friend or foe information
    if (pAttacker)
    {
        if (pAttacker->WhoAmI() == HPC_UNIT)
            pWS->fof.MakeDamage(pAttacker, lpUnit, 10); // 10 - amount of damage
    }
    //
    logic.TakeDamage(pAttacker);
    lpUnit->SetFlag(UNIT_RECALCENEMY);
    RecalcEnemy();
    L1NeedPlaySuffer(CalcSufferDirection(pAttacker, bAreaSpell));
}
/////////////////////////////////////////////////////////////////////////////////////
// warning, non magic attack will be modified by this function, use with care
bool CUnitServer::DetermineHit(SRPGAttackPortion *pAttackPortion, CUnit *pAttacker,float accuracyBonus)
{
    ASSERT(fabs(pAttackPortion->fpToHit) < FLT_MAX);
    float fToHit = FLT_MAX;
    bool bMagicAttack = pAttackPortion->IsMagicAttack();
    if(auto* owner=ModRuntime::Current();owner&&!owner->CombatSetting("JMVAttacks")&&owner->CombatSetting("JMVShit"))
        if(owner->CustomCriticalHit(*pAttackPortion))bMagicAttack=false;
    auto*mod=ModRuntime::Current();
    const bool conditional=mod&&ModConditionalCombat::Fallback(mod->CombatSetting("JMVShit")!=0,mod->CombatSetting("JMVAttacks")!=0);
    if(mod){auto weapon=mod->AttackWeapon(*pAttackPortion);if(weapon&&(weapon->GetWeaponType()==RPGWST_HANDS3||weapon->GetWeaponType()==RPGWST_HANDS7))bMagicAttack=false;}
    if (!bMagicAttack)
    {
        // if unit is in aggressive mode then backstab is failed
        if (IsAggressive())
            pAttackPortion->fBackStabMultiplier = 1;
        if (pAttacker)
            fToHit = GetToHit(static_cast<CUnitServer *>(pAttacker->GetUnitSpecific()),
                              pAttackPortion->fBackStabMultiplier > 1);
        // Пенальти за избранный удар - 10, при ударе в голову - 25
        if (pAttackPortion->bCalledShot)
        {
            if(mod&&(conditional||mod->CombatSetting("JMVAttacks")))fToHit=ModConditionalCombat::Location(fToHit,mod->HitLocationModifier(int(pAttackPortion->hl)));
            else if (pAttackPortion->hl == RPGHL_HEAD)
                fToHit = Max(0.0f, fToHit - 25.0f);
            else if (pAttackPortion->hl != RPGHL_BODY)
                fToHit = Max(0.0f, fToHit - 10.0f);
        }
    }
    //
    if(!bMagicAttack)fToHit+=Max(0.f,accuracyBonus);
    SAttackParams apDefender;
    if(conditional)mod->ConditionalAttackParams(rpgUnitStats,*lpUnit,apDefender,false);else GetAttackParams(apDefender);
    //
    if (pAttackPortion->fBackStabMultiplier > 1)
        apDefender.nDefense = 0;
    //
    if (fToHit >= apDefender.nDefense)
        fToHit = FLT_MAX;
    else
        fToHit = -FLT_MAX;
    if(mod&&(conditional||mod->CombatSetting("JMVAttacks"))){
        float avoidance=0;bool avoid=false,stun=false;
        const auto&effects=lpUnit->GetSpellEffects();for(int i=0;i<effects.GetSize();++i){const auto&e=effects[i];if(e.nSpellID==SPELL_ID_STUN)stun=true;if(e.nSpellID==SPELL_ID_SHRINK){avoid=true;avoidance=Max(avoidance,e.fpPower);}}
        // The shared predicate retains the unconditional native CRT draw.
        if(ModConditionalCombat::Avoidance(avoid,avoidance,pAttackPortion->fBackStabMultiplier,stun,[](){return float((rand()*100/RAND_MAX)%100);})){fToHit=-FLT_MAX;rpgUnitStats.bFlags|=RPGSTAT_SHOWHPFLY|RPGSTAT_PARRY;if(auto*sound=theWorld.GetSoundManager())sound->BeginFX("weapons\\miss.wav",1000,lpUnit->GetCP(),8,40,FALSE);}
    }
    pAttackPortion->fpToHit = fToHit; // store new 'to-hit' value in the attack portion
    return fToHit > 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// warning, non magic attack will be modified by this function, use with care
bool CUnitServer::ApplyDirectDamage(float amount)
{
    if (!ModInvincible::UnitValid(lpUnit, true) || !std::isfinite(amount) || amount <= 0 ||
        (ModInvincible::Mask(lpUnit) & ModInvincible::Direct)) return false;
    auto* common = GetMapObjectServer()->GetRPGStats();
    const float before = common->ssHP.fpCurrent, maximum = common->ssHP.fpMax;
    if (!std::isfinite(before) || !std::isfinite(maximum) || maximum <= 0) return false;
    const auto* owner = lpUnit->GetOwner();
    const auto publish = [&]() {
        rpgUnitStats.bFlags |= RPGSTAT_SHOWHPFLY;
        AddUpdateMask(CMapObject::VLD_MASK_STATS);
        AddUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
    };
    const auto finishDeath = [&]() {
        for (auto& limb : rpgUnitStats.hls)
            if (limb.state >= RPGALS_CRIPPLED && limb.state <= RPGALS_ACTIVE) limb.fpHP = 0;
        common->ssHP.fpCurrent = 0;
        publish();
        // Native finalizer: death/AI/corpse/animation, no attack or resistance reroll.
        TakeDamage(nullptr, FALSE);
        common->ssHP.fpCurrent = 0;
        return lpUnit->IsDead() != FALSE;
    };
    // A preceding native contact can leave a live actor with zero HP.
    if (before <= 0) return owner && owner->bGod ? false : finishDeath();
    float saved[RPG_NUM_HIT_LOCATIONS];
    int active = 0;
    for (unsigned i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i) {
        const auto& limb = rpgUnitStats.hls[i];
        if (limb.state < RPGALS_NOTUSED || limb.state > RPGALS_ACTIVE) return false;
        saved[i] = limb.fpHP;
        // Unused golem parts may contain uninitialised weights, ignored by CollectHP.
        if (limb.state == RPGALS_NOTUSED) continue;
        if (!std::isfinite(limb.fpHP) || !std::isfinite(limb.fpMaxHP) || !std::isfinite(limb.fpVitality)) return false;
        if (limb.fpMaxHP > 0 && limb.fpHP > 0 && limb.fpVitality > 0) ++active;
    }
    if (!active) return false;
    float target = (std::fmax)(0.f, before - amount);
    if (target == 0 && owner && owner->bGod) target = (std::min)(before, 1.f);
    const float initial = rpgUnitStats.CollectHP(maximum);
    if (!std::isfinite(initial) || initial < target - .001f) return false;
    float low = 0, high = 1;
    for (int iteration = 0; iteration < 24; ++iteration) {
        const float fraction = (low + high) * .5f;
        for (unsigned i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i) {
            auto& limb = rpgUnitStats.hls[i];
            if (limb.state >= RPGALS_CRIPPLED && limb.fpMaxHP > 0 && limb.fpVitality > 0)
                limb.fpHP = saved[i] * (1 - fraction);
        }
        if (rpgUnitStats.CollectHP(maximum) > target) low = fraction;
        else high = fraction;
    }
    const float actual = rpgUnitStats.CollectHP(maximum);
    if (!std::isfinite(actual) || std::abs(actual - target) > (std::fmax)(.001f, maximum * .000002f)) {
        for (unsigned i = 0; i < RPG_NUM_HIT_LOCATIONS; ++i) rpgUnitStats.hls[i].fpHP = saved[i];
        return false;
    }
    common->ssHP.fpCurrent = target;
    publish();
    GameJournal::ObserveDamage(nullptr, lpUnit, before - target, 1, -1);
    if (target == 0) return finishDeath();
    return true;
}
BOOL CUnitServer::ProcessCustomAttackPortion(SRPGAttackPortion *portion,CUnit *attacker,CUnit *experience)
{
    auto *mod=ModRuntime::Current();
    bool inherited=mod&&ModConditionalCombat::DownstreamFallback(
        mod->CombatSetting("JMVShit")!=0,mod->CombatSetting("JMVAttacks")!=0,
        ModConditionalCombat::DamageCall::DirectCustom);
    return ProcessAttackPortionOwned(portion,attacker,experience,inherited,true);
}
BOOL CUnitServer::ProcessAttackPortion(SRPGAttackPortion *portion,CUnit *attacker,CUnit *experience)
{
    auto *mod=ModRuntime::Current();
    bool inherited=mod&&ModConditionalCombat::DownstreamFallback(
        mod->CombatSetting("JMVShit")!=0,mod->CombatSetting("JMVAttacks")!=0,
        ModConditionalCombat::DamageCall::RecipientVirtual);
    return ProcessAttackPortionOwned(portion,attacker,experience,inherited,false);
}
BOOL CUnitServer::ProcessAttackPortionOwned(SRPGAttackPortion *lpAttackPortion,CUnit *pAttacker,
                                          CUnit *pExpDest,bool inheritedDefense,bool explicitCustom)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    int skillImpact=ModRuntime::Current()?ModRuntime::Current()->TakeSkillImpact(*lpAttackPortion):0;
    float beforeSkillHp=GetMapObjectServer()->GetRPGStats()->ssHP.fpCurrent;
    const auto deliveredAttack = ModAttackEvents::Begin(*lpAttackPortion,pAttacker,lpUnit,beforeSkillHp);
    const auto completeAttack = [&](BOOL result) {
        ModAttackEvents::Complete(*pWS,deliveredAttack,GetMapObjectServer()->GetCurrentHP());
        return result;
    };
    ModInvincible::ScopedChannels protectedChannels(*lpAttackPortion, ModInvincible::Mask(lpUnit) & 0x7f);
    int journalCritical = -1;
    BOOL bWasCorpse = GetUnit()->IsDead();
    bool bHadOwner = GetUnit()->GetOwner() != 0;
    //
    // if ToHit is not initiated then calc it
    if (fabs(lpAttackPortion->fpToHit) < FLT_MAX)
        DetermineHit(lpAttackPortion, pAttacker);
    //
    if (!bWasCorpse)
    {
        pWS->AddObjectToActiveSet(GetUnit());
        if (pAttacker)
            aiLog << "Unit " << lpUnit->GetID() << " was attacked by " << pAttacker->GetID();
        else
            aiLog << "Unit " << lpUnit->GetID() << " was attacked by xz";
        aiLog << " skill=" << lpAttackPortion->fpToHit << " damage=" << lpAttackPortion->fpDamage;
        if (lpAttackPortion->fBackStabMultiplier > 1.0f)
            aiLog << " backstab with power=" << lpAttackPortion->fBackStabMultiplier;
        if (lpAttackPortion->bCalledShot)
            aiLog << " called shot";
        aiLog << " destHL=";
        switch (lpAttackPortion->hl)
        {
        case RPGHL_HEAD:
            aiLog << "Head!";
            break;
        case RPGHL_BODY:
            aiLog << "Body";
            break;
        case RPGHL_LHAND:
            aiLog << "LHand";
            break;
        case RPGHL_RHAND:
            aiLog << "RHand";
            break;
        case RPGHL_LLEG:
            aiLog << "LLeg";
            break;
        case RPGHL_RLEG:
            aiLog << "RLeg";
            break;
        case RPGHL_ALL:
            aiLog << "all";
            break;
        default:
            aiLog << "xz";
            break;
        }
        aiLog << endl;
    }
    // are not we in god mode?
    CPlayer *pPlayer = GetUnit()->GetOwner();
    if (pPlayer)
    {
        if (pPlayer->bGod >= 2)
            return completeAttack(FALSE);
        if (ModConditionalCombat::RejectOtherExperienceOwner(inheritedDefense,
                pExpDest && pExpDest->GetOwner() && pPlayer != pExpDest->GetOwner()))
            return completeAttack(FALSE);
    }
    //
    SAttackParams apDefender;
    ModConditionalCombat::QueryDefense(inheritedDefense,
        [&](){ModRuntime::Current()->ConditionalAttackParams(rpgUnitStats,*lpUnit,apDefender,true);},
        [&](){GetAttackParams(apDefender);});
    //
    if (lpAttackPortion->fBackStabMultiplier > 1)
        apDefender.nDefense = 0;
    if (ModConditionalCombat::CustomEquipmentPolicy(inheritedDefense))
    if (CModPackage *mod = ModRuntime::Current())
        if (!mod->CombatSetting("JMVBackstabDefenderEquipment"))
        {
            const CItemWeapon *weapon = lpUnit->GetCurrentQWeapon();
            if (weapon)
            {
                const ERPGWeaponTypes type = weapon->GetWeaponType();
                if ((type >= RPGWST_HANDS1 && type <= RPGWST_HANDS7) ||
                    type == RPGWST_BOW || type == RPGWST_CROSSBOW)
                    lpAttackPortion->fBackStabMultiplier = 1.0f;
            }
        }
    if (auto* historyOwner=ModRuntime::Current())
        historyOwner->CompensateHitHistory(rpgUnitStats,*lpUnit,pAttacker,*lpAttackPortion,apDefender.nDefense,explicitCustom);
    if (lpAttackPortion->fpToHit >= apDefender.nDefense)
    {
        rpgUnitStats.bFlags |= RPGSTAT_SHOWHPFLY;
        AddUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
        //
        UseArmorSpells(true);
        CItemWeapon *pAttackerWeapon = pAttacker != 0 ? pAttacker->GetCurrentQWeapon() : 0;
        auto* mod=ModRuntime::Current();
        BOOL damageApplied;
        if(mod&&mod->CombatSetting("JMVAttacks")){auto defense=mod->IncomingAttack(rpgUnitStats,*lpUnit,pAttacker,*lpAttackPortion,ModNativeCrtRandom::Random(100));auto weapon=mod->AttackWeapon(*lpAttackPortion);if(weapon)pAttackerWeapon=weapon.get();defense.critical=mod->ConsumeCritical(*lpAttackPortion);journalCritical=defense.critical?1:0;damageApplied=rpgUnitStats.ProcessModAttackPortion(lpUnit,GetMapObjectServer()->GetRPGStats(),lpAttackPortion,pAttackerWeapon,pAttacker,defense);}
        else if(mod&&ModCustomLower::Custom0(false,explicitCustom)){
            if(pAttacker&&pAttackerWeapon){int leftSlot=mod->CustomLeft(*pAttacker);auto*left=leftSlot>=0?pAttacker->GetQWeapon(leftSlot):nullptr;
                bool valid=left&&!left->IsBroken()&&left->GetWeaponType()!=RPGWST_HANDS1;
                if(ModCustomLower::SelectLeft(true,valid,[](){return int(random.Get(0,1));},[&](){return left->GetHP()/left->GetMaxHP()>pAttackerWeapon->GetHP()/pAttackerWeapon->GetMaxHP();}))pAttackerWeapon=left;
            }
            auto defense=mod->IncomingCustomAttack0(rpgUnitStats,*lpUnit,pExpDest,*lpAttackPortion);
            journalCritical=defense.critical?1:0;
            damageApplied=rpgUnitStats.ProcessModAttackPortion(lpUnit,GetMapObjectServer()->GetRPGStats(),lpAttackPortion,pAttackerWeapon,pExpDest,defense);
        }
        else damageApplied=rpgUnitStats.ProcessAttackPortion(lpUnit,GetMapObjectServer()->GetRPGStats(),lpAttackPortion,pAttackerWeapon);
        if (damageApplied)
        {
            if (pAttacker && !lpAttackPortion->IsMagicAttack())
            {
                if (pAttacker->GetTypeID() != MOTID_HUMAN)
                    lpUnit->rpgUserUnitStats.wAttackerPrototype = pAttacker->GetSubtypeID() +
                                                                  RPGWST_MAGIC;
                else
                    lpUnit->rpgUserUnitStats.wAttackerPrototype = static_cast<CUnitServer *>(
                                                                      pAttacker->GetUnitSpecific())
                                                                      ->GetCurrentWeaponType();
            }
            else
                lpUnit->rpgUserUnitStats.wAttackerPrototype = RPGWST_UNKNOWN;
            rpgUnitStats.RecalcStats(GetMapObjectServer()->GetRPGStats(), lpUnit);
            AddUpdateMask(CMapObject::VLD_MASK_STATS);
            AddUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);
            GameJournal::ObserveDamage(pAttacker, lpUnit, beforeSkillHp-GetMapObjectServer()->GetCurrentHP(), 1, journalCritical);
            if (!bWasCorpse)
            {
                TakeDamage(pAttacker, lpAttackPortion->bAreaEffect);
                if(skillImpact==ModSkills::ImpactCripple&&!lpUnit->IsDead()&&mod)mod->ApplyCripple(*lpUnit);
                if(skillImpact==ModSkills::ImpactPowerHit&&pAttacker)PowerHitFeedback(pAttacker,beforeSkillHp);
                if(pAttacker&&beforeSkillHp>GetMapObjectServer()->GetRPGStats()->ssHP.fpCurrent&&
                   (skillImpact==ModSkills::ImpactCripple||skillImpact==ModSkills::ImpactPreciseHit)){
                    const char* resource=skillImpact==ModSkills::ImpactCripple?"skills\\cripplinghit\\1.wav":"skills\\precisehit\\1.wav";
                    if(auto*sound=theWorld.GetSoundManager())sound->BeginFX(resource,8000,lpUnit->GetCP(),5,25,FALSE);
                }
                // if unit was killed pExpDest will get exp for that
                // killing units with owner does not give you exp
                if (!bWasCorpse && GetUnit()->IsDead() && pExpDest && pExpDest->GetOwner() &&
                    !bHadOwner)
                {
                    if(mod)
                        pWS->GainExperience(pExpDest->GetOwner(),ModSpells::KillExperience(GetUnit()->GetPrototype()->fExp,mod->CombatSetting("JMVExpMult")));
                    else pWS->GainExperience(pExpDest->GetOwner(), GetUnit()->GetPrototype()->fExp);
                }
            }
            return completeAttack(TRUE);
        }
    }
    // modify friend or foe information
    if (pAttacker)
    {
        if (pAttacker->WhoAmI() == HPC_UNIT)
            ((CWorldServer *)GetWorld())
                ->fof.MakeDamage(pAttacker, lpUnit, 10); // 10 - amount of damage
    }
    //
    GameJournal::ObserveDamage(pAttacker, lpUnit, 0, lpAttackPortion->fpToHit < apDefender.nDefense ? 0 : 2, journalCritical);
    logic.TakeDamage(pAttacker);

    return completeAttack(FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CUnitServer::Heal(float fpPower)
{
    if (fpPower > 0 && (ModInvincible::Mask(lpUnit) & ModInvincible::Buffs)) return FALSE;
    const float journalBeforeHP=GetMapObjectServer()->GetCurrentHP();
    if (rpgUnitStats.Heal(GetMapObjectServer()->GetRPGStats(), fpPower))
    {
        rpgUnitStats.RecalcStats(GetMapObjectServer()->GetRPGStats(), lpUnit);
        AddUpdateMask(CMapObject::VLD_MASK_STATS);
        AddUpdateMask(CMapObject::VLD_MASK_UNIT_STATS);

        GameJournal::ObserveHeal(lpUnit, GetMapObjectServer()->GetCurrentHP()-journalBeforeHP);
        aiLog << "Unit " << lpUnit->GetID() << " was healed" << endl;
        return TRUE;
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// !!! New commands from user
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::InitializeUnit()
{
    curCommand.Clear();
    CmdWait();
    currentPose.Convert(lpUnit->currentPose);
    wishPose.Convert(lpUnit->wishPose);
    L1ResetState();
    lpUnit->RecalcAIParams();
    // select first weapon
    lpUnit->dwWishWeapon = 0;
    L1ChangeWeapon();
    L1StartIdle();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::CmdRotate(const CPlot3D &vecDest, float fpAngSpeed)
{
    const auto facing=-lpUnit->GetYAxis();
    GameJournal::Event("CMD","rotate","unit=%d requested=%.6f previous=%.6f speed=%.6f",
                       lpUnit->GetID(),std::atan2(vecDest.y,vecDest.x),std::atan2(facing.y,facing.x),fpAngSpeed);
    nextCommand.Clear();
    nextCommand.cmd = UC_ROTATE;
    nextCommand.ptTarget = vecDest;
    nextCommand.pTarget = NULL;
    nextCommand.fpParam = fpAngSpeed;
    nextCommand.bDoItFast = false;
}

void CUnitServer::CmdMoveTo(const CPlot3D &ptDest, bool bRun, bool bGroupCmd, float fpMaxPathDist,const Navigation::Location* goalSupport)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();

    nextCommand.Clear();
    nextCommand.cmd = UC_MOVETO;
    nextCommand.ptTarget = ptDest;
    if(goalSupport){nextCommand.navGoal=*goalSupport;nextCommand.navGoalValid=true;}
    nextCommand.pTarget = NULL;
    nextCommand.bDoItFast = bRun;
    nextCommand.nQSpellNum = bGroupCmd ? 1 : 0;
    nextCommand.dwParam = 0;
    nextCommand.ptTarget.z = fpMaxPathDist;

    CPlayer *pPlayer = lpUnit->GetOwner();
    if (pPlayer && ptDest.z != -1313)
    {
        if (!pWS->IsCombatPause())
            nextCommand.dwParam = 1;
        else
        {
            CmdShowMove(ptDest, bGroupCmd, bRun, fpMaxPathDist);
        }
    }
}

void CUnitServer::CmdShowMove(const CPlot3D &ptDest, bool bGroupCmd, bool bRun, float fpMaxPathDist)
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();

    SAIPath pathFake;
    CUnitList removed;
    LowRemoveUnitsForPathFind(removed, ptDest, bGroupCmd);
    // change pose to stand in case of run pathfind
    EUnitPose prevPose = lpUnit->currentPose;
    if (bRun && lpUnit->currentPose != UP_STAND && lpUnit->currentPose != UP_RUN)
    {
        pWS->aiMap.RemoveObject(lpUnit);
        lpUnit->currentPose = UP_STAND;
        lpUnit->RecalcAIParams();
        if (!pWS->aiMap.CanPutUnitRelaxed(lpUnit))
            lpUnit->currentPose = prevPose;
        pWS->aiMap.AddObject(lpUnit);
    }
    //
    bool bFound = LowFindPath(ptDest, pathFake, pWS->aiMap.IsWater(lpUnit->GetCP()), fpMaxPathDist,
                             lpUnit->IsHero()?Navigation::GoalPolicy::ClosestReachable:Navigation::GoalPolicy::Exact,
                             nextCommand.navGoalValid?&nextCommand.navGoal:nullptr);
    //
    if (bRun)
    {
        pWS->aiMap.RemoveObject(lpUnit);
        lpUnit->currentPose = prevPose;
        lpUnit->RecalcAIParams();
        pWS->aiMap.AddObject(lpUnit);
    }
    //
    for (POSITION pos = removed.GetHeadPosition(); pos;)
        pWS->aiMap.AddObject(removed.GetNext(pos));
    ShowFoundMovePath(pathFake, bFound);
}

void CUnitServer::CmdAttack(CMapObject *pTarget, ERPGHitLocations hl, bool bRun, DWORD dwAck)
{
    ASSERT(pTarget != lpUnit);
    if (pTarget == 0 || pTarget == lpUnit)
        return;
    if (pTarget->WhoAmI() == HPC_UNIT && hl < RPGHL_ALL)
    {
        CUnit *pT = (CUnit *)pTarget;
        CUnitServer *pUS = (CUnitServer *)pT->GetUnitSpecific();
        if (pUS->rpgUnitStats.hls[hl].state != RPGALS_ACTIVE)
            hl = RPGHL_ALL;
    }
    nextCommand.Clear();
    nextCommand.cmd = UC_ATTACK;
    nextCommand.pTarget = pTarget;
    nextCommand.nQSpellNum = (int)hl;
    nextCommand.bDoItFast = bRun;

    if (!IsTargetReachable(nextCommand))
    {
        InformFailure(ACK_HERO_REFUSE_TARGET_TOO_HIGH);
        nextCommand.Clear();
        return;
    }
    if (!FindAttackPath(nextCommand, dwAck))
        nextCommand.Clear();
}

void CUnitServer::CmdCast(int nQSpellNum, const CPlot3D &pointToCast, bool bRun)
{
    CPlot3D ptCastTo = pointToCast;
    if (IsPotion(nQSpellNum))
    {
        UsePotion(nQSpellNum, 0, pointToCast);
        return;
    }
    if (!CanCastSpell(GetSpell(nQSpellNum), (nQSpellNum & 0x80000000) == 0))
    {
        InformFailure(ACK_HERO_REFUSE_TOO_COMPLEX_SPELL_TO_CAST);
        return;
    }
    nextCommand.Clear();
    nextCommand.cmd = UC_CASTPOINT;
    nextCommand.pTarget = NULL;
    nextCommand.nQSpellNum = nQSpellNum;
    nextCommand.ptTarget = ptCastTo;
    nextCommand.bDoItFast = bRun;

    DWORD dwPositiveAck = IsCastPossible(nextCommand) ? ACK_HERO_ACK_CAST : ACK_NOTHING;
    if (!FindAttackPath(nextCommand, dwPositiveAck))
        nextCommand.Clear();
}

void CUnitServer::CmdCast(int nQSpellNum, CMapObject *pTarget, bool bRun)
{
    if(!pTarget)return;
    auto*mod=ModRuntime::Current();CUnit*target=pTarget->WhoAmI()==HPC_UNIT?static_cast<CUnit*>(pTarget):nullptr;
    CSpell*tacticalSpell=target&&ModTactical::Enabled(lpUnit)?ModTactical::Spell(lpUnit,nQSpellNum):nullptr;ModSkills::Config active;
    bool track=tacticalSpell&&!mod->SkillConfig(tacticalSpell,active);
    if(track&&!lpUnit->IsHero()&&!lpUnit->GetOwner()){
        if(ModTactical::KeepCommand(this,curCommand)||ModTactical::KeepCommand(this,nextCommand))return;
        if(ModTactical::ObsoleteHeal(this,tacticalSpell,target))return;
    }
    bool had=track&&ModTactical::HasCommand(this,nQSpellNum,target);
    CPlot3D ptCastTo = pTarget->GetTargetCP();
    if (IsPotion(nQSpellNum))
    {
        UsePotion(nQSpellNum, pTarget, pTarget->GetCP());
        return;
    }
    // lets check if this unit can cast the spell
    if (!CanCastSpell(GetSpell(nQSpellNum), (nQSpellNum & 0x80000000) == 0))
    {
        InformFailure(ACK_HERO_REFUSE_TOO_COMPLEX_SPELL_TO_CAST);
        return;
    }
    nextCommand.Clear();
    nextCommand.cmd = UC_CASTUNIT;
    nextCommand.pTarget = pTarget;
    nextCommand.nQSpellNum = nQSpellNum;
    nextCommand.ptTarget = CPlot3D(0, 0, 0);
    nextCommand.bDoItFast = bRun;

    DWORD dwPositiveAck = IsCastPossible(nextCommand) ? ACK_HERO_ACK_CAST : ACK_NOTHING;
    if (!FindAttackPath(nextCommand, dwPositiveAck))
        nextCommand.Clear();
    if(track&&!had&&ModTactical::HasCommand(this,nQSpellNum,target))ModTactical::Record(lpUnit,tacticalSpell,nQSpellNum,target);
}

bool CUnitServer::GroupSearchPathMatches(const CPlot3D &target, float tolerance)
{
    const CPlot3D end = path.GetFinalPoint();
    const float dx = path.ptDestination.x - target.x, dy = path.ptDestination.y - target.y;
    const float ex = end.x - target.x, ey = end.y - target.y;
    return !path.bRouteFailed && dx*dx + dy*dy < 0.0001f &&
        ex*ex + ey*ey <= tolerance*tolerance;
}
void CUnitServer::StopGroupSearchMotion()
{
    if (curCommand.cmd == UC_IDLE &&
        (nextCommand.cmd == UC_MOVETO || nextCommand.cmd == UC_ROTATE))
    {
        nextCommand.Clear();
        nextCommand.cmd = UC_IDLE;
        nextCommand.pTarget = nullptr;
        nextCommand.bDoItFast = false;
    }
    CmdWait();
}

void CUnitServer::CmdWait()
{
    if (curCommand.cmd == UC_IDLE)
        return;
    // CRAP
    if (nextCommand.cmd == UC_PLAYANIM || nextCommand.cmd == UC_PLAYANIMCYCLE)
        return;
    // end of CRAP
    nextCommand.Clear();
    nextCommand.cmd = UC_IDLE;
    nextCommand.pTarget = NULL;
    nextCommand.bDoItFast = false;
}

void CUnitServer::CmdRest()
{
    if (curCommand.cmd == UC_REST)
        return;
    nextCommand.Clear();
    nextCommand.cmd = UC_REST;
    nextCommand.pTarget = NULL;
    nextCommand.bDoItFast = false;
}

void CUnitServer::CmdNewPose(EUnitPose pose)
{
    if (lpUnit->GetOwner())
        GameJournal::Event("CMD", "pose", "unit=%d current=%d requested=%d at=(%.3f,%.3f)",
                           lpUnit->GetID(), int(currentPose.Get()), int(pose),
                           lpUnit->GetCPX(), lpUnit->GetCPY());
    nBoredCounter = 0;
    lpUnit->wishPose = pose;
    if (LcCanChangePose(pose))
    {
        if (pose != UP_RUN || rpgUnitStats.CanRun(true) == ECR_CAN_RUN)
            InformFailure(ACK_HERO_ACK_CHANGE_POSE);
    }
    if (pose != UP_STAND && pose != UP_RUN)
        curCommand.bDoItFast = false;
}

void CUnitServer::CmdNewWeapon(DWORD dwWeapon)
{
    nBoredCounter = 0;
    lpUnit->dwWishWeapon = dwWeapon;
}

void CUnitServer::CmdSkill(CMapObject *pTarget, int nSkill, DWORD dwParam, bool bRun, DWORD dwAck)
{
    nextCommand.Clear();
    nextCommand.cmd = UC_SKILL;
    nextCommand.pTarget = pTarget;
    nextCommand.nQSpellNum = nSkill;
    nextCommand.dwParam = dwParam;
    nextCommand.bDoItFast = bRun;
    // if ( curCommand.cmd != UC_SKILL )
    // oldPose = lpUnit->wishPose;

    if (!FindAttackPath(nextCommand, dwAck))
        nextCommand.Clear();
}

void CUnitServer::CmdSteal(CUnit *pTarget, bool bRun)
{
    // check for availability of stealing
    //	if ( rpgUnitStats.cManualSkills[ RPGMSKL_STEALING ] == 0 )
    //		return;
    if (!pTarget || pTarget->IsDead() || pTarget == lpUnit)
        return;
    CmdSkill(pTarget, RPGMSKL_STEALING, 0, bRun, ACK_HERO_ACK_STEAL_FROM_UNIT);
}

void CUnitServer::CmdTame(CUnit *pTarget, bool bRun)
{
    // check for availability of stealing
    //	if ( rpgUnitStats.cManualSkills[ RPGMSKL_TAME ] == 0 )
    //		return;
    if (!pTarget || pTarget->IsDead() || pTarget == lpUnit ||
        pTarget->GetOwner() == lpUnit->GetOwner())
        return;
    if (pTarget->GetSubtypeID() == MOTID_HUMAN)
        return;
    CmdSkill(pTarget, RPGMSKL_TAME, 0, bRun, ACK_HERO_ACK_USE_OBJECT);
}

void CUnitServer::CmdScience(CMapObject *pTarget, int nItemID, bool bRun)
{
    // check for availability of stealing
    // if ( rpgUnitStats.cManualSkills[ RPGMSKL_SCIENCE ] == 0 )
    //	return;
    if (!pTarget) //|| pTarget->IsDead() || pTarget == lpUnit )
        return;
    CmdSkill(pTarget, RPGMSKL_SCIENCE, (DWORD)nItemID, bRun, ACK_HERO_ACK_USE_OBJECT);
}

void CUnitServer::CmdLoot(CUnit *pTarget, bool bRun)
{
    if (!pTarget || !pTarget->IsDead() || pTarget == lpUnit)
        return;
    CmdSkill(pTarget, RPGMSKL_LOOT, 0, bRun, ACK_HERO_ACK_LOOT);
}

bool CUnitServer::HasLeverRingChoice(const SUnitBasicCmd& cmd) const
{
    if(!modLeverRing||!modLeverTarget||!cmd.IsScience()||!cmd.HasPointOfUse()||
        static_cast<CMapObject*>(cmd.pTarget)!=modLeverTarget||cmd.pTarget->GetRTID()!=modLeverId)return false;
    const auto delta=cmd.ptTarget-modLeverPoint;
    return delta.x==0&&delta.y==0&&delta.z==0;
}

void CUnitServer::QueueAnimationLoop(DWORD id)
{
    nextCommand.Clear();
    nextCommand.dwParam=id;
    nextCommand.cmd=UC_PLAYANIMCYCLE;
}

void CUnitServer::StopAnimationLoop()
{
    CGSVariableSet* vars;std::string key;DWORD id;
    if(!ModAnimationLoop::Read(this,vars,key,id))return;
    vars->DelVar(key.c_str());
    const bool pending=ModAnimationLoop::Matches(nextCommand,id);
    const bool current=ModAnimationLoop::Matches(curCommand,id)||
        (pending&&curCommand.cmd==UC_PLAYANIMCYCLE);
    if(current&&eL1State==UL1_SPECIAL_CYCLE)L1SetPhase(UL1_NONE);
    if(pending)nextCommand.Clear();
    if(current&&nextCommand.cmd==UC_NONE){
        nextCommand.Clear();nextCommand.cmd=UC_IDLE;nextCommand.bDoItFast=true;
    }
}

void CUnitServer::UpdateAnimationLoop()
{
    CGSVariableSet* vars;std::string key;DWORD id;
    if(!ModAnimationLoop::Read(this,vars,key,id))return;
    if(lpUnit->IsDead()||lpUnit->IsInvalid()){vars->DelVar(key.c_str());return;}
    const bool pending=ModAnimationLoop::Matches(nextCommand,id);
    const bool current=ModAnimationLoop::Matches(curCommand,id);
    if((!ModAnimationLoop::Housekeeping(nextCommand.cmd)&&!pending)||
        (!pending&&!current&&!ModAnimationLoop::Housekeeping(curCommand.cmd))){
        StopAnimationLoop();return;
    }
    if(pending){
        if(eL1State==UL1_SPECIAL_CYCLE&&curCommand.cmd==UC_PLAYANIMCYCLE)L1SetPhase(UL1_NONE);
        return;
    }
    if(current&&eL1State==UL1_SPECIAL_CYCLE){
        if(nextCommand.cmd!=UC_NONE)nextCommand.Clear();
        return;
    }
    QueueAnimationLoop(id);
}

void CUnitServer::CmdPlayAnimation(const char *pszAnimation)
{
    const SADEntry *pEntry;
    pEntry = ((CMapObjectManager *)GetUnit()->GetWorld()->GetMapObjectManager())
                 ->GetAnimationEntry(GetUnit()->GetMaskName(), pszAnimation);
    if (!pEntry)
        return;
    nextCommand.Clear();
    nextCommand.dwParam = pEntry->dwEntryIndex;
    if (pEntry->dwFlags & ADB_STAGE_CYCLE)
        nextCommand.cmd = UC_PLAYANIMCYCLE;
    else
        nextCommand.cmd = UC_PLAYANIM;
    nextCommand.bDoItFast = false;
}

void CUnitServer::CmdUseTrap(int nQItemNum)
{
    nextCommand.Clear();
    nextCommand.cmd = UC_TRAP;
    nextCommand.nQSpellNum = nQItemNum;
    nextCommand.bDoItFast = false;
}

void CUnitServer::InterruptCommand()
{
    L1SetPhase(UL1_NONE);
    level1Command.Clear();
    L1StandStill();
    curCommand.Clear();
    nextCommand.Clear();
    CmdWait();
}

/////////////////////////////////////////////////////////////////////////////////////
void CUnitServer::GetAttackParams(SAttackParams &params) const
{
    if(auto*mod=ModRuntime::Current()){mod->AttackParams(rpgUnitStats,*lpUnit,params,float(ModNativeCrtRandom::Random(100)));return;}
    bool bHero = IsIDofPersonage(lpUnit->GetID());
    CItemWeapon *pWeapon = lpUnit->GetCurrentQWeapon();
    int i;

    if (!bHero || pWeapon == 0)
    {
        params.fDamageMin = rpgUnitStats.fpNAWDamageMin;
        params.fDamageMax = rpgUnitStats.fpNAWDamageMin + rpgUnitStats.fpNAWDamageRnd;
        params.nAttack = rpgUnitStats.fpNAWToHit;
        params.nDefense = rpgUnitStats.fpNAWParry;
        params.fWeaponWeight = rpgUnitStats.fpNAWWeight;
        //
        if (pWeapon != 0) // get attack and weapon type from the weapon
        {
            params.rpgWeaponType = pWeapon->GetWeaponType();
            params.rpgAttackType = CItemWeapon::GetAttackType(pWeapon->GetSubtypeID());
        }
        else // get attack and weapon type from the race model
        {
            params.rpgWeaponType = rpgUnitStats.rpgNAWWeaponType;
            const SUDBRaceModel *pRaceModel = lpUnit->GetRaceModel();
            for (i = 0; i < RPG_NUM_ATTACK_TYPES; ++i)
            {
                if (pRaceModel->attacks[i] != 0)
                {
                    params.rpgAttackType = static_cast<ERPGAttackTypes>(i);
                    break;
                }
            }
        }
    }
    else
    {
        params.fDamageMin = pWeapon->GetDamageMin();
        params.fDamageMax = pWeapon->GetDamageMax();
        params.rpgWeaponType = pWeapon->GetWeaponType();
        params.rpgAttackType = CItemWeapon::GetAttackType(pWeapon->GetSubtypeID());
        params.fWeaponWeight = pWeapon->CItem::GetWeight();
        params.nAttack = rpgUnitStats.GetWeaponSkillLevel(
                             params.rpgWeaponType,
                             const_cast<CUnit *>(const_cast<CUnitServer *>(this)->GetUnit())) +
                         pWeapon->GetAtackBonus();
        params.nDefense = rpgUnitStats.GetWeaponSkillLevel(
                              params.rpgWeaponType,
                              const_cast<CUnit *>(const_cast<CUnitServer *>(this)->GetUnit())) +
                          pWeapon->GetDefenceBonus();
        //
        params.rpgAttackType = CItemWeapon::GetAttackType(pWeapon->GetSubtypeID());
    }
    // we can't defend with the bow class weapons
    if ((params.rpgWeaponType == RPGWST_BOW) || (params.rpgWeaponType == RPGWST_CROSSBOW))
        params.nDefense = 0;
    // correct damage params with spells
    params.fDamageMax = rpgUnitStats.GetModifiedDamage(lpUnit, params.fDamageMax);
    params.fDamageMin = rpgUnitStats.GetModifiedDamage(lpUnit, params.fDamageMin);
    //
    params.fToHitRandom = rpgUnitStats.fpNAWToHitRandom;
    //
    params.ApplyDifficultyLevel(bHero);
}
/////////////////////////////////////////////////////////////////////////////////////
// return random to-hit value
float CUnitServer::GetToHit(CUnitServer *pAttacker, bool bBackstab) const
{
    SAttackParams apAttacker, apDefender;
    auto*mod=ModRuntime::Current();
    const int spellAddonHitMode = mod ? mod->SpellAddon().Feature19() : 0;
    const bool spellAddonHit = spellAddonHitMode >= 1;
    const float rangeDivisor = spellAddonHit
        ? mod->SpellAddon().Scalar(ModSpellAddon::ScalarKey::gameplay_range) : RPG_CONST_Z;
    if(mod&&ModConditionalCombat::Fallback(mod->CombatSetting("JMVShit")!=0,mod->CombatSetting("JMVAttacks")!=0)){
        mod->ConditionalAttackParams(*pAttacker->GetRPGStats(),*pAttacker->lpUnit,apAttacker,true);
        mod->ConditionalAttackParams(*const_cast<CUnitServer*>(this)->GetRPGStats(),*lpUnit,apDefender,true);
        float distance = spellAddonHit
            ? NumericPolicy::DistanceModifier(fabs(lpUnit->GetCP()-pAttacker->lpUnit->GetCP())*2.f,
                                         rangeDivisor, lpUnit->GetAttackDistance())
            : fabs(lpUnit->GetCP()-pAttacker->lpUnit->GetCP())*2.f/(RPG_CONST_Z*lpUnit->GetAttackDistance()*2.f);
        float jitter=apAttacker.fToHitRandom>0?2.f*random.Get(MINT(apAttacker.fToHitRandom))-(apAttacker.fToHitRandom+1):0.f;
        return ModConditionalCombat::Base(float(apAttacker.nAttack),jitter,distance);
    }
    pAttacker->GetAttackParams(apAttacker);
    GetAttackParams(apDefender);
    bool jmv=ModRuntime::Current()&&ModRuntime::Current()->CombatSetting("JMVAttacks");
    float fWeightBonus = spellAddonHit
        ? NumericPolicy::WeightBonus(mod->SpellAddon().Scalar(ModSpellAddon::ScalarKey::gameplay_weight),
                               apAttacker.fWeaponWeight, apDefender.fWeaponWeight)
        : apAttacker.fWeaponWeight / (RPG_CONST_Y * apDefender.fWeaponWeight);
    float fDistanceModifier = spellAddonHit
        ? NumericPolicy::DistanceModifier(fabs(lpUnit->GetCP() - pAttacker->lpUnit->GetCP()) * 2.0f,
                                     rangeDivisor, lpUnit->GetAttackDistance())
        : fabs(lpUnit->GetCP() - pAttacker->lpUnit->GetCP()) * 2.0f /
          (RPG_CONST_Z * lpUnit->GetAttackDistance() * 2.0f);
    float fToHitRandom = 0;
    if (apAttacker.fToHitRandom > 0)
        fToHitRandom = 2.0f * random.Get(MINT(apAttacker.fToHitRandom)) -
                       (apAttacker.fToHitRandom + 1);

    if(!jmv)if(auto*mod=ModRuntime::Current()){if(mod->CombatSetting("DistanceModifier"))fDistanceModifier=0;if(mod->CombatSetting("WeightBonus"))fWeightBonus=0;}
    float fToHit = static_cast<float>(apAttacker.nAttack) + fToHitRandom - fDistanceModifier;
    if (!jmv&&!bBackstab && spellAddonHitMode != 2)
        fToHit += fWeightBonus;

    return Max(0.0f, fToHit);
}
/////////////////////////////////////////////////////////////////////////////////////
SL0AnimationSeq *CUnitServer::GetExtendedAnimationSequence(unsigned kind)
{
    switch (kind)
    {
    case 1: return &level0Animation;
    case 2: return &level1Command.attackAnimation;
    case 3: return &sufferAnimationMem;
    case 4: return &animMoveCache.anim;
    case 5: return &animIdleCache.anim;
    default: return nullptr;
    }
}
// !!! SAVE/LOAD code
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
int CUnitServer::GetHeroLevel() const
{
    const CModPackage *mod = ModRuntime::Current();
    if (mod && mod->SpellAddon().Feature47() == 1)
    {
        const auto &profile = mod->SpellAddon();
        const float divisor = profile.Scalar(ModSpellAddon::ScalarKey::rebalanceengine_divlvla);
        const float multiplier = profile.Scalar(ModSpellAddon::ScalarKey::rebalanceengine_mullvla);
        const float addOutside = profile.Scalar(ModSpellAddon::ScalarKey::rebalanceengine_addlvla);
        const float addInside = profile.Scalar(ModSpellAddon::ScalarKey::rebalanceengine_addlvlb);
        return NumericPolicy::RebalancedHeroLevel(rpgUnitStats.fpExperience, divisor, multiplier,
                                                   addInside, addOutside);
    }
    float fPower = rpgUnitStats.fpExperience / theAIGeneralSettings.fConsiderModifier;
    return NumericPolicy::HeroLevelFromPower(fPower, theAIGeneralSettings.fConsiderLogBase);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::HaveLowShapes() const
{
    return ((lpUnit->lpArmors[RPGAST_SHIRT] != 0) || (lpUnit->lpArmors[RPGAST_PANTS] != 0) ||
            (lpUnit->lpArmors[RPGAST_BOOTS] != 0) || (lpUnit->lpArmors[RPGAST_GLOVES] != 0));
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUnitServer::HaveHighShapes() const
{
    return ((lpUnit->lpArmors[RPGAST_HELM] != 0) || (lpUnit->lpArmors[RPGAST_PLATE] != 0) ||
            (lpUnit->lpArmors[RPGAST_LEGGINGS] != 0));
}
/////////////////////////////////////////////////////////////////////////////////////

bool CUnitServer::CanRequestWeaponSkill(int slot,CMapObject*target){
 auto*mod=ModRuntime::Current();auto*spell=slot>=0&&slot<int(lpUnit->GetSpellSlotCount())?GetSpell(slot):nullptr;ModSkills::Config config;
 return mod&&mod->TargetSkill(spell)&&mod->SkillConfig(spell,config)&&target&&target!=lpUnit&&target->WhoAmI()==HPC_UNIT&&!target->IsDead()&&(mod->PointSkill(spell)?static_cast<CUnit*>(target)->CanBeDamagedByAreaSpell():spell->IsValidTarget(lpUnit,static_cast<CUnit*>(target)))&&mod->SkillReady(*lpUnit,spell,config,true)&&GetMapObjectServer()&&config.Cost(spell->GetManaCost())<=GetMapObjectServer()->GetRPGStats()->ssMP.fpCurrent;
}
bool CUnitServer::TagWeaponSkill(int slot,CMapObject*target){
 if(slot<0||slot>=int(lpUnit->GetSpellSlotCount())||nextCommand.cmd!=UC_ATTACK||nextCommand.pTarget!=target)return false;
 nextCommand.dwParam=ModSkills::CommandTag|DWORD(slot);return true;
}
int CUnitServer::PrepareWeaponSkill(bool leftHand){
 if(curCommand.cmd!=UC_ATTACK)return 0;int slot=ModSkills::Slot(curCommand.dwParam);if(slot<0)return 0;
 auto*mod=ModRuntime::Current();if(!CanRequestWeaponSkill(slot,curCommand.pTarget)){InformFailure(ACK_HERO_REFUSE_CANT_CAST);return -1;}
 CSpell*spell=GetSpell(slot);ModSkills::Config config;if(!mod->SkillConfig(spell,config))return -1;
 const int hitSlot=ModPreparedAttack::HitSlot(leftHand,int(lpUnit->GetCurrentWeaponSlotIndex()),leftHand?mod->CustomLeft(*lpUnit):-1);
 auto*used=hitSlot>=0&&hitSlot<4?lpUnit->GetQWeapon(hitSlot):nullptr;if(!used||!config.Weapon(used->GetWeaponType()))return -1;
 auto*target=dynamic_cast<CUnitServer*>(static_cast<CUnit*>(curCommand.pTarget)->GetUnitSpecific());if(!target)return -1;
 bool cripple=!_stricmp(spell->GetPrototype()->szCodeName,"cripplinghit");
 if(cripple&&!mod->CanCripple(*target->GetUnit()))return -1;
 if(fabs(level1AP.fpToHit)<FLT_MAX)target->DetermineHit(&level1AP,lpUnit,config.accuracyBonus);
 if(config.scaleDamageWithMelee)level1AP.fpDamage*=config.damageMultiplier;
 if(cripple)mod->MarkSkillImpact(level1AP,ModSkills::ImpactCripple);
 if(!_stricmp(spell->GetPrototype()->szCodeName,"precisehit"))mod->MarkSkillImpact(level1AP,ModSkills::ImpactPreciseHit);
 if((!_stricmp(spell->GetPrototype()->szCodeName,"powerhit")||!_stricmp(spell->GetPrototype()->szCodeName,"test"))&&(used->GetWeaponType()==4||used->GetWeaponType()==14)){mod->MarkSkillImpact(level1AP,ModSkills::ImpactPowerHit);auto*visible=theWorld.GetUnit(lpUnit->GetID());if(visible)if(auto*glow=visible->FindPowerHitGlow(level1Command.nHitTime))glow->MarkPowerHitDelivered();}
 mod->StartSkillCooldown(*lpUnit,spell,config);curCommand.dwParam=0;
 GetMapObjectServer()->ChangeCurrentMP(-config.Cost(spell->GetManaCost()));return config.projectileCount;
}

bool CUnitServer::CanRequestPointSkill(int slot,const CPlot3D& point,bool release){
 auto*mod=ModRuntime::Current();auto*spell=slot>=0&&slot<int(lpUnit->GetSpellSlotCount())?GetSpell(slot):nullptr;ModSkills::Config config;
 if(!mod||!mod->PointSkill(spell)||!mod->SkillConfig(spell,config)||!mod->SkillReady(*lpUnit,spell,config,true)||!GetMapObjectServer())return false;
 if(!(lpUnit->GetOwner()!=nullptr||(!lpUnit->IsHero()&&config.allowNpc)))return false;
 return ModSpellIdentity::PointEligible(release,true,true,GetMapObjectServer()->GetRPGStats()->ssMP.fpCurrent,config.Cost(spell->GetManaCost()),GetCurrentWeaponRange(),distanceXY2(lpUnit->GetCP(),point),config.areaRadius);
}
bool CUnitServer::TagPointSkill(int slot){
 if(nextCommand.cmd!=UC_CASTPOINT||nextCommand.nQSpellNum!=slot||slot<0||slot>=16)return false;
 nextCommand.dwParam=ModSkills::CommandTag|DWORD(slot);return true;
}
BOOL CUnitServer::CreateCustomAttackPortion0(SRPGAttackPortion& ap){
 auto*mod=ModRuntime::Current();if(!mod)return FALSE;
 ap.Clear();float roll=float(ModCustomLower::CrtRoll(100));int slot=int(lpUnit->GetCurrentWeaponSlotIndex());auto*w=slot>=0&&slot<4?lpUnit->GetQWeapon(slot):nullptr;
 if(!w){memcpy(ap.fpAttacks,rpgUnitStats.fpNAWAttacks,sizeof(ap.fpAttacks));ap.nSkill=0;ap.fpToHit=rpgUnitStats.fpNAWToHit*(IsIDofPersonage(lpUnit->GetID())?1.f:theAIGeneralSettings.fAttackLevels[theSettings.nDifficultyLevel]);float sample=rpgUnitStats.fpNAWDamageRnd?float(random.Get(MINT(rpgUnitStats.fpNAWDamageRnd*100.f)))*.01f:0.f;ap.fpDamage=mod->ModifiedDamage(rpgUnitStats,*lpUnit,rpgUnitStats.fpNAWDamageMin+sample,roll,-1,false);}
 else {w->GetDamageDistribution(ap.fpAttacks);ap.nSkill=w->GetProficiencyClass();ap.fpToHit=2*ModNativeCrtRandom::Dice(RPG_CONST_X)-(RPG_CONST_X+1);ap.fpDamage=w->GetRandomDamage();int left=mod->CustomLeft(*lpUnit);float lo=mod->ModifiedDamage(rpgUnitStats,*lpUnit,w->GetDamageMin(),roll,left,false),hi=mod->ModifiedDamage(rpgUnitStats,*lpUnit,w->GetDamageMax(),roll,left,false);ap.fpDamage=random.GetFloat(lo,hi);}
 ap.hl=RPGHL_ALL;ap.bAreaEffect=FALSE;ap.fBackStabMultiplier=1;ap.bCalledShot=false;
 if(lpUnit->GetOwner()&&lpUnit->GetOwner()->bGod>=3)ap.MakeDeadlyAttack();
 mod->RememberCustomAttack0(rpgUnitStats,*lpUnit,ap,roll);return TRUE;
}

int CUnitServer::PrepareExplosiveShot(){
 if(curCommand.cmd!=UC_CASTPOINT)return 0;int slot=ModSkills::Slot(curCommand.dwParam);if(slot<0)return 0;
 if(!CanRequestPointSkill(slot,curCommand.ptTarget,true)){InformFailure(ACK_HERO_REFUSE_CANT_CAST);return -1;}
 auto*mod=ModRuntime::Current();auto*spell=GetSpell(slot);ModSkills::Config config;if(!mod->SkillConfig(spell,config))return -1;
 SRPGAttackPortion ap;bool created=mod->CombatSetting("JMVAttacks")?CreateCurrentWeaponAttackPortion(ap,FALSE,RPGHL_ALL,false,false):CreateCustomAttackPortion0(ap);if(!created){InformFailure(ACK_HERO_REFUSE_CANT_CAST);return -1;}
 if(config.scaleDamageWithMelee)ap.fpDamage*=config.damageMultiplier;
 ModSkills::ExplosiveDamage(ap);mod->ForgetAttack(ap);
 auto*world=static_cast<CWorldServer*>(GetWorld());
 auto*arrow=static_cast<CEffectArrowFigure*>(world->GetEffectManager()->CreateData(ETypeIDs(HPC_EFFECTARROWFIGURE)));if(!arrow)return -1;
 CPlot3D start=lpUnit->GetUnitHeadPosition();CPlot3D point=curCommand.ptTarget;point.z=world->GetSurfaceValue(point.x,point.y)+1;
 arrow->SetCP(start);arrow->SetTarget(-2);arrow->nAttackerID=lpUnit->GetID();arrow->bNewArrow=true;arrow->attackPortion=ap;
 arrow->SetExplosivePoint(point,config.areaRadius,world->GetSegmentTimeCounter());world->AddEffect(arrow);
 mod->StartSkillCooldown(*lpUnit,spell,config);curCommand.dwParam=0;GetMapObjectServer()->ChangeCurrentMP(-config.Cost(spell->GetManaCost()));return 1;
}

bool CUnitServer::PowerHitTrailActive(int hit,bool delivered,std::uintptr_t weaponToken){
 if(lpUnit->IsDead()||lpUnit->CheckFlag(UNIT_STUN)||bL1PlayingSuffer)return false;
 auto*weapon=lpUnit->GetCurrentQWeapon();if(!weapon||(weapon->GetWeaponType()!=4&&weapon->GetWeaponType()!=14))return false;
 if(delivered)return (L1IsFree()||L1IsIdle()||(L1IsAttacking()&&level1Command.nHitTime==hit))&&reinterpret_cast<std::uintptr_t>(weapon)==weaponToken;
 if(!L1IsAttacking()||level1Command.nHitTime!=hit)return false;
 int slot=ModSkills::Slot(curCommand.dwParam);if(slot<0||curCommand.cmd!=UC_ATTACK)return false;
 auto*spell=GetSpell(slot);if(!spell||!spell->GetPrototype())return false;
 const char*name=spell->GetPrototype()->szCodeName;
 return (!_stricmp(name,"powerhit")||!_stricmp(name,"test"))&&CanRequestWeaponSkill(slot,curCommand.pTarget);
}
void CUnitServer::StartPowerHitGlow(int now){
 int hit=level1Command.nHitTime;if(now>=hit-1||hit-now>120||!PowerHitTrailActive(hit,false,0))return;
 auto*carrier=theWorld.GetUnit(lpUnit->GetID());auto*manager=static_cast<CParticleManager*>(theWorld.GetParticleManager());
 if(!carrier||!manager||carrier->FindPowerHitGlow(hit))return;
 auto*particle=manager->CreateParticleSystem(PTID_LIGHTNINGBLAST);if(!particle)return;
 particle->ConfigurePowerHit(carrier,CPlot3D(0,0,0),CPlot3D(0,0,0),hit);
 particle->SetPowerHitWindup(hit-now,reinterpret_cast<std::uintptr_t>(lpUnit->GetCurrentQWeapon()));
}
void CUnitServer::PowerHitFeedback(CUnit*attacker,float beforeHp){
 if(beforeHp<=GetMapObjectServer()->GetRPGStats()->ssHP.fpCurrent)return;
 CPlot3D direction=lpUnit->GetCP()-attacker->GetCP();direction.z=0;float length=sqrtf(direction.x*direction.x+direction.y*direction.y);
 if(length>.001f)direction*=1/length;else direction=CPlot3D(1,0,0);
 auto*visible=theWorld.GetUnit(lpUnit->GetID());CPlot3D point=visible?visible->GetCP():lpUnit->GetCP();
 float radius=(std::max)(.2f,(std::min)(.8f,lpUnit->bv.fpRadius));point-=direction*(radius*.6f);point.z+=radius;
 if(auto*manager=static_cast<CParticleManager*>(theWorld.GetParticleManager()))if(auto*p=manager->CreateParticleSystem(PTID_SMOKE))p->ConfigurePowerHit(nullptr,point,direction,-1);
 if(auto*sound=theWorld.GetSoundManager())sound->BeginFX("skills\\powerhit\\1.wav",8000,point,5,25,FALSE);
 if(lpUnit->IsDead()||GetMapObjectServer()->IsDead()||lpUnit->CheckFlag(UNIT_STUN)||bL1PlayingSuffer||eL1State==UL1_SPECIAL||eL1State==UL1_SPECIAL_CYCLE||nL1SufferDirection<0)return;
 int directionCode=nL1SufferDirection;SL0AnimationSeq animation;animation.Clear();GetSufferAnimation(animation,directionCode,false,false);
 const SADEntry*best=nullptr;int score=-1;DWORD pose=GetPoseAnimationMask(currentPose),weapons=GetAnimationWeaponMask();
 if(animation.IsEmpty())for(DWORD i=0;;++i){auto*entry=lpUnit->GetAnimationEntryByIndex(i);if(!entry)break;
  if(!entry->pszAnimationName[0]||entry->dwEntryIndex!=i||entry->GetAction()!=ADB_ACTION_SUFFER||entry->GetStage()!=ADB_STAGE_UNIQUE||!entry->dwAnimationLength||(entry->dwFlags&ADB_MODIFIER_ACTION_STAY)!=ADB_MODIFIER_ACTION_STAY)continue;
  DWORD state=entry->GetState();if(state!=pose&&!((pose==ADB_STATE_NEUTRAL||pose==ADB_STATE_ATTACK)&&(state==ADB_STATE_NEUTRAL||state==ADB_STATE_ATTACK)))continue;
  int value=(state==pose?4:0)+((entry->GetWeapons()&weapons)==weapons?2:0)+((entry->dwFlags&ADB_MODIFIER_DIRECTION2_MASK)==(DWORD(directionCode)&ADB_MODIFIER_DIRECTION2_MASK)?1:0);
  if(value>score&&GetAnimationLength(i)>0){best=entry;score=value;}
 }
 if(animation.IsEmpty()&&!best)return;
 bool active=L1IsAttacking(),waiting=L1IsIdle()&&curCommand.cmd==UC_ATTACK;
 if(active||waiting){if(active)L1CancelAttack(true);LcCancelCommand();L1StartIdle();}
 nL1SufferDirection=directionCode;powerHitSuffer=best;L1StartSuffer();powerHitSuffer=nullptr;
}

bool CUnitServer::CanRequestAISkill(int slot,CUnit*target){
 auto*mod=ModRuntime::Current();auto*spell=slot>=0&&slot<int(lpUnit->GetSpellSlotCount())?GetSpell(slot):nullptr;ModSkills::Config config;
 if(!mod||!mod->SkillConfig(spell,config)||!config.allowNpc||(lpUnit->IsHero()&&!mod->CompanionSkillAI(*lpUnit))||!target||target==lpUnit||target->IsDead()||!spell->IsValidTarget(lpUnit,target)||!mod->SkillReady(*lpUnit,spell,config,true)||!GetMapObjectServer())return false;
 return config.Cost(spell->GetManaCost())<=GetMapObjectServer()->GetRPGStats()->ssMP.fpCurrent+.0001f;
}
bool CUnitServer::HasAISkillCommand(int slot,CUnit*target){for(auto*cmd:{&curCommand,&nextCommand})if(ModSkills::Slot(cmd->dwParam)==slot&&((cmd->cmd==UC_ATTACK&&cmd->pTarget==target)||cmd->cmd==UC_CASTPOINT))return true;return false;}
bool CUnitServer::TagAISkill(int slot,CUnit*target){
 auto*mod=ModRuntime::Current();if(!mod||slot<0||slot>=int(lpUnit->GetSpellSlotCount()))return false;
 for(auto*cmd:{&nextCommand,&curCommand})if(cmd->cmd==UC_ATTACK&&cmd->pTarget==target){cmd->dwParam=ModSkills::CommandTag|DWORD(slot);
  if(mod->PointSkill(GetSpell(slot))){cmd->cmd=UC_CASTPOINT;cmd->nQSpellNum=slot;cmd->ptTarget=target->GetCP();cmd->pTarget=nullptr;cmd->bUsePoint=false;}return true;
 }return false;
}
