////// MapEditObject.cpp: implementation file ////
//												//
//	started             16/11/98				//
//												//
//	last time revised   22/01/99				//
//////////////////////////////////////////////////
#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////

#include "MapEditObject.h"
#include "MapEditorChunkIDs.h"

#include "WorldServer.h"
#include "WorldClient.h"
#include "Object3DClientSpecific.h"
#include "EffectManager.h"
#include "Terrain.h"
#include "Effect.h"
#include "UnitServer.h"

// Вычесляет id клиентской визуализации
inline int GetVisualisationID(int nInst)
{
    return N_ID_EFFECT_MIRROR - nInst;
}

// Вычесляет id еффекта по клиентской визуализации
inline int GetEffectID(int nID)
{
    return -nID + N_ID_EFFECT_MIRROR;
}

ETypeIDs GetIDByType(CString szType)
{
    szType.MakeLower();
    if (szType == "trap")
        return MOTID_TRAP;

    if (szType == "lever")
        return MOTID_LEVER;

    if (szType == "torch")
        return MOTID_TORCH;

    if (szType == "human")
        return MOTID_HUMAN;

    if (szType == "monster")
        return MOTID_MONSTER;

    if (szType == "animal")
        return MOTID_ANIMAL;

    if (szType == "flora")
        return MOTID_TREE;

    if (szType == "tree")
        return MOTID_TREE;

    if (szType == "structure")
        return MOTID_BUILDING;

    if (szType == "building")
        return MOTID_BUILDING;

    if (szType == "stone")
        return MOTID_STONE;

    if (szType == "object")
        return MOTID_OBJECT;

    if (szType == "unit")
        return MOTID_UNIT;

    return MOTID_OBJECT;
}
CString GetTypeByID(ETypeIDs tidTypeID)
{
    switch (tidTypeID)
    {
    case MOTID_TRAP:
        return CString("trap");
        break;

    case MOTID_LEVER:
        return CString("lever");
        break;

    case MOTID_TORCH:
        return CString("torch");
        break;

    case MOTID_HUMAN:
        return CString("human");
        break;

    case MOTID_MONSTER:
        return CString("monster");
        break;

    case MOTID_ANIMAL:
        return CString("animal");
        break;

    case MOTID_TREE:
        return CString("tree");
        break;

    case MOTID_BUILDING:
        return CString("building");
        break;

    case MOTID_STONE:
        return CString("stone");
        break;

    case MOTID_OBJECT:
        return CString("object");
        break;

    case MOTID_UNIT:
        return CString("unit");
        break;

    default:
        return CString(""); // TID_EMPTY;
    }
}

//---------------------------------------------------------------------------------
SObjectProTemplate::SObjectProTemplate()
{
    const CString szEmpty = ("none");

    szUnitPrototype = szEmpty;
    szDescriptor = szEmpty;
    szName = szEmpty;
    szTemplate = szEmpty;
    tidTypeID = TID_EMPTY;
    nMaxHP = 0;
    szPrimTextures.SetSize(0);
    szSecTextures.SetSize(0);
    szBodyParts.SetSize(0);
    szAnimationSet.SetSize(0);

    complection.grace = 0.5f;
    complection.strength = 0.5f;
    complection.tall = 0.5f;
}

SObjectProTemplate::~SObjectProTemplate()
{
}

void SObjectProTemplate::operator=(const SObjectProTemplate &protempl)
{
    VERIFY(this != &protempl);
    szDescriptor = protempl.szDescriptor;
    szName = protempl.szName;
    szTemplate = protempl.szTemplate;
    szUnitPrototype = protempl.szUnitPrototype;
    tidTypeID = protempl.tidTypeID;
    complection = protempl.complection;
    nMaxHP = protempl.nMaxHP;

    int nPT = protempl.szPrimTextures.GetSize();
    int nST = protempl.szSecTextures.GetSize();
    int nAS = protempl.szAnimationSet.GetSize();
    szPrimTextures.SetSize(nPT);
    szSecTextures.SetSize(nST);
    szAnimationSet.SetSize(nST);

    for (int i = 0; i < nPT; i++)
        szPrimTextures[i] = protempl.szPrimTextures[i];
    for (int j = 0; j < nST; j++)
        szSecTextures[j] = protempl.szSecTextures[j];
    for (int j = 0; j < nAS; j++)
        szAnimationSet[j] = protempl.szAnimationSet[j];

    int nBP = protempl.szBodyParts.GetSize();
    for (int k = 0; k < nBP; k++)
        szBodyParts[k] = protempl.szBodyParts[k];
}

//---------------------------------------------------------------------------------
SObjectTemplate::SObjectTemplate()
{
    const CString szEmpty = ("none");

    szDescriptor = szEmpty;
    szName = szEmpty;
    szTemplate = szEmpty;
    szUnitPrototype = szEmpty;
    tidTypeID = TID_EMPTY;
    szPrimTexture = szEmpty;
    szSecTexture = szEmpty;
    szBodyParts.SetSize(0);

    complection.grace = 0.5f;
    complection.strength = 0.5f;
    complection.tall = 0.5f;
    nMaxHP = 0;
}

SObjectTemplate::~SObjectTemplate()
{
}

void SObjectTemplate::operator=(const SObjectTemplate &fakeobj)
{
    VERIFY(this != &fakeobj);

    nID = fakeobj.nID;
    szDescriptor = fakeobj.szDescriptor;
    szName = fakeobj.szName;
    szTemplate = fakeobj.szTemplate;
    szParentTemplate = fakeobj.szParentTemplate;
    szUnitPrototype = fakeobj.szUnitPrototype;
    tidTypeID = fakeobj.tidTypeID;

    if (fakeobj.szPrimTexture.CompareNoCase("none"))
        szPrimTexture = fakeobj.szPrimTexture;

    if (fakeobj.szSecTexture.CompareNoCase("none"))
        szSecTexture = fakeobj.szSecTexture;

    int nBP = fakeobj.szBodyParts.GetSize();
    szBodyParts.SetSize(nBP);

    for (int k = 0; k < nBP; k++)
        szBodyParts[k] = fakeobj.szBodyParts[k];

    complection = fakeobj.complection;
}

