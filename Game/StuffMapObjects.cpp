#include "StdAfx.h"
#include "ItemApproachCatalog.h"
#include "EngineDiagnostics.h"
#include "ModRegisteredLoot.h"
#include "ModRuntime.h"
#include "UnitServer.h"
#include <cmath>
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "StuffMapObjects.h"
#include "World.h"
#include "WorldServer.h"
#include "WorldClient.h"
#include "Unit.h"
#include "SavePrimitives.h"
#include "METraps.h"
#include "SpellManager.h"
#include "LeverDatabase.h"
#include "UnitServer.h"
/////////////////////////////////////////////////////////////////////////////////////
// радиус, в котором срабатывает механическа€ ловушка
const float FP_TRAP_RADIUS = 1;
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                         class CTree functions                              ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CTree::CTree()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CTree::~CTree()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                       class CBuilding functions                            ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CBuilding::CBuilding()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CBuilding::~CBuilding()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                         class CStone functions                             ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CStone::CStone()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CStone::~CStone()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                      class CStuffObject functions                          ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CStuffObject::CStuffObject()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CStuffObject::~CStuffObject()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                            CGeneralTrapObject                              ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
BOOL CGeneralTrapObject::Create()
{
    BOOL bRv = CMapObject::Create();
    dwEnemyMask = 0xFFFFFFFF;
    SetFlag(TRAP_ACTIVE);
    SetFlag(OBJ_HIDDEN);
    return bRv;
}
// ******************************************************************************** //
// **                      class CTrapObject functions                           ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CTrapObject::CTrapObject()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTrapObject::Create()
{
    BOOL bRv = CMapObject::Create();
    ResetFlag(OBJ_HIDDEN);
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
int CTrapObject::Segment()
{
    CMapObject::Segment();
    if (CheckFlag(TRAP_ACTIVE) && GetWorld()->IsServer())
    {
        CUnitList units;
        CWorldServer *pWS = (CWorldServer *)GetWorld();
        pWS->aiMap.GetUnitsNear(GetCP(), units, FP_TRAP_RADIUS);
        if (CheckFlag(TRAP_STARTUP))
        {
            if (units.GetCount() == 0)
                ResetFlag(TRAP_STARTUP);
        }
        if (units.GetCount())
        {
            CUnit *pTarget = units.GetHead();
            // crap - visualize
            CString szScript;
            float fpX = GetCP().x;
            float fpY = GetCP().y;
            szScript.Format("Worldscript( CreateParticleSource( 100, %g, %g, %g, 1, \"Mushroom\"))",
                            fpX, fpY, pWS->GetSurfaceValue(fpX, fpY));
            theWorldServer.script.LoadText(szScript);
            // complection.grace = 1;
            // CRAP here some mechanical damage should be inflicted
            // CRAP - change appearence
            ResetFlag(TRAP_ACTIVE);
        }
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTrapObject::ActivateTrap()
{
    SetFlag(TRAP_STARTUP);
    SetFlag(TRAP_ACTIVE);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CTrapObject::ApplyScience(const SRPGScienceStats &tool, CUnit *pWho)
{
    if (CheckFlag(TRAP_ACTIVE) && tool.CanDisarmTrap())
    {
        // CRAP - change appearence, may be pick up trap
        ResetFlag(TRAP_ACTIVE);
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTrapObject::CanApplyScience(const SRPGScienceStats &tool)
{
    return tool.CanDisarmTrap();
}
/////////////////////////////////////////////////////////////////////////////////////
void CTrapObject::Load(CSave &file)
{
    CMapObject::Load(file);
    file.Read(dwEnemyMask);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTrapObject::Store(CSave &file)
{
    CMapObject::Store(file);
    file.Write(dwEnemyMask);
}
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                   class CMagicTrapObject functions                         ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
// выбрать всех юнитов, наход€щихс€ в области, описываемой trapAreas и €вл€ющихс€ врагами
// по маске dwEnemyMask
static void GetUnitsOnArea(CWorldServer *pWS, CAArray<SMEArea, SMEArea &> &trapAreas,
                           DWORD dwEnemyMask, CList<CUnit *, CUnit *> &unitsRes)
{
    CUnitList units;
    unitsRes.RemoveAll();
    for (int i = 0; i < trapAreas.GetSize(); i++)
    {
        CPlot3D ptAreaCenter(0, 0, 0);
        float fpAreaRadius;
        trapAreas[i].GetRound(ptAreaCenter.x, ptAreaCenter.y, fpAreaRadius);
        pWS->aiMap.GetUnitsNear(ptAreaCenter, units, fpAreaRadius);
        for (POSITION pos = units.GetHeadPosition(); pos;)
        {
            CUnit *pUnit = units.GetNext(pos);
            if (pUnit->IsDead())
                continue;
            // check for diplomacy
            if (dwEnemyMask & (1 << pUnit->GetPlayerNum()))
            {
                // additional check for point to be inside is not required when we use rounds
                unitsRes.AddTail(pUnit);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
CMagicTrapObject::CMagicTrapObject()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMagicTrapObject::Create()
{
    BOOL bRv = CGeneralTrapObject::Create();
    pSpell = 0;
    trapAreas.SetSize(0);
    ptTargets.SetSize(0);
    trappedPersons.RemoveAll();
    nCastingInterval = 10;
    nTimeToCast = 0;
    bCastOnce = FALSE;
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMagicTrapObject::Destroy()
{
    CMapObject::Destroy();
    pSpell = 0;
    trapAreas.SetSize(0);
    ptTargets.SetSize(0);
    trappedPersons.RemoveAll();
}
/////////////////////////////////////////////////////////////////////////////////////
void CMagicTrapObject::SetParams(SMEMagicTrap &data)
{
    dwEnemyMask = data.dwDiplomacy;
    pSpell = GetWorld()->GetSpellManager()->CreateSpell(data.szSpell);
    SetCP(data.ptPosition);
    trapAreas = data.aAreas;
    ptTargets = data.aTargets;
    trappedPersons.RemoveAll();
    nCastingInterval = data.nCastingInterval;
    nTimeToCast = 0;
    bCastOnce = data.bCastOnce ? TRUE : FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CMagicTrapObject::IsUnitOnTrap(CUnit *pUnit)
{
    for (int i = 0; i < trapAreas.GetSize(); i++)
    {
        if (trapAreas[i].IsInside(pUnit->GetCP()))
            return TRUE;
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// окучить попавших под раздачу юнитов (или выстрелить в заготовленные места)
void CMagicTrapObject::CastSpell(CList<CUnit *, CUnit *> &targets)
{
    if (targets.GetCount() == 0)
        return;
    if (pSpell.IsEmpty())
        return;
    CPlot3D ptSrc = GetCP();
    ptSrc.z += GetWorldBase()->GetSurfaceValue(ptSrc.x, ptSrc.y);
    // кастуем спелл либо на юнита, либо на указанную область
    if (pSpell->IsOnUnit() || ptTargets.GetSize() == 0)
    {
        // find best target if only one target is allowed
        if (bCastOnce)
        {
            CUnit *pTarget = 0;
            float fpMinDist = 1e30f;
            for (POSITION pos = targets.GetHeadPosition(); pos;)
            {
                CUnit *pUnit = targets.GetNext(pos);
                float fpDist = fabs2(pUnit->GetCP() - GetCP());
                if (fpDist < fpMinDist)
                {
                    fpDist = fpMinDist;
                    pTarget = pUnit;
                }
            }
            ASSERT(pTarget);
            if (!pTarget)
                return;
            targets.RemoveAll();
            targets.AddTail(pTarget);
        }
        // fire at every target
        for (POSITION pos = targets.GetHeadPosition(); pos;)
        {
            CUnit *pTarget = targets.GetNext(pos);
            theWorldServer.CastSpell(pSpell, 0, ptSrc, pTarget, pTarget->GetCP());
        }
    }
    else
    {
        for (int i = 0; i < ptTargets.GetSize(); i++)
        {
            CPlot3D ptTarget(ptTargets[i].x, ptTargets[i].y, 0);
            theWorldServer.CastSpell(pSpell, 0, ptSrc, 0, ptTarget);
        }
    }
    nTimeToCast = nCastingInterval;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMagicTrapObject::WorkAsTrigger()
{
    POSITION pos;
    // for every trapped unit check if one is dead or outside of trapAreas
    for (pos = trappedPersons.GetHeadPosition(); pos;)
    {
        POSITION delPos = pos;
        CUnit *pUnit = trappedPersons.GetNext(pos);
        if (!IsUnitOnTrap(pUnit) || pUnit->IsDead())
            trappedPersons.RemoveAt(delPos);
    }
    // check if there are new victims
    if (CheckFlag(TRAP_ACTIVE))
    {
        BOOL bCasted = FALSE;
        CWorldServer *pWS = (CWorldServer *)GetWorld();
        CList<CUnit *, CUnit *> units, targets;
        GetUnitsOnArea(pWS, trapAreas, dwEnemyMask, units);
        for (pos = units.GetHeadPosition(); pos;)
        {
            CUnit *pUnit = units.GetNext(pos);
            if (!trappedPersons.Find(pUnit))
            {
                // нашли нового юнита, который наступил на ловушку
                trappedPersons.AddTail(pUnit);
                targets.AddTail(pUnit);
            }
        }
        if (targets.GetCount())
            CastSpell(targets);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CMagicTrapObject::DamageValidTargets()
{
    CWorldServer *pWS = (CWorldServer *)GetWorld();
    CList<CUnit *, CUnit *> units;
    if (!CheckFlag(TRAP_ACTIVE))
        return;
    GetUnitsOnArea(pWS, trapAreas, dwEnemyMask, units);
    if (units.GetCount())
        CastSpell(units);
}
/////////////////////////////////////////////////////////////////////////////////////
// собственно выполн€ет функции ловушки
int CMagicTrapObject::Segment()
{
    int nRv = CMapObject::Segment();
    if (GetWorld()->IsServer() && !pSpell.IsEmpty())
    {
        if (nCastingInterval <= 0)
            WorkAsTrigger();
        else
        {
            if (nTimeToCast > 0)
                --nTimeToCast;
            if (nTimeToCast <= 0)
                DamageValidTargets();
        }
    }
    return nRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CMagicTrapObject::RemoveReferencesOn(CMapObject *pTarget)
{
    CMapObject::RemoveReferencesOn(pTarget);
    for (POSITION pos = trappedPersons.GetHeadPosition(); pos;)
    {
        POSITION delPos = pos;
        CUnit *pUnit = trappedPersons.GetNext(pos);
        if (pUnit == pTarget)
            trappedPersons.RemoveAt(delPos);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CMagicTrapObject::Load(CSave &file)
{
    CMapObject::Load(file);
    pSpell = LoadGBSmartPtr<CSpell>(file, GetWorld()->GetSpellManager());
    LoadCArray(trapAreas, file);
    LoadMapObjectPtrList(trappedPersons, file, *GetWorld());
    LoadCArray(ptTargets, file);
    file.Read(nCastingInterval);
    file.Read(nTimeToCast);
    file.Read(bCastOnce);
    file.Read(dwEnemyMask);
}
/////////////////////////////////////////////////////////////////////////////////////
void CMagicTrapObject::Store(CSave &file)
{
    CMapObject::Store(file);
    StoreGBPtr(file, pSpell);
    StoreCArray(trapAreas, file);
    StoreMapObjectPtrList(trappedPersons, file);
    StoreCArray(ptTargets, file);
    file.Write(nCastingInterval);
    file.Write(nTimeToCast);
    file.Write(bCastOnce);
    file.Write(dwEnemyMask);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                     class CLeverObject functions                           ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CLeverObject::CLeverObject()
{
    Create();
#ifdef _DEBUG
    pszGameClassName = GetGameClassName();
#endif // _DEBUG
}
/////////////////////////////////////////////////////////////////////////////////////
CLeverObject::~CLeverObject()
{
    Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLeverObject::Create()
{
    BOOL bRv = CMapObject::Create();
    fpStartVal = fpFinalVal = fpFinalTime = fpTimePeriod1 = 0;
    update.SetHistoriesNumber(VLD_MASK_LEVERMAX + 1);
    science.Clear();
    nCurrentState = 0;
    nTotalStates = 1;
    bCycle = 0;
    bIsDoor = false;
    bGraphRecalc = false;
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
float CLeverObject::GetAttackDistance()
{
    ASSERT(bv.IsSphere());
    return bv.fpRadius;
}
/////////////////////////////////////////////////////////////////////////////////////
void CLeverObject::SetParams(SMELever &data)
{
    nCurrentState = data.nCurrentState;
    nTotalStates = data.nTotalStates;
    bCycle = data.isCycled ? TRUE : FALSE;
    science = data.science;
    bIsDoor = data.isDoor;
    bGraphRecalc = data.needGraphRecalc;
    if (nTotalStates > 1)
        complection.grace = float(nCurrentState) / (nTotalStates - 1);
    else
        complection.grace = 0;
    SetUpdateMask(CMapObject::VLD_MASK_COMPLECTION);
}
/////////////////////////////////////////////////////////////////////////////////////
int CLeverObject::Segment()
{
    CMapObject::Segment();
    // здесь проводим изменение complection дл€ поворота этого рычага на клиенте
    if (!GetWorld()->IsServer())
    {
        float fCoeff = (fpFinalTime - theWorld.GetSegmentTimeCounter()) * fpTimePeriod1;
        SComplection comp;
        GetComplection(comp);
        if (fCoeff <= 0)
        {
            if (comp.grace != fpFinalVal)
            {
                comp.grace = fpFinalVal;
                SetComplection(comp);
                theWorld.UpdateBridgeInformation(this);
            }
            return 0;
        }
        else
        {
            comp.grace = fpFinalVal - fCoeff * (fpFinalVal - fpStartVal);
            SetComplection(comp);
            theWorld.UpdateBridgeInformation(this);
            return 1;
        }
    }
    //
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CLeverObject::UpdateTo()
{
    CUpdateHistory *pPkt;
    CMapObject::UpdateTo();

    if (pPkt = update.GetHistory(VLD_MASK_LEVER))
    {
        if (fpFinalVal != complection.grace)
        {
            fpFinalVal = complection.grace;
            fpTimePeriod1 = 0;
        }
        pPkt->Write(&science, sizeof(science));
        (*pPkt) << fpStartVal << fpFinalVal << fpFinalTime << fpTimePeriod1;
        bool bDisable = CheckFlag(LEVER_DISABLED) != 0;
        pPkt->Write(&bDisable, sizeof(bDisable));
        pPkt->FinishUpdate();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLeverObject::UpdateFrom(EUpdateType tip)
{
    BOOL bRv;
    CUpdateHistory *pPkt;

    // mapobject update
    bRv = CMapObject::UpdateFrom(tip);

    // unit RPG stats
    if (pPkt = update.GetHistory(VLD_MASK_LEVER))
    {
        bool bDisable;
        pPkt->Read(&science, sizeof(science));
        (*pPkt) >> fpStartVal >> fpFinalVal >> fpFinalTime >> fpTimePeriod1;
        pPkt->Read(&bDisable, sizeof(bDisable));
        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
        //
        if (bDisable)
            SetFlag(LEVER_DISABLED);
        else
            ResetFlag(LEVER_DISABLED);
        // add to the segment-calling list in the world
        theWorld.AddDynamicObject(this);
        if (tip == utFrame)
        {
            // play sound
            const SLDBPrototype *pLever = theLeverDatabase.levers[GetMaskName()];
            if (pLever && (fpStartVal != fpFinalVal))
                theWorld.GetSoundManager()->BeginFX(pLever->szSwitchSound, 0, GetCP(), 8, 40);
        }
    }

    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CLeverObject::SwitchState(int nState, float fpTime)
{
    POSITION pos;
    int nPrevState = nCurrentState;
    // switch time is not specified, use some default value
    if (fpTime < 0)
    {
        const SLDBPrototype *pLever = theLeverDatabase.levers.GetEntry(GetMaskName());
        if (pLever && pLever->dwSwitchTime >= 0)
            fpTime = pLever->dwSwitchTime;
        else
            fpTime = 10; // default switch state time
    }
    if (nState < 0)
    {
        if (!bCycle)
            nCurrentState = Min(nCurrentState + 1, nTotalStates - 1);
        else
            nCurrentState = (nCurrentState + 1) % (nTotalStates);
    }
    else
        nCurrentState = Max(Min(nState, nTotalStates - 1), 0);
    // check if switching on server
    CWorldServer *pWS = (CWorldServer *)GetWorldBase();
    ASSERT(pWS->IsServer());
    if (!pWS->IsServer())
        return;
    CList<CUnit *, CUnit *> units;
    pWS->aiMap.GetUnitsNear(GetCP(), units, bv.fpRadius + 0.5f);
    for (pos = units.GetHeadPosition(); pos;)
        pWS->aiMap.RemoveObject(units.GetNext(pos));
    // remove object from aiMap
    pWS->aiMap.SetGraphRebuild(bGraphRecalc);
    pWS->aiMap.StartDelayedConvert();
    pWS->aiMap.RemoveObject(this);
    //
    float fpNewVal;
    if (nTotalStates > 1)
        fpNewVal = float(nCurrentState) / (nTotalStates - 1);
    else
        fpNewVal = 0;
    // fill info about transition
    if (fpTime > 0)
    {
        fpStartVal = complection.grace;
        fpFinalVal = fpNewVal;
        fpFinalTime = pWS->GetSegmentTimeCounter() + fpTime + 1;
        fpTimePeriod1 = 1 / fpTime;
    }
    else
    {
        fpStartVal = fpFinalVal = fpNewVal;
        fpFinalTime = 0;
        fpTimePeriod1 = 0;
    }
    // changing complection itself
    complection.grace = fpNewVal;
    LoadFigureBoundingVolumes(GetMaskName());
    // set update flags
    SetUpdateMask(CMapObject::VLD_MASK_LEVER);
    //	SetUpdateMask( CMapObject::VLD_MASK_COMPLECTION );
    // return object to aiMap
    pWS->aiMap.AddObject(this);
    pWS->aiMap.FinishDelayedConvert();
    pWS->aiMap.SetGraphRebuild(true);
    for (pos = units.GetHeadPosition(); pos;)
    {
        CPlot3D ptNewPos;
        CUnit *pUnit = units.GetNext(pos);
        if (!pWS->aiMap.CanPutUnitRelaxed(pUnit))
        {
            ptNewPos = pUnit->GetCP();
            pWS->aiMap.FindUnitPosition(pUnit, pUnit->GetCP(), ptNewPos);
            pUnit->SetCP(ptNewPos);
            ((CUnitServer *)pUnit->GetUnitSpecific())->InitializeUnit();
        }
        pWS->aiMap.AddObject(pUnit);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CLeverObject::EnableLever(bool bEnable)
{
    if (bEnable)
        ResetFlag(LEVER_DISABLED);
    else
        SetFlag(LEVER_DISABLED);
    SetUpdateMask(CMapObject::VLD_MASK_LEVER);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLeverObject::IsScientific()
{
    if (CheckFlag(LEVER_DISABLED))
        return FALSE;
    return science.dwTools != RPGTOOL_NONE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CLeverObject::SetNewScience(const SRPGScienceStats &_science)
{
    science = _science;
    SetUpdateMask(CMapObject::VLD_MASK_LEVER);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CLeverObject::ApplyScience(const SRPGScienceStats &tool, CUnit *pWho)
{
    const bool approach=!ModRuntime::Current()||ScriptActivationAllowed(pWho);
    static unsigned activationSamples=0;
    if(ModRuntime::Current()&&activationSamples++<24)EngineDiagnostics::Event("Lever activate object=%d approach=%d tool=%u required=%u skill=%u requiredSkill=%u canOpen=%d",GetID(),approach,tool.dwTools,science.dwTools,tool.dwSkill,science.dwSkill,tool.CanOpen(science));
    if(!approach)return false;
    if (tool.CanOpen(science))
    {
        SwitchState(-1);
        EnableLever(false);
        JMV_RegisteredLootOnLeverActivated(this,pWho);
        return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CLeverObject::CanApplyScience(const SRPGScienceStats &tool)
{
    return tool.CanOpen(science);
}
/////////////////////////////////////////////////////////////////////////////////////
DWORD CLeverObject::GetScienceKeyNumber()
{
    if (science.dwTools & RPGTOOL_KEY)
        return science.dwKeyNumber;
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CLeverObject::UsesAuthoredInteraction() const { return theLeverDatabase.levers.GetEntry(GetMaskName())!=nullptr; }
static void LeverFallback(CLeverObject&lever,CArray<CPlot3D,CPlot3D&>&points,float radius){
 const auto&p=lever.GetCP();if(!std::isfinite(radius)||radius<=0||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return;
 points.SetSize(16);for(int i=0;i<16;i++){double angle=i*(6.2831853071795864769/16);points[i]=CPlot3D(p.x+radius*float(cos(angle)),p.y+radius*float(sin(angle)),p.z);}
}
bool CLeverObject::ScriptActivationAllowed(CUnit*who){
 if(!who||UsesAuthoredInteraction())return true;auto*server=dynamic_cast<CUnitServer*>(who->GetUnitSpecific());if(!server)return false;
 float radius=who->GetAttackDistance();CArray<CPlot3D,CPlot3D&> points;LeverFallback(*this,points,radius);const auto&cmd=server->GetScriptCommand();bool current=false;
 for(int i=0;i<points.GetSize();i++){auto d=cmd.ptTarget-points[i];if(d.x==0&&d.y==0&&d.z==0)current=true;}
 float dx=who->GetCP().x-cmd.ptTarget.x,dy=who->GetCP().y-cmd.ptTarget.y,ox=who->GetCP().x-GetCP().x,oy=who->GetCP().y-GetCP().y;
 float distance=dx*dx+dy*dy,objectDistance=ox*ox+oy*oy,reach=radius+.2f;
 return cmd.IsScience()&&cmd.pTarget==this&&cmd.HasPointOfUse()&&current&&std::isfinite(distance)&&distance<=.2f*.2f&&std::isfinite(objectDistance)&&std::isfinite(reach)&&radius>0&&objectDistance<=reach*reach;
}
void CLeverObject::GetUsePoints(CArray<CPlot3D, CPlot3D &> &points, float fpUnitRadius)
{
    points.SetSize(0);
    const SLDBPrototype *pLever = theLeverDatabase.levers.GetEntry(GetMaskName());
    if (pLever && ItemApproachCatalog::UsesWorldXY(GetMaskName()))
    {
        // Only catalog-confirmed models use world XY; authored zero points
        // stay empty and invalid rows never expose a partial result.
        if (pLever->dwNumValidPlaces > 2 || !pLever->dwNumValidPlaces) return;
        const CPlot3D origin = GetCP();
        const float tall = complection.tall;
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
            !std::isfinite(fpUnitRadius) || !std::isfinite(pLever->fpScaleMn) || !std::isfinite(tall)) return;
        const float scale = pLever->fpScaleMn * tall + (1 - tall);
        if (!std::isfinite(scale)) return;
        CPlot3D result[2];
        for (DWORD i = 0; i < pLever->dwNumValidPlaces; ++i)
        {
            const float dx = pLever->ptUsePlaces[i].x, dy = pLever->ptUsePlaces[i].y;
            if (!std::isfinite(dx) || !std::isfinite(dy)) return;
            const double length = sqrt(double(dx) * dx + double(dy) * dy);
            result[i] = origin;
            if (length > 0)
            {
                const double distance = length * scale + fpUnitRadius;
                result[i].x += static_cast<float>(dx / length * distance);
                result[i].y += static_cast<float>(dy / length * distance);
            }
            if (!std::isfinite(result[i].x) || !std::isfinite(result[i].y)) return;
        }
        points.SetSize(pLever->dwNumValidPlaces);
        for (DWORD i = 0; i < pLever->dwNumValidPlaces; ++i) points[i] = result[i];
        return;
    }
    if(ModRuntime::Current() && !pLever){LeverFallback(*this,points,fpUnitRadius);return;}
    if (pLever && pLever->dwNumValidPlaces > 0)
    {
        CPlot3D ptUsePoint;
        for (unsigned i = 0; i < pLever->dwNumValidPlaces; i++)
        {
            ptUsePoint = GetCP();
            float fpDx, fpDy, fpMn;
            fpDx = pLever->ptUsePlaces[i].x;
            fpDy = pLever->ptUsePlaces[i].y;
            fpMn = sqrt(sqr(fpDx) + sqr(fpDy));
            if (fpMn > 0)
            {
                fpDx /= fpMn;
                fpDy /= fpMn;
            }
            fpMn *= pLever->fpScaleMn * complection.tall + (1 - complection.tall);
            fpMn += fpUnitRadius;
            ptUsePoint += GetXAxis() * fpDx * fpMn;
            ptUsePoint += GetYAxis() * fpDy * fpMn;
            points.Add(ptUsePoint);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CLeverObject::Load(CSave &file)
{
    CMapObject::Load(file);
    file.Read(nCurrentState);
    file.Read(nTotalStates);
    file.Read(bCycle);
    science.Load(file);
    file.Read(bIsDoor);
    file.Read(bGraphRecalc);
}
/////////////////////////////////////////////////////////////////////////////////////
void CLeverObject::Store(CSave &file)
{
    CMapObject::Store(file);
    file.Write(nCurrentState);
    file.Write(nTotalStates);
    file.Write(bCycle);
    science.Store(file);
    file.Write(bIsDoor);
    file.Write(bGraphRecalc);
}
/////////////////////////////////////////////////////////////////////////////////////
// ******************************************************************************** //
// **                            CTorchObject                                    ** //
// ******************************************************************************** //
/////////////////////////////////////////////////////////////////////////////////////
CTorchObject::CTorchObject()
{
}
/////////////////////////////////////////////////////////////////////////////////////
CTorchObject::~CTorchObject()
{
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTorchObject::Create()
{
    CMapObject::Create();
    pTorchSpecific = 0;

    update.SetHistoriesNumber(VLD_MASK_TORCHMAX + 1);
    update.Clear();
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObject::Destroy()
{
    pTorchSpecific = 0;
    update.Clear();
    CMapObject::Destroy();
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObject::UpdateTo()
{
    CUpdateHistory *pPkt;
    CMapObject::UpdateTo();

    // torch`s params
    if (pPkt = update.GetHistory(VLD_MASK_TORCH))
    {
        pTorchSpecific->UpdateTo(*pPkt);
        pPkt->FinishUpdate();
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTorchObject::UpdateFrom(EUpdateType tip)
{
    BOOL bRv;
    CUpdateHistory *pPkt;

    // mapobject update
    bRv = CMapObject::UpdateFrom(tip);

    // unit RPG stats
    if (pPkt = update.GetHistory(VLD_MASK_TORCH))
    {
        pTorchSpecific->UpdateFrom(*pPkt);
        bRv &= pPkt->UpdateFromIsOk();
        pPkt->FinishUpdate();
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObject::Load(CSave &file)
{
    CMapObject::Load(file);
    pTorchSpecific->Load(file);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObject::Store(CSave &file)
{
    CMapObject::Store(file);
    pTorchSpecific->Store(file);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObject::SetSpecific(LPGAMEBASE lpNewSpecific)
{
    if (lpNewSpecific->GetTypeID() == SPTID_TORCH_SPECIFIC)
        SetTorchSpecific((CTorchObjectSpecific *)lpNewSpecific);
    else
        CMapObject::SetSpecific(lpNewSpecific);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectSpecific::UpdateTo(CUpdateHistory &pkt)
{
    pkt << (int)tidParticlesType;
    pkt << fpParticleScale;
    pkt << fpLightRange;
    pkt.Write(&dcvLightColor, sizeof(dcvLightColor));
    pkt.Write(&ptParticlesCenter, sizeof(ptParticlesCenter));
    pkt.Write(&ptLightCenter, sizeof(ptLightCenter));
    pkt.WriteString(szSFXName);
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectSpecific::UpdateFrom(CUpdateHistory &pkt)
{
    int nTID;
    pkt >> nTID;
    tidParticlesType = ETypeIDs(nTID);
    pkt >> fpParticleScale;
    pkt >> fpLightRange;
    pkt.Read(&dcvLightColor, sizeof(dcvLightColor));
    pkt.Read(&ptParticlesCenter, sizeof(ptParticlesCenter));
    pkt.Read(&ptLightCenter, sizeof(ptLightCenter));
    szSFXName = pkt.ReadString();

    SetParticles(tidParticlesType, ptParticlesCenter, fpParticleScale);
    SetLight(ptLightCenter, fpLightRange, dcvLightColor);
    SetSFX(szSFXName);
    /*
    if ( ((CWorldClient*)GetWorld())->IsNightTime() )
    {
    */
    TurnOn();
    StartupEffect();
    /*
    }
    else
            TurnOff();
    */
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectSpecific::Load(CSave &file)
{
    file.Read(&tidParticlesType, sizeof(tidParticlesType));

    file.Read(szSFXName);
    file.Read(fpParticleScale);
    file.Read(fpLightRange);
    file.Read(&dcvLightColor, sizeof(dcvLightColor));

    file.Read(&ptParticlesCenter, sizeof(ptParticlesCenter));
    file.Read(&ptLightCenter, sizeof(ptLightCenter));
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectSpecific::Store(CSave &file)
{
    file.Write(&tidParticlesType, sizeof(tidParticlesType));

    file.Write(szSFXName);
    file.Write(fpParticleScale);
    file.Write(fpLightRange);
    file.Write(&dcvLightColor, sizeof(dcvLightColor));

    file.Write(&ptParticlesCenter, sizeof(ptParticlesCenter));
    file.Write(&ptLightCenter, sizeof(ptLightCenter));
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectSpecific::SetParticles(ETypeIDs tidParticlesTypeID, const CPlot3D &position,
                                        const float fpScale)
{
    tidParticlesType = tidParticlesTypeID;
    ptParticlesCenter = position;
    fpParticleScale = fpScale;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectSpecific::SetLight(const CPlot3D &position, const float range,
                                    const D3DCOLORVALUE &color)
{
    ptLightCenter = position;
    fpLightRange = range;
    dcvLightColor = color;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectSpecific::SetSFX(LPCSTR pszSFXName)
{
    szSFXName = pszSFXName;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CTorchObjectSpecific::Create()
{
    lpTorch = 0;

    tidParticlesType = TID_EMPTY;
    szSFXName.Empty();
    fpParticleScale = 1;
    fpLightRange = 10;
    dcvLightColor.r = 1;
    dcvLightColor.g = 1;
    dcvLightColor.b = 1;
    dcvLightColor.a = 1;

    ptParticlesCenter = VNULL;
    ptLightCenter = VNULL;

    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
void CTorchObjectSpecific::Destroy()
{
    lpTorch = 0;

    szSFXName.Empty();
}
/////////////////////////////////////////////////////////////////////////////////////
