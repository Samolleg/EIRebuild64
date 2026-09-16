#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiPathFinder.h"
#include "aiMap.h"
#include "unit.h"

/////////////////////////////////////////////////////////////////////////////////////
#ifndef _DEBUG
#pragma optimize("agw", on)
#endif
/////////////////////////////////////////////////////////////////////////////////////
EAIDirection AINumberToDir[9] = { DOWNLEFT, DOWN,   DOWNRIGHT, LEFT,   NULL_DIR,
                                  RIGHT,    UPLEFT, UP,        UPRIGHT };

/////////////////////////////////////////////////////////////////////////////////////
// CWavePathFinder &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
// размер массива для поиска путей - с непроходимыми границами вместе
const int N_ARRAY_WAVE_SIZE = N_MAP_WAVE_SEARCH_SIZE + 2;

/////////////////////////////////////////////////////////////////////////////////////
int CWavePathFinder::GetPassTime(SPTWave *pDst, int nMult)
{
    if (!pDst->bCanPass)
        return -1;
    if (bBack)
    {
        {
            SAITile &srcTile = tiles[pDst->nY][pDst->nX];
            SAITile &dstTile = tiles[pSrc->nY][pSrc->nX];
            int nCost = dstTile.GetTerrainCost(costsInfo), nMul;
            if (nCost < 0)
                return -1;
            if (srcTile.IsLocked(nLockCheck) && srcTile.nLocked >= dstTile.nLocked)
                return -1;
            nMul = costsInfo.pElevationCosts[dstTile.nHeight - srcTile.nHeight];
            if (nMul < 0)
                return -1;
            nCost = (nCost * nMul) >> 10;
            nCost = (nCost * nMult) >> 10;
            return nCost;
        }
    }
    SAITile &dstTile = tiles[pDst->nY][pDst->nX];
    SAITile &srcTile = tiles[pSrc->nY][pSrc->nX];
    if (dstTile.IsLocked(nLockCheck) && dstTile.nLocked >= srcTile.nLocked)
        return -1;
    int nCost = dstTile.GetTerrainCost(costsInfo), nMul;
    if (nCost < 0)
        return -1;
    nMul = costsInfo.pElevationCosts[dstTile.nHeight - srcTile.nHeight];
    if (nMul < 0)
        return -1;
    nCost = (nCost * nMul) >> 10;
    nCost = (nCost * nMult) >> 10;
    return nCost;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::SetMap(CAIMap *pMp, bool bBackWave)
{
    int x, y;
    pMap = pMp;
    bBack = bBackWave;
    ASSERT(N_ARRAY_WAVE_SIZE == N_MAP_WAVE_SEARCH_SIZE + 2);
    waves.SetSizes(N_ARRAY_WAVE_SIZE, N_ARRAY_WAVE_SIZE);
    for (y = 0; y < N_ARRAY_WAVE_SIZE; y++)
    {
        for (x = 0; x < N_ARRAY_WAVE_SIZE; x++)
        {
            waves[y][x].bCanPass = 1;
            if (x == 0 || x == N_ARRAY_WAVE_SIZE - 1 || y == 0 || y == N_ARRAY_WAVE_SIZE - 1)
                waves[y][x].bCanPass = 0;
            waves[y][x].bPermanent = false;
            waves[y][x].nCost = N_INFINITE;
            waves[y][x].nX = x;
            waves[y][x].nY = y;
            waves[y][x].pNext = NULL;
            waves[y][x].pPrev = NULL;
            waves[y][x].backDir = NULL_DIR;
        }
    }
    tiles.SetSize(N_ARRAY_WAVE_SIZE);
    pDead = NULL;
    bStarted = false;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::ClearWaveUnits(SPTWave *pTile)
{
    while (pTile)
    {
        SPTWave *pNxt = pTile->pNext;
        pTile->nCost = N_INFINITE;
        pTile->pNext = 0;
        pTile->pPrev = 0;
        pTile->backDir = NULL_DIR;
        pTile->bPermanent = false;
        pTile->bCanPass = 1;
        pTile = pNxt;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
inline void CWavePathFinder::FindNewSrc()
{
    // pFst = pFirst;
    int nMin = N_INFINITE;
    pSrc = NULL;

    ASSERT(N_WAVE_LIST_SPLIT_NUMBER == 4);
#define CheckOneFirst(first)          \
    if (first && first->nCost < nMin) \
    {                                 \
        nMin = first->nCost;          \
        pSrc = first;                 \
    }
    CheckOneFirst(pFirst4[0]);
    CheckOneFirst(pFirst4[1]);
    CheckOneFirst(pFirst4[2]);
    CheckOneFirst(pFirst4[3]);
}

/////////////////////////////////////////////////////////////////////////////////////
inline void CWavePathFinder::AddActiveNode(SPTWave *pDst, int nCost)
{
    PPTWave &pFirst = pFirst4[pDst->nX & (N_WAVE_LIST_SPLIT_NUMBER - 1)];

    if (pDst == pFirst)
        return;
    // remove from list if its already in it
    if (pDst->pNext)
        pDst->pNext->pPrev = pDst->pPrev;
    if (pDst->pPrev)
        pDst->pPrev->pNext = pDst->pNext;

    ASSERT(pDead != pDst);
    // if ( pDead == pDst) pDead = pDst;
    pDst->pNext = NULL;
    pDst->pPrev = NULL;
    // insert into list on new place, keeps list sorted
    if (pFirst == NULL)
    {
        pDst->pNext = pFirst;
        pFirst = pDst;
        ASSERT(pDst != pDst->pNext);
    }
    else if (pFirst->nCost >= nCost)
    {
        pDst->pNext = pFirst;
        pFirst->pPrev = pDst;
        pFirst = pDst;
        ASSERT(pDst != pDst->pNext);
    }
    else
    {
        SPTWave *pTry = pFirst;
        // int count = 0;
        while (pTry->pNext && pTry->pNext->nCost < nCost)
            pTry = pTry->pNext;
        //{ pTry = pTry->pNext; count++;}
        // avrgleng+=count; avrgcount++;

        ASSERT(pTry->pNext != pDst);
        ASSERT(pTry != pTry->pNext);

        pDst->pNext = pTry->pNext;
        if (pTry->pNext)
            pTry->pNext->pPrev = pDst;
        pTry->pNext = pDst;
        pDst->pPrev = pTry;

        ASSERT(pDst != pDst->pNext);
    }
}
/*inline int UnderEstimate( SPTWave *pSrc, SPTWave *pFinal ) // for alpha-star optimization
{
        return ManhDistance( pSrc->nX, pSrc->nY, pFinal->nX, pFinal->nY ) * 100;
}*/

// int avrgleng = 0, avrgcount = 0;

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::TryStep(SPTWave *pDst, EAIDirection backDir, int nMult)
{
    int nDelay, nCost;
    if (nCurrentCost >= pDst->nCost)
        return;
    nDelay = GetPassTime(pDst, nMult);
    if (nDelay < 0)
        return;
    nCost = nDelay + nCurrentCost; // + UnderEstimate( pDst, pFinalTile );
    if (nCost < pDst->nCost)
    {
        pDst->nCost = nCost;
        pDst->backDir = backDir;
        AddActiveNode(pDst, nCost);
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::SetUnitInfo(CUnit *pUnit)
{
    pMap->GetRouteCostsPathFind(pUnit, costsInfo);
    nCurrentUnitClass = pUnit->GetAIClass();
    nLockCheck = pMap->GetUnitLockCheck(pUnit);
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::SetUnitInfo(int nAIClass)
{
    pMap->GetRouteCosts(nAIClass, costsInfo);
    nCurrentUnitClass = nAIClass;
    nLockCheck = pMap->GetUnitLockCheck(nAIClass); // not a preferred path, precision is too low!
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::StartWave(int nSrcX, int nSrcY)
{
    pSrc = &waves[nSrcY - nYPos][nSrcX - nXPos];
    pSrc->nCost = 0; // UnderEstimate( pSrc, pFinalTile );
    pSrc->backDir = NULL_DIR;
    pSrc->bPermanent = true;

    for (int i = 0; i < N_WAVE_LIST_SPLIT_NUMBER; i++)
        pFirst4[i] = NULL;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::InitializeTiles()
{
    for (int i = 0; i < N_ARRAY_WAVE_SIZE; i++)
    {
        int nMapY = i + nYPos;
        if (nMapY < 0 || nMapY >= pMap->map.GetYSize())
            continue;
        tiles[i] = (&pMap->map[nMapY][0]) + nXPos;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::CalcOffsets(int nSrcX, int nSrcY, int nDstX, int nDstY)
{
    if (abs(nSrcX - nDstX) >= N_MAP_WAVE_SEARCH_SIZE ||
        abs(nSrcY - nDstY) >= N_MAP_WAVE_SEARCH_SIZE)
        return false;
    if (nSrcX < 0 || nSrcX >= pMap->XSize() || nDstX < 0 || nDstX >= pMap->XSize())
        return false;
    if (nSrcY < 0 || nSrcY >= pMap->YSize() || nDstY < 0 || nDstY >= pMap->YSize())
        return false;
    // find position of waves[][] on ai map
    nXPos = (nSrcX + nDstX) / 2 - N_MAP_WAVE_SEARCH_SIZE / 2;
    nYPos = (nSrcY + nDstY) / 2 - N_MAP_WAVE_SEARCH_SIZE / 2;
    nXPos = Min(nXPos, Min(nSrcX, nDstX) - 1);
    nYPos = Min(nYPos, Min(nSrcY, nDstY) - 1);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::StartSearch(int nSrcX, int nSrcY, int nDstX, int nDstY)
{
    ASSERT(!bStarted);
    if (!CalcOffsets(nSrcX, nSrcY, nDstX, nDstY))
        return false;

    InitializeTiles();
    StartWave(nSrcX, nSrcY);
    bStarted = true;

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::StartSearch(CUnit *pUnit, int nSrcX, int nSrcY, int nDstX, int nDstY)
{
    SetUnitInfo(pUnit);
    return StartSearch(nSrcX, nSrcY, nDstX, nDstY);
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::StartSearch(int nAIClass, int nSrcX, int nSrcY, int nDstX, int nDstY)
{
    SetUnitInfo(nAIClass);
    return StartSearch(nSrcX, nSrcY, nDstX, nDstY);
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::ContinueSearch(int nSrcX, int nSrcY, int nDstX, int nDstY,
                                     bool bStartFromBorders)
{
    ASSERT(bStarted);
    int nPrevXPos = nXPos, nPrevYPos = nYPos;

    if (!CalcOffsets(nSrcX, nSrcY, nDstX, nDstY))
        return false;

    InitializeTiles();
    ASSERT(abs(nXPos - nPrevXPos) < N_MAP_WAVE_SEARCH_SIZE - 1 ||
           abs(nYPos - nPrevYPos) < N_MAP_WAVE_SEARCH_SIZE - 1);
    if (abs(nXPos - nPrevXPos) >= N_MAP_WAVE_SEARCH_SIZE - 1 ||
        abs(nYPos - nPrevYPos) >= N_MAP_WAVE_SEARCH_SIZE - 1)
        return false;

    // shift everything
    // fix nx ny
    // new nodes should be cleared
    // clear links
    int x, y, i;
    CArray2D<SPTWave> save = waves;
    int nMinCost = N_INFINITE;
    for (y = 1; y <= N_MAP_WAVE_SEARCH_SIZE; y++)
    {
        for (x = 1; x <= N_MAP_WAVE_SEARCH_SIZE; x++)
        {
            int pX = x + nXPos - nPrevXPos;
            int pY = y + nYPos - nPrevYPos;
            // reset permanent flags on borders to allow continue search after window shifting
            if (pX < 1 || pY < 1 || pX > N_MAP_WAVE_SEARCH_SIZE || pY > N_MAP_WAVE_SEARCH_SIZE)
            {
                waves[y][x].nCost = N_INFINITE;
                waves[y][x].bPermanent = false;
                waves[y][x].backDir = NULL_DIR;
                ASSERT(waves[y][x].nX == x && waves[y][x].nY == y);
            }
            else
            {
                if (pX == 1 || pX == N_MAP_WAVE_SEARCH_SIZE || pY == 1 ||
                    pY == N_MAP_WAVE_SEARCH_SIZE)
                    nMinCost = Min(nMinCost, save[pY][pX].nCost);
                waves[y][x] = save[pY][pX];
                waves[y][x].nX = x;
                waves[y][x].nY = y;
            }
            waves[y][x].pNext = NULL;
            waves[y][x].pPrev = NULL;
        }
    }
    // form lists ( first / dead )
    pDead = NULL;
    for (i = 0; i < N_WAVE_LIST_SPLIT_NUMBER; i++)
        pFirst4[i] = NULL;
    if (bStartFromBorders)
    {
        // continue with picking up border tiles
        for (y = 1; y <= N_MAP_WAVE_SEARCH_SIZE; y++)
        {
            for (x = 1; x <= N_MAP_WAVE_SEARCH_SIZE; x++)
            {
                if (waves[y][x].nCost == N_INFINITE)
                    continue;
                if (waves[y][x].nCost < nMinCost)
                {
                    waves[y][x].pNext = pDead;
                    waves[y][x].bPermanent = true;
                    pDead = &waves[y][x];
                }
                else
                {
                    waves[y][x].bPermanent = false;
                    AddActiveNode(&waves[y][x], waves[y][x].nCost);
                }
            }
        }
    }
    else
    {
        // ordinary continue without picking up border tiles
        for (y = 1; y <= N_MAP_WAVE_SEARCH_SIZE; y++)
        {
            for (x = 1; x <= N_MAP_WAVE_SEARCH_SIZE; x++)
            {
                if (waves[y][x].bPermanent)
                {
                    waves[y][x].pNext = pDead;
                    pDead = &waves[y][x];
                }
                else if (waves[y][x].nCost < N_INFINITE)
                {
                    AddActiveNode(&waves[y][x], waves[y][x].nCost);
                }
            }
        }
    }
    FindNewSrc();
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::CopyBackDirs()
{
    int x, y, begX, begY, konX, konY;
    begX = Max(1, -nXPos);
    begY = Max(1, -nYPos);
    konX = Min(N_MAP_WAVE_SEARCH_SIZE, pMap->XSize() - nXPos - 1);
    konY = Min(N_MAP_WAVE_SEARCH_SIZE, pMap->YSize() - nYPos - 1);
    for (y = begY; y <= konY; y++)
    {
        for (x = begX; x <= konX; x++)
        {
            if (waves[y][x].nCost < N_INFINITE && pMap->tempMap[y + nYPos][x + nXPos] == NULL_DIR)
                pMap->tempMap[y + nYPos][x + nXPos] = waves[y][x].backDir;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::StartSquare(int nLowX, int nLowY, int nHighX, int nHighY, int nSrcX,
                                  int nSrcY)
{
    int i, nXSize, nYSize;
    ASSERT(!bStarted);
    if (abs(nHighX - nLowX) + 4 > N_MAP_WAVE_SEARCH_SIZE ||
        abs(nHighY - nLowY) + 4 > N_MAP_WAVE_SEARCH_SIZE)
        return false;
    if (nSrcX < nLowX || nSrcX > nHighX || nSrcY < nLowY || nSrcY > nHighY)
        return false;
    nXPos = Min(nHighX, nLowX) - 1;
    nYPos = Min(nHighY, nLowY) - 1;

    // limit square space
    nXSize = abs(nHighX - nLowX) + 2;
    nYSize = abs(nHighY - nLowY) + 2;
    for (i = 1; i < nXSize; i++)
    {
        waves[nYSize][i].bCanPass = 0;
        waves[nYSize][i].pNext = pDead;
        pDead = &waves[nYSize][i];
    }
    for (i = 1; i <= nYSize; i++)
    {
        waves[i][nXSize].bCanPass = 0;
        waves[i][nXSize].pNext = pDead;
        pDead = &waves[i][nXSize];
    }

    InitializeTiles();
    StartWave(nSrcX, nSrcY);
    bStarted = true;

    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::StartSquare(CUnit *pUnit, int nLowX, int nLowY, int nHighX, int nHighY,
                                  int nSrcX, int nSrcY)
{
    SetUnitInfo(pUnit);
    return StartSquare(nLowX, nLowY, nHighX, nHighY, nSrcX, nSrcY);
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::StartSquare(int nAIClass, int nLowX, int nLowY, int nHighX, int nHighY,
                                  int nSrcX, int nSrcY)
{
    SetUnitInfo(nAIClass);
    return StartSquare(nLowX, nLowY, nHighX, nHighY, nSrcX, nSrcY);
}
/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::NextWave()
{
    ASSERT(bStarted);

    int nHash;
    nHash = pSrc->nX & (N_WAVE_LIST_SPLIT_NUMBER - 1);

    pSrc->bPermanent = true;
    pFirst4[nHash] = pSrc->pNext;
    if (pFirst4[nHash])
        pFirst4[nHash]->pPrev = NULL;
    // if(pDead) pDead->pPrev = pSrc;  pSrc->pPrev = NULL; // for alpha star is essential, because
    // it can reuse nodes
    pSrc->pNext = pDead;
    pDead = pSrc;

    nCurrentCost = pSrc->nCost; // - UnderEstimate( pSrc, pFinalTile );

    TryStep(pSrc + N_ARRAY_WAVE_SIZE - 1, DOWNRIGHT, 1448);
    TryStep(pSrc + N_ARRAY_WAVE_SIZE, DOWN, 1024);
    TryStep(pSrc + N_ARRAY_WAVE_SIZE + 1, DOWNLEFT, 1448);
    TryStep(pSrc + 1, LEFT, 1024);
    TryStep(pSrc - 1, RIGHT, 1024);
    TryStep(pSrc - N_ARRAY_WAVE_SIZE - 1, UPRIGHT, 1448);
    TryStep(pSrc - N_ARRAY_WAVE_SIZE, UP, 1024);
    TryStep(pSrc - N_ARRAY_WAVE_SIZE + 1, UPLEFT, 1448);

    FindNewSrc();
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::GetPosition(int &nX, int &nY, int &nCost)
{
    nCost = N_INFINITE;
    if (!pSrc)
        return;
    nX = pSrc->nX + nXPos;
    nY = pSrc->nY + nYPos;
    nCost = pSrc->nCost;
}

/////////////////////////////////////////////////////////////////////////////////////
int CWavePathFinder::GetPermanentCost(int nX, int nY)
{
    nX -= nXPos;
    nY -= nYPos;
    if (nX < 0 || nX >= N_ARRAY_WAVE_SIZE || nY < 0 || nY >= N_ARRAY_WAVE_SIZE)
        return N_INFINITE;
    if (!waves[nY][nX].bPermanent)
        return N_INFINITE;
    return waves[nY][nX].nCost;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::RunSquare()
{
    ASSERT(bStarted);
    if (!bStarted)
        return;
    while (pSrc)
        NextWave();
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::RunHalfPath(int nDstX, int nDstY, int nFinishDist)
{
    ASSERT(bStarted);
    if (!bStarted)
        return false;
    // int nDist = ManhattenDistance( pSrc->nX, pSrc->nY, nDstX, nDstY ) / 3;
    nDstX -= nXPos;
    nDstY -= nYPos;
    while (pSrc)
    {
        if (ManhattenDistance(pSrc->nX, pSrc->nY, nDstX, nDstY) < nFinishDist)
        {
            NextWave();
            return true;
        }
        NextWave();
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CWavePathFinder::RunSimpleOneWave(int nDstX, int nDstY)
{
    ASSERT(bStarted);
    if (!bStarted)
        return false;
    SPTWave *pDest = &waves[nDstY - nYPos][nDstX - nXPos];
    while (pSrc && pSrc != pDest)
    {
        NextWave();
    }
    return pSrc != NULL;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::BackTraceRoute(int nX, int nY, CAIRoute &res)
{
    res.Clear();
    nX -= nXPos;
    nY -= nYPos;
    if (nX < 0 || nX >= N_ARRAY_WAVE_SIZE || nY < 0 || nY >= N_ARRAY_WAVE_SIZE)
        return;
    while (waves[nY][nX].backDir != NULL_DIR)
    {
        res.AddHeadUnit(nX + nXPos, nY + nYPos);
        switch (waves[nY][nX].backDir)
        {
        case DOWN:
            nY--;
            break;
        case UP:
            nY++;
            break;
        case LEFT:
            nX--;
            break;
        case RIGHT:
            nX++;
            break;
        case DOWNLEFT:
            nY--;
            nX--;
            break;
        case DOWNRIGHT:
            nY--;
            nX++;
            break;
        case UPLEFT:
            nY++;
            nX--;
            break;
        case UPRIGHT:
            nY++;
            nX++;
            break;
        default:
            ASSERT(0 && "");
        }
    }
    res.AddHeadUnit(nX + nXPos, nY + nYPos);
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::SaveLocks(bool bClear)
{
    savedLocks.SetSizes(N_ARRAY_WAVE_SIZE, N_ARRAY_WAVE_SIZE);
    int x, y, begX, begY, konX, konY;
    begX = Max(1, -nXPos);
    begY = Max(1, -nYPos);
    konX = Min(N_MAP_WAVE_SEARCH_SIZE, pMap->XSize() - nXPos - 1);
    konY = Min(N_MAP_WAVE_SEARCH_SIZE, pMap->YSize() - nYPos - 1);
    for (y = begY; y <= konY; y++)
    {
        for (x = begX; x <= konX; x++)
        {
            savedLocks[y][x] = pMap->map[y + nYPos][x + nXPos].nLocked;
        }
    }
    if (bClear)
    {
        for (y = begY; y <= konY; y++)
        {
            for (x = begX; x <= konX; x++)
            {
                pMap->map[y + nYPos][x + nXPos].nLocked = 0;
            }
        }
    }
    bLocksSaved = true;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::RestoreLocks()
{
    ASSERT(bLocksSaved);
    ASSERT(savedLocks.GetXSize() == N_ARRAY_WAVE_SIZE &&
           savedLocks.GetYSize() == N_ARRAY_WAVE_SIZE);
    int x, y, begX, begY, konX, konY;
    begX = Max(1, -nXPos);
    begY = Max(1, -nYPos);
    konX = Min(N_MAP_WAVE_SEARCH_SIZE, pMap->XSize() - nXPos - 1);
    konY = Min(N_MAP_WAVE_SEARCH_SIZE, pMap->YSize() - nYPos - 1);
    for (y = begY; y <= konY; y++)
    {
        for (x = begX; x <= konX; x++)
        {
            pMap->map[y + nYPos][x + nXPos].nLocked = savedLocks[y][x];
        }
    }
    bLocksSaved = false;
}

/////////////////////////////////////////////////////////////////////////////////////
void CWavePathFinder::CleanUp()
{
    ClearWaveUnits(pDead);
    for (int i = 0; i < N_WAVE_LIST_SPLIT_NUMBER; i++)
        ClearWaveUnits(pFirst4[i]);

#ifdef _DEBUG
    for (int y = 0; y < N_ARRAY_WAVE_SIZE; y++)
        for (int x = 0; x < N_ARRAY_WAVE_SIZE; x++)
            ASSERT(waves[y][x].nCost == N_INFINITE);
#endif
    pDead = NULL;
    bStarted = false;
    nCurrentUnitClass = -1;
    nLockCheck = 0;
}

/////////////////////////////////////////////////////////////////////////////////////
// &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
// Route optimizing &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
// is performed as iterative process

// SAIOptBuildEdge &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
EAIDirection SAIOptBuildEdge::GetDir()
{
    return GetDirection(nSrcX, nSrcY, nDstX, nDstY);
}

// SAIOptBuildEdgesList &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
// строит набор векторов для данного направления, флаг bFinal нужен для обозначения того
// требуется исходящие или входящие вектора в случае NULL_DIR
void SAIOptBuildEdgesList::Build(EAIDirection dir, bool bFinal)
{
    int i;
    Clear();
    switch (dir)
    {
    case RIGHT:
        BuildHorizontalEdge(1, 0, 0, 1);
        break;
    case DOWN:
        BuildHorizontalEdge(0, -1, 1, 0);
        break;
    case LEFT:
        BuildHorizontalEdge(-1, 0, 0, -1);
        break;
    case UP:
        BuildHorizontalEdge(0, 1, -1, 0);
        break;
    case UPLEFT:
        BuildDiagonalEdge(-1, 1, -1, 0);
        break;
    case UPRIGHT:
        BuildDiagonalEdge(1, 1, 0, 1);
        break;
    case DOWNLEFT:
        BuildDiagonalEdge(-1, -1, 0, -1);
        break;
    case DOWNRIGHT:
        BuildDiagonalEdge(1, -1, 1, 0);
        break;
    case NULL_DIR:
        BuildStar(bFinal);
        break;
    }
    for (i = 0; i < edges.GetSize(); i++)
        edges[i].nNumber = i;
}

/////////////////////////////////////////////////////////////////////////////////////
void SAIOptBuildEdgesList::BuildHorizontalEdge(int nDirX, int nDirY, int nSideX, int nSideY)
{
    edges.Add(SAIOptBuildEdge(0, 0, nDirX, nDirY));
    edges.Add(SAIOptBuildEdge(-nSideX, -nSideY, nDirX - nSideX, nDirY - nSideY));
    edges.Add(SAIOptBuildEdge(nSideX, nSideY, nDirX + nSideX, nDirY + nSideY));
    edges.Add(SAIOptBuildEdge(-nSideX, -nSideY, nDirX, nDirY));
    edges.Add(SAIOptBuildEdge(0, 0, nDirX + nSideX, nDirY + nSideY));
    edges.Add(SAIOptBuildEdge(nSideX, nSideY, nDirX, nDirY));
    edges.Add(SAIOptBuildEdge(0, 0, nDirX - nSideX, nDirY - nSideY));
}

/////////////////////////////////////////////////////////////////////////////////////
void SAIOptBuildEdgesList::BuildDiagonalEdge(int nDirX, int nDirY, int nSideX1, int nSideY1)
{
    int nSideX2 = nDirX - nSideX1;
    int nSideY2 = nDirY - nSideY1;
    edges.Add(SAIOptBuildEdge(0, 0, nDirX, nDirY));
    edges.Add(SAIOptBuildEdge(nSideX1, nSideY1, nDirX + nSideX1, nDirY + nSideY1));
    edges.Add(SAIOptBuildEdge(nSideX1 - nDirX, nSideY1 - nDirY, nSideX1, nSideY1));
    edges.Add(SAIOptBuildEdge(nSideX2, nSideY2, nDirX + nSideX2, nDirY + nSideY2));
    edges.Add(SAIOptBuildEdge(nSideX2 - nDirX, nSideY2 - nDirY, nSideX2, nSideY2));
    edges.Add(SAIOptBuildEdge(0, 0, nSideX1, nSideY1));
    edges.Add(SAIOptBuildEdge(0, 0, nSideX2, nSideY2));
    edges.Add(SAIOptBuildEdge(nSideX1, nSideY1, nDirX, nDirY));
    edges.Add(SAIOptBuildEdge(nSideX2, nSideY2, nDirX, nDirY));
}

/////////////////////////////////////////////////////////////////////////////////////
void SAIOptBuildEdgesList::BuildStar(bool bFinal)
{
    SRouteUnit src(0, 0);
    for (int i = 1; i <= 8; i++)
    {
        SRouteUnit r = src.Move(EAIDirection(i));
        if (bFinal)
            edges.Add(SAIOptBuildEdge(-r.nX, -r.nY, 0, 0));
        else
            edges.Add(SAIOptBuildEdge(0, 0, r.nX, r.nY));
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void SAIOptBuildEdgesList::Shift(int nDX, int nDY)
{
    for (int i = 0; i < edges.GetSize(); i++)
    {
        edges[i].nSrcX += nDX;
        edges[i].nSrcY += nDY;
        edges[i].nDstX += nDX;
        edges[i].nDstY += nDY;
    }
}

// SAIOptStage &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
// определить длину пути при проходе по тайлу со сменой направления с in на out
static int GetPathLeng(EAIDirection in, EAIDirection out)
{
    int dif = DirectionDifference(in, out);
    switch (dif)
    {
    case 0:
        return IsDiagonalDirection(in) ? 1448 : 1024;
    case 1:
        return 1145;
    case 2:
        return IsDiagonalDirection(in) ? 1608 : 1134;
    case 3:
        return 512;
    case 4:
        return 0;
    }
    ASSERT("Wrong directions difference" && false);
    return 0;
}

// создать структуру для рассчета стоимости прохода по тайлу
static void CalcOptCost(SAIOptCostCalc &r, SAIOptBuildEdge &srcEdge, SAIOptBuildEdge &dstEdge)
{
    EAIDirection srcDir = GetDirection(srcEdge.nSrcX, srcEdge.nSrcY, srcEdge.nDstX, srcEdge.nDstY);
    EAIDirection dstDir = GetDirection(dstEdge.nSrcX, dstEdge.nSrcY, dstEdge.nDstX, dstEdge.nDstY);
    r.nSrcEdge = srcEdge.nNumber;
    r.nDstEdge = dstEdge.nNumber;
    r.nTileDX = dstEdge.nDstX - dstEdge.nSrcX;
    r.nTileDY = dstEdge.nDstY - dstEdge.nSrcY;
    r.nTileX = dstEdge.nSrcX;
    r.nTileY = dstEdge.nSrcY;
    r.nTurnLeng = DirectionDifference(srcDir, dstDir) - 1;
    if (r.nTurnLeng < 0)
        r.nTurnLeng = 0;
    r.nPathLeng = GetPathLeng(srcDir, dstDir);
    r.backDir = InvertDirection(dstDir);
}

// построить структуру, содержащую информацию о рассчете перехода из состояния src в состояние dst
void SAIOptStage::BuildOptStage(SAIOptBuildEdgesList &src, SAIOptBuildEdgesList &dst)
{
    int nSrcEdge, nDstEdge;
    Clear();
    // build copy list
    for (nSrcEdge = 0; nSrcEdge < src.edges.GetSize(); nSrcEdge++)
    {
        for (nDstEdge = 0; nDstEdge < dst.edges.GetSize(); nDstEdge++)
        {
            if (src.edges[nSrcEdge].IsEqual(dst.edges[nDstEdge]))
            {
                SAIOptCopy cp;
                cp.nSrcEdge = nSrcEdge;
                cp.nDstEdge = nDstEdge;
                copyList.Add(cp);
            }
        }
    }
    // build inter reduce list
    for (nSrcEdge = 0; nSrcEdge < src.edges.GetSize(); nSrcEdge++)
    {
        for (nDstEdge = 0; nDstEdge < dst.edges.GetSize(); nDstEdge++)
        {
            if (src.edges[nSrcEdge].IsNext(dst.edges[nDstEdge]))
            {
                SAIOptCostCalc r;
                CalcOptCost(r, src.edges[nSrcEdge], dst.edges[nDstEdge]);
                interList.Add(r);
            }
        }
    }
    // build local reduce list
    for (nSrcEdge = 0; nSrcEdge < dst.edges.GetSize(); nSrcEdge++)
    {
        for (nDstEdge = 0; nDstEdge < dst.edges.GetSize(); nDstEdge++)
        {
            if (dst.edges[nSrcEdge].IsNext(dst.edges[nDstEdge]))
            {
                SAIOptCostCalc r;
                CalcOptCost(r, dst.edges[nSrcEdge], dst.edges[nDstEdge]);
                localList.Add(r);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// initialize optimizer &&&&&&&&&&&&&&&&&&&&&&&&&
static SAIOptStage optMatrix[9][9];

static void InitializeRouteOptimizer()
{
    int nStart, nFinish;
    for (nStart = 0; nStart < 9; nStart++)
    {
        for (nFinish = 0; nFinish < 9; nFinish++)
        {
            SAIOptBuildEdgesList src, dst;
            src.Build(EAIDirection(nStart), false);
            dst.Build(EAIDirection(nFinish), true);
            SRouteUnit test(0, 0);
            /*			if ( nStart != NULL_DIR )
                                    {
                                            test = test.Move( EAIDirection( nStart ) );
                                            dst.Shift( test.nX, test.nY );
                                    }*/
            if (nStart != NULL_DIR)
            {
                test = test.Move(InvertDirection(EAIDirection(nStart)));
                src.Shift(test.nX, test.nY);
            }
            optMatrix[nStart][nFinish].BuildOptStage(src, dst);
        }
    }
}

struct SOptInit
{
    SOptInit() { InitializeRouteOptimizer(); }
};
static SOptInit optimizerInit;

/////////////////////////////////////////////////////////////////////////////////////
// Optimizer itself &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
static SAIOptNodesPool nodesPool;

/////////////////////////////////////////////////////////////////////////////////////
void SRouteOptimizer::TryToReduce(SAIOptCostCalc &r, SAIOptEdge &srcEdge, SAIOptEdge &dstEdge)
{
    if (srcEdge.nCost == N_INFINITE)
        return;
    int nCost, nMul;

    SAITile &srcTile = pMap->map[nCurY + r.nTileY][nCurX + r.nTileX];
    SAITile &dstTile = pMap->map[nCurY + r.nTileY + r.nTileDY][nCurX + r.nTileX + r.nTileDX];

    if (dstTile.IsLocked(nLockCheck) && dstTile.nLocked >= srcTile.nLocked)
        return;
    nCost = dstTile.GetTerrainCost(costsInfo);
    if (nCost < 0)
        return;
    nMul = costsInfo.pElevationCosts[dstTile.nHeight - srcTile.nHeight];
    if (nMul < 0)
        return;
    nCost = (nCost * nMul) >> 10;
    nCost = (nCost * r.nPathLeng) >> 10;
    nCost += Float2Int(costsInfo.fpRotationSpeed1 * r.nTurnLeng * N_ROUTE_OPTIMIZE_TURN_COST);

    nCost += srcEdge.nCost;
    if (nCost < dstEdge.nCost)
    {
        dstEdge.nCost = nCost;
        dstEdge.pNode->backDir = r.backDir;
        dstEdge.pNode->pPrev = srcEdge.pNode;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void SRouteOptimizer::CalcNextStage(SAIOptState &src, SAIOptState &dst, SAIOptStage &stage)
{
    int i;
    dst.SetInfinite();
    for (i = 0; i < 9; i++)
        dst.edges[i].pNode = nodesPool.Get();
    // copy
    for (i = 0; i < stage.copyList.GetSize(); i++)
    {
        SAIOptCopy &cp = stage.copyList[i];
        dst.edges[cp.nDstEdge] = src.edges[cp.nSrcEdge];
    }
    // try inter reduces
    for (i = 0; i < stage.interList.GetSize(); i++)
    {
        SAIOptCostCalc &r = stage.interList[i];
        TryToReduce(r, src.edges[r.nSrcEdge], dst.edges[r.nDstEdge]);
    }
    // try local reduces
    for (i = 0; i < stage.localList.GetSize(); i++)
    {
        SAIOptCostCalc &r = stage.localList[i];
        TryToReduce(r, dst.edges[r.nSrcEdge], dst.edges[r.nDstEdge]);
    }
}

/////////////////////////////////////////////////////////////////////////////////////
int SRouteOptimizer::OptimizeRoute(CUnit *pUnit, CAIMap *pAIMap, CAIRoute &route,
                                   EAIDirection startDir, float fpFinishCentered)
{
    int i;
    if (route.route.GetCount() < 3)
        return 0;
    pMap = pAIMap;
    pMap->GetRouteCostsPathFind(pUnit, costsInfo);
    nUnitClass = pUnit->GetAIClass();
    nLockCheck = pMap->GetUnitLockCheck(pUnit);

    int nDstX = route.route.GetTail().nX;
    int nDstY = route.route.GetTail().nY;
    int nSrcX = route.route.GetHead().nX;
    int nSrcY = route.route.GetHead().nY;
    int nPrevMinimal = N_INFINITE, nMinimalCost;
    while (1)
    {
        SAIOptState state1, state2;
        SAIOptState *cur = &state1, *dest = &state2, *tmp;
        ;

        nodesPool.Init((route.route.GetCount() + 5) * 10);

        // initialize starting state
        cur->Clear();
        for (i = 0; i < 8; i++)
        {
            EAIDirection dir = EAIDirection(i + 1);
            cur->edges[i].pNode = nodesPool.Get();
            cur->edges[i].pNode->backDir = InvertDirection(dir); // NULL_DIR;
            cur->edges[i].pNode->pPrev = NULL;
            cur->edges[i].nCost = N_INFINITE;

            // calc starting cost
            SRouteUnit zero(0, 0);
            SRouteUnit r = zero.Move(dir);
            SAITile &srcTile = pMap->map[nSrcY][nSrcX];
            SAITile &dstTile = pMap->map[nSrcY + r.nY][nSrcX + r.nX];

            int nCost = dstTile.GetTerrainCost(costsInfo), nMul, nDir;
            if (dstTile.IsLocked(nLockCheck) && dstTile.nLocked >= srcTile.nLocked)
                nCost = -1;
            if (nCost >= 0)
            {
                nMul = costsInfo.pElevationCosts[dstTile.nHeight - srcTile.nHeight];
                if (nMul > 0)
                {
                    nCost = (nCost * nMul) >> 10;
                    if (IsDiagonalDirection(dir))
                        nCost = (nCost * 1448) >> 10;
                    nDir = Max(0, DirectionDifference(dir, startDir) - 1);
                    nCost += Float2Int(costsInfo.fpRotationSpeed1 * nDir *
                                       N_ROUTE_OPTIMIZE_TURN_COST);
                    cur->edges[i].nCost = nCost;
                }
            }
        }
        cur->edges[8].nCost = N_INFINITE;

        // for every unit in route perform calculations
        POSITION pos = route.route.GetHeadPosition();
        SRouteUnit *pCurUnit, *pNextUnit;
        pCurUnit = &route.route.GetNext(pos);
        int nPrevDir = 0, nCurDir;
        while (pos)
        {
            pNextUnit = &route.route.GetNext(pos);
            nCurDir = GetRouteDirection(*pCurUnit, *pNextUnit);

            nCurX = pCurUnit->nX;
            nCurY = pCurUnit->nY;
            CalcNextStage(*cur, *dest, optMatrix[nPrevDir][nCurDir]);

            tmp = cur;
            cur = dest;
            dest = tmp; // cur && dest
            nPrevDir = nCurDir;
            pCurUnit = pNextUnit;
        }
        nCurX = pCurUnit->nX;
        nCurY = pCurUnit->nY;
        CalcNextStage(*cur, *dest, optMatrix[nPrevDir][0]);
        tmp = cur;
        cur = dest;
        dest = tmp; // cur && dest

        // analyze results
        nMinimalCost = N_INFINITE;
        SAIOptRouteNode *pMinimalRoute;
        for (i = 0; i < 8; i++)
        {
            if (cur->edges[i].nCost == N_INFINITE)
                continue;
            if (IsDiagonalDirection(EAIDirection(i + 1)))
                cur->edges[i].nCost += Float2Int((1 - fpFinishCentered) * 2048);
            if (cur->edges[i].nCost < nMinimalCost)
            {
                nMinimalCost = cur->edges[i].nCost;
                pMinimalRoute = cur->edges[i].pNode;
            }
        }
        ASSERT(nMinimalCost != N_INFINITE);
        ASSERT(nMinimalCost <= nPrevMinimal);
        if (nMinimalCost >= nPrevMinimal)
            break;
        nPrevMinimal = nMinimalCost;

        // create route from list of route nodes
        route.Clear();
        route.AddHeadUnit(nDstX, nDstY);
        while (1)
        {
            route.route.AddHead(route.route.GetHead().Move(pMinimalRoute->backDir));
            if (!pMinimalRoute->pPrev)
                break;
            pMinimalRoute = pMinimalRoute->pPrev;
        }
        ASSERT(route.route.GetHead().nX == nSrcX && route.route.GetHead().nY == nSrcY);
    }
    return nMinimalCost;
}