void SObjectTemplate::Reset()
{
    const CString szEmpty = ("none");

    szDescriptor = szEmpty;
    szName = szEmpty;
    szTemplate = szEmpty;
    szUnitPrototype = szEmpty;
    tidTypeID = TID_EMPTY;
    szPrimTexture = szEmpty;
    szSecTexture = szEmpty;
    szBodyParts.SetSize(0);

    complection.grace = 0.5f;
    complection.strength = 0.5f;
    complection.tall = 0.5f;
    nMaxHP = 0;
}

//---------------------------------------------------------------------------------
CMEBasicObject::CMEBasicObject()
{
    const CString szEmpty = ("none");

    m_eType = MEOT_UNKNOW;

    p_lpMapObject = NULL;
    p_nID = -1;
    p_nMobNumber = -1;

    p_ptPosition = CPlot3D(0.f, 0.f, 0.f);
    p_qRotation = CQuaternion();

    p_szName = szEmpty;
    p_szComments = szEmpty;
}

void CMEBasicObject::operator=(const CMEBasicObject &obj)
{
    p_lpMapObject = obj.p_lpMapObject;
    p_nID = obj.p_nID;
    p_nMobNumber = obj.p_nMobNumber;

    p_ptPosition = obj.p_ptPosition;
    p_qRotation = obj.p_qRotation;

    p_szName = obj.p_szName;
    p_szComments = obj.p_szComments;
}

LPMAPOBJECT CMEBasicObject::GetClientObject()
{
    return (LPMAPOBJECT)theWorld.GetMapObjectManager()->Find(p_nID);
}

void CMEBasicObject::MoveOn(CPlot3D inc)
{
    p_ptPosition += inc;
    UpdateObject();
}

void CMEBasicObject::GetXYBV(CPoint *aptVertexs)
{
    CPlot3D ptTemp, ptBase;
    ptBase = p_lpMapObject->bv.ptMax;

    // 0
    ptTemp = p_qRotation.Rotate(ptBase) + p_lpMapObject->bv.ptCenter + p_lpMapObject->GetCP();
    ptTemp *= 100;
    aptVertexs[0] = CPoint(int(ptTemp.x), int(ptTemp.y));

    // 1
    ptBase.x = -ptBase.x;
    ptTemp = p_qRotation.Rotate(ptBase) + p_lpMapObject->bv.ptCenter + p_lpMapObject->GetCP();
    ptTemp *= 100;
    aptVertexs[1] = CPoint(int(ptTemp.x), int(ptTemp.y));

    // 2
    ptBase.y = -ptBase.y;
    ptTemp = p_qRotation.Rotate(ptBase) + p_lpMapObject->bv.ptCenter + p_lpMapObject->GetCP();
    ptTemp *= 100;
    aptVertexs[2] = CPoint(int(ptTemp.x), int(ptTemp.y));

    // 3
    ptBase.x = -ptBase.x;
    ptTemp = p_qRotation.Rotate(ptBase) + p_lpMapObject->bv.ptCenter + p_lpMapObject->GetCP();
    ptTemp *= 100;
    aptVertexs[3] = CPoint(int(ptTemp.x), int(ptTemp.y));
}

//---------------------------------------------------------------------------------
CMEObject::CMEObject()
    : CMEBasicObject()
{
    CString szEmpty = ("none");

    m_eType = MEOT_OBJECT;

    p_tidTypeID = TID_EMPTY;

    p_szQuestInfo = "";
    p_szTemplate = szEmpty;
    p_szParentTemplate = szEmpty;
    p_nParentTemplate = -1;

    p_Complection.grace = 0.5f;
    p_Complection.strength = 0.5f;
    p_Complection.tall = 0.5f;

    p_isUseInScript = false;
    p_isShadow = true;
    p_nPlayer = 0;

    p_szPrimTexture = szEmpty;
    p_szSecTexture = szEmpty;

    p_aBodyParts.SetSize(0);
}

void CMEObject::operator=(const CMEObject &obj)
{
    *((CMEBasicObject *)this) = (CMEBasicObject)obj;

    p_tidTypeID = obj.p_tidTypeID;

    p_szQuestInfo = obj.p_szQuestInfo;
    p_szTemplate = obj.p_szTemplate;
    p_szParentTemplate = obj.p_szParentTemplate;
    p_nParentTemplate = obj.p_nParentTemplate;

    p_Complection = obj.p_Complection;

    p_isUseInScript = obj.p_isUseInScript;
    p_isShadow = obj.p_isShadow;
    p_nPlayer = obj.p_nPlayer;

    p_szPrimTexture = obj.p_szPrimTexture;
    p_szSecTexture = obj.p_szSecTexture;

    p_aBodyParts.RemoveAll();
    p_aBodyParts.Copy(obj.p_aBodyParts);
}

void CMEObject::Save()
{
    BeginChunk(ID_OBJECT);
    WriteData();
    EndChunk();
}

void CMEObject::WriteData()
{
    WriteStringArray(ID_OBJBODYPARTS, p_aBodyParts);
    WriteByte(ID_OBJ_PLAYER, p_nPlayer);
    WriteDWord(ID_NID, p_nID);
    WriteDWord(ID_OBJTYPE, p_tidTypeID);
    WriteString(ID_OBJNAME, p_szName);
    WriteString(ID_OBJTEMPLATE, p_szTemplate);
    WriteString(ID_PARENTTEMPLATE, p_szParentTemplate);

    WriteString(ID_OBJPRIMTXTR, p_szPrimTexture);
    WriteString(ID_OBJSECTXTR, p_szSecTexture);
    WriteString(ID_OBJCOMMENTS, p_szComments);

    WritePlot(ID_OBJPOSITION, p_ptPosition);
    WriteQuaternion(ID_OBJROTATION, p_qRotation);
    WriteByte(ID_OBJ_USE_IN_SCRIPT, p_isUseInScript);
    WriteByte(ID_OBJ_IS_SHADOW, p_isShadow);
    WriteDWord(ID_OBJ_PARENT_ID, p_nParentTemplate);
    WriteString(ID_OBJ_QUEST_INFO, p_szQuestInfo);

    WriteHeader(ID_OBJCOMPLECTION, sizeof(float) * 3);
    GetFile()->Write(&p_Complection.grace, sizeof(float));
    GetFile()->Write(&p_Complection.strength, sizeof(float));
    GetFile()->Write(&p_Complection.tall, sizeof(float));
}

