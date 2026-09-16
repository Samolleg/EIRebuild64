/**********************************************************************
 *<
        FILE: aiUnitLogic.cpp

        DESCRIPTION:  CAIUnitLogic implementation

 *>	Copyright (c) 1999, Nival Interactive
 **********************************************************************/
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiUnitLogic.h"
#include "WorldServer.h"
#include "UnitServer.h"
#include "PlayerServer.h"
#include "MapObjectServer.h"
#include "unit.h"
#include "events.h"
#include "console.h"
#include "aiAck.h"
#include "randomGen.h"
#include "aiSettings.h"
#include "ackFormat.h"
#include "GSVars.h"
#include "ServerConsts.h"

/////////////////////////////////////////////////////////////////////////////////////
const int AI_NO_MOTIVATION_POWER = -1000;
/////////////////////////////////////////////////////////////////////////////////////
static ostream &aiLog = NConsole::ai;
/////////////////////////////////////////////////////////////////////////////////////
#ifndef _DEBUG
#pragma optimize("agw", on)
#endif
/////////////////////////////////////////////////////////////////////////////////////
// constants
// given in meters
// bonus of current motivation power - helps to change motivations less frequently
const int N_CURRENT_MOTIVATION_BONUS = 100;
// follow distance, when unit is this distance from another he move
const float FP_FOLLOW_DISTANCE_2 = 4 * 4;
// min follow distance, when unit comes to pastor too close he should stop
const float FP_MIN_FOLLOW_DISTANCE_2 = 2 * 2;
const float FP_MAX_SENTRY_POINT_DEVIATION_2 = 1 * 1;
// расстояние на которое юнит отходит от места патрулирования
// если видит кого-либо и хочет его убить
const float FP_GETAWAY_TO_KILL_RADIUS = 80;
// если заметил нечто подозрительное, это расстояние должно быть больше максимальной
// дистанции атаки с помощью ranged weapon, разница to_kill - to_check должна быть тоже
// больше максимальной дистанции атаки
const float FP_GETAWAY_TO_CHECK_RADIUS = 35;
const float FP_HEALCAST_RADIUS = 30;
// насколько дальше обычных радиусов можно отойти в случае продолжения действия
const float FP_GETAWAY_ACTION_BONUS_RADIUS = 10;
const float FP_SAFE_DISTANCE = 10;
/////////////////////////////////////////////////////////////////////////////////////
// SAITurnLookOrder
void SAITurnLookOrder::Load(CSave &file)
{
    file.Read(fpAngle);
    file.Read(fpTurnSpeed);
    file.Read(fpTime);
    if (file.GetVersion() < 68)
        return;
    file.Read(bRest);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAITurnLookOrder::Store(CSave &file)
{
    file.Write(fpAngle);
    file.Write(fpTurnSpeed);
    file.Write(fpTime);
    file.Write(bRest);
}
/////////////////////////////////////////////////////////////////////////////////////
// CAISpellCaster
/////////////////////////////////////////////////////////////////////////////////////
void CAISpellCaster::SAIPotentialAttack::Load(CSave &file)
{
    if (file.GetVersion() < 90)
    {
        float fpMinDistance;
        file.Read(nAttackNumber);
        file.Read(fpMinDistance);
        attack.Load(file);
        type = nAttackNumber < 0 ? AT_WEAPON : AT_DIRECT_DAMAGE;
        fpRange = 1;
        nTargets = 0;
        fpHealPower = 0;
        nSpellID = 0;
        return;
    }
    file.Read(&type, sizeof(type));
    file.Read(nAttackNumber);
    file.Read(fpRange);
    file.Read(nTargets);
    file.Read(fpDamageRadius);
    file.Read(nDuration);
    attack.Load(file);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAISpellCaster::SAIPotentialAttack::Store(CSave &file)
{
    file.Write(&type, sizeof(type));
    file.Write(nAttackNumber);
    file.Write(fpRange);
    file.Write(nTargets);
    file.Write(fpDamageRadius);
    file.Write(nDuration);
    attack.Store(file);
}
/////////////////////////////////////////////////////////////////////////////////////
float CAISpellCaster::SAIPotentialAttack::GetDamage()
{
    switch (type)
    {
    case AT_WEAPON:
        return attack.GetMaxDamage();
    case AT_DIRECT_DAMAGE:
    case AT_AREA_DAMAGE:
    case AT_CONT_AREA_DAMAGE:
        return attack.GetMaxDamage() * 1.3f;
    case AT_DISBONUS:
    case AT_COMBAT_BONUS:
    case AT_PIECEFUL_BONUS:
        return 1;
    case AT_HEALING:
        return 10;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAISpellCaster::FillAttackData(SAIPotentialAttack &a, EAttackType type, int nNumber,
                                    float fpRange, int nTargets, float fpDamageRadius,
                                    int nDuration)
{
    a.fpRange = fpRange;
    a.nAttackNumber = nNumber;
    a.type = type;
    a.nTargets = nTargets;
    a.fpDamageRadius = fpDamageRadius;
    a.nDuration = nDuration;
}
/////////////////////////////////////////////////////////////////////////////////////
SRPGGeneralAttack &CAISpellCaster::AddAttack(EAttackType type, int nNumber, float fpRange,
                                             int nTargets, float fpDamageRadius, int nDuration)
{
    attacks.SetSize(attacks.GetSize() + 1);
    SAIPotentialAttack &a = attacks[attacks.GetSize() - 1];
    FillAttackData(a, type, nNumber, fpRange, nTargets, fpDamageRadius, nDuration);
    return a.attack;
}
/////////////////////////////////////////////////////////////////////////////////////
SRPGGeneralAttack &CAISpellCaster::AddHeal(EAttackType type, int nNumber, float fpRange,
                                           int nTargets, float fpDamageRadius, int nDuration)
{
    heals.SetSize(heals.GetSize() + 1);
    SAIPotentialAttack &a = heals[heals.GetSize() - 1];
    FillAttackData(a, type, nNumber, fpRange, nTargets, fpDamageRadius, nDuration);
    return a.attack;
}
/////////////////////////////////////////////////////////////////////////////////////
CAISpellCaster::SAIPotentialAttack *CAISpellCaster::GetAttack(int nAttackNumber)
{
    int i;
    for (i = 0; i < attacks.GetSize(); i++)
    {
        if (attacks[i].nAttackNumber == nAttackNumber)
            return &attacks[i];
    }
    for (i = 0; i < heals.GetSize(); i++)
    {
        if (heals[i].nAttackNumber == nAttackNumber)
            return &heals[i];
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
CAISpellCaster::EAttackType CAISpellCaster::GetSpellType(CSpell *pSpell)
{
    if (!pSpell)
        return AT_WEAPON;
    switch (pSpell->GetSubtypeID() >> 16)
    {
    case SPELL_ID_ACIDRAY:
    case SPELL_ID_FIREARROW:
    case SPELL_ID_LIGHTNING:
    case SPELL_ID_RICKMAGIC:
    case SPELL_ID_CURSEMAGIC:
        return AT_DIRECT_DAMAGE;
    case SPELL_ID_FIREBALL:
    case SPELL_ID_INVOKE_LGHTNG:
    case SPELL_ID_COLUMN_ACID:
        return AT_AREA_DAMAGE;
    case SPELL_ID_FIREWALL:
    case SPELL_ID_WALL_LGHTNG:
    case SPELL_ID_POISONFOG:
        return AT_CONT_AREA_DAMAGE;
    case SPELL_ID_WEAKEN:
    case SPELL_ID_FEEBLEMIND:
    case SPELL_ID_STUN:
        return AT_DISBONUS;
    case SPELL_ID_PROT_FIRE:
    case SPELL_ID_PROT_ELECTRIC:
    case SPELL_ID_PROT_ACID:
    case SPELL_ID_STRENGTHEN:
    case SPELL_ID_ANTIMAGIC:
        return AT_COMBAT_BONUS;
    case SPELL_ID_EAGLE_SIGHT:
    case SPELL_ID_INFRAVISION:
    case SPELL_ID_DETECT_LIFE:
    case SPELL_ID_INVISIBILITY:
    case SPELL_ID_SILENCE:
    case SPELL_ID_LICHDOM:
    case SPELL_ID_REGENERATION:
    case SPELL_ID_SPEED:
        return AT_PIECEFUL_BONUS;
    case SPELL_ID_HEALING:
        return AT_HEALING;
    case SPELL_ID_STENCH:
    case SPELL_ID_SLOW:
    case SPELL_ID_CHARM:
    case SPELL_ID_ENLARGE:
    case SPELL_ID_SHRINK:
    case SPELL_ID_CAMPFIRE:
    case SPELL_ID_FIREWORKS:
        return AT_USELESS;
    }
    return AT_USELESS;
}
/////////////////////////////////////////////////////////////////////////////////////
// получить минимально допустимое расстояние для использования спела pSpell
float CAISpellCaster::GetMinimalDistance(CUnit *pUnit, EAttackType at)
{
    switch (at)
    {
    case AT_AREA_DAMAGE:
    case AT_CONT_AREA_DAMAGE:
        if (!pUnit->CanBeDamagedByAreaSpell())
            return 0;
        return 7;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAISpellCaster::Initialize(CUnit *pUnit)
{
    int i;
    CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
    attacks.SetSize(0);
    heals.SetSize(0);
    if (!IsIDofPersonage(pUnit->GetID()))
    {
        if (pUS->GetNativeWeaponType() != RPGWST_MAGIC)
            pUS->GetWeaponAttack(AddAttack(AT_WEAPON, -1, pUS->GetCurrentWeaponRange()));
        for (i = 0; i < QI_MAX_SPELLS; i++)
        {
            CSpell *pSpell = pUnit->GetQSpell(i);
            if (pSpell)
            {
                EAttackType type = GetSpellType(pSpell);
                if (type != AT_USELESS)
                {
                    // float fpMinDistance = GetMinimalDistance( type, pUnit, pSpell );
                    float fpRange = pSpell->GetRange();
                    if (pUS->GetNativeWeaponType() == RPGWST_MAGIC)
                        fpRange = pUS->GetNativeWeaponRange();
                    int nTargets = Max((DWORD)1, pSpell->GetNumTargets()) - 1;
                    float fpDamageRadius = 0;
                    int nDuration = 0;
                    switch (type)
                    {
                    case AT_WEAPON:
                        break;
                    case AT_DIRECT_DAMAGE:
                        break;
                    case AT_AREA_DAMAGE:
                        fpDamageRadius = pSpell->GetRadius();
                        break;
                    case AT_CONT_AREA_DAMAGE:
                        fpDamageRadius = pSpell->GetRadius();
                        nDuration = pSpell->GetDuration();
                        break;
                    case AT_DISBONUS:
                    case AT_PIECEFUL_BONUS:
                    case AT_COMBAT_BONUS:
                        nDuration = pSpell->GetSubtypeID() >> 16;
                        break;
                    case AT_HEALING:
                        fpDamageRadius = pSpell->GetPower();
                        break;
                    }
                    switch (type)
                    {
                    case AT_WEAPON:
                    case AT_DIRECT_DAMAGE:
                    case AT_AREA_DAMAGE:
                    case AT_CONT_AREA_DAMAGE:
                        pUS->GetSpellAttack(i, AddAttack(type, i, fpRange, nTargets, fpDamageRadius,
                                                         nDuration));
                        break;
                    case AT_DISBONUS:
                        AddAttack(type, i, fpRange, nTargets, fpDamageRadius, nDuration);
                        break;
                    case AT_COMBAT_BONUS:
                        // AddAttack( type, i, fpRange, nTargets, fpDamageRadius, nDuration );
                        AddHeal(type, i, fpRange, nTargets, fpDamageRadius, nDuration);
                        break;
                    case AT_PIECEFUL_BONUS:
                    case AT_HEALING:
                        AddHeal(type, i, fpRange, nTargets, fpDamageRadius, nDuration);
                        break;
                    }
                }
            }
        }
    }
    else
        pUS->GetWeaponAttack(AddAttack(AT_WEAPON, -1, pUS->GetCurrentWeaponRange()));
    // calc average damage
    fpAvrgDamage = 0;
    for (i = 0; i < attacks.GetSize(); i++)
        fpAvrgDamage = Max(fpAvrgDamage, attacks[i].GetDamage());
}
/////////////////////////////////////////////////////////////////////////////////////
void CAISpellCaster::AttackEnemy(CAIUnitLogic &logic, const CEnemy &target)
{
    // if ( pEnemy->WhoAmI() == HPC_UNIT && !logic.environment.IsUnitVisible( logic.GetUnit(),
    // (CUnit*) pEnemy ) ) return;
    ASSERT(target.pEnemy);
    if (!target.pEnemy)
        return;
    CEnemy cur;
    GetCurrentEnemy(logic, cur);
    if (target == cur)
        return;
    if (target.nAttackNumber == -1) // bHasSpells )//GetUnit()->GetQSpell( 0 ) )
        logic.StartAttack(target.pEnemy);
    else
        logic.StartCast(target.nAttackNumber, target.pEnemy, false);
}
/////////////////////////////////////////////////////////////////////////////////////
// определить текущего врага
void CAISpellCaster::GetCurrentEnemy(CAIUnitLogic &logic, CEnemy &res)
{
    if (logic.IsAttacking())
    {
        res.nAttackNumber = -1;
        res.pEnemy = (CUnit *)logic.GetCurrentTarget();
        if (!res.pEnemy)
            res.Clear();
        else
        {
            if (res.pEnemy->WhoAmI() != HPC_UNIT)
                res.pEnemy = 0;
        }
    }
    else if (logic.IsCasting())
    {
        res.nAttackNumber = logic.GetSpellNumBeignCast();
        res.pEnemy = (CUnit *)logic.GetCurrentTarget();
        if (!res.pEnemy)
            res.Clear();
        else
        {
            if (res.pEnemy->WhoAmI() != HPC_UNIT)
                res.pEnemy = 0;
        }
    }
    else
        res.Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAISpellCaster::IsEngaging(CAIUnitLogic &logic, const CEnemy &target)
{
    CEnemy test;
    GetCurrentEnemy(logic, test);
    return test == target;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAISpellCaster::CanAttackEnemyFrom(CAIUnitLogic &logic, const CPlot3D &ptFrom,
                                        const CEnemy &target)
{
    SUnitBasicCmd cmd;
    if (target.nAttackNumber < 0)
        cmd.MakeAttack(target.pEnemy);
    else
        cmd.MakeCast(target.nAttackNumber, target.pEnemy);
    return logic.GetUnitServer()->CanAttackFrom(cmd, ptFrom);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAISpellCaster::CanAttackEnemy(CAIUnitLogic &logic, const CEnemy &target, bool bDoNotMove)
{
    SAIPath fakePath;
    CUnit *pAttacker = logic.GetUnit();
    CPlot3D ptFrom;
    // check for spell
    if (target.nAttackNumber >= 0)
    {
        CSpell *pSpell = pAttacker->GetQSpell(target.nAttackNumber);
        ASSERT(pSpell);
        if (pSpell && pSpell->IsOnTile())
        {
            if (!target.pEnemy->CanBeDamagedByAreaSpell())
                return false;
        }
    }
    if (pAttacker == target.pEnemy || bDoNotMove)
        ptFrom = pAttacker->GetCP();
    else
    {
        // check I path to target exist or at least monster can attack target from closest reachable
        // point
        float fpMaxPathLength = pAttacker->GetMaxAttackPathLength(
            fabs(pAttacker->GetCP() - target.pEnemy->GetCP()));
        float fpMinDistance = pAttacker->GetAttackDistance() + target.pEnemy->GetAttackDistance();
        theWorldServer.aiMap.RemoveObject(target.pEnemy);
        if (!theWorldServer.aiMap.NewPath(pAttacker, target.pEnemy->GetCP(), fakePath,
                                          fpMaxPathLength, fpMinDistance))
            ptFrom = pAttacker->GetCP();
        else
            ptFrom = fakePath.GetFinalPoint();
        theWorldServer.aiMap.AddObject(target.pEnemy);
    }
    // some advanced checks
    if (!CanAttackEnemyFrom(logic, ptFrom, target))
        return false;
    // check II attack will not be conducted from dangerous place and no dangerous area is on the
    // way
    //    this check is not applied for now
    // check III if monster posesses area effect spell then attack into place where he will not be
    // damaged
    //    this check is not applied for now
    // some extended test if ! (monster possess only spells and regeneration time for spell is
    // rather large) if all checks are conducted change unit under attack ( pTarget is changed )
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAISpellCaster::IsSafeAttack(CAIUnitLogic &logic, CEnemy &a)
{
    if (a.IsEmpty())
        return true;
    CUnit *pThis = logic.GetUnit();
    SAIPotentialAttack *pAttack = GetAttack(a.nAttackNumber);
    ASSERT(pAttack);
    if (!pAttack)
        return false;

    float fpMinDistance = GetMinimalDistance(pThis, pAttack->type);
    if (fpMinDistance != 0)
    {
        float fpMinDist = sqr(fpMinDistance);
        CPlot3D ptDst = a.pEnemy->GetCP();
        int nOurPlayer = pThis->GetPlayerNum();
        for (POSITION pos = logic.environment.units.GetHeadPosition(); pos;)
        {
            CUnit *pUnit = logic.environment.units.GetNext(pos);
            if (distanceXY2(ptDst, pUnit->GetCP()) < fpMinDist &&
                theWorldServer.fof.IsPlayerAlly(pUnit->GetPlayerNum(), nOurPlayer) &&
                pUnit->CanBeDamagedByAreaSpell())
                return false;
        }
        if (distanceXY2(pThis->GetCP(), ptDst) <= sqr(pAttack->fpDamageRadius))
            return false;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
static WORD CalcGroupDamage(const SRPGGeneralAttack &atk, CUnitList &units, const CPlot3D &ptPos,
                            float fpRadius)
{
    WORD wResult = 0;
    for (POSITION pos = units.GetHeadPosition(); pos;)
    {
        CUnit *pTarget = units.GetNext(pos);
        if (distanceXY2(pTarget->GetCP(), ptPos) > sqr(fpRadius))
            continue;
        CUnitServer *pUS = (CUnitServer *)pTarget->GetUnitSpecific();
        wResult += atk.CalcDamage(pUS->GetUnifiedDefendInfo());
    }
    return wResult;
}
/////////////////////////////////////////////////////////////////////////////////////
// множители к разным слагаемым оценочной функции
const float FP_C_SAMETARGET = 10;
const float FP_C_MAGICATTACK = 3;
const float FP_C_DAMAGEMULT = 1;
const float FP_C_MAGICDAMAGEMULT = 1.3f;
const float FP_C_SAVEDAMAGEMULT = 3;
const float FP_C_RANGECOST = 25; // for meter of range
const float FP_C_KICKPRICE = 2;
float CAISpellCaster::CalcAttackCost(const CPlot3D &ptPreferred, CUnit *pCaster, CUnitList &units,
                                     CUnit *pCurrentTarget, CUnit *pTarget, float fpLowBorder,
                                     SAIPotentialAttack &attack)
{
    WORD wDamage = 0;
    // (+) for keeping target
    float fpCost = pTarget == pCurrentTarget ? FP_C_SAMETARGET : 0;
    CUnitServer *pTUS = (CUnitServer *)pTarget->GetUnitSpecific();
    // attack type dependant bonuses
    // (+) damage from spell by calcing it by specified rules
    // (+) for toHit - all magic attacks has bonus
    // (+) multitarget spells are preferred if multiple enemies exist
    int nTargetsCount = units.GetCount() - 1;
    switch (attack.type)
    {
    case AT_WEAPON:
        wDamage = attack.attack.CalcDamage(pTUS->GetUnifiedDefendInfo());
        fpCost += wDamage * FP_C_DAMAGEMULT;
        break;
    case AT_DIRECT_DAMAGE:
        wDamage = attack.attack.CalcDamage(pTUS->GetUnifiedDefendInfo());
        fpCost += wDamage * FP_C_MAGICDAMAGEMULT;
        fpCost += FP_C_MAGICATTACK;
        fpCost += Min(nTargetsCount, attack.nTargets) * wDamage;
        break;
    case AT_AREA_DAMAGE:
        wDamage = CalcGroupDamage(attack.attack, units, pTarget->GetCP(), attack.fpDamageRadius);
        fpCost += wDamage * FP_C_MAGICDAMAGEMULT;
        fpCost += FP_C_MAGICATTACK;
        fpCost += Min(nTargetsCount + 1, attack.nTargets) * wDamage;
        break;
    case AT_CONT_AREA_DAMAGE:
        if (!pTUS->IsL1Moving())
        {
            wDamage = CalcGroupDamage(attack.attack, units, pTarget->GetCP(),
                                      attack.fpDamageRadius);
            wDamage *= attack.nDuration / 4 / 2;
            fpCost += wDamage * FP_C_MAGICDAMAGEMULT;
            fpCost += FP_C_MAGICATTACK;
            fpCost += Min(nTargetsCount + 1, attack.nTargets) * wDamage;
        }
        else
            return -1e30f;
        break;
    case AT_DISBONUS:
        if (pTarget->HasSpellEffect(attack.nSpellID))
            return -1e30f;
        else
        {
            fpCost += 1;
            fpCost += Min(nTargetsCount, attack.nTargets) * 1;
        }
        break;
    case AT_COMBAT_BONUS:
        if (pTarget->HasSpellEffect(attack.nSpellID))
            return -1e30f;
        else
        {
            fpCost += 1;
            fpCost += Min(nTargetsCount, attack.nTargets) * 1;
        }
        break;
    case AT_PIECEFUL_BONUS:
        if (pTarget->HasSpellEffect(attack.nSpellID))
            return -1e30f;
        else
        {
            fpCost += 1;
            fpCost += Min(nTargetsCount, attack.nTargets) * 1;
        }
        break;
    case AT_HEALING: {
        fpCost += FP_C_MAGICATTACK;
        int nHits = pTarget->GetHP(), nMaxHits = pTarget->GetMaxHP();
        CUnitServer *pTUS = (CUnitServer *)pTarget->GetUnitSpecific();
        nMaxHits = Float2Int(pTUS->GetRPGStats()->CollectMaxHPWithoutDeadHLs(nMaxHits));

        if (nHits > nMaxHits - 2)
            return -1e30f;
        if (nHits * 3 < nMaxHits)
        {
            float fpMult = FP_C_SAVEDAMAGEMULT; // * Min( 1, fpHealPower / pTarget->GetMaxHP() );
            fpCost = pTUS->logic.caster.fpAvrgDamage * fpMult;
            if (pCaster->GetHP() == pCaster->GetMaxHP())
                fpCost += pTUS->logic.caster.fpAvrgDamage;
        }
        else
        {
            fpCost += 2;
            fpCost += Min(nTargetsCount, attack.nTargets) * 2;
        }
    }
    break;
    }
    //
    if (fpCost < fpLowBorder)
        return fpCost;
    // (-) for distance - (distance to unit - attack range)
    float fpAttackRange, fpDistMult = 1;
    switch (attack.type)
    {
    case AT_WEAPON:
    case AT_DIRECT_DAMAGE:
    case AT_AREA_DAMAGE:
    case AT_CONT_AREA_DAMAGE:
    case AT_DISBONUS:
        fpAttackRange = 0;
        break;
    default:
        fpDistMult = 10;
        fpAttackRange = attack.fpRange;
        break;
    }
    float fpDist = sqrt(distanceXY2(pTarget->GetCP(), ptPreferred));
    fpDist = Max(0.0f, fpDist - fpAttackRange);
    fpCost -= fpDist * FP_C_RANGECOST * fpDistMult;
    if (fpCost < fpLowBorder)
        return fpCost;
    // (-) for enemy`s strength - number of hits required to kill him
    switch (attack.type)
    {
    case AT_WEAPON:
    case AT_DIRECT_DAMAGE:
    case AT_AREA_DAMAGE:
    case AT_CONT_AREA_DAMAGE: {
        int nHits = pTarget->GetHP(), nKicks;
        if (wDamage == 0)
            nKicks = 20; // явно долго придется парня пинать
        else
            nKicks = nHits / wDamage;
        fpCost -= nKicks * FP_C_KICKPRICE;
        if (fpCost < fpLowBorder)
            return fpCost;
    }
    break;
    }
    // (-) for being unable attack from current position
    // CRAP not accounted for yet
    return fpCost;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAISpellCaster::CastHeals(CAIUnitLogic &logic, bool bDoNotMove)
{
    CEnemy res;
    CUnitList units;
    CPlot3D ptUnitPos = logic.GetUnit()->GetCP();
    FindBestTarget(logic, res, units, ptUnitPos, true, bDoNotMove);
    if (!res.IsEmpty())
        AttackEnemy(logic, res);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAISpellCaster::IsValidHealToCast(EAttackType type, bool bHealOnly)
{
    if (!bHealOnly)
        return true;
    return type != AT_PIECEFUL_BONUS;
}
/////////////////////////////////////////////////////////////////////////////////////
// эта функция проверяет и возможность атаки тоже
void CAISpellCaster::FindBestTarget(CAIUnitLogic &logic, CEnemy &res, CUnitList &units,
                                    const CPlot3D &ptPrefered, bool bHeal, bool bDoNotMove)
{
    POSITION pos, posBest;
    if (heals.GetSize() == 0 && (units.GetCount() == 0 || attacks.GetSize() == 0))
    {
        res.pEnemy = 0;
        return;
    }
    CList<CEnemyInfo> targets;

    float fpLowBorder;
    CEnemy curEnemy;
    // determine current enemy
    GetCurrentEnemy(logic, curEnemy);
    if (!IsSafeAttack(logic, curEnemy))
        curEnemy.Clear();
    CUnit *pCurrentTarget = curEnemy;
    CUnit *pThis = logic.GetUnit();
    //
    // Calc low border for price of suitable attack
    fpLowBorder = -1e20f;
    if (pCurrentTarget)
    {
        // если потенциальный враг только один, то нефиг париться - выбираем его
        if (units.GetCount() == 1 && heals.GetSize() == 0)
        {
            res = curEnemy;
            return;
        }
        SAIPotentialAttack *pAttack = GetAttack(curEnemy.nAttackNumber);
        if (pAttack)
            fpLowBorder = CalcAttackCost(ptPrefered, pThis, units, pCurrentTarget, pCurrentTarget,
                                         fpLowBorder, *pAttack);
        else
        {
            ASSERT(0);
            pCurrentTarget = 0;
            curEnemy.Clear();
        }
        if (fpLowBorder < -1e20f)
        {
            curEnemy.Clear();
            fpLowBorder = -1e20f;
        }
    }
    else
        curEnemy.Clear();
    // form potential targets list
    // add attacks
    if (!bHeal)
    {
        for (pos = units.GetHeadPosition(); pos;)
        {
            CUnit *pTest = units.GetNext(pos);
            for (int i = 0; i < attacks.GetSize(); i++)
            {
                SAIPotentialAttack &a = attacks[i];
                float fpCost = CalcAttackCost(ptPrefered, pThis, units, pCurrentTarget, pTest,
                                              fpLowBorder, a);
                if (fpCost <= fpLowBorder)
                    continue;
                targets.AddTail(CEnemyInfo(pTest, attacks[i].nAttackNumber, fpCost));
            }
        }
    }
    // add heals
    if (heals.GetSize())
    {
        POSITION pos;
        CUnitList allies;
        // form allies list
        allies.AddTail(pThis); // любой нормальный кастер сам себе друг :)
        for (pos = logic.environment.units.GetHeadPosition(); pos;)
        {
            CUnit *pUnit = logic.environment.units.GetNext(pos);
            if (pUnit->IsDead())
                continue;
            if (logic.environment.ignoredList.IsIgnored(pUnit))
                continue;
            if (theWorldServer.fof.IsPlayerAlly(pThis->GetPlayerNum(), pUnit->GetPlayerNum()))
                allies.AddTail(pUnit);
        }
        // try to cast on them
        for (pos = allies.GetHeadPosition(); pos;)
        {
            CUnit *pTest = units.GetNext(pos);
            for (int i = 0; i < heals.GetSize(); i++)
            {
                SAIPotentialAttack &a = heals[i];
                if (!IsValidHealToCast(a.type, bHeal))
                    continue;
                float fpCost = CalcAttackCost(ptPrefered, pThis, allies, pCurrentTarget, pTest,
                                              fpLowBorder, a);
                if (fpCost <= fpLowBorder)
                    continue;
                targets.AddTail(CEnemyInfo(pTest, heals[i].nAttackNumber, fpCost));
            }
        }
    }
    // choose best and test it for possibility
    while (targets.GetCount())
    {
        float fpCurrent = -1e30f;
        res = curEnemy;
        posBest = 0;
        for (pos = targets.GetHeadPosition(); pos;)
        {
            POSITION curPos = pos;
            CEnemyInfo &info = targets.GetNext(pos);
            if (info.fpCost > fpCurrent)
            {
                res = (CEnemy &)info;
                posBest = curPos;
                fpCurrent = info.fpCost;
            }
        }
        ASSERT(posBest);
        if (posBest == 0)
            return;
        if (res == curEnemy)
            return;
        if (IsSafeAttack(logic, res))
        {
            // в случае heal мы лечим только попадающих в круг наших интересов юнитов
            bool bOk = true;
            if (bHeal && !logic.IsInHealCastRange(res.pEnemy->GetCP()))
                bOk = CanAttackEnemyFrom(logic, pThis->GetCP(), res);
            if (bOk && CanAttackEnemy(logic, res, bDoNotMove))
                return;
        }
        // проверим, если все виды атаки врага исчерпаны, то добавим его в список игнорируемых
        int nCount = 0;
        for (pos = targets.GetHeadPosition(); pos;)
            if (targets.GetNext(pos).pEnemy == res.pEnemy)
                nCount++;
        if (nCount == 1 && res.pEnemy != curEnemy.pEnemy)
            logic.AddIgnoredTarget(res.pEnemy, 20);
        ///
        targets.RemoveAt(posBest);
    }
    res = curEnemy;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAISpellCaster::Load(CSave &file)
{
    fpAvrgDamage = 0;
    LoadCArray(attacks, file);
    if (file.GetVersion() < 90)
        return;
    LoadCArray(heals, file);
    file.Read(fpAvrgDamage);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAISpellCaster::Store(CSave &file)
{
    StoreCArray(attacks, file);
    StoreCArray(heals, file);
    file.Write(fpAvrgDamage);
}
/////////////////////////////////////////////////////////////////////////////////////
// CAIUnitMotivation
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitMotivation::StartUsing(CAIUnitLogic &logic)
{
    logic.StartIdle();
}
/////////////////////////////////////////////////////////////////////////////////////
// basic unit motivation Duty
/////////////////////////////////////////////////////////////////////////////////////
int CUMDuty::GetPower(CAIUnitLogic &logic)
{
    if (!logic.IsInAttackRange(logic.GetUnit()->GetCP(), 0))
        return 4000;
    return 50;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMDuty::CommandUnit(CAIUnitLogic &logic)
{
    if (random.Get(0, 1000) == 1)
        logic.GetUnitServer()->acknowledgements.SayAck(AI_ACK_DURING_IDLE);
    logic.caster.CastHeals(logic, false); //! logic.IsInHealCastRange( logic.GetUnit()->GetCP() ) );
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMDuty::StartUsing(CAIUnitLogic &logic)
{
    CAIUnitMotivation::StartUsing(logic);
    logic.SetNewRandomRadiusKoef();
    logic.GetUnitServer()->SetAggression(false);
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation Guard
/////////////////////////////////////////////////////////////////////////////////////
void CUMGuard::Set(const CPlot3D &point, float fpRadius, float fpIdleTime)
{
    nGuardFail = 0;
    ptGuard = point;
    if (fpRadius < 0)
        fpGuardRadius = theAIGeneralSettings.fpGuardRadius;
    else
        fpGuardRadius = fpRadius;
    if (fpIdleTime < 0)
        fpStayIdleTime = theAIGeneralSettings.fpGuardStayTime;
    else
        fpStayIdleTime = fpIdleTime;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMGuard::CommandUnit(CAIUnitLogic &logic)
{
    float fpRad;
    CPlot3D majorLook;
    CUMDuty::CommandUnit(logic);
    if (logic.IsIdling())
    {
        logic.SetNewAreaPoint(ptGuard, FP_GETAWAY_TO_CHECK_RADIUS + fpGuardRadius,
                              FP_GETAWAY_TO_KILL_RADIUS + fpGuardRadius);
        SAIRallyPoint rp;
        if (!logic.GenerateGuardRallyPoint(ptGuard, fpGuardRadius, rp.ptPoint))
        {
            nGuardFail++;
            if (nGuardFail > 100)
            {
                if (!logic.GenerateGuardRallyPoint(logic.GetUnit()->GetCP(), fpGuardRadius,
                                                   rp.ptPoint))
                    return;
            }
            else
                return;
        }
        nGuardFail = 0;
        majorLook = rp.ptPoint - ptGuard;
        fpRad = fabs(majorLook);
        if (fpGuardRadius)
            majorLook *= 0.5f * (fpRad / fpGuardRadius);
        bool bRest = random.Get(0, 10) == 0;
        logic.GenerateTurnNLookOrders(rp, majorLook, fpStayIdleTime, bRest);
        logic.StartTurnLook(rp);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMGuard::Load(CSave &file)
{
    file.Read(fpGuardRadius);
    file.Read(fpStayIdleTime);
    file.Read(&ptGuard, sizeof(ptGuard));
    if (file.GetVersion() < 102)
        return;
    file.Read(nGuardFail);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMGuard::Store(CSave &file)
{
    file.Write(fpGuardRadius);
    file.Write(fpStayIdleTime);
    file.Write(&ptGuard, sizeof(ptGuard));
    file.Write(nGuardFail);
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation Patrol
/////////////////////////////////////////////////////////////////////////////////////
CUMPatrol::CUMPatrol()
{
    nCurrentPatrolPoint = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPatrol::AddPatrolPoint(const CPlot3D &plot)
{
    int nX = Float2Int(Meter2AITile(plot.x));
    int nY = Float2Int(Meter2AITile(plot.y));
    patrol.Add(SAIRallyPoint(
        CPlot3D(AITile2Meter(float(nX) + 0.5f), AITile2Meter(float(nY) + 0.5f), 0.0f)));
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPatrol::MirrorPatrolRoute()
{
    int nSize = patrol.GetSize();
    // CRAP - problem with CArray, operator[] returns &, function Add takes &, and Add may change
    // array`s size
    patrol.SetSize(nSize * 2);
    patrol.SetSize(nSize);
    // end of CRAP
    for (int i = nSize - 1; i >= 0; i--)
        patrol.Add(patrol[i]);
}
/////////////////////////////////////////////////////////////////////////////////////
SAIRallyPoint *CUMPatrol::GetLastPatrolPoint()
{
    if (patrol.GetSize())
        return &patrol[patrol.GetSize() - 1];
    return NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
int CUMPatrol::GetPower(CAIUnitLogic &logic)
{
    if (patrol.GetSize() == 0)
    {
        ReportError("Unit %i has no patrol points, switching to guard", logic.GetUnit()->GetID());
        AddPatrolPoint(logic.GetUnit()->GetCP());
        return AI_NO_MOTIVATION_POWER;
    }
    return CUMDuty::GetPower(logic);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPatrol::CommandUnit(CAIUnitLogic &logic)
{
    ASSERT(patrol.GetSize());
    CUMDuty::CommandUnit(logic);
    if (patrol.GetSize() == 0)
        return;
    if (logic.IsIdling())
    {
        nCurrentPatrolPoint++;
        ASSERT(nCurrentPatrolPoint >= 0);
        if (nCurrentPatrolPoint >= patrol.GetSize())
            nCurrentPatrolPoint = 0;
        SAIRallyPoint rp = patrol[nCurrentPatrolPoint];
        if (rp.lookOrders.GetSize() <= 0)
            logic.GenerateTurnNLookOrders(rp, CPlot3D(0, 0, 0));
        logic.StartTurnLook(rp);
        logic.SetNewAreaPoint(rp.ptPoint, FP_GETAWAY_TO_CHECK_RADIUS, FP_GETAWAY_TO_KILL_RADIUS);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPatrol::Load(CSave &file)
{
    LoadCArray<SAIRallyPoint>(patrol, file);
    file.Read(nCurrentPatrolPoint);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPatrol::Store(CSave &file)
{
    StoreCArray<SAIRallyPoint>(patrol, file);
    file.Write(nCurrentPatrolPoint);
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation Sentry
/////////////////////////////////////////////////////////////////////////////////////
void CUMSentry::Set(const CPlot3D &point)
{
    ptSentry = point;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMSentry::CommandUnit(CAIUnitLogic &logic)
{
    CUMDuty::CommandUnit(logic);
    if (logic.IsIdling())
    {
        SAIRallyPoint rp;
        rp.ptPoint = ptSentry;
        bool bRest = random.Get(0, 10) == 0;
        if (bRest)
            logic.GenerateTurnNLookOrders(rp, CPlot3D(0, 0, 0),
                                          theAIGeneralSettings.fpGuardStayTime, true);
        else
            logic.GenerateTurnNLookOrders(rp, CPlot3D(0, 0, 0),
                                          theAIGeneralSettings.fpSentryStayTime, false);
        logic.StartTurnLook(rp);
        logic.SetNewAreaPoint(rp.ptPoint, FP_GETAWAY_TO_CHECK_RADIUS, FP_GETAWAY_TO_KILL_RADIUS);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMSentry::Load(CSave &file)
{
    file.Read(&ptSentry, sizeof(ptSentry));
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMSentry::Store(CSave &file)
{
    file.Write(&ptSentry, sizeof(ptSentry));
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation Follow
/////////////////////////////////////////////////////////////////////////////////////
void CUMFollow::RemoveReferencesOn(CMapObject *pTarget)
{
    if (pFollowTo == pTarget)
        pFollowTo = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMFollow::CommandUnit(CAIUnitLogic &logic)
{
    if (logic.IsIdling() && !pFollowTo.IsEmpty())
        logic.StartFollow(pFollowTo);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMFollow::Load(CSave &file)
{
    pFollowTo.Load(file, theWorldServer);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMFollow::Store(CSave &file)
{
    pFollowTo.Store(file);
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation Suspection
/////////////////////////////////////////////////////////////////////////////////////
int CUMSuspection::GetPower(CAIUnitLogic &logic)
{
    CPlot3D &susp = ptUnderSuspection;
    bool bIgnoreDistance = false;
    if (logic.GetLastHitBy() && !logic.WasHitByAlly())
    {
        CPlot3D ptInterest = logic.GetLastHitBy()->GetCP();
        if (logic.IsInSuspectionRange(ptInterest, this))
        {
            logic.environment.AddPoI(ptInterest, AI_PPOWER_HITFROM, AI_PPDEC_HITFROM);
            bIgnoreDistance = true; // if being attacked search from any distance
        }
    }
    const CPlot2D &poi = logic.environment.GetPoI();
    if (poi.x != susp.x || poi.y != susp.y)
    {
        if (bIgnoreDistance || logic.IsInSuspectionRange(cast2D3D(poi), this))
        {
            nPower = logic.environment.GetPoIPower();
            susp = cast2D3D(poi);
        }
        else
            nPower = Min(nPower, logic.environment.GetPoIPower());
    }
    else
        nPower = Min(nPower, logic.environment.GetPoIPower());
    return nPower;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMSuspection::CommandUnit(CAIUnitLogic &logic)
{
    if (logic.IsIdling() || distanceXY2(ptUnderSuspection, logic.GetCurrentRally()) > 0.2f)
    {
        SAIRallyPoint rp;
        if (logic.IsIdling())
        {
            // if monster failed to come closer to object of interest decrease motivation power
            if (logic.GetUnitServer()->IsCmdFailed())
                nPower -= 50;
            // поищем точку для обзора недалеко от подозрительной
            if (!logic.GenerateGuardRallyPoint(ptUnderSuspection, 5, rp.ptPoint))
            {
                nPower -= 5;
                return;
            }
        }
        else
            rp.ptPoint = ptUnderSuspection;
        logic.GenerateTurnNLookOrders(rp, CPlot3D(0, 0, 0));
        logic.StartTurnLook(rp);
        nPower = logic.environment.GetPoIPower();
    }
    else
    {
        // decrease motivation power with time when in range of suspected point
        if (fabs2(logic.GetUnit()->GetCP() - ptUnderSuspection) < 25)
            nPower -= 5;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMSuspection::StartUsing(CAIUnitLogic &logic)
{
    logic.GetUnitServer()->SetAggression(true);
    SAIRallyPoint rp;
    rp.ptPoint = ptUnderSuspection;
    logic.GenerateTurnNLookOrders(rp, CPlot3D(0, 0, 0));
    logic.StartTurnLook(rp);
    logic.GetUnitServer()->acknowledgements.SayAck(AI_ACK_TO_SUSPECTION);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMSuspection::FinishUsing(CAIUnitLogic &logic)
{
    logic.GetUnitServer()->SetAggression(false);
    logic.GetUnitServer()->acknowledgements.SayAck(AI_ACK_FROM_SUSPECTION);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMSuspection::Load(CSave &file)
{
    file.Read(&ptUnderSuspection, sizeof(CPlot3D));
    file.Read(nPower);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMSuspection::Store(CSave &file)
{
    file.Write(&ptUnderSuspection, sizeof(CPlot3D));
    file.Write(nPower);
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation Aggression
/////////////////////////////////////////////////////////////////////////////////////
int CUMAggression::GetPower(CAIUnitLogic &logic)
{
    // CMapObjectPtr pOldTarget = pTarget;
    // pick up target
    tempEnemy.Clear();
    if (bRevenge)
    {
        if (!pTarget.IsEmpty() && pTarget->IsDead())
        {
            pTarget = 0;
            return AI_NO_MOTIVATION_POWER;
        }
        if (pTarget.IsEmpty())
        {
            CMapObject *pFoe = logic.GetLastHitBy();
            if (pFoe && logic.WasHitByAlly())
                pFoe = 0;
            if (pFoe && pFoe != logic.GetUnit() && !logic.IsIgnored(pFoe))
                pTarget = pFoe;
        }
        if (pTarget.IsEmpty())
            return AI_NO_MOTIVATION_POWER;
        if (pTarget->IsDead() || pTarget->WhoAmI() != HPC_UNIT)
        {
            pTarget = 0;
            return AI_NO_MOTIVATION_POWER;
        }
        if (!logic.environment.IsUnitVisible(logic.GetUnit(), pTarget))
            return AI_NO_MOTIVATION_POWER;
        tempEnemy = CAISpellCaster::CEnemy((CUnit *)pTarget);
        // if target is not in range add it to ignored list with some timing
        /*if ( !logic.IsInAttackRange( pTarget->GetCP(), this ) )
        {
                logic.AddIgnoredTarget( pTarget, random.Get( 20, 50 ) );
                pTarget = 0;
                return AI_NO_MOTIVATION_POWER;
        }*/
    }
    else
    {
        if (!logic.bEnemyVisible)
            return AI_NO_MOTIVATION_POWER;
        // get closest target, not in ignored list
        static CUnitList targets;
        CUnit *pSelf = logic.GetUnit();
        logic.environment.GetTarget(pSelf, targets);
        logic.caster.FindBestTarget(logic, tempEnemy, targets, pSelf->GetCP(), false);
        pTarget = tempEnemy;
        // if ( IsEngaging( enemy ) )
        // return 10000;
        // if target is not in range add it to ignored list with some timing
        /*if ( !pTarget.IsEmpty() && !logic.IsInAttackRange( pTarget->GetCP(), this ) )
        {
                logic.AddIgnoredTarget( pTarget, 30 );
                pTarget = pOldTarget;
        }*/
        /*if ( pTarget.IsEmpty() )
                pTarget = pOldTarget;*/
    }
    // any target was found ?
    if (tempEnemy.IsEmpty())
        return AI_NO_MOTIVATION_POWER;
    // aint target dead ?
    //
    return 10000;
}
/////////////////////////////////////////////////////////////////////////////////////
// здесь предполагается, что перед этой функцией всегда вызывается GetPower()
void CUMAggression::CommandUnit(CAIUnitLogic &logic)
{
    // ack
    if (random.Get(0, 1000) == 1)
        logic.GetUnitServer()->acknowledgements.SayAck(AI_ACK_DURING_ATTACK);
    //
    // ASSERT( ((CUnit*)pTarget) == tempEnemy );
    // pTarget = tempEnemy;
    /*	if ( logic.IsIdling() )
            {
                    // if failed to attack unit ignore this target for awhile
                    if ( logic.GetUnitServer()->IsCmdFailed() && !pTarget.IsEmpty() )
                    {
                            logic.AddIgnoredTarget( pTarget, 5 );
                            pTarget = 0;
                    }
                    else
                    {
                            if ( !pTarget.IsEmpty() && pTarget->IsDead() )
                                    pTarget = 0;
                            else
                            {
                                    ASSERT( !pTarget.IsEmpty() );
                                    if ( pTarget.IsEmpty() )
                                            return;
                    //logic.caster.AttackEnemy( logic, tempEnemy );
                            //}
                    //}
            }*/
    if (!tempEnemy.IsEmpty()) //! pTarget.IsEmpty() )
    {
        logic.CallForHelp(((CUnit *)tempEnemy)->GetCP()); // should not make this every frame
        if (!logic.caster.IsEngaging(logic, tempEnemy))
            logic.caster.AttackEnemy(logic, tempEnemy);
    }
    /* // some not neccessary code crafted to avoid frequent target switching
            if ( --nRelaxation < 0 && !logic.GetUnit()->IsAttacking() )
            {
                    pTarg = logic.environment.GetTarget( logic.GetUnit() );
                    if ( pTarg != pTarget )
                    {
                            pTarget = pTarg;
                            logic.AttackEnemy( pTarget );
                            nRelaxation = nCurrentRelaxStart;
                            nCurrentRelaxStart *= 2;
                    }
            }*/
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMAggression::StartUsing(CAIUnitLogic &logic)
{
    logic.GetUnitServer()->SetAggression(true);
    // ASSERT( !pTarget.IsEmpty() );
    logic.StartIdle();
    logic.GetUnitServer()->acknowledgements.SayAck(AI_ACK_TO_AGGRESSION);
    // nRelaxation = nCurrentRelaxStart;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMAggression::FinishUsing(CAIUnitLogic &logic)
{
    if (pTarget == (CMapObject *)nullptr || pTarget->IsDead())
        logic.environment.ClearPoI();
    pTarget = 0;
    logic.GetUnitServer()->SetAggression(false);
    logic.GetUnitServer()->acknowledgements.SayAck(AI_ACK_FROM_AGGRESSION);
    // nCurrentRelaxStart = 5;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMAggression::Load(CSave &file)
{
    pTarget.Load(file, theWorldServer);
    file.Read(&bRevenge, sizeof(bRevenge));
    // file.Read( nRelaxation );
    // file.Read( nCurrentRelaxStart );
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMAggression::Store(CSave &file)
{
    pTarget.Store(file);
    file.Write(&bRevenge, sizeof(bRevenge));
    // file.Write( nRelaxation );
    // file.Write( nCurrentRelaxStart );
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation Corpse Watcher - wants to explore ally corpses
/////////////////////////////////////////////////////////////////////////////////////
int CUMCorpseWatcher::GetPower(CAIUnitLogic &logic)
{
    if (logic.environment.freshCorpses.GetCount())
    {
        logic.environment.AddPoI(logic.environment.freshCorpses.RemoveHead()->GetCP(),
                                 AI_PPOWER_SEECORPSE, AI_PPDEC_SEECORPSE);
    }
    return AI_NO_MOTIVATION_POWER;
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation is fear
/////////////////////////////////////////////////////////////////////////////////////
CUMFear::CUMFear(bool _bFearPlayer)
{
    fpFearRadius = FP_SAFE_DISTANCE;
    fpRandomShift = 3;
    ptDangerousPlace.x = -1313; // это значение означает, что юнит не находится в режиме страха
    ptPreparedRunaway.z = 0;    // нет подготовленного места отступления
                                // попасть в опасную область
    bFearPlayer = _bFearPlayer;
}
/////////////////////////////////////////////////////////////////////////////////////
// in general case fear has less priority then suspection or aggression
// this makes units more brave
int CUMFear::GetPower(CAIUnitLogic &logic)
{
    CUnitServer *pUS = logic.GetUnitServer();
    CUnit *pThis = pUS->GetUnit();
    // return random shift to normal value
    fpRandomShift = (fpRandomShift * 2 + 3) / 3;
    // if unit was attacked by some one - go away from this persons place, on some range
    //  if person insists - increase range
    if (logic.GetLastHitBy())
    {
        const CPlot3D &ptInterest = logic.GetLastHitBy()->GetCP();
        logic.environment.AddPoI(ptInterest, AI_PPOWER_HITFROM, AI_PPDEC_HITFROM);
        // calc new safe distance
        fpFearRadius = Max(fpFearRadius, fabs(logic.GetUnit()->GetCP() - ptInterest) * 1.5f);
    }
    {
        CUnit *pEnemy = logic.environment.GetNearestEnemy(pThis);
        if (pEnemy)
        {
            // bow shooters run from enemies
            if (pUS->HasRangedAttack() && pThis->GetAIClass() < 6 &&
                pUS->GetUnit()->GetQSpell(0) == 0 &&
                pUS->GetRPGStats()->CanRun(false) == ECR_CAN_RUN &&
                pUS->GetRPGStats()->GetMoveDamageModifier() > 0.6f)
            {
                float fpSafeDist;
                if (logic.GetCurrentMotivationType() == AI_UM_AGGRESSION)
                    fpSafeDist = theAIGeneralSettings.fpRangedRunDist;
                else
                    fpSafeDist = theAIGeneralSettings.fpRangedAttackDist;
                //
                CPlot3D unitPos = pThis->GetCP();
                CPlot3D enemyPos = pEnemy->GetCP();
                // range is to be accounted for
                if (distanceXY2(enemyPos, unitPos) < sqr(fpSafeDist)) // 100 )//25 )
                {
                    if (!logic.GetUnitServer()->HasMove()) //! pUS->IsL1Moving() )
                    {
                        // check if we can retreat
                        CPlot3D majorLook = enemyPos - unitPos;
                        float fpLeng = fabs(majorLook);
                        if (fpLeng != 0)
                            majorLook *= 5 / fpLeng;
                        float fpRandomShift = 3;
                        CPlot3D ptRandomShift(random.GetFloat(-fpRandomShift, fpRandomShift),
                                              random.GetFloat(-fpRandomShift, fpRandomShift), 0);
                        CPlot3D ptPlace = unitPos - majorLook + ptRandomShift;
                        // check is not path too long
                        SAIPath fakePath;
                        float fpDist = fabs(ptPlace - unitPos);
                        float fpMaxPathLength = fpDist * 3 + 10;
                        if (theWorldServer.aiMap.NewPath(pThis, ptPlace, fakePath, fpMaxPathLength,
                                                         3))
                        {
                            if (fakePath.nPathCost <= 5000 * Meter2AITile(fpDist))
                            {
                                // а сможет ли стрелок оттуда стрелять по текущей (она же самая
                                // ближняя) цели
                                if ( //! logic.caster.IsEngaging( logic, pEnemy ) ||
                                    logic.caster.CanAttackEnemyFrom(logic, fakePath.GetFinalPoint(),
                                                                    pEnemy))
                                {
                                    if (fabs2(ptPlace, fakePath.GetFinalPoint()) < 1)
                                    {
                                        ptPreparedRunaway = ptPlace;
                                        ptPreparedRunaway.z = 1;
                                        return 15000;
                                    }
                                }
                            }
                        }
                    }
                    else
                        return 15000;
                }
            }
        }
    }
    if (logic.bEnemyVisible)
    {
        return 3000;
    }
    // if unit is on dangerous area try to find way out - place out of dangerous area should be
    // chosen in 	direction of current motivation target as possible ( if possible )
    if (theWorldServer.aiMap.IsDangerous(pThis->GetCP()) && pThis->CanBeDamagedByAreaSpell())
    {
        ptDangerousPlace = pThis->GetCP();
        return 350;
    }
    if (ptDangerousPlace.x != -1313 && theWorldServer.aiMap.IsDangerous(ptDangerousPlace))
    {
        return 350;
    }
    if (bFearPlayer)
    {
        CPlot3D ptPoI;
        float fpMinDist = 1e30f;
        for (POSITION pos = logic.environment.visibleUnits.GetHeadPosition(); pos;)
        {
            CUnit *pUnit = logic.environment.visibleUnits.GetNext(pos);
            if (pUnit->GetOwner())
            {
                float fpDist = distanceXY2(logic.GetUnit()->GetCP(), pUnit->GetCP());
                if (fpDist < fpMinDist)
                {
                    fpMinDist = fpDist;
                    ptPoI = pUnit->GetCP();
                }
            }
        }
        if (fpMinDist < sqr(fpFearRadius))
            logic.environment.AddPoI(ptPoI, AI_PPOWER_SEEENEMY, AI_PPDEC_SEEENEMY);
    }
    ptDangerousPlace.x = -1313;
    if (logic.environment.GetPoIPower() > 50)
    {
        if (logic.GetCurrentMotivation() == this ||
            fabs2(logic.GetUnit()->GetCP() - cast2D3D(logic.environment.GetPoI())) <
                sqr(fpFearRadius))
            return logic.environment.GetPoIPower() / 6;
    }
    fpFearRadius = (fpFearRadius * 30 + FP_SAFE_DISTANCE) / 31;
    return AI_NO_MOTIVATION_POWER;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMFear::CommandUnit(CAIUnitLogic &logic)
{
    // if idle and range is lower then required try to find path away with increasing random search
    // radius if range is ok emulate sentry and try to have fun
    float fpLeng;
    SAIRallyPoint rp;
    CPlot3D majorLook;
    const CPlot3D &unitPos = logic.GetUnit()->GetCP();
    CPlot3D poi = cast2D3D(logic.environment.GetPoI());
    CUnit *pEnemy;

    if (logic.bEnemyVisible)
    {
        pEnemy = logic.environment.GetNearestEnemy(logic.GetUnit());
        if (pEnemy)
        {
            poi = pEnemy->GetCP();
            logic.CallForHelp(pEnemy->GetCP());
        }
    }
    else if (theWorldServer.aiMap.IsDangerous(unitPos))
    {
        // CRAP - need more advanced get away point finder to get from trouble with minimal damage
        poi = unitPos;
    }
    else if (ptDangerousPlace.x != -1313 && theWorldServer.aiMap.IsDangerous(ptDangerousPlace))
    {
        poi = ptDangerousPlace;
    }
    else if (bFearPlayer && logic.environment.GetPoIPower() > 500)
        ;
    else
        logic.environment.ClearPoI();
    if (fabs2(unitPos - poi) < sqr(fpFearRadius))
    {
        // walk away
        if (!logic.GetUnitServer()->HasMove())
        {
            CPlot3D ptPlace;
            if (ptPreparedRunaway.z != 0)
            {
                ptPlace = ptPreparedRunaway;
                ptPreparedRunaway.z = 0;
            }
            else
            {
                majorLook = poi - unitPos;
                fpLeng = fabs(majorLook);
                fpRandomShift = Min(fpFearRadius * 2.5f, fpRandomShift);
                if (fpLeng != 0)
                    majorLook *= 5 / fpLeng;
                CPlot3D ptRandomShift(random.GetFloat(-fpRandomShift, fpRandomShift),
                                      random.GetFloat(-fpRandomShift, fpRandomShift), 0);
                ptPlace = unitPos - majorLook + ptRandomShift;
            }
            // check is not path too long
            SAIPath fakePath;
            float fpMaxPathLength = fabs(ptPlace - unitPos) * 3 + 10;
            if (theWorldServer.aiMap.NewPath(logic.GetUnit(), ptPlace, fakePath, fpMaxPathLength,
                                             3))
                logic.StartMove(ptPlace);
            fpRandomShift *= 2;
        }
    }
    if (logic.IsIdling())
        logic.caster.CastHeals(logic, true);
    if (logic.IsIdling())
    {
        rp.ptPoint = logic.GetUnit()->GetCP();
        majorLook = poi - unitPos;
        fpLeng = fabs(majorLook);
        if (fpLeng != 0)
            majorLook /= fpLeng * 2;
        logic.GenerateTurnNLookOrders(rp, majorLook);
        logic.StartTurnLook(rp);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMFear::StartUsing(CAIUnitLogic &logic)
{
    // set aggression to unitserver if logic has aggressive motivation (this means unit can run)
    // if ( logic.FindMotivation( AI_UM_AGGRESSION ) )
    logic.GetUnitServer()->SetAggression(true);
    logic.StartIdle();
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMFear::FinishUsing(CAIUnitLogic &logic)
{
    logic.GetUnitServer()->SetAggression(false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMFear::Load(CSave &file)
{
    file.Read(fpFearRadius);
    file.Read(fpRandomShift);
    file.Read(&ptDangerousPlace, sizeof(ptDangerousPlace));
    if (file.GetVersion() < 83)
        return;
    file.Read(bFearPlayer);
    if (file.GetVersion() < 112)
        return;
    file.Read(&ptPreparedRunaway, sizeof(ptPreparedRunaway));
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMFear::Store(CSave &file)
{
    file.Write(fpFearRadius);
    file.Write(fpRandomShift);
    file.Write(&ptDangerousPlace, sizeof(ptDangerousPlace));
    file.Write(bFearPlayer);
    file.Write(&ptPreparedRunaway, sizeof(ptPreparedRunaway));
}
/////////////////////////////////////////////////////////////////////////////////////
// unit motivation is absent : under players control
/////////////////////////////////////////////////////////////////////////////////////
CUMPlayer::CUMPlayer()
{
    ptSwarmTarget = CPlot3D(0, 0, 0);
    cmdState = PC_IDLE;
    bRun = false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::RemoveReferencesOn(CMapObject *_pTarget)
{
    if (pTarget == _pTarget)
        pTarget = NULL;
    if (pFollowTo == _pTarget)
        pFollowTo = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUMPlayer::DoPathExist(CAIUnitLogic &logic, const CPlot3D &ptSrc, const CPlot3D &ptDst)
{
    if (distanceXY2(ptSrc, ptDst) < 0.01f)
        return false;
    SAIPath fakePath;
    float fpMaxPathLength = fabs(ptSrc, ptDst) * 2;
    CUnit *pThis = logic.GetUnit();
    CPlot3D ptStore = pThis->GetCP();
    theWorldServer.aiMap.MoveObject(pThis, ptSrc); // pThis->SetCP( ptSrc );
    bool bRes = theWorldServer.aiMap.NewPath(logic.GetUnit(), ptDst, fakePath, fpMaxPathLength, 0);
    theWorldServer.aiMap.MoveObject(pThis, ptStore); // pThis->SetCP( ptStore );
    return bRes;
}
/////////////////////////////////////////////////////////////////////////////////////
// cmds
void CUMPlayer::DoMoveTo(CAIUnitLogic &logic, const CPlot3D &ptTarget, bool bGroup, bool _bRun,
                         const CPlot3D &ptOffset)
{
    cmdState = PC_MOVE;
    ptSwarmTarget = ptTarget;
    if (DoPathExist(logic, ptTarget, ptTarget + ptOffset))
        ptSwarmTarget += ptOffset;
    bRun = _bRun;
    if (!IsCombat(logic))
        pFollowTo = 0;
    pTarget = 0;
    // StartMoving( logic, bGroup ); // Zarus requested waiting of followers only in swarm mode
    logic.StartMove(ptSwarmTarget, bRun, bGroup);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::DoSwarm(CAIUnitLogic &logic, const CPlot3D &ptTarget, bool bGroup, bool _bRun,
                        const CPlot3D &ptOffset)
{
    cmdState = PC_SWARM;
    ptSwarmTarget = ptTarget;
    if (DoPathExist(logic, ptTarget, ptTarget + ptOffset))
        ptSwarmTarget += ptOffset;
    bRun = _bRun;
    if (!IsCombat(logic))
        pFollowTo = 0;
    pTarget = 0;
    StartMoving(logic, bGroup);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::DoAttack(CAIUnitLogic &logic, CMapObject *_pTarget, ERPGHitLocations hl, bool _bRun)
{
    if (_pTarget == logic.GetUnit())
        return;
    cmdState = PC_IDLE;
    pTarget = _pTarget;
    logic.StartPlayerAttack(_pTarget, hl, _bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::DoSkill(CAIUnitLogic &logic, CMapObject *pTarget, int nSkill, int nItemID,
                        bool _bRun)
{
    cmdState = PC_IDLE;
    logic.StartUsingSkill(pTarget, nSkill, nItemID, _bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::DoCast(CAIUnitLogic &logic, int nSpell, CMapObject *pTarget, bool _bRun)
{
    cmdState = PC_IDLE;
    logic.StartCast(nSpell, pTarget, bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::DoCast(CAIUnitLogic &logic, int nSpell, const CPlot3D &ptTarget, bool _bRun)
{
    cmdState = PC_IDLE;
    logic.StartCast(nSpell, ptTarget, bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::DoFollow(CAIUnitLogic &logic, CUnit *pLeader)
{
    if (logic.GetUnit() == pLeader)
    {
        pFollowTo = 0;
        return;
    }
    // break circular follow
    if (pLeader)
    {
        CUnit *pFather = pLeader;
        for (;;)
        {
            pFather = ((CUnitServer *)pFather->GetUnitSpecific())->logic.GetPlayerLeaderExact();
            if (!pFather)
                break;
            if (pFather == logic.GetUnit())
            {
                CAIUnitLogic &bl = ((CUnitServer *)pFather->GetUnitSpecific())->logic;
                CUMPlayer *pBreak = bl.GetPlayer();
                if (pBreak)
                    pBreak->DoFollow(bl, 0);
                break;
            }
        }
    }
    if (logic.IsMoving())
        DoStop(logic);
    pFollowTo = pLeader;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::DoStop(CAIUnitLogic &logic)
{
    cmdState = PC_IDLE;
    logic.StartIdle();
}
/////////////////////////////////////////////////////////////////////////////////////
// добавить в список потенциальных целей юниты, которых наши атакуют в данный момент
void CUMPlayer::AddAttackedTargets(CAIUnitLogic &logic, CUnitList &targets)
{
    CPlot3D ptPoI;
    CUnitList list;
    CUnit *pThis = logic.GetUnit(), *pTarget;

    theWorldServer.aiMap.GetUnitsNear(pThis->GetCP(), list,
                                      theAIGeneralSettings.fpPlayerCallForHelp);
    for (POSITION pos = list.GetHeadPosition(); pos;)
    {
        CUnit *pUnit = list.GetNext(pos);
        if (pUnit == pThis)
            continue;
        if (pUnit->GetOwner() != pThis->GetOwner())
            continue;
        CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
        CAIUnitLogic &lgc = pUS->logic;
        pTarget = (CUnit *)lgc.GetCurrentTarget();
        if (!pTarget || pTarget->GetOwner() == pThis->GetOwner())
            continue;
        if (lgc.IsAttacking() && pTarget->WhoAmI() == HPC_UNIT)
        {
            if (targets.Find(pTarget) == 0)
                targets.AddTail(pTarget);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUMPlayer::IsVisibleTarget(CAIUnitLogic &logic, CUnit *pUnit)
{
    if (logic.environment.visibleUnits.Find(pUnit) != 0)
        return true;
    CPlayer *pPlayer = logic.GetUnit()->GetOwner();
    if (pPlayer)
    {
        if (((CPlayerServer *)pPlayer->GetPlayerSpecific())->IsUnitVisibleToParty(pTarget))
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// seek for targets if they exist attack `em
void CUMPlayer::SeekTargets(CAIUnitLogic &logic)
{
    CUnit *pThis = logic.GetUnit();
    CUnitServer *pUS = logic.GetUnitServer();
    if (pThis->GetAggression() == PAS_AGGRESSIVE)
    {
        if (!pTarget.IsEmpty() && !pTarget->IsDead())
        {
            // continue attacking previous target if possible
            CAISpellCaster::CEnemy enemy(pTarget);
            if (logic.caster.CanAttackEnemy(logic, enemy, true))
            {
                logic.StartAttack(pTarget);
                return;
            }
        }
        CUnitList targets;
        CAISpellCaster::CEnemy enemy;
        float fpSwarmRadius;
        if (cmdState == PC_SWARM)
            fpSwarmRadius = theAIGeneralSettings.fpSwarmRadius;
        else
        {
            ptSwarmTarget = CPlot3D(0, 0, 0);
            fpSwarmRadius = -1;
        }
        logic.environment.GetAggressiveTarget(pThis, targets, ptSwarmTarget, fpSwarmRadius);
        //
        // remove targets that can not attack us
        for (POSITION pos = targets.GetHeadPosition(); pos;)
        {
            POSITION delPos = pos;
            CUnit *pUnit = targets.GetNext(pos);
            CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
            float fpAttackDist = Max(pUS->GetVisionSense(), pUS->GetLifeSense()) * 1.10f;
            if (distanceXY2(pUnit->GetCP(), pThis->GetCP()) > sqr(fpAttackDist))
                targets.RemoveAt(delPos);
        }
        // наши друзья кому-то чистят морду - стоит пойти подсобить, если не слишком далеко
        AddAttackedTargets(logic, targets);
        //
        pThis->SetFlag(UNIT_DONTMOVE);
        logic.caster.FindBestTarget(logic, enemy, targets, pThis->GetCP(), false);
        pThis->ResetFlag(UNIT_DONTMOVE);
        CUnit *pEnemy = enemy;
        if (pEnemy)
        {
            // switching to combat mode - stop movement
            if (!IsCombat(logic))
            {
                // guys from this group should stand
                CUnitList group;
                GetGroup(logic, group);
                for (POSITION pos = group.GetHeadPosition(); pos;)
                {
                    CUnit *pUnit = group.GetNext(pos);
                    if (pUnit == pThis)
                        continue;
                    CAIUnitLogic &lgc = ((CUnitServer *)pUnit->GetUnitSpecific())->logic;
                    CUMPlayer *pUP = lgc.GetPlayer();
                    if (pUP && !lgc.IsAttacking() && !lgc.IsCasting())
                    {
                        pUP->DoStop(lgc);
                    }
                }
            }
            if (!logic.caster.IsEngaging(logic, enemy))
                logic.StartAttack(pEnemy);
            pTarget = 0; // automatic attack mode
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// everyone who follow us or follow same person as we (maybe transitive) is in one group
// with us
void CUMPlayer::GetGroup(CAIUnitLogic &logic, CUnitList &group)
{
    group.RemoveAll();
    CUnit *pThis = logic.GetUnit();
    if (!pThis)
        return;
    group.AddTail(pThis);
    CPlayer *pPlayer = pThis->GetOwner();
    if (pPlayer)
    {
        CUnit *pFather = pThis;
        for (;;)
        {
            pFather = ((CUnitServer *)pFather->GetUnitSpecific())->logic.GetPlayerLeader();
            if (group.Find(pFather) != NULL)
                break;
            if (pFather)
                group.AddTail(pFather);
            else
                break;
        }
        bool bContinue = true;
        while (bContinue)
        {
            bContinue = false;
            CUnitPtrHashIterator unitIter;
            for (CUnit *pAlly = unitIter.GetFirst(&pPlayer->unitsUnderControl); pAlly;
                 pAlly = unitIter.GetNext())
            {
                CUnit *pLead = ((CUnitServer *)pAlly->GetUnitSpecific())->logic.GetPlayerLeader();
                if (group.Find(pLead) != 0 && group.Find(pAlly) == 0)
                {
                    bContinue = true;
                    group.AddTail(pAlly);
                }
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CUMPlayer::IsCombat(CAIUnitLogic &logic)
{
    CPlayer *pPlayer = logic.GetUnit()->GetOwner();
    if (pPlayer)
        return ((CPlayerServer *)pPlayer->GetPlayerSpecific())->dangerState == WDS_COMBAT;
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// followers are searched among units belonging same player
bool CUMPlayer::AreFollowersInRange(CAIUnitLogic &logic, bool bWider)
{
    CUnit *pThis = logic.GetUnit();
    CPlayer *pPlayer = pThis->GetOwner();
    float fpKoef = bWider ? 2 : 1.4f;
    if (pPlayer)
    {
        CUnitPtrHashIterator unitIter;
        for (CUnit *pAlly = unitIter.GetFirst(&pPlayer->unitsUnderControl); pAlly;
             pAlly = unitIter.GetNext())
        {
            CUMPlayer *pUP = ((CUnitServer *)pAlly->GetUnitSpecific())->logic.GetPlayer();
            if (((CUnit *)pUP->pFollowTo) == pThis)
            {
                if (fabs2(pAlly->GetCP(), pThis->GetCP()) >
                    sqr(theAIGeneralSettings.fpFollowMaxDist * fpKoef + 1))
                    return false;
            }
        }
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::StartMoving(CAIUnitLogic &logic, bool bGroup)
{
    if (!IsCombat(logic) && !AreFollowersInRange(logic, true))
    {
        ptSwarmTarget.z = 0;
        logic.ShowMove(ptSwarmTarget, bGroup);
    }
    else
    {
        ptSwarmTarget.z = 1;
        logic.StartMove(ptSwarmTarget, bRun, bGroup);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::CommandUnit(CAIUnitLogic &logic)
{
    bool bIsCombatMode = IsCombat(logic);

    // complain about attack on defensive
    if (logic.GetLastHitBy() != 0 && logic.GetUnit()->GetAggression() != PAS_AGGRESSIVE)
        logic.GetUnitServer()->InformFailure(ACK_HERO_NOTIFY_ATTACKED_IN_DEFENCE);
    // process move/swarm or idle
    switch (cmdState)
    {
    case PC_IDLE:
        if (pFollowTo.IsEmpty() && logic.IsFollowing())
            logic.StartIdle();
        //
        if (logic.IsAttacking() && pTarget == (CMapObject *)nullptr)
        {
            // maybe target switch is to be done?
            SeekTargets(logic);
        }
        else
        {
            // if idling start follow
            if (logic.IsIdling() || (!pFollowTo.IsEmpty() && logic.IsFollowing()))
            {
                if (!pFollowTo.IsEmpty())
                {
                    if (!bIsCombatMode)
                        logic.StartFollow(pFollowTo);
                    // account if we follow moving person then we also move (ignore attacks)
                    CUnit *pLeader = (CUnit *)pFollowTo;
                    if (pLeader)
                    {
                        CUMPlayer *pUP = ((CUnitServer *)pLeader->GetUnitSpecific())
                                             ->logic.GetPlayer();
                        if (pUP && pUP->cmdState == PC_MOVE)
                            break;
                    }
                }
                // look for suitable targets
                SeekTargets(logic);
            }
        }
        break;
    case PC_MOVE:
        if (logic.IsIdling())
            DoStop(logic); // move is over
        break;
    case PC_SWARM:
        if (ptSwarmTarget.z == 1)
        {
            // move was in progress
            if (logic.IsIdling())
                DoStop(logic); // move is over
            else if (!bIsCombatMode && !AreFollowersInRange(logic, true))
            {
                logic.GetUnitServer()->InformFailure(ACK_HERO_NOTIFY_WAITING_FOLLOWERS);
                // wait for followers
                ptSwarmTarget.z = 0;
                logic.StartIdle();
            }
        }
        else
        {
            // was waiting for followers
            if (bIsCombatMode || AreFollowersInRange(logic, false))
            {
                ptSwarmTarget.z = 1;
                CPlot3D ptDest = ptSwarmTarget;
                ptDest.z = -1313; // do not show path
                logic.StartMove(ptDest);
            }
        }
        if (cmdState == PC_SWARM)
        {
            SeekTargets(logic);
        }
        break;
    default:
        DoStop(logic);
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::Load(CSave &file)
{
    pTarget.Load(file, theWorldServer);
    pFollowTo.Load(file, theWorldServer);
    file.Read(&ptSwarmTarget, sizeof(ptSwarmTarget));
    file.Read(&cmdState, sizeof(cmdState));
    if (file.GetVersion() >= 70 && file.GetVersion() < 94)
    {
        CScriptObjectGroup swarmTargets;
        swarmTargets.Load(file);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CUMPlayer::Store(CSave &file)
{
    pTarget.Store(file);
    pFollowTo.Store(file);
    file.Write(&ptSwarmTarget, sizeof(ptSwarmTarget));
    file.Write(&cmdState, sizeof(cmdState));
}
/////////////////////////////////////////////////////////////////////////////////////
// CAIUnitLogic implementation
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::ClearMotivations()
{
    motivations.RemoveAll();
    pControl = 0;
    InitRadiuses();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::ClearRefs()
{
    ClearMotivations();
    state = US_IDLE;
    environment.Clear();
    currentRally.Clear();
    pTarget = NULL;
    pHitBy = NULL;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::Clear()
{
    ClearRefs();
    pUnitServer = NULL;
    nAlarmInvoked = 0;
    nAlarmNumber = 0;
    nAlarmDelay = 1;
    nCurrentAlarm = 0;
    nLookFinishTime = -10000;
    bEnemyVisible = FALSE;
    alarmLogics.RemoveAll();
    fpCallForHelpRadius = AI_DEFAULT_CALL_FOR_HELP_RADIUS;
    alarmCondition = AC_SEE_ENEMY;
    ptPreviousPoint = ptCurrentPoint = CPlot3D(0, 0, 0);
    bAlwaysOnTheRun = false;
    nGuardPntFail = 0;
    nFollowRest = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::Set(CUnitServer *pUS)
{
    if (pUS == pUnitServer)
        return;
    pUnitServer = pUS;
    ptPreviousPoint = ptCurrentPoint = CPlot3D(-1313, 0, 0);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIUnitLogic::IsInArea(const CPlot3D &ptPlace, float fpRadius)
{
    float fpR2 = sqr(fpRadius);
    const CPlot3D &curP = ptCurrentPoint, &prevP = ptPreviousPoint;
    if (sqr(curP.x - ptPlace.x) + sqr(curP.y - ptPlace.y) < fpR2)
        return true;
    if (curP.x == prevP.x && curP.y == prevP.y)
        return false;
    float fpX = ptPlace.x, fpY = ptPlace.y;
    float fpDirX = prevP.x - curP.x;
    float fpDirY = prevP.y - curP.y;
    float fpDot = (fpX - curP.x) * fpDirX + (fpY - curP.y) * fpDirY;
    float fpDist2 = sqr(fpDirX) + sqr(fpDirY);
    fpDot /= fpDist2;
    fpDot = Max(0.0f, fpDot);
    fpDot = Min(fpDot, 1.0f);
    fpX -= fpDirX * fpDot;
    fpY -= fpDirY * fpDot;
    return sqr(curP.x - fpX) + sqr(curP.y - fpY) < fpR2;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIUnitLogic::IsInHealCastRange(const CPlot3D &ptPlace)
{
    return IsInArea(ptPlace, FP_HEALCAST_RADIUS);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIUnitLogic::IsInSuspectionRange(const CPlot3D &ptPlace, CAIUnitMotivation *pWhoAsk)
{
    return true;
    /*
            float fpRadius = fpSuspectRadius * fpRadiusKoef;
            if ( pControl == pWhoAsk )
                    fpRadius += FP_GETAWAY_ACTION_BONUS_RADIUS;
            return IsInArea( ptPlace, fpRadius );*/
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIUnitLogic::IsInAttackRange(const CPlot3D &ptPlace, CAIUnitMotivation *pWhoAsk)
{
    return true;
    /*
            float fpRadius = fpAttackRadius * fpRadiusKoef;
            if ( pControl == pWhoAsk )
                    fpRadius += FP_GETAWAY_ACTION_BONUS_RADIUS;
            return IsInArea( ptPlace, fpRadius );*/
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::SetNewRandomRadiusKoef()
{
    fpRadiusKoef = random.GetFloat(0.8f, 1.2f);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::SetNewAreaPoint(const CPlot3D &ptCenter, float fpSuspect, float fpAttack)
{
    ptPreviousPoint = ptCurrentPoint;
    ptCurrentPoint = ptCenter;
    fpSuspectRadius = fpSuspect;
    fpAttackRadius = fpAttack;
    if (ptPreviousPoint.x == -1313)
        ptPreviousPoint = ptCurrentPoint;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::InitRadiuses()
{
    fpSuspectRadius = 100000; // FP_GETAWAY_TO_CHECK_RADIUS;
    fpAttackRadius = 100000;  // FP_GETAWAY_TO_KILL_RADIUS;
    fpRadiusKoef = 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::RecalcEnemyVisible()
{
    CMapObjectPtrList &units = *environment.GetVisibleList();
    POSITION pos = units.GetHeadPosition();
    CUnit *pThis = GetUnit();
    bEnemyVisible = FALSE;
    while (pos)
    {
        CUnit *pUnit = units.GetNext(pos);
        if (pThis->IsEnemy(pUnit) && !pUnit->IsDead())
        {
            bEnemyVisible = TRUE;
            return;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::InvokeAlarm()
{
    if (nAlarmInvoked)
        return;
    nAlarmInvoked = 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::Segment()
{
    int nBestPower, nPower;
    POSITION pos;
    CAIUnitMotivation *pBest;

    // delayed alarm
    if (nAlarmInvoked && nAlarmDelay > 0)
    {
        nAlarmDelay--;
        if (nAlarmDelay == 0 && nAlarmNumber)
        {
            theWorldServer.InvokeAlarm(nAlarmNumber, GetUnit()->GetCP());
            GetUnitServer()->acknowledgements.SayAck(AI_ACK_SELF_ALARM);
        }
    }

    // коэффициент радиуса действия стремится к единице со временем
    fpRadiusKoef = (fpRadiusKoef * 30 + 1) / 31;

    // update environment
    environment.Segment(pUnitServer);

    RecalcEnemyVisible();

    // do not interrupt attacks
    if (pUnitServer && pUnitServer->IsL1Attacking())
        return;

    if (bEnemyVisible && !nAlarmInvoked &&
        (alarmCondition == AC_SEE_ENEMY || alarmCondition == AC_SUSPECTION))
        InvokeAlarm();

    nBestPower = 1; // такую силу имеет желание постоять отдохнуть ( startidle )
    pBest = 0;
    // определим силу с которой предыдущий motivation желает остаться главным
    if (pControl)
    {
        nPower = pControl->GetPower(*this) + N_CURRENT_MOTIVATION_BONUS;
        if (nPower > nBestPower)
        {
            pBest = pControl;
            nBestPower = nPower;
        }
    }
    // переберем motivations и проверим нет ли каких стоящих желаний
    for (pos = motivations.GetHeadPosition(); pos;)
    {
        CAIUnitMotivation *pMotiv = motivations.GetNext(pos);
        if (pMotiv == pControl)
            continue;
        int nPower = pMotiv->GetPower(*this);
        if (nPower > nBestPower)
        {
            nBestPower = nPower;
            pBest = pMotiv;
        }
    }
    // don`t we get a new controlling motivation ?
    if (pBest != pControl)
    {
        if (pBest)
        {
            CString szTemp, szMotive;
            switch (pBest->GetType())
            {
            case AI_UM_GUARD:
                szMotive = "Guard";
                break;
            case AI_UM_PATROL:
                szMotive = "Patrol";
                break;
            case AI_UM_SENTRY:
                szMotive = "Sentry";
                break;
            case AI_UM_SUSPECTION:
                szMotive = "Suspection";
                break;
            case AI_UM_AGGRESSION:
                szMotive = "Aggression";
                break;
            case AI_UM_CORPSEWATCHER:
                szMotive = "CorpseWatcher";
                break;
            case AI_UM_FEAR:
                szMotive = "Fear";
                break;
            case AI_UM_PLAYER:
                szMotive = "Player";
                break;
            case AI_UM_TAMED:
                szMotive = "Tamed";
                break;
            case AI_UM_FOLLOW:
                szMotive = "Follow";
                break;
            }
            szTemp.Format("Unit %i new motivation %s with power of %i",
                          pUnitServer->GetUnit()->GetID(), szMotive, nBestPower);
            aiLog << szTemp << endl;
            // не пора ли поднять аларм?
            switch (pBest->GetType())
            {
            case AI_UM_SUSPECTION:
            case AI_UM_CORPSEWATCHER:
            case AI_UM_AGGRESSION:
            case AI_UM_FEAR:
                if (alarmCondition == AC_SUSPECTION)
                    InvokeAlarm();
                break;
            }
        }

        if (pControl)
            pControl->FinishUsing(*this);
        if (pBest)
            pBest->StartUsing(*this);
        pControl = pBest;
    }
    // now motivation issues commands ( if one exist, else start idling )
    if (pControl)
        pControl->CommandUnit(*this);
    else
        StartIdle();
    // reset information about last unit who inflicted damage to this unit
    pHitBy = NULL;
    environment.FreshCorpsesAreExplored();

    // process low level logic
    switch (state)
    {
    case US_IDLE:
        pTarget = NULL;
        pUnitServer->CmdWait();
        break;
    case US_CAST:
        if (!pUnitServer->HasCast())
        {
            if (!pTarget.IsEmpty() && pUnitServer->IsCmdFailed())
                environment.AddIgnoredTarget(pTarget, 15);
            pTarget = NULL;
            state = US_IDLE;
        }
        break;
    case US_SKILL:
        pTarget = NULL;
        if (!pUnitServer->HasSkill())
            state = US_IDLE;
        break;
    case US_ATTACK:
        ExecAttack();
        break;
    case US_MOVE:
        pTarget = NULL;
        if (!pUnitServer->HasMove() && !pUnitServer->HasRotate())
            state = US_IDLE;
        break;
    case US_FOLLOW:
        ExecFollow();
        break;
    case US_TURNLOOK:
        pTarget = NULL;
        ExecTurnNLook();
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::RemoveReferencesOn(CMapObject *_pTarget)
{
    POSITION pos = motivations.GetHeadPosition();
    // переберем motivations и проверим нет ли каких стоящих желаний
    while (pos)
        motivations.GetNext(pos)->RemoveReferencesOn(_pTarget);
    if (pTarget == _pTarget)
        pTarget = NULL;
    if (pHitBy == _pTarget)
        pHitBy = NULL;
    environment.RemoveReferencesOn(_pTarget);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::AlarmWasInvoked(int nAlarm)
{
    ASSERT(nAlarm >= 0 && nAlarm < alarmLogics.GetSize());
    if (nAlarm < 0 || nAlarm >= alarmLogics.GetSize())
        return;
    if (alarmLogics[nAlarm].bEnabled || nAlarm == 0)
        SetLogic(alarmLogics[nAlarm], nAlarm);
    GetUnitServer()->acknowledgements.SayAck(AI_ACK_ALARM);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::CallForHelp(const CPlot3D &ptPoint)
{
    CUnitList list;
    POSITION pos;
    CPlot3D ptPoI;
    CUnit *pSelf = GetUnit();
    theWorldServer.aiMap.GetUnitsNear(pSelf->GetCP(), list, fpCallForHelpRadius);
    pos = list.GetHeadPosition();
    while (pos)
    {
        CUnit *pUnit = list.GetNext(pos);
        CUnitServer *pServer;
        if (pUnit == pSelf)
            continue;
        if (theWorldServer.fof.IsPlayerAlly(pSelf->GetPlayerNum(), pUnit->GetPlayerNum()))
        {
            pServer = (CUnitServer *)pUnit->GetUnitSpecific();
            CString szText;
            // some optimization
            if (NConsole::eCurrentFilter & NConsole::AI)
            {
                szText.Format("Unit %i call for help unit %i from %fm distance", pSelf->GetID(),
                              pUnit->GetID(), fabs(pSelf->GetCP() - pUnit->GetCP()));
            }
            pServer->logic.environment.HearSound(ptPoint, AI_HEAR_CALL_FOR_HELP_DELAY, szText);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// utility functions
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::GenerateTurnNLookOrders(SAIRallyPoint &rp, const CPlot3D &majorLook,
                                           float fpStayIdleTime, bool bRest)
{
    int i, k;
    rp.lookOrders.SetSize(0);
    k = random.Get(1, 4);
    CPlot3D ptLookSrc = rp.ptPoint;
    ptLookSrc.z = theWorldServer.aiMap.GetTileSurface(ptLookSrc) + 1.4f;
    if (fpStayIdleTime < 0)
        fpStayIdleTime = 40;
    else
        fpStayIdleTime *= 15;
    // rest в агрессивном состоянии не приемлем
    if (bRest && GetUnitServer()->IsAggressive())
    {
        ASSERT(0);
        bRest = false;
    }
    // rest for humans is disabled
    if (bRest && strncmp(GetUnit()->GetMaskName(), "unhu", 4) == 0)
        bRest = false;
    // не поворачиваться в idle`e
    if (bRest)
    {
        fpStayIdleTime *= k;
        k = 1;
    }
    for (i = 0; i < k; i++)
    {
        // направление в котором юнит будет смотреть не должно содержать препятствий, мешающих
        // обзору
        float fpAngle, fpTime;
        int nSafeCounter = 5;
        CPlot3D look;
        do
        {
            fpAngle = float(random.GetFloat(0, 2 * 3.15f));
            fpTime = float(random.GetFloat(fpStayIdleTime / 4, fpStayIdleTime));
            look.x = float(cos(fpAngle));
            look.y = float(sin(fpAngle));
            look.z = 0;
            look = look + majorLook;
            if (nSafeCounter-- < 0)
                break;
        } while (theWorldServer.aiMap.TraceRelief(ptLookSrc, ptLookSrc + look * 2) != 1);
        fpAngle = float(atan2(look.y, look.x));
        // add one more direction
        rp.lookOrders.Add(SAITurnLookOrder(fpAngle, fpTime, 0, bRest));
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CAIUnitMotivation *CAIUnitLogic::FindMotivation(EAIMotivation motive)
{
    for (POSITION pos = motivations.GetHeadPosition(); pos;)
    {
        CAIUnitMotivation *pMotive = motivations.GetNext(pos);
        if (pMotive->GetType() == motive)
            return pMotive;
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
EAIMotivation CAIUnitLogic::GetCurrentMotivationType()
{
    CAIUnitMotivation *pCur = pControl;
    if (pControl)
        return pControl->GetType();
    return AI_UM_IDLE;
}
/////////////////////////////////////////////////////////////////////////////////////
CUMPatrol *CAIUnitLogic::GetPatrol()
{
    return static_cast<CUMPatrol *>(FindMotivation(AI_UM_PATROL));
}
/////////////////////////////////////////////////////////////////////////////////////
CUMPlayer *CAIUnitLogic::GetPlayer()
{
    return static_cast<CUMPlayer *>(FindMotivation(AI_UM_PLAYER));
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CAIUnitLogic::GetPlayerLeader()
{
    CUnit *pRes = GetPlayerLeaderExact();
    if (!pRes && state == US_FOLLOW)
        pRes = pTarget;
    return pRes;
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CAIUnitLogic::GetPlayerLeaderExact()
{
    CUnit *pRes = 0;
    CUMPlayer *pPlayerUM = (CUMPlayer *)FindMotivation(AI_UM_PLAYER);
    if (pPlayerUM)
        pRes = pPlayerUM->GetLeader();
    return pRes;
}
/////////////////////////////////////////////////////////////////////////////////////
CMapObject *CAIUnitLogic::GetAggressionTarget()
{
    CUMAggression *pAggrUM = (CUMAggression *)FindMotivation(AI_UM_AGGRESSION);
    if (pAggrUM && pControl == (CAIUnitMotivation *)pAggrUM)
        return pAggrUM->GetTarget();
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// если добавляемая motivation уже есть в списке старая версия удаляется, новая добавляется
// если добавляется call of duty motivation, а в списке уже есть call of duty motivation, то
// старая call of duty удаляется
void CAIUnitLogic::AddMotivation(CAIUnitMotivation *pMotive)
{
    POSITION pos = motivations.GetHeadPosition(), posCheck;
    EAIMotivation newMotive = pMotive->GetType();
    bool bDuty = pMotive->IsDuty();
    while (pos)
    {
        posCheck = pos;
        CAIUnitMotivation *pM = motivations.GetNext(pos);
        if (newMotive == pM->GetType() || (bDuty && pM->IsDuty()))
        {
            if ((CAIUnitMotivation *)pControl == pM)
                pControl = 0;
            motivations.RemoveAt(posCheck);
            break;
        }
    }
    motivations.AddTail(pMotive);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::RemoveMotivation(EAIMotivation mot)
{
    for (POSITION pos = motivations.GetHeadPosition(); pos;)
    {
        POSITION prevPos = pos;
        CAIUnitMotivation *pMotive = motivations.GetNext(pos);
        if (pMotive->GetType() == mot)
            motivations.RemoveAt(prevPos);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// make unit behave like standard guard logic
void CAIUnitLogic::SetGuard(const CPlot3D &pos, float fpRadius)
{
    ClearMotivations();
    AddGuard(pos, fpRadius);
    AddSuspection();
    AddAggression();
    AddCorpseWatcher();
    AddFear();
    StartIdle();
}
/////////////////////////////////////////////////////////////////////////////////////
// similar to standard standground logic ( but with aggression )
void CAIUnitLogic::SetSentry(const CPlot3D &pos)
{
    ClearMotivations();
    AddSentry(pos);
    AddSuspection();
    AddAggression();
    AddCorpseWatcher();
    AddFear();
    StartIdle();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::PatchCallForHelpRadius()
{
    if (fpCallForHelpRadius == theAIGeneralSettings.fpCallForHelpRadiusDefault)
        fpCallForHelpRadius = theAIGeneralSettings.fpCallForHelpRadius;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::SetLogic(SGeneralUnitLogic &logic, int nAlarm)
{
    int i, k;
    CUMPatrol *pPatrol;
    ClearMotivations();
    fpCallForHelpRadius = logic.fCallForHelpRadius;
    PatchCallForHelpRadius();
    alarmCondition = logic.eAlarmCondition;
    switch (logic.eLogicModel)
    {
    case UM_IDLE:
        break;
    case UM_PLAYER:
        AddMotivation(dbgnew CUMPlayer);
        break;
    case UM_GUARD:
        AddGuard(logic.ptGuardPoint, logic.fpGuardRadius);
        break;
    case UM_GUARD_ALARM: {
        const SAlarmInfo &info = theWorldServer.GetAlarm(nAlarm);
        AddGuard(CPlot3D(info.fpX, info.fpY, 0), logic.fpGuardRadius);
    }
    break;
    case UM_PATROL:
        AddPatrol();
        pPatrol = GetPatrol();
        pPatrol->ClearPatrolRoute();
        for (i = 0; i < logic.patrolPoints.GetSize(); i++)
        {
            SAIRallyPoint *pPoint;
            pPatrol->AddPatrolPoint(logic.patrolPoints[i].ptPosition);
            if (logic.patrolPoints[i].actions.GetSize())
            {
                pPoint = pPatrol->GetLastPatrolPoint();
                ASSERT(pPoint);
                for (k = 0; k < logic.patrolPoints[i].actions.GetSize(); k++)
                {
                    float fpAngle = 0, fpX, fpY;
                    SActionPoint &ap = logic.patrolPoints[i].actions[k];
                    fpX = ap.ptLook.x - logic.patrolPoints[i].ptPosition.x;
                    fpY = ap.ptLook.y - logic.patrolPoints[i].ptPosition.y;
                    if (fpX != 0 || fpY != 0)
                        fpAngle = (float)atan2(fpY, fpX);
                    pPoint->AddLookOrder(fpAngle, (float)ap.nWaitSeg, ap.fpTurnSpeed,
                                         (ap.nFlags & GUAP_REST) != 0);
                }
            }
        }
        if (!logic.bCyclic)
            pPatrol->MirrorPatrolRoute();
        break;
    case UM_SENTRY:
        AddSentry(logic.ptGuardPoint);
        break;
    }
    if (logic.eLogicModel != UM_IDLE && logic.eLogicModel != UM_PLAYER)
    {
        if (logic.aggressionMode == UAM_AGGRESSIVE || logic.aggressionMode == UAM_REVENGE)
        {
            if (logic.aggressionMode == UAM_AGGRESSIVE)
                AddAggression();
            else
                AddRevenge();
            AddSuspection();
            AddCorpseWatcher();
            AddFear(false);
        }
        if (logic.aggressionMode == UAM_FEAR)
        {
            AddCorpseWatcher();
            AddFear(false);
        }
        if (logic.aggressionMode == UAM_FEARPLAYER)
        {
            AddCorpseWatcher();
            AddFear(true);
        }
    }
    bAlwaysOnTheRun = logic.HasMotivation(GUM_ALWAYS_ACTIVE) != FALSE;

    nCurrentAlarm = nAlarm;
    nAlarmNumber = logic.nAlarmNumber;
    nAlarmInvoked = 0;
    nAlarmDelay = Max(1, logic.nAlarmDelay);
    StartIdle();
    caster.Initialize(GetUnit());
    if (bAlwaysOnTheRun)
        theWorldServer.AddObjectToActiveSet(GetUnit());
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::SetLogics(SGeneralUnitLogic *pLogics, int nNumber)
{
    alarmLogics.SetSize(nNumber);
    for (int i = 0; i < nNumber; i++)
        alarmLogics[i] = pLogics[i];
    SetLogic(alarmLogics[0], 0);
}
/////////////////////////////////////////////////////////////////////////////////////
// low level logics execution
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::ExecFollow()
{
    ASSERT(!pTarget.IsEmpty());
    if (pTarget.IsEmpty() ||
        pTarget->IsDead()) // || ( pTarget->WhoAmI() == HPC_UNIT && !environment.IsUnitVisible(
                           // GetUnit(), (CUnit*)pTarget ) ) )
    {
        pTarget = 0;
        state = US_IDLE;
        return;
    }
    // если недавно была отдана команда идти, то подождать некоторое время
    if (nFollowRest++ < 8)
        return;
    //
    float FP_FOLLOW_DISTANCE_2 = sqr(theAIGeneralSettings.fpFollowMaxDist);
    float FP_MIN_FOLLOW_DISTANCE_2 = sqr(theAIGeneralSettings.fpFollowMinDist);
    float FP_GETAWAY = 1.3f;
    CUnit *pUnit = pUnitServer->GetUnit();
    float fpTargetDist = fabs(pTarget->GetCP() - pUnit->GetCP());
    int nTime = theWorldServer.GetSegmentTimeCounter();
    //
    CUnitServer *pUS = (CUnitServer *)((CUnit *)pTarget)->GetUnitSpecific();
    if (pUS->HasMove())
    {
        // target is moving
        /*-if some one go behind us
           test if he is coming closer (+5 time) and will be closer then min
             test if he will be close enough to us (or to our dest pos) near time
               if we can step out of his way (+40 time) do it else walk to his +40
           else
             if we are far-go, else - stay*/
        CPlot3D ptFutureUnit = pUnit->GetCP();
        pUnitServer->GetPathDestination(ptFutureUnit);
        CPlot3D ptFutureTarget = pTarget->GetFuturePosition(nTime + 5);
        float fpDistToFuture = distanceXY2(pUnit->GetCP(), ptFutureTarget);
        if (fpDistToFuture < sqr(fpTargetDist) && fpDistToFuture < FP_MIN_FOLLOW_DISTANCE_2)
        {
            bool bNeedGetOut = false;
            float fpDFprev = fpDistToFuture;
            for (int i = 6; i < 100; i++)
            {
                CPlot3D ptFTarget = pTarget->GetFuturePosition(nTime + i);
                float fpDF = distanceXY2(pUnit->GetCP(), ptFTarget);
                if (fpDF > fpDFprev)
                    break;
                if (fpDF < FP_GETAWAY)
                {
                    bNeedGetOut = true;
                    break;
                }
                fpDFprev = fpDF;
            }
            //
            if (bNeedGetOut)
            {
                CPlot3D ptFT = pTarget->GetFuturePosition(nTime + 40);
                CPlot3D ptDir = ptFT - pTarget->GetCP();
                ptDir = CPlot3D(-ptDir.y, ptDir.x, 0);
                if (fabs2(ptDir) > 0)
                {
                    ptDir *= 1.05f * FP_GETAWAY / fabs(ptDir);
                    if ((pUnit->GetCP() - pTarget->GetCP()) * ptDir < 0)
                        ptDir = -ptDir;
                    SAIPath fakePath;
                    CPlot3D ptDest = ptFT + ptDir;
                    theWorldServer.aiMap.NewPath(pUnit, ptDest, fakePath, 10);
                    if (distanceXY2(ptDest, fakePath.GetFinalPoint()) <
                        0.1f) // FP_MIN_FOLLOW_DISTANCE_2 )
                    {
                        // похоже уйти удасться
                        pUnitServer->CmdMoveTo(ptDest, false, false, 10);
                        nFollowRest = 6;
                    }
                    else
                    {
                        if (distanceXY2(ptFutureUnit, ptFT) > 0.5f)
                        {
                            pUnitServer->CmdMoveTo(ptFT, false, false, 20);
                            nFollowRest = 6;
                        }
                    }
                }
            }
        }
        else
        {
            if (distanceXY2(ptFutureTarget, ptFutureUnit) > FP_FOLLOW_DISTANCE_2)
            {
                pUnitServer->CmdMoveTo(ptFutureTarget, false, false, fpTargetDist * 3 + 10);
                nFollowRest = 6;
            }
        }
    }
    else
    {
        // target is standing
        if (sqr(fpTargetDist) < FP_MIN_FOLLOW_DISTANCE_2)
        {
            pUnitServer->CmdWait();
            return;
        }
        //
        CPlot3D ptDest;
        if (!pUnitServer->GetPathDestination(ptDest))
            ptDest = pUnit->GetCP();
        //
        if (distanceXY2(ptDest, pTarget->GetCP()) > FP_FOLLOW_DISTANCE_2)
        {
            pUnitServer->CmdMoveTo(pTarget->GetCP(), false, false, fpTargetDist * 3 + 10);
            nFollowRest = 0;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::ExecTurnNLook()
{
    float fpAngle, fpTurnSpeed;
    if (pUnitServer->HasMove() || pUnitServer->HasRotate())
        return;
    if (pUnitServer->IsCmdFailed() &&
        distanceXY2(currentRally.ptPoint, pUnitServer->GetUnit()->GetCP()) >
            FP_MAX_SENTRY_POINT_DEVIATION_2)
    {
        StartIdle();
        return;
    }
    // if we are in start rotation for next direction state
    if (nLookFinishTime == -10000)
    {
        if (currentRally.lookOrders.GetSize() == 0)
        {
            StartIdle();
            return;
        }
        nLookFinishTime = -9999;
        fpAngle = currentRally.lookOrders[0].fpAngle;
        fpTurnSpeed = currentRally.lookOrders[0].fpTurnSpeed;
        pUnitServer->CmdRotate(CPlot3D(float(cos(fpAngle)), float(sin(fpAngle)), 0), fpTurnSpeed);
        return;
    }
    // if we are in start waiting phase( rotation is finished )
    if (nLookFinishTime == -9999)
    {
        ASSERT(currentRally.lookOrders.GetSize());
        if (currentRally.lookOrders[0].bRest)
            pUnitServer->CmdRest();
        nLookFinishTime = Float2Int(theWorldServer.GetSegmentTimeCounter() +
                                    currentRally.lookOrders[0].fpTime);
        currentRally.lookOrders.RemoveAt(0);
        return;
    }
    // if enough time elapsed
    if (theWorldServer.GetSegmentTimeCounter() > nLookFinishTime)
        nLookFinishTime = -10000;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::ExecAttack()
{
    if (pTarget.IsEmpty())
    {
        state = US_IDLE;
        return;
    }
    if (GetUnit()->IsHero())
    {
        if (pTarget->IsDead())
        {
            state = US_IDLE;
            return;
        }
    }
    else
    {
        if (pTarget->IsDead() || (pTarget->WhoAmI() == HPC_UNIT &&
                                  !environment.IsUnitVisible(GetUnit(), (CUnit *)pTarget)))
        {
            if (!pTarget->IsDead())
                environment.AddPoI(pTarget->GetCP(), AI_PPOWER_SEEENEMY, AI_PPDEC_SEEENEMY);
            state = US_IDLE;
            return;
        }
    }
    if (!pUnitServer->HasAttack(pTarget) && !pUnitServer->HasCast())
    {
        state = US_IDLE;
        return;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// low level commands starting
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartIdle()
{
    pTarget = NULL;
    state = US_IDLE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartTurnLook(const SAIRallyPoint &newRally)
{
    pTarget = NULL;
    state = US_TURNLOOK;
    nLookFinishTime = -10000;
    currentRally = newRally;
    pUnitServer->CmdMoveTo(currentRally.ptPoint, false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartMove(const CPlot3D &ptDest, bool bRun, bool bGroup)
{
    state = US_MOVE;
    pUnitServer->CmdMoveTo(ptDest, bRun, bGroup);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::ShowMove(const CPlot3D &ptDest, bool bGroup)
{
    pUnitServer->CmdShowMove(ptDest, bGroup, false);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartRotate(const CPlot3D &vecDest, float fpTurnSpeed)
{
    state = US_MOVE;
    pUnitServer->CmdRotate(vecDest, fpTurnSpeed);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartAttack(CMapObject *pVictim)
{
    state = US_ATTACK;
    pTarget = pVictim;
    DWORD dwAck = ACK_HERO_NOTIFY_DECIDED_TO_ATTACK;
    if (pUnitServer->GetUnit()->GetOwner())
    {
        SUnitBasicCmd cmd;
        cmd.MakeAttack(pVictim);
        if (pUnitServer->CanAttack(cmd))
            dwAck = ACK_NOTHING;
    }
    pUnitServer->CmdAttack(pTarget, RPGHL_ALL, false, dwAck);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::ResetLogic()
{
    environment.Clear();
    SetLogic(alarmLogics[(int)nCurrentAlarm], nCurrentAlarm);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartPlayerAttack(CMapObject *pVictim, ERPGHitLocations hl, bool bRun)
{
    if (!GetUnit()->IsHero())
    {
        if (pVictim->WhoAmI() == HPC_UNIT &&
            !environment.IsUnitVisible(GetUnit(), (CUnit *)pVictim))
        {
            ASSERT(0);
            return;
        }
    }
    state = US_ATTACK;
    pTarget = pVictim;
    pUnitServer->CmdAttack(pTarget, hl, bRun, ACK_HERO_ACK_ATTACK);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartUsingSkill(CMapObject *pVictim, int nSkill, int nItemID, bool bRun)
{
    if (pVictim == 0)
        return;
    state = US_SKILL;
    pTarget = NULL;
    switch (nSkill)
    {
    case RPGMSKL_STEALING:
        ASSERT(pVictim->WhoAmI() == HPC_UNIT);
        if (pVictim->WhoAmI() != HPC_UNIT)
            return;
        pUnitServer->CmdSteal((CUnit *)pVictim, bRun);
        break;
    case RPGMSKL_SCIENCE:
        pUnitServer->CmdScience(pVictim, nItemID, bRun);
        break;
    case RPGMSKL_TAME:
        ASSERT(pVictim->WhoAmI() == HPC_UNIT);
        if (pVictim->WhoAmI() != HPC_UNIT)
            return;
        pUnitServer->CmdTame((CUnit *)pVictim, bRun);
        break;
    case RPGMSKL_LOOT:
        ASSERT(pVictim->WhoAmI() == HPC_UNIT);
        if (pVictim->WhoAmI() != HPC_UNIT)
            return;
        pUnitServer->CmdLoot((CUnit *)pVictim, bRun);
        break;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartCast(int nQSpellNum, const CPlot3D &pointToCast, bool bRun)
{
    if (!pUnitServer->IsPotion(nQSpellNum))
        state = US_CAST;
    nSpellBeingCast = nQSpellNum;
    pUnitServer->CmdCast(nQSpellNum, pointToCast, bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartCast(int nQSpellNum, CMapObject *pTarg, bool bRun)
{
    if (!pUnitServer->IsPotion(nQSpellNum))
        state = US_CAST;
    pTarget = pTarg;
    nSpellBeingCast = nQSpellNum;
    pUnitServer->CmdCast(nQSpellNum, pTarg, bRun);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::StartFollow(CMapObject *pPastor)
{
    state = US_FOLLOW;
    pTarget = pPastor;
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CAIUnitLogic::GetUnit()
{
    return pUnitServer->GetUnit();
}
/////////////////////////////////////////////////////////////////////////////////////
// припомним кто это был на всякий случай, позовем на помощь
void CAIUnitLogic::TakeDamage(CMapObject *pAttacker)
{
    pHitBy = pAttacker;
    if (pAttacker)
    {
        if (!WasHitByAlly())
            CallForHelp(pAttacker->GetCP());
    }
    else
        CallForHelp(GetUnit()->GetCP());
    if (!nAlarmInvoked && alarmCondition == AC_IS_ATTACKED)
        InvokeAlarm();
    if (pAttacker && pAttacker->WhoAmI() == HPC_UNIT)
        environment.AddVisibleUnit(pUnitServer->GetUnit(), (CUnit *)pAttacker);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIUnitLogic::WasHitByAlly()
{
    if (!pHitBy.IsEmpty() && pHitBy->WhoAmI() == HPC_UNIT)
    {
        if (theWorldServer.fof.IsPlayerAlly(GetUnit()->GetPlayerNum(),
                                            ((CUnit *)pHitBy)->GetPlayerNum()))
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIUnitLogic::GenerateGuardRallyPoint(const CPlot3D &ptCenter, float fpRadius, CPlot3D &ptRes)
{
    if (nGuardPntFail > 0)
    {
        nGuardPntFail--;
        return false;
    }
    float fpX, fpY;
    int nSafeCounter = 10;
    do
    {
        fpX = random.GetFloat(-fpRadius, fpRadius);
        fpY = random.GetFloat(-fpRadius, fpRadius);
        if (--nSafeCounter < 0)
            break;
    } while (sqr(fpX) + sqr(fpY) > sqr(fpRadius));
    fpX = ptCenter.x + fpX;
    fpY = ptCenter.y + fpY;
    ptRes = CPlot3D(fpX, fpY, 0);
    // check if terrain is flat enough
    if (theWorldServer.aiMap.GetTerrainGrad(ptRes) > 0.3f)
        return FALSE;
    // test if there are some units messing around
    CUnitList units;
    CUnit *pThis = GetUnit();
    theWorldServer.aiMap.GetUnitsNear(ptRes, units, pThis->GetCollisionDistance() * 2.5f);
    for (POSITION pos = units.GetHeadPosition(); pos;)
        if (theWorldServer.fof.IsPlayerAlly(units.GetNext(pos)->GetPlayerNum(),
                                            pThis->GetPlayerNum()))
            return FALSE;
    // is it not too far to get there?
    SAIPath fakePath;
    float fpDist = fabs(pUnitServer->GetUnit()->GetCP() - ptRes);
    float fpMaxPathLeng = fpDist * 3 + 10;
    bool bRes = true;
    if (!theWorldServer.aiMap.NewPath(pUnitServer->GetUnit(), ptRes, fakePath, fpMaxPathLeng))
        bRes = false;
    if (bRes && fakePath.nPathCost > 5000 * Meter2AITile(fpDist))
        bRes = false;
    if (!bRes)
        nGuardPntFail = Min(10, Float2Int(fpDist / 3));
    return bRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::Load(CSave &file)
{
    int i, k, iControl, nLogics;
    CAIUnitMotivation *pLoad;
    ClearMotivations();

    file.Read(i);
    state = EUnitState(i);
    currentRally.Load(file);
    file.Read(nLookFinishTime);
    pTarget.Load(file, theWorldServer);
    pHitBy.Load(file, theWorldServer);
    file.Read(fpCallForHelpRadius);
    file.Read(fpSuspectRadius);
    file.Read(fpAttackRadius);
    file.Read(fpRadiusKoef);
    file.Read(&ptPreviousPoint, sizeof(ptPreviousPoint));
    file.Read(&ptCurrentPoint, sizeof(ptCurrentPoint));
    file.Read(&alarmCondition, sizeof(alarmCondition));
    file.Read(nLogics);
    file.Read(iControl);
    pControl = NULL;
    for (k = 0; k < nLogics; k++)
    {
        file.Read(i);
        switch (i)
        {
        case AI_UM_GUARD:
            pLoad = dbgnew CUMGuard;
            break;
        case AI_UM_PATROL:
            pLoad = dbgnew CUMPatrol;
            break;
        case AI_UM_SENTRY:
            pLoad = dbgnew CUMSentry;
            break;
        case AI_UM_SUSPECTION:
            pLoad = dbgnew CUMSuspection;
            break;
        case AI_UM_AGGRESSION:
            pLoad = dbgnew CUMAggression;
            break;
        case AI_UM_CORPSEWATCHER:
            pLoad = dbgnew CUMCorpseWatcher;
            break;
        case AI_UM_FEAR:
            pLoad = dbgnew CUMFear;
            break;
        case AI_UM_PLAYER:
            pLoad = dbgnew CUMPlayer;
            break;
        case AI_UM_TAMED:
            pLoad = dbgnew CUMTamed;
            break;
        case AI_UM_FOLLOW:
            pLoad = dbgnew CUMFollow;
            break;
        default:
            ASSERT(0 && "Unknown motivation loaded");
            return;
        }
        pLoad->Load(file);
        motivations.AddTail(pLoad);
        if (k == iControl)
            pControl = motivations.GetTail();
    }
    environment.Load(file);
    file.Read(nAlarmInvoked);
    file.Read(nAlarmNumber);
    LoadCArray<SGeneralUnitLogic, SGeneralUnitLogic &>(alarmLogics, file);
    file.Read(nAlarmDelay);
    PatchCallForHelpRadius();
    file.Read(bAlwaysOnTheRun);
    if (file.GetVersion() < 75)
        return;
    caster.Load(file);
    if (file.GetVersion() < 113)
        return;
    file.Read(nCurrentAlarm);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitLogic::Store(CSave &file)
{
    POSITION pos;
    int k, iControl;
    CAIUnitMotivation *pLoad;

    file.Write((int)state);
    currentRally.Store(file);
    file.Write(nLookFinishTime);
    pTarget.Store(file);
    pHitBy.Store(file);
    file.Write(fpCallForHelpRadius);
    file.Write(fpSuspectRadius);
    file.Write(fpAttackRadius);
    file.Write(fpRadiusKoef);
    file.Write(&ptPreviousPoint, sizeof(ptPreviousPoint));
    file.Write(&ptCurrentPoint, sizeof(ptCurrentPoint));
    file.Write(&alarmCondition, sizeof(alarmCondition));
    file.Write((int)motivations.GetCount());
    pos = motivations.GetHeadPosition();
    iControl = -1;
    k = 0;
    while (pos)
    {
        pLoad = motivations.GetNext(pos);
        if (pLoad == pControl)
            iControl = k;
        k++;
    }
    file.Write(iControl);
    pos = motivations.GetHeadPosition();
    while (pos)
    {
        pLoad = motivations.GetNext(pos);
        file.Write((int)pLoad->GetType());
        pLoad->Store(file);
    }
    environment.Store(file);
    file.Write(nAlarmInvoked);
    file.Write(nAlarmNumber);
    StoreCArray<SGeneralUnitLogic, SGeneralUnitLogic &>(alarmLogics, file);
    file.Write(nAlarmDelay);
    file.Write(bAlwaysOnTheRun);
    caster.Store(file);
    file.Write(nCurrentAlarm);
}
/////////////////////////////////////////////////////////////////////////////////////
