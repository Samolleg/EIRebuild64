#include "StdAfx.h"
#include "GameJournal.h"
#include "ModMobCommands.h"
#include "ModNpcConsumables.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "Script.h"
#include "ScriptParser.h"
#include "MapObjectManager.h"
#include "WorldServer.h"
#include "LightManager.h"
#include "UnitClientSpecific.h"
#include "PlayerManager.h"
#include "PlayerServer.h"
#include "Particle.h"
#include "MaterialManager.h"
#include "RandomGen.h"
#include "MapObjectServer.h"
#include "UnitServer.h"
#include "EffectManager.h"
#include "player.h"
#include "console.h"
#include "Camera.h"

#include "Effect.h"
#include "StuffMapObjects.h"
#include "ScenarioTracker.h"
#include "QuestDatabase.h"
#include "SpellManager.h"
#include "ItemManager.h"
// crap for camera
#include "WorldInterface.h"

#include "MoviePlayback.h"
#include "Renderer.h"

/*==========================================================================================================
        Scripts execution

        Nival Interactive, 1999
==========================================================================================================*/
/////////////////////////////////////////////////////////////////////////////////////
static CScript *pLoadingScript;
/////////////////////////////////////////////////////////////////////////////////////
void LoadCameraPlacements(LPCSTR pszFileName);
/////////////////////////////////////////////////////////////////////////////////////
// quest script creation code
static CString szCurrentQuest;
struct SQuestSubobj
{
    ESCCmd cmd;
    CString szParam;
    float fpParam;
};
static CArray<SQuestSubobj, SQuestSubobj &> questSubObjs;
static void StartQuest(const char *pszName)
{
    ASSERT(szCurrentQuest == "");
    questSubObjs.RemoveAll();
    szCurrentQuest = pszName;
}
/////////////////////////////////////////////////////////////////////////////////////
static void FinishQuest(CScript *pScript)
{
    int i;
    CString szQuest;
    // declare script subobjs
    for (i = 0; i < questSubObjs.GetSize() + 1; i++)
    {
        CString szDecl;
        szDecl.Format("DeclareScript Su%i()\n", i);
        szQuest += szDecl;
    }
    // create script for every subobj
    for (i = 0; i < questSubObjs.GetSize(); i++)
    {
        CString szHead, szCond, szTail, szTail2;
        szHead.Format("Script Su%i(if(\n", i);
        szTail.Format("\n)then(\nKillScript()\nSu%i()\n"
                      "GSSetVar(0,\"q.%s.%s.%i\",2)\n",
                      i + 1, (const char *)szCurrentQuest, (const char *)szCurrentQuest, i + 1);
        //
        if (i == questSubObjs.GetSize() - 1)
            szTail2 = ")\n)\n";
        else
        {
            szTail2.Format("GSSetVar(0,\"q.%s.%s.%i\",1)\n"
                           ")\n)\n",
                           (const char *)szCurrentQuest, (const char *)szCurrentQuest, i + 2);
        }
        SQuestSubobj &q = questSubObjs[i];
        switch (q.cmd)
        {
        case SC_QOBJAREA:
            szCond.Format("any( i, Heroes, IsInArea( %f, GetX(i), GetY(i) ) )", q.fpParam);
            break;
        case SC_QOBJSEEOBJECT:
            szCond.Format("any( i, Heroes, IsLess( DistanceUnitUnit( i, %s ), 7 ) )",
                          (const char *)q.szParam);
            break;
        case SC_QOBJSEEUNIT:
            szCond.Format("IsUnitVisible( %s )", (const char *)q.szParam);
            break;
        case SC_QOBJKILLUNIT:
            szCond.Format("Not( IsAlive( %s ) )", (const char *)q.szParam);
            break;
        case SC_QOBJKILLGROUP:
            szCond.Format("Not( any( i, %s, IsAlive(i) ) )", (const char *)q.szParam);
            break;
        case SC_QOBJUSE:
            szCond.Format("IsEqual( GetLeverState(%s), %f )", (const char *)q.szParam, q.fpParam);
            break;
        case SC_QOBJGETITEM:
            szCond.Format("HaveItem( 0, %f )", q.fpParam);
            break;
        }
        szQuest += szHead + szCond + szTail + szTail2;
    }
    // create script for quest finish
    {
        CString szComplete;
        szComplete.Format("Script Su%i(if()then(KillScript()\nQuestComplete(0,\"%s\")\n"
                          "GSSetVar(0,\"q.%s.%s\", 2)\n"
                          ")\n)\n",
                          questSubObjs.GetSize(), (const char *)szCurrentQuest,
                          (const char *)szCurrentQuest, (const char *)szCurrentQuest);
        szQuest += szComplete;
    }
    szQuest += "WorldScript( Su0() )\n";
    pScript->LoadText(szQuest);
    szCurrentQuest = "";
}
/////////////////////////////////////////////////////////////////////////////////////
static void AddSubObj(ESCCmd cmd, const CString &szParam, float fpParam)
{
    ASSERT(szCurrentQuest != "");
    SQuestSubobj q;
    q.cmd = cmd;
    q.szParam = szParam;
    q.fpParam = fpParam;
    questSubObjs.Add(q);
}
/////////////////////////////////////////////////////////////////////////////////////
void SScriptLightInfo::Clear()
{
    if (pLight)
    {
        theWorldServer.RemoveEffect(pLight->GetID());
        pLight->Release();
    }
    pLight = NULL;
}
// lightning removal
void SLightningInfo::Clear()
{
    if (pLightning)
    {
        theWorldServer.RemoveEffect(pLightning->GetID());
        pLightning->Release();
    }
    pLightning = NULL;
}

void SParticleInfo::Clear()
{
    if (pParticle)
    {
        theWorldServer.RemoveEffect(pParticle->GetID());
        pParticle->Release();
    }
    pParticle = NULL;
}

void SFXInfo::Clear()
{
    if (pSound)
    {
        theWorldServer.RemoveEffect(pSound->GetID());
        pSound->Release();
    }
    pSound = NULL;
}