void CMEObject::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_OBJ_USE_IN_SCRIPT:
        p_isUseInScript = ReadAsByte() == 1;
        break;

    case ID_OBJ_IS_SHADOW:
        p_isShadow = ReadAsByte() == 1;
        break;

    case ID_OBJ_PARENT_ID:
        p_nParentTemplate = (int)ReadAsDWord();
        break;

    case ID_NID: // object's unique ID within a map
        p_nID = (int)ReadAsDWord();
        break;

    case ID_OBJTYPE: // object type
        p_tidTypeID = (ETypeIDs)ReadAsDWord();
        break;

    case ID_OBJ_PLAYER: // object player
        p_nPlayer = ReadAsByte();
        break;

    case ID_OBJ_QUEST_INFO: // name
        p_szQuestInfo = ReadAsString();
        break;

    case ID_OBJNAME: // name
        p_szName = ReadAsString();
        break;

    case ID_OBJTEMPLATE: // template
        p_szTemplate = ReadAsString();
        break;

    case ID_PARENTTEMPLATE: // Parent template name
        p_szParentTemplate = ReadAsString();
        break;

    case ID_OBJCOMMENTS: // Comments
        p_szComments = ReadAsString();
        break;

    case ID_OBJPRIMTXTR: // primary textures
        p_szPrimTexture = ReadAsString();
        break;

    case ID_OBJSECTXTR: // secondary textures
        p_szSecTexture = ReadAsString();
        break;

    case ID_OBJPOSITION: // position
        p_ptPosition = ReadAsPlot();
        break;

    case ID_OBJROTATION: // rotation
        p_qRotation = ReadAsQuaternion();
        break;

    case ID_OBJCOMPLECTION: // complection
        p_Complection.grace = ReadAsFloat();
        p_Complection.strength = ReadAsFloat();
        p_Complection.tall = ReadAsFloat();
        break;

    case ID_OBJBODYPARTS: // bodyparts
        ReadAsStringArray(p_aBodyParts);
        break;
    }
}

CMEObject::~CMEObject()
{
#ifdef __MAP_EDITOR__
    if (p_lpMapObject)
    {
        theWorldServer.RemoveMapObject(p_nID);
        p_lpMapObject = NULL;
    }
#endif //__MAP_EDITOR__
}

void CMEObject::RestateObject()
{
    if (p_lpMapObject)
    {
        theWorldServer.RemoveMapObject(p_nID);
        p_lpMapObject = NULL;
    }
    UpdateObject();
}

void CMEObject::UpdatePosition()
{
    if (p_lpMapObject)
        p_lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
}

void CMEObject::UpdateObject()
{
    if (p_lpMapObject == NULL)
    {
        theWorldServer.AddMapObject(this);
    }
    else
    {
        // Update existing object
        theWorldServer.aiMap.StartDelayedConvert();

        theWorldServer.aiMap.RemoveObject(p_lpMapObject);
        p_lpMapObject->SetCP(p_ptPosition);
        p_lpMapObject->SetOrientation(p_qRotation);
        p_lpMapObject->SetComplection(p_Complection);
        p_lpMapObject->SetBaseComplection(p_Complection);
        p_lpMapObject->RestoreComplection();

        if (!p_isShadow)
            p_lpMapObject->SetFlag(OBJ_NOSHADOW);
        else
            p_lpMapObject->ResetFlag(OBJ_NOSHADOW);

        p_lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_POSROT);
        p_lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_COMPLECTION);

        theWorldServer.aiMap.AddObject(p_lpMapObject);
        theWorldServer.aiMap.FinishDelayedConvert();
    }
}

CString CMEObject::GetParentTemplate()
{
    CString szID;
    szID.Format("%i", p_nParentTemplate);
    return szID;
}

//-------------------------------------------------------------------------------------------------------
CMETorch::~CMETorch()
{
}

CMETorch::CMETorch()
    : CMEObject()
{
    m_eType = MEOT_TORCH;

    p_ptLink = CPlot3D(0, 0, 0);
    p_fStrenght = 0.7f;
    p_szSound = "nature\\fire\\small.wav";
}

void CMETorch::operator=(const CMETorch &obj)
{
    *((CMEObject *)this) = obj;

    p_ptLink = obj.p_ptLink;
    p_fStrenght = obj.p_fStrenght;
    p_szSound = obj.p_szSound;
}

void CMETorch::UpdateObject()
{
    if (p_lpMapObject != NULL)
    {
        theWorldServer.RemoveMapObject(p_nID);
        p_lpMapObject = NULL;
    }
    theWorldServer.AddMapObject(this);
}

void CMETorch::Save()
{
    BeginChunk(ID_TORCH);

    WritePlot(ID_TORCH_PTLINK, p_ptLink);
    WriteFloat(ID_TORCH_STRENGHT, p_fStrenght);
    WriteString(ID_TORCH_SOUND, p_szSound);

    // Save MEObject data
    CMEObject::WriteData();

    EndChunk();
}

void CMETorch::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_TORCH_SOUND:
        p_szSound = ReadAsString();
        break;

    case ID_TORCH_STRENGHT:
        p_fStrenght = ReadAsFloat();
        break;

    case ID_TORCH_PTLINK:
        p_ptLink = ReadAsPlot();
        break;

    default:
        // Попробовать прочитать как свойства CMEObject
        CMEObject::ReadSubChunk();
        break;
    }
}

//-------------------------------------------------------------------------------------------------------
CMEMagicTrap::CMEMagicTrap()
{
    m_eType = MEOT_MAGIC_TRAP;
}

void CMEMagicTrap::Save()
{
    BeginChunk(ID_MAGIC_TRAP);

    WriteDWord(ID_MT_DIPLOMACY, p_Specific.dwDiplomacy);
    WriteString(ID_MT_SPELL, p_Specific.szSpell);
    WriteDWord(ID_MT_CAST_INTERVAL, p_Specific.nCastingInterval);
    WriteByte(ID_LEVER_CAST_ONCE, p_Specific.bCastOnce);
    WriteArray(ID_MT_AREAS, p_Specific.aAreas);
    WriteArray(ID_MT_TARGETS, p_Specific.aTargets);

    // Save CMEObject propites
    CMEObject::WriteData();

    EndChunk();
}

