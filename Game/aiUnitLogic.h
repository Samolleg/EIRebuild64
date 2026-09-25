#pragma once
/**********************************************************************
 *<
        FILE: aiUnitLogic.h

        DESCRIPTION:  CAIUnitLogic definition

 *>	Copyright (c) 1999, Nival Interactive
 **********************************************************************/

#include "aiScanUnits.h"
#include "aiMap.h"
#include "GeneralUnitLogic.h"
#include "SaveLoader.h"
#include "basic.h"
#include "Script.h"

/////////////////////////////////////////////////////////////////////////////////////
// state of a unit
enum EUnitState
{
    US_IDLE = 0,
    US_MOVE,
    US_TURNLOOK,
    US_ATTACK,
    US_CAST,
    US_SKILL,
    US_FOLLOW
};
/////////////////////////////////////////////////////////////////////////////////////
class CUnitServer;
class CAIScanUnits;
class CAIUnitLogic;
class CSpell;
class CItemWeapon;

/////////////////////////////////////////////////////////////////////////////////////
struct SAITurnLookOrder
{
    float fpAngle;     // угол в направление которого нужно смотреть
    float fpTurnSpeed; // скорость разворота в направлении fpAngle
    float fpTime;      // время, в течение которого нужно в этом направлении смотреть в сегментах
    bool bRest;        // отдыхает ли юнит в этой точке
    SAITurnLookOrder() = default;
    SAITurnLookOrder(float fpA, float fpT, float fpTS, bool bR)
        : fpAngle(fpA)
        , fpTime(fpT)
        , fpTurnSpeed(fpTS)
        , bRest(bR)
    {
    }
    void Load(CSave &file);
    void Store(CSave &file);
};
typedef CAArray<SAITurnLookOrder> CAITLOrders;
/////////////////////////////////////////////////////////////////////////////////////
// точка маршрута патруля
struct SAIRallyPoint
{
    CPlot3D ptPoint;
    CAITLOrders lookOrders;
    void Clear()
    {
        ptPoint = CPlot3D(0, 0, 0);
        lookOrders.SetSize(0);
    }
    void AddLookOrder(float fpAngle, float fpTime, float fpTurnSpeed, bool bRest)
    {
        lookOrders.Add(SAITurnLookOrder(fpAngle, fpTime, fpTurnSpeed, bRest));
    }
    SAIRallyPoint() { Clear(); }
    SAIRallyPoint(const CPlot3D &point)
    {
        Clear();
        ptPoint = point;
    }
    void Load(CSave &file)
    {
        file.Read(&ptPoint, sizeof(ptPoint));
        LoadCArray<SAITurnLookOrder>(lookOrders, file);
    }
    void Store(CSave &file)
    {
        file.Write(&ptPoint, sizeof(ptPoint));
        StoreCArray<SAITurnLookOrder>(lookOrders, file);
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// использование боевого потенциала юнита
class CAISpellCaster
{
public:
    class CEnemy
    {
    protected:
        int nAttackNumber; // -1 - melee, other are spell number
        CUnit *pEnemy;

    public:
        CEnemy()
        {
            pEnemy = 0;
            nAttackNumber = -1;
        }
        CEnemy(CUnit *_pEnemy)
            : pEnemy(_pEnemy)
        {
            nAttackNumber = -1;
        }
        operator CUnit *() { return pEnemy; }
        void Clear() { pEnemy = 0; }
        bool IsEmpty() { return pEnemy == 0; }
        bool operator==(const CEnemy &a) const
        {
            return pEnemy == a.pEnemy && nAttackNumber == a.nAttackNumber;
        }
        friend class CAISpellCaster;
    };

private:
    enum EAttackType
    {
        AT_WEAPON,
        AT_DIRECT_DAMAGE,
        AT_AREA_DAMAGE,
        AT_CONT_AREA_DAMAGE,
        AT_DISBONUS,
        AT_COMBAT_BONUS,
        AT_PIECEFUL_BONUS,
        AT_HEALING,
        AT_USELESS
    };
    class CEnemyInfo : public CEnemy
    {
    public:
        float fpCost;
        CEnemyInfo() = default;
        CEnemyInfo(CUnit *pEnemy, int _nAttack, float _fpCost)
            : CEnemy(pEnemy)
            , fpCost(_fpCost)
        {
            nAttackNumber = _nAttack;
        }
        friend class CAISpellCaster;
    };
    struct SAIPotentialAttack
    {
        EAttackType type;
        int nAttackNumber;
        float fpRange;
        int nTargets; // number of additional targets
        union
        {
            float fpDamageRadius; // radius of damage for area spells or power of healing
            float fpHealPower;
        };
        union
        {
            int nDuration; // for CONT_AREA
            int nSpellID;  // for BONUS/DISBONUS
        };
        SRPGGeneralAttack attack; // description of maximal possible damage

        void Load(CSave &file);
        void Store(CSave &file);
        float GetDamage();
    };

private:
    CArray<SAIPotentialAttack, SAIPotentialAttack &> attacks;
    CArray<SAIPotentialAttack, SAIPotentialAttack &> heals;
    // средние наносимые юнитом повреждения, характеризуют его ценность
    float fpAvrgDamage;
    CItemWeapon* skillWeapon=nullptr;
    DWORD skillWeaponType=0;
    CSpell* skillSlots[16]={};
    bool skillCacheValid=false,skillCompanion=false;
    //
    void FillAttackData(SAIPotentialAttack &a, EAttackType type, int nNumber, float fpRange,
                        int nTargets, float fpDamageRadius, int nDuration);
    SRPGGeneralAttack &AddAttack(EAttackType type, int nNumber, float fpRange = 0, int nTargets = 0,
                                 float fpDamageRadius = 0, int nDuration = 0);
    SRPGGeneralAttack &AddHeal(EAttackType type, int nNumber, float fpRange = 0, int nTargets = 0,
                               float fpDamageRadius = 0, int nDuration = 0);
    SAIPotentialAttack *GetAttack(int nAttackNumber);
    EAttackType GetSpellType(CSpell *pSpell);
    bool IsValidHealToCast(EAttackType type, bool bHealOnly);
    float GetMinimalDistance(CUnit *pUnit, EAttackType at);
    void GetCurrentEnemy(CAIUnitLogic &logic, CEnemy &res);
    bool IsSafeAttack(CAIUnitLogic &logic, CEnemy &a);
    float CalcAttackCost(const CPlot3D &ptPreferred, CUnit *pCaster, CUnitList &units,
                         CUnit *pCurrentTarget, CUnit *pTarget, float fpLowBorder,
                         SAIPotentialAttack &attack);

public:
    void Initialize(CUnit *pUnit);
    void RefreshSkillCache(CUnit*);
    bool HasWeaponSkills(CUnit*);
    bool IsWeaponSkillChoice(CAIUnitLogic&,const CEnemy&);
    void SelectRevengeSpell(CAIUnitLogic&,CEnemy&);
    // интерфейс к системе выбора лучшей цели и выполнение выбранной команды
    void FindBestTarget(CAIUnitLogic &logic, CEnemy &res, CUnitList &units,
                        const CPlot3D &ptPrefered, bool bHeal, bool bDoNotMove = false);
    bool CanAttackEnemyFrom(CAIUnitLogic &logic, const CPlot3D &ptFrom, const CEnemy &target);
    bool CanAttackEnemy(CAIUnitLogic &logic, const CEnemy &target, bool bDoNotMove);
    void AttackEnemy(CAIUnitLogic &logic, const CEnemy &target);
    bool IsEngaging(CAIUnitLogic &logic, const CEnemy &target);
    // во время невозможности выполнять атакующие действия можно подлечивать своих
    void CastHeals(CAIUnitLogic &logic, bool bDoNotMove); // when situation is peaceful
    bool HasHeals() { return heals.GetSize() != 0; }
    //
    void Load(CSave &file);
    void Store(CSave &file);
    friend struct SAIPotentialAttack;
};
/////////////////////////////////////////////////////////////////////////////////////
enum EAIMotivation
{
    AI_UM_GUARD,
    AI_UM_PATROL,
    AI_UM_SENTRY,
    AI_UM_SUSPECTION,
    AI_UM_AGGRESSION,
    AI_UM_CORPSEWATCHER,
    AI_UM_PLAYER,
    AI_UM_FEAR,
    AI_UM_TAMED,
    AI_UM_IDLE,
    AI_UM_FOLLOW
};
/////////////////////////////////////////////////////////////////////////////////////
class CAIUnitMotivation : public CObjectBase
{
public:
    // process new situation & get motivation`s power ( to check aganist other motivations )
    virtual int GetPower(CAIUnitLogic &logic) = 0;
    // control the unit
    virtual void CommandUnit(CAIUnitLogic &logic) = 0;
    // start using the motivation
    virtual void StartUsing(CAIUnitLogic &logic);
    // finish using the motivation
    virtual void FinishUsing(CAIUnitLogic &logic) {}
    virtual EAIMotivation GetType() = 0;
    virtual bool IsDuty() { return FALSE; }
    virtual void RemoveReferencesOn(CMapObject *pTarget) {}
    virtual void Load(CSave &file) {}
    virtual void Store(CSave &file) {}
};
/////////////////////////////////////////////////////////////////////////////////////
// abstract class implementing common things for duty motivations
class CUMDuty : public CAIUnitMotivation
{
public:
    virtual bool IsDuty() { return TRUE; }
    virtual int GetPower(CAIUnitLogic &logic);
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual void StartUsing(CAIUnitLogic &logic);
};
/////////////////////////////////////////////////////////////////////////////////////
// guard - call of duty unit`s motivation, forces unit to walk around some area and
// look into different sides
class CUMGuard : public CUMDuty
{
    OBJECT_BASIC_METHODS(CUMGuard);

private:
    CPlot3D ptGuard;
    float fpGuardRadius, fpStayIdleTime;
    int nGuardFail;

public:
    // радиус в метрах, время в секундах ( максимальное )
    void Set(const CPlot3D &point, float fpRadius, float fpIdleTime);
    CUMGuard() { nGuardFail = 0; }
    CUMGuard(const CPlot3D &pt, float fpRadius, float fpIdleTime = -1)
    {
        Set(pt, fpRadius, fpIdleTime);
    }
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual EAIMotivation GetType() { return AI_UM_GUARD; }
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// patrol - call of duty unit`s motivation, forces unit to walk on the route and
// look into different sides at the definite points
class CUMPatrol : public CUMDuty
{
    OBJECT_BASIC_METHODS(CUMPatrol);

private:
    CAArray<SAIRallyPoint> patrol; // точки для патрулирования
    int nCurrentPatrolPoint;       // текущая точка патруля
public:
    CUMPatrol();
    void ClearPatrolRoute() { patrol.SetSize(0); }
    void AddPatrolPoint(const CPlot3D &plot);
    SAIRallyPoint *GetLastPatrolPoint();
    void MirrorPatrolRoute();
    virtual int GetPower(CAIUnitLogic &logic);
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual EAIMotivation GetType() { return AI_UM_PATROL; }
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// sentry - call of duty unit`s motivation, forces unit to stay on some point and
// look into different sides
class CUMSentry : public CUMDuty
{
    OBJECT_BASIC_METHODS(CUMSentry);

private:
    CPlot3D ptSentry;

public:
    void Set(const CPlot3D &point);
    CUMSentry() {}
    CUMSentry(const CPlot3D &plot) { Set(plot); }
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual EAIMotivation GetType() { return AI_UM_SENTRY; }
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// follow - unit wants to sentry - call of duty unit`s motivation, forces unit to stay on some point
// and look into different sides
class CUMFollow : public CUMDuty
{
    OBJECT_BASIC_METHODS(CUMFollow);

private:
    CMapObjectPtr pFollowTo;

public:
    CUMFollow() {}
    CUMFollow(CMapObject *pTarget)
        : pFollowTo(pTarget)
    {
    }
    virtual void RemoveReferencesOn(CMapObject *pTarget);
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual EAIMotivation GetType() { return AI_UM_FOLLOW; }
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// suspection - when unit sees some footstep or hear strange sound
class CUMSuspection : public CAIUnitMotivation
{
    OBJECT_BASIC_METHODS(CUMSuspection);

private:
    CPlot3D ptUnderSuspection;
    int nPower;

public:
    virtual int GetPower(CAIUnitLogic &logic);
    // virtual CPlot3D GetInterestPoint( CAIUnitLogic &logic );
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual void StartUsing(CAIUnitLogic &logic);
    virtual void FinishUsing(CAIUnitLogic &logic);
    virtual EAIMotivation GetType() { return AI_UM_SUSPECTION; }
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// aggression - attack enemy in sight
class CUMAggression : public CAIUnitMotivation
{
    OBJECT_BASIC_METHODS(CUMAggression);

private:
    CMapObjectPtr pTarget; // для припоминания врагов в revenge
    bool bRevenge;
    CAISpellCaster::CEnemy tempEnemy; // for temprorary storage only
                                      // int nRelaxation, nCurrentRelaxStart;
public:
    CUMAggression() { bRevenge = false; }
    CUMAggression(bool bRev) { bRevenge = bRev; }
    void MakeRevenge() { bRevenge = true; }
    bool IsRevenge() { return bRevenge; }
    virtual int GetPower(CAIUnitLogic &logic);
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual void StartUsing(CAIUnitLogic &logic);
    virtual void FinishUsing(CAIUnitLogic &logic);
    virtual EAIMotivation GetType() { return AI_UM_AGGRESSION; }
    virtual void RemoveReferencesOn(CMapObject *_pTarget)
    {
        if (pTarget == _pTarget)
            pTarget = NULL;
    }
    CMapObject *GetTarget() { return pTarget; }
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// corpse watchin is very interesting occupation... for some special people :)
class CUMCorpseWatcher : public CAIUnitMotivation
{
    OBJECT_BASIC_METHODS(CUMCorpseWatcher);

public:
    virtual int GetPower(CAIUnitLogic &logic);
    virtual void CommandUnit(CAIUnitLogic &logic) { ASSERT(0); }
    virtual EAIMotivation GetType() { return AI_UM_CORPSEWATCHER; }
};
/////////////////////////////////////////////////////////////////////////////////////
// motivation fear
class CUMFear : public CAIUnitMotivation
{
    OBJECT_BASIC_METHODS(CUMFear);
    float fpFearRadius;
    float fpRandomShift;
    CPlot3D ptDangerousPlace;
    CPlot3D ptPreparedRunaway;
    bool bFearPlayer;

public:
    CUMFear(bool _bFearPlayer = false);
    virtual int GetPower(CAIUnitLogic &logic);
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual void StartUsing(CAIUnitLogic &logic);
    virtual void FinishUsing(CAIUnitLogic &logic);
    virtual EAIMotivation GetType() { return AI_UM_FEAR; }
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
// player motivation: do nothing ( do not even start idle )
class CUMPlayer : public CAIUnitMotivation
{
    OBJECT_BASIC_METHODS(CUMPlayer);

private:
    enum ECmdState
    {
        PC_IDLE,
        PC_MOVE,
        PC_SWARM
    };

    CMapObjectPtr pTarget;
    CMapObjectPtr pFollowTo;
    CPlot3D ptSwarmTarget;
    ECmdState cmdState;
    bool bRun;

    void SeekTargets(CAIUnitLogic &logic);
    void AddAttackedTargets(CAIUnitLogic &logic, CUnitList &targets);
    bool IsVisibleTarget(CAIUnitLogic &logic, CUnit *pUnit);
    bool IsCombat(CAIUnitLogic &logic);
    bool AreFollowersInRange(CAIUnitLogic &logic, bool bWider);
    void StartMoving(CAIUnitLogic &logic, bool bGroup);
    void GetGroup(CAIUnitLogic &logic, CUnitList &group);
    bool DoPathExist(CAIUnitLogic &logic, const CPlot3D &ptSrc, const CPlot3D &ptDst);

public:
    CUMPlayer();
    // cmds
    void DoMoveTo(CAIUnitLogic &logic, const CPlot3D &ptTarget, bool bGroup, bool _bRun,
                  const CPlot3D &ptOffset);
    void DoSwarm(CAIUnitLogic &logic, const CPlot3D &ptTarget, bool bGroup, bool _bRun,
                 const CPlot3D &ptOffset);
    void DoAttack(CAIUnitLogic &logic, CMapObject *pTarget, ERPGHitLocations hl, bool _bRun);
    void DoSkill(CAIUnitLogic &logic, CMapObject *pTarget, int nSkill, int nItemID, bool _bRun);
    void DoCast(CAIUnitLogic &logic, int nSpell, CMapObject *pTarget, bool _bRun);
    void DoCast(CAIUnitLogic &logic, int nSpell, const CPlot3D &ptTarget, bool _bRun);
    void DoFollow(CAIUnitLogic &logic, CUnit *pLeader);
    void DoStop(CAIUnitLogic &logic);
    //
    CUnit *GetLeader() const { return pFollowTo; }
    virtual int GetPower(CAIUnitLogic &logic) { return 10; }
    virtual void CommandUnit(CAIUnitLogic &logic);
    virtual void RemoveReferencesOn(CMapObject *_pTarget);
    virtual EAIMotivation GetType() { return AI_UM_PLAYER; }
    virtual void Load(CSave &file);
    virtual void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
class CUMTamed : public CUMPlayer
{
    OBJECT_BASIC_METHODS(CUMTamed);

public:
    virtual int GetPower(CAIUnitLogic &logic) { return 100000000; }
    virtual EAIMotivation GetType() { return AI_UM_TAMED; }
};
/////////////////////////////////////////////////////////////////////////////////////
// CAIUnitLogic - motivations management and elementary commands executing
class CAIUnitLogic
{
private:
    CUnitServer *pUnitServer;
    EUnitState state; // текущая команда, выполняемая монстром
    SAIRallyPoint
        currentRally; // текущая точка патруля/ охраны - для выполнения команды US_TURNLOOK
    union
    {
        int nLookFinishTime; // время когда надо заканчивать смотреть по сторонам, во время
                             // us_turnlook
        int nSpellBeingCast; // корректно во время us_cast
    };
    CMapObjectPtr pTarget; // цель логики нижнего уровня ( attack/follow/cast )
    CMapObjectPtr pHitBy;  // кто нас ударил
    char nAlarmInvoked;    // аларм уже вызывался этим юнитом
    char nAlarmNumber;     // номер аларма, который должен быть вызван при удовлетворении условий
    char nCurrentAlarm;
    bool bAlwaysOnTheRun; // юнит всегда активен
    float fpCallForHelpRadius;
    EAlarmCondition alarmCondition;
    int nAlarmDelay; // время, осавшееся до реального вызова аларма, если nAlarmInvoked == 1
    // параметры для определения зоны интересов юнита
    float fpSuspectRadius; // расстояние на котором юнит интересуется происходящими событиями
    float fpAttackRadius;  // расстояние на котором юнит способен атаковать врагов
    float
        fpRadiusKoef; // множитель к предыдущим двум радиусам для удаления четкой границы интересов
    CPlot3D ptPreviousPoint, ptCurrentPoint; // две точки, задающие область интересов
    int nGuardPntFail; // counter to reduced overhead of guard point search in bad cases
    int nFollowRest;

    CList<CObj<CAIUnitMotivation>, CAIUnitMotivation *> motivations;
    CPtr<CAIUnitMotivation> pControl; // под чьим контролем находится юнит в данный момент

    CAArray<SGeneralUnitLogic, SGeneralUnitLogic &> alarmLogics;

private:
    // выполнение сложных низкоуровневых команд
    void ExecFollow();
    void ExecAttack();
    void ExecTurnNLook();

    // пересчитать флажок видимости врага
    void RecalcEnemyVisible();
    // установить новую логику поведения
    void SetLogic(SGeneralUnitLogic &logic, int nAlarm);
    // вызвать аларм
    void InvokeAlarm();
    // определить лежит ли точка ptPlace в области интересов юнита с радиусом fpRadius
    bool IsInArea(const CPlot3D &ptPlace, float fpRadius);
    // иницализировать радиусы области интересов
    void InitRadiuses();
    void PatchCallForHelpRadius();

public:
    EUnitState GetScriptState() const { return state; }
    void ResetScriptTarget(){environment.Clear();if(nCurrentAlarm>=0 && nCurrentAlarm<alarmLogics.GetSize())SetLogic(alarmLogics[nCurrentAlarm],nCurrentAlarm);}
    CAISpellCaster caster;
    CAIUnitEnvironment environment; // окружающая обстановка юнита, регулярно обновляется
    bool bEnemyVisible;             // рассчитываемая каждый сегмент переменная

    // general functions
    void Clear();
    void ClearRefs();
    ~CAIUnitLogic() { Clear(); }
    CAIUnitLogic() { Clear(); }
    CAIUnitLogic(const CAIUnitLogic &) { ASSERT(0 && "copy of unit logic is prohibited"); }
    CAIUnitLogic &operator=(const CAIUnitLogic &)
    {
        ASSERT(0 && "assignment of unit logic is prohibited");
        return *this;
    }
    // инициализация
    void Set(CUnitServer *pUS);
    void Segment();
    // убрать ссылки на pTarget
    void RemoveReferencesOn(CMapObject *pTarget);
    void ForceRescan() { environment.ForceNewListsForming(); }
    // информировать, что аларм nAlarm был вызван и возможно нужно сменить логику
    void AlarmWasInvoked(int nAlarm);
    bool GenerateGuardRallyPoint(const CPlot3D &ptCenter, float fpRadius, CPlot3D &ptRes);
    // позвать на помощь в точку ptPoint
    void CallForHelp(const CPlot3D &ptPoint);
    float GetCallForHelpRadius() const { return fpCallForHelpRadius; }
    //
    CAIUnitMotivation *GetCurrentMotivation() { return pControl; }
    EAIMotivation GetCurrentMotivationType();
    CAIUnitMotivation *FindMotivation(EAIMotivation motive);
    CPlot3D GetCurrentRally() { return currentRally.ptPoint; }
    CMapObject *GetCurrentTarget() const { return pTarget; }
    //
    CUMPlayer *GetPlayer();
    CMapObject *GetAggressionTarget();
    CUnit *GetPlayerLeader();
    CUnit *GetPlayerLeaderExact();
    // get lists
    CMapObjectPtrList *GetVisibleList() { return environment.GetVisibleList(); };
    CMapObjectPtrList *GetUnitsList() { return environment.GetUnitsList(); };
    CUnitServer *GetUnitServer() { return pUnitServer; }
    CUnit *GetUnit();
    // проверки на принадлежность точки области интересов юнита, если спрашивает текущая
    // активная мотивация, она имеет бонус по дальности, заданный константой
    bool IsInSuspectionRange(const CPlot3D &ptPlace, CAIUnitMotivation *pWhoAsk);
    bool IsInAttackRange(const CPlot3D &ptPlace, CAIUnitMotivation *pWhoAsk);
    bool IsInHealCastRange(const CPlot3D &ptPlace);
    void SetNewRandomRadiusKoef();
    void SetNewAreaPoint(const CPlot3D &ptCenter, float fpSuspect, float fpAttack);
    // motivations of behaviour. Set by script or server, use standard logics please
    void ClearMotivations();
    void AddMotivation(CAIUnitMotivation *pMotive);
    void AddSentry(const CPlot3D &pos) { AddMotivation(dbgnew CUMSentry(pos)); }
    void AddGuard(const CPlot3D &pos, float fpRadius, float fpIdleTime = -1)
    {
        AddMotivation(dbgnew CUMGuard(pos, fpRadius, fpIdleTime));
    }
    void AddPatrol() { AddMotivation(dbgnew CUMPatrol); }
    void AddSuspection() { AddMotivation(dbgnew CUMSuspection); }
    void AddAggression() { AddMotivation(dbgnew CUMAggression); }
    void AddRevenge() { AddMotivation(dbgnew CUMAggression(true)); }
    void AddCorpseWatcher() { AddMotivation(dbgnew CUMCorpseWatcher); }
    void AddFear(bool bFearPlayer = false) { AddMotivation(dbgnew CUMFear(bFearPlayer)); }
    void AddFollow(CMapObject *pTarget) { AddMotivation(dbgnew CUMFollow(pTarget)); }
    void RemoveMotivation(EAIMotivation mot);
    CUMPatrol *GetPatrol(); // search list of motives and find patrol
    // some standard unit logics
    void SetGuard(const CPlot3D &pos, float fpRadius);
    void SetSentry(const CPlot3D &pos);
    void SetPlayer()
    {
        ClearMotivations();
        AddMotivation(dbgnew CUMPlayer);
        StartIdle();
    }
    void SetIdle()
    {
        ClearMotivations();
        StartIdle();
    }
    void SetLogics(SGeneralUnitLogic *pLogics, int nNumber);
    // utility functions
    // создать список направлений в которые юнит должен посмотреть
    void GenerateTurnNLookOrders(SAIRallyPoint &rp, const CPlot3D &majorLook,
                                 float fpStayIdleTime = -1, bool bRest = false);
    // state checking functions
    bool IsIdling() { return state == US_IDLE; }
    bool IsAttacking() { return state == US_ATTACK; }
    bool IsCasting() { return state == US_CAST; }
    bool IsEngaging(CMapObject *pVictim)
    {
        return (IsAttacking() || IsCasting()) && pTarget == pVictim;
    }
    bool IsMoving() { return state == US_MOVE; }
    bool IsFollowing() { return state == US_FOLLOW; }
    bool IsAlwaysActive() { return bAlwaysOnTheRun; }
    int GetSpellNumBeignCast() const { return state == US_CAST ? nSpellBeingCast : -1; }
    // execute different low level commands
    void StartIdle();
    void StartMove(const CPlot3D &ptDest, bool bRun = false, bool bGroup = false);
    void ShowMove(const CPlot3D &ptDest, bool bGroup);
    void StartRotate(const CPlot3D &vecDest, float fpTurnSpeed = 0);
    void StartTurnLook(const SAIRallyPoint &newRally);
    void StartPlayerAttack(CMapObject *pVictim, ERPGHitLocations hl, bool bRun);
    void StartAttack(CMapObject *pVictim);
    void StartUsingSkill(CMapObject *pVictim, int nSkill, int nItemID, bool bRun);
    void StartCast(int nQSpellNum, CMapObject *pTarg, bool bRun);
    void StartCast(int nQSpellNum, const CPlot3D &pointToCast, bool bRun);
    void StartFollow(CMapObject *pVictim);
    void ResetLogic();
    // ignored list interface
    bool IsIgnored(CMapObject *pTarget) { return environment.IsIgnored(pTarget); }
    void AddIgnoredTarget(CMapObject *pTarget, int nTime)
    {
        environment.AddIgnoredTarget(pTarget, nTime);
    }
    // some callbacks
    void TakeDamage(CMapObject *pAttacker);
    CMapObject *GetLastHitBy() { return pHitBy; }
    bool WasHitByAlly();
    // load&store
    void Load(CSave &file);
    void Store(CSave &file);
};
/////////////////////////////////////////////////////////////////////////////////////
