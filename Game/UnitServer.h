#pragma once
#include "SaveSchema.h"
/*==========================================================================================================

        Server specific unit class header

        Written by Ivanov Evgeny
        Nival Interactive, 1998

==========================================================================================================*/
/////////////////////////////////////////////////////////////////////////////////////
#include <utility>
#include <cstdint>

#include "Unit.h"
#include "MapObjectServer.h"
#include "aiUnitLogic.h"
#include "aiMap.h"
#include "aiAck.h"
/////////////////////////////////////////////////////////////////////////////////////
class CEffectParticle;
class CWorldServer;
/////////////////////////////////////////////////////////////////////////////////////
// ----------------------------- Server player
// -------------------------------------------------------------
enum EUnitCommand
{
    UC_IDLE,          // wait for command, do nothing
    UC_MOVETO,        // move to point < pointToMove >
    UC_ROTATE,        // unit rotates
    UC_ATTACK,        // attack unit < pTarget >
    UC_CASTPOINT,     // unit start casting a point spell
    UC_CASTUNIT,      // unit start casting a unit spell
    UC_SKILL,         // unit uses some special skill
    UC_PLAYANIM,      // unit plays some special animation
    UC_PLAYANIMCYCLE, // unit plays some animation in cycle
    UC_NONE,
    UC_TRAP, // unit is using some quick item as a trap
    UC_REST, // unit is resting
};
/////////////////////////////////////////////////////////////////////////////////////
enum EUnitL1State
{
    UL1_MOVE,
    UL1_ROTATE,
    UL1_IDLE,
    UL1_SPECIAL,
    UL1_SPECIAL_CYCLE,
    UL1_ATTACK,
    UL1_NONE
};
/////////////////////////////////////////////////////////////////////////////////////
// описание команды, используемое при выполнении разного рода сложных действий - атак,
// использования скилов итп
struct SUnitBasicCmd
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    EUnitCommand cmd;      // command id
    int nQSpellNum;        // number of quick spell to cast
    CMapObjectPtr pTarget; // command target if there is one
    CPlot3D ptTarget;      // command destination
    bool bUsePoint;        // flag if cmd should be done from ptTarget

    void Clear()
    {
        cmd = UC_NONE;
        nQSpellNum = 0;
        ptTarget = CPlot3D(0, 0, 0);
        pTarget.Clear();
        bUsePoint = false;
    }
    SUnitBasicCmd() { Clear(); }
    //
    bool IsLoot() const { return cmd == UC_SKILL && nQSpellNum == RPGMSKL_LOOT; }
    bool IsScience() const { return cmd == UC_SKILL && nQSpellNum == RPGMSKL_SCIENCE; }
    bool IsEmpty() const { return cmd == UC_NONE; }
    bool IsCast() const { return cmd == UC_CASTPOINT || cmd == UC_CASTUNIT; }
    bool IsMove() const { return cmd == UC_MOVETO; }
    bool IsAttack() const { return cmd == UC_ATTACK; }
    bool IsSkill() const { return cmd == UC_SKILL; }
    bool HasPointOfUse() const { return bUsePoint; }
    //
    void MakeAttack(CMapObject *_pTarget)
    {
        Clear();
        cmd = UC_ATTACK;
        nQSpellNum = RPGHL_ALL;
        pTarget = _pTarget;
    }
    void MakeCast(int nSpell, CMapObject *_pTarget)
    {
        Clear();
        cmd = UC_CASTUNIT;
        pTarget = _pTarget;
        nQSpellNum = nSpell;
    }
    void MakeCast(int nSpell, const CPlot3D &ptTarg)
    {
        Clear();
        cmd = UC_CASTPOINT;
        ptTarget = ptTarg;
        nQSpellNum = nSpell;
    }
    void MakeSkill(CMapObject *_pTarget, int nSkill)
    {
        Clear();
        cmd = UC_SKILL;
        pTarget = _pTarget;
        nQSpellNum = nSkill;
    }
    //
    CPlot3D GetTargetCP() const
    {
        if (pTarget.IsEmpty())
            return ptTarget;
        else
            return pTarget->GetTargetCP();
    }
    float GetTargetSpeed() const
    {
        if (pTarget.IsEmpty())
            return 0;
        else
            return pTarget->GetSpeed();
    }
    int GetTargetID() const
    {
        if (pTarget.IsEmpty())
            return -1;
        else
            return pTarget->GetID();
    }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SUnitCommand : public SUnitBasicCmd
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    // SUnitAttack cmd;               // описание команды с необходимыми параметрами
    union
    {
        float fpParam; // additional command parameter
        DWORD dwParam;
    };
    bool bDoItFast;

    void Clear()
    {
        SUnitBasicCmd::Clear();
        fpParam = 0;
        bDoItFast = false;
    }
    // bool IsPointAttack() const { return ptTarget.z >= 0; }
    SUnitCommand() { Clear(); }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