void CMEMagicTrap::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_LEVER_CAST_ONCE:
        p_Specific.bCastOnce = ReadAsByte() == 1;
        break;

    case ID_MT_DIPLOMACY:
        p_Specific.dwDiplomacy = ReadAsDWord();
        break;

    case ID_MT_SPELL:
        p_Specific.szSpell = ReadAsString();
        break;

    case ID_MT_CAST_INTERVAL:
        p_Specific.nCastingInterval = ReadAsDWord();
        break;

    case ID_MT_AREAS:
        ReadAsArray(p_Specific.aAreas);
        break;

    case ID_MT_TARGETS:
        ReadAsArray(p_Specific.aTargets);
        break;

    default:
        // Попробовать прочитать как свойства CMEObject
        CMEObject::ReadSubChunk();
        break;
    }
}

void CMEMagicTrap::operator=(const CMEMagicTrap &obj)
{
    *((CMEObject *)this) = obj;

    p_Specific = obj.p_Specific;
}

//-------------------------------------------------------------------------------------------------------
CMELever::CMELever()
{
    m_eType = MEOT_LEVER;
}

void CMELever::Save()
{
    BeginChunk(ID_LEVER);

    WriteBinary(ID_LEVER_SCIENCE_STATS_NEW, &p_Specific.science, sizeof(SRPGScienceStats));
    WriteByte(ID_LEVER_CUR_STATE, p_Specific.nCurrentState);
    WriteByte(ID_LEVER_TOTAL_STATE, p_Specific.nTotalStates);
    WriteByte(ID_LEVER_IS_CYCLED, p_Specific.isCycled);
    WriteByte(ID_LEVER_IS_DOOR, p_Specific.isDoor);
    WriteByte(ID_LEVER_RECALC_GRAPH, p_Specific.needGraphRecalc);

    // Save CMEObject propites
    CMEObject::WriteData();

    EndChunk();
}

void CMELever::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_LEVER_SCIENCE_STATS_NEW:
        ReadAsBinary(&p_Specific.science, sizeof(SRPGScienceStats));
        break;

    case ID_LEVER_IS_DOOR:
        p_Specific.isDoor = ReadAsByte() == 1;
        break;

    case ID_LEVER_RECALC_GRAPH:
        p_Specific.needGraphRecalc = ReadAsByte() == 1;
        break;

    case ID_LEVER_CUR_STATE:
        p_Specific.nCurrentState = ReadAsByte();
        break;

    case ID_LEVER_TOTAL_STATE:
        p_Specific.nTotalStates = ReadAsByte();
        break;

    case ID_LEVER_IS_CYCLED:
        p_Specific.isCycled = ReadAsByte() == 1;
        break;

    default:
        // Попробовать прочитать как свойства CMEObject
        CMEObject::ReadSubChunk();
        break;
    }
}

void CMELever::operator=(const CMELever &obj)
{
    *((CMEObject *)this) = obj;

    p_Specific = obj.p_Specific;
}

//-------------------------------------------------------------------------------------------------------

CMEUnit::CMEUnit()
    : CMEObject()
{
    m_eType = MEOT_UNIT;

    p_szUnitPrototype = "none";
    nCurAlarm = 0;
    p_sLogic[0].bEnabled = true;
    memset(&p_UnitSpecific.stats, 0, sizeof(p_UnitSpecific.stats));
}

void CMEUnit::operator=(const CMEUnit &obj)
{
    *((CMEObject *)this) = obj;

    nCurAlarm = obj.nCurAlarm;
    p_szUnitPrototype = obj.p_szUnitPrototype;
    memmove(&p_UnitSpecific.stats, &obj.p_UnitSpecific.stats, sizeof(p_UnitSpecific.stats));
    p_UnitSpecific.aArmors.Copy(obj.p_UnitSpecific.aArmors);
    p_UnitSpecific.aQuestItems.Copy(obj.p_UnitSpecific.aQuestItems);
    p_UnitSpecific.aQuickItems.Copy(obj.p_UnitSpecific.aQuickItems);
    p_UnitSpecific.aSpells.Copy(obj.p_UnitSpecific.aSpells);
    p_UnitSpecific.aWeapons.Copy(obj.p_UnitSpecific.aWeapons);
    p_UnitSpecific.needImport = obj.p_UnitSpecific.needImport;
    for (int i = 0; i < N_NUMBER_OF_ALARM; i++)
        p_sLogic[i] = obj.p_sLogic[i];
}

void CMEUnit::Selected(bool isSelected)
{
}

void CMEUnit::UpdateObject()
{
    if (p_lpMapObject == NULL)
    {
        theWorldServer.AddMapObject(this);
    }
    else
    {
        // Update existing object
        theWorldServer.aiMap.MoveObject(p_lpMapObject, p_ptPosition);
        theWorldServer.aiMap.OrientObject(p_lpMapObject, p_qRotation);

        p_lpMapObject->SetOrientation(p_qRotation);
        p_lpMapObject->SetComplection(p_Complection);
        p_lpMapObject->SetBaseComplection(p_Complection);
        p_lpMapObject->RestoreComplection();

        if (!p_isShadow)
            p_lpMapObject->SetFlag(OBJ_NOSHADOW);
        else
            p_lpMapObject->ResetFlag(OBJ_NOSHADOW);

        CMapObjectServer *pServerSpec = (CMapObjectServer *)p_lpMapObject->GetMapObjectSpecific();

        ASSERT(p_lpMapObject->WhoAmI() == HPC_UNIT);
        CUnitServer *pUnitServer = (CUnitServer *)((CUnit *)p_lpMapObject)->GetUnitSpecific();
        pUnitServer->InitializeUnit();
        /*		switch ( p_eDefModel )
                        {
                                case UM_IDLE:
                                        pUnitServer->logic.ClearMotivations();
                                        break;
                                case UM_GUARD:
                                        pUnitServer->logic.SetGuard( p_lpMapObject->GetCP(),
           float(p_nRArea) ); break;
                        }*/
        //		p_lpMapObject->SetUpdateMask( CMapObject::VLD_MASK_POSROT  );
        p_lpMapObject->SetUpdateMask(CMapObject::VLD_MASK_COMPLECTION);
    }
}

