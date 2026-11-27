/**********************************************************************
 *<
        FILE: aiScanUnits.cpp

        DESCRIPTION:  Senses

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "StdAfx.h"
#include "SaveSchema.h"
#include "NumericPolicy.h"
#include "CorePerf.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiScanUnits.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include "GeneralSettings.h"
#include "aiMap.h"
#include "UnitServer.h"
#include "mapObjectServer.h"
#include "WorldServer.h"
#include "console.h"

/////////////////////////////////////////////////////////////////////////////////////
#ifndef _DEBUG
#pragma optimize("agw", on)
#endif
/////////////////////////////////////////////////////////////////////////////////////
static ostream &aiLog = NConsole::ai;
/////////////////////////////////////////////////////////////////////////////////////
const int N_LISTS_REFRESH_DELAY = 8;
const int N_NORMAL_DISTRIBUTION_LENGTH = 1024;
const float FP_AI_SMELL_SPEED = 0.1f; // скорость распространени€ запаха в метрах за сегмент
const int N_AI_SMELL_START_DELAY = 150;
const float FP_AI_STEPS_TRACKING_DISTANCE = 1.5f;
// во сколько раз рассто€ние от монстра до цели должно превысить его радиус зрени€, чтобы он потер€л
// цель из вида
const float FP_AI_LOST_FROM_SIGHT_MUL = 1.05f;

/////////////////////////////////////////////////////////////////////////////////////
static float fpNormalDistribution[N_NORMAL_DISTRIBUTION_LENGTH];
static int nNormalDistribCounter = 0;
/////////////////////////////////////////////////////////////////////////////////////
inline float GetNextRnd()
{
    // return fpNormalDistribution[ ( nNormalDistribCounter++ ) & ( N_NORMAL_DISTRIBUTION_LENGTH - 1
    // ) ];
    return 1;
}

/////////////////////////////////////////////////////////////////////////////////////
// static float fpVisionProbability[9] = { 1, 0.986f, 0.962f, 0.919f, 0.467f, 0.089f, 0.076f,
// 0.036f, 0.01f};
static float GetVisionRatio(float fpRadius, float fpVisionRadius)
{
    if (fpVisionRadius == 0)
        return AI_MAX_VISIBILITY_INFO_MULT;
    float fpRes = fpRadius / fpVisionRadius;
    return Min(AI_MAX_VISIBILITY_INFO_MULT, fpRes);
}
static float GetVisionProbability(float fpRadius, float fpVisionRadius)
{
    float t = fpRadius / fpVisionRadius;
    if (t < 0 || t > 1)
        return 0;
    t = (float)cos(t * 3.1415926535f);
    if (IsNegative(t))
        t = -(float)pow(-t, 0.2f);
    else
        t = (float)pow(t, 0.2f);
    t = (t + 1) / 2;
    return t;
    /*	int nGroup = Float2Int( fpRadius * 8 / fpVisionRadius );
            if ( nGroup > 8 || nGroup < 0 )
                    return 0;
            return fpVisionProbability[ nGroup ];*/
}
/////////////////////////////////////////////////////////////////////////////////////
// SVisibilityMask
void SVisibilityMask::Clear()
{
    nShiftX = 0x7fffffff;
    nShiftY = 0x7fffffff;
    data.Clear();
    data.FillZero();
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
void SVisibilityMask::Extend(int nCenterX, int nCenterY, int nRadius)
{
    bool bMove = false;
    int nNewSize;
    int nlx = nCenterX - nRadius;
    int nhx = nCenterX + nRadius;
    int nly = nCenterY - nRadius;
    int nhy = nCenterY + nRadius;
    if (nShiftX != 0x7fffffff)
    {
        bMove = nlx < nShiftX || nly < nShiftY || nhx > nShiftX + data.GetXSize() - 1 ||
                nhy > nShiftY + data.GetYSize() - 1;
        nlx = Min(nlx, nShiftX);
        nhx = Max(nhx, nShiftX + data.GetXSize() - 1);
        nly = Min(nly, nShiftY);
        nhy = Max(nhy, nShiftY + data.GetYSize() - 1);
        if (bMove)
        {
            nNewSize = Max(nhy - nly, nhx - nlx) + 1;
            CArray2D<char> oldData = data;
            data.SetSizes(nNewSize, nNewSize);
            data.FillZero();
            for (int y = 0; y < oldData.GetYSize(); y++)
            {
                for (int x = 0; x < oldData.GetXSize(); x++)
                    data[y + nShiftY - nly][x + nShiftX - nlx] = oldData[y][x];
            }
            nShiftX = nlx;
            nShiftY = nly;
        }
    }
    else
    {
        nNewSize = Max(nhy - nly, nhx - nlx) + 1;
        data.SetSizes(nNewSize, nNewSize);
        data.FillZero();
        nShiftX = nlx;
        nShiftY = nly;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// SAIMark
SAIMark *SAIMark::pFree = NULL;
static CList<SAIMark> AIMarksPool(1024);

SAIMark *SAIMark::GetNewMark()
{
    SAIMark *pRes = pFree;
    if (pFree)
        pFree = pRes->pNext;
    else
        pRes = &AIMarksPool.GetAt(AIMarksPool.AddHead(SAIMark()));
    pRes->pNext = 0;
    pRes->pPrev = 0;
    pRes->pNextDel = 0;
    return pRes;
}

/////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////
// CAIMarkManager
/////////////////////////////////////////////////////////////////////////////////////
SAIMarkCell *CAIMarkManager::GetCell(float fpX, float fpY)
{
    int nX = Float2Int(fpX / N_AI_MARK_CELL_SIZE);
    int nY = Float2Int(fpY / N_AI_MARK_CELL_SIZE);
    if (nX < 0 || nY < 0 || nX >= cells.GetXSize() || nY >= cells.GetYSize())
        return NULL;
    return &cells[nY][nX];
}

/////////////////////////////////////////////////////////////////////////////////////
CAIMarkManager::~CAIMarkManager()
{
    for (int i = 0; i < N_MAX_MARK_LIFETIME; i++)
    {
        for (int k = 0; k < N_AI_MARKS_PLAYER_NUM; k++)
        {
            SAIMark *pDel;
            pDel = pDeleteLists[i][k];
            while (pDel)
            {
                pDel->ReleaseMark();
                pDel = pDel->pNextDel;
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIMarkManager::Clear()
{
    int i, k;
    nSegment = -1;
    for (i = 0; i < N_MAX_MARK_LIFETIME; i++)
        for (k = 0; k < N_AI_MARKS_PLAYER_NUM; k++)
            pDeleteLists[i][k] = NULL;
    cells.SetSizes(1, 1);
    cells[0][0].Clear();
}

/////////////////////////////////////////////////////////////////////////////////////
BOOL CAIMarkManager::SetMap(CAIMap *pMap, float fpXSize, float fpYSize)
{
    Clear();
    pAIMap = pMap;
    int nXSize = ((int)(fpXSize / N_AI_MARK_CELL_SIZE)) + 1;
    int nYSize = ((int)(fpYSize / N_AI_MARK_CELL_SIZE)) + 1;
    cells.SetSizes(nXSize, nYSize);
    cells.FillZero();

    // load normal distribution random numbers from resource
    CMappedResource aiRes;
    if (!aiRes.OpenResource(CString(theWorldServer.GetResourcePath()) + "aiinfo.res"))
        return ReportError("Unable to read file aiinfo.res");
    LPBYTE pLoc = aiRes.GetFile("normal.dat");
    if (!pLoc)
        return ReportError("CAIMap::Load.\n Can't open file \"normal.dat\"");
    memcpy(fpNormalDistribution, pLoc, sizeof(fpNormalDistribution));
    aiRes.CloseFile(pLoc);
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////
// creating & deleting footsteps
/////////////////////////////////////////////////////////////////////////////////////
void CAIMarkManager::Segment(int nCurSegment)
{
    int k;
    nSegment = nCurSegment;
    // удалим устаревшие следы
    for (k = 0; k < N_AI_MARKS_PLAYER_NUM; k++)
    {
        SAIMark *pDel;
        pDel = pDeleteLists[nSegment & (N_MAX_MARK_LIFETIME - 1)][k];
        while (pDel)
        {
            // удалим след из списков
            if (pDel->pPrev)
            {
                pDel->pPrev->pNext = pDel->pNext;
                if (pDel->pNext)
                    pDel->pNext->pPrev = pDel->pPrev;
            }
            else
            {
                SAIMarkCell *pCell;
                pCell = GetCell(pDel->ptPosition.x, pDel->ptPosition.y);
                ASSERT(pCell);
                ASSERT(pCell[0][k] == pDel);
                if (pCell[0][k])
                    pCell[0][k] = pDel->pNext;
                if (pDel->pNext)
                    pDel->pNext->pPrev = NULL;
            }
            pDel->ReleaseMark();
            pDel = pDel->pNextDel;
        }
        pDeleteLists[nSegment & (N_MAX_MARK_LIFETIME - 1)][k] = NULL;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
SAIMark *CAIMarkManager::MakeNewMark(const CPlot3D &pos, int nPlayerNum)
{
    ASSERT(nSegment >= 0);
    SAIMarkCell *pCell = GetCell(pos.x, pos.y);
    if (!pCell)
        return NULL;
    nPlayerNum &= (N_AI_MARKS_PLAYER_NUM - 1);

    SAIMark *pNew = SAIMark::GetNewMark();
    // добавим в список присутствующих марков в сетке
    pNew->pNext = pCell[0][nPlayerNum];
    if (pCell[0][nPlayerNum])
        pCell[0][nPlayerNum]->pPrev = pNew;
    pNew->pPrev = NULL;
    pCell[0][nPlayerNum] = pNew;
    // инициализаци€
    pNew->ptPosition.x = pos.x;
    pNew->ptPosition.y = pos.y;
    pNew->ptPointPosition.x = pos.x;
    pNew->ptPointPosition.y = pos.y;
    pNew->nTime = nSegment;
    // добавим в список на уничтожение
    PAIMark &pDel = pDeleteLists[(pNew->nTime - 1) & (N_MAX_MARK_LIFETIME - 1)][nPlayerNum];
    pNew->pNextDel = pDel;
    pDel = pNew;
    return pNew;
}

/////////////////////////////////////////////////////////////////////////////////////
// сила оставл€емых следов рассчитываетс€ по параметрам юнита и типу террейна
SAIMark *CAIMarkManager::AddFootStep(const CPlot3D &pos, CUnitServer *pUnitServer)
{
    SAITileInfo &footstepTile = theWorldServer.aiMap.GetTileInfo(pos);
    ProduceSoundFootstep(pUnitServer,
                         pUnitServer->GetFSSoundDetect() * footstepTile.fpFootstepStrengths[2]);
    return 0; // CRAP now we have no footsteps
    /*
            CUnit *pUnit = pUnitServer->GetUnit();
            SAIMark *pRes = MakeNewMark( pos, pUnit->GetPlayerNum() );
            if ( pRes )
            {
                    // because time in table is in seconds for smelling and we use segments here we
       got
                    // to multiply strength of footsteps by factor of 15
                    // this strength is a multiplier to a feeling power (track/smell power is in
       seconds) pRes->fpStrength[0] = 15 * pUnitServer->GetFSVisionDetect() *
       footstepTile.fpFootstepStrengths[0]; pRes->fpStrength[1] = 15 *
       pUnitServer->GetFSSmellDetect() * footstepTile.fpFootstepStrengths[1]; pRes->nUnitID =
       pUnit->GetID(); pRes->nPlayer = pUnit->GetPlayerNum();
            }
            return pRes;*/
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIMarkManager::RemoveFootSteps(CUnitServer *pUnitServer)
{
    int i, nPlayerIdx, nID;
    nPlayerIdx = pUnitServer->GetUnit()->GetPlayerNum() & (N_AI_MARKS_PLAYER_NUM - 1);
    nID = pUnitServer->GetUnit()->GetID();
    for (i = 0; i < N_MAX_MARK_LIFETIME; i++)
    {
        SAIMark **ppPrev, *pCur;
        ppPrev = &pDeleteLists[i][nPlayerIdx];
        pCur = ppPrev[0];
        while (pCur)
        {
            if (pCur->nUnitID == nID)
            {
                // удалим след
                ppPrev[0] = pCur->pNextDel;
                if (pCur->pPrev)
                    pCur->pPrev->pNext = pCur->pNext;
                else
                {
                    SAIMarkCell *pCell;
                    pCell = GetCell(pCur->ptPosition.x, pCur->ptPosition.y);
                    ASSERT(pCell);
                    ASSERT(pCell[0][nPlayerIdx] == pCur);
                    if (pCell[0][nPlayerIdx])
                        pCell[0][nPlayerIdx] = pCur->pNext;
                }
                if (pCur->pNext)
                    pCur->pNext->pPrev = pCur->pPrev;
                pCur->ReleaseMark();
                pCur = ppPrev[0];
            }
            else
            {
                ppPrev = &pCur->pNextDel;
                pCur = pCur->pNextDel;
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMarkManager::ProduceSoundFootstep(CUnitServer *pUnitSrc, float fpStrength, int nDelay,
                                          const CPlot3D *pSoundPos)
{
    POSITION pos;
    CUnitList heard;
    float fpSense;
    CUnit *pSrc = pUnitSrc->GetUnit();
    const CPlot3D &soundPos = pSoundPos ? *pSoundPos : pSrc->GetCP();
    if(auto* rider=pSrc->MountedRider())pSrc=rider;

    pAIMap->GetUnitsNear(soundPos, heard, 50);
    pos = heard.GetHeadPosition();
    while (pos)
    {
        CUnit *pUnit = heard.GetNext(pos);
        if (pUnit->IsEnemy(pSrc))
        {
            int nMarkPower;
            CAIUnitEnvironment &env = ((CUnitServer *)pUnit->GetUnitSpecific())->logic.environment;

            // check if the mark is strong enough to be more interesting then current point of
            // interest this is an optimization
            nMarkPower = AI_PPOWER_SOUND;
            if (nMarkPower <= env.GetPoIPower())
                continue;

            fpSense = ((CUnitServer *)pUnit->GetUnitSpecific())->GetFSSoundSense();
            if (RollDetect(fabs(soundPos - pUnit->GetCP()), fpStrength * fpSense))
            {
                CString szText;
                if (NConsole::eCurrentFilter & NConsole::AI)
                {
                    szText.Format("Unit %i heard unit %i from %fm distance", pUnit->GetID(),
                                  pSrc->GetID(), fabs(pUnit->GetCP() - pSrc->GetCP()));
                }
                env.HearSound(pSrc->GetCP(), nDelay, szText);
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// perception of footsteps and enemies or ally corpses :)
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAIMarkManager::RollVisionLow(const CPlot3D &ptToTarget, float fpDist, float fpSenseRadius,
                                   float fpVisionArc)
{
    if (fpSenseRadius < fpDist)
        return FALSE;
    // check up direction
    if ((ptToTarget * ptUnitDir) < fpVisionArc * fpDist)
        return FALSE;
    fpSenseRadius *= pAIMap->TraceRelief(ptLookFrom, ptLookFrom + ptToTarget);
    return fpSenseRadius * GetNextRnd() > fpDist;
}

// определить виден ли dst с учетом преп€тствий, затухани€ и силы чувства
BOOL CAIMarkManager::RollVision(CPlot3D &dst, float fpSenseRadius, float fpVisionArc)
{
    CPlot3D ptToTarget = dst - ptLookFrom;
    return RollVisionLow(dst - ptLookFrom, fabs(ptToTarget), fpSenseRadius, fpVisionArc);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAIMarkManager::RollDetect(float fpDist, float fpSenseRadius)
{
    return fpSenseRadius * GetNextRnd() > fpDist;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMarkManager::PerceptFootstepList(SAIMark *pMark, CUnitServer *pUnitServer,
                                         CAIUnitEnvironment &env)
{
    BOOL bVisible = FALSE;
    CUnit *pUnit = pUnitServer->GetUnit();
    const int nSmellDelay = pUnitServer->IsAggressive() ? 0 : N_AI_SMELL_START_DELAY;
    for (; pMark; pMark = pMark->pNext)
    {
        int nMarkPower;
        // check if we are interested in the unit, who left this mark
        if (!pUnit->IsEnemy(pMark->nPlayer))
            continue;
        // check if the mark is strong enough to be more interesting then current point of interest
        nMarkPower = AI_PPOWER_TRACKING - (nSegment - pMark->nTime);
        if (nMarkPower <= env.GetPoIPower())
            continue;
        // check if visible via smell/tracking( vision )
        CPlot3D markDir(pMark->ptPosition.x - ptLookFrom.x, pMark->ptPosition.y - ptLookFrom.y, 0);
        float fpDist = fabs(markDir);
        if ((nSegment - pMark->nTime - nSmellDelay > 0) && fpDist < FP_AI_STEPS_TRACKING_DISTANCE)
        {
            if (nSegment - pMark->nTime <= pMark->fpStrength[0] * pUnitServer->GetFSVisionSense() &&
                RollVisionLow(markDir, fpDist, FP_AI_STEPS_TRACKING_DISTANCE + 0.1f,
                              pUnitServer->GetVisionArc()))
            {
                aiLog << "Unit " << pUnit->GetID() << " sees footstep of " << pMark->nUnitID
                      << " from ";
                aiLog << fpDist << "m distance" << std::endl;

                env.AddPoI(cast2D3D(pMark->ptPointPosition), nMarkPower, -1);
            }
            else if (nSegment - pMark->nTime <=
                     pMark->fpStrength[1] * pUnitServer->GetFSSmellSense())
            {
                aiLog << "Unit " << pUnit->GetID() << " smells footstep of " << pMark->nUnitID
                      << " from ";
                aiLog << fpDist << "m distance" << std::endl;

                env.AddPoI(cast2D3D(pMark->ptPointPosition), nMarkPower, -1);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIMarkManager::FootstepPerception(CUnitServer *pUnitServer, CAIUnitEnvironment &env)
{
    int i, k, nGridCount;
    long nFlags = 0;
    CUnit *pUnit = pUnitServer->GetUnit();

    for (k = 0; k < N_MAX_PLAYERS; k++)
    {
        if (pUnit->IsEnemy(k))
            nFlags |= 1 << (k & N_AI_MARKS_PLAYER_NUM - 1);
    }
    ptLookFrom = pUnit->GetUnitHeadPosition();
    float fpX = ptLookFrom.x, fpY = ptLookFrom.y;
    float fpRange = FP_AI_STEPS_TRACKING_DISTANCE; // pUnitServer->GetMaxFootstepRange() *
                                                   // AI_MAX_FOOTSTEP_BONUS;
    SAIGridPoint *pGrid = env.footStepGrid.GetGridPoints(fpRange / N_AI_MARK_CELL_SIZE, nGridCount);
    for (i = 0; i < nGridCount; i++)
    {
        SAIMarkCell *pMark = GetCell(fpX + N_AI_MARK_CELL_SIZE * pGrid[i].nDX,
                                     fpY + N_AI_MARK_CELL_SIZE * pGrid[i].nDY);
        if (!pMark)
            continue;
        for (k = 0; k < N_AI_MARKS_PLAYER_NUM; k++)
        {
            if (nFlags & (1 << k) && pMark[0][k])
                PerceptFootstepList(pMark[0][k], pUnitServer, env);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// CAIMarkManager vision methods
/////////////////////////////////////////////////////////////////////////////////////
void CAIMarkManager::FormLists(CUnit *pUnit, CAIUnitEnvironment &env)
{
    int nGridCount;
    if (--env.nDelayListsUpdate > 0)
        return;

    pUnit->SetFlag(UNIT_RECALCENEMY);

    env.nDelayListsUpdate = N_LISTS_REFRESH_DELAY + (rand() & 3);

    int nUnitCellX = nTileX / pAIMap->GetObjectCellSize(),
        nUnitCellY = nTileY / pAIMap->GetObjectCellSize();
    CMapObject *pExclude = (CMapObject *)pUnit;

    float fpRange = ((CUnitServer *)pUnit->GetUnitSpecific())->GetMaxVisionRange() *
                    AI_MAX_VISION_BONUS;
    SAIGridPoint *pGrid = env.unitGrid.GetGridPoints(fpRange / pAIMap->GetObjectCellSize(),
                                                     nGridCount);

    env.units.RemoveAll();
    for (int i = 0; i < nGridCount; i++)
    {
        // Get units list on the i-th cell surrounding unit
        CUnitList *pList = pAIMap->GetUnitCellList(nUnitCellX + pGrid[i].nDX,
                                                   nUnitCellY + pGrid[i].nDY);
        if (pList == NULL)
            continue;

        POSITION pos = pList->GetHeadPosition();
        while (pos)
        {
            CUnit *pUnit = pList->GetNext(pos);
            if (pUnit == pExclude) // we don't need to add the hero unit itself
                continue;
            env.units.AddHead(pUnit);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// initialize set of variables required for vision
// процесс инициализации разделен на две частей дл€ повышени€ быстродействи€ -
// PrepareVision() и StartUpVision() & WantVision()
void CAIMarkManager::StartUpVision(CUnit *pUnit)
{
    if (pAIMap->TileInBounds(nTileX, nTileY))
        ptLookFrom.z += pAIMap->GetTileHeight(nTileX, nTileY);
    ptLookFrom.z += pUnit->bv.ptMax.z * 1.8f + pUnit->bv.ptCenter.z + pUnit->bv.ptBone.z;
    ptUnitDir = -pUnit->GetYAxis();
    bVisionInitiated = true;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIMarkManager::PrepareVision(CUnit *pUnit)
{
    bVisionInitiated = false;
    ASSERT(pUnit);
    ASSERT(pAIMap);
    ptLookFrom = pUnit->GetCP();
    nTileX = Float2AITile(ptLookFrom.x);
    nTileY = Float2AITile(ptLookFrom.y);
}

/////////////////////////////////////////////////////////////////////////////////////
// определить видимые юниты, представл€ющие интерес
void CAIMarkManager::Scan(CUnit *pUnit, CAIUnitEnvironment &env)
{
    CorePerf::Scope coreTiming(CorePerf::Vision);
    POSITION pos = env.units.GetHeadPosition();
    CUnitServer *pServer = (CUnitServer *)pUnit->GetUnitSpecific();
    BOOL bIsHero = pUnit->IsHero() || pUnit->CheckFlag(UNIT_LOOKATALLIES);
    pUnit->ResetFlag(UNIT_LOOKATALLIES);
    while (pos != NULL)
    {
        CUnit *pMOUnit = (CUnit *)env.units.GetNext(pos);
        CUnitServer *pTargetServer;
        BOOL bDead = pMOUnit->IsDead();
        BOOL bEnemy = pUnit->IsEnemy(pMOUnit) || pMOUnit->GetOwner();
        BOOL bVisible;
        if (bDead)
        {
            if (!bIsHero && bEnemy)
                continue;
            if (env.seenCorpses.Find(pMOUnit))
                continue;
        }
        else
        {
            if (!bIsHero && !bEnemy)
                continue;
            if (env.visibleUnits.Find(pMOUnit))
                continue;
        }
        WantVision(pUnit);
        CPlot3D ptView = pMOUnit->GetUnitHeadPosition();

        pTargetServer = (CUnitServer *)pMOUnit->GetUnitSpecific();
        ptView -= ptLookFrom;
        float fpDist = (float)sqrt(sqr(ptView.x) + sqr(ptView.y)); // fabs( ptView );
        // ordinary vision
        bVisible = RollVisionLow(ptView, fpDist,
                                 pTargetServer->GetVisionDetect() * pServer->GetVisionSense(),
                                 pServer->GetVisionArc());
        // peripheral vision
        if (!bVisible && IsPositive(ptView * ptUnitDir))
        {
            bVisible = fpDist < pServer->GetPeripheral();
            if (bVisible)
                aiLog << "Unit " << pUnit->GetID() << " sees unit " << pMOUnit->GetID()
                      << " with peripheral vision" << std::endl;
        }
        if (!bVisible && !bDead)
        {
            // sense life
            if (!bVisible)
            {
                bVisible = RollDetect(fpDist,
                                      pTargetServer->GetLifeDetect() * pServer->GetLifeSense());
                if (bVisible)
                    aiLog << "using sense life ";
            }
        }
        if (bVisible)
        {
            if (bDead)
                env.SeeNewCorpse(pUnit, pMOUnit);
            else
            {
                env.visibleUnits.AddHead(pMOUnit);

                aiLog << "Unit " << pUnit->GetID() << " sees unit " << pMOUnit->GetID() << " from ";
                aiLog << fabs(pUnit->GetCP() - pMOUnit->GetCP()) << "m distance" << std::endl;
                // CRAP !!! to do - make something about the fact that we see an enemy ?
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// main function, refreshing unit`s environment
// ASI f06: one WORD counter per observer, shared by the visible-list traversal.
// The owner chose separate runtime storage instead of corrupting fExpPenalty.
static bool KeepSpellAddonUnseenUnit(CUnitServer *pObserver, float fpDist,
                                    const ModSpellAddon::Profile &profile)
{
    if (pObserver->GetUnit()->GetPlayerNum() == 0)
        return false;
    const WORD counter = WORD(pObserver->dwSpellAddonLostSight);
    if (counter >= WORD(profile.Int(ModSpellAddon::IntKey::gameplay_losehardcore)))
    {
        pObserver->dwSpellAddonLostSight &= 0xffff0000u;
        return false;
    }
    // FISTP writes a signed WORD into a zero DWORD, without sign extension.
    // Preserve the current rounding and the indefinite WORD 8000h, zero-extended.
    const DWORD distance = static_cast<uint16_t>(NumericPolicy::RoundToInt16(fpDist));
    const DWORD factor1 = DWORD(int(static_cast<signed char>(profile.Int(ModSpellAddon::IntKey::gameplay_losefactor1))));
    const DWORD factor2 = DWORD(int(static_cast<signed char>(profile.Int(ModSpellAddon::IntKey::gameplay_losefactor2))));
    const DWORD factor3 = DWORD(int(static_cast<signed char>(profile.Int(ModSpellAddon::IntKey::gameplay_losefactor3))));
    const DWORD quotient = (distance * factor1 + factor3) /
                           (DWORD(pObserver->GetUnit()->GetLevel()) + factor2);
    const DWORD base = DWORD(theSettings.nDifficultyLevel) +
                      DWORD(int(static_cast<signed char>(profile.Int(ModSpellAddon::IntKey::gameplay_difffactor))));
    const int maxadd = int(static_cast<signed char>(profile.Int(ModSpellAddon::IntKey::gameplay_maxadd)));
    // The ASI uses signed JL here; q < maxadd adds base, not base * maxadd.
    const DWORD increment = int(quotient) >= maxadd ? base * quotient : base;
    pObserver->dwSpellAddonLostSight = (pObserver->dwSpellAddonLostSight & 0xffff0000u) |
                                       WORD(counter + increment);
    // The final mode-6 patch always jumps over RDTSC, even with follow != 0.
    return true;
}

void CAIMarkManager::Feel(CUnitServer *pHero, CAIUnitEnvironment &env)
{
    POSITION pos, posCheck;
    CUnit *pUnit = pHero->GetUnit();
    ASSERT(pUnit);
    bool bHero = pUnit->IsHero() != FALSE;
    const CModPackage *mod = ModRuntime::Current();
    const bool spellAddonPursuit = mod && mod->SpellAddon().Feature06() == 6;
    float fpLostVisionCoef = bHero ? 1 : (spellAddonPursuit ?
        mod->SpellAddon().Scalar(ModSpellAddon::ScalarKey::gameplay_visiondependence) :
        FP_AI_LOST_FROM_SIGHT_MUL);
    if (!pUnit)
        return;
    const bool deagrOnLostSight = ModRuntime::Current() &&
                                  ModRuntime::Current()->CombatSetting("DeagrPlayerOnLoose");
    PrepareVision(pUnit);
    FormLists(pUnit, env);
    // remove visible units that are out of range
    pos = env.visibleUnits.GetHeadPosition();
    while (pos)
    {
        posCheck = pos;
        CUnit *pVUnit = env.visibleUnits.GetNext(pos);
        CUnitServer *pTarget = (CUnitServer *)pVUnit->GetUnitSpecific();
        float fpDist, fpVision;

        fpDist = fabs(pVUnit->GetCP() - pUnit->GetCP());

        if (pVUnit->IsDead())
        {
            if (theWorldServer.fof.IsPlayerAlly(pUnit->GetPlayerNum(), pVUnit->GetPlayerNum()))
            {
                // friendly юнит умер на наших глазах !
                env.seenCorpses.AddTail(pVUnit);
                env.AddPoI(pVUnit->GetCP(), AI_PPOWER_SEECORPSE, AI_PPDEC_SEECORPSE);
            }
            env.SeeNewCorpse(pUnit, pVUnit);
        }
        else
        {
            const bool enemy = pUnit->IsEnemy(pVUnit) != FALSE;
            if (spellAddonPursuit)
            {
                // f06 owns the final f05 NOP as well: attackers get both sense tests.
                fpVision = pHero->GetVisionSense() * pTarget->GetVisionDetect() * fpLostVisionCoef;
                if (fpVision > fpDist)
                    continue;
                fpVision = pHero->GetLifeSense() * pTarget->GetLifeDetect() * fpLostVisionCoef;
                if (fpVision > fpDist)
                    continue;
                if ((enemy || !mod->SpellAddon().Int(ModSpellAddon::IntKey::gameplay_loseallyfast)) &&
                    KeepSpellAddonUnseenUnit(pHero, fpDist, mod->SpellAddon()))
                    continue;
            }
            // Preserve the previous EIRebuild path when f06 mode 6 is inactive.
            else if (!enemy || bHero)
            {
                // unit attacking us should remain visible
                if (!deagrOnLostSight && pTarget->IsAggressive() && pTarget->logic.GetCurrentTarget() == pUnit)
                    continue;
                // а не пропал ли юнит из вида ?
                fpVision = pHero->GetVisionSense() * pTarget->GetVisionDetect() * fpLostVisionCoef;
                if (fpVision > fpDist)
                    continue;
                fpVision = pHero->GetLifeSense() * pTarget->GetLifeDetect() * fpLostVisionCoef;
                if (fpVision > fpDist)
                    continue;
                // expensive check
                if (!deagrOnLostSight && pTarget->logic.GetAggressionTarget() == pUnit)
                    continue;
            }
            else
                continue;
        }
        aiLog << "Unit " << pUnit->GetID() << " lost from sight unit " << pVUnit->GetID()
              << std::endl;
        if (pUnit->IsEnemy(pVUnit))
            env.AddPoI(pVUnit->GetCP(), AI_PPOWER_SEEENEMY, AI_PPDEC_SEEENEMY);
        if (deagrOnLostSight) pHero->CancelFollowOnLostSight(pVUnit);
        env.visibleUnits.RemoveAt(posCheck);
    }
    // perform vision tests
    Scan(pUnit, env);
    // scan for footsteps
    // CRAP now we have no footsteps perceptions (tracking/smelling)
    // FootstepPerception( pHero, env );
}

/////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////
// CAIUnitEnvironment
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitEnvironment::Clear()
{
    units.RemoveAll();
    visibleUnits.RemoveAll();
    seenCorpses.RemoveAll();
    freshCorpses.RemoveAll();
    // objects.RemoveAll();
    ptPointOfInterest = CPlot2D(0, 0);
    nPoIPower = 0;
    // bMoved = TRUE;
    nDelayListsUpdate = 0;
    ignoredList.Clear();
    delayedSounds.Clear();
    nPoIPowerDecrease = 0;
}
/////////////////////////////////////////////////////////////////////////////////////
// find nearest non dead, not ignored enemy
void CAIUnitEnvironment::GetTarget(CUnit *pAttacker, CUnitList &res)
{
    GetTarget(pAttacker, pAttacker->GetCP(), res);
}
/////////////////////////////////////////////////////////////////////////////////////
// like GetTarget() but nearest to ptNear
void CAIUnitEnvironment::GetTarget(CUnit *pAttacker, const CPlot3D &ptNear, CUnitList &res)
{
    ASSERT(pAttacker);
    res.RemoveAll();

    for (POSITION pos = visibleUnits.GetHeadPosition(); pos;)
    {
        CUnit *pUnit = visibleUnits.GetNext(pos);
        if (!pAttacker->IsEnemy(pUnit))
            continue;
        if (pUnit->IsDead())
            continue;
        if (!ignoredList.IsIgnored(pUnit))
            res.AddTail(pUnit);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// return nearest non ignored aggressive enemy
void CAIUnitEnvironment::GetAggressiveTarget(CUnit *pAttacker, CUnitList &res,
                                             const CPlot3D &ptSwarm, float fpSwarmRadius)
{
    ASSERT(pAttacker);
    res.RemoveAll();
    fpSwarmRadius = fpSwarmRadius > 0 ? sqr(fpSwarmRadius) : -1;

    for (POSITION pos = visibleUnits.GetHeadPosition(); pos;)
    {
        CUnit *pUnit = visibleUnits.GetNext(pos);
        if (!pUnit->IsEnemy(pAttacker))
            continue;
        if (pUnit->IsDead())
            continue;
        if (ignoredList.IsIgnored(pUnit))
            continue;
        // swarm targets are welcome :)
        if (fabs2(pUnit->GetCP(), ptSwarm) >= fpSwarmRadius)
        {
            // test if this unit attacks Attacker`s ally
            CUnitServer *pUS = (CUnitServer *)pUnit->GetUnitSpecific();
            CUMAggression *pAggr = (CUMAggression *)pUS->logic.FindMotivation(AI_UM_AGGRESSION);
            if (!pAggr || (pAggr->IsRevenge() && pAggr->GetTarget() == 0))
                continue;
            // if potential target attacks someone else do not disturb it
            CMapObject *pTarg = pUS->logic.GetAggressionTarget();
            if (pTarg && pTarg->WhoAmI() == HPC_UNIT)
            {
                if (!theWorldServer.fof.IsPlayerAlly(pAttacker->GetPlayerNum(),
                                                     ((CUnit *)pTarg)->GetPlayerNum()))
                    continue;
            }
            if (!pUS->logic.bEnemyVisible)
                continue;
        }
        res.AddTail(pUnit);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// find simply nearest enemy
CUnit *CAIUnitEnvironment::GetNearestEnemy(CUnit *pAttacker)
{
    float fpWeight = 1e30f;
    CUnit *pAim = NULL;
    ASSERT(pAttacker);

    for (POSITION pos = visibleUnits.GetHeadPosition(); pos;)
    {
        float fpNew;
        CUnit *pUnit = visibleUnits.GetNext(pos);
        if (!pAttacker->IsEnemy(pUnit))
            continue;
        if (pUnit->IsDead())
            continue;
        fpNew = fabs2(pAttacker->GetCP() - pUnit->GetCP());
        if (fpNew < fpWeight)
        {
            pAim = pUnit;
            fpWeight = fpNew;
        }
    }
    return pAim;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitEnvironment::Segment(CUnitServer *pHero)
{
    ignoredList.Segment();
    delayedSounds.Segment(pHero->GetUnit(), *this);
    theWorldServer.aiMap.marks.Feel(pHero, *this);
    nPoIPower += nPoIPowerDecrease;
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAIUnitEnvironment::IsUnitVisible(CUnit *pWatcher, CUnit *pUnit)
{
    if (pWatcher->GetPlayerNum() == pUnit->GetPlayerNum() ||
        pWatcher->GetOwner() == pUnit->GetOwner())
        return TRUE;
    return (visibleUnits.Find(pUnit) != NULL);
}
/////////////////////////////////////////////////////////////////////////////////////
// calcs probability to be noted this segment
void CAIUnitEnvironment::CalcNotability(CUnitServer *pHero, SUnitVisibilityInfo &res)
{
    res.fpSight = AI_MAX_VISIBILITY_INFO_MULT;
    res.fpSmell = AI_MAX_VISIBILITY_INFO_MULT;
    res.fpSound = AI_MAX_VISIBILITY_INFO_MULT;
    res.fpSenseLife = AI_MAX_VISIBILITY_INFO_MULT;
    res.fpTracking = AI_MAX_VISIBILITY_INFO_MULT;
    POSITION pos = units.GetHeadPosition();
    CPlot3D ptViewFrom = pHero->GetUnit()->GetUnitHeadPosition();
    while (pos)
    {
        CUnit *pUnit = units.GetNext(pos);
        CPlot3D ptUnitHead;
        float fpDist, fpVisionDist, fpTrace; //, fpVisionDistI
        if (pUnit->IsDead())
            continue;
        // if ( !theWorldServer.fof.IsPlayerAlly( pHero->GetUnit()->GetPlayerNum(),
        // pUnit->GetPlayerNum() ) )
        if (pHero->GetUnit()->IsEnemy(pUnit))
        {
            CUnitServer *pUnitServer = (CUnitServer *)pUnit->GetUnitSpecific();
            fpDist = fabs(pUnit->GetCP() - pHero->GetUnit()->GetCP());
            fpVisionDist = pUnitServer->GetVisionSense() * pHero->GetVisionDetect();
            // fpVisionDistI = pUnitServer->GetFSVisionSense() * pHero->GetFSVisionDetect();
            if (fpDist < fpVisionDist) //|| fpDist < fpVisionDistI )
            {
                ptUnitHead = pUnit->GetUnitHeadPosition();
                fpTrace = theWorldServer.aiMap.TraceRelief(ptViewFrom, ptUnitHead);

                res.fpSight = Min(res.fpSight, GetVisionRatio(fpDist, fpVisionDist * fpTrace));
                // res.fpTracking = Min( res.fpTracking, GetVisionRatio( fpDist, fpVisionDistI *
                // fpTrace ) );
            }
            res.fpSenseLife = Min(res.fpSenseLife,
                                  GetVisionRatio(fpDist, pUnitServer->GetLifeSense() *
                                                             pHero->GetLifeDetect()));
            res.fpSmell = Min(res.fpSmell, GetVisionRatio(fpDist, pUnitServer->GetFSSmellSense() *
                                                                      pHero->GetFSSmellDetect()));
            res.fpSound = Min(res.fpSound, GetVisionRatio(fpDist, pUnitServer->GetFSSoundSense() *
                                                                      pHero->GetFSSoundDetect()));
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitEnvironment::HearSound(const CPlot3D &ptInterest, int nDelay,
                                   const char *pszDescription)
{
    delayedSounds.AddSound(ptInterest, nDelay, pszDescription);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitEnvironment::HearNewEnemies(DWORD dwEnemyMask, int nDelay, const char *pszDescription)
{
    delayedSounds.AddNewEnemies(dwEnemyMask, nDelay, pszDescription);
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAIUnitEnvironment::AddPoI(const CPlot3D &pos, int nPower, int nDecrease)
{
    if (nPower > nPoIPower)
    {
        BOOL bRv;
        bRv = (nPower - nPoIPower) > 100 || fabs2(pos - cast2D3D(ptPointOfInterest)) > 10;
        ptPointOfInterest = cast3D2D(pos);
        nPoIPower = nPower;
        nPoIPowerDecrease = nDecrease;
        return bRv;
    }
    return FALSE;
}
/////////////////////////////////////////////////////////////////////////////////////
// перед добавлением в список видимых юнитов провер€етс€ не пропадет ли этот юнит после
// первого же сегмента, если пропадет, то он и не добавл€етс€, функци€ работает корректно
// только дл€ юнитов принадлежащих игроку
void CAIUnitEnvironment::AddVisibleUnit(CUnit *pUnit, CUnit* pVisible)
{
    if (visibleUnits.Find(pVisible))
        return;
    CUnitServer *pHero = (CUnitServer *)pUnit->GetUnitSpecific();
    CUnitServer *pTarget = (CUnitServer *)pVisible->GetUnitSpecific();
    float fpDist, fpVision;
    fpDist = fabs(pUnit->GetCP() - pVisible->GetCP());

    fpVision = pHero->GetVisionSense() * pTarget->GetVisionDetect();
    if (pTarget->logic.IsEngaging(pUnit))
    {
        // pVisible is attacking pUnit, so it should be visible
        ;
    }
    else
    {
        if (fpVision > fpDist)
        {
            fpVision = pHero->GetLifeSense() * pTarget->GetLifeDetect();
            if (fpVision > fpDist)
                return;
        }
    }
    if (pVisible->IsDead())
        SeeNewCorpse(pUnit, pVisible);
    else
    {
        visibleUnits.AddHead(pVisible);
        aiLog << "Unit " << pUnit->GetID() << " is forced to see unit " << pVisible->GetID()
              << " from ";
        aiLog << fpDist << "m distance" << std::endl;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitEnvironment::SeeNewCorpse(CUnit *pUnit, CUnit *pCorpse)
{
    aiLog << "Unit " << pUnit->GetID() << " sees corpse of unit " << pCorpse->GetID() << std::endl;
    seenCorpses.AddTail(pCorpse);
    if (theWorldServer.fof.IsPlayerAlly(pUnit->GetPlayerNum(), pCorpse->GetPlayerNum()))
        freshCorpses.AddTail(pCorpse);
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIUnitEnvironment::RemoveReferencesOn(CMapObject *pTarget)
{
    POSITION pos;
    CMapObjectPtr pTarg(pTarget);
    while (pos = units.Find(pTarg))
        units.RemoveAt(pos);
    while (pos = visibleUnits.Find(pTarg))
        visibleUnits.RemoveAt(pos);
    while (pos = seenCorpses.Find(pTarg))
        seenCorpses.RemoveAt(pos);
    while (pos = freshCorpses.Find(pTarg))
        freshCorpses.RemoveAt(pos);
    ignoredList.RemoveReferencesOn(pTarget);
}

/////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// CAIIgnoredTargetsList
/////////////////////////////////////////////////////////////////////////////////////
bool CAIIgnoredTargetsList::IsIgnored(CMapObject *pTarget)
{
    for (int i = 0; i < ignoredList.GetSize(); i++)
    {
        if (ignoredList[i].pTarget == pTarget)
            return true;
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIIgnoredTargetsList::Segment()
{
    for (int i = 0; i < ignoredList.GetSize(); i++)
    {
        if (--ignoredList[i].nTime < 0)
            ignoredList.RemoveAt(i--);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIIgnoredTargetsList::AddIgnoredTarget(CMapObject *pTarget, int nTime)
{
    if (!pTarget)
        return;
    for (int i = 0; i < ignoredList.GetSize(); i++)
    {
        if (ignoredList[i].pTarget == pTarget)
        {
            ignoredList[i].nTime = Max(ignoredList[i].nTime, nTime);
            return;
        }
    }
    ignoredList.Add(SIgnoredTarget(pTarget, nTime));
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIIgnoredTargetsList::RemoveReferencesOn(CMapObject *pTarget)
{
    for (int i = 0; i < ignoredList.GetSize(); i++)
    {
        if (ignoredList[i].pTarget == pTarget)
            ignoredList.RemoveAt(i--);
    }
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// CAIDelayedSoundsList
/////////////////////////////////////////////////////////////////////////////////////
void CAIDelayedSoundsList::Segment(CUnit *pUnit, CAIUnitEnvironment &env)
{
    for (int i = 0; i < soundsQueue.GetSize(); i++)
    {
        if (--soundsQueue[i].nDelay < 0)
        {
            if (soundsQueue[i].dwNewEnemies)
            {
                aiLog << "Unit " << pUnit->GetID()
                      << " hear delayed sound: " << soundsQueue[i].szDescription << std::endl;
                // information about enemies
                ((CUnitServer *)pUnit->GetUnitSpecific())
                    ->HearNewEnemiesMsg(soundsQueue[i].dwNewEnemies);
            }
            else
            {
                // simple point of interest
                if (env.AddPoI(soundsQueue[i].ptSrc, AI_PPOWER_SOUND, AI_PPDEC_SOUND))
                    aiLog << "Delayed sound: " << soundsQueue[i].szDescription << std::endl;
            }
            soundsQueue.RemoveAt(i--);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIDelayedSoundsList::RemoveDiplomacyMsgs()
{
    for (int i = 0; i < soundsQueue.GetSize(); i++)
    {
        if (soundsQueue[i].dwNewEnemies)
            soundsQueue.RemoveAt(i--);
    }
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
void CAIDelayedSoundsList::AddSound(const CPlot3D &ptSrc, int nDelay, const char *pszText)
{
    // удалим старые звуки, которые должны были быть услышаны после нового звука
    for (int i = 0; i < soundsQueue.GetSize(); i++)
    {
        if (soundsQueue[i].dwNewEnemies)
            continue;
        if (soundsQueue[i].nDelay >= nDelay)
            soundsQueue.RemoveAt(i--);
    }
    soundsQueue.Add(SSound(ptSrc, nDelay, pszText));
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIDelayedSoundsList::AddNewEnemies(DWORD dwNewEnemies, int nDelay, const char *pszText)
{
    SSound n;
    n.nDelay = nDelay;
    n.dwNewEnemies = dwNewEnemies;
    n.szDescription = pszText;
    soundsQueue.Add(n);
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// функци€ вычисл€ет на каких тайлах pWatcher может заметить объект pTarget с помощью
// надежного чувства
void BuildVisionMask(CUnit *pWatcher, CUnit *pTarget, SVisibilityMask &res)
{
    ASSERT(pWatcher);
    ASSERT(pTarget);
    CPlot3D ptUnitDir = -pWatcher->GetYAxis();
    CPlot3D ptLookFrom = pWatcher->GetUnitHeadPosition();
    CAIMap &map = theWorldServer.aiMap;
    res.Clear();

    // ordinary vision
    float fpSenseRadius = ((CUnitServer *)pWatcher->GetUnitSpecific())->GetVisionSense() *
                          ((CUnitServer *)pTarget->GetUnitSpecific())->GetVisionDetect();
    float fpVisionArc = ((CUnitServer *)pWatcher->GetUnitSpecific())->GetVisionArc();
    fpSenseRadius = Max(0.0f, fpSenseRadius);
    map.TraceVisionToArea(ptLookFrom, fpSenseRadius, ptUnitDir, fpVisionArc,
                          pTarget->GetUnitHeadHeight(), res);

    // sense life
    float fpSenseLifeRadius = ((CUnitServer *)pWatcher->GetUnitSpecific())->GetLifeSense() *
                              ((CUnitServer *)pTarget->GetUnitSpecific())->GetLifeDetect();
    if (fpSenseLifeRadius != 0)
        map.TraceVisionToArea(ptLookFrom, fpSenseLifeRadius, ptUnitDir, -1, -1000, res);

    // peripheral vision
    float fpPeripheralSenseRadius = ((CUnitServer *)pWatcher->GetUnitSpecific())->GetPeripheral();
    if (fpPeripheralSenseRadius != 0)
        map.TraceVisionToArea(ptLookFrom, fpPeripheralSenseRadius, ptUnitDir, 0, -1000, res);
}
/////////////////////////////////////////////////////////////////////////////////////

// Explicit local visibility payload: origin, dimensions and one byte per cell.
void SVisibilityMask::StoreLocal(CSave &file) const
{
    const int x = data.GetXSize(), y = data.GetYSize();
    const auto cells = static_cast<std::uint64_t>(x) * y;
    if (x <= 0 || y <= 0 || cells > INT32_MAX)
        throw SaveSchema::Error(SaveSchema::ErrorCode::Limit, "local visibility dimensions");
    file.Write(nShiftX); file.Write(nShiftY);
    file.Write(x); file.Write(y);
    file.Write(data.GetBuffer(), static_cast<UINT>(cells));
}
bool SVisibilityMask::LoadLocal(CSave &file)
{
    int shiftX = 0, shiftY = 0, x = 0, y = 0;
    file.Read(shiftX); file.Read(shiftY);
    file.Read(x); file.Read(y);
    const auto cells = static_cast<std::uint64_t>(x) * y;
    if (!file.IsOk() || x <= 0 || y <= 0 || cells > INT32_MAX || cells != file.Remaining())
    {
        file.Fail("invalid_local_visibility_dimensions");
        return false;
    }
    data.SetSizes(x, y);
    if (file.Read(data.GetBuffer(), static_cast<UINT>(cells)) != cells || !file.IsOk()) return false;
    nShiftX = shiftX; nShiftY = shiftY;
    return true;
}