// описание единичной анимации для L0, если IsEmpty(), то предназначен просто для смены
// скорости анимации
struct SL0Animation
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    BYTE bStart, bCyclic, bSoundSet;
    int nLength;
    EUnitAnimationSpeed animationSpeed;

    SL0Animation()
    {
        bStart = bCyclic = 0xff;
        nLength = -1;
        animationSpeed = UAS_NORMAL_;
        bSoundSet = 0;
    }
    bool operator!=(const SL0Animation &a)
    {
        return a.bStart != bStart || a.bCyclic != bCyclic || a.nLength != nLength ||
               a.animationSpeed != animationSpeed || a.bSoundSet != bSoundSet;
    }
    bool operator==(const SL0Animation &a)
    {
        return a.bStart == bStart && a.bCyclic == bCyclic && a.nLength == nLength &&
               a.animationSpeed == animationSpeed && a.bSoundSet == bSoundSet;
    }
    bool IsEmpty() { return bStart == 0xff && bCyclic == 0xff; }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
// последовательность анимаций, используется в L0 как элементарная анимация
struct SL0AnimationSeq
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    CAArray<SL0Animation> animations;
    // Full ADB indices live beside the byte-sized, on-disk animation records.
    CAArray<DWORD> startIds, cycleIds;
    int nStartSegment = -1; // -1 if start from current

    SL0AnimationSeq &operator+=(const SL0AnimationSeq &add);
    void Clear()
    {
        animations.SetSize(0);
        startIds.SetSize(0);
        cycleIds.SetSize(0);
        nStartSegment = -1;
    }
    void Freeze(int nTime) { nStartSegment += nTime; }
    EUnitAnimationSpeed GetAnimationSpeed();
    bool IsEmpty() { return animations.GetSize() == 0; }
    bool IsSame(SL0AnimationSeq &cmp);
    void AddSimple(DWORD bStart, int nLength);
    void AddCycle(DWORD bCycle);
    void AddPumpCycle();
    void Append(const SL0Animation &animation, DWORD startId, DWORD cycleId);
    DWORD GetStartId(int index) const;
    DWORD GetCycleId(int index) const;
    void SetStartId(int index, DWORD id);
    void SetCycleId(int index, DWORD id);
    void EnsureIds();
    void SetSpeed(EUnitAnimationSpeed speed);
    void SetSoundSet(BYTE bSoundSet);
    bool IsEndless();
    int GetLength();
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
// полное описание позы для определения анимации suffer, переходов между состояниями и др
struct SL1Pose
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    EUnitPose pose; // no run, only stan/crawl/lay
    bool bAggressive;
    BYTE bRun; // набор битиков, каждый из которых может заставлять юнит бежать, 1 - от
               // переключателя, 2 - uc_runto

    void Clear()
    {
        pose = UP_STAND;
        bAggressive = false;
        bRun = 0;
    }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
    void Convert(EUnitPose p)
    {
        bRun = 0;
        pose = p;
        if (p == UP_RUN)
        {
            pose = UP_STAND;
            bRun = 1;
        }
    }
    void SetRun(BYTE bArg) { bRun = bArg; }
    EUnitPose Get()
    {
        if (bRun & 1)
            return UP_RUN;
        else
            return pose;
    }
    bool WantRun() { return bRun != 0; }
};
/////////////////////////////////////////////////////////////////////////////////////
// описание текущей команды, выполняемой на 1-м уровне
// 1-й уровень обеспечивает прозрачную отработку stun/suffer
struct SL1Command
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    SL0AnimationSeq attackAnimation; // анимация атаки/ходьбы
    int nTimeStrikeMoment; // момент начала состояния атаки (анимация для атаки содержится в
                           // nAttackAnimation)
    int nHitTime;          // момент нанесения удара ( определяется анимацией )
    int nTargetID;         // идентификато цели
    float fpHitDist2;      // максимально допустимое расстояние удара, squared
    bool bCast;
    ERPGHitLocations eAttackLocation;
    bool bCalledShot;
    ERPGMagicSubtypes castType;
    CPlot3D ptDir = CPlot3D(0, 0, 0);

    void Clear();
    void CreateCastAttack(SL0AnimationSeq animation, int nTime, int nHit,
                          ERPGMagicSubtypes magicType);
    void CreateAimlessAttack(SL0AnimationSeq animation, int nTime, int nHit);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
    void Freeze(int nTime)
    {
        nTimeStrikeMoment += nTime;
        nHitTime += nTime;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
struct SContSpellInfo
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    CSmartPtr<CItem> pItem; // pointer to spell with some continous spell
    int nDelay;             // delay to cast next time
    bool bIsUsed;           // for temprorary use in BuildContinousSpells list

    SContSpellInfo() { nDelay = 0; bIsUsed = false; }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
struct STrainInfo
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    bool bIsTrain;
    CSmartPtr<CUnit> master;

    STrainInfo() { bIsTrain = false; }
    void Clear()
    {
        bIsTrain = false;
        master = 0;
    }
    void RemoveReferencesOn(CMapObject *pTarget);
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
/////////////////////////////////////////////////////////////////////////////////////
// объект для запоминания наиболее часто используемых анимаций
struct SAnimationCache
{
    DWORD dwAnimationFlags;
    SL0AnimationSeq anim;
    void Clear()
    {
        dwAnimationFlags = 0x666DEAD;
        anim.Clear();
    }
    SAnimationCache() { Clear(); }
};
/////////////////////////////////////////////////////////////////////////////////////
// структура для хранения промежуточных результотов трассировки до цели
struct SUCTraceInfo
{
    CPlot3D ptDest;
    float fpMinDist2;
    float fpOurHeadH; // высота головы в состоянии стоя для смотрящего юнита
    CUnit *pTarget;   // to avoid costly computation of unitheadpos when its not needed

    void Clear()
    {
        ptDest = CPlot3D(0, 0, 0);
        fpMinDist2 = 0;
        pTarget = 0;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
typedef void (*ServerAnimationSwitchCallback)(CUnit *pUnit);
/////////////////////////////////////////////////////////////////////////////////////
class CUnitServer : public CUnitSpecific
{
    unsigned navigationWait=0;
    unsigned navigationRejects=0; // transient; turns do not erase a failed-move retry

    friend class CModPackage;
    friend class CUnit;

private:
    // Lowest level data
    // int nLowAnimTimeLeft;
    // Level 0 data
    bool bL0Frozen, bL0MovementFrozen; // stun flag & suffer flag
    SL0AnimationSeq level0Animation;
    // Level I data
    SAIPath path;
    SL1Command level1Command;
    SRPGAttackPortion level1AP;

    bool bL1PlayingSuffer;
    bool bL1MovementFailed; // последний StartMove закончился неудачно
    bool bL1SpeedChanged;
    bool bL1MoveAnimationSet; // если false, то при начале ходьбы после поворота будет выставлена
                              // анимация ходьбы
    // сюда записывается анимация, бывшая активной в момент начала проигрывания анимации suffer
    SL0AnimationSeq sufferAnimationMem;
    int nL1SufferDirection;        // откуда ударили юнита, -1 если не ударяли
    EUnitL1State eL1State;         // текущее состояние L1
    SL1Pose currentPose, wishPose; // текущая поза и желаемая полные позы
    int nL1RunFailCounter;         // счетчик сегментов во время которых бег оказывался невозможным
    CSmartPtr<CEffectParticle> pCastParticle; // casting particle
    int nAttackParamsSetTime;
    // Level commands data
    int nCanAttackTime; // минимальное время, когда может быть нанесен новый удар
    int nMoveToTargetTime;
    bool bLcCommandFail;
    bool bLcCompletelyDead;
    bool bLcChangingPoseToAttack;
    SUnitCommand curCommand, nextCommand; // текущая и следующая команды
    STrainInfo train;
    // EUnitPose oldPose;
    // General data
    // RPG system info
    SRPGUnitStats rpgUnitStats; // unit's RPG stats
    SRPGGeneralAttack rpgDefend;
    //
    float fpZSpeed;
    // модификаторы к заметностям  и сенсам
    float fpModSound, fpSModSound, fpModVision, fpSModVision;
    // для реализации tame
    ETameLevel tameLevel;   // текущий уровень tame юнита
    int nOriginalPlayerNum; // предыдущий номер плеера юнита (до tame)
    int nPlayerWhoTamed;
    // через какое количество сегментов необходимо генерировать новый след, число должен состоять
    // только из единиц в двоичном представлении ( 0, 1, 3, 7, 15 )
    int nFootstepGenInterval;
    SAIMark *lastFootstep; // последний след юнита
    CList<SContSpellInfo> activeContSpells;
    int nBoredCounter;
    int nCommandExecStart;
    // for animations
    SAnimationCache animMoveCache, animIdleCache;
    ServerAnimationSwitchCallback animCallback;

    DWORD dwFutureEnemies; // маска, показывающая врагов, о которых уже сообщили но они еще не
                           // активизированы
public:
    friend class CModPackage;
    friend class CAIUnitLogic; // source-equivalent follower wish-pose synchronization
    bool CanRequestWeaponSkill(int slot,CMapObject*target);
    bool CanRequestPointSkill(int slot,const CPlot3D& point,bool release=false);
    bool TagPointSkill(int slot);
    bool CanRequestAISkill(int slot,CUnit* target);
    void SyncNpcMagicState();
    bool HasAISpellMana(int slot);
    bool HasAISkillCommand(int slot,CUnit* target);
    bool TagAISkill(int slot,CUnit* target);
    bool PowerHitTrailActive(int hit,bool delivered,std::uintptr_t weaponToken);
    bool TagWeaponSkill(int slot,CMapObject*target);
    void CancelFollowOnLostSight(CUnit* target);
    const SUnitCommand& GetScriptCommand() const { return curCommand; }
    const SUnitCommand& GetNextScriptCommand() const { return nextCommand; }
    EUnitPose GetScriptPose() const { return currentPose.pose; }
    CAIUnitLogic logic;
    CAIAcknowledgements acknowledgements;
    // Runtime-only ASI f06 counter; never part of RPG stats or the save stream.
    DWORD dwSpellAddonLostSight;

private:
    // LOWEST LEVEL interface
    //  pose change functions
    void LowSetDead(BOOL bDead);
    void LowSetMoving(BOOL bMoving);
    void LowNewPose(EUnitPose pose);
    void LowNewWishPose(EUnitPose pose);
    bool LowFindPath(const CPlot3D &ptDest, SAIPath &res, BOOL bFastPath,
                     float fpMaxPathLength = 1e6f, Navigation::GoalPolicy goalPolicy = Navigation::GoalPolicy::Exact);
    void LowRemoveUnitsForPathFind(CUnitList &units, const CPlot3D &ptDest, bool bGroup);
    // LEVEL 0 of unit control
    bool L0ShiftAnimations();
    void L0StartNextAnimationSegment();
    //  L0 interface
    void L0StartAnimation(SL0AnimationSeq &anim);
    void L0GetAnimation(SL0AnimationSeq &curAnimation);
    void L0Freeze(bool bFreeze);
    void L0FreezeMovement(bool bFreeze);
    bool L0HasAnimation();
    bool L0IsPlayingSuffer();
    void L0Segment();
    // LEVEL I of commands execution
    void L1ChangeWeapon();
    void L1StartRotateAnimation();
    void L1NewPathSegment();
    void L1ExecMove();
    void L1CheckRun();
    void L1ExecAttack();
    void L1StandStill();
    void L1StopMovement(bool bFailed);
    void L1SetPhase(EUnitL1State phase);
    void L1StartSuffer();
    //  L1 interface
    void L1ResetState();
    bool L1StartMove(const CPlot3D &ptTarget, float fpMaxPathLength = 1e6f);
    void L1StartRotate(const CPlot3D &vecDest, float fpAngSpeed = 0);
    void L1StartIdle();
    void L1StartSpecialCycle(SL0AnimationSeq animation);
    void L1StartSpecial(SL0AnimationSeq animation);
    void L1StartAttack(bool bPredictTarget,
                       BYTE nSoundSet); // level1Command contains information about new attack
                                        // process, level1AP - attack portion for it
    void L1CancelAttack(bool bAbortAnimation = false); // should clear pointer to target
    bool L1IsInterruptable();                          // - standingidle||move/rotate
    bool L1IsMoveFailed() { return bL1MovementFailed; }
    void L1ResetMoveFailed() { bL1MovementFailed = false; }
    void L1SetMoveFailed() { bL1MovementFailed = true; }
    bool L1IsFree() { return eL1State == UL1_NONE; } // command execution is over
    bool L1IsIdle() { return eL1State == UL1_IDLE; }
    bool L1IsMoving() { return eL1State == UL1_MOVE; }
    bool L1IsAttacking() { return eL1State == UL1_ATTACK; }
    void L1SpeedChanged(); // should repath if moving
    void L1NeedPlaySuffer(int nSufferDirection);
    void L1Segment(); // perform L1 actions
    // LEVEL COMMANDS - highest level of commands execution
    bool LcCanPlaySuffer(int nSufferLeng);
    bool LcTargetIsValid(const SUnitCommand &cmd);
    bool LcRecheckAttack();
    void TraceMeleeReach(int phase, CMapObject *target, CItemWeapon *weapon = nullptr,
                         float predictedDistance2 = -1, float hpBefore = -1,
                         float hpAfter = -1);
    enum class AttackCallbackOrigin { Ordinary = 0, Dual = 1, Original = 2 };
    void
    LcAttackCallback(ERPGHitLocations eAttackLocation,
                     bool bCalledShot, bool leftHand=false,
                     AttackCallbackOrigin origin=AttackCallbackOrigin::Ordinary); // this function is called when attack is to be performed
    void LcRestartCommand();
    bool LcChangePose();
    bool LcCanChangePose(EUnitPose p);
    void LcConvertPoses();
    void LcStartNextCommand();
    void LcCancelCommand();
    bool LcProcessNewCommand();
    void LcSegment();

    // ANIMATIONS HANDLING
    DWORD GetPoseAnimationMask(const SL1Pose &pose);
    DWORD GetAnimationWeaponMask();
    void GetAttackAnimation(SL0AnimationSeq &res, int &nHit, EUnitCommand cmd, DWORD dwAttackFlags);
    void GetCastAnimation(SL0AnimationSeq &res, int &nHit, EUnitCommand cmd, int nLength);
    void GetSufferAnimation(SL0AnimationSeq &res, int nSufferDirection, bool bDiyng, bool bMoving);
    void GetIdleAnimation(SL0AnimationSeq &res);
    SL0AnimationSeq *GetIdleAnimation();
    void GetMoveAnimation(SL0AnimationSeq &res, bool bStart = false);
    SL0AnimationSeq *GetMoveAnimation();
    bool IsMoveAnimation(SL0AnimationSeq &res);
    bool GetTransAnimation(SL0AnimationSeq &res, const SL1Pose &srcPose, const SL1Pose &dstPose);
    int CalcSufferDirection(CUnit *pAttacker, BOOL bAreaSpell);
    int GetAnimationLength(DWORD bAnim);
    int GetAnimationHit(DWORD bAnim);
    // вычисление hit location и attack direction исходя из роста и расположения охотника и жертвы
    // возвращает указатель на анимацию атаки, хит локейшн в который наносится удар записывается в
    // cmd, момент нанесения удара в nHit
    bool CalcAttackDirection(SL1Command &cmd, SL0AnimationSeq &res, int &nHit, CMapObject *pMO);

    // SOME WEAPON PARAMS
public:
    float GetCurrentWeaponRange();
    bool IsCurrentWeaponABow();
    DWORD GetCorpseAnimationWeaponMask() { return GetAnimationWeaponMask(); }

private:
    void SelectStandingIdle(SL0AnimationSeq &sequence);
    ERPGWeaponTypes GetCurrentWeaponType() const;
    float GetSpellRange(CSpell *pSpell);
    int GetSpellCastTime(CSpell *pSpell);
    virtual void GetAttackParams(SAttackParams &ap) const;
    float GetToHit(CUnitServer *pAttacker, bool bBackstab) const;
    // получить спелл по его номеру - либо из quickspells либо из wands
    CItemQuick *GetSpellQuickItem(int nQSpellNum);
    CSpell *GetSpell(int nQSpellNum);
    CSpell *GetSpell(const SUnitBasicCmd &cmd);
    bool CanCastSpell(CSpell *pSpell, bool bCheckRPG); // check on rpgstats
    int GetAttackCycleTime(EUnitCommand cmd,
                           CItemWeapon *pWeapon); // returns attack period in segments

    // ATTACK CHECKS
    // при невозможности кастануть в указанную точку кастует на возможное расстояние
    bool IsCastPossible(const SUnitBasicCmd &cmd);
    float GetHitDist(const SUnitBasicCmd &cmd);
    float GetCloseAttackDist(const SUnitBasicCmd &cmd);
    bool AttackIsDirect(const SUnitBasicCmd &cmd, const CPlot3D &ptTargetPos, int nSegment);
    bool CreateTestAttackPlan(const SUnitBasicCmd &cmd);
    bool IsLootLocked(CMapObject *pTarget);
    bool NeedReliefTrace(const SUnitBasicCmd &cmd, SUCTraceInfo &info);
    void SetReliefTraceDest(SUCTraceInfo &info, const CPlot3D &ptUnitPos);
    bool CheckRelief(SUCTraceInfo &info, const CPlot3D &ptFrom); // ptFrom задает x & y
    bool IsTargetReachable(const SUnitBasicCmd &cmd);            // height check
public:
    // проверяет, соответствует ли индекс спела/qItema potion`у
    bool IsPotion(int nQSpellNum);
    // позволяет проверить возможна ли атака cmd по pTarget с текущей позиции
    bool CanAttack(const SUnitBasicCmd &cmd);
    bool CanAttackFrom(const SUnitBasicCmd &cmd, const CPlot3D &ptPos);

private:
    // ATTACK PLANNING
    // определяет можно ли атаковать pTarget из текущей позиции, если можно создает план
    bool CreateAttackPlan(const SUnitBasicCmd &atk, SL1Command &cmd);
    void StartAttack();
    void MoveToTarget();
    void ExecAttack();
    float CalcAttackPathLeng(const SUnitCommand &cmd, CAIFullRoute &move);
    // может изменить команду - превратить в attack from point, вернет false в случае неудачи
    bool FindAttackPath(SUnitCommand &cmd, DWORD dwPositiveAck);
    void ShowFoundPath(CAIFullRoute &move, const CPlot3D &ptDest, int pathType, int pathResult,
                       float fpShowLeng = 1e6f);
    void ShowFoundMovePath(SAIPath &move, bool bFound);
    // returns true if attack is feasible
    bool CalcAttackDest(SUnitCommand &cmd, CAIFullRoute *pRes);

    // ATTACK EXECUTION
    // bBackstab в данном случае показывает, что взаимное положение юнитов допускает
    // использование навыка backstab, направление удара вычисляется в CalcAttackDirection()
    BOOL ProcessAttackPortionOwned(SRPGAttackPortion*, CUnit*, CUnit*, bool inheritedDefense, bool explicitCustom);
    BOOL CreateCustomAttackPortion0(SRPGAttackPortion&);
    BOOL CreateConditionalRetailAttackPortion(SRPGAttackPortion &ap, BOOL bBackstab,
                                             ERPGHitLocations eAttackLocation, bool bCalledShot);
    BOOL CreateCurrentWeaponAttackPortion(SRPGAttackPortion &ap, BOOL bBackstab,
                                          ERPGHitLocations eAttackLocation, bool bCalledShot, bool leftHand=false);
    // вычислить попали ли по цели
    bool DetermineHit(SRPGAttackPortion *pAttackPortion, CUnit *pAttacker, float accuracyBonus=0);
    // если в оружии есть спелл он должен быть использован
    void UseCurrentWeaponSpell(CMapObject *pTarget, CItemWeapon *attackWeapon=nullptr, bool leftHand=false);
    void UseArmorSpells(bool bBeforeAttack);
    void UseSpell();
    void UseTrap();
    void UseSkill(CMapObject *pTarget);
    int PrepareWeaponSkill(bool leftHand=false);
    int PrepareExplosiveShot();
    void StartPowerHitGlow(int now);
    void PowerHitFeedback(CUnit* attacker,float beforeHp);
    const SADEntry* powerHitSuffer=nullptr;
    float lastCrippledFactor=1;
    void UseAttack(CMapObject *pTarget, SRPGAttackPortion &attackPortion,bool applyWeaponSpell=true,int projectileIndex=0,int projectileCount=1,const char* activeSkillCode=nullptr,AttackCallbackOrigin origin=AttackCallbackOrigin::Ordinary);
    void UseAttackStart(CMapObject *pTarget, ERPGHitLocations eAttackLocation, bool bCalledShot, bool leftHand=false);
    void UsePotion(int nQSpellNum, CMapObject *pTarget, const CPlot3D &ptCastTo);

    // TAME, taming support functions
    BOOL TameFailed(CUnit *pWho);
    void RemoveTame();
    int GetPlayerWhoTamed();

    // DIPLOMACY spreading
    void RecalcEnemy(); // update enemy mask with neighbouring units
    void InformAboutEnemy(CUnit *pWho, DWORD dwNewEnemiesMask);

    // FOOTSTEPS generation
    void CreateFootstep();
    void CalcNewFootstepGenInterval();

    // Continous spells
    void RebuildContinousSpellsList();
    void UseContinousSpells();
    void SpendItemMana(CItem *pItem, float fpMana);

    void CalcUnifiedDefend();

public:
    CUnitServer();
    virtual ~CUnitServer();
    virtual void FreeReferences() { Clear(); }
    // create/destroy
    virtual BOOL Create();
    virtual void Destroy();
    void Clear();
    // some additional function that simplify some calls
    void AddUpdateMask(int nBit) { lpUnit->SetUpdateMask(nBit); }
    void NotifyModSpeedChanged() { L1SpeedChanged(); }
    virtual CAIFullRoute *GetRoute() { return &path.curSegment; }
    CMapObjectServer *GetMapObjectServer()
    {
        return (CMapObjectServer *)lpUnit->GetMapObjectSpecific();
    }
    void InformFailure(DWORD dwAction);
    void InformAttackPathNotFound(const SUnitCommand &cmd);
    //
    // вызывается из aiscanunits когда сообщение об изменении доходит до юнита
    void HearNewEnemiesMsg(DWORD dwNewEnemies);
    virtual void RemovePendingDiplomacy();
    // try to tame unit
    BOOL Tame(CUnit *pWho, float fpWhoSkill);
    virtual ETameLevel GetCurrentTameLevel() const { return tameLevel; }
    //
    // RPG system interface
    virtual void TimeOfDayChanged();
    void SetRPGStats(SRPGUnitStats *lpRPGNewUnitStats);
    void GetRPGStats(SRPGUnitStats *pRPGNewUnitStats);
    const SRPGGeneralAttack &GetUnifiedDefendInfo() const { return rpgDefend; }
    // float GetHP() { return rpgUnitStats.
    void GainExperience(float fpExp);
    virtual void RecalcAIClass();
    virtual void ComplectionChanged();
    virtual void ChangeSpeedWithSpell();
    void GetWeaponAttack(SRPGGeneralAttack &res) const;
    void GetSpellAttack(int nSpellNum, SRPGGeneralAttack &res);
    ERPGWeaponTypes GetNativeWeaponType() { return rpgUnitStats.rpgNAWWeaponType; }
    float GetNativeWeaponRange() { return rpgUnitStats.fpNAWRange; }
    // this two functions are only to use with map editor...
    SRPGUnitStats *GetRPGStats() { return &rpgUnitStats; }
    //
    virtual int GetHeroLevel() const;
    // some item notifications
    virtual BOOL NotifyItemsUpdated();
    void SynchronizeItemMana(CItem *pItem) { if (pItem) SpendItemMana(pItem, 0.0f); }
    virtual void InventoryUpdated() {}
    BOOL ChangeWeapon(DWORD dwQWeaponIndex);
    virtual bool Wear(DWORD dwArmor);
    virtual bool Remove(ERPGArmorTypes type);
    virtual void RemoveAllArmors();
    virtual BOOL RecalcRPGStats(bool bStrengthChanged = false);
    virtual void ExportStats();
    //
    virtual bool HaveLowShapes() const;
    virtual bool HaveHighShapes() const;
    // пересчитывает заметности, шумность, частоту оставления следов
    virtual void RecalcAIParams();
    //
    // DAMAGE Processing of some actions influencing health of unit
    BOOL ProcessCustomAttackPortion(SRPGAttackPortion*, CUnit*, CUnit*);
    virtual BOOL ProcessAttackPortion(SRPGAttackPortion *lpAttackPortion, CUnit *pAttacker,
                                      CUnit *pExpDest);
    virtual BOOL Heal(float fpPower);
    // вызывается из MapObject, юнит получил некий damage
    void TakeDamage(CUnit *pAttacker, BOOL bAreaSpell);
    // вызывается если юнит таки помер
    void MakeNewEnemies(CMapObjectPtrList &units, CUnit *pTest, CUnit *pEnemy);
    void AlarmEverybodyWhoSawDeath(CUnit *pAttacker);
    //
    // "low" level commands, called by logic mainly
    void InitializeUnit();
    void SetAggression(bool bAggr) { wishPose.bAggressive = bAggr; }
    bool IsAggressive() { return wishPose.bAggressive; }
    CPlot3D GetScriptMoveFinalPoint() { return path.GetFinalPoint(); }
    bool IsCmdFailed()
    {
        return bLcCommandFail;
    } // previous Cast/Skill/Loot/Attack/Move command failed
    bool IsAlwaysActive() { return logic.IsAlwaysActive(); }
    bool IsTamed() const { return tameLevel != UTL_NONE; }
    void CmdRotate(const CPlot3D &vecDest, float fpAngSpeed);
    void CmdMoveTo(const CPlot3D &ptDest, bool bRun, bool bGroupCmd = false,
                   float fpMaxPathDist = 1e6f);
    void CmdShowMove(const CPlot3D &ptDest, bool bGroupCmd, bool bRun, float fpMaxPathDist = 1e6f);
    void CmdAttack(CMapObject *pTarget, ERPGHitLocations hl, bool bRun, DWORD dwAck);
    void CmdCast(int nQSpellNum, const CPlot3D &pointToCast, bool bRun);
    void CmdCast(int nQSpellNum, CMapObject *pTarget, bool bRun);
    void CmdSkill(CMapObject *pTarget, int nSkill, DWORD dwParam, bool bRun,
                  DWORD dwAck); // does not perform checks
    void CmdSteal(CUnit *pTarget, bool bRun);
    void CmdTame(CUnit *pTarget, bool bRun);
    void CmdScience(CMapObject *pTarget, int nItemID, bool bRun);
    void CmdLoot(CUnit *pTarget, bool bRun);
    void CmdWait();
    void StopGroupSearchMotion();
    bool GroupSearchPathMatches(const CPlot3D &target, float tolerance);
    void CmdRest();
    void CmdNewPose(EUnitPose pose);
    void CmdNewWeapon(DWORD dwWeapon);
    void CmdPlayAnimation(const char *pszAnimation);
    bool HasLeverRingChoice(const SUnitBasicCmd& cmd) const;
    void QueueAnimationLoop(DWORD id);
    void StopAnimationLoop();
    void UpdateAnimationLoop();
    void CmdUseTrap(int nQItemNum);
    void InterruptCommand();
    void SetAnimationSwitchCallback(ServerAnimationSwitchCallback cb) { animCallback = cb; }
    // проверки текущих команд
    bool HasAttack(CMapObject *pTarget)
    {
        return (curCommand.IsAttack() && curCommand.pTarget == pTarget) ||
               (nextCommand.IsAttack() && nextCommand.pTarget == pTarget);
    }
    bool HasAttack() { return curCommand.IsAttack() || nextCommand.IsAttack(); }
    bool HasCast() { return curCommand.IsCast() || nextCommand.IsCast(); }
    bool HasMove() { return curCommand.IsMove() || nextCommand.IsMove(); }
    bool HasSkill() { return curCommand.cmd == UC_SKILL || nextCommand.cmd == UC_SKILL; }
    bool HasRotate() { return curCommand.cmd == UC_ROTATE || nextCommand.cmd == UC_ROTATE; }
    bool HasNoCommand()
    {
        return (curCommand.cmd == UC_REST || curCommand.cmd == UC_IDLE) &&
               nextCommand.cmd == UC_NONE;
    }
    bool IsAttacking() { return curCommand.IsAttack() || curCommand.IsCast(); }
    bool IsL1Attacking() { return L1IsAttacking(); }
    bool IsL1Moving() { return L1IsMoving(); }
    bool IsDanger();
    bool GetPathDestination(CPlot3D &ptRes);
    bool HasRangedAttack();
    //
    virtual int Segment();
    virtual void RemoveReferencesOn(CMapObject *pTarget);
    //
    // load&store
    virtual void Load(CSave &file) = delete;
    virtual void Store(CSave &file) = delete;
    // get some sense parameters
    // прямые, достоверные чувства
    float GetVisionSense() const
    {
        return rpgUnitStats.senses[RPGS_SIGHT].fpEffectiveValue * fpSModVision;
    }
    float GetVisionDetect() const
    {
        return rpgUnitStats.percepts[RPGS_SIGHT].fpEffectiveValue * fpModVision;
    }
    float GetLifeSense() const { return rpgUnitStats.senses[RPGS_SENSELIFE].fpEffectiveValue; }
    float GetLifeDetect() const { return rpgUnitStats.percepts[RPGS_SENSELIFE].fpEffectiveValue; }
    float GetVisionArc() const { return rpgUnitStats.fpVisionArc; }
    float GetPeripheral() const { return rpgUnitStats.fpPeripheral; }
    // чувства следов/ звуков, достаточны для подозрений только
    float GetFSVisionSense() const
    {
        return rpgUnitStats.senses[RPGS_TRACKING].fpEffectiveValue * fpSModVision;
    }
    float GetFSVisionDetect() const
    {
        return rpgUnitStats.percepts[RPGS_TRACKING].fpEffectiveValue;
    }
    float GetFSSmellSense() const { return rpgUnitStats.senses[RPGS_SMELL].fpEffectiveValue; }
    float GetFSSmellDetect() const { return rpgUnitStats.percepts[RPGS_SMELL].fpEffectiveValue; }
    float GetFSSoundSense() const
    {
        return rpgUnitStats.senses[RPGS_HEARING].fpEffectiveValue * fpSModSound;
    }
    float GetFSSoundDetect() const
    {
        return rpgUnitStats.percepts[RPGS_HEARING].fpEffectiveValue * fpModSound;
    }
    float GetFSSoundNativeDetect() const
    {
        return rpgUnitStats.percepts[RPGS_HEARING].fpEffectiveValue;
    }
    // float GetMaxFootstepRange() const { return Max( GetFSVisionSense(), GetFSSmellSense() ); }
    float GetMaxVisionRange() const { return Max(GetVisionSense(), GetLifeSense()); }
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CUnitServer"; }
    SL0AnimationSeq *GetExtendedAnimationSequence(unsigned kind);
    bool RestoreNavigation();
    virtual EClassIDs WhoAmI() const { return HPC_UNITSERVERSPECIFIC; }
public:
    virtual void Store(SaveSchema::Writer &) const;
    virtual void Load(SaveSchema::Reader &);
    virtual void DiscardSave();
private:
    // AD07 selection evidence is transient. A loaded ring must be reselected.
    const CMapObject* modLeverTarget = nullptr;
    ObjectId modLeverId = 0;
    CPlot3D modLeverPoint;
    bool modLeverRing = false;
};
/////////////////////////////////////////////////////////////////////////////////////
// Terrain check for spell cast
extern bool IsSpellCastingDirect(CWorldServer *pWS, CSpell *pSpell, CUnit *pWho,
                                 const CPlot3D &ptFrom, CUnit *pTarget, const CPlot3D &ptTarget);