void CActionPointChunk::Save()
{
    BeginChunk(ID_ACTION_PT);

    WritePlot(ID_ACTION_PT_LOOK_PT, pAction->ptLook);
    WriteDWord(ID_ACTION_PT_WAIT_SEG, pAction->nWaitSeg);
    WriteFloat(ID_ACTION_PT_TURN_SPEED, pAction->fpTurnSpeed);
    WriteByte(ID_ACTION_PT_FLAGS, pAction->nFlags);

    EndChunk();
}

void CActionPointChunk::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_ACTION_PT_LOOK_PT:
        pAction->ptLook = ReadAsPlot();
        break;

    case ID_ACTION_PT_WAIT_SEG:
        pAction->nWaitSeg = (int)ReadAsDWord();
        break;

    case ID_ACTION_PT_TURN_SPEED:
        pAction->fpTurnSpeed = ReadAsFloat();
        break;
    case ID_ACTION_PT_FLAGS:
        pAction->nFlags = ReadAsByte();
        break;
    }
}

void CPatrolPointChunk::Save()
{
    BeginChunk(ID_GUARD_PT);

    WritePlot(ID_GUARD_PT_POSITION, pPoint->ptPosition);

    CActionPointChunk cAction;
    cAction.SetParentChunk(this);
    for (int i = 0; i < pPoint->actions.GetSize(); i++)
    {
        cAction.pAction = &pPoint->actions[i];
        cAction.Save();
    }

    EndChunk();
}

void CPatrolPointChunk::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_GUARD_PT_POSITION:
        pPoint->ptPosition = ReadAsPlot();
        break;

    case ID_ACTION_PT: {
        CActionPointChunk cAction;
        cAction.SetParentChunk(this);
        int i = pPoint->AddActionPoint(CPlot3D(0, 0, 0));
        cAction.pAction = &pPoint->actions[i];
        cAction.ReadAllSubChunks();
    }
    break;
    }
}

void CGeneralLogicChunk::Save()
{
    BeginChunk(ID_UNIT_LOGIC);

    WriteByte(ID_UNIT_LOGIC_ALARM_CONDITION, pLogic->eAlarmCondition);
    WriteFloat(ID_UNIT_LOGIC_HELP, pLogic->fCallForHelpRadius);
    WriteByte(ID_UNIT_LOGIC_CYCLIC, pLogic->bCyclic);

    /*	WriteByte( ID_UNIT_LOGIC_REVENGE,  pLogic->HasMotivation( GUM_REVENGE ) );
            WriteByte( ID_UNIT_LOGIC_AGRESSIV, pLogic->HasMotivation( GUM_AGGRESSIVE ) );
            WriteByte( ID_UNIT_LOGIC_FEAR, pLogic->HasMotivation( GUM_FEAR ) );*/
    WriteByte(ID_UNIT_LOGIC_AGRESSION_MODE, pLogic->aggressionMode);

    WriteByte(ID_UNIT_LOGIC_ALWAYS_ACTIVE, pLogic->HasMotivation(GUM_ALWAYS_ACTIVE));
    WriteDWord(ID_UNIT_LOGIC_MODEL, pLogic->eLogicModel);
    WriteFloat(ID_UNIT_LOGIC_GUARD_R, pLogic->fpGuardRadius);
    WriteFloat(ID_UNIT_LOGIC_WAIT, pLogic->fpGuardStayIdleTime);
    WritePlot(ID_UNIT_LOGIC_GUARD_PT, pLogic->ptGuardPoint);

    WriteByte(ID_UNIT_LOGIC_USE, pLogic->bEnabled);
    WriteByte(ID_UNIT_LOGIC_NALARM, pLogic->nAlarmNumber);

    CPatrolPointChunk cPoint;
    cPoint.SetParentChunk(this);
    for (int i = 0; i < pLogic->patrolPoints.GetSize(); i++)
    {
        cPoint.pPoint = &pLogic->patrolPoints[i];
        cPoint.Save();
    }

    EndChunk();
}

void CGeneralLogicChunk::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_UNIT_LOGIC_ALARM_CONDITION:
        pLogic->eAlarmCondition = EAlarmCondition(ReadAsByte());
        break;

    case ID_UNIT_LOGIC_ALWAYS_ACTIVE:
        pLogic->SetMotivation(GUM_ALWAYS_ACTIVE, ReadAsByte());
        break;

    case ID_UNIT_LOGIC_HELP:
        pLogic->fCallForHelpRadius = ReadAsFloat();
        break;

    case ID_UNIT_LOGIC_AGRESSION_MODE:
        pLogic->aggressionMode = EUnitAggressionMode(ReadAsByte());
        break;

    case ID_UNIT_LOGIC_AGRESSIV:
        //			pLogic->SetMotivation( GUM_AGGRESSIVE, ReadAsByte() );
        if (ReadAsByte() && pLogic->aggressionMode > UAM_AGGRESSIVE)
            pLogic->aggressionMode = UAM_AGGRESSIVE;
        break;

    case ID_UNIT_LOGIC_FEAR:
        //			pLogic->SetMotivation( GUM_FEAR, ReadAsByte() );
        if (ReadAsByte() && pLogic->aggressionMode > UAM_FEAR)
            pLogic->aggressionMode = UAM_FEAR;
        break;

    case ID_UNIT_LOGIC_REVENGE:
        //			pLogic->SetMotivation( GUM_REVENGE, ReadAsByte() );
        if (ReadAsByte() && pLogic->aggressionMode > UAM_REVENGE)
            pLogic->aggressionMode = UAM_REVENGE;
        break;

    case ID_UNIT_LOGIC_USE:
        pLogic->bEnabled = ReadAsByte();
        break;

    case ID_UNIT_LOGIC_NALARM:
        pLogic->nAlarmNumber = ReadAsByte();
        break;

    case ID_UNIT_LOGIC_CYCLIC:
        pLogic->bCyclic = ReadAsByte();
        break;

    case ID_UNIT_LOGIC_MODEL:
        pLogic->eLogicModel = (EUnitModel)ReadAsDWord();
        break;

    case ID_UNIT_LOGIC_GUARD_R:
        pLogic->fpGuardRadius = ReadAsFloat();
        break;

    case ID_UNIT_LOGIC_WAIT:
        pLogic->fpGuardStayIdleTime = ReadAsFloat();
        break;

    case ID_UNIT_LOGIC_GUARD_PT:
        pLogic->ptGuardPoint = ReadAsPlot();
        break;

    case ID_GUARD_PT: {
        CPatrolPointChunk cPoint;
        cPoint.SetParentChunk(this);
        int i = pLogic->AddPatrolPoint(CPlot3D(0, 0, 0));
        cPoint.pPoint = &pLogic->patrolPoints[i];
        cPoint.ReadAllSubChunks();
    }
    break;
    }
}