/////////////////////////////////////////////////////////////////////////////////////
// load/store script effects information

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// SScriptArea
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
bool SScriptArea::SScriptAreaElement::IsInArea(float fpX, float fpY)
{
    if (bRect)
        return fpX >= fpLowX && fpX <= fpHighX && fpY >= fpLowY && fpY <= fpHighY;
    return sqr(fpX - fpCenterX) + sqr(fpY - fpCenterY) < fpRadius2;
}
/////////////////////////////////////////////////////////////////////////////////////
void SScriptArea::AddSquare(float fpLowX, float fpLowY, float fpHighX, float fpHighY)
{
    SScriptAreaElement elem;
    elem.bRect = true;
    elem.fpLowX = fpLowX;
    elem.fpLowY = fpLowY;
    elem.fpHighX = fpHighX;
    elem.fpHighY = fpHighY;
    areas.Add(elem);
}
/////////////////////////////////////////////////////////////////////////////////////
void SScriptArea::AddRound(float fpCenterX, float fpCenterY, float fpRadius)
{
    SScriptAreaElement elem;
    elem.bRect = false;
    elem.fpCenterX = fpCenterX;
    elem.fpCenterY = fpCenterY;
    elem.fpRadius2 = sqr(fpRadius);
    areas.Add(elem);
}
/////////////////////////////////////////////////////////////////////////////////////
bool SScriptArea::IsInArea(float fpX, float fpY)
{
    for (int i = 0; i < areas.GetSize(); i++)
        if (areas[i].IsInArea(fpX, fpY))
            return true;
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// --------------- GetUnit/MapObject specific by object identificator -------------------------
// get mapobjectspecific by object identificator
static CMapObjectServer *GetMOByID(int nUnitID)
{
    CMapObject *pMO;
    pMO = theWorldServer.GetMapObject(nUnitID);
    if (!pMO)
        return NULL;
    return (CMapObjectServer *)pMO->GetMapObjectSpecific();
}

static CUnitServer *GetUnitByID(int nUnitID)
{
    CUnit *pUnit;
    pUnit = (CUnit *)theWorldServer.GetMapObject(nUnitID);
    if (!pUnit)
        return NULL;
    if (pUnit->WhoAmI() != HPC_UNIT)
        return NULL;
    return (CUnitServer *)pUnit->GetUnitSpecific();
}

// ------------------------- CScriptObject ---------------------------------
CScriptObject::CScriptObject()
{
    pObject = NULL;
}

void CScriptObject::Clear()
{
    if (pObject)
    {
        pObject->Release();
    }
    pObject = NULL;
    travellingId = -1;
}

void CScriptObject::SetObject(CMapObject *pObj)
{
    Clear();
    if (pObj)
        pObj->AddRef();
    pObject = pObj;
}

void CScriptObject::Copy(const CScriptObject &a)
{
    pObject = a.pObject;
    travellingId = a.travellingId;
    if (pObject)
        pObject->AddRef();
}

// Only explicit party departure can park a reference. Ordinary death/deletion
// keeps the usual invalid-reference behavior and cannot resurrect an actor.
void CScriptObject::PartyTravel(CWorldServer &world, const CParty &party, bool leaving)
{
    if (leaving) {
        if (!pObject) return;
        for (int i=0;i<party.people.GetSize();++i)
            if (party.people[i].nID==pObject->GetID() && !pObject->IsDead()) {
                const int id=pObject->GetID(); Clear(); travellingId=id; return;
            }
    } else if (travellingId>=0) {
        const int id=travellingId;
        bool member=false;
        for (int i=0;i<party.people.GetSize();++i) member|=party.people[i].nID==id;
        SetObject(member ? world.GetUnit(id) : nullptr);
    }
}

/*CMapObject* CScriptObject::GetObject()
{
        CheckObjectRefsValid();
        return pObject;
}*/

CMapObjectServer *CScriptObject::GetSpecific()
{
    // CheckObjectRefsValid();
    if (pObject)
        return (CMapObjectServer *)pObject->GetMapObjectSpecific();
    return NULL;
}

CUnit *CScriptObject::GetUnit()
{
    CUnit *pUnit;
    pUnit = (CUnit *)GetObject();
    if (pUnit)
    {
        if (pUnit->WhoAmI() == HPC_UNIT)
        {
            return pUnit;
        }
    }
    return NULL;
}

CUnitServer *CScriptObject::GetUnitSpecific()
{
    CUnit *pUnit;
    pUnit = GetUnit();
    if (pUnit)
        return (CUnitServer *)pUnit->GetUnitSpecific();
    return NULL;
}

void CScriptObject::CheckObjectValid()
{
    if (pObject && pObject->IsInvalid())
    {
        pObject->Release();
        pObject = NULL;
    }
}





static ObjectId ScriptObjectId(const CMapObject *object)
{
    return object ? object->GetRTID() : 0;
}

//------------------------ CScriptObjectGroup ----------------------------------------------
void CScriptObjectGroup::Add(CScriptObjectGroup &grpA, CScriptObjectGroup &grpB)
{
    // grpA.Sort();
    // grpB.Sort();
    int nA, nB, nRes;
    int nASize, nBSize, nResSize;
    nASize = grpA.GetSize();
    nBSize = grpB.GetSize();
    nResSize = 0;
    members.SetSize(nASize + nBSize);
    nA = 0;
    nB = 0;
    nRes = 0;
    while (nA < nASize && nB < nBSize)
    {
        CMapObject *pA, *pB;
        pA = grpA.members[nA].GetObject();
        pB = grpB.members[nB].GetObject();
        if (pA == pB)
        {
            members[nRes++].SetObject(pA);
            nA++;
            nB++;
            continue;
        }
        if (ScriptObjectId(pA) > ScriptObjectId(pB))
        {
            members[nRes++].SetObject(pB);
            nB++;
        }
        else
        {
            members[nRes++].SetObject(pA);
            nA++;
        }
    }
    while (nA < nASize)
        members[nRes++].SetObject(grpA.members[nA++].GetObject());
    while (nB < nBSize)
        members[nRes++].SetObject(grpB.members[nB++].GetObject());
    members.SetSize(nRes);
}

void CScriptObjectGroup::Sub(CScriptObjectGroup &grpA, CScriptObjectGroup &grpB)
{
    // grpA.Sort();
    // grpB.Sort();
    int nA, nB, nRes;
    int nASize, nBSize, nResSize;
    nASize = grpA.GetSize();
    nBSize = grpB.GetSize();
    nResSize = 0;
    members.SetSize(nASize);
    nA = 0;
    nB = 0;
    nRes = 0;
    while (nA < nASize && nB < nBSize)
    {
        CMapObject *pA, *pB;
        pA = grpA.members[nA].GetObject();
        pB = grpB.members[nB].GetObject();
        if (pA == pB)
        {
            nA++;
            nB++;
            continue;
        }
        if (ScriptObjectId(pA) > ScriptObjectId(pB))
        {
            nB++;
        }
        else
        {
            members[nRes++].SetObject(pA);
            nA++;
        }
    }
    while (nA < nASize)
        members[nRes++].SetObject(grpA.members[nA++].GetObject());
    members.SetSize(nRes);
}

void CScriptObjectGroup::Cross(CScriptObjectGroup &grpA, CScriptObjectGroup &grpB)
{
    // grpA.Sort();
    // grpB.Sort();
    int nA, nB, nRes;
    int nASize, nBSize, nResSize;
    nASize = grpA.GetSize();
    nBSize = grpB.GetSize();
    nResSize = 0;
    members.SetSize(Max(nASize, nBSize));
    nA = 0;
    nB = 0;
    nRes = 0;
    while (nA < nASize && nB < nBSize)
    {
        CMapObject *pA, *pB;
        pA = grpA.members[nA].GetObject();
        pB = grpB.members[nB].GetObject();
        if (pA == pB)
        {
            members[nRes++].SetObject(pA);
            nA++;
            nB++;
            continue;
        }
        if (ScriptObjectId(pA) > ScriptObjectId(pB))
        {
            nB++;
        }
        else
        {
            nA++;
        }
    }
    members.SetSize(nRes);
}

static int __cdecl CmpObjects(const void *ppA, const void *ppB)
{
    // !!! to do this can be optimized, but replacement of qsort() on something will give more
    const auto a = ScriptObjectId(static_cast<const CScriptObject *>(ppA)->GetObject());
    const auto b = ScriptObjectId(static_cast<const CScriptObject *>(ppB)->GetObject());
    return a < b ? -1 : a > b ? 1 : 0;
}

void CScriptObjectGroup::Sort()
{
    qsort(members.GetData(), members.GetSize(), sizeof(CScriptObject), &CmpObjects);
}

int CScriptObjectGroup::GetIndex(CScriptObject &obj)
{
    CMapObject *pMO = obj.GetObject();
    for (int i = 0; i < members.GetSize(); i++)
        if (members[i].GetObject() == pMO)
            return i;
    return -1;
}

void CScriptObjectGroup::Remove(CScriptObject &obj)
{
    int nIdx = GetIndex(obj);
    if (nIdx < 0)
        return;
    members.RemoveAt(nIdx);
}

void CScriptObjectGroup::Remove(CMapObject *pMO)
{
    for (int i = 0; i < members.GetSize(); i++)
        if (members[i].GetObject() == pMO)
        {
            members.RemoveAt(i);
            return;
        }
}

void CScriptObjectGroup::Add(CMapObject *pMO)
{
    /*	if( GetIndex( obj ) >= 0 )
                    return;
            members.Add( obj );*/
    CScriptObject *pData;
    int nDown = 0, nUp = GetSize() - 1, nSize;
    CMapObject *pMOMid;
    if (pMO == NULL)
        return;
    while (nUp - nDown >= 0)
    {
        int nMid = (nUp + nDown) / 2;
        pMOMid = members[nMid].GetObject();
        if (pMO == pMOMid)
            return;
        if (ScriptObjectId(pMO) > ScriptObjectId(pMOMid))
            nDown = nMid + 1;
        else
            nUp = nMid - 1;
    }
    nSize = members.GetSize();

    members.SetSize(nSize + 1);
    pData = members.GetData();
    // CScriptObject contains a native pointer; initialize the whole empty slot.
    if (nSize > nDown)
        memmove(pData + nDown + 1, pData + nDown, sizeof(pData[0]) * (nSize - nDown));

    ::new (pData + nDown) CScriptObject;
    pData[nDown].SetObject(pMO);
}

void CScriptObjectGroup::CheckObjectRefs()
{
    int i;
    for (i = 0; i < members.GetSize();)
    {
        members[i].CheckObjectRefs();
        if (members[i].GetObject() || members[i].TravellingId()>=0)
            i++;
        else
            members.RemoveAt(i);
    }
}

void CScriptObjectGroup::AddList(CMapObjectPtrList &list)
{
    POSITION pos = list.GetHeadPosition();
    while (pos)
        Add(list.GetNext(pos));
}

void CScriptObjectGroup::MakeList(CMapObjectPtrList &list)
{
    list.RemoveAll();
    for (int i = 0; i < members.GetSize(); i++)
        list.AddTail(members[i].GetObject());
}

BOOL CScriptObjectGroup::HasMapObject(CMapObject *pSearch)
{
    for (int i = 0; i < members.GetSize(); i++)
    {
        if (members[i] == pSearch)
            return TRUE;
    }
    return FALSE;
}





// --------------------------- SScriptVariablesSet ---------------------------
void SScriptVariablesSet::SetZeroValues()
{
    int i;
    for (i = 0; i < fVars.GetSize(); i++)
        fVars[i] = 0;
    for (i = 0; i < szVars.GetSize(); i++)
        szVars[i] = "";
    for (i = 0; i < oVars.GetSize(); i++)
        oVars[i].SetObject(NULL);
    for (i = 0; i < gVars.GetSize(); i++)
        gVars[i].Clear();
}

BOOL SScriptVariablesSet::IsVariableName(const char *pszName)
{
    if (fVars.GetVariableIndex(pszName) >= 0)
        return TRUE;
    if (szVars.GetVariableIndex(pszName) >= 0)
        return TRUE;
    if (oVars.GetVariableIndex(pszName) >= 0)
        return TRUE;
    if (gVars.GetVariableIndex(pszName) >= 0)
        return TRUE;
    return FALSE;
}

BOOL SScriptVariablesSet::GetVariable(const char *pszName, int &nNumber,
                                      EScriptParameterType &eType)
{
    eType = ptFloat;
    nNumber = fVars.GetVariableIndex(pszName);
    if (nNumber >= 0)
        return TRUE;
    eType = ptString;
    nNumber = szVars.GetVariableIndex(pszName);
    if (nNumber >= 0)
        return TRUE;
    eType = ptObject;
    nNumber = oVars.GetVariableIndex(pszName);
    if (nNumber >= 0)
        return TRUE;
    eType = ptGroup;
    nNumber = gVars.GetVariableIndex(pszName);
    if (nNumber >= 0)
        return TRUE;
    return FALSE;
}

void SScriptVariablesSet::AddVars(const SScriptVariablesSet &a)
{
    fVars.AddVars(a.fVars);
    szVars.AddVars(a.szVars);
    oVars.AddVars(a.oVars);
    gVars.AddVars(a.gVars);
}

void SScriptVariablesSet::CheckObjectRefs()
{
    int i;
    for (i = 0; i < oVars.GetSize(); i++)
        oVars[i].CheckObjectRefs();
    for (i = 0; i < gVars.GetSize(); i++)
        gVars[i].CheckObjectRefs();
}

void SScriptVariablesSet::CopyValues(const SScriptVariablesSet &src)
{
    fVars.CopyValues(src.fVars);
    szVars.CopyValues(src.szVars);
    oVars.CopyValues(src.oVars);
    gVars.CopyValues(src.gVars);
}





// ------------------------- SSCCmd ------------------------------------------

void SSCCmd::SetParameters(CAArray<SScriptParam, SScriptParam &> &parameters,
                           SScriptVariablesSet &vars)
{
    int i;
    for (i = 0; i < parameters.GetSize(); i++)
    {
        SScriptParam &p = parameters[i];
        switch (p.eType)
        {
        case ptFloat:
            vars.fVars.GetVar(p.nVarNumber, params[p.nNumber]);
            break;
        case ptString:
            vars.szVars.GetVar(p.nVarNumber, szParams[p.nNumber]);
            break;
        case ptObject:
            oParams[p.nNumber] = vars.oVars.GetVar(p.nVarNumber);
            break;
        case ptGroup:
            gParams[p.nNumber] = vars.gVars.GetVar(p.nVarNumber);
            break;
        default:
            ReportError("SSCCmd::SetParameters, unknown parameter type");
        }
    }
}

void SSCCmd::CalcFuncParam(CScript *pScript, SScriptVariablesSet &vars, SScriptFuncParam &funcParam)
{
    switch (funcParam.eType)
    {
    case ptFloat:
        funcParam.cmd->CalcFunction(pScript, vars, params[funcParam.nNumber]);
        break;
    case ptString:
        funcParam.cmd->CalcFunction(pScript, vars, szParams[funcParam.nNumber]);
        break;
    case ptObject:
        funcParam.cmd->CalcFunction(pScript, vars, &oParams[funcParam.nNumber]);
        break;
    case ptGroup:
        funcParam.cmd->CalcFunction(pScript, vars, &gParams[funcParam.nNumber]);
        break;
    default:
        ReportError("SSCCmd::CalcParameters, unknown function parameter type");
    }
}

void SSCCmd::CalcParameters(CScript *pScript, SScriptVariablesSet &vars)
{
    int i;
    // подставим параметры - локальные и глобальные переменные
    // недостаток : каждый сегмент копируютс€ локальные параметры, которые не измен€ютс€ за
    // врем€ жизни скрипта, но дл€ того чтобы из заранее скопировать в команды необходимо
    // иметь копию списка команд условий, в текущей версии список условий ( на который указывает
    // pCondition ) общий дл€ всех запущенных скриптов одного типа, поэтому приходитс€ копировать
    // локальные переменные каждый сегмент
    SetParameters(localParameters, vars);
    SetParameters(globalParameters, pScript->GetGlobalVars());
    // рассчитаем параметры - функции
    for (i = 0; i < funcParameters.GetSize(); i++)
        CalcFuncParam(pScript, vars, funcParameters[i]);
}

SScriptFuncParam *SSCCmd::GetFirstFloatFuncParam()
{
    int i;
    for (i = 0; i < funcParameters.GetSize(); i++)
        if (funcParameters[i].eType == ptFloat)
            return &funcParameters[i];
    return NULL;
}

// static float sqr( float a ) { return a*a; }

void SSCCmd::CalcFunction(CScript *pScript, SScriptVariablesSet &vars, float &res)
{
    int i;
    CUnit *pUnit;
    CPlayer *pPlayer;
    CMapObject *pMO, *pMO1;
    CMapObjectServer *pMOs;
    SScriptFuncParam *pFuncParam;
    EDiplomacy eDiplomacy;

    CalcParameters(pScript, vars);
    if(ModMob::Float(*this,res,pScript))return;
    switch (command)
    {
    case SC_ISEQUAL:
        res = (params[0] == params[1]);
        break;
    case SC_ISLESS:
        res = (params[0] < params[1]);
        break;
    case SC_ISGREATER:
        res = (params[0] > params[1]);
        break;
    case SC_ISEQUALSTRING:
        res = (szParams[0] == szParams[1]);
        break;
    case SC_ADD:
        res = params[0] + params[1];
        break;
    case SC_SUB:
        res = params[0] - params[1];
        break;
    case SC_MUL:
        res = params[0] * params[1];
        break;
    case SC_DIV:
        res = params[0] / params[1];
        break;
    case SC_NOT:
        res = params[0] == 0;
        break;
    case SC_HP:
        pMOs = oParams[0]->GetSpecific();
        if (pMOs)
            res = pMOs->GetCurrentHP();
        else
            res = 0;
        break;
    case SC_MAXHP:
        pMOs = oParams[0]->GetSpecific();
        if (pMOs)
            res = pMOs->GetMaxHP();
        else
            res = 0;
        break;
    case SC_MANA:
        pMOs = oParams[0]->GetSpecific();
        if (pMOs)
            res = pMOs->GetCurrentMP();
        else
            res = 0;
        break;
    case SC_MAXMANA:
        pMOs = oParams[0]->GetSpecific();
        if (pMOs)
            res = pMOs->GetMaxMP();
        else
            res = 0;
        break;
    case SC_GETX:
        pMO = oParams[0]->GetObject();
        if (pMO)
            res = pMO->GetCP().GetX();
        else
            res = 0;
        break;
    case SC_GETY:
        pMO = oParams[0]->GetObject();
        if (pMO)
            res = pMO->GetCP().GetY();
        else
            res = 0;
        break;
    case SC_GETZ:
        pMO = oParams[0]->GetObject();
        if (pMO)
        {
            if (pMO->WhoAmI() == HPC_UNIT)
                res = pMO->GetCP().GetZ() + ((CUnit *)pMO)->CalcZ(pMO->GetCP().x, pMO->GetCP().y);
            else
                res = pMO->GetCP().GetZ() +
                      theWorldServer.GetZValue(pMO->GetCP().x, pMO->GetCP().y);
        }
        else
            res = 0;
        break;
    case SC_GETLOOTITEMSCOUNT:
        res = 0;
        pPlayer = theWorldServer.GetPlayer((int)params[0]);
        if (pPlayer)
            res = pPlayer->items.GetCount();
        break;
    case SC_GETMONEY:
        res = 0;
        pPlayer = theWorldServer.GetPlayer((int)params[0]);
        if (pPlayer)
            res = pPlayer->party.nMoney;
        break;
    case SC_WASLOOTED:
        res = 1;
        pMO = oParams[0]->GetObject();
        if (pMO)
            res = pMO->CheckFlag(UNIT_ALREADY_HOOVER) != 0;
        break;
    case SC_GETFUTUREX:
        pMO = oParams[0]->GetObject();
        if (pMO)
            res = pMO->GetFuturePosition(
                         Float2Int(theWorldServer.GetSegmentTimeCounter() + params[0]))
                      .x;
        else
            res = 0;
        break;
    case SC_GETFUTUREY:
        pMO = oParams[0]->GetObject();
        if (pMO)
            res = pMO->GetFuturePosition(
                         Float2Int(theWorldServer.GetSegmentTimeCounter() + params[0]))
                      .y;
        else
            res = 0;
        break;
    case SC_ISUNITINWATER:
        pMO = oParams[0]->GetObject();
        res = pMO ? theWorldServer.aiMap.IsWater(pMO->GetCP()) : 0;
        break;
    case SC_GROUPSIZE:
        res = (float)gParams[0]->GetSize();
        break;
    case SC_GROUPHAS:
        res = gParams[0]->HasMapObject((CMapObject *)oParams[0][0]) ? 1.0f : 0;
        break;
    case SC_RANDOM:
        res = random.GetFloat(0, params[0]);
        break;
    case SC_GETDIPLOMACY:
        eDiplomacy = theWorldServer.fof.GetDiplomacy((int)params[0], (int)params[1]);
        res = 0;
        if (eDiplomacy == War)
            res = -1;
        if (eDiplomacy == Ally)
            res = 1;
        break;
    case SC_ISALIVE:
        pMOs = oParams[0]->GetSpecific();
        res = 0;
        if (pMOs)
            res = (float)(!pMOs->IsDead());
        break;
    case SC_ISDEAD:
        pMOs = oParams[0]->GetSpecific();
        res = 0;
        if (pMOs)
            res = (float)pMOs->IsDead();
        break;
    case SC_ISENEMY:
        pUnit = oParams[0]->GetUnit();
        res = 0;
        if (pUnit)
            res = (float)pUnit->IsEnemy((int)params[0]);
        break;
    case SC_GETPLAYER:
        pUnit = oParams[0]->GetUnit();
        res = 0;
        if (pUnit)
            res = (float)pUnit->GetOwnerNum();
        break;
    case SC_GETAICLASS:
        pUnit = oParams[0]->GetUnit();
        res = -1;
        if (pUnit)
            res = (float)pUnit->GetAIClass();
        break;
    case SC_GETLEVERSTATE:
        pMO = oParams[0]->GetObject();
        res = 0;
        if (pMO && pMO->WhoAmI() == HPC_LEVER)
            res = ((CLeverObject *)pMO)->GetCurrentState();
        break;
    case SC_GETOBJECTID:
        pMO = oParams[0]->GetObject();
        res = 0;
        if (pMO)
            res = (float)pMO->GetID();
        break;
    case SC_ISALARM:
        res = theWorldServer.IsAlarm(Float2Int(params[0])) ? 1 : 0;
        break;
    case SC_ALARMTIME:
        res = (float)theWorldServer.GetAlarm(Float2Int(params[0])).nTime;
        break;
    case SC_ALARMPOSX:
        res = theWorldServer.GetAlarm(Float2Int(params[0])).fpX;
        break;
    case SC_ALARMPOSY:
        res = theWorldServer.GetAlarm(Float2Int(params[0])).fpY;
        break;
    case SC_DISTANCEUNITUNIT:
        pMO = oParams[0]->GetObject();
        pMO1 = oParams[1]->GetObject();
        if (pMO && pMO1)
        {
            float fpX = pMO->GetCP().x - pMO1->GetCP().x;
            float fpY = pMO->GetCP().y - pMO1->GetCP().y;
            res = sqrt(sqr(fpX) + sqr(fpY));
        }
        else
            res = 1e30f;
        break;
    case SC_DISTANCEUNITPOINT:
        pMO = oParams[0]->GetObject();
        if (pMO)
            res = (float)sqrt(sqr(pMO->GetCP().x - params[0]) + sqr(pMO->GetCP().y - params[1]));
        else
            res = 1e30f;
        break;
    case SC_EVERY:
        ASSERT(globalParameters[0].eType == ptObject);
        ASSERT(globalParameters[0].nNumber == 0);
        pFuncParam = GetFirstFloatFuncParam();
        if (pFuncParam)
        {
            res = 1;
            for (i = 0; i < gParams[0]->GetSize(); i++)
            {
                *oParams[0] = gParams[0]->GetAt(i);
                // pScript->GetGlobalVars().oVars[ globalParameters[0].nVarNumber ] =
                // gParams[0]->members[i];
                CalcFuncParam(pScript, vars, *pFuncParam);
                if (params[0] == 0)
                {
                    res = 0;
                    break;
                }
            }
        }
        else
            res = params[0];
        break;
    case SC_ANY:
        ASSERT(globalParameters[0].eType == ptObject);
        ASSERT(globalParameters[0].nNumber == 0);
        pFuncParam = GetFirstFloatFuncParam();
        if (pFuncParam)
        {
            res = 0;
            for (i = 0; i < gParams[0]->GetSize(); i++)
            {
                *oParams[0] = gParams[0]->GetAt(i);
                // pScript->GetGlobalVars().oVars[ globalParameters[0].nVarNumber ] =
                // gParams[0]->members[i];
                CalcFuncParam(pScript, vars, *pFuncParam);
                if (params[0] != 0)
                {
                    res = 1;
                    break;
                }
            }
        }
        else
            res = params[0] != 0;
        break;
    case SC_SUM:
        ASSERT(globalParameters[0].eType == ptObject);
        ASSERT(globalParameters[0].nNumber == 0);
        pFuncParam = GetFirstFloatFuncParam();
        if (pFuncParam)
        {
            res = 0;
            for (i = 0; i < gParams[0]->GetSize(); i++)
            {
                *oParams[0] = gParams[0]->GetAt(i);
                // pScript->GetGlobalVars().oVars[ globalParameters[0].nVarNumber ] =
                // gParams[0]->members[i];
                CalcFuncParam(pScript, vars, *pFuncParam);
                res = res + params[0];
            }
        }
        else
            res = params[0] * gParams[0]->GetSize();
        break;
    case SC_HAVEITEM:
        res = 0;
        pPlayer = theWorldServer.GetPlayer((int)params[0]);
        if (pPlayer && pPlayer->HasQuestItem((DWORD)params[1]))
            res = 1;
        break;
    case SC_ISPLAYERINDANGER:
        res = 0;
        pPlayer = theWorldServer.GetPlayer((int)params[0]);
        if (pPlayer && ((CPlayerServer *)pPlayer->GetPlayerSpecific())->dangerState == WDS_COMBAT)
            res = 1;
        break;
    case SC_ISPLAYERINSAFETY:
        res = 0;
        pPlayer = theWorldServer.GetPlayer((int)params[0]);
        if (pPlayer && ((CPlayerServer *)pPlayer->GetPlayerSpecific())->dangerState == WDS_NORMAL)
            res = 1;
        break;
    case SC_GETMERCSNUMBER:
        res = theWorldServer.GetMercsNumber((int)params[0]);
        break;
    case SC_GSGETVAR:
        {
            res = 0;
            pPlayer = theWorldServer.GetPlayer((int)params[0]);
            if (pPlayer)
                res = pPlayer->party.vars.GetVar(pScript->SettlementKey(szParams[0]).c_str());
        }
        break;
    case SC_ISCAMERAPLAYING:
        res = IWorldClient.IsCameraPlaying();
        break;
    case SC_ISNIGHT:
        res = theWorldServer.GetNightPower();
        break;
    case SC_GETWORLDTIME:
        res = theWorldServer.GetWorldTime();
        break;
    case SC_ISINSQUARE:
        res = params[0] >= params[2] && params[0] <= params[4] && params[1] >= params[3] &&
              params[1] <= params[5];
        return;
    case SC_UNITINSQUARE:
        pMO = oParams[0]->GetObject();
        res = 0;
        if (pMO)
        {
            float x, y;
            x = pMO->GetCP().x;
            y = pMO->GetCP().y;
            res = x >= params[0] && x <= params[2] && y >= params[1] && y <= params[3];
        }
        return;
    case SC_UNITISVISIBLE:
        pMO = oParams[0]->GetObject();
        res = 0;
        if (pMO)
        {
            pPlayer = theWorldServer.GetPlayer(0);
            ASSERT(pPlayer);
            if (pPlayer)
                res = ((CPlayerServer *)pPlayer->GetPlayerSpecific())
                          ->currentVisible.HasMapObject(pMO);
        }
        return;
    case SC_GETZVALUE:
        res = theWorldServer.GetZValue(params[0], params[1]);
        return;
    case SC_GETBSZVALUE:
        res = theWorldServer.GetBridgeSurfaceZValue(params[0], params[1]);
        break;
    case SC_ISINAREA:
        res = pScript->GetArea(Float2Int(params[0])).IsInArea(params[1], params[2]) ? 1 : 0;
        break;
    case SC_ISBLOCKED:
        pMO = oParams[0]->GetObject();
        res = 0;
        if (pMO)
            res = pMO->CheckFlag(UNIT_BLOCKED) != 0;
        break;
    default:
        ReportError("Function does not return float at SSCCmd::CalcFunction");
        break;
    }
}

void SSCCmd::CalcFunction(CScript *pScript, SScriptVariablesSet &vars, CString &res)
{
    CalcParameters(pScript, vars);
    if(ModMob::String(*this,res,pScript))return;
    /*switch( command )
            {
                    default:*/
    ReportError("Function does not return string at SSCCmd::CalcFunction");
    /*			break;
            }*/
}

void SSCCmd::CalcFunction(CScript *pScript, SScriptVariablesSet &vars, CScriptObject **res)
{
    CMapObject *pMO;
    CPlayer *pPlayer;
    CalcParameters(pScript, vars);
    res[0] = pScript->GetTempObject();
    if(ModMob::Object(*this,*res[0]))return;
    switch (command)
    {
    case SC_GETOBJECT:
        pMO = pScript->NamespaceWorld().GetMapObject(pScript->SettlementObject(std::to_string((int)params[0]).c_str()));
        if (pMO == NULL)
            ReportWarning("Object with ID %d not found in function \"%s\".\n", (int)params[0],
                          "GetObject");
        res[0]->SetObject(pMO);
        break;
    case SC_GETUNITOFPLAYER:
        pPlayer = theWorldServer.GetPlayer(params[0]);
        pMO = 0;
        if (pPlayer)
        {
            int nIdx = Float2Int(params[1]);
            if (nIdx >= 0 && nIdx < pPlayer->party.people.GetSize())
                pMO = theWorldServer.GetUnit(pPlayer->party.people[nIdx].nID);
        }
        res[0]->SetObject(pMO);
        break;
    case SC_GETOBJECTBYNAME:
        pMO = pScript->NamespaceWorld().GetMapObject(pScript->SettlementObject(szParams[0], true));
        if (pMO == NULL)
            ReportWarning("Object with name %s not found in function \"%s\".\n", szParams[0],
                          "GetObjectByName");
        res[0]->SetObject(pMO);
        break;
    case SC_GETOBJECTBYID:
        pMO = pScript->NamespaceWorld().GetMapObject(pScript->SettlementObject(szParams[0]));
        if (pMO == NULL)
            ReportWarning("Object with ID %s not found in function \"%s\".\n", szParams[0],
                          "GetObjectByID");
        res[0]->SetObject(pMO);
        break;
    case SC_GETLEADER:
        {
            int nID = 1000011214;
            pPlayer = theWorldServer.GetPlayer(0);
            if (pPlayer && pPlayer->party.people.GetSize() > 0)
                nID = pPlayer->party.people[0].nID;
            res[0]->SetObject(theWorldServer.GetMapObject(nID));
        }
        break;
    default:
        ReportError("Function does not return object at SSCCmd::CalcFunction");
        break;
    }
}

void SSCCmd::CalcFunction(CScript *pScript, SScriptVariablesSet &vars, CScriptObjectGroup **res)
{
    int i;
    CUnitServer *pUnitServer;
    CUnit *pUnit;
    CPlayer *pPlayer;
    CMapObjectIterator objIterator;
    CalcParameters(pScript, vars);
    res[0] = pScript->GetTempGroup();
    if(ModMob::Group(*this,*res[0]))return;
    switch (command)
    {
    case SC_GROUPADD:
        res[0]->Add(*gParams[0], *gParams[1]);
        break;
    case SC_GROUPSUB:
        res[0]->Sub(*gParams[0], *gParams[1]);
        break;
    case SC_GROUPCROSS:
        res[0]->Cross(*gParams[0], *gParams[1]);
        break;
    case SC_UNITSEE:
        res[0]->Clear();
        pUnitServer = oParams[0]->GetUnitSpecific();
        if (pUnitServer)
            res[0]->AddList(*pUnitServer->logic.GetVisibleList());
        break;
    case SC_GROUPSEE:
        res[0]->Clear();
        for (i = 0; i < gParams[0]->GetSize(); i++)
        {
            pUnitServer = gParams[0]->GetAt(i).GetUnitSpecific();
            if (pUnitServer)
                res[0]->AddList(*pUnitServer->logic.GetVisibleList());
        }
        break;
    case SC_PLAYERSEE:
        pPlayer = theWorldServer.GetPlayer(params[0]);
        if (pPlayer)
        {
            *res[0] = ((CPlayerServer *)pPlayer->GetPlayerSpecific())->addToVisible;
            CUnitPtrHashIterator unitIter;
            CUnit *pUnit = unitIter.GetFirst(&pPlayer->unitsUnderControl);
            for (; pUnit; pUnit = unitIter.GetNext())
            {
                res[0]->Add(pUnit);
                res[0]->AddList(*((CUnitServer *)pUnit->GetUnitSpecific())->logic.GetVisibleList());
            }
        }
        else
            res[0]->Clear();
        break;
    case SC_GETPLAYERUNITS:
        pUnit = (CUnit *)objIterator.GetFirst(
            (CMapObjectManager *)theWorldServer.GetMapObjectManager());
        for (; pUnit; pUnit = (CUnit *)objIterator.GetNext())
        {
            if (pUnit->WhoAmI() != HPC_UNIT)
                continue;
            if (pUnit->GetPlayerNum() == params[0])
                res[0]->Add(pUnit);
        }
        break;
    default:
        ReportError("Function does not return string at SSCCmd::CalcFunction");
        break;
    }
}

void SSCCmd::Clear()
{
    command = SC_UNKNOWN;
    params.SetSize(0);
    szParams.SetSize(0);
    oParams.SetSize(0);
    gParams.SetSize(0);
    localParameters.SetSize(0);
    globalParameters.SetSize(0);
    funcParameters.SetSize(0);
    pLoopBody = NULL;
}





//----------------------- CScriptThreadState ---------------------------------
// **************************************************
//      Script thread commands execution
// **************************************************
void CScriptThreadState::RunThread(SScriptThread *pNewThread, SScriptVariablesSet *newVars)
{
    pThread = pNewThread;
    if (newVars)
        variables.CopyValues(*newVars);
    nCurCommand = 0;
    nSegment = 0;
    nSleepStart = -1;
}

void CScriptThreadState::RunOneSegment(CScript *pScript, SScriptThread *pNewThread)
{
    ModMob::SynchronousScope loopScope;
    int nSaveCurCommand = nCurCommand, nSaveSegment = nSegment, nSaveSleepStart = nSleepStart;
    SScriptThread *pSaveThread = pThread;
    pThread = pNewThread;
    nCurCommand = 0;
    nSegment = 0;
    nSleepStart = -1;

    Segment(pScript);

    pThread = pSaveThread;
    nCurCommand = nSaveCurCommand;
    nSegment = nSaveSegment;
    nSleepStart = nSaveSleepStart;
}

void CScriptThreadState::CheckObjectRefs()
{
    variables.CheckObjectRefs();
}

// relies on local hat and works only for single player
static CParty *GetPartyAndName(CPlayer *pPlayer, CString &szName)
{
    if (pPlayer)
    {
        CString szParty;
        int nPos = szName.Find("::");
        if (nPos >= 0)
        {
            szParty = szName.Mid(0, nPos);
            szName = szName.Mid(nPos + 2, szName.GetLength() - nPos - 2);
        }
        if (szParty == theHat.GetSinglePlayerPartySet().szPartyOnServer)
            return &pPlayer->party;
        else
            return theHat.GetSinglePlayerPartySet().GetParty(szParty);
    }
    return 0;
}

// вернет FALSE, если в скрипте больше нет команд
BOOL CScriptThreadState::Segment(CScript *pScript)
{
    GameJournal::Source journalSource("MQ");
    int i;
    float fpParam;
    CPlot3D objectPos;
    CUnit *pUnit;
    CParty *pParty;
    CMapObject *pMO;
    CPlayer *pPlayer;
    CUMPatrol *pPatrol;
    SAIRallyPoint *pRally;
    CUnitServer *pUnitServer;
    CMapObjectServer *pMOServer;
    SScriptFuncParam *pFuncParam;
    EDiplomacy eDiplomacy;

    ASSERT(pThread);
    ASSERT(pScript);
    CScript &script = *pScript;
    CScript::SettlementScope settlementScope(script, pThread->settlement);
    nSegment++;
    if (IsFinished())
        return FALSE;
    CheckObjectRefs();
    // Perform commands
    BOOL bNextCommand = TRUE;
    while (!IsFinished() && bNextCommand)
    {
        SSCCmd *pCmd = &pThread->commands[nCurCommand];
        pCmd->CalcParameters(pScript, variables);
        bNextCommand = TRUE;
        if(!ModMob::Void(*pCmd,&nCurCommand,pThread->commands.GetSize(),&nSleepStart,&bNextCommand,pScript->CanDeferCommand(),pScript))switch (pCmd->command)
        {
        case SC_WAITSEGMENT:
            if (nSegment < Float2Int(pCmd->params[0]))
                bNextCommand = FALSE;
            break;
        case SC_SLEEP:
            if (nSleepStart < 0)
                nSleepStart = nSegment;
            if ((nSegment - nSleepStart) < Float2Int(pCmd->params[0]))
                bNextCommand = FALSE;
            else
                nSleepStart = -1;
            break;
        case SC_RUNSCRIPT:
            script.RunScript(pCmd);
            break;
        case SC_KILLSCRIPT:
            // команда KillScript может использоватьс€ только в секции then() скрипта и до
            // операторов типа waitxxx
            if (!pLinkedOneScript)
                ReportError("KillScript command not allowed here");
            pLinkedOneScript->nDelFlag = 1; // script is to be erased
            break;
        case SC_FOR:
            ASSERT(pCmd->globalParameters[0].eType == ptObject);
            ASSERT(pCmd->globalParameters[0].nNumber == 0);
            pScript->IncForFlag();
            for (i = pCmd->gParams[0]->GetSize() - 1; i >= 0; i--)
            {
                *pCmd->oParams[0] = pCmd->gParams[0]->GetAt(i);
                // pScript->GetGlobalVars().oVars[ pCmd->globalParameters[0].nVarNumber ] =
                // pCmd->gParams[0]->members[i];
                RunOneSegment(pScript, pCmd->pLoopBody);
            }
            pScript->DecForFlag();
            break;
        case SC_FORIF:
            ASSERT(pCmd->globalParameters[0].eType == ptObject);
            ASSERT(pCmd->globalParameters[0].nNumber == 0);
            pScript->IncForFlag();
            pFuncParam = pCmd->GetFirstFloatFuncParam();
            for (i = pCmd->gParams[0]->GetSize() - 1; i >= 0; i--)
            {
                *pCmd->oParams[0] = pCmd->gParams[0]->GetAt(i);
                // pScript->GetGlobalVars().oVars[ pCmd->globalParameters[0].nVarNumber ] =
                // pCmd->gParams[0]->members[i];
                if (pFuncParam)
                    pCmd->CalcFuncParam(pScript, variables, *pFuncParam);
                if (pCmd->params[0])
                {
                    RunOneSegment(pScript, pCmd->pLoopBody);
                }
            }
            pScript->DecForFlag();
            break;
        case SC_GLOBALASSIGN:
            switch ((int)pCmd->params[1])
            {
            case 0:
                pScript->GetGlobalVars().fVars.SetAt((int)pCmd->params[0], pCmd->params[2]);
                break;
            case 1:
                pScript->GetGlobalVars().szVars.SetAt((int)pCmd->params[0], pCmd->szParams[0]);
                break;
            case 2:
                pScript->GetGlobalVars().oVars.SetAt((int)pCmd->params[0], *pCmd->oParams[0]);
                break;
            case 3:
                pScript->GetGlobalVars().gVars.SetAt((int)pCmd->params[0], *pCmd->gParams[0]);
                break;
            }
            break;
        case SC_ADDOBJECT:
            pCmd->gParams[0]->Add(*pCmd->oParams[0]);
            break;
        case SC_REMOVEOBJECT:
            pCmd->gParams[0]->Remove(*pCmd->oParams[0]);
            break;
        case SC_SLEEPUNTILIDLE:
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pUnit && !((CUnitServer *)pUnit->GetUnitSpecific())->logic.IsIdling())
                bNextCommand = FALSE;
            break;
        case SC_SLEEPUNTIL:
            if (pCmd->params[0] == 0)
                bNextCommand = FALSE;
            break;
        case SC_CREATEPOINTLIGHT:
            script.CreatePointLight(pCmd);
            break;
        case SC_MOVEPOINTLIGHT:
            script.MovePointLight(pCmd);
            break;
        case SC_DELETEPOINTLIGHT:
            script.DeletePointLight(pCmd);
            break;
        case SC_CREATEPARTICLESOURCE:
            script.CreateParticleSource(pCmd);
            break;
        case SC_MOVEPARTICLESOURCE:
            script.MoveParticleSource(pCmd);
            break;
        case SC_DELETEPARTICLESOURCE:
            script.DeleteParticleSource(pCmd);
            break;
        case SC_ATTACHPARTICLESOURCE:
            script.AttachParticleSource(Float2Int(pCmd->params[0]), Float2Int(pCmd->params[1]));
            break;
        case SC_ATTACHPARTICLESOURCE1:
            if (pCmd->oParams[0]->GetObject() != 0)
                script.AttachParticleSource(Float2Int(pCmd->params[0]),
                                            pCmd->oParams[0]->GetObject()->GetID());
            break;
        case SC_SETPARTICLESOURCESIZE:
            script.SetParticleSourceSize(pCmd);
            break;
        case SC_CREATELIGHTNING:
            script.CreateLightning(pCmd);
            break;
        case SC_DELETELIGHTNING:
            script.DeleteLightning(pCmd);
            break;
        case SC_SENDEVENT:
            ((CPlayerManager *)theWorldServer.GetPlayerManager())
                ->SendEvent((int)pCmd->params[0], (DWORD)pCmd->params[1]);
            break;
        case SC_GODMODE:
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
                pPlayer->bGod = (int)pCmd->params[1];
            break;
        case SC_SETPLAYERAGGRESSION:
            /*pPlayer = theWorldServer.GetPlayer( (int)pCmd->params[0] );
            if ( pPlayer )
                    //pPlayer->aggression = pCmd->params[1] != 0 ? PAS_AGGRESSIVE : PAS_STEALTH;
                    */
            break;
        case SC_SENDSTRINGEVENT:
            ((CPlayerManager *)theWorldServer.GetPlayerManager())
                ->SendStringEvent((int)pCmd->params[0], pCmd->szParams[0]);
            break;
        case SC_ACTIVATETRAP:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO)
            {
                if (pCmd->params[0])
                    pMO->SetFlag(TRAP_ACTIVE);
                else
                    pMO->ResetFlag(TRAP_ACTIVE);
            }
            break;
        case SC_SETSCIENCE:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO && pMO->WhoAmI() == HPC_LEVER)
            {
                SRPGScienceStats science;
                science.dwKeyNumber = (int)pCmd->params[0];
                science.dwSkill = (int)pCmd->params[1];
                science.dwTools = 0;
                if (pCmd->params[2])
                    science.dwTools |= RPGTOOL_HAND;
                if (pCmd->params[3])
                    science.dwTools |= RPGTOOL_PICKLOCK;
                if (pCmd->params[4])
                    science.dwTools |= RPGTOOL_CROWBAR;
                if (pCmd->params[5])
                    science.dwTools |= RPGTOOL_KEY;
                ((CLeverObject *)pMO)->SetNewScience(science);
            }
            break;
        case SC_ENABLELEVER:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO && pMO->WhoAmI() == HPC_LEVER)
            {
                ((CLeverObject *)pMO)->EnableLever(pCmd->params[0] != 0);
            }
            break;
        case SC_SETDIPLOMACY:
            fpParam = pCmd->params[2];
            eDiplomacy = fpParam > 0 ? Ally : (fpParam == 0 ? Neutral : War);
            theWorldServer.fof.SetDiplomacy((int)pCmd->params[0], (int)pCmd->params[1], eDiplomacy);
            break;
        case SC_SETPLAYER:
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pUnit)
            {
                pUnit->SetEnemyMask(theWorldServer.fof.GetEnemyMask((int)pCmd->params[0]));
                pUnit->SetPlayerNum((int)pCmd->params[0]);
            }
            break;
        case SC_SETENEMY:
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pUnit)
                pUnit->AddEnemy((int)pCmd->params[0]);
            break;
        case SC_KILLUNIT: {
            pMO = pCmd->oParams[0]->GetObject();
            SRPGAttackPortion attackPortion;
            attackPortion.MakeDeadlyAttack();
            if (pMO)
                pMO->ProcessAttackPortion(&attackPortion, 0, 0);
        }
        break;
        case SC_MOVETO:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.StartMove(CPlot3D(pCmd->params[0], pCmd->params[1], 0));
            break;
        case SC_ROTATETO:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.StartRotate(CPlot3D(pCmd->params[0], pCmd->params[1], 0) -
                                               pUnitServer->GetUnit()->GetCP());
            break;
        case SC_MOVETOOBJECT:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            pMO = pCmd->oParams[1]->GetObject();
            if (pUnitServer && pMO)
            {
                objectPos = pMO->GetCP();
                pUnitServer->logic.StartMove(CPlot3D(objectPos.x, objectPos.y, 0));
                // pUnitServer->CmdMoveTo( CPlot3D( objectPos.x, objectPos.y, 0 ) );
            }
            break;
        case SC_BLOCKUNIT:
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pUnit)
            {
                if (pCmd->params[0] != 0)
                    pUnit->SetFlag(UNIT_BLOCKED);
                else
                    pUnit->ResetFlag(UNIT_BLOCKED);
            }
            break;
        case SC_PLAYANIMATION:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->CmdPlayAnimation(pCmd->szParams[0]);
            break;
        case SC_LIE:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->CmdNewPose(UP_LAY);
            break;
        case SC_CRAWL:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->CmdNewPose(UP_CRAWL);
            break;
        case SC_STAND:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->CmdNewPose(UP_STAND);
            break;
        case SC_RUN:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->CmdNewPose(UP_RUN);
            break;
        case SC_WALK:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->CmdNewPose(UP_STAND);
            break;
        case SC_REST:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->CmdRest();
            break;
        case SC_FOLLOW:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.StartFollow(pCmd->oParams[1]->GetObject());
            break;
        case SC_ATTACK:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer && pCmd->oParams[1]->GetObject())
                pUnitServer->logic.StartAttack(pCmd->oParams[1]->GetObject());
            break;
        case SC_RESETTARGET:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.ResetLogic();
            break;
        case SC_CAST: {
            float x, y, z;
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
            {
                // pUnitServer->logic.SetIdle();
                x = pCmd->params[0];
                y = pCmd->params[1];
                z = theWorldServer.GetZValue(x, y);
                pUnitServer->logic.StartCast(0, CPlot3D(x, y, z), false);
                // pUnitServer->CmdCast( SLTID_FIREBALL, CPlot3D( x, y, z ) );
            }
            break;
        }
        case SC_CASTSPELLPOINT: {
            CSmartPtr<CSpell> pSpell = theWorldServer.GetSpellManager()->CreateSpell(
                pCmd->szParams[0]);
            if (!pSpell.IsEmpty())
            {
                CPlot3D ptSrc(pCmd->params[0], pCmd->params[1], 0);
                theWorldServer.CastSpell(pSpell, 0, ptSrc, 0,
                                         CPlot3D(pCmd->params[2], pCmd->params[3], 0));
            }
        }
        break;
        case SC_CASTSPELLUNIT: {
            CSmartPtr<CSpell> pSpell = theWorldServer.GetSpellManager()->CreateSpell(
                pCmd->szParams[0]);
            if (!pSpell.IsEmpty())
            {
                CPlot3D ptSrc(pCmd->params[0], pCmd->params[1], 0);
                CMapObject *pTarget = pCmd->oParams[0]->GetObject();
                if (pTarget)
                    theWorldServer.CastSpell(pSpell, 0, ptSrc, pTarget, pTarget->GetCP());
            }
        }
        break;
        case SC_IDLE:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.SetIdle();
            break;
        case SC_GUARD:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.SetGuard(CPlot3D(pCmd->params[0], pCmd->params[1], 0),
                                            pCmd->params[2]);
            break;
        case SC_SENTRY:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.SetSentry(CPlot3D(pCmd->params[0], pCmd->params[1], 0));
            break;
        case SC_UM_CLEAR:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.ClearMotivations();
            break;
        case SC_UM_SENTRY:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddSentry(CPlot3D(pCmd->params[0], pCmd->params[1], 0));
            break;
        case SC_UM_GUARD:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddGuard(CPlot3D(pCmd->params[0], pCmd->params[1], 0),
                                            pCmd->params[2]);
            break;
        case SC_UM_GUARDEX:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddGuard(CPlot3D(pCmd->params[0], pCmd->params[1], 0),
                                            pCmd->params[2], pCmd->params[3]);
            break;
        case SC_UM_FOLLOW:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddFollow(pCmd->oParams[1]->GetObject());
            break;
        case SC_UM_PATROL:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddPatrol();
            break;
        case SC_UM_PATROLCLEAR:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
            {
                pPatrol = pUnitServer->logic.GetPatrol();
                if (pPatrol)
                    pPatrol->ClearPatrolRoute();
            }
            break;
        case SC_UM_PATROLADDPOINT:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
            {
                pPatrol = pUnitServer->logic.GetPatrol();
                if (pPatrol)
                    pPatrol->AddPatrolPoint(CPlot3D(pCmd->params[0], pCmd->params[1], 0));
            }
            break;
        case SC_UM_PATROLADDPOINTLOOK:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
            {
                pPatrol = pUnitServer->logic.GetPatrol();
                if (pPatrol)
                {
                    pRally = pPatrol->GetLastPatrolPoint();
                    if (pRally)
                        pRally->AddLookOrder(pCmd->params[0], pCmd->params[1], pCmd->params[2],
                                             false);
                }
            }
            break;
        case SC_UM_PLAYER:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.SetPlayer();
            break;
        case SC_UM_AG:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
            {
                pUnitServer->logic.RemoveMotivation(AI_UM_SUSPECTION);
                pUnitServer->logic.RemoveMotivation(AI_UM_AGGRESSION);
                pUnitServer->logic.RemoveMotivation(AI_UM_CORPSEWATCHER);
                pUnitServer->logic.RemoveMotivation(AI_UM_FEAR);
                switch (Float2Int(pCmd->params[0]))
                {
                case 0:
                    pUnitServer->logic.AddFear(true);
                    pUnitServer->logic.AddSuspection();
                    pUnitServer->logic.AddCorpseWatcher();
                    break;
                case 1:
                    pUnitServer->logic.AddFear(false);
                    pUnitServer->logic.AddSuspection();
                    pUnitServer->logic.AddCorpseWatcher();
                    break;
                case 2:
                    pUnitServer->logic.AddRevenge();
                    pUnitServer->logic.AddFear(false);
                    pUnitServer->logic.AddSuspection();
                    pUnitServer->logic.AddCorpseWatcher();
                    break;
                case 3:
                    pUnitServer->logic.AddAggression();
                    pUnitServer->logic.AddFear(false);
                    pUnitServer->logic.AddSuspection();
                    pUnitServer->logic.AddCorpseWatcher();
                    break;
                }
            }
            break;
        case SC_UM_STANDARD:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
            {
                pUnitServer->logic.AddAggression();
                pUnitServer->logic.AddSuspection();
                pUnitServer->logic.AddCorpseWatcher();
            }
            break;
        case SC_UM_AGGRESSION:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddAggression();
            break;
        case SC_UM_REVENGE:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddRevenge();
            break;
        case SC_UM_FEAR:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddFear(pCmd->params[0] != 0);
            break;
        case SC_UM_SUSPECTION:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddSuspection();
            break;
        case SC_UM_CORPSEWATCHER:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->logic.AddCorpseWatcher();
            break;
        case SC_INFLICTDAMAGE:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
            {
                SRPGAttackPortion attackPortion;
                attackPortion.Clear();
                attackPortion.nSkill = 0;
                attackPortion.fpToHit = 100;
                attackPortion.fpAttacks[RPGAT_ELECTRICAL] = 1;
                attackPortion.hl = RPGHL_BODY;
                attackPortion.fpDamage = pCmd->params[0];
                pUnitServer->ProcessAttackPortion(&attackPortion, 0, 0);
            }
            break;
        case SC_INVOKEALARM:
            theWorldServer.InvokeAlarm((int)pCmd->params[0],
                                       CPlot3D(pCmd->params[1], pCmd->params[2], 0), true);
            break;
        case SC_CREATEFX:
            script.CreateSFX(pCmd);
            break;
        case SC_CREATEFXSOURCE:
        case SC_CREATERANDOMIZEDFXSOURCE:
            script.CreateSFXSource(pCmd);
            break;
        case SC_DELETEFXSOURCE:
            script.DeleteSFXSource(pCmd);
            break;
        case SC_GSSETVAR:
            {
                pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
                if (pPlayer)
                    ((CPlayerServer *)pPlayer->GetPlayerSpecific())
                        ->SetVar(pScript->SettlementKey(pCmd->szParams[0]).c_str(), pCmd->params[1]);
            }
            break;
        case SC_GSSETVARMAX:
            {
                pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
                if (pPlayer)
                    ((CPlayerServer *)pPlayer->GetPlayerSpecific())
                        ->SetVarMax(pScript->SettlementKey(pCmd->szParams[0]).c_str(), pCmd->params[1]);
            }
            break;
        case SC_GSDELVAR:
            {
                pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
                if (pPlayer)
                    ((CPlayerServer *)pPlayer->GetPlayerSpecific())->DelVar(pScript->SettlementKey(pCmd->szParams[0]).c_str());
            }
            break;
        case SC_ADDUNITUNDERCONTROL:
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pPlayer && pUnit)
                pUnit->SetOwner(pPlayer);
            break;
        case SC_REMOVEUNITFROMCONTROL:
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pUnit)
                pUnit->SetOwner(NULL);
            break;
        case SC_ADDUNITTOPARTY: // relies on local hat, single only
            if (script.CurrentSettlement()) {
                Settlement::Name name;
                const std::string value=(LPCSTR)pCmd->szParams[0];
                const auto actor=Settlement::ParseName(value,name) ? value : Settlement::Prefix(script.CurrentSettlement())+value;
                if (!theWorldServer.Settlements().Recruit(theWorldServer,int(pCmd->params[0]),actor))
                    ReportError("Cannot recruit settlement actor: %s",actor.c_str());
                break;
            }
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
            {
                CString szName = pCmd->szParams[0];
                pParty = GetPartyAndName(pPlayer, szName);
                if (pParty)
                    pParty->CreatePersonage(szName, pCmd->szParams[1], "");
            }
            break;
        case SC_REMOVEUNITFROMPARTY: // relies on local hat, single only
            if (script.CurrentSettlement()) {
                Settlement::Name name;
                const std::string value=(LPCSTR)pCmd->szParams[0];
                const auto actor=Settlement::ParseName(value,name) ? value : Settlement::Prefix(script.CurrentSettlement())+value;
                if (!theWorldServer.Settlements().Dismiss(theWorldServer,int(pCmd->params[0]),actor))
                    ReportError("Cannot dismiss settlement actor: %s",actor.c_str());
                break;
            }
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
            {
                CString szName = pCmd->szParams[0];
                pParty = GetPartyAndName(pPlayer, szName);
                if (pParty)
                    pParty->RemovePersonage(GetPersonageIDByName(szName));
            }
            break;
        case SC_CREATEPARTY: // relies on local hat, single only
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
                theHat.GetSinglePlayerPartySet().CreateParty(pCmd->szParams[0]);
            break;
        case SC_REMOVEPARTY: // relies on local hat, single only
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
                theHat.GetSinglePlayerPartySet().RemoveParty(pCmd->szParams[0]);
            break;
        case SC_SETCURRENTPARTY: // relies on local hat, single only
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
                theHat.GetSinglePlayerPartySet().szCurrentParty = pCmd->szParams[0];
            break;
        case SC_ADDLOOT:
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
            {
                CParty *pPartySrc, *pPartyDst;
                pPartySrc = theHat.GetSinglePlayerPartySet().GetParty(pCmd->szParams[0]);
                pPartyDst = theHat.GetSinglePlayerPartySet().GetParty(pCmd->szParams[1]);
                if (pPartySrc && pPartyDst)
                {
                    for (int i = 0; i < pPartySrc->allWagonItems.GetSize(); i++)
                    {
                        CItem *pItem = pPartySrc->allWagonItems[i];
                        pPartyDst->AddItemToWagon(pItem, true);
                    }
                    pPartyDst->nMoney += pPartySrc->nMoney;
                }
                else
                    TRACE0("Script::AddLoot failed");
            }
            break;
        case SC_COPYLOOT:
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
            {
                CParty *pPartySrc, *pPartyDst;
                pPartySrc = theHat.GetSinglePlayerPartySet().GetParty(pCmd->szParams[0]);
                pPartyDst = theHat.GetSinglePlayerPartySet().GetParty(pCmd->szParams[1]);
                if (pPartySrc && pPartyDst)
                {
                    while (pPartyDst->allWagonItems.GetSize())
                    {
                        CItem *pItem = pPartyDst->allWagonItems[0];
                        pPartyDst->RemoveItemFromWagon(pItem, true);
                    }
                    for (int i = 0; i < pPartySrc->allWagonItems.GetSize(); i++)
                    {
                        CItem *pItem = pPartySrc->allWagonItems[i];
                        pPartyDst->AddItemToWagon(pItem, true);
                    }
                    pPartyDst->nMoney = pPartySrc->nMoney;
                }
                else
                    TRACE0("Script::CopyLoot failed");
            }
            break;
        case SC_COPYITEMS:
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
            {
                CParty *pPartySrc, *pPartyDst;
                CPersonage *pSrc, *pDst;
                CString szNameSrc = pCmd->szParams[0], szNameDst = pCmd->szParams[1];
                pPartySrc = GetPartyAndName(pPlayer, szNameSrc);
                pPartyDst = GetPartyAndName(pPlayer, szNameDst);
                pSrc = pPartySrc->GetPersonage(GetPersonageIDByName(szNameSrc));
                pDst = pPartyDst->GetPersonage(GetPersonageIDByName(szNameDst));
                if (pSrc && pDst)
                    pDst->CopyItemsFrom(*pSrc, pPartyDst);
                else
                    TRACE0("Script::CopyItems failed");
            }
            break;
        case SC_COPYSTATS:
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
            {
                CParty *pPartySrc, *pPartyDst;
                CPersonage *pSrc, *pDst;
                CString szNameSrc = pCmd->szParams[0], szNameDst = pCmd->szParams[1];
                pPartySrc = GetPartyAndName(pPlayer, szNameSrc);
                pPartyDst = GetPartyAndName(pPlayer, szNameDst);
                pSrc = pPartySrc->GetPersonage(GetPersonageIDByName(szNameSrc));
                pDst = pPartyDst->GetPersonage(GetPersonageIDByName(szNameDst));
                if (pSrc && pDst)
                {
                    pDst->stats = pSrc->stats;
                    pDst->szName = pSrc->szName;
                }
            }
            break;
        case SC_ADDUNITTOSERVER:
            theWorldServer.AddPersonage(GetPersonageIDByName(pCmd->szParams[0]), pCmd->szParams[1],
                                        pCmd->szParams[2],
                                        CPlot3D(pCmd->params[0], pCmd->params[1], pCmd->params[2]));
            break;
        case SC_REMOVEUNITFROMSERVER:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO)
                theWorldServer.RemoveMapObject(pMO->GetID());
            break;
        case SC_REMOVEOBJECTFROMSERVER:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO)
                theWorldServer.RemoveMapObject(pMO->GetID());
            break;
        case SC_LEAVETOZONE:
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
                ((CPlayerServer *)pPlayer->GetPlayerSpecific())
                    ->LeaveToZone(pCmd->szParams[0], (int)pCmd->params[1]);
            break;
        case SC_REDEPLOYPARTY:
            pPlayer = theWorldServer.GetPlayer((int)pCmd->params[0]);
            if (pPlayer)
            {
                CPlayerServer *pPS = (CPlayerServer *)pPlayer->GetPlayerSpecific();
                SDeployPlayerInfo info;
                theWorldServer.RemovePlayerParty(pPlayer, &info);
                theHat.SetSinglePlayerParty(pPlayer->party);
                theHat.GetSinglePlayerParty(pPlayer->party);
                if (info.units.GetSize() == 1 && pPlayer->party.people.GetSize() == 1)
                    info.units[0].nID = pPlayer->party.people[0].nID;
                pPS->SendParty();
                pPS->GetCameraCP().z = -1313; // он подал знак
                theWorldServer.DeployPlayerParty(pPlayer, &info, false);
            }
            break;
        case SC_SETCP:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO)
            {
                float fpOldZ = pMO->GetCP().z;
                theWorldServer.aiMap.MoveObject(pMO, CPlot3D(pCmd->params[0], pCmd->params[1],
                                                             pCmd->params[2]));
                if (pMO->WhoAmI() == HPC_UNIT)
                {
                    ((CUnitServer *)((CUnit *)pMO)->GetUnitSpecific())->InitializeUnit();
                    if (fpOldZ != pMO->GetCP().z)
                        pMO->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
                }
                else
                {
                    POSITION pos;
                    CList<CUnit *, CUnit *> units;
                    theWorldServer.aiMap.GetUnitsNear(pMO->GetCP(), units, pMO->bv.fpRadius * 2);
                    for (pos = units.GetHeadPosition(); pos;)
                        theWorldServer.aiMap.RemoveObject(units.GetNext(pos));
                    for (pos = units.GetHeadPosition(); pos;)
                    {
                        CPlot3D ptNewPos;
                        CUnit *pUnit = units.GetNext(pos);
                        if (!theWorldServer.aiMap.CanPutUnitRelaxed(pUnit))
                        {
                            ptNewPos = pUnit->GetCP();
                            theWorldServer.aiMap.FindUnitPosition(pUnit, pUnit->GetCP(), ptNewPos);
                            pUnit->SetCP(ptNewPos);
                            ((CUnitServer *)pUnit->GetUnitSpecific())->InitializeUnit();
                        }
                        theWorldServer.aiMap.AddObject(pUnit);
                    }
                    pMO->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
                }
            }
            break;
        case SC_SETCPFAST:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO)
            {
                theWorldServer.aiMap.SetGraphRebuild(false);
                theWorldServer.aiMap.MoveObject(pMO, CPlot3D(pCmd->params[0], pCmd->params[1],
                                                             pCmd->params[2]));
                if (pMO->WhoAmI() == HPC_UNIT)
                    ((CUnitServer *)((CUnit *)pMO)->GetUnitSpecific())->InitializeUnit();
                theWorldServer.aiMap.SetGraphRebuild(true);
                pMO->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
            }
            break;
        case SC_SWITCHLEVERSTATE:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO && pMO->WhoAmI() == HPC_LEVER)
                ((CLeverObject *)pMO)->SwitchState((int)pCmd->params[0]);
            break;
        case SC_SWITCHLEVERSTATEEX:
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO && pMO->WhoAmI() == HPC_LEVER)
                ((CLeverObject *)pMO)->SwitchState((int)pCmd->params[0], pCmd->params[1]);
            break;
        case SC_RECALCMERCBRIEFINGS:
            theWorldServer.RecalcMercBriefings();
            break;
        case SC_GIVESKILL:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
            {
                if (pCmd->szParams[0].CompareNoCase("science") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGManualSkills(RPGMSKL_SCIENCE),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
                if (pCmd->szParams[0].CompareNoCase("melee") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGAutoSkills(RPGASKL_MELEE),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
                if (pCmd->szParams[0].CompareNoCase("archery") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGAutoSkills(RPGASKL_ARCHERY),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
                if (pCmd->szParams[0].CompareNoCase("backstab") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGAutoSkills(RPGASKL_BACKSTAB),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
                if (pCmd->szParams[0].CompareNoCase("elemental") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGAutoSkills(RPGASKL_ELEMENTAL),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
                if (pCmd->szParams[0].CompareNoCase("sense") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGAutoSkills(RPGASKL_SENSE),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
                if (pCmd->szParams[0].CompareNoCase("astral") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGAutoSkills(RPGASKL_ASTRAL),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
                if (pCmd->szParams[0].CompareNoCase("stealth") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGAutoSkills(RPGASKL_STEALTH),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
                if (pCmd->szParams[0].CompareNoCase("awareness") == 0)
                    pUnitServer->GetRPGStats()->PresentSkill(ERPGAutoSkills(RPGASKL_AWARENESS),
                                                             pCmd->params[0],
                                                             pUnitServer->GetUnit());
            }
            break;
        case SC_GIVESTRENGTH:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->GetRPGStats()->PresentStrength(pCmd->params[0],
                                                            pUnitServer->GetUnit());
            break;
        case SC_GIVEDEXTERITY:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->GetRPGStats()->PresentDexterity(pCmd->params[0],
                                                             pUnitServer->GetUnit());
            break;
        case SC_GIVEINTELLIGENCE:
            pUnitServer = pCmd->oParams[0]->GetUnitSpecific();
            if (pUnitServer)
                pUnitServer->GetRPGStats()->PresentIntelligence(pCmd->params[0],
                                                                pUnitServer->GetUnit());
            break;
        case SC_GIVEMONEY:
            pPlayer = theWorldServer.GetPlayer(pCmd->params[0]);
            if (pPlayer)
            {
                CList<CItem *, CItem *> items;
                pPlayer->party.nMoney += (int)pCmd->params[1];
                pPlayer->AddItemsToHoover(items);
            }
            break;
        case SC_GIVEITEM:
        case SC_GIVEQUESTITEM:
            pPlayer = theWorldServer.GetPlayer(pCmd->params[0]);
            if (pPlayer)
            {
                CList<CItem *, CItem *> items;
                CItem *pItem = theWorldServer.CreateItem(pCmd->szParams[0]);
                if (pItem)
                {
                    items.AddTail(pItem);
                    pPlayer->AddItemsToHoover(items);
                }
            }
            break;
        case SC_GIVEUNITQUESTITEM:
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pUnit)
            {
                CItemQuest *pItem = (CItemQuest *)theWorldServer.CreateItem(pCmd->szParams[0]);
                if (pItem && pItem->WhoAmI() == HPC_ITEM_QUEST)
                    pUnit->AddQuestItem(pItem);
            }
            break;
        case SC_GIVEUNITSPELL:
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pUnit)
            {
                CItemSpell *pItem = theWorldServer.CreateItemSpell(pCmd->szParams[0]);
                if (pItem)
                {
                    pUnit->AddQSpell(pItem);
                    ((CUnitServer *)pUnit->GetUnitSpecific())->NotifyItemsUpdated();
                }
            }
            break;
        case SC_ERASEQUESTITEM:
            pPlayer = theWorldServer.GetPlayer(pCmd->params[0]);
            if (pPlayer)
                pPlayer->RemoveQuestItem(pCmd->params[1]);
            break;
        case SC_REMOVEQUESTITEM:
            pUnit = pCmd->oParams[0]->GetUnit();
            if (pUnit)
            {
                int nNum = pUnit->GetQuestItemsNum();
                int nQuestID = pCmd->params[0];
                for (int i = 0; i < nNum; i++)
                {
                    if (pUnit->GetQuestItem(i)->GetScriptID() == nQuestID)
                    {
                        pUnit->RemoveQuestItem(i);
                        break;
                    }
                }
            }
            break;
        case SC_FIXITEMS: {
            CItemManager *pMan = theWorldServer.GetItemManager();
            CItemIterator it;
            for (CItem *pItem = it.GetFirst(pMan); pItem; pItem = it.GetNext())
                pItem->RestoreHP();
        }
        break;
        case SC_QUESTCOMPLETE:
            {
                pPlayer = theWorldServer.GetPlayer(pCmd->params[0]);
                if (pPlayer)
                {
                    const SQDBQuest *pQuest = theQuestDatabase.quests.GetEntry(pCmd->szParams[0]);
                    if (pQuest)
                    {
                        theWorldServer.GainExperience(pPlayer, pQuest->fExp);
                        theHat.UpdateAssortment();
                    }
                }
            }
            break;
        case SC_QSTART:
            StartQuest(pCmd->szParams[0]);
            break;
        case SC_QFINISH:
            FinishQuest(pScript);
            break;
        case SC_QOBJAREA:
            AddSubObj((ESCCmd)pCmd->command, "", pCmd->params[0]);
            break;
        case SC_QOBJSEEOBJECT:
            AddSubObj((ESCCmd)pCmd->command, pCmd->szParams[0], 0);
            break;
        case SC_QOBJSEEUNIT:
            AddSubObj((ESCCmd)pCmd->command, pCmd->szParams[0], 0);
            break;
        case SC_QOBJKILLUNIT:
            AddSubObj((ESCCmd)pCmd->command, pCmd->szParams[0], 0);
            break;
        case SC_QOBJKILLGROUP:
            AddSubObj((ESCCmd)pCmd->command, pCmd->szParams[0], 0);
            break;
        case SC_QOBJUSE:
            AddSubObj((ESCCmd)pCmd->command, pCmd->szParams[0], pCmd->params[0]);
            break;
        case SC_QOBJGETITEM:
            AddSubObj((ESCCmd)pCmd->command, "", pCmd->params[0]);
            break;
        case SC_SETWATERLEVEL:
            theWorldServer.SetWaterLevel(Float2Int(pCmd->params[0]), pCmd->params[1],
                                         Float2Int(pCmd->params[2]));
            break;
        case SC_ADDMOB:
            if (pScript->CurrentSettlement()) {
                if (!pScript->NamespaceWorld().Settlements().HasMob(pScript->CurrentSettlement(), (LPCSTR)pCmd->szParams[0]))
                    throw std::runtime_error("Settlement AddMob requires a declared dependency");
            } else theWorldServer.AddSecondaryMob(pCmd->szParams[0]);
            break;
        case SC_PLAYMUSIC:
            pPlayer = theWorldServer.GetPlayer(pCmd->params[0]);
            if (pPlayer)
                ((CPlayerServer *)pPlayer->GetPlayerSpecific())->PlayMusic(pCmd->szParams[0]);
            break;
        case SC_FIXWORLDTIME:
            theWorldServer.SetWorldTimeScale(0);
            theWorldServer.SetWorldTime(pCmd->params[0]);
            break;
        case SC_RUNWORLDTIME:
            theWorldServer.SetWorldTimeScale(theSettings.nDefaultTimeScale);
            theWorldServer.SetWorldTime(pCmd->params[0]);
            break;
        case SC_SETSPELLAGGRESSION:
            pMOServer = pCmd->oParams[0]->GetSpecific();
            //				if( pMOServer )
            //					pMOServer->spellEffects.SetSpellEffect(
            // PTID_AGGRESSIONFOG, int( pCmd->params[0] ) );
            break;
            /*		case SC_STARTANIMATION:
                                            pUnit->animations.SetNextAnimation(
               pCmd->stringParameters[0], TRUE ); break;*/
            // some client commands
            /*			case SC_SETSUNLIGHT:
                                            CLightManager* pLightManager;
                                            CLight* pSunLight;

                                            pLightManager = ( CLightManager*
               )theWorld.GetLightManager(); pSunLight = pLightManager->GetSunLight();

                                            pSunLight->SetDirectionalLight(
                                                    D3DVECTOR( pCmd->params[3]/255,
               pCmd->params[4]/255, pCmd->params[5]/255 ), D3DVECTOR( pCmd->params[0],
               pCmd->params[1], pCmd->params[2] ) );

                                            D3DCOLORVALUE amb;
                                            amb.r = pCmd->params[6]/255;
                                            amb.g = pCmd->params[7]/255;
                                            amb.b = pCmd->params[8]/255;
                                            amb.a = 1;
                                            pLightManager->SetAmbientLight( amb );
                                            pLightManager->SetSunLight( pSunLight );

                                            break;*/

            /*			case SC_SETCAMERA:
                                            theWorld.GetCamera()->SetCP( CPlot3D( pCmd->params[0],
               pCmd->params[1], pCmd->params[2] ) ); break;*/

            /*		case SC_SCRIPTGROUPCAMERAACTIVATE:
                                            ca.nGroupToActivate = int( pCmd->parameters[0] );
                                            ca.pointToActivate = CPlot3D( pCmd->parameters[1],
               pCmd->parameters[2], pCmd->parameters[3] ); ca.fpRadius2 = pCmd->parameters[4] *
               pCmd->parameters[4]; cameraActivateList.AddTail( ca ); break;*/
        case SC_SETWIND:
            theWorldServer.SetServerWind(CPlot3D(pCmd->params[0], pCmd->params[1], pCmd->params[2]),
                                         pCmd->params[3]);
            break;
        case SC_CONSOLEFLOAT:
            NConsole::game << pCmd->params[0] << endl;
            break;
        case SC_CONSOLESTRING:
            NConsole::game << pCmd->szParams[0] << endl;
            break;
        case SC_SETBG:
            GetRenderer()->SetFogBGColor(pCmd->params[0] / 255.f, pCmd->params[1] / 255.f,
                                         pCmd->params[2] / 255.f);
            break;
        case SC_PLAYCAMERA:
            IWorldClient.StartCameraPlay(pCmd->szParams[0]);
            break;
        case SC_PLAYMOVIE:
            Video::Movie::Play(IWorld.GetCDPath() + "Movies\\" + pCmd->szParams[0]);
            break;
        case SC_HIDEOBJECT:
            ASSERT(theWorldServer.curInterface == IFACE_BASE);
            pMO = pCmd->oParams[0]->GetObject();
            if (pMO)
            {
                if (pCmd->params[0])
                    pMO->SetFlag(MAPOBJ_TRANSPARENT);
                else
                    pMO->ResetFlag(MAPOBJ_TRANSPARENT);
            }
            break;
        case SC_ADDRECTTOAREA: {
            SScriptArea &area = pScript->GetArea(Float2Int(pCmd->params[0]));
            area.AddSquare(pCmd->params[1], pCmd->params[2], pCmd->params[3], pCmd->params[4]);
        }
        break;
        case SC_ADDROUNDTOAREA: {
            SScriptArea &area = pScript->GetArea(Float2Int(pCmd->params[0]));
            area.AddRound(pCmd->params[1], pCmd->params[2], pCmd->params[3]);
        }
        break;
        case SC_DELETEAREA:
            pScript->DeleteArea(Float2Int(pCmd->params[0]));
            break;
        case SC_SHOWCREDITS:
            // CRAP to do
            break;
            /*		case SC_RESTARTZONE:
                                            if ( pPl )
                                                    ((CPlayerClient*)pPl->GetPlayerSpecific())->RestartZoneRequest();
                                            break;*/
            /*			case SC_SHOWBMP:
            #ifdef __INTERFACE2D__
                                            theWorld.GetMainWindow()->ShowBMP( pCmd->szParams[0] );
            #endif // __INTERFACE2D__
                                            Sleep( Float2Int( pCmd->params[0] * 1000.f ) );
                                            break;*/
            /*		case SC_LOADSCRIPTS:
                                            LoadCameraPlacements( theWorld.GetWorkPath() +
            pCmd->stringParameters[0] ); int nTemp; nTemp = nStartSegment; szScriptToLoad =
            theWorld.GetWorkPath() + pCmd->stringParameters[1]; Load( szScriptToLoad );

                                            nStartSegment = nSegmentNum + nTemp + 1;
                                            return;
                                    case SC_SETCAMERAORIENTATION:
                                            SetCameraOrientation( pCmd );
                                            break;
                                    case SC_PLAYSFX:
                                            theWorld.GetSoundManager()->BeginFX(
            pCmd->stringParameters[0], 2, CPlot3D( pCmd->parameters[0], pCmd->parameters[1],
            pCmd->parameters[2] ), pCmd->parameters[3], FALSE ); break;*/
        default:
            assert("Wrong command");
        }
        pScript->ReleaseTemp();
        if (bNextCommand)
            nCurCommand++;
    }
    return TRUE;
}





// -------------------------------- SScriptCondition --------------------------------------------
void SScriptCondition::RunCheck(CScript *pScript, SOneScript *pOneScript)
{
    int i;
    float fCheck;
    ASSERT(pActions);
    ASSERT(pConditions);
    CScript::SettlementScope settlementScope(*pScript, pConditions->settlement);

    for (i = 0; i < pConditions->commands.GetSize(); i++)
    {
        pConditions->commands[i].CalcFunction(pScript, pOneScript->vars, fCheck);
        pScript->ReleaseTemp();
        if (fCheck == 0)
            return;
    }
    GameJournal::Count("SCRIPT", "trigger", pOneScript->szScriptName);
    pScript->RunThread(pActions, pOneScript);
}





// ----------------------------------- COneScript ---------------------------------------------
void SOneScript::SetParameters(SSCCmd &cmd)
{
    int i;
    if (cmd.params.GetSize() - 1 != vars.fVars.GetSize())
    {
        ReportError("Wrong number of script %s float parameters", (const char *)szScriptName);
        return;
    }
    // один дополнительный строковый параметр - им€ скрипта дл€ запуска в команде SC_RUN_SCRIPT
    if (cmd.szParams.GetSize() - 1 != vars.szVars.GetSize())
    {
        ReportError("Wrong number of script %s string parameters", (const char *)szScriptName);
        return;
    }
    if (cmd.oParams.GetSize() != vars.oVars.GetSize())
    {
        ReportError("Wrong number of script %s object parameters", (const char *)szScriptName);
        return;
    }
    if (cmd.gParams.GetSize() != vars.gVars.GetSize())
    {
        ReportError("Wrong number of script %s group parameters", (const char *)szScriptName);
        return;
    }
    for (i = 1; i < cmd.params.GetSize(); i++)
        vars.fVars.SetAt(i - 1, cmd.params[i]);
    for (i = 1; i < cmd.szParams.GetSize(); i++)
        vars.szVars.SetAt(i - 1, cmd.szParams[i]);
    for (i = 0; i < cmd.oParams.GetSize(); i++)
        vars.oVars.SetAt(i, *cmd.oParams[i]);
    for (i = 0; i < cmd.gParams.GetSize(); i++)
        vars.gVars.SetAt(i, *cmd.gParams[i]);
}

BOOL SOneScript::AddParameter(CString &szParam, EScriptParameterType eType)
{
    BOOL bRv;
    CString szType;
    switch (eType)
    {
    case ptFloat:
        bRv = vars.fVars.AddVariable(szParam);
        szType = "f";
        break;
    case ptString:
        bRv = vars.szVars.AddVariable(szParam);
        szType = "s";
        break;
    case ptObject:
        bRv = vars.oVars.AddVariable(szParam);
        szType = "o";
        break;
    case ptGroup:
        bRv = vars.gVars.AddVariable(szParam);
        szType = "g";
        break;
    }
    if (bRv)
        szScriptParams = szScriptParams + szType;
    return bRv;
}

void SOneScript::CheckObjectRefs()
{
    vars.CheckObjectRefs();
}

void SOneScript::Segment(CScript *pScript)
{
    CheckObjectRefs();
    for (int i = 0; i < conditions.GetSize(); i++)
    {
        conditions[i].RunCheck(pScript, this);
        if (nDelFlag)
            return;
    }
}





// ----------------------------------- CScript ------------------------------------------------
// **************************************************
//      Scripts & script threads running functions
// **************************************************

void CScript::RunScript(SSCCmd *pCmd)
{
    GameJournal::Count("SCRIPT", "run", pCmd->szParams[0]);
    int i;
    POSITION pos;
    SOneScript *pNewScript;
    CString &szRunName = pCmd->szParams[0];
    /*	for( i=0; i<scripts.GetSize(); i++ )
            {
                    if( scripts[i].szScriptName == szRunName )
                    {*/
    i = (int)pCmd->params[0];
    pos = runningScripts.AddHead(scripts[i]);
    pNewScript = &runningScripts.GetAt(pos);
    pNewScript->SetParameters(*pCmd);
    pNewScript->RunScript();
    return;
    /*}
}
ReportWarning( "Unknown script %s", (const char*) szRunName );*/
}

BOOL CScript::IsScriptRunning(const char *pszName)
{
    if (CurrentSettlement()) {
        CString scopedName = SignName(pszName);
        for (POSITION scoped = runningScripts.GetHeadPosition(); scoped;)
            if (runningScripts.GetNext(scoped).szScriptName == scopedName) return TRUE;
        return FALSE;
    }
    POSITION pos = runningScripts.GetHeadPosition();
    while (pos)
    {
        if (runningScripts.GetNext(pos).szScriptName == pszName)
            return TRUE;
    }
    CString signedName = SignName(pszName);
    pos = runningScripts.GetHeadPosition();
    while (pos)
    {
        if (runningScripts.GetNext(pos).szScriptName == pszName)
            return TRUE;
    }
    return FALSE;
}

void CScript::RunThread(SScriptThread *pThread, SOneScript *pOneScript)
{
    POSITION pos;
    CScriptThreadState newState;
    if (pOneScript)
        newState.RunThread(pThread, &pOneScript->vars);
    else
        newState.RunThread(pThread);
    newState.LinkOneScript(pOneScript);
    if (newState.Segment(this))
    {
        newState.LinkOneScript(NULL);
        pos = runningThreads.AddHead(newState);
    }
}

void CScript::RunThreadDelayed(SScriptThread *pThread)
{
    CScriptThreadState newState;
    newState.RunThread(pThread);
    runningThreads.AddHead(newState);
}

// **************************************************
//      Script commands implementation
// **************************************************
SScriptArea &CScript::GetArea(int nID)
{
    for (int i = 0; i < scriptAreas.GetSize(); i++)
    {
        if (scriptAreas[i].nID == nID)
            return scriptAreas[i];
    }
    scriptAreas.Add(SScriptArea());
    scriptAreas[scriptAreas.GetSize() - 1].nID = nID;
    return scriptAreas[scriptAreas.GetSize() - 1];
}

void CScript::DeleteArea(int nID)
{
    for (int i = 0; i < scriptAreas.GetSize(); i++)
    {
        if (scriptAreas[i].nID == nID)
            scriptAreas.RemoveAt(i--);
    }
}

void CScript::CreateLightning(SSCCmd *pCmd)
{
    SLightningInfo &info = lightnings.GetAt(lightnings.AddTail(SLightningInfo()));
    if (pCmd->params[0] < 0)
        info.nScriptID = nPrevNegativeParticleSourceID--;
    else
        info.nScriptID = (int)pCmd->params[0];
    info.pLightning = (CEffectLightning *)((CEffectManager *)theWorldServer.GetEffectManager())
                          ->CreateData(ETypeIDs(HPC_EFFECTLIGHTNING));
    info.pLightning->AddRef();

    info.pLightning->SetParams(CPlot3D(pCmd->params[1], pCmd->params[2], pCmd->params[3]),
                               CPlot3D(pCmd->params[4], pCmd->params[5], pCmd->params[6]),
                               pCmd->params[7]);

    theWorldServer.AddEffect(info.pLightning);
}

void CScript::DeleteLightning(SSCCmd *pCmd)
{
    POSITION pos = lightnings.GetHeadPosition(), posCheck;
    while (pos)
    {
        posCheck = pos;
        SLightningInfo &info = lightnings.GetNext(pos);
        if (info.nScriptID == (int)pCmd->params[0])
            lightnings.RemoveAt(posCheck);
    }
}

void CScript::CreateSFX(SSCCmd *pCmd)
{
    CEffectSound *pSound;
    pSound = (CEffectSound *)((CEffectManager *)theWorldServer.GetEffectManager())
                 ->CreateData(ETypeIDs(HPC_EFFECTSOUND));
    pSound->SetCP(CPlot3D(pCmd->params[0], pCmd->params[1], pCmd->params[2]));
    pSound->fpMinRadius = pCmd->params[3];
    pSound->fpMaxRadius = pCmd->params[4];
    pSound->bLooping = 0;
    ASSERT(strlen(pCmd->szParams[0]) < 255);
    strcpy(pSound->szSoundName, pCmd->szParams[0]);
    theWorldServer.AddEffect(pSound);
}

void CScript::CreateSFXSource(SSCCmd *pCmd)
{
    int nID;
    POSITION pos;
    SFXInfo *pNewFX;

    nID = Float2Int(pCmd->params[0]);
    if (nID < 0)
        nID = nPrevNegativeSoundID--;
    pos = sfxes.GetHeadPosition();
    pNewFX = NULL;
    while (pos)
    {
        SFXInfo &FX = sfxes.GetNext(pos);
        if (FX.nScriptID == nID)
        {
            pNewFX = &FX;
            break;
        }
    }
    if (pNewFX)
        pNewFX->Clear();
    else
        pNewFX = &sfxes.GetAt(sfxes.AddTail(SFXInfo()));

    pNewFX->nScriptID = nID;
    pNewFX->cp = CPlot3D(pCmd->params[1], pCmd->params[2], pCmd->params[3]);
    pNewFX->fpMinRadius = pCmd->params[4];
    pNewFX->fpMaxRadius = pCmd->params[5];
    pNewFX->szSoundName = pCmd->szParams[0];
    if (pCmd->command == SC_CREATEFXSOURCE)
    {
        pNewFX->pSound = (CEffectSound *)((CEffectManager *)theWorldServer.GetEffectManager())
                             ->CreateData(ETypeIDs(HPC_EFFECTSOUND));
        pNewFX->pSound->SetCP(pNewFX->cp);
        pNewFX->pSound->fpMinRadius = pNewFX->fpMinRadius;
        pNewFX->pSound->fpMaxRadius = pNewFX->fpMaxRadius;
        ASSERT(pNewFX->szSoundName.GetLength() < 255);
        strcpy(pNewFX->pSound->szSoundName, pNewFX->szSoundName);
        theWorldServer.AddEffect(pNewFX->pSound);
        pNewFX->bIsRandomized = FALSE;
        pNewFX->pSound->AddRef();
    }
    else
    {
        pNewFX->pSound = 0; // hSnd = SND_EMPTY;
        pNewFX->nMinSegment = Float2Int(pCmd->params[6]);
        pNewFX->nMaxSegment = Float2Int(pCmd->params[7]);
        pNewFX->fpRandRadius = pCmd->params[8];
        pNewFX->nDelay = 0;
        pNewFX->bIsRandomized = TRUE;
    }
}

void CScript::DeleteSFXSource(SSCCmd *pCmd)
{
    int nID;
    POSITION pos, oldPos;

    nID = Float2Int(pCmd->params[0]);
    pos = sfxes.GetHeadPosition();
    while (pos)
    {
        oldPos = pos;
        SFXInfo &FX = sfxes.GetNext(pos);
        if (FX.nScriptID == nID)
        {
            sfxes.RemoveAt(oldPos);
            break;
        }
    }
}

void CScript::CreatePointLight(SSCCmd *pCmd)
{
    int nScriptID = Float2Int(pCmd->params[0]);
    int i = 0;
    for (; i < lights.GetSize(); i++)
        if (lights[i].pLight)
        {
            if (lights[i].nScriptID == nScriptID)
            {
                theWorldServer.RemoveEffect(lights[i].pLight->GetID());
                lights[i].pLight->Release();
                break;
            }
        }
        else
            break;
    lights[i].bIsMoving = FALSE;
    lights[i].nScriptID = nScriptID;
    lights[i].bAutoDelete = FALSE;
    lights[i].pLight = (CEffectPointLight *)((CEffectManager *)theWorldServer.GetEffectManager())
                           ->CreateData(ETypeIDs(HPC_EFFECTPOINTLIGHT));
    lights[i].pLight->AddRef();
    lights[i].pLight->color = CPlot3D(pCmd->params[5] / 255, pCmd->params[6] / 255,
                                      pCmd->params[7] / 255);
    lights[i].pLight->SetCP(CPlot3D(pCmd->params[1], pCmd->params[2], pCmd->params[3]));

    // lights[i].pLight->SetAttenuation( D3DVECTOR( pCmd->parameters[8], pCmd->parameters[9],
    // pCmd->parameters[10] ) );
    lights[i].pLight->fpRange = pCmd->params[4];
    theWorldServer.AddEffect(lights[i].pLight);

    lights[i].moveVector = CPlot3D(0, 0, 0);
    lights[i].nSegmentsToMove = 0;
}

void CScript::MovePointLight(SSCCmd *pCmd)
{
    int nScriptID = Float2Int(pCmd->params[0]);
    for (int i = 0; i < lights.GetSize(); i++)
        if (lights[i].pLight && lights[i].nScriptID == nScriptID)
        {
            lights[i].bIsMoving = TRUE;
            lights[i].bAutoDelete = Float2Int(pCmd->params[5]);
            lights[i].nSegmentsToMove = Float2Int(pCmd->params[4]);

            CPlot3D pointToMove = CPlot3D(pCmd->params[1], pCmd->params[2], pCmd->params[3]);
            CPlot3D dist = pointToMove - lights[i].pLight->GetCP();

            lights[i].moveVector = dist * (1 / float(lights[i].nSegmentsToMove));

            return;
        }
}

void CScript::DeletePointLight(SSCCmd *pCmd)
{
    int nScriptID = Float2Int(pCmd->params[0]);
    for (int i = 0; i < lights.GetSize(); i++)
        if (lights[i].pLight && lights[i].nScriptID == nScriptID)
        {
            theWorldServer.RemoveEffect(lights[i].pLight->GetID());
            lights[i].pLight->Release();
            lights[i].pLight = NULL;
            return;
        }
}

struct SParticleNameToID
{
    LPCSTR pszName;
    ETypeIDs tid;
    int nLifeTime;
};
static SParticleNameToID nameToID[] = {
    { "AcidRay", PTID_ACIDRAY, -1 },
    { "FireArrow", PTID_FIREARROW, -1 },
    { "FireBall", PTID_FIREBALL, -1 },
    { "CampFire", PTID_CAMPFIRE, -1 },
    { "FireBlast", PTID_FIREBLAST, 200 },
    { "LightningBlast", PTID_LIGHTNINGBLAST, 200 },
    { "Fire", PTID_FIRE, -1 },
    { "Smoke", PTID_SMOKE, -1 },
    { "VulcanSmoke", PTID_VULCANSMOKE, -1 },
    { "Healing", PTID_HEALING, 200 },
    { "PoisonFog", PTID_POISONFOG, -1 },
    { "Geyser", PTID_GEYSER, -1 },
    { "Tornado", PTID_TORNADO, -1 },
    { "Casting", PTID_CASTING, 200 },
    { "Nuke", PTID_NUKE, 200 },
    { "Mushroom", PTID_MUSHROOM, 200 },
    { "Link", PTID_LINK, -1 },
    { "BlueGas", PTID_BLUEGAS, -1 },
    { "SphereAcid", PTID_SPHEREACID, -1 },
    { "SphereElectricity", PTID_SPHEREELECTRICITY, -1 },
    { "SphereFire", PTID_SPHEREFIRE, -1 },
    { "ClayRing", PTID_CLAYRING, 200 },
    { "Teleport", PTID_TELEPORT, 200 },
    { "Modifier1", PTID_MODIFIER1, 200 },
    { "Modifier2", PTID_MODIFIER2, 200 },
    { "Modifier3", PTID_MODIFIER3, 200 },
    { "Modifier4", PTID_MODIFIER4, 200 },
    { "Modifier5", PTID_MODIFIER5, 200 },
    { "Modifier6", PTID_MODIFIER6, 200 },
    { "Modifier7", PTID_MODIFIER7, 200 },
    { "Modifier8", PTID_MODIFIER8, 200 },
    { "Modifier9", PTID_MODIFIER9, 200 },
    { "Modifier10", PTID_MODIFIER10, 200 },
    { "Modifier11", PTID_MODIFIER11, 200 },
    { "Modifier12", PTID_MODIFIER12, 200 },
    { "Antimagic", PTID_ANTIMAGIC, -1 },
    { "CastingFire", PTID_CASTINGFIRE, -1 },
    { "CastingElectricity", PTID_CASTINGELECTRICITY, -1 },
    { "CastingAcid", PTID_CASTINGACID, -1 },
    { "CastingDivination", PTID_CASTINGDIVINATION, -1 },
    { "CastingIllusion", PTID_CASTINGILLUSION, -1 },
    { "CastingDomination", PTID_CASTINGDOMINATION, -1 },
    { "CastingEnchantment", PTID_CASTINGENCHANTMENT, -1 },
    { "CastingHealing", PTID_CASTINGHEALING, -1 },
    { "ZoneExit", PTID_ZONEEXIT, -1 },
    { "Portal", PTID_PORTAL, -1 },
    { "PortalStar", PTID_PORTALSTAR, -1 },
    { "Cylinder1", PTID_CYLINDER1, 200 },
    { "Cylinder2", PTID_CYLINDER2, 200 },
    { "FireStar", PTID_FIRESTAR, 20 },
    { "AcidStar", PTID_ACIDSTAR, 20 },
    { "Sparks", PTID_SPARKS, 60 },
    { "VisionStar1", PTID_VISIONSTAR1, -1 },
    { "VisionStar2", PTID_VISIONSTAR2, -1 },
    { "VisionStar3", PTID_VISIONSTAR3, -1 },
    { "Regeneration", PTID_REGENERATION, -1 },
    { "Silence", PTID_SILENCE, -1 },
    { "FeebleMind", PTID_FEEBLEMIND, -1 },
    { "FeetCloud1", PTID_FEETCLOUD1, -1 },
    { "FeetCloud2", PTID_FEETCLOUD2, -1 },
    { "CurseStars", PTID_CURSESTARS, -1 },
    { "CurseHolder", PTID_CURSEHOLDER, -1 },
    { "StartTrans", PTID_STARTTRANS, 200 },
    { "Transform", PTID_TRANSFORM, 200 },
};

void CScript::CreateParticleSource(SSCCmd *pCmd)
{
    CEffectParticle *pParticle;
    // Find type id
    ETypeIDs tid = TID_EMPTY;
    int nLifeTime;
    CString effectName = pCmd->szParams[0];
    for (int i = 0; i < sizeof(nameToID) / sizeof(nameToID[0]); i++)
        if (stricmp(nameToID[i].pszName, effectName) == 0)
        {
            tid = nameToID[i].tid;
            nLifeTime = nameToID[i].nLifeTime;
            break;
        }
    if (tid == TID_EMPTY)
        return;
    // Find free slot
    int nScriptID = Float2Int(pCmd->params[0]);
    if (nScriptID < 0)
        nScriptID = nPrevNegativeParticleSourceID--;
    POSITION pos = particles.GetHeadPosition();
    SParticleInfo *pNewInfo = NULL;
    while (pos)
    {
        SParticleInfo &info = particles.GetNext(pos);
        if (info.nScriptID == nScriptID)
        {
            pNewInfo = &info;
            break;
        }
    }
    if (pNewInfo)
        pNewInfo->Clear();
    else
        pNewInfo = &particles.GetAt(particles.AddTail(SParticleInfo()));
    // Create particle system
    pParticle = (CEffectParticle *)((CEffectManager *)theWorldServer.GetEffectManager())
                    ->CreateData(ETypeIDs(HPC_EFFECTPARTICLE));
    pParticle->typeID = tid;
    pParticle->SetCP(CPlot3D(pCmd->params[1], pCmd->params[2], pCmd->params[3]));
    pParticle->AddRef();
    pParticle->fpScale = pCmd->params[4];
    // pParticle->AttachCarrier( NULL );
    theWorldServer.AddEffect(pParticle);
    pNewInfo->pParticle = pParticle;
    pNewInfo->bIsMoving = FALSE;
    pNewInfo->nScriptID = nScriptID;
    pNewInfo->bAutoDelete = FALSE;
    pNewInfo->moveVector = CPlot3D(0, 0, 0);
    pNewInfo->nSegmentsToMove = 0;
    pNewInfo->nLifeTime = nLifeTime;
}

void CScript::MoveParticleSource(SSCCmd *pCmd)
{
    int nScriptID = Float2Int(pCmd->params[0]);
    POSITION pos = particles.GetHeadPosition();
    while (pos)
    {
        SParticleInfo &Info = particles.GetNext(pos);
        if (Info.nScriptID == nScriptID)
        {
            Info.bIsMoving = TRUE;
            Info.bAutoDelete = Float2Int(pCmd->params[5]);
            Info.nSegmentsToMove = Float2Int(pCmd->params[4]);

            CPlot3D pointToMove = CPlot3D(pCmd->params[1], pCmd->params[2], pCmd->params[3]);
            CPlot3D dist = pointToMove -
                           Info.pParticle->GetCP(); // если эффект приаттачен к
                                                    // какому-либо объекту движение будет означать
                                                    // изменение относительного положени эффекта к
                                                    // объекту, к которому он приаттачен

            Info.moveVector = dist * (1 / float(Info.nSegmentsToMove));
            return;
        }
    }
}

void CScript::DeleteParticleSource(SSCCmd *pCmd)
{
    int nScriptID = Float2Int(pCmd->params[0]);
    POSITION pos = particles.GetHeadPosition();
    while (pos)
    {
        POSITION oldPos = pos;
        SParticleInfo &Info = particles.GetNext(pos);
        if (Info.nScriptID == nScriptID)
        {
            particles.RemoveAt(oldPos);
            return;
        }
    }
}

void CScript::AttachParticleSource(int nScriptID, int nObjID)
{
    POSITION pos = particles.GetHeadPosition();
    while (pos)
    {
        POSITION oldPos = pos;
        SParticleInfo &Info = particles.GetNext(pos);
        if (Info.nScriptID == nScriptID)
        {
            Info.pParticle->Attach(nObjID);
            return;
        }
    }
}

void CScript::SetParticleSourceSize(SSCCmd *pCmd)
{
    int nScriptID = Float2Int(pCmd->params[0]);
    POSITION pos = particles.GetHeadPosition();
    while (pos)
    {
        SParticleInfo &Info = particles.GetNext(pos);
        if (Info.nScriptID == nScriptID)
        {
            Info.pParticle->SetScale(pCmd->params[1]);
            return;
        }
    }
}
// **************************************************
//      Clear script
// **************************************************
CScript::CScript()
{
    lights.SetSize(256);
    memset(lights.GetData(), 0, lights.GetSize() * sizeof(lights[0]));
    tempGroups.AddHead(CScriptObjectGroup());
    tempObjects.AddHead(CScriptObject());
    Clear();
}

void CScript::StopExecution()
{
    while (!runningScripts.IsEmpty())
        runningScripts.RemoveHead();
    while (!runningThreads.IsEmpty())
        runningThreads.RemoveHead();
}

void CScript::Clear()
{
    settlementContext = 0;
    if (detachedSaveState) { DiscardSaveState(); return; }
    bLoaded = FALSE;
    nSecondaryCount = 0;
    scriptError.text = "Unknown error";
    scriptError.eError = SC_NO_ERROR;
    scriptError.nLine = 0;

    StopExecution();

    worldActions.RemoveAll();
    globalVars.Clear();

    nForCount = 0;
    ReleaseTemp();

    for (int i = 0; i < lights.GetSize(); i++)
        if (lights[i].pLight)
        {
            theWorldServer.RemoveEffect(lights[i].pLight->GetID());
            lights[i].pLight->Release();
        }
    memset(lights.GetData(), 0, lights.GetSize() * sizeof(lights[0]));

    sfxes.RemoveAll();
    particles.RemoveAll();
    lightnings.RemoveAll();
    scriptAreas.RemoveAll();

    nPrevNegativeParticleSourceID = -2;
    nPrevNegativeSoundID = -2;

    scripts.RemoveAll();
    while (!threads.IsEmpty())
        threads.RemoveHead();
}

void CScript::RestartScript()
{
    ASSERT(bLoaded);

    StopExecution();

    nForCount = 0;
    ReleaseTemp();

    for (int i = 0; i < lights.GetSize(); i++)
        if (lights[i].pLight)
        {
            theWorldServer.RemoveEffect(lights[i].pLight->GetID());
            lights[i].pLight->Release();
        }
    memset(lights.GetData(), 0, lights.GetSize() * sizeof(lights[0]));

    sfxes.RemoveAll();
    particles.RemoveAll();
    lightnings.RemoveAll();
    globalVars.SetZeroValues();

    for (POSITION pos = worldActions.GetHeadPosition(); pos;)
        RunThreadDelayed(&worldActions.GetNext(pos));
    JMV_NpcConsumablesRestart();
}
// -------------------------------- Temprorary objects management ---------------------------
void CScript::ReleaseTemp()
{
    if (nForCount > 0)
        return;
    while (tempGroups.GetCount() > 1)
        tempGroups.RemoveTail();
    tempGroups.GetTail().Clear();
    while (tempObjects.GetCount() > 1)
        tempObjects.RemoveTail();
    tempObjects.GetTail().SetObject(NULL);
}

// -------------------------------- Total script execution ----------------------------------
void CScript::CheckObjectRefs()
{
    POSITION pos;
    // execute all running threads
    for (pos = runningThreads.GetHeadPosition(); pos;)
        runningThreads.GetNext(pos).CheckObjectRefs();
    // execute all scripts checks
    for (pos = runningScripts.GetHeadPosition(); pos;)
        runningScripts.GetNext(pos).CheckObjectRefs();
    globalVars.CheckObjectRefs();
    ReleaseTemp();
}

void CScript::PartyTravel(const CParty &party, bool leaving)
{
    auto object=[&](CScriptObject &value){value.PartyTravel(theWorldServer,party,leaving);};
    auto group=[&](CScriptObjectGroup &value){
        for(int i=0;i<value.members.GetSize();++i)object(value.members[i]);
        if(!leaving)value.CheckObjectRefs();
    };
    auto variables=[&](SScriptVariablesSet &v){
        for(int i=0;i<v.oVars.GetSize();++i)object(v.oVars[i]);
        for(int i=0;i<v.gVars.GetSize();++i)group(v.gVars[i]);
    };
    variables(globalVars);
    for(int i=0;i<scripts.GetSize();++i)variables(scripts[i].vars);
    for(POSITION p=runningScripts.GetHeadPosition();p;)variables(runningScripts.GetNext(p).vars);
    for(POSITION p=runningThreads.GetHeadPosition();p;)variables(runningThreads.GetNext(p).variables);
    for(POSITION p=tempObjects.GetHeadPosition();p;)object(tempObjects.GetNext(p));
    for(POSITION p=tempGroups.GetHeadPosition();p;)group(tempGroups.GetNext(p));
}

void CScript::Segment()
{
    POSITION pos;
    // if script was not loaded it cant be run
    if (!bLoaded)
        return;
    // execute all running threads
    pos = runningThreads.GetHeadPosition();
    while (pos)
    {
        POSITION prevPos = pos;
        CScriptThreadState &thread = runningThreads.GetNext(pos);
        if (!thread.Segment(this))
            runningThreads.RemoveAt(prevPos);
    }
    // execute all scripts checks
    pos = runningScripts.GetHeadPosition();
    while (pos)
    {
        POSITION prevPos = pos;
        SOneScript &s = runningScripts.GetNext(pos);
        s.Segment(this);
        if (s.nDelFlag)
        { // скрипту больше не жить :)
            runningScripts.RemoveAt(prevPos);
        }
    }
    globalVars.CheckObjectRefs();

    // Process SFXes
    pos = sfxes.GetHeadPosition();
    while (pos)
    {
        SFXInfo &FX = sfxes.GetNext(pos);
        if (FX.bIsRandomized)
            continue;
        if (--(FX.nDelay) <= 0)
        {
            FX.nDelay = random.Get(FX.nMinSegment, FX.nMaxSegment);
            FX.Clear();
            FX.pSound = (CEffectSound *)((CEffectManager *)theWorldServer.GetEffectManager())
                            ->CreateData(ETypeIDs(HPC_EFFECTSOUND));
            FX.pSound->SetCP(CPlot3D(FX.cp.x + random.GetFloat(-FX.fpRandRadius, FX.fpRandRadius),
                                     FX.cp.y + random.GetFloat(-FX.fpRandRadius, FX.fpRandRadius),
                                     FX.cp.z));
            FX.pSound->fpMinRadius = FX.fpMinRadius;
            FX.pSound->fpMaxRadius = FX.fpMaxRadius;
            FX.pSound->AddRef();
            ASSERT(FX.szSoundName.GetLength() < 255);
            strcpy(FX.pSound->szSoundName, FX.szSoundName);
            theWorldServer.AddEffect(FX.pSound);
            /*		if ( theWorld.GetSoundManager()->IsFXValid( pFX->hSnd ) == FALSE )
                                    {
                                            CPlot3D cp = CPlot3D(
                                                    pFX->cp.x + random.GetFloat( -pFX->fpRandRadius,
               pFX->fpRandRadius ), pFX->cp.y + random.GetFloat( -pFX->fpRandRadius,
               pFX->fpRandRadius ), pFX->cp.z ); pFX->hSnd = theWorld.GetSoundManager()->BeginFX(
               pFX->szSoundName, 0, cp, pFX->fpHearingRadius, FALSE );
                                    }*/
        }
    }
    // Process lights
    for (int i = 0; i < lights.GetSize(); i++)
        if (lights[i].pLight && lights[i].bIsMoving)
        {
            if (lights[i].nSegmentsToMove-- <= 0)
            {
                lights[i].bIsMoving = FALSE;
                if (lights[i].bAutoDelete)
                {
                    theWorldServer.RemoveEffect(lights[i].pLight->GetID());
                    lights[i].pLight->Release();
                    lights[i].pLight = NULL;
                }
                continue;
            }
            CPlot3D position = lights[i].pLight->GetCP();
            position.x += lights[i].moveVector.x;
            position.y += lights[i].moveVector.y;
            position.z += lights[i].moveVector.z;

            lights[i].pLight->SetCP(position);
            lights[i].pLight->SetUpdateMask();
        }
    // Process particle sources
    pos = particles.GetHeadPosition();
    while (pos)
    {
        BOOL bDelFlag = FALSE;
        POSITION oldPos = pos;
        SParticleInfo &Info = particles.GetNext(pos);
        if (Info.nLifeTime == 0)
            bDelFlag = TRUE;
        if (Info.nLifeTime > 0)
            Info.nLifeTime--;
        if (Info.bIsMoving)
        {
            if (Info.nSegmentsToMove-- <= 0)
            {
                Info.bIsMoving = FALSE;
                if (Info.bAutoDelete)
                    bDelFlag = TRUE;
            }
            else
            {
                Info.pParticle->SetCP(Info.pParticle->GetCP() + Info.moveVector);
                Info.pParticle->SetUpdateMask();
            }
        }
        if (bDelFlag)
            particles.RemoveAt(oldPos);
    }
    ReleaseTemp();
}

// --------------------------------- CScript interface ----------------------------------------
SScriptThread *CScript::AllocateNewThread()
{
    SScriptThread *pRes = &threads.GetAt(threads.AddTail(SScriptThread()));
    pRes->SetNumber(threads.GetCount());
    pRes->settlement = CurrentSettlement();
    return pRes;
}

SScriptThread *CScript::AllocateNewWorldThread()
{
    SScriptThread *pRes = &worldActions.GetAt(worldActions.AddTail(SScriptThread()));
    pRes->SetNumber(worldActions.GetCount() | 0x80000000);
    pRes->settlement = CurrentSettlement();
    return pRes;
}

int CScript::GetRunningScriptIndex(SOneScript *pScript)
{
    POSITION pos = runningScripts.GetHeadPosition();
    int nCount = 0;
    while (pos)
    {
        if (pScript == &runningScripts.GetNext(pos))
            return nCount;
        nCount++;
    }
    return -1;
}

SOneScript *CScript::GetRunningScript(int nNumber)
{
    POSITION pos = runningScripts.GetHeadPosition();
    int nCount = 0;
    while (pos)
    {
        SOneScript *pRes = &runningScripts.GetNext(pos);
        if (nCount == nNumber)
            return pRes;
        nCount++;
    }
    return NULL;
}

SScriptThread *CScript::GetThreadByIndex(int nIdx)
{
    POSITION pos;
    if (nIdx & 0x80000000)
    {
        pos = worldActions.GetHeadPosition();
        while (pos)
        {
            SScriptThread &res = worldActions.GetNext(pos);
            if (res.GetNumber() == nIdx)
                return &res;
        }
    }
    else
    {
        pos = threads.GetHeadPosition();
        while (pos)
        {
            SScriptThread &res = threads.GetNext(pos);
            if (res.GetNumber() == nIdx)
                return &res;
        }
    }
    return NULL;
}

SOneScript *CScript::GetScriptByName(const char *szName)
{
    if (!szName || szName[0] == 0)
        return NULL;
    for (int i = 0; i < scripts.GetSize(); i++)
    {
        if (scripts[i].szScriptName == szName)
            return &scripts[i];
    }
    return NULL;
}

HScriptThread CScript::GetWorldScript()
{
    if (worldActions.GetHeadPosition() == NULL)
        worldActions.AddHead(SScriptThread());
    return &worldActions.GetHead();
}

SOneScript *CScript::GetScript(const char *szName)
{
    for (int i = 0; i < scripts.GetSize(); i++)
    {
        if (scripts[i].szScriptName == szName)
            return &scripts[i];
    }
    return NULL;
}

BOOL CScript::ParseGlobalVars(CString szVars)
{
    SOneScript objScript;
    CLexemeList lexemes;

    objScript.Clear();
    szVars = szVars + "\n)";
    if (!CreateLexemesList(szVars, lexemes))
        return FALSE;
    POSITION pos = lexemes.GetHeadPosition();
    if (!LoadParameters(objScript, pos, lexemes, TRUE))
        return FALSE;
    if (pos != 0)
        return ScriptError(lexemes.GetAt(pos).nLine, SC_UNEXPECTED_SYMBOL,
                           "Symbols behind variables declaration");
    objScript.vars.SetZeroValues(); // this guarantees that execution will start in identical
                                    // environment every time
    globalVars.AddVars(objScript.vars);
    return TRUE;
}

BOOL CScript::CreateOneScript(const char *pszOneScriptName, CString szParams)
{
    SOneScript objScript;
    CLexemeList lexemes;

    objScript.szScriptName = SignName(pszOneScriptName);
    if (GetScriptIdx(objScript.szScriptName) != -1)
        return ScriptError(0, SC_DUPLICATE_SCRIPT, "Duplicate script %s", pszOneScriptName);
    szParams = szParams + "\n)";
    if (!CreateLexemesList(szParams, lexemes))
        return FALSE;
    POSITION pos = lexemes.GetHeadPosition();
    if (!LoadParameters(objScript, pos, lexemes))
        return FALSE;
    scripts.Add(objScript);
    return TRUE;
}

BOOL CScript::CreateCondition(const char *pszOneScriptName, HScriptThread &pIfThread,
                              HScriptThread &pThenThread)
{
    SOneScript *pOneScript;
    SScriptCondition condition;
    pOneScript = GetScript(SignName(pszOneScriptName));
    if (!pOneScript)
        return FALSE;
    // allocate new thread in list of all threads and load conditions in it
    condition.pConditions = AllocateNewThread();
    pIfThread = condition.pConditions;
    // allocate new thread in list of all threads and load commands in it
    condition.pActions = AllocateNewThread();
    pThenThread = condition.pActions;
    pOneScript->conditions.Add(condition);
    return TRUE;
}

BOOL CScript::ParseIfThread(const char *pszOneScriptName, HScriptThread pThread, CString szSrc)
{
    CLexemeList lexemes;
    SOneScript *pOneScript;

    pOneScript = GetScript(SignName(pszOneScriptName));
    if (!pOneScript)
        return FALSE;
    szSrc = szSrc + "\n)";
    if (!CreateLexemesList(szSrc, lexemes))
        return FALSE;
    POSITION pos = lexemes.GetHeadPosition();
    if (!LoadConditionsList(*pThread, pos, lexemes, pOneScript->vars))
        return FALSE;
    if (pos != 0)
        return ScriptError(lexemes.GetAt(pos).nLine, SC_UNEXPECTED_SYMBOL,
                           "Symbols behind conditions list");
    return TRUE;
}

BOOL CScript::ParseCommandThread(const char *pszOneScriptName, HScriptThread pThread, CString szSrc)
{
    CLexemeList lexemes;
    SOneScript *pOneScript = NULL;

    if (pszOneScriptName)
    {
        pOneScript = GetScript(SignName(pszOneScriptName));
        if (!pOneScript)
            return FALSE;
    }
    szSrc = szSrc + "\n)";
    if (!CreateLexemesList(szSrc, lexemes))
        return FALSE;
    POSITION pos = lexemes.GetHeadPosition();
    if (pOneScript)
    {
        if (!LoadCommandsList(*pThread, pos, lexemes, &pOneScript->vars))
            return FALSE;
    }
    else
    {
        if (!LoadCommandsList(*pThread, pos, lexemes, NULL))
            return FALSE;
    }
    if (pos != 0)
        return ScriptError(lexemes.GetAt(pos).nLine, SC_UNEXPECTED_SYMBOL,
                           "Symbols behind commands list");
    return TRUE;
}

// -------------------- CScript load&store ---------------------------------------



