////// ObjectDBReader.cpp implementation file  ///
//												//
//	  The Reader of object data base file       //
//												//
//												//
//	started             16/12/98				//
//												//
//	last time revised   16/12/98				//
//////////////////////////////////////////////////

#include "StdAfx.h"
#include "EngineDiagnostics.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "ObjectDBReader.h"
#include "MapEditObject.h"
#include "MapEditorChunkIDs.h"
#include "MapObjectManager.h"
#include "UnitServer.h"

#include "WorldClient.h"

#include "Quantizator.h"
#include "2DScreenWait.h"

#include <map>
#include <string>
#include <list>

//--------------------------------------------------------------------------
/////////////////////////////////////////////////////////////////////////////////////
struct SComplectionContainer
{
    CPlot3D ptComplection;
    CMEObject *pMapEditObj;
    //
    SComplectionContainer(CMEObject *pNewMapEditObj, float fStrength, float fGrace, float fTall)
        : pMapEditObj(pNewMapEditObj)
        , ptComplection(fStrength, fGrace, fTall)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
bool CGameMOBFile::Load(CWorldServer *pWorldServer, bool bSkipScript)
{
    CBasicObjectMap objects;
    SDiplomation dipl;
    CString szScript;

    if (Read(objects, dipl, szScript))
    {
        int i, j;
        for (i = 0; i < N_MAX_PLAYERS; i++)
            for (j = 0; j < N_MAX_PLAYERS; j++)
                theWorldServer.fof.SetDiplomacy(i, j, dipl.aDipl[i][j]);
    }

    CHashMapIterator<int, CMEBasicObject *> hmi;
    CMEBasicObject *pObject;

    // CRAP - quanizate map objects complection
    if (theSettings.nAutomaticQuantization)
    {
        typedef std::list<SComplectionContainer> CComplectionContainerList;
        typedef std::map<std::string, CComplectionContainerList> CComplectionContainerMap;
        CComplectionContainerMap objectsMap;
        // collect objects
        for (pObject = hmi.GetFirst(&objects); !hmi.IsReachEnd(); pObject = hmi.GetNext())
        {
            if (pObject->m_eType == MEOT_OBJECT)
            {
                CMEObject *pMapEditObj = (CMEObject *)pObject;
                if ((pMapEditObj->p_tidTypeID != MOTID_HUMAN) &&
                    (pMapEditObj->p_tidTypeID != MOTID_MONSTER) &&
                    (pMapEditObj->p_tidTypeID != MOTID_ANIMAL))
                {
                    objectsMap[std::string(pMapEditObj->p_szTemplate)].emplace_back(
                        SComplectionContainer(pMapEditObj, pMapEditObj->p_Complection.strength,
                                              pMapEditObj->p_Complection.grace,
                                              pMapEditObj->p_Complection.tall));
                }
            }
        }
        // quanizate objects
        for (auto iter = objectsMap.begin(); iter != objectsMap.end(); ++iter)
        {
            NQuantizator::CPlot3DVector complections;
            NQuantizator::CPlot3DVector palette;
            complections.clear();
            for (auto liter = iter->second.begin(); liter != iter->second.end(); ++liter)
                complections.push_back(liter->ptComplection);
            palette.clear();
            NQuantizator::Quanizate(complections, palette, 2);
            for (auto liter = iter->second.begin(); liter != iter->second.end(); ++liter)
            {
                liter->ptComplection = NQuantizator::FindBestMatch(palette, liter->ptComplection);
                liter->pMapEditObj->p_Complection.strength = liter->ptComplection.x;
                liter->pMapEditObj->p_Complection.grace = liter->ptComplection.y;
                liter->pMapEditObj->p_Complection.tall = liter->ptComplection.z;
            }
        }
        //
        objectsMap.clear();
        // CRAP ends
    }

    int nLoadObjectCounter = 0;
    int nObjectNumbersInStep = objects.GetCount() / 5 + 1;
    for (pObject = hmi.GetFirst(&objects); !hmi.IsReachEnd(); pObject = hmi.GetNext())
    {
        if (pObject->m_eType == MEOT_MAGIC_TRAP)
        {
            CMEMagicTrap *pTrap = static_cast<CMEMagicTrap *>(pObject);
            pTrap->p_Specific.ptPosition = pTrap->p_ptPosition;
            pTrap->p_Specific.dwDiplomacy = 0;
            for (int j = 0; j < N_MAX_PLAYERS; j++)
                pTrap->p_Specific.dwDiplomacy |= (dipl.aDipl[pTrap->p_nPlayer][j] == War) << j;
        }
        theWorldServer.AddMapObject(pObject);
        // чтобы колонна не ходила
        /*if (pObject->p_nID != 2 || ((CMEObject *)pObject)->p_szTemplate != "unmoco2")
        {*/
        if (pObject->m_eType == MEOT_UNIT && pObject->p_lpMapObject)
        {
            CUnitServer *pUnitServer = (CUnitServer *)((CUnit *)pObject->p_lpMapObject)
                                           ->GetUnitSpecific();
            CMEUnit *pUnit = (CMEUnit *)pObject;

            pUnitServer->logic.SetLogics(pUnit->p_sLogic, N_NUMBER_OF_ALARM);
        }
        //}
        delete pObject;
        if (++nLoadObjectCounter % nObjectNumbersInStep == 0)
            C2DScreenWait::Step(nLoadObjectCounter / nObjectNumbersInStep + 1);
    }
    /*	if ( szScript.GetLength() < 100 )
            {
                    AfxMessageBox( "CGameMOBFile::Load\nScript not found." );
            }*/
    if (!bSkipScript){
        BOOL parsed=theWorldServer.script.LoadText(szScript);
        auto error=theWorldServer.script.GetLastError();
        EngineDiagnostics::Event("MOB script parsed=%d bytes=%d line=%d error=%s",parsed,szScript.GetLength(),error.nLine,(LPCSTR)error.text);
    }
    return true;
}

bool CGameMOBFile::Read(CBasicObjectMap &objects, SDiplomation &dipl, CString &szScript)
{
    bool isMain = true;
    ReadNextHeader(); // Main section
    CChunkTag sSectionChunk(this);
    for (sSectionChunk.ReadNextHeader();; sSectionChunk.ReadNextHeader())
    {
        if (!sSectionChunk.IsAllData())
            switch (sSectionChunk.GetID())
            {
            case ID_WORLD_SET: {
                SWorldSetting ws;
                CChunkTag sChunk(&sSectionChunk);
                sChunk.ReadNextHeader();
                ws.wind_dir = sChunk.ReadAsPlot();
                sChunk.ReadNextHeader();
                ws.wind_str = sChunk.ReadAsFloat();
                sChunk.ReadNextHeader();
                ws.time = sChunk.ReadAsFloat();
                sChunk.ReadNextHeader();
                ws.ambient = sChunk.ReadAsFloat();
                sChunk.ReadNextHeader();
                ws.sun_light = sChunk.ReadAsFloat();
                theWorldServer.SetWorldTime(ws.time);
                theWorldServer.SetServerWind(ws.wind_dir,
                                             Min(ws.wind_str, theSettings.fpDefaultWindForce));
            }
            break;
            case ID_SS_TEXT:
                szScript = sSectionChunk.ReadAsCodedString();
                break;
            case ID_SS_TEXT_OLD:
                szScript = sSectionChunk.ReadAsString();
                break;
            case ID_DIPLOMATION: // Diplomaty section
            {
                CChunkTag sChunk(&sSectionChunk);
                sChunk.ReadNextHeader();
                VERIFY(sChunk.GetID() == ID_DIPLOMATION_FOF);
                sChunk.ReadAsBinary(&dipl.aDipl, sizeof(dipl.aDipl));
                sChunk.ReadNextHeader();
                VERIFY(sChunk.GetID() == ID_DIPLOMATION_PL_NAMES);
                dipl.aPlayers.RemoveAll();
                sChunk.ReadAsStringArray(dipl.aPlayers);
            }
            break;
            case ID_OBJECTSECTION: // Map edit objects section
            {
                CChunkTag sObjChunk(&sSectionChunk);
                CMEBasicObject *pNewObject = NULL;
                for (sObjChunk.ReadNextHeader();; sObjChunk.ReadNextHeader())
                {
                    switch (sObjChunk.GetID())
                    {
                    case ID_UNIT:
                        pNewObject = dbgnew CMEUnit;
                        break;
                    case ID_OBJECT:
                        pNewObject = dbgnew CMEObject;
                        break;
                    case ID_PARTICL:
                        pNewObject = dbgnew CMEParticle;
                        break;
                    case ID_LIGHT:
                        pNewObject = dbgnew CMELight;
                        break;
                    case ID_SOUND:
                        pNewObject = dbgnew CMESound;
                        break;
                    case ID_TORCH:
                        pNewObject = dbgnew CMETorch;
                        break;
                    case ID_MAGIC_TRAP:
                        pNewObject = dbgnew CMEMagicTrap;
                        break;
                    case ID_LEVER:
                        pNewObject = dbgnew CMELever;
                        break;

                    default:
#ifdef _DEBUG
                        CString szText;
                        szText.Format("Unknow chunk with ID = %X", sObjChunk.GetID());
                        AfxMessageBox(szText);
#endif
                        break;
                    }
                    if (pNewObject)
                    {
                        pNewObject->SetParentChunk(&sObjChunk);
                        pNewObject->ReadAllSubChunks();
                        objects.SetAt(pNewObject->p_nID, pNewObject);
                        pNewObject = NULL;
                    }
                    if (sSectionChunk.IsAllData())
                        break;
                }
            }
            break;
            }

        switch (sSectionChunk.GetID()) // For Chunk without body
        {
        case ID_PR_OBJECTDBFILE: // Status of MOB file: primary, secondory
        {
            isMain = true;
        }
        break;
        case ID_SC_OBJECTDBFILE: {
            isMain = false;
        }
        break;
        }
        sSectionChunk.SkipLostData();
        if (IsAllData())
            break;
    }
    return isMain;
}