void CMEUnit::Save()
{
    BeginChunk(ID_UNIT);

    // Unit property
    WriteByte(ID_UNIT_NEED_IMPORT, p_UnitSpecific.needImport);
    WriteString(ID_UNIT_PROTOTYPE, p_szUnitPrototype);
    WriteStringArray(ID_UNIT_ARMORS, p_UnitSpecific.aArmors);
    WriteStringArray(ID_UNIT_WEAPONS, p_UnitSpecific.aWeapons);
    WriteStringArray(ID_UNIT_SPELLS, p_UnitSpecific.aSpells);
    WriteStringArray(ID_UNIT_QUICK_ITEMS, p_UnitSpecific.aQuickItems);
    WriteStringArray(ID_UNIT_QUEST_ITEMS, p_UnitSpecific.aQuestItems);
    WriteBinary(ID_UNIT_STATS, &p_UnitSpecific.stats, sizeof(p_UnitSpecific.stats));

    // Save MEObject data
    CMEObject::WriteData();

    CGeneralLogicChunk cLogic;
    cLogic.SetParentChunk(this);
    for (int i = 0; i < N_NUMBER_OF_ALARM; i++)
    {
        p_sLogic[i].ptGuardPoint = p_ptPosition;
        cLogic.pLogic = &p_sLogic[i];
        cLogic.Save();
    }

    EndChunk();
}

void CMEUnit::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_UNIT_NEED_IMPORT:
        p_UnitSpecific.needImport = ReadAsByte();
        break;

    case ID_UNIT_ARMORS:
        ReadAsStringArray(p_UnitSpecific.aArmors);
        break;

    case ID_UNIT_WEAPONS:
        ReadAsStringArray(p_UnitSpecific.aWeapons);
        break;

    case ID_UNIT_SPELLS:
        ReadAsStringArray(p_UnitSpecific.aSpells);
        break;

    case ID_UNIT_QUICK_ITEMS:
        ReadAsStringArray(p_UnitSpecific.aQuickItems);
        break;

    case ID_UNIT_QUEST_ITEMS:
        ReadAsStringArray(p_UnitSpecific.aQuestItems);
        break;

    case ID_UNIT_STATS:
        ReadAsBinary(&p_UnitSpecific.stats, sizeof(p_UnitSpecific.stats));
        break;

    case ID_UNIT_LOGIC: {
        CGeneralLogicChunk cLogic;
        cLogic.SetParentChunk(this);
        cLogic.pLogic = &p_sLogic[nCurAlarm++];
        cLogic.pLogic->ClearMotivation();
        cLogic.pLogic->aggressionMode = UAM_LAST;
        cLogic.ReadAllSubChunks();
        if (cLogic.pLogic->aggressionMode == UAM_LAST)
            cLogic.pLogic->aggressionMode = UAM_AGGRESSIVE;
    }
    break;

    case ID_UNIT_PROTOTYPE:
        p_szUnitPrototype = ReadAsString();
        break;

    default:
        // Попробовать прочитать как свойства CMEObject
        CMEObject::ReadSubChunk();
        break;
    }
}

//-------------------------------------------------------------------------------------------------------
void CMEEffect::operator=(const CMEEffect &obj)
{
    *((CMEBasicObject *)this) = obj;
    m_isEffectEnable = true;
    p_lpEffect = obj.p_lpEffect;
}

CMEEffect::CMEEffect()
    : CMEBasicObject()
{
    p_lpMapObject = NULL;
    p_lpEffect = NULL;
    m_isEffectEnable = true;
}

CMEEffect::~CMEEffect()
{
#ifdef __MAP_EDITOR__
    if (p_lpMapObject)
    {
        p_lpMapObject->Release();
        theWorld.RemoveObject(GetVisualisationID(p_nID));
        theWorldServer.RemoveEffect(p_nID);
    }
#endif //__MAP_EDITOR__
}

LPMAPOBJECT CMEEffect::GetClientObject()
{
    return (LPMAPOBJECT)theWorld.GetMapObjectManager()->Find(GetVisualisationID(p_nID));
}

void CMEEffect::EnableVisualisation(bool bEnable)
{
    m_isEffectEnable = bEnable;
    if (bEnable)
        p_lpMapObject->ResetFlag(OBJ_HIDDEN);
    else
        p_lpMapObject->SetFlag(OBJ_HIDDEN);
}

void CMEEffect::MoveOn(CPlot3D inc)
{
    p_ptPosition += inc;
    float hieght = theWorld.GetTerrain()->GetZValue(p_ptPosition.x, p_ptPosition.y);
    if (p_ptPosition.z < hieght)
        p_ptPosition.z = hieght;
    UpdateObject();
}

LPMAPOBJECT CMEEffect::CreateClientObject(CString szTemplate, CString szTexture, CPlot3D ptPosition,
                                          int nID)
{
#ifdef __MAP_EDITOR__
    LPMAPOBJECT lpMapObject;
    lpMapObject = (LPUNIT)theWorld.GetMapObjectManager()->CreateData(MOTID_OBJECT, nID);
    lpMapObject->AddRef();

    CObject3DClientSpecific *lpObjectSpecific = (CObject3DClientSpecific *)
                                                    lpMapObject->GetObjectSpecific();

    lpMapObject->SetCP(ptPosition);

    CStringArray szstrarr;
    lpMapObject->LoadByTemplate(szTemplate, szstrarr);
    lpObjectSpecific->SetMaterials();
    lpMapObject->LoadAnimation(szTemplate);
    lpMapObject->InitAnimation();
    lpObjectSpecific->LoadTextures(szTexture, "");
    lpMapObject->RecalcBoundingVolumeFast();

    theWorld.AddObject(lpMapObject);
    return lpMapObject;
#else
    return NULL;
#endif //__MAP_EDITOR__
}

void CMEEffect::CreateClientVisualisationObject(CString szTemplate, CString szTexture)
{
#ifdef __MAP_EDITOR__
    p_lpMapObject = CreateClientObject(szTemplate, szTexture, p_ptPosition,
                                       GetVisualisationID(p_nID));
#endif //__MAP_EDITOR__
}

//---------------------------------------------------------------------------------
CMEParticle::CMEParticle()
    : CMEEffect()
{
    m_eType = MEOT_PARTICLE;

    p_idType = TID_EMPTY;
    p_fEfectStrength = 1.0f;
}

void CMEParticle::operator=(const CMEParticle &obj)
{
    *((CMEEffect *)this) = obj;

    p_idType = obj.p_idType;
    p_fEfectStrength = obj.p_fEfectStrength;
}

void CMEParticle::Save()
{
    BeginChunk(ID_PARTICL);

    WriteDWord(ID_PARTICL_ID, p_nID);
    WritePlot(ID_PARTICL_POSITION, p_ptPosition);
    WriteString(ID_PARTICL_NAME, p_szName);
    WriteString(ID_PARTICL_COMMENTS, p_szComments);
    WriteDWord(ID_PARTICL_TYPE, p_idType);
    WriteFloat(ID_PARTICL_SCALE, p_fEfectStrength);

    EndChunk();
}

void CMEParticle::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_PARTICL_ID:
        p_nID = ReadAsDWord();
        break;
    case ID_PARTICL_TYPE:
        p_idType = (ETypeIDs)ReadAsDWord();
        break;
    case ID_PARTICL_POSITION:
        p_ptPosition = ReadAsPlot();
        break;
    case ID_PARTICL_SCALE:
        p_fEfectStrength = ReadAsFloat();
        break;
    case ID_PARTICL_NAME:
        p_szName = ReadAsString();
        break;
    case ID_PARTICL_COMMENTS:
        p_szComments = ReadAsString();
        break;
    }
}

void CMEParticle::UpdateObject()
{
    if (p_lpMapObject == NULL)
    {
        CreateClientVisualisationObject("efcu3", "particles");
        if (p_idType != TID_EMPTY)
        {
            theWorldServer.AddMapObject(this);
            p_lpEffect = ((CEffectManager *)theWorldServer.GetEffectManager())->FindByID(p_nID);
        }
    }
    else
    {
        CEffectParticle *pParticle = (CEffectParticle *)p_lpEffect;
        VERIFY(pParticle);

        p_lpMapObject->SetCP(p_ptPosition);

        pParticle->fpScale = p_fEfectStrength;
        pParticle->typeID = p_idType;
        pParticle->SetCP(p_ptPosition);
        pParticle->SetUpdateMask();
    }
}

//---------------------------------------------------------------------------------
CMELight::CMELight()
    : CMEEffect()
{
    m_eType = MEOT_LIGHT;

    p_ptColor = CPlot3D(1.0f, 1.0f, 1.0f);
    p_isMakeShadow = true;
    p_fRange = 10.0f;
}

void CMELight::operator=(const CMELight &obj)
{
    *((CMEEffect *)this) = obj;

    p_ptColor = obj.p_ptColor;
    p_isMakeShadow = obj.p_isMakeShadow;
    p_fRange = obj.p_fRange;
}

void CMELight::Save()
{
    BeginChunk(ID_LIGHT);

    WriteDWord(ID_LIGHT_ID, p_nID);
    WriteByte(ID_LIGHT_SHADOW, p_isMakeShadow);
    WritePlot(ID_LIGHT_POSITION, p_ptPosition);
    WritePlot(ID_LIGHT_COLOR, p_ptColor);
    WriteFloat(ID_LIGHT_RANGE, p_fRange);
    WriteString(ID_LIGHT_NAME, p_szName);
    WriteString(ID_LIGHT_COMMENTS, p_szComments);

    EndChunk();
}

void CMELight::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_LIGHT_ID:
        p_nID = ReadAsDWord();
        break;
    case ID_LIGHT_SHADOW:
        p_isMakeShadow = ReadAsByte() ? true : false;
        break;
    case ID_LIGHT_POSITION:
        p_ptPosition = ReadAsPlot();
        break;
    case ID_LIGHT_RANGE:
        p_fRange = ReadAsFloat();
        break;
    case ID_LIGHT_NAME:
        p_szName = ReadAsString();
        break;
    case ID_LIGHT_COMMENTS:
        p_szComments = ReadAsString();
        break;
    case ID_LIGHT_COLOR:
        p_ptColor = ReadAsPlot();
        break;
    }
}

void CMELight::UpdateObject()
{
    if (p_lpMapObject == NULL)
    {
        CreateClientVisualisationObject("efcu0", "light");

        theWorldServer.AddMapObject(this);
        p_lpEffect = ((CEffectManager *)theWorldServer.GetEffectManager())->FindByID(p_nID);
    }
    else
    {
        CEffectPointLight *pLight = (CEffectPointLight *)p_lpEffect;
        VERIFY(pLight);

        pLight->color = p_ptColor;
        pLight->fpRange = p_fRange;
        pLight->SetCP(p_ptPosition);
        p_lpMapObject->SetCP(p_ptPosition);

        pLight->SetUpdateMask();
    }
}

//---------------------------------------------------------------------------------
CMESound::CMESound()
    : CMEEffect()
{
    m_eType = MEOT_SOUND;

    p_aResNames.RemoveAll(); // names of sound in resoures
    p_nR = 10;               // first radius
    p_nR2 = 15;              // second radius
    p_nSoundsNumb = 1;       // max number of sounds, plays in one time
    p_nMin = 1500;           // minimun volue of range
    p_nMax = 3000;           // maximum volue of range
    p_lpContur = NULL;       // contur of first sounds area
    p_lpContur2 = NULL;      // contur of second sounds area
    p_isAmbient = false;     // is this sound ambient?
}

CMESound::~CMESound()
{
    RemoveContur();
}

void CMESound::operator=(const CMESound &obj)
{
    *((CMEEffect *)this) = obj;

    p_nR = p_nR;
    p_nR2 = p_nR2;

    p_nSoundsNumb = p_nSoundsNumb;

    p_nMin = p_nMin;
    p_nMax = p_nMax;

    p_lpContur = p_lpContur;
    p_lpContur2 = p_lpContur2;
    p_isAmbient = false;
    p_isMusic = false;

    p_aResNames.RemoveAll();
    p_aResNames.Copy(obj.p_aResNames);
}

void CMESound::Save()
{
    BeginChunk(ID_SOUND);

    WriteStringArray(ID_SOUND_RESNAME, p_aResNames);
    WriteDWord(ID_SOUND_ID, p_nID);
    WritePlot(ID_SOUND_POSITION, p_ptPosition);
    WriteDWord(ID_SOUND_RANGE, p_nR);
    WriteDWord(ID_SOUND_RANGE2, p_nR2);
    WriteString(ID_SOUND_NAME, p_szName);
    WriteString(ID_SOUND_COMMENTS, p_szComments);

    WriteDWord(ID_SOUND_MIN, p_nMin);
    WriteDWord(ID_SOUND_MAX, p_nMax);
    WriteByte(ID_SOUND_AMBIENT, p_isAmbient);
    WriteByte(ID_SOUND_IS_MUSIC, p_isMusic);
    EndChunk();
}

void CMESound::ReadSubChunk()
{
    switch (CChunkTag::GetID())
    {
    case ID_SOUND_MIN:
        p_nMin = ReadAsDWord();
        break;
    case ID_SOUND_AMBIENT:
        p_isAmbient = ReadAsByte() == 1;
        break;
    case ID_SOUND_IS_MUSIC:
        p_isMusic = ReadAsByte() == 1;
        break;
    case ID_SOUND_MAX:
        p_nMax = ReadAsDWord();
        break;
    case ID_SOUND_ID:
        p_nID = ReadAsDWord();
        break;
    case ID_SOUND_POSITION:
        p_ptPosition = ReadAsPlot();
        break;
    case ID_SOUND_RANGE:
        p_nR = ReadAsDWord();
        break;
    case ID_SOUND_RANGE2:
        p_nR2 = ReadAsDWord();
        break;
    case ID_SOUND_NAME:
        p_szName = ReadAsString();
        break;
    case ID_SOUND_RESNAME:
        p_aResNames.RemoveAll(); // Temporary
        ReadAsStringArray(p_aResNames);
        break;
    case ID_SOUND_COMMENTS:
        p_szComments = ReadAsString();
        break;
    }
}

void CMESound::UpdateObject()
{
    if (p_lpMapObject == NULL)
    {
        CreateClientVisualisationObject("efcu2", "sound");
        // Create sound area contur
        CreateContur();
        // Create real sound
        if (p_aResNames.GetSize() > 0)
        {
            theWorldServer.AddMapObject(this);
            p_lpEffect = ((CEffectManager *)theWorldServer.GetEffectManager())->FindByID(p_nID);
        }
    }
    else
    {
        p_lpMapObject->SetCP(p_ptPosition);
        CreateContur();

        if (p_lpEffect != NULL)
            theWorldServer.RemoveEffect(p_nID);

        if (p_aResNames.GetSize() > 0)
        {
            theWorldServer.AddMapObject(this);
            p_lpEffect = ((CEffectManager *)theWorldServer.GetEffectManager())->FindByID(p_nID);
        }
        else
            p_lpEffect = NULL;
    }
}

void CMESound::EnableVisualisation(bool bEnable)
{
    CMEEffect::EnableVisualisation(bEnable);
    UpdateContur();
}

void CMESound::UpdateContur()
{
    CreateContur();
}

void CMESound::CreateContur()
{
#ifdef __MAP_EDITOR__
    if (p_lpContur)
        RemoveContur();

    if (m_isEffectEnable)
    {
        p_lpContur = dbgnew CPolyLine;
        p_lpContur->MakeCircle(p_ptPosition, p_nR);
        theWorld.AddPolyLine(p_lpContur);

        p_lpContur2 = dbgnew CPolyLine;
        p_lpContur->MakeCircle(p_ptPosition, p_nR2);
        p_lpContur2->SetColor(RGBA_MAKE(180, 0, 0, 255));
        theWorld.AddPolyLine(p_lpContur2);
    }
#endif
}

void CMESound::RemoveContur()
{
#ifdef __MAP_EDITOR__
    if (p_lpContur)
    {
        theWorld.RemovePolyLine(p_lpContur->GetID());
        FREE(p_lpContur);
    }
    if (p_lpContur2)
    {
        theWorld.RemovePolyLine(p_lpContur2->GetID());
        FREE(p_lpContur2);
    }
#endif
}

void CMESound::MoveContur(CPlot3D inc)
{
#ifdef __MAP_EDITOR__
    for (int i = 0; i < p_lpContur->GetNPlots(); i++)
        p_lpContur->SetPlotAt(i, p_lpContur->GetPlot(i) + inc);
    for (i = 0; i < p_lpContur2->GetNPlots(); i++)
        p_lpContur2->SetPlotAt(i, p_lpContur2->GetPlot(i) + inc);
#endif
}

//---------------------------------------------------------------------------------
void SWorldSetting::operator=(const SWorldSetting &ws)
{
    VERIFY(this != &ws);
    wind_dir = ws.wind_dir;
    wind_str = ws.wind_str;
    time = ws.time;
    ambient = ws.ambient;
    sun_light = ws.sun_light;
}

//---------------------------------------------------------------------------------
void SDiplomation::operator=(const SDiplomation &dipl)
{
    for (int i = 0; i < N_MAX_PLAYERS; i++)
        for (int j = 0; j < N_MAX_PLAYERS; j++)
            aDipl[i][j] = dipl.aDipl[i][j];
    aPlayers.RemoveAll();
    for (int i = 0; i < N_MAX_PLAYERS; i++)
    {
        aPlayers.Add(dipl.aPlayers[i]);
    }
}

SDiplomation::SDiplomation()
{
    for (int i = 0; i < N_MAX_PLAYERS; i++)
        for (int j = 0; j < N_MAX_PLAYERS; j++)
            aDipl[i][j] = Neutral;
    for (int i = 0; i < N_MAX_PLAYERS; i++)
        aDipl[i][i] = Ally;
    for (int i = 0; i < N_MAX_PLAYERS; i++)
    {
        aPlayers.Add("");
        aPlayers[i].Format("Player-%i", i);
    }
}
