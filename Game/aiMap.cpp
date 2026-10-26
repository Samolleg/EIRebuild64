/**********************************************************************
 *<
        FILE: aiMap.cpp

        DESCRIPTION:  AI map implementation

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/
#include "StdAfx.h"
#include "SaveSchema.h"
#include "EngineDiagnostics.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiMap.h"
#include "aiRoute.h"
#include "aiGraph.h"
#include "aiGraph.inl"
#include "aiScanUnits.h"
#include "CMappedResource.h"
#include "FileRegistry.h"
#include "mapFormat.h"
#include "tools.h"
#include "WinRegistry.h"
#include "unit.h"
#include "MapObjectServer.h"
// crap, for routes start segment & resource path ...
#include "WorldServer.h"
#include "Terrain.h"
/*
#include <fstream>
using std::fstream;
using std::endl;
//using namespace std;
*/

#ifndef _DEBUG
#pragma optimize("agw", on)
#endif
/////////////////////////////////////////////////////////////////////////////////////
// предельное значение стоимости пути при котором бег еще возможен
const int N_AI_RUN_COST_LIMIT = 12000;
// max angle that can be moved
const float FP_AI_MAX_ANGLE_TO_MOVE = 40;
const float FP_AI_MAX_ANGLE_TO_FLY = 60;

/////////////////////////////////////////////////////////////////////////////////////
// SAIPath implementation
/////////////////////////////////////////////////////////////////////////////////////
void SAIPath::NotMoving(CUnit *pUnit)
{
    Clear();
    bFinished = true;
    ptFinish.x = Meter2AITile(pUnit->GetCP().x);
    ptFinish.y = Meter2AITile(pUnit->GetCP().y);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIPath::CreateRotation(int nStartSegment, CUnit *pUnit, const CPlot3D &vecDst,
                             float fpAngSpeed)
{
    NotMoving(pUnit);
    curSegment.SetStartPos(pUnit->GetCP(), -pUnit->GetYAxis(), nStartSegment);
    curSegment.MakeRotation(vecDst, fpAngSpeed);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIPath::CreateAttack(int nStartSegment, CUnit *pUnit, int nTargetID, int nHitSegment,
                           int nTurnFinishSegment)
{
    NotMoving(pUnit);
    curSegment.SetStartPos(pUnit->GetCP(), -pUnit->GetYAxis(), nStartSegment);
    curSegment.MakeAttackMotion(nTargetID, nHitSegment, nTurnFinishSegment);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIPath::CreatePointAttack(int nStartSegment, CUnit *pUnit, const CPlot3D &vecDst,
                                int nHitSegment, int nTurnFinishSegment)
{
    NotMoving(pUnit);
    curSegment.SetStartPos(pUnit->GetCP(), -pUnit->GetYAxis(), nStartSegment);
    curSegment.MakeAttackPointMotion(vecDst, nHitSegment, nTurnFinishSegment);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIPath::CreateStand(CUnit *pUnit)
{
    NotMoving(pUnit);
    curSegment.SetStartPos(pUnit->GetCP(), -pUnit->GetYAxis(), curSegment.GetStartSegment());
}
/////////////////////////////////////////////////////////////////////////////////////
int SAIPath::GetSegmentLength()
{
    return toGo.route.GetCount() + curSegment.route.route.GetCount() - 2;
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIPath::GetWholeRoute(CAIFullRoute &res)
{
    res = curSegment;
    if (res.GetMoveType() != CAIFullRoute::Walk)
        return;
    res.route += toGo;
    res.route.EraseDuplicates();
    // Preview uses the same speeds as execution.
    res.MakeMovement(ptFinish.x, ptFinish.y);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIPath::Clear()
{
    // graphRoute.Clear();
    toGo.Clear();
    curSegment.Clear();
    ptFinish = CPlot2D(0, 0);
    bFinished = true;
    bRouteFailed = false;
    bTryAnyPath = false;
    bFirstTry = true;
    excludeVertices.RemoveAll();
    nPathCost = 0;
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
// SAIObjectPlacement &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
void SAIObjectPlacement::CalcCoveringSquare(CAIMap &map, float fpX, float fpY, float fpR, int &nLx,
                                            int &nHx, int &nLy, int &nHy)
{
    nLx = Float2Int(fpX - fpR) - 1;
    nLy = Float2Int(fpY - fpR) - 1;
    nHx = Float2Int(fpX + fpR) + 1;
    nHy = Float2Int(fpY + fpR) + 1;
    nLx = Max(0, nLx);
    nLy = Max(0, nLy);
    nHx = Min(nHx, map.XSize() - 1);
    nHy = Min(nHy, map.YSize() - 1);
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIObjectPlacement::CreateDangerCircle(CAIMap &map, const CPlot3D &ptCenter, float fpRadius)
{
    int nLx, nHx, nLy, nHy;
    float fpX = Meter2AITile(ptCenter.x);
    float fpY = Meter2AITile(ptCenter.y);
    float fpR = Meter2AITile(fpRadius) + 1;
    float fpR2 = sqr(fpR);
    Clear();
    CalcCoveringSquare(map, fpX, fpY, fpR, nLx, nHx, nLy, nHy);
    for (int y = nLy; y <= nHy; y++)
    {
        for (int x = nLx; x <= nHx; x++)
        {
            if ((sqr(fpX - x) + sqr(fpY - y)) < fpR2)
                AddTileNoCheck(SAIOneTile(x, y, 0, 1, AI_PASS_DANGER, AI_OPACITY_0));
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// опасная область представляет собой пересечение круга радиусом fabs(ptWall) в точке ptCenter и
// прямоугольника, ориентированного в направлении ptWall, центр которого совпадает с центром круга
// высота этого прямоугольника составляет 2*fpRadius, его шириной считается ptWall
void SAIObjectPlacement::CreateDangerWall(CAIMap &map, const CPlot3D &ptCenter, float fpRadius,
                                          const CPlot3D &ptWall)
{
    int nLx, nHx, nLy, nHy;
    float fpX = Meter2AITile(ptCenter.x);
    float fpY = Meter2AITile(ptCenter.y);
    float fpR = Meter2AITile(fabs(ptWall)) + 1;
    float fpLargeR = fpR;
    float fpLargeR2 = sqr(fpLargeR);
    CPlot3D ptNormal;
    ptNormal.x = -ptWall.y;
    ptNormal.y = ptWall.x;
    ptNormal.z = 0;
    // захватываем больше тайлов чем нужно чтобы опасная область гарантированно была помечена
    ptNormal /= (Meter2AITile(fpRadius) + 1) * fabs(ptNormal);

    Clear();
    CalcCoveringSquare(map, fpX, fpY, fpR, nLx, nHx, nLy, nHy);
    for (int y = nLy; y <= nHy; y++)
    {
        for (int x = nLx; x <= nHx; x++)
        {
            if ((sqr(fpX - x) + sqr(fpY - y)) < fpLargeR2)
            {
                if (((fpX - x) * ptNormal.x + (fpY - y) * ptNormal.y) < 1)
                    AddTileNoCheck(SAIOneTile(x, y, 0, 1, AI_PASS_DANGER, AI_OPACITY_0));
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIObjectPlacement::AddMaxTile(int nX, int nY, int nH, int nPassability, int nVisibility)
{
    bHashUsed = true;
    int i = nHash[(nX + nY) & 15];
    while (i >= 0)
    {
        if (tiles[i].nX == nX && tiles[i].nY == nY)
        {
            tiles[i].nHMax = Max(tiles[i].nHMax, nH);
            tiles[i].nPassability = Min(char(nPassability), tiles[i].nPassability);
            tiles[i].nVisibility = Min(char(nVisibility), tiles[i].nVisibility);
            return;
        }
        i = tiles[i].nNext;
    }
    tiles.Add(SAIOneTile(nX, nY, 0x7fffffff, nH, nPassability, nVisibility));
    tiles[tiles.GetSize() - 1].nNext = nHash[(nX + nY) & 15];
    nHash[(nX + nY) & 15] = tiles.GetSize() - 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIObjectPlacement::AddMinTile(int nX, int nY, int nH, int nPassability, int nVisibility)
{
    bHashUsed = true;
    int i = nHash[(nX + nY) & 15];
    while (i >= 0)
    {
        if (tiles[i].nX == nX && tiles[i].nY == nY)
        {
            tiles[i].nHMin = Min(tiles[i].nHMin, nH);
            return;
        }
        i = tiles[i].nNext;
    }
    tiles.Add(SAIOneTile(nX, nY, nH, 0, nPassability, nVisibility));
    tiles[tiles.GetSize() - 1].nNext = nHash[(nX + nY) & 15];
    nHash[(nX + nY) & 15] = tiles.GetSize() - 1;
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIObjectPlacement::SetEdge(int nX, int nY)
{
    assert(bHashUsed);
    int i = nHash[(nX + nY) & 15];
    while (i >= 0)
    {
        if (tiles[i].nX == nX && tiles[i].nY == nY)
        {
            tiles[i].nEdgeMark = 1;
            return;
        }
        i = tiles[i].nNext;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void SAIObjectPlacement::CalcBoundingRectangle(SRectRegion &area)
{
    int i;
    int &nLX = area.nLowX, &nHX = area.nHighX, &nLY = area.nLowY, &nHY = area.nHighY;
    nLY = nLX = 100000;
    nHX = nHY = 0;
    for (i = 0; i < tiles.GetSize(); i++)
    {
        nLX = Min(nLX, tiles[i].nX);
        nLY = Min(nLY, tiles[i].nY);
        nHX = Max(nHX, tiles[i].nX);
        nHY = Max(nHY, tiles[i].nY);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// STerrainTile
/////////////////////////////////////////////////////////////////////////////////////
// Terrain passability calculations
struct STerrainTile
{
    int nMinH, nMaxH, nSpeed, nCost, nWaterUnitH;
    bool bDanger;
    void Init(int nTerraSpeed, int nTerraCost, int nTerraH, int nUnitHeight, int _nWaterUnitH);
    void AddObstacle(int nOMinH, int nOMaxH, int nPass);
    void SetWater(int nDepth);
    int GetMapTerrainIndex();
};
/////////////////////////////////////////////////////////////////////////////////////
inline void STerrainTile::Init(int nTerraSpeed, int nTerraCost, int nTerraH, int nUnitHeight,
                               int _nWaterUnitH)
{
    bDanger = false;
    nSpeed = 1024;
    nCost = nTerraCost;
    nMinH = nTerraH;
    nMaxH = nMinH + nUnitHeight;
    nWaterUnitH = _nWaterUnitH;
    ASSERT(nMaxH > nMinH);
}
/////////////////////////////////////////////////////////////////////////////////////
static int nTerrainIndexTable[5][16] = {
    { 0, 0, 1, 1, 1, 1, 4, 7, 11, 12, 14, 14, 15, 15, 15, 15 },
    { 0, 0, 1, 1, 1, 1, 4, 7, 11, 12, 13, 13, 13, 13, 13, 13 },
    { 0, 0, 1, 1, 1, 1, 4, 7, 8, 9, 10, 10, 10, 10, 10, 10 },
    { 0, 0, 1, 1, 1, 1, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6 },
    { 0, 0, 1, 1, 1, 1, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
};
static int nObjectHardCosts[3] = { 290, 20, 0 };
static int nObjectSpeedCosts[3] = { 7, 2, 0 };
const float fpLog06 = float(log(0.6));
/////////////////////////////////////////////////////////////////////////////////////
inline void STerrainTile::AddObstacle(int nOMinH, int nOMaxH, int nPass)
{
    switch (nPass)
    {
    case AI_PASS_DANGER:
        bDanger = true;
        return;
    case AI_PASS_BRIDGE:
    case AI_PASS_EASY:
        return;
    }
    ASSERT(nPass >= AI_PASS_NO && nPass < AI_PASS_EASY);
    if (nSpeed < 0)
        return;
    if (nOMinH > nMaxH || nOMaxH < nMinH)
        return;
    nOMinH = Max(nOMinH, nMinH);
    nOMaxH = Min(nOMaxH, nMaxH);
    float fpPart = float(nOMaxH - nOMinH) / (nMaxH - nMinH);
    nCost += Float2Int(nObjectHardCosts[nPass] * fpPart);
    // CRAP, replace with table
    nSpeed = Float2Int(float(nSpeed * exp(fpLog06 * fpPart * nObjectSpeedCosts[nPass])));
}
/////////////////////////////////////////////////////////////////////////////////////
inline void STerrainTile::SetWater(int nDepth)
{
    if (nSpeed < 0)
        return;
    if (nDepth == N_AI_MAX_DEPTH + 1 || nDepth >= nWaterUnitH - 1)
        nSpeed = -1;
    else if (nWaterUnitH < 1024)
    {
        if (nDepth > nWaterUnitH / 2)
        {
            nCost += 30;
            nSpeed = (nSpeed * 100) >> 7;
        }
        else
        {
            // nCost += 3; // 15
            nSpeed = (nSpeed * 110) >> 7;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
inline int STerrainTile::GetMapTerrainIndex()
{
    if (nSpeed < 0)
        return 0;
    if (nCost > 70)
        return 0;
    int nSIndex, nCostIndex;
    nSIndex = (nSpeed + 50) / 102;
    if (nSIndex > 15)
        nSIndex = 15;
    if (bDanger)
        nCost *= 2;
    if (nCost < 10)
        nCostIndex = nCost < 3 ? (nCost < 2 ? 0 : 1) : 2;
    else
        nCostIndex = nCost < 35 ? 3 : 4;
    // if ( bDanger )
    // nCostIndex = 4;
    return nTerrainIndexTable[nCostIndex][nSIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
static float fpUnitsHeights[8] = { 2, 0.4f, 1.0f, 1.6f, 2.2f, 1.7f, 3, 6 };
/////////////////////////////////////////////////////////////////////////////////////
// CAITerrainMemory
/////////////////////////////////////////////////////////////////////////////////////
void CAITerrainMemory::Save(short nX, short nY, const SAITile &tile, const SAIObjTile &objTile)
{
    int nKey = MakeKey(nX, nY);
    if (data.find(nKey) != data.end())
    {
        ASSERT(0);
        return;
    }
    SAIKeepTerrain keep;
    keep.nHeight = tile.nHeight;
    keep.nDepth = tile.nDepth;
    keep.nTerrainIndex = objTile.nTerrainIndex;
    data[nKey] = keep;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAITerrainMemory::Restore(short nX, short nY, SAITile &tile, SAIObjTile &objTile)
{
    int nKey = MakeKey(nX, nY);
    auto i = data.find(nKey);
    if (i == data.end())
    {
        ASSERT(0);
        return;
    }
    tile.nHeight = i->second.nHeight;
    tile.nDepth = i->second.nDepth;
    objTile.nTerrainIndex = i->second.nTerrainIndex;
    data.erase(i);
}
/////////////////////////////////////////////////////////////////////////////////////
int CAITerrainMemory::GetSavedHeight(short nX, short nY) const
{
    int nKey = MakeKey(nX, nY);
    auto i = data.find(nKey);
    if (i == data.end())
    {
        ASSERT(0);
        return 0;
    }
    return i->second.nHeight;
}
/////////////////////////////////////////////////////////////////////////////////////
// CLockMatrix &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
static CLockMatrixManager lockMatrixManager;

// can be optimized by using symmetry
CLockMatrix::CLockMatrix(float _fpRadius)
{
    int x, y;
    fpRadius = _fpRadius;
    memset(nLocks, 0, sizeof(nLocks));
    for (y = 0; y < N_AI_LOCK_MATRIX_SIZE; y++)
    {
        for (x = 0; x < N_AI_LOCK_MATRIX_SIZE; x++)
        {
            float fpR = sqrt(sqr(x - (N_AI_LOCK_MATRIX_SIZE / 2)) +
                             sqr(y - (N_AI_LOCK_MATRIX_SIZE / 2)));
            fpR = AITile2Meter(fpR);
            float fpRes = 16 * (fpRadius + FP_AI_MAX_LOCK_RADIUS - fpR);
            int nRes = Float2Int(fpRes);
            if (nRes > 0)
                nLocks[y][x] = nRes;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CAILockManager &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
void SAILockBlock::AddLocks(SAILockBlock &block)
{
    // to be optimized
    for (int i = 0; i < 16; i++)
        nLocks[0][i] = Max(nLocks[0][i], block.nLocks[0][i]);
}
/////////////////////////////////////////////////////////////////////////////////////
bool SAILockBlock::IsEmpty()
{
    return (dwLocks[0] | dwLocks[1] | dwLocks[2] | dwLocks[3]) == 0;
}
/////////////////////////////////////////////////////////////////////////////////////
SAILockBlock *CAILockManager::AllocBlock()
{
    SAILockBlock *pRes;
    if (pDead)
    {
        pRes = pDead;
        pDead = pRes->pNext;
    }
    else
        pRes = &blocks.GetAt(blocks.AddTail(SAILockBlock()));
    return pRes;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAILockManager::Lock(void *_pOwner, CLockMatrix *pMatrix, int nX, int nY)
{
    int x, y, i, nDx, nDy;
    char nLocksArray[N_AI_LOCK_MATRIX_SIZE + 4][N_AI_LOCK_MATRIX_SIZE + 4];
    memset(nLocksArray, 0, sizeof(nLocksArray));
    nDx = nX & 3;
    nDy = nY & 3;
    for (y = 0; y < N_AI_LOCK_MATRIX_SIZE; y++)
        memcpy(nLocksArray[y + nDy] + nDx, pMatrix->nLocks[y], N_AI_LOCK_MATRIX_SIZE);

    // без выполнения этого условия этот код работать не будет
    ASSERT(N_AI_LOCK_BLK_PER_MATRIX * 4 == N_AI_LOCK_MATRIX_SIZE + 4);
    SAILockBlock *pCurBlock = AllocBlock(), **pHashEntry;
    for (y = 0; y < N_AI_LOCK_BLK_PER_MATRIX; y++)
    {
        for (x = 0; x < N_AI_LOCK_BLK_PER_MATRIX; x++)
        {
            for (i = 0; i < 4; i++)
                pCurBlock->dwLocks[i] = *(DWORD *)&nLocksArray[y * 4 + i][x * 4];
            if (!pCurBlock->IsEmpty())
            {
                int nBlockX = x + (nX >> 2);
                int nBlockY = y + (nY >> 2);
                pCurBlock->SetCoord(nBlockX, nBlockY);
                pCurBlock->pOwner = _pOwner;
                pHashEntry = &pHash[(nBlockX * 17 + nBlockY) & (N_AI_LOCK_HASH_SIZE - 1)];
                pCurBlock->pNext = pHashEntry[0];
                pHashEntry[0] = pCurBlock;
                AddBlock(*pCurBlock);

                pCurBlock = AllocBlock();
            }
        }
    }
    FreeBlock(pCurBlock);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAILockManager::Unlock(void *_pOwner, int nX, int nY)
{
    int x, y;
    SAILockBlock curBlock, *pCur, **pPrev;
    for (y = nY >> 2; y < (nY >> 2) + N_AI_LOCK_BLK_PER_MATRIX; y++)
    {
        for (x = nX >> 2; x < (nX >> 2) + N_AI_LOCK_BLK_PER_MATRIX; x++)
        {
            bool bErased = false;

            curBlock.SetCoord(x, y);
            curBlock.ClearLocks();
            pPrev = &pHash[(x * 17 + y) & (N_AI_LOCK_HASH_SIZE - 1)];
            pCur = pPrev[0];
            while (pCur)
            {
                if (pCur->nCoords == curBlock.nCoords)
                {
                    if (pCur->pOwner == _pOwner)
                    {
                        bErased = true;
                        // вырежем этот блок из списка и добавим его в свободные
                        pPrev[0] = pCur->pNext;
                        FreeBlock(pCur);
                        pCur = pPrev[0];
                        continue;
                    }
                    else
                        curBlock.AddLocks(*pCur);
                }
                pPrev = &pCur->pNext;
                pCur = pPrev[0];
            }
            if (bErased)
                ReplaceBlock(curBlock);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CAIStaticLockManager &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
void CAIStaticLockManager::ReplaceBlock(SAILockBlock &block)
{
    int x, y, nDx = int(block.nX) * 4, nDy = int(block.nY) * 4;
    if (((unsigned)nDx >= map.XSize()) | ((unsigned)nDy >= map.YSize()))
        return;
    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
            map.map[y + nDy][x + nDx].nLocked = block.nLocks[y][x];
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIStaticLockManager::AddBlock(SAILockBlock &block)
{
    int x, y, nDx = int(block.nX) * 4, nDy = int(block.nY) * 4;
    if (((unsigned)nDx >= map.XSize()) | ((unsigned)nDy >= map.YSize()))
        return;
    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
            map.map[y + nDy][x + nDx].nLocked = Max(map.map[y + nDy][x + nDx].nLocked,
                                                    (BYTE)block.nLocks[y][x]);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CAIDynamicLockManager &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
void CAIDynamicLockManager::ReplaceBlock(SAILockBlock &block)
{
    int y, nDx = int(block.nX) * 4, nDy = int(block.nY) * 4;
    if (((unsigned)nDx >= map.XSize()) | ((unsigned)nDy >= map.YSize()))
        return;
    for (y = 0; y < 4; y++)
        *(DWORD *)&map.dLocks[y + nDy][nDx] = block.dwLocks[y];
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIDynamicLockManager::AddBlock(SAILockBlock &block)
{
    int x, y, nDx = int(block.nX) * 4, nDy = int(block.nY) * 4;
    if (((unsigned)nDx >= map.XSize()) | ((unsigned)nDy >= map.YSize()))
        return;
    for (y = 0; y < 4; y++)
    {
        for (x = 0; x < 4; x++)
            map.dLocks[y + nDy][x + nDx] = Max(map.dLocks[y + nDy][x + nDx], block.nLocks[y][x]);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// CAIMap &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
CAIMap::CAIMap()
    : staticLocks(*this)
    , dynamicLocks(*this)
{
    bIsLoaded = false;
    bDelayedConvert = false;
    convertRegion.Clear();
    ConstructAreas();
    nLastDangerAddTime = 0;
    bEnableGraphRecalc = false;
}
/////////////////////////////////////////////////////////////////////////////////////
// области для блокирования и проверок различными юнитами
const int N_UNIT_MASK_FLY = 0x0000000F, // маски террейнов соответсвующих типов юнитов
    N_UNIT_MASK_3X3 = 0x0FF00000, N_UNIT_MASK_7X7 = 0xF0000000;
static int nLStar3[][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 100, 100 } },
           nLStar5[][2] = { { -2, 0 }, { -1, -1 }, { -1, 0 }, { -1, 1 },   { 0, -2 },
                            { 0, -1 }, { 0, 0 },   { 0, 1 },  { 0, 2 },    { 1, -1 },
                            { 1, 0 },  { 1, 1 },   { 2, 0 },  { 100, 100 } },
           nLStar7[]
                  [2] = { { -3, 0 },  { -2, -2 }, { -2, -1 }, { -2, 0 }, { -2, 1 }, { -2, 2 },
                          { -1, -2 }, { -1, -1 }, { -1, 0 },  { -1, 1 }, { -1, 2 }, { 0, -3 },
                          { 0, -2 },  { 0, -1 },  { 0, 0 },   { 0, 1 },  { 0, 2 },  { 0, 3 },
                          { 1, -2 },  { 1, -1 },  { 1, 0 },   { 1, 1 },  { 1, 2 },  { 2, -2 },
                          { 2, -1 },  { 2, 0 },   { 2, 1 },   { 2, 2 },  { 3, 0 },  { 100, 100 } },
           nLLock1[][2] = { { -1, -1 }, { -1, 0 }, { -1, 1 }, { 0, -1 }, { 0, 0 },
                            { 0, 1 },   { 1, -1 }, { 1, 0 },  { 1, 1 },  { 100, 100 } },
           nLLock2[][2] = { { -2, -1 }, { -2, 0 }, { -2, 1 }, { -1, -2 },  { -1, -1 }, { -1, 0 },
                            { -1, 1 },  { -1, 2 }, { 0, -2 }, { 0, -1 },   { 0, 0 },   { 0, 1 },
                            { 0, 2 },   { 1, -2 }, { 1, -1 }, { 1, 0 },    { 1, 1 },   { 1, 2 },
                            { 2, -1 },  { 2, 0 },  { 2, 1 },  { 100, 100 } },
           nLLock3[][2] = { { -3, -1 }, { -3, 0 },   { -3, 1 },  { -2, -2 }, { -2, -1 }, { -2, 0 },
                            { -2, 1 },  { -2, 2 },   { -1, -3 }, { -1, -2 }, { -1, -1 }, { -1, 0 },
                            { -1, 1 },  { -1, 2 },   { -1, 3 },  { 0, -3 },  { 0, -2 },  { 0, -1 },
                            { 0, 0 },   { 0, 1 },    { 0, 2 },   { 0, 3 },   { 1, -3 },  { 1, -2 },
                            { 1, -1 },  { 1, 0 },    { 1, 1 },   { 1, 2 },   { 1, 3 },   { 2, -2 },
                            { 2, -1 },  { 2, 0 },    { 2, 1 },   { 2, 2 },   { 3, -1 },  { 3, 0 },
                            { 3, 1 },   { 100, 100 } };

static void InitArea(int (*nNums)[2], CUnitCovering &res)
{
    res.RemoveAll();
    while (nNums[0][0] != 100)
    {
        res.Add(SUnitLockOffset(nNums[0][0], nNums[0][1]));
        nNums++;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::ConstructAreas()
{
    InitArea(nLStar3, aStar3);
    InitArea(nLStar5, aStar5);
    InitArea(nLStar7, aStar7);
    InitArea(nLLock1, aLock1);
    InitArea(nLLock2, aLock2);
    InitArea(nLLock3, aLock3);
}
/////////////////////////////////////////////////////////////////////////////////////
CUnitCovering *CAIMap::GetTerrainArea(int nUnitClass)
{
    switch (nUnitClass)
    {
    case 5:
    case 6:
        return &aStar5;
    case 7:
        return &aStar7;
    }
    return &aStar3;
}
/////////////////////////////////////////////////////////////////////////////////////
// ----------------------------------- Load ---------------------------------------------------
static BOOL WrongFormat(const char *pName)
{
    return ReportError("Wrong format resource: " + CString(pName));
}

int CAIMap::DepthFloatToInt(float fpDepth)
{
    int nDepth = GetMapHeight(fpDepth);
    if (nDepth > N_AI_MAX_DEPTH)
        return N_AI_MAX_DEPTH + 1;
    if (nDepth < 0)
        return 0;
    return nDepth;
}

BOOL CAIMap::Load(LPCSTR pszMapName)
{
    EngineDiagnostics::Note("CAIMap::Load");
    EngineDiagnostics::Note(pszMapName);
    ASSERT(!bIsLoaded);
    // Load map header
    CMappedResource mapRes;
    CString szResourceName = pszMapName;
    int nInd = szResourceName.ReverseFind('.');

    /*	Old method
            CString szBaseNameFull = nInd > 0 ? szResourceName.Left(nInd) : szResourceName;
            nInd = szBaseNameFull.ReverseFind( '\\' );
            CString szBaseName = nInd > 0 ? szBaseNameFull.Right( szBaseNameFull.GetLength() - nInd
       - 1 ) : szBaseNameFull;
    */
    // Open resource
    if (!mapRes.OpenResource(szResourceName))
        return false;

    // New method
    CStringArray aResNames;
    mapRes.GetFileList(aResNames, "*.mp");
    ASSERT(aResNames.GetSize() == 1);
    CString szBaseName = aResNames[0].Left(aResNames[0].Find('.'));

    SMapHeader mapHeader;
    DWORD dwHdSize;
    LPBYTE pMapHeader = mapRes.GetFile(szBaseName + PSZ_MAPFILE_EXT, &dwHdSize);
    if (!pMapHeader)
        return ReportError("Unable to open file \"" + szResourceName + "\"");
    if (dwHdSize < sizeof(mapHeader))
        return ReportError("Wrong file format: " + szResourceName);
    memcpy(&mapHeader, pMapHeader, sizeof(mapHeader));
    if (mapHeader.dwSignature != DW_SMAPHEADER_SIGNATURE ||
        dwHdSize != DWORD(sizeof(mapHeader) + mapHeader.wObjectsCount * sizeof(SMapObject)) +
                        int(mapHeader.dwTiles) * sizeof(int) +
                        int(mapHeader.dwAnimatedTiles) * sizeof(STileAnimInfo))
        return ReportError("Wrong file format: " + szResourceName);

    float fpMaxZ = mapHeader.fpMaxZ;
    fpZMult = float(N_AI_MAX_HEIGHT) / fpMaxZ;

    // Creating z movement cost multiplier table
    nElevationMultipliers.SetSize(N_AI_MAX_HEIGHT * 2 + 1);
    nElevationMultipliersFly.SetSize(N_AI_MAX_HEIGHT * 2 + 1);
    for (int i = -int(N_AI_MAX_HEIGHT); i <= int(N_AI_MAX_HEIGHT); i++)
    {
        float fpZDiff = fabs(float(i) / fpZMult);
        float fpAngle = fabs(ToDegree(float(atan2(fpZDiff, FP_CL_MP_SIZE / 2.f))));

        float fpPassCost = float(square(1 + square(sin(ToRadian(fpAngle)))));
        // float fpPassCost = ( fpAngle / FP_AI_MAX_ANGLE_TO_MOVE )
        //* ( FP_AI_PASS_COST_MULT_AT_MAX_ANGLE - 1 ) + 1;
        if (i < 0)
            fpPassCost = float(1 / sqrt(fpPassCost));
        int nMult = Float2Int(fpPassCost * float(1 << 10));

        if (fpAngle > FP_AI_MAX_ANGLE_TO_FLY)
            nElevationMultipliersFly[i + N_AI_MAX_HEIGHT] = -1;
        else
            nElevationMultipliersFly[i + N_AI_MAX_HEIGHT] = 1024;

        if (fpAngle > FP_AI_MAX_ANGLE_TO_MOVE)
            nElevationMultipliers[i + N_AI_MAX_HEIGHT] = -1;
        else
            nElevationMultipliers[i + N_AI_MAX_HEIGHT] = nMult;
    }

    // Reading objects
    CSimplyArray<SMapObject, SMapObject &> objects;
    objects.SetSize(mapHeader.wObjectsCount);
    memcpy(objects.GetData(), pMapHeader + sizeof(mapHeader),
           mapHeader.wObjectsCount * sizeof(SMapObject));
    // Reading tiles terrain IDs

#define GetTerrainType(nTileIndex)                                     \
    (((unsigned)nTileIndex < mapHeader.dwTextures * N_TILE_IN_TEXTURE) \
         ? (ETERRTYPE)tileIDs[nTileIndex]                              \
         : TERRTYPE_HIGHROCK)

    CIntArray tileIDs;
    tileIDs.SetSize(mapHeader.dwTiles);
    memcpy(tileIDs.GetData(),
           pMapHeader + sizeof(SMapHeader) + mapHeader.wObjectsCount * sizeof(SMapObject),
           int(mapHeader.dwTiles) * sizeof(int));
    //
    navigation.Source("mpr:header", pMapHeader, dwHdSize);
    mapRes.CloseFile(pMapHeader);
    // Creating tiles info table
    CMappedResource aiRes;
    if (!aiRes.OpenResource(CString(theWorldServer.GetResourcePath()) + "aiinfo.res"))
        return ReportError("Unable to read file aiinfo.res");
    DWORD navMaterialSize=0;
    LPBYTE pLoc = aiRes.GetFile("tileDesc.reg", &navMaterialSize);
    if (!pLoc)
        return ReportError("CAIMap::Load.\n Can't open file \"tileDesc.reg\"");
    CFileRegistry aiReg;
    if (!aiReg.Open((LPVOID *)pLoc))
        return ReportError("CAIMap::Load.\n Can't open aiReg");

    CStringArray sections;
    aiReg.GetSections(sections);
    int nMax = 0;
    /*	for ( i = 0; i < sections.GetSize(); i++ )
            {
                    int nNmb = atoi( sections[i].GetBuffer( 0 ) );
                    if ( nNmb > nMax )
                            nMax = nNmb;
            }*/
    tilesInfo.SetSize(TERRTYPE_LAST);
    for (int i = 0; i < tilesInfo.GetSize(); i++)
    {
        *tilesInfo[i].pszName = 0;
        tilesInfo[i].nCostMultiplier = 1;
        tilesInfo[i].nSpeed = -1;
        tilesInfo[i].fpFootstepStrengths[0] = 1;
        tilesInfo[i].fpFootstepStrengths[1] = 1;
        tilesInfo[i].fpFootstepStrengths[2] = 1;
    }
    for (int i = 0; i < sections.GetSize(); i++)
    {
        int nNmb = atoi(sections[i].GetBuffer(0));
        strcpy(tilesInfo[nNmb].pszName, aiReg.GetString(sections[i], "Name", "").GetBuffer(0));
        tilesInfo[nNmb].nCostMultiplier = aiReg.GetInt(sections[i], "CostMul", -1);
        tilesInfo[nNmb].nSpeed = aiReg.GetInt(sections[i], "Speed", -1);
        tilesInfo[nNmb].fpFootstepStrengths[0] = aiReg.GetInt(sections[i], "StepLook", 100) /
                                                 100.0f;
        tilesInfo[nNmb].fpFootstepStrengths[1] = aiReg.GetInt(sections[i], "StepSmell", 100) /
                                                 100.0f;
        tilesInfo[nNmb].fpFootstepStrengths[2] = aiReg.GetInt(sections[i], "StepSound", 100) /
                                                 100.0f;
    }
    navigation.Source("materials", pLoc, navMaterialSize);
    aiReg.Close();
    aiRes.CloseFile(pLoc);
    aiRes.CloseResource();
    // Reserving memory for tiles and initializing them
    int nMapSizeX, nMapSizeY;
    nMapSizeX = mapHeader.dwXSectorsCount * N_CL_PSECTOR_S * 2;
    nMapSizeY = mapHeader.dwYSectorsCount * N_CL_PSECTOR_S * 2;
    map.SetSizes(nMapSizeX, nMapSizeY);
    mapObj.SetSizes(nMapSizeX, nMapSizeY);
    tempMap.SetSizes(nMapSizeX, nMapSizeY);

    // Reading sectors
    for (int j = 0; j < int(mapHeader.dwYSectorsCount); j++)
    {
        for (int i = 0; i < int(mapHeader.dwXSectorsCount); i++)
        {
            char pszSectorName[256];
            SSectorHeader header;
            DWORD dwSize;

            GetSectorName(i, j, szBaseName.GetBuffer(0), pszSectorName);
            LPBYTE pSector = mapRes.GetFile(pszSectorName, &dwSize);
            LPBYTE pBaseSector = pSector;
            if (!pSector)
                return ReportError("Unable to locate resource: " + CString(pszSectorName));
            if (dwSize < sizeof(header))
                return WrongFormat(pszSectorName);
            // Read sector header
            memcpy(&header, pSector, sizeof(header));
            if (header.dwSignature != DW_SSECTORHEADER_SIGNATURE_NORMALS)
                return WrongFormat(pszSectorName);
            bool bHaveLayers = (header.cFlags & C_SECTORLAYERED) != 0;
            bool bHaveLayerTiles = (header.cFlags & C_SECTORLAYER_TILED) != 0;
            DWORD dwSectorSize = sizeof(header) + sizeof(SSectVert2) * N_V_PSECTOR +
                                 sizeof(TILE_ID) * N_TILES_PSECTOR;

            if (bHaveLayers)
                dwSectorSize += sizeof(SSectVert2) * N_V_PSECTOR + sizeof(WORD) * N_TILES_PSECTOR;
            if (bHaveLayerTiles)
                dwSectorSize += sizeof(TILE_ID) * N_TILES_PSECTOR;
            if (dwSize != dwSectorSize)
                return WrongFormat(pszSectorName);

            pSector += sizeof(header);

            navigation.Source(pszSectorName, pBaseSector, dwSize);
            const auto* navVertices=reinterpret_cast<const SSectVert2*>(pSector);
            auto vertex=[&](int x,int y) {
                const auto& v=navVertices[y*N_V_PSECTOR_S+x];
                return Navigation::Point{GetCoord(v.sbXOff,float(i*N_CL_PSECTOR_S+x)*FP_CL_MP_SIZE),
                    GetCoord(v.sbYOff,float(j*N_CL_PSECTOR_S+y)*FP_CL_MP_SIZE),GetFloatValueByRange(v.usZ,fpMaxZ)};
            };
            for(int y=0;y<N_CL_PSECTOR_S;++y)for(int x=0;x<N_CL_PSECTOR_S;++x) {
                auto lu=vertex(x,y),ru=vertex(x+1,y),ld=vertex(x,y+1),rd=vertex(x+1,y+1);
                navigation.Terrain().ground.push_back({lu,ru,rd,1});
                navigation.Terrain().ground.push_back({lu,rd,ld,1});
            }
            SSectVert2 *pVert2 = (SSectVert2 *)pSector;
            // Reading heights and depths
            for (int js = 0; js < N_CL_PSECTOR_S; js++)
            {
                int nYIndex = j * N_CL_PSECTOR_S * 2 + js * 2;
                for (int is = 0; is < N_CL_PSECTOR_S; is++, pVert2++)
                {
                    int nXIndex = i * N_CL_PSECTOR_S * 2 + is * 2;
                    float fpZlu = GetFloatValueByRange(pVert2->usZ, fpMaxZ);
                    float fpZru = GetFloatValueByRange((pVert2 + 1)->usZ, fpMaxZ);
                    float fpZld = GetFloatValueByRange((pVert2 + N_V_PSECTOR_S)->usZ, fpMaxZ);
                    float fpZrd = GetFloatValueByRange((pVert2 + N_V_PSECTOR_S + 1)->usZ, fpMaxZ);

                    float fpBaseZlu = fpZld / 4 + fpZlu / 2 + fpZru / 4;
                    float fpBaseZru = fpZlu / 4 + fpZru / 2 + fpZrd / 4;
                    float fpBaseZld = fpZlu / 4 + fpZld / 2 + fpZrd / 4;
                    float fpBaseZrd = fpZld / 4 + fpZrd / 2 + fpZru / 4;
                    int nH;

                    nH = Float2Int(fpBaseZlu * fpZMult);
                    assert(nH <= N_AI_MAX_HEIGHT);
                    map[nYIndex][nXIndex].nHeight = nH;

                    nH = Float2Int(fpBaseZru * fpZMult);
                    assert(nH <= N_AI_MAX_HEIGHT);
                    map[nYIndex][nXIndex + 1].nHeight = nH;

                    nH = Float2Int(fpBaseZld * fpZMult);
                    assert(nH <= N_AI_MAX_HEIGHT);
                    map[nYIndex + 1][nXIndex].nHeight = nH;

                    nH = Float2Int(fpBaseZrd * fpZMult);
                    assert(nH <= N_AI_MAX_HEIGHT);
                    map[nYIndex + 1][nXIndex + 1].nHeight = nH;

                    if (bHaveLayers)
                    {
                        float fpLrZlu = GetFloatValueByRange((pVert2 + N_V_PSECTOR)->usZ, fpMaxZ);
                        float fpLrZru = GetFloatValueByRange((pVert2 + 1 + N_V_PSECTOR)->usZ,
                                                             fpMaxZ);
                        float fpLrZld = GetFloatValueByRange(
                            (pVert2 + N_V_PSECTOR_S + N_V_PSECTOR)->usZ, fpMaxZ);
                        float fpLrZrd = GetFloatValueByRange(
                            (pVert2 + N_V_PSECTOR_S + 1 + N_V_PSECTOR)->usZ, fpMaxZ);

                        float fpLayerZlu = fpLrZld / 4 + fpLrZlu / 2 + fpLrZru / 4;
                        float fpLayerZru = fpLrZlu / 4 + fpLrZru / 2 + fpLrZrd / 4;
                        float fpLayerZld = fpLrZlu / 4 + fpLrZld / 2 + fpLrZrd / 4;
                        float fpLayerZrd = fpLrZld / 4 + fpLrZrd / 2 + fpLrZru / 4;

                        float fpDepthlu = fpLayerZlu - fpBaseZlu;
                        float fpDepthru = fpLayerZru - fpBaseZru;
                        float fpDepthld = fpLayerZld - fpBaseZld;
                        float fpDepthrd = fpLayerZrd - fpBaseZrd;

                        map[nYIndex][nXIndex].nDepth = DepthFloatToInt(fpDepthlu);
                        map[nYIndex][nXIndex + 1].nDepth = DepthFloatToInt(fpDepthru);
                        map[nYIndex + 1][nXIndex].nDepth = DepthFloatToInt(fpDepthld);
                        map[nYIndex + 1][nXIndex + 1].nDepth = DepthFloatToInt(fpDepthrd);
                    }
                }
                pVert2++;
            }
            pSector += sizeof(SSectVert2) * N_V_PSECTOR;
            if (bHaveLayers)
                pSector += sizeof(SSectVert2) * N_V_PSECTOR;
            // Read tiles
            TILE_ID *pTID = (TILE_ID *)pSector;
            for (int js = 0; js < N_CL_PSECTOR_S; js += 2)
            {
                int nYIndex = j * N_CL_PSECTOR_S * 2 + js * 2;
                for (int is = 0; is < N_CL_PSECTOR_S; is += 2)
                {
                    int nXIndex = i * N_CL_PSECTOR_S * 2 + is * 2;
                    int nTile = GetTileInd(*pTID++);
                    BYTE nti = GetTerrainType(nTile);
                    for (int nAdd = 0; nAdd < 4; nAdd++)
                    {
                        mapObj[nYIndex + nAdd][nXIndex + 0].nTerrainIndex = nti;
                        mapObj[nYIndex + nAdd][nXIndex + 1].nTerrainIndex = nti;
                        mapObj[nYIndex + nAdd][nXIndex + 2].nTerrainIndex = nti;
                        mapObj[nYIndex + nAdd][nXIndex + 3].nTerrainIndex = nti;
                    }
                }
            }
            pSector += sizeof(TILE_ID) * N_TILES_PSECTOR;
            if (bHaveLayerTiles)
                pSector += sizeof(TILE_ID) * N_TILES_PSECTOR;
            // Read object IDs
            if (bHaveLayers)
            {
                WORD *pOID = (WORD *)pSector;
                for (int js = 0; js < N_CL_PSECTOR_S; js += 2)
                {
                    int nYIndex = j * N_CL_PSECTOR_S * 2 + js * 2;
                    for (int is = 0; is < N_CL_PSECTOR_S; is += 2)
                    {
                        int nXIndex = i * N_CL_PSECTOR_S * 2 + is * 2;
                        WORD oind = *pOID++;
                        bool bWaterFlag = false;
                        if (oind != TILE_UNDEF && (objects[oind].eType == TOT_WATER_NOTEXTURE ||
                                                   objects[oind].eType == TOT_WATER))
                            bWaterFlag = true;
                        for (int nAdd = 0; nAdd < 4; nAdd++)
                        {
                            for (int n = 0; n < 4; n++)
                            {
                                SAIObjTile &tile = mapObj[nYIndex + nAdd][nXIndex + n];
                                SAITile &ntile = map[nYIndex + nAdd][nXIndex + n];
                                ntile.bWaterFlag = ntile.nDepth > 0 ? bWaterFlag : false;
                            }
                        }
                    }
                }
            }
            // Read layer tiles
            if (bHaveLayerTiles)
            {
                pSector -= sizeof(TILE_ID) * N_TILES_PSECTOR;
                TILE_ID *pTID = (TILE_ID *)pSector;
                const WORD *pLayerObjectIDs = (const WORD *)(pSector + sizeof(TILE_ID) * N_TILES_PSECTOR);
                for (int js = 0; js < N_CL_PSECTOR_S; js += 2)
                {
                    int nYIndex = j * N_CL_PSECTOR_S * 2 + js * 2;
                    for (int is = 0; is < N_CL_PSECTOR_S; is += 2)
                    {
                        int nXIndex = i * N_CL_PSECTOR_S * 2 + is * 2;
                        // Match sector rendering: an absent layer object leaves no active tile.
                        const WORD layerObjectID = *pLayerObjectIDs++;
                        if (layerObjectID != TILE_UNDEF && *pTID != TILE_UNDEF)
                        {
                            int nTile = GetTileInd(*pTID);
                            BYTE nti = GetTerrainType(nTile);
                            for (int nAdd = 0; nAdd < 4; nAdd++)
                            {
                                for (int n = 0; n < 4; n++)
                                {
                                    SAIObjTile &tile = mapObj[nYIndex + nAdd][nXIndex + n];
                                    SAITile &ntile = map[nYIndex + nAdd][nXIndex + n];
                                    if (ntile.bWaterFlag && ntile.nDepth > 0)
                                        tile.nTerrainIndex = nti;
                                }
                            }
                        }
                        pTID++;
                    }
                }
            }
            mapRes.CloseFile(pBaseSector);
        }
    }
    bIsLoaded = true;
    bEnableGraphRecalc = false;

    // Testing
    // CStdioFile fout( "C:\\res.txt", CFile::modeCreate | CFile::modeWrite );
    // for( i = 0; i < map.GetSize(); i++ )
    //{
    // char buf[256];
    // sprintf( buf, "L:%u Pass:%u TII:%u\tH: %u\tW:%u D:%u\r\n",
    // map[i].bIsLocked, map[i].bCanPass, map[i].nTileInfoIndex, map[i].nHeight, map[i].bWaterFlag,
    // map[i].nDepth );
    // fout.WriteString( buf );
    //}
    // fout.Close();

    // Initialize passailities tables
    CalcDefaultPassabilities();
    InitializeBasicTerrainCosts();
    // Material boundaries are retained by Recast. This first slice imports the
    // standing-human ground policy; the complete class/water policy is NAV04.
    // Use the initialized material table: per-cell dwTerrains is still zero
    // while StartZoneLoading defers conversion until FinishZoneLoading.
    for(auto& triangle:navigation.Terrain().ground) {
        int x=Float2AITile((triangle.a.x+triangle.b.x+triangle.c.x)/3);
        int y=Float2AITile((triangle.a.y+triangle.b.y+triangle.c.y)/3);
        triangle.area=TileInBounds(x,y)?BYTE((tilesInfo[mapObj[y][x].nTerrainIndex].dwTerrains>>12)&15):0;
    }
    // Initialize rest objects
    dLocks.SetSizes(nMapSizeX, nMapSizeY);
    dLocks.FillZero();
    wave.SetMap(this, false);
    waveBack.SetMap(this, true);
    aiGraph.SetMap(this);
    marks.SetMap(this, AITile2Meter(float(XSize())), AITile2Meter(float(YSize())));
    mapObjects.Set(XSize(), YSize(), this);
    // generate passabilities for all map
    ConvertObjMapToPassabilities(SRectRegion(0, XSize() - 1, 0, YSize() - 1));
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// make it easy way ;)
#undef new
// #ifdef _REDEF_NEW
// #define new DEBUG_NEW
// #undef _REDEF_NEW
// #endif
void CAIMap::Clear()
{
    CAIMap::~CAIMap();
    ::new ((void *)this) CAIMap;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::InitializeBasicTerrainCosts()
{
    memset(nTerrainCosts, 0xff, sizeof(nTerrainCosts));
    memset(nTerrainFlatCosts, 0xff, sizeof(nTerrainFlatCosts));
    memset(nTerrainSpeeds, 0xff, sizeof(nTerrainSpeeds));
    nTerrainCosts[0] = -1;
    nTerrainCosts[1] = int(1024 * 0.5) * 50;
    nTerrainCosts[2] = int(1024 * 0.6) * 50;
    nTerrainCosts[3] = int(1024 * 0.7) * 50;
    nTerrainCosts[4] = int(1024 * 0.6) * 20;
    nTerrainCosts[5] = int(1024 * 0.7) * 20;
    nTerrainCosts[6] = int(1024 * 0.8) * 20;
    nTerrainCosts[7] = int(1024 * 0.7) * 5;
    nTerrainCosts[8] = int(1024 * 0.8) * 5;
    nTerrainCosts[9] = int(1024 * 0.9) * 5;
    nTerrainCosts[10] = int(1024 * 1.0) * 5;
    nTerrainCosts[11] = int(1024 * 0.8) * 2;
    nTerrainCosts[12] = int(1024 * 0.9) * 2;
    nTerrainCosts[13] = int(1024 * 1.0) * 2;
    nTerrainCosts[14] = int(1024 * 1.0);
    nTerrainCosts[15] = int(1024 * 1.2);
    nTerrainFlatCosts[0] = -1;
    for (int i = 1; i < 16; i++)
        nTerrainFlatCosts[i] = 1024;
    nTerrainSpeeds[0] = -1;
    nTerrainSpeeds[1] = int(1024 * 0.5);
    nTerrainSpeeds[2] = int(1024 * 0.6);
    nTerrainSpeeds[3] = int(1024 * 0.7);
    nTerrainSpeeds[4] = int(1024 * 0.6);
    nTerrainSpeeds[5] = int(1024 * 0.7);
    nTerrainSpeeds[6] = int(1024 * 0.8);
    nTerrainSpeeds[7] = int(1024 * 0.7);
    nTerrainSpeeds[8] = int(1024 * 0.8);
    nTerrainSpeeds[9] = int(1024 * 0.9);
    nTerrainSpeeds[10] = int(1024 * 1.0);
    nTerrainSpeeds[11] = int(1024 * 0.8);
    nTerrainSpeeds[12] = int(1024 * 0.9);
    nTerrainSpeeds[13] = int(1024 * 1.0);
    nTerrainSpeeds[14] = int(1024 * 1.0);
    nTerrainSpeeds[15] = int(1024 * 1.2);
}
/////////////////////////////////////////////////////////////////////////////////////
// Map Objects &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
// one tile operations
bool CAIMap::AddObjectTile(SAIOneTile &tile)
{
    int nIdx;
    int &nX = tile.nX, &nY = tile.nY;
    if (!TileInBounds(nX, nY))
        return false;
    if (tile.nHMin >= tile.nHMax)
        return true;
    ASSERT(tile.nHMax >= 0 && tile.nHMax <= N_AI_MAX_OBJECT_HEIGHT);
    SAIObjTile &otile = mapObj[nY][nX];
    if (otile.bEmpty)
    {
        otile.bEmpty = 0;
        otile.nMaxObjectHeight = tile.nHMax;
        otile.nMinObjectHeight = tile.nHMin;
        otile.nObjPassability = tile.nPassability;
        otile.nObjVisibility = tile.nVisibility;
    }
    else // there are already object(s) on tile
    {
        CAIObjectsCell *pCell = mapObjects.GetCellByTile(nX, nY);
        if (!otile.bManyObj)
        {
            nIdx = pCell->GetFreeTileIndex();
            pCell->objectTiles[nIdx] = SAICellObjectTile(otile.nMinObjectHeight,
                                                         otile.nMaxObjectHeight,
                                                         otile.nObjPassability,
                                                         otile.nObjVisibility);
            pCell->objectTiles[nIdx].nNext = -1;
            otile.nFirstCellIndex = nIdx;
            otile.bManyObj = 1;
        }

        nIdx = pCell->GetFreeTileIndex();
        pCell->objectTiles[nIdx] = SAICellObjectTile(tile.nHMin, tile.nHMax, tile.nPassability,
                                                     tile.nVisibility);
        pCell->objectTiles[nIdx].nNext = otile.nFirstCellIndex;
        otile.nFirstCellIndex = nIdx;
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::RemoveObjectTile(SAIOneTile &tile)
{
    int nIdx;
    int &nX = tile.nX, &nY = tile.nY;
    if (!TileInBounds(nX, nY))
        return false;
    if (tile.nHMin >= tile.nHMax)
        return true;
    SAIObjTile &otile = mapObj[nY][nX];
    ASSERT(!otile.bEmpty);
    if (otile.bEmpty)
        return false;
    if (!otile.bManyObj)
    {
        ASSERT(tile.nHMin == int(otile.nMinObjectHeight) &&
               tile.nHMax == int(otile.nMaxObjectHeight) &&
               unsigned(tile.nPassability) == otile.nObjPassability &&
               unsigned(tile.nVisibility) == otile.nObjVisibility);
        otile.bEmpty = 1;
    }
    else
    {
        CAIObjectsCell *pCell = mapObjects.GetCellByTile(nX, nY);
        otile.nFirstCellIndex = pCell->RemoveTile(otile.nFirstCellIndex, tile.nHMin, tile.nHMax,
                                                  tile.nPassability, tile.nVisibility);
        nIdx = otile.nFirstCellIndex;
        if (pCell->objectTiles[nIdx].nNext == -1)
        {
            SAICellObjectTile &objTile = pCell->objectTiles[nIdx];
            otile.nMinObjectHeight = objTile.nMinHeight;
            otile.nMaxObjectHeight = objTile.nMaxHeight;
            otile.nObjPassability = objTile.nPassability;
            otile.nObjVisibility = objTile.nVisibility;
            otile.bManyObj = 0;
            pCell->RemoveTile(nIdx);
        }
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::TileHasBridge(int x, int y, int *pnBridgeHeight)
{
    bool bRv = false;
    int nRes = 0;
    if (!mapObj[y][x].bEmpty)
    {
        if (mapObj[y][x].bManyObj)
        {
            CAIObjectsCell *pCell = mapObjects.GetCellByTile(x, y);
            int nIdx = mapObj[y][x].nFirstCellIndex;
            while (nIdx >= 0)
            {
                SAICellObjectTile &t = pCell->objectTiles[nIdx];
                if (t.nPassability == AI_PASS_BRIDGE)
                {
                    nRes = Max(nRes, (int)t.nMaxHeight);
                    bRv = true;
                }
                nIdx = t.nNext;
            }
        }
        else
        {
            if (mapObj[y][x].nObjPassability == AI_PASS_BRIDGE)
            {
                nRes = Max(nRes, (int)mapObj[y][x].nMaxObjectHeight);
                bRv = true;
            }
        }
    }
    if (pnBridgeHeight)
        pnBridgeHeight[0] = nRes;
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
// set of tiles operations, on all or nothing basis
bool CAIMap::AddRemoveMapObject(SAIObjectPlacement &place, bool bRemove)
{
    int i, k;
    if (bRemove)
    {
        for (i = 0; i < place.tiles.GetSize(); i++)
        {
            if (!RemoveObjectTile(place.tiles[i]))
            {
                ASSERT("Cant remove tile from aimap" && 0);
                // return false;
            }
        }
    }
    else
    {
        for (i = 0; i < place.tiles.GetSize(); i++)
        {
            if (!AddObjectTile(place.tiles[i]))
            {
                for (k = 0; k < i; k++)
                    RemoveObjectTile(place.tiles[k]);
                return false;
            }
        }
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::KeepOriginalTerrain(int nX, int nY, bool bSave)
{
    if (bSave)
        terrainKeeper.Save(nX, nY, map[nY][nX], mapObj[nY][nX]);
    else
        terrainKeeper.Restore(nX, nY, map[nY][nX], mapObj[nY][nX]);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::AddRemoveBridge(SAIObjectPlacement &place, bool bRemove)
{
    int i;
    if (bRemove)
    {
        for (i = 0; i < place.tiles.GetSize(); i++)
        {
            SAIOneTile &tile = place.tiles[i];
            int nX = tile.nX, nY = tile.nY;
            if (!TileInBounds(nX, nY))
                continue;
            if (tile.nHMin >= tile.nHMax)
                continue;
            tile.nPassability = AI_PASS_BRIDGE;
            if (!RemoveObjectTile(tile))
            {
                ASSERT("Cant remove bridge tile from aimap" && 0);
                // return false;
            }
            else
            {
                int nBridgeH;
                SAITile &mapTile = map[nY][nX];
                if (!TileHasBridge(nX, nY, &nBridgeH))
                {
                    ASSERT(mapTile.bBridge);
                    if (mapTile.bBridge)
                        KeepOriginalTerrain(nX, nY, false);
                    mapTile.bBridge = 0;
                    mapTile.bWaterFlag = mapTile.nDepth != 0;
                }
                else
                    mapTile.nHeight = Max(terrainKeeper.GetSavedHeight(nX, nY), nBridgeH);
            }
        }
    }
    else
    {
        for (i = 0; i < place.tiles.GetSize(); i++)
        {
            SAIOneTile &tile = place.tiles[i];
            int nX = tile.nX, nY = tile.nY;
            if (!TileInBounds(nX, nY))
                continue;
            if (tile.nHMin >= tile.nHMax)
                continue;
            tile.nPassability = AI_PASS_BRIDGE;
            SAITile &mapTile = map[nY][nX];
            if (!mapTile.bBridge)
                KeepOriginalTerrain(nX, nY, true);
            if (!AddObjectTile(tile))
            {
                ASSERT(0);
                return false;
            }
            mapTile.bBridge = 1;
            mapTile.bWaterFlag = 0;
            mapTile.nDepth = 0;
            mapTile.nHeight = Max(mapTile.nHeight, (WORD)tile.nHMax);
            mapObj[nY][nX].nTerrainIndex = AI_N_ROAD_TYPE_ID;
        }
    }
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
int CAIMap::GetIntObjectHeight(float fpHeight)
{
    int nObjectHeight = GetMapHeight(fpHeight);
    if (nObjectHeight > N_AI_MAX_OBJECT_HEIGHT)
        nObjectHeight = N_AI_MAX_OBJECT_HEIGHT;
    if (nObjectHeight < 0)
        nObjectHeight = 0;
    return nObjectHeight;
}
///////////////////////////////////////////////////////////////////
// FUNCTION:	AddRemoveTileMapObject
// DESCRIPTION:	adds or removes map object from AI tiles occupied
// PARAMETERS:  pObject - pointer on the object;
//				wX, wY coordinates of new point in AI tiles if object is Added, if
// Removed
//- ignored; 				pCell - pointer to object's cell; bRemove - if true - object
// is removed, otherwise added
// RETURNS:		true if success
///////////////////////////////////////////////////////////////////
bool CAIMap::AddRemoveMOLowLevel(CMapObject *pObject, bool bRemove)
{
    //////////// find AI tiles covered with this MapObject ////////////
    // 1) retrieve corner points of parallelepiped
    // 2) for every face of ||-piped find covered tiles
    // 3) first find delta X, delta Y, delta Z in both directions
    // 4) for every piece of plane find AI tiles and Add them to the list
    // AND then for every AI tile we must have min height, max height in respect
    // to the Ground, which can be evaluated from the tile itself

    int nBodyPart, i, j, k, l;
    // test if the object is Human set only one AI tile under the object
    navigation.Object(pObject,bRemove,*this);
    int nID = pObject->GetID();
    int nX = Float2AITile(pObject->GetCP().x);
    int nY = Float2AITile(pObject->GetCP().y);
    bool bBridge = false, bRv;

    place.Clear();
    placeBridge.Clear();
    if (pObject->WhoAmI() == HPC_UNIT)
    {
        if (bRemove)
            RemoveUnit((CUnit *)pObject, nX, nY);
        else
            AddUnit((CUnit *)pObject, nX, nY);
        return true;
    }
    else
    {
        pObject->CreateCurrentPlacement();
        // const CStringArray szBodyParts;// = pObject->GetBodyPartNames();
        const CStringArray *pszNeedParts = &((CMapObjectServer *)pObject->GetMapObjectSpecific())
                                                ->szNames;
        if (pszNeedParts->GetSize() == 0)
            pszNeedParts = &pObject->GetBodyPartNames();
        // for( i = 0; i < pObject->fbvs.GetSize(); i++ )
        for (nBodyPart = 0; nBodyPart < pszNeedParts->GetSize(); nBodyPart++)
        {
            SAIObjectPlacement *pPlace;
            i = pObject->GetFigureIndexByName((*pszNeedParts)[nBodyPart]);
            if (i < 0)
            {
                ReportError("Bodypart \"%s\" for object \"%s\" not found\n",
                            (*pszNeedParts)[nBodyPart], pObject->GetMaskName());
                ASSERT(0 && "Body part not found");
                continue;
            }
            if (pObject->fbvs[i].IsIgnored())
                continue;
            int nPassability = pObject->fbvs[i].GetPassability(); // AI_PASS_EASY;
            int nVisibility = pObject->fbvs[i].GetOpacity();      // AI_OPACITY_100;

            if (pObject->fbvs[i].IsBridge())
            {
                bBridge = true;
                pPlace = &placeBridge;
            }
            else
                pPlace = &place;

            // 1) retrieve corner points of parallelepiped
            CPlot3D ptMin = pObject->fbvs[i].ptMin, ptMax = pObject->fbvs[i].ptMax;

            CPlot3D ptFLD(ptMax.x, ptMin.y, ptMin.z), ptFRD(ptMax.x, ptMax.y, ptMin.z),
                ptBLD = ptMin, ptBRD(ptMin.x, ptMax.y, ptMin.z), ptFLU(ptMax.x, ptMin.y, ptMax.z),
                ptFRU = ptMax, ptBLU(ptMin.x, ptMin.y, ptMax.z), ptBRU(ptMin.x, ptMax.y, ptMax.z);
            CPlot3D normals[6];
            // 2) define faces of ||-piped: for every face set 3 points -
            // 		offsetA, center, offsetB
            CPlot3D faces[8] = { ptFLD,
                                 ptBLD, // 	 Center of the DOWN face
                                 ptBRD, // BACK
                                 ptBRU, // RIGHT
                                 ptFRU, // UP
                                 ptFLU, // FRONT
                                 ptFLD, // LEFT
                                 ptBLD };
            normals[0] = CPlot3D(0, 0, -1);
            normals[1] = CPlot3D(-1, 0, 0);
            normals[2] = CPlot3D(0, 1, 0);
            normals[3] = CPlot3D(0, 0, 1);
            normals[4] = CPlot3D(1, 0, 0);
            normals[5] = CPlot3D(0, -1, 0);

            // 3) Rotate faces basing on quaternion
            CPlot3D ptObjPos = pObject->GetCP();
            ptObjPos.z += GetOriginalTileHeight(pObject->GetCP());
            for (j = 0; j < 8; j++)
            {
                faces[j] = faces[j] + pObject->fbvs[i].ptBone + pObject->fbvs[i].ptCenter;
                faces[j] = pObject->RotateVector(faces[j]);
                //				faces[j] = pObject->rotation.RotateVector( faces[j]
                //);
                faces[j] += ptObjPos;
            }
            for (j = 0; j < 6; j++)
                normals[j] = pObject->RotateVector(normals[j]);
            //				normals[j] = pObject->rotation.RotateVector( normals[j] );

            // 4) for every face
            for (j = 1; j < 7; j++)
            {
                // faces[j-1] - corner A, faces[j] - center, faces[j+1] - corner B
                CPlot3D ptDeltaA = faces[j - 1] - faces[j], ptDeltaB = faces[j + 1] - faces[j];
                float fpNA = (float)sqrt(ptDeltaA.x * ptDeltaA.x + ptDeltaA.y * ptDeltaA.y) /
                             0.5f, // 0.5f = size of AI tile / size of real tile
                    fpNB = (float)sqrt(ptDeltaB.x * ptDeltaB.x + ptDeltaB.y * ptDeltaB.y) / 0.5f;
                int nA = Float2Int(fpNA * 1.5f) + 2, nB = Float2Int(fpNB * 1.5f) + 2;
                // Calculate offsets
                CPlot3D ptOffsetA = ptDeltaA / float(nA), ptOffsetB = ptDeltaB / float(nB);

                // for every AI tile on faces
                for (k = 0; k < nA; k++)
                {
                    for (l = 0; l < nB; l++)
                    {
                        // 1) Calculate coordinates of the AI tile
                        // Object Center + Bone + BV center + face corner + offsets
                        CPlot3D ptAItile = faces[j] + float(k + 0.5f) * ptOffsetA +
                                           float(l + 0.5f) * ptOffsetB;
                        int nOX = Float2AITile(ptAItile.x), nOY = Float2AITile(ptAItile.y);

                        // test tile to be in bounds
                        if (nOX >= XSize() || nOY >= YSize() || nOX < 0 || nOY < 0)
                            continue; // out of bounds

                        float fpObjectHeight = ptAItile.z;
                        int nObjectHeight = GetIntObjectHeight(fpObjectHeight);

                        if (normals[j - 1].z > 0)
                            pPlace->AddMaxTile(nOX, nOY, nObjectHeight, nPassability, nVisibility);
                        else
                            pPlace->AddMinTile(nOX, nOY, nObjectHeight, nPassability, nVisibility);
                        if (k == 0 || l == 0 || k == nA - 1 || l == nB - 1)
                            pPlace->SetEdge(nOX, nOY);
                    }
                }
            }
        }
    }

    if (bBridge)
    {
        bRv = true;
        AddRemoveBridge(placeBridge, bRemove);
    }
    bRv = AddRemoveMapObject(place, bRemove);

    SRectRegion area, areaB;
    place.CalcBoundingRectangle(area);
    placeBridge.CalcBoundingRectangle(areaB);
    area.Union(areaB);
    area.Grow(3);
    bool bStoreGraphRecalc = bEnableGraphRecalc;
    bEnableGraphRecalc = bEnableGraphRecalc && pObject->NeedGraphRecalc();
    ConvertObjMapToPassabilities(area);
    bEnableGraphRecalc = bStoreGraphRecalc;
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::AddDangerArea(int nTime, SAIObjectPlacement &place)
{
    bool bGraphMem = bEnableGraphRecalc;
    bEnableGraphRecalc = false;
    bool bRv = AddRemoveMapObject(place, false);
    SRectRegion area;
    place.CalcBoundingRectangle(area);
    area.Grow(3);
    ConvertObjMapToPassabilities(area);
    nLastDangerAddTime = nTime;
    bEnableGraphRecalc = bGraphMem;
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::RemoveDangerArea(SAIObjectPlacement &place)
{
    bool bGraphMem = bEnableGraphRecalc;
    bEnableGraphRecalc = false;
    bool bRv = AddRemoveMapObject(place, true);
    SRectRegion area;
    place.CalcBoundingRectangle(area);
    area.Grow(3);
    ConvertObjMapToPassabilities(area);
    bEnableGraphRecalc = bGraphMem;
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::CanPutObject(CMapObject *pMapObject, int nX, int nY)
{
    if(pMapObject->WhoAmI()!=HPC_UNIT)return true;
    if(!TileInBounds(nX,nY))return false;
    auto* unit=static_cast<CUnit*>(pMapObject);Navigation::Point point{AITile2Meter(nX+.5f),AITile2Meter(nY+.5f),GetTileHeight(nX,nY)};
    return navigation.CanPlace(unit,point) && CheckCircleCollision(unit,CPlot3D(point.x,point.y,unit->GetCP().z))==nullptr;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::CanPutObject(CMapObject *pMapObject, const CPlot3D &p)
{
    if(pMapObject->WhoAmI()!=HPC_UNIT)return true;
    if(!TileInBounds(Float2AITile(p.x),Float2AITile(p.y)))return false;
    auto* unit=static_cast<CUnit*>(pMapObject);
    return navigation.CanPlace(unit,{p.x,p.y,GetOriginalTileHeight(p)}) && CheckCircleCollision(unit,p)==nullptr;
}
/////////////////////////////////////////////////////////////////////////////////////
// locking operations
/////////////////////////////////////////////////////////////////////////////////////
inline BYTE GetLockCheckValue(float fpRadius)
{
    float fpRes = FP_AI_MAX_LOCK_RADIUS - fpRadius; // - 0.3f );
    int nRes = Max(0, Float2Int(fpRes * 16));
    return nRes;
}
/////////////////////////////////////////////////////////////////////////////////////
// ширины юнитов в зависимости от aiclass
static float fpUnitRadiuses[8] = { 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.7f, 0.7f, 1.5f };
BYTE CAIMap::GetUnitLockCheck(int nUnitClass)
{
    if (nUnitClass >= 0 && nUnitClass < 8)
        return GetLockCheckValue(fpUnitRadiuses[nUnitClass]);
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
BYTE CAIMap::GetUnitLockCheck(CUnit *pUnit)
{
    return GetLockCheckValue(pUnit->GetCollisionDistance());
}
/////////////////////////////////////////////////////////////////////////////////////
CUnit *CAIMap::CheckCircleCollision(CUnit *pUnit, const CPlot3D &newPos)
{
    CUnitList nearUnits;
    POSITION pos;
    float fpRad1 = pUnit->GetCollisionDistance(), fpRad;
    mapObjects.GetUnitsNear(newPos, nearUnits, 10);
    pos = nearUnits.GetHeadPosition();
    while (pos)
    {
        CUnit *pCheck = nearUnits.GetNext(pos);
        if (pUnit == pCheck)
            continue;
        fpRad = sqr(fpRad1 + pCheck->GetCollisionDistance());
        if (distanceXY2(newPos, pCheck->GetCP()) < fpRad && !pCheck->IsDead())
        {
            int nX = Float2AITile(pUnit->GetCP().x);
            int nY = Float2AITile(pUnit->GetCP().y);
            int nOX = Float2AITile(pCheck->GetCP().x);
            int nOY = Float2AITile(pCheck->GetCP().y);
            int nDx, nDy;
            CLockMatrix *pMatrix = GetUnitLockMatrix(pCheck->GetCollisionDistance());
            nDx = N_AI_LOCK_MATRIX_SIZE / 2 + nX - nOX;
            nDy = N_AI_LOCK_MATRIX_SIZE / 2 + nY - nOY;
            int nCheck = GetUnitLockCheck(pUnit);
            if (!pMatrix || nDx < 0 || nDy < 0 || nDx >= N_AI_LOCK_MATRIX_SIZE ||
                nDy >= N_AI_LOCK_MATRIX_SIZE)
                return pCheck;
            if (pMatrix->nLocks[nDy][nDx] >= nCheck)
                return pCheck;
        }
    }
    return 0;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::ConvertLocks(const CPlot3D &ptPlace, bool bLock)
{
    int nX = Meter2AITile(ptPlace.x), nLx, nHx;
    int nY = Meter2AITile(ptPlace.y), nLy, nHy;
    void *pOwner;
    static CPtr<CLockMatrix> pMatrix;
    if (!pMatrix->IsValid())
        pMatrix = dbgnew CLockMatrix;

    pOwner = (void *)(0x8000000 + nX + (nY << 16));
    nLx = Max(0, nX - 7);
    nLy = Max(0, nY - 7);
    nHx = Min(XSize() - 1, nX + 7);
    nHy = Min(YSize() - 1, nY + 7);
    for (int y = nLy; y < nHy; y++)
    {
        for (int x = nLx; x < nHx; x++)
            pMatrix->nLocks[y - nLy][x - nLx] = dLocks[y][x];
    }
    if (bLock)
        staticLocks.Lock(pOwner, pMatrix, nLx, nLy);
    else
        staticLocks.Unlock(pOwner, nLx, nLy);
}
/////////////////////////////////////////////////////////////////////////////////////
// можно ли поставить pObject по указанным координатам, и если нельзя то насколько
// серьезна причина
bool CAIMap::NotLocked(CMapObject *pObject, int nX, int nY, EAILock &res)
{
    int nTypeID = pObject->GetTypeID();
    if (nX < 0 || nX >= XSize() || nY < 0 || nY >= YSize())
        return false;
    if (pObject->WhoAmI() == HPC_UNIT)
    {
        BYTE nCheck = GetUnitLockCheck((CUnit *)pObject);
        res = AI_LOCK_STATIC;
        if (map[nY][nX].IsLocked(nCheck))
            return false;
        res = AI_LOCK_DYNAMIC;
        if (dLocks[nY][nX] > nCheck)
            return false;
    }
    res = AI_LOCK_NO;
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// можно при текущем раскладе блокировок юниту pUnit переместится на (nX, nY)
bool CAIMap::CanMoveTo(CUnit *pUnit, int nX, int nY, EAILock &res)
{
    res=AI_LOCK_STATIC;if(!TileInBounds(nX,nY))return false;
    auto from=navigation.Position(pUnit,*this);Navigation::Location location;
    Navigation::Point target{AITile2Meter(nX+.5f),AITile2Meter(nY+.5f),from.z};
    if(!navigation.Step(pUnit,from,target,location))return false;
    if(CheckCircleCollision(pUnit,CPlot3D(target.x,target.y,pUnit->GetCP().z))){res=AI_LOCK_DYNAMIC;return false;}
    res=AI_LOCK_NO;return true;
}
/////////////////////////////////////////////////////////////////////////////////////
CLockMatrix *CAIMap::GetUnitLockMatrix(float fpRadius)
{
    CLockMatrixDesc d;
    float fpColDistance = fpRadius;
    if (fpColDistance <= 0.2f)
        return 0;
    d.SetRadius(Float2Int(fpColDistance * 10 + 1) * 0.1f);
    return lockMatrixManager.GetData(d);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::AddUnit(CUnit *pUnit, int nX, int nY)
{
    if (pUnit->IsDead())
        return true;
    CLockMatrix *pMatrix = GetUnitLockMatrix(pUnit->GetCollisionDistance());
    if (!pMatrix)
        return true;
    if (pUnit->IsMoving())
        dynamicLocks.Lock(pUnit, pMatrix, nX - N_AI_LOCK_MATRIX_SIZE / 2,
                          nY - N_AI_LOCK_MATRIX_SIZE / 2);
    else
        staticLocks.Lock(pUnit, pMatrix, nX - N_AI_LOCK_MATRIX_SIZE / 2,
                         nY - N_AI_LOCK_MATRIX_SIZE / 2);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::RemoveUnit(CUnit *pUnit, int nX, int nY)
{
    if (pUnit->IsDead())
        return true;
    CLockMatrix *pMatrix = GetUnitLockMatrix(pUnit->GetCollisionDistance());
    if (!pMatrix)
        return true;
    if (pUnit->IsMoving())
        dynamicLocks.Unlock(pUnit, nX - N_AI_LOCK_MATRIX_SIZE / 2, nY - N_AI_LOCK_MATRIX_SIZE / 2);
    else
        staticLocks.Unlock(pUnit, nX - N_AI_LOCK_MATRIX_SIZE / 2, nY - N_AI_LOCK_MATRIX_SIZE / 2);
    return true;
}
/////////////////////////////////////////////////////////////////////////////////////
// Passabilities calculation
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::CalcDefaultPassabilities()
{
    for (int i = 0; i < tilesInfo.GetSize(); i++)
    {
        DWORD dwTerrains = 0;
        for (int kind = 0; kind < 8; kind++)
        {
            int nUnitHeight = Float2Int(fpUnitsHeights[kind] * fpZMult);
            STerrainTile terra;
            if (kind == 0)
                terra.Init(1024, 1, 0, nUnitHeight, true);
            else
            {
                SAITileInfo &info = tilesInfo[i];
                terra.Init(info.nSpeed, info.nCostMultiplier, 0, nUnitHeight, kind != 1);
            }
            dwTerrains += terra.GetMapTerrainIndex() << (kind * 4);
        }
        tilesInfo[i].dwTerrains = dwTerrains;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::SetUnpassableDueToTerrainGradient(int nLowX, int nHighX, int nLowY, int nHighY)
{
    int x, y, nMaxGrad, nMaxGradFly, k;
    nLowX = Max(2, nLowX);
    nLowY = Max(2, nLowY);
    nHighX = Min(XSize() - 3, nHighX);
    nHighY = Min(YSize() - 3, nHighY);
    static int nD[4][2] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 } };
    static int nMask7x7Tiles[13][2] = { { 0, 2 },  { -1, 1 }, { 0, 1 }, { 1, 1 }, { -2, 0 },
                                        { -1, 0 }, { 0, 0 },  { 1, 0 }, { 2, 0 }, { -1, -1 },
                                        { 0, -1 }, { 1, -1 }, { 0, -2 } };
    nMaxGrad = 0;
    while (nElevationMultipliers[nElevationMultipliers.GetSize() / 2 + nMaxGrad] > -1)
        nMaxGrad++;
    nMaxGrad--;
    nMaxGradFly = 0;
    while (nElevationMultipliersFly[nElevationMultipliersFly.GetSize() / 2 + nMaxGradFly] > -1)
        nMaxGradFly++;
    nMaxGradFly--;
    ASSERT(nMaxGradFly >= nMaxGrad);
    for (y = nLowY; y <= nHighY; y++)
    {
        for (x = nLowX; x <= nHighX; x++)
        {
            int nH = map[y][x].nHeight, nDif = 0;
            for (int i = 0; i < 4; i++)
            {
                int nTest = abs(((int)map[y + nD[i][0]][x + nD[i][1]].nHeight) - nH);
                if (nTest > nDif)
                {
                    nDif = nTest;
                    if (nTest > nMaxGradFly)
                        break;
                }
            }
            //
            if (nDif > nMaxGradFly)
            {
                map[y][x].dwTerrains = 0;
                for (k = 0; k < 4; k++)
                    map[y + nD[k][0]][x + nD[k][1]].dwTerrains &= ~N_UNIT_MASK_3X3;
                for (k = 0; k < 13; k++)
                    map[y + nMask7x7Tiles[k][0]][x + nMask7x7Tiles[k][1]]
                        .dwTerrains &= ~N_UNIT_MASK_7X7;
            }
            else if (nDif > nMaxGrad)
            {
                map[y][x].dwTerrains &= N_UNIT_MASK_FLY;
                for (k = 0; k < 4; k++)
                    map[y + nD[k][0]][x + nD[k][1]].dwTerrains &= ~N_UNIT_MASK_3X3;
                for (k = 0; k < 13; k++)
                    map[y + nMask7x7Tiles[k][0]][x + nMask7x7Tiles[k][1]]
                        .dwTerrains &= ~N_UNIT_MASK_7X7;
            }
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
// перенести области напроходимости на nDX, nDY nLowX etc - куда переносить ( регион назначения ) ,
// nDx - смещени
void CAIMap::ShiftUnpassableTerrain(int nKind, int nDX, int nDY, int nLowX, int nHighX, int nLowY,
                                    int nHighY)
{
    int x, y, nMask;
    nLowX = Max(-nDX, nLowX);
    nLowY = Max(-nDY, nLowY);
    nHighX = Min(XSize() - 1 - nDX, nHighX);
    nHighY = Min(YSize() - 1 - nDY, nHighY);
    nMask = ~(15 << (nKind * 4));
    for (y = nLowY; y <= nHighY; y++)
    {
        for (x = nLowX; x <= nHighX; x++)
        {
            if (tempMap[y + nDY][x + nDX])
                continue;
            map[y][x].dwTerrains &= nMask;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::ConvertObjMapToPassabilities(const SRectRegion &area)
{
    if (bDelayedConvert)
        convertRegion.Union(area);
    else
        RealConvertObjMapToPassabilities(area.nLowX, area.nHighX, area.nLowY, area.nHighY);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::StartDelayedConvert()
{
    if (bDelayedConvert)
        FinishDelayedConvert();
    convertRegion.Clear();
    bDelayedConvert = true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::FinishDelayedConvert()
{
    ASSERT(bDelayedConvert);
    RealConvertObjMapToPassabilities(convertRegion.nLowX, convertRegion.nHighX, convertRegion.nLowY,
                                     convertRegion.nHighY);
    bDelayedConvert = false;
}
/////////////////////////////////////////////////////////////////////////////////////
int CAIMap::CalcOneTilePassability(STerrainTile &terra, int nUnitClass, int nUnitHeight, int x,
                                   int y)
{
    int nUnitGround;
    const SAITile &tile = map[y][x];
    const SAIObjTile &tileObj = mapObj[y][x];
    SAITileInfo &info = tilesInfo[tileObj.nTerrainIndex];
    if (tileObj.bEmpty && tile.nDepth == 0)
        return ((info.dwTerrains >> (nUnitClass * 4)) & 15);
    // NOTE can be optimized
    if (nUnitClass == 0)
    {
        // levitating unit
        nUnitGround = tile.nHeight;
        if (tile.nDepth > 0)
            nUnitGround += tile.nDepth;
        terra.Init(1024, 1, nUnitGround, nUnitHeight, N_AI_MAX_OBJECT_HEIGHT * 2);
    }
    else
    {
        // ordinary unit
        int nUnitHeightW = nUnitClass == 1 ? 0 : nUnitHeight;
        nUnitGround = tile.nHeight;
        terra.Init(info.nSpeed, info.nCostMultiplier, nUnitGround, nUnitHeight, nUnitHeightW);
        if (tile.nDepth > 0)
            terra.SetWater(tile.nDepth);
    }
    if (!tileObj.bEmpty)
    {
        if (tileObj.bManyObj)
        {
            CAIObjectsCell *pCell = mapObjects.GetCellByTile(x, y);
            int nIdx = tileObj.nFirstCellIndex;
            while (nIdx >= 0)
            {
                SAICellObjectTile &t = pCell->objectTiles[nIdx];
                terra.AddObstacle(t.nMinHeight, t.nMaxHeight, t.nPassability);
                nIdx = t.nNext;
            }
        }
        else
        {
            terra.AddObstacle(tileObj.nMinObjectHeight, tileObj.nMaxObjectHeight,
                              tileObj.nObjPassability);
        }
    }
    return terra.GetMapTerrainIndex();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::RealConvertObjMapToPassabilities(int nLowX, int nHighX, int nLowY, int nHighY)
{
    int x, y, i, k;
    CUnitCovering *pArea;
    if (nLowX > nHighX || nLowY > nHighY)
        return;
    nLowX = Max(0, nLowX);
    nLowY = Max(0, nLowY);
    nHighX = Min(XSize() - 1, nHighX);
    nHighY = Min(YSize() - 1, nHighY);
    // получим координаты области, в которую будут переносится рассчитанные проходимости
    int nCLowX = nLowX, nCLowY = nLowY, nCHighX = nHighX, nCHighY = nHighY;
    // расширим область вычисления проходимостей
    nLowX -= 3;
    nHighX += 3;
    nLowY -= 3;
    nHighY += 3;
    nLowX = Max(0, nLowX);
    nLowY = Max(0, nLowY);
    nHighX = Min(XSize() - 1, nHighX);
    nHighY = Min(YSize() - 1, nHighY);
    STerrainTile terra;

    for (i = 0; i < 8; i++)
    {
#ifdef __FAST_PASSABILITIES__
        if (i != 3)
            continue;
#endif
        int nUnitHeight = Float2Int(fpUnitsHeights[i] * fpZMult);
        for (y = nLowY; y <= nHighY; y++)
        {
            for (x = nLowX; x <= nHighX; x++)
                tempMap[y][x] = (char)CalcOneTilePassability(terra, i, nUnitHeight, x, y);
        }
        // перенесем рассчитанные проходимости для нужной области в основную карту
        for (y = nCLowY; y <= nCHighY; y++)
        {
            for (x = nCLowX; x <= nCHighX; x++)
            {
                SAITile &tile = map[y][x];
                tile.dwTerrains &= ~(15 << (i * 4));
                tile.dwTerrains |= int(tempMap[y][x]) << (i * 4);
            }
        }
        // расширим области непроходимого террейна
        pArea = GetTerrainArea(i);
        for (k = 0; k < pArea->GetSize(); k++)
            ShiftUnpassableTerrain(i, (*pArea)[k].x, (*pArea)[k].y, nCLowX, nCHighX, nCLowY,
                                   nCHighY);
    }
    SetUnpassableDueToTerrainGradient(nCLowX, nCHighX, nCLowY, nCHighY);
    // set borders to be unpassable
    for (x = nLowX; x <= nHighX; x++)
    {
        map[0][x].dwTerrains = 0;
        map[YSize() - 1][x].dwTerrains = 0;
        map[1][x].dwTerrains = 0;
        map[YSize() - 2][x].dwTerrains = 0;
        map[2][x].dwTerrains = 0;
        map[YSize() - 3][x].dwTerrains = 0;
    }
    for (y = nLowY; y <= nHighY; y++)
    {
        map[y][0].dwTerrains = 0;
        map[y][XSize() - 1].dwTerrains = 0;
        map[y][1].dwTerrains = 0;
        map[y][XSize() - 2].dwTerrains = 0;
        map[y][2].dwTerrains = 0;
        map[y][XSize() - 3].dwTerrains = 0;
    }
    // если пришла пора - перестроим граф
    if (bEnableGraphRecalc)
    {
        POSITION pos;
        // определим область, подлежащую реконструкции
        int nGLX = nLowX / N_AI_GRAPH_CELL_SIZE - 1;
        int nGLY = nLowY / N_AI_GRAPH_CELL_SIZE - 1;
        int nGHX = nHighX / N_AI_GRAPH_CELL_SIZE + 1;
        int nGHY = nHighY / N_AI_GRAPH_CELL_SIZE + 1;
        // уберем лишних свидетелей
        CUnitList units;
        float fpRadius = AITile2Meter(
            sqrt(sqr((nHighX - nLowX) * 0.5f + 1) + sqr((nHighY - nLowY) * 0.5f + 1)));
        GetUnitsNear(CPlot3D(AITile2Meter((nLowX + nHighX) * 0.5f),
                             AITile2Meter((nLowY + nHighY) * 0.5f), 0),
                     units, fpRadius);
        for (pos = units.GetHeadPosition(); pos;)
        {
            CUnit *pUnit = units.GetNext(pos);
            RemoveObject(pUnit);
        }
        // собственно перестройка
        aiGraph.BuildRegion(this, nGLX, nGHX, nGLY, nGHY);
        // вернем свидетелей как ни в чем не бывало
        for (pos = units.GetHeadPosition(); pos;)
        {
            CUnit *pUnit = units.GetNext(pos);
            AddObject(pUnit);
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::GetRouteCosts(CUnit *pUnit, SRouteCostsInfo &info)
{
    GetRouteCosts(pUnit->GetAIClass(), info);
    pUnit->GetSpeeds(info.fpBaseSpeed, info.fpRotationSpeed);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::GetRouteCosts(int nAIClass, SRouteCostsInfo &info)
{
    info.fpBaseSpeed = 0.1f;
    info.fpRotationSpeed = 0.1f;
    info.fpRotationSpeed1 = 1 / info.fpRotationSpeed;
    info.nShift = 4 * nAIClass;
    if (info.nShift == 0)
        info.pElevationCosts = nElevationMultipliersFly.GetData() +
                               nElevationMultipliersFly.GetSize() / 2;
    else
        info.pElevationCosts = nElevationMultipliers.GetData() +
                               nElevationMultipliers.GetSize() / 2;
    info.pTerrainCosts = nTerrainCosts;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::GetRouteCostsPathFind(CUnit *pUnit, SRouteCostsInfo &info)
{
    GetRouteCosts(pUnit, info);
    if (!pUnit->NeedFastPath())
        info.pTerrainCosts = nTerrainCosts;
    else
        info.pTerrainCosts = nTerrainFlatCosts;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::GetRouteSpeeds(CUnit *pUnit, SRouteCostsInfo &info)
{
    pUnit->GetSpeeds(info.fpBaseSpeed, info.fpRotationSpeed);
    info.fpRotationSpeed1 = 1 / info.fpRotationSpeed;
    info.nShift = 4 * pUnit->GetAIClass();
    if (info.nShift == 0)
        info.pElevationCosts = nElevationMultipliersFly.GetData() +
                               nElevationMultipliersFly.GetSize() / 2;
    else
        info.pElevationCosts = nElevationMultipliers.GetData() +
                               nElevationMultipliers.GetSize() / 2;
    info.pTerrainCosts = nTerrainSpeeds;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::BiWaveFindPath(CAIRoute &res, int *nResCost)
{
    bool bRv = true;
    int x, y, cost, costBack;
    // using 2 waves method
    while (1)
    {
        wave.NextWave();
        waveBack.NextWave();
        wave.GetPosition(x, y, cost);
        if (cost != N_INFINITE)
        {
            costBack = waveBack.GetPermanentCost(x, y);
            if (costBack < N_INFINITE)
                break;
            else
            {
                waveBack.GetPosition(x, y, cost);
                if (cost != N_INFINITE)
                {
                    costBack = wave.GetPermanentCost(x, y);
                    if (costBack < N_INFINITE)
                        break;
                }
                else
                {
                    bRv = false;
                    break;
                }
            }
        }
        else
        {
            bRv = false;
            break;
        }
    }
    CAIRoute backRoute;
    if (bRv)
    {
        if (nResCost)
            nResCost[0] = cost + costBack;
        wave.BackTraceRoute(x, y, res);
        waveBack.BackTraceRoute(x, y, backRoute);
        res += backRoute.Invert();
        res.EraseDuplicates();
    }
    waveBack.CleanUp();
    wave.CleanUp();
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::NavRoute(CUnit* unit, Navigation::Point from, Navigation::Point goal, CAIRoute& result, float maxLength, float goalRadius, int aiClass, Navigation::Location* source)
{
    result.Clear();
    float costs[16];SRouteCostsInfo info;
    if(unit)GetRouteCostsPathFind(unit,info);else GetRouteCosts(aiClass,info);
    costs[0]=1;for(int i=1;i<16;++i)costs[i]=float(info.pTerrainCosts[i])/1024.f;
    const auto route=navigation.Find(unit,from,goal,maxLength,goalRadius,costs,aiClass);
    if(route.status!=Navigation::Status::Complete)return false;
    if(source&&!route.points.empty())*source=route.points.front();
    auto append=[&](Navigation::Point p, unsigned area) {
        SRouteUnit node(WORD(Float2AITile(p.x)),WORD(Float2AITile(p.y)));
        node.point=CPlot2D(Meter2AITile(p.x),Meter2AITile(p.y));node.supportZ=p.z;node.terrain=WORD(area);
        if(result.route.GetCount()&&Distance(result.route.GetTail().point,node.point)<.0001f)return;
        result.route.AddTail(node);
    };
    for(size_t i=0;i<route.points.size();++i) {
        auto p=route.points[i].position;
        if(i){auto a=route.points[i-1].position;int n=(std::max)(1,int(std::ceil(Navigation::Distance(a,p)/.25f)));
            if(result.route.GetCount()+n>60000){result.Clear();return false;}
            for(int k=1;k<n;++k){float f=float(k)/n;append({a.x+(p.x-a.x)*f,a.y+(p.y-a.y)*f,a.z+(p.z-a.z)*f},route.points[i-1].area);}}
        append(p,route.points[i].area);
    }
    return result.route.GetCount()>0;
}
bool CAIMap::FindPath(CUnit *pUnit, int nSrcX, int nSrcY, int nDstX, int nDstY, CAIRoute &res, int *nResCost)
{
    auto from=navigation.Position(pUnit,*this);from.x=AITile2Meter(nSrcX+.5f);from.y=AITile2Meter(nSrcY+.5f);
    Navigation::Point goal{AITile2Meter(nDstX+.5f),AITile2Meter(nDstY+.5f),GetTileHeight(nDstX,nDstY)};
    bool ok=NavRoute(pUnit,from,goal,res);if(nResCost)*nResCost=ok?int(res.route.GetCount()):N_INFINITE;return ok;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::FindPath(int nAIClass, int nSrcX, int nSrcY, int nDstX, int nDstY, CAIRoute &res, int *nResCost)
{
    if(!TileInBounds(nSrcX,nSrcY)||!TileInBounds(nDstX,nDstY))return false;
    Navigation::Point from{AITile2Meter(nSrcX+.5f),AITile2Meter(nSrcY+.5f),GetTileHeight(nSrcX,nSrcY)};
    Navigation::Point goal{AITile2Meter(nDstX+.5f),AITile2Meter(nDstY+.5f),GetTileHeight(nDstX,nDstY)};
    bool ok=NavRoute(nullptr,from,goal,res,1.e6f,0,nAIClass);if(nResCost)*nResCost=ok?int(res.route.GetCount()):N_INFINITE;return ok;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::BackTracePath(int nX, int nY, CAIRoute &res)
{
    res.Clear();
    if (nX < 0 || nX >= XSize() || nY < 0 || nY >= YSize())
        return;
    while (tempMap[nY][nX] != NULL_DIR)
    {
        res.AddHeadUnit(nX, nY);
        switch (tempMap[nY][nX])
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
    res.AddHeadUnit(nX, nY);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::ClearMapForLongBackTrace(CAIRoute &route)
{
    int x, y;
    if (route.route.GetCount() < 1)
        return;
    POSITION pos = route.route.GetHeadPosition();
    int nMinX = route.route.GetAt(pos).nX, nMinY = route.route.GetAt(pos).nY;
    int nMaxX = nMinX, nMaxY = nMinY;
    while (pos)
    {
        SRouteUnit &r = route.route.GetNext(pos);
        nMinX = Min(int(r.nX), nMinX);
        nMinY = Min(int(r.nY), nMinY);
        nMaxX = Max(int(r.nX), nMaxX);
        nMaxY = Max(int(r.nY), nMaxY);
    }
    nMaxX++;
    nMaxY++;
    nMinX *= N_AI_GRAPH_CELL_SIZE;
    nMaxX *= N_AI_GRAPH_CELL_SIZE;
    nMinY *= N_AI_GRAPH_CELL_SIZE;
    nMaxY *= N_AI_GRAPH_CELL_SIZE;
    nMinX -= N_MAP_WAVE_SEARCH_SIZE;
    nMaxX += N_MAP_WAVE_SEARCH_SIZE;
    nMinY -= N_MAP_WAVE_SEARCH_SIZE;
    nMaxY += N_MAP_WAVE_SEARCH_SIZE;
    nMinX = Max(0, nMinX);
    nMaxX = Min(XSize(), nMaxX);
    nMinY = Max(0, nMinY);
    nMaxY = Min(YSize(), nMaxY);
    for (y = nMinY; y < nMaxY; y++)
    {
        for (x = nMinX; x < nMaxX; x++)
            tempMap[y][x] = 0;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
struct SWishPoint
{
    int nX, nY;   // координаты, в которые нужно прийти
    POSITION pos; // если не пришли, то кто виноват
    SWishPoint() = default;
    SWishPoint(int x, int y, POSITION ppos)
        : nX(x)
        , nY(y)
        , pos(ppos)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::FindGraphPath(CUnit *pUnit, int nSrcX, int nSrcY, int nDstX, int nDstY, SAIPath &res, bool bTryWalkAround, int nMaxPathLength)
{
    if(!TileInBounds(nSrcX,nSrcY)||!TileInBounds(nDstX,nDstY))return false;
    auto from=navigation.Position(pUnit,*this);from.x=AITile2Meter(nSrcX+.5f);from.y=AITile2Meter(nSrcY+.5f);
    Navigation::Point goal{AITile2Meter(nDstX+.5f),AITile2Meter(nDstY+.5f),GetTileHeight(nDstX,nDstY)};
    return NavRoute(pUnit,from,goal,res.toGo,AITile2Meter(float(nMaxPathLength)));
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::CalcSpeeds(CUnit *pUnit, CAIRoute &route)
{
    SRouteCostsInfo speeds,costs;GetRouteSpeeds(pUnit,speeds);GetRouteCosts(pUnit,costs);
    for(POSITION pos=route.route.GetHeadPosition();pos;) {
        auto& current=route.route.GetNext(pos);const auto& next=pos?route.route.GetAt(pos):current;
        if(!TileInBounds(current.nX,current.nY)||!TileInBounds(next.nX,next.nY))continue;
        const int area=current.terrain;int speed=nTerrainSpeeds[area],cost=nTerrainCosts[area];
        float distance=AITile2Meter(Distance(current.point,const_cast<CPlot2D&>(next.point)));
        float dz=next.supportZ-current.supportZ,angle=std::atan2(std::abs(dz),(std::max)(.001f,distance));
        float multiplier=std::pow(1+std::pow(std::sin(angle),2.f),2.f);if(dz<0)multiplier=1/std::sqrt(multiplier);
        current.wSpeed256=WORD((std::max)(1,(std::min)(65535,int(speed*N_RT_SPEED/1024.f/multiplier))));
        current.SetFlags(false,cost>N_AI_RUN_COST_LIMIT);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::CanRun(CUnit *pUnit)
{
    SRouteCostsInfo costsInfo;
    GetRouteCosts(pUnit, costsInfo);
    int nDstX = Float2AITile(pUnit->GetCP().x);
    int nDstY = Float2AITile(pUnit->GetCP().y);
    if (!TileInBounds(nDstX, nDstY))
        return false;
    if (TileIsDangerous(nDstX, nDstY))
    {
        STerrainTile terra;
        int nAIClass = pUnit->GetAIClass();
        int nUnitHeight = Float2Int(fpUnitsHeights[nAIClass] * fpZMult);
        CalcOneTilePassability(terra, nAIClass, nUnitHeight, nDstX, nDstY);
        return terra.nCost < N_AI_RUN_COST_LIMIT;
    }
    SAITile &srcTile = map[nDstY][nDstX];
    int nCost = srcTile.GetTerrainCost(costsInfo);
    return nCost > 0 && nCost < N_AI_RUN_COST_LIMIT;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::CanPutUnit(CUnit *pUnit)
{
    return navigation.CanPlace(pUnit,navigation.Position(pUnit,*this));
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::CanPutUnit(int nAIClass, const CPlot3D &ptPos)
{
    if(!TileInBounds(Float2AITile(ptPos.x),Float2AITile(ptPos.y)))return false;
    return navigation.CanPlace(nullptr,{ptPos.x,ptPos.y,GetOriginalTileHeight(ptPos)},nAIClass);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::CanPutUnitRelaxed(CUnit *pUnit)
{
    return CanPutUnit(pUnit);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::FindUnitPosition(CUnit *pUnit, const CPlot3D &ptBasePoint, CPlot3D &ptResPos)
{
    auto from=navigation.Position(pUnit,*this);from.x=ptBasePoint.x;from.y=ptBasePoint.y;from.z=GetOriginalTileHeight(ptBasePoint);
    auto route=navigation.Find(pUnit,from,{ptResPos.x,ptResPos.y,GetOriginalTileHeight(ptResPos)},1.e6f,1.f);
    if(route.status!=Navigation::Status::Complete||route.points.empty())return false;
    auto p=route.points.back().position;ptResPos.x=p.x;ptResPos.y=p.y;return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::FindPathDestination(CUnit *pUnit, CPlot3D &finish, bool bAccountColor, float fpMinDistance)
{
    auto route=navigation.Find(pUnit,navigation.Position(pUnit,*this),{finish.x,finish.y,GetOriginalTileHeight(finish)},1.e6f,fpMinDistance);
    if(route.status!=Navigation::Status::Complete||route.points.empty())return false;
    auto p=route.points.back().position;finish.x=Meter2AITile(p.x);finish.y=Meter2AITile(p.y);return true;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::CreatePathSegment(CUnit *pUnit, SAIPath &path)
{
    CAIFullRoute &s = path.curSegment;
    s.Clear();
    s.SetStartPos(pUnit->GetCP(), -pUnit->GetYAxis(), static_cast<CWorldServer*>(pUnit->GetWorld())->GetSegmentTimeCounter() - 1);
    while (s.route.route.GetCount() < N_AI_MAX_SEGMENT_SIZE && path.toGo.route.GetCount() > 0)
        s.route.route.AddTail(path.toGo.route.RemoveHead());
    if (path.toGo.route.GetCount() > 0)
    {
        path.bFinished = false;
        SRouteUnit &last = s.route.route.GetTail();
        path.toGo.route.AddHead(last);
        s.MakeMovement(last.point.x, last.point.y);
    }
    else
    {
        path.bFinished = true;
        s.MakeMovement(path.ptFinish.x, path.ptFinish.y);
    }
    // Speeds and exact corner points were assigned before segmenting.
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::NewPath(CUnit *pUnit, const CPlot3D &ptDst, SAIPath &res, float fpMaxPathLength, float fpMinDistance)
{
    res.toGo.Clear();res.ptDestination=ptDst;res.bRouteFailed=false;
    if(!std::isfinite(ptDst.x)||!std::isfinite(ptDst.y)||!TileInBounds(Float2AITile(ptDst.x),Float2AITile(ptDst.y)))return false;
    auto from=navigation.Position(pUnit,*this);
    Navigation::Point goal{ptDst.x,ptDst.y,GetOriginalTileHeight(ptDst)}; // ptDst.z is a command distance, not world height.
    if(!NavRoute(pUnit,from,goal,res.toGo,fpMaxPathLength,fpMinDistance,3,&pUnit->navLocation)) {res.bRouteFailed=true;return false;}
    auto& last=res.toGo.route.GetTail();res.ptFinish=last.point;
    if(res.toGo.route.GetCount()<2){res.toGo.Clear();return false;}
    // NavRoute returned the stable address including the actual profile.
    res.nPathCost=int(res.toGo.route.GetCount());res.bFirstTry=false;
    CalcSpeeds(pUnit,res.toGo);CreatePathSegment(pUnit,res);return true;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::ContinuePath(CUnit *pUnit, SAIPath &res)
{
    bool bRv = !res.IsPathFinished();
    ASSERT(bRv);
    if (res.bRouteFailed && res.toGo.route.GetCount() == 0)
    {
        bRv = NewPath(pUnit, res.ptDestination, res);
    }
    else
    {
        CreatePathSegment(pUnit, res);
        if (res.bRouteFailed)
            res.bFinished = false;
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
float CAIMap::GetTileHeight(const CPlot3D &p) const
{
    int nTileX = Float2AITile(p.x), nTileY = Float2AITile(p.y);
    if (!TileInBounds(nTileX, nTileY))
        return 0;
    return GetTileHeight(nTileX, nTileY);
}

float CAIMap::GetOriginalTileHeight(const CPlot3D &p) const
{
    int nTileX = Float2AITile(p.x), nTileY = Float2AITile(p.y);
    if (!TileInBounds(nTileX, nTileY))
        return 0;
    if (map[nTileY][nTileX].bBridge)
        return ((float)(terrainKeeper.GetSavedHeight(nTileX, nTileY))) / fpZMult;
    return GetTileHeight(nTileX, nTileY);
}

float CAIMap::GetTileSurface(const CPlot3D &p) const
{
    int nTileX = Float2AITile(p.x), nTileY = Float2AITile(p.y);
    if (!TileInBounds(nTileX, nTileY))
        return 0;
    SAITile &tile = map[nTileY][nTileX];
    return float(tile.nHeight + tile.nDepth) / fpZMult;
}

bool CAIMap::IsBridge(const CPlot3D &p) const
{
    int nTileX = Float2AITile(p.x), nTileY = Float2AITile(p.y);
    if (!TileInBounds(nTileX, nTileY))
        return false;
    return IsTileOnBridge(nTileX, nTileY);
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::IsWater(const CPlot3D &p) const
{
    int nTileX = Float2AITile(p.x), nTileY = Float2AITile(p.y);
    if (!TileInBounds(nTileX, nTileY))
        return false;
    return map[nTileY][nTileX].bWaterFlag;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::TileIsDangerous(int nX, int nY)
{
    SAIObjTile &otile = mapObj[nY][nX];
    if (!otile.bEmpty)
    {
        if (otile.bManyObj)
        {
            {
                CAIObjectsCell *pCell = mapObjects.GetCellByTile(nX, nY);
                int nIdx;
                nIdx = otile.nFirstCellIndex;
                while (nIdx >= 0)
                {
                    SAICellObjectTile &t = pCell->objectTiles[nIdx];
                    if (t.nPassability == AI_PASS_DANGER)
                        return true;
                    nIdx = t.nNext;
                }
            }
        }
        else
        {
            if (otile.nObjPassability == AI_PASS_DANGER)
                return true;
        }
    }
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
bool CAIMap::IsDangerous(const CPlot3D &ptPlace)
{
    int nX = Float2AITile(ptPlace.x), nY = Float2AITile(ptPlace.y);
    if (!TileInBounds(nX, nY))
        return false;
    return TileIsDangerous(nX, nY);
}
/////////////////////////////////////////////////////////////////////////////////////
int CAIMap::GetTerraType(const CPlot3D &p) const
{
    int nX = Float2AITile(p.x), nY = Float2AITile(p.y);
    if (!TileInBounds(nX, nY))
        return -1;
    return mapObj[nY][nX].nTerrainIndex;
}
/////////////////////////////////////////////////////////////////////////////////////
// for some visualization of aimap
void CAIMap::DrawMap(int nUnit, const char *lpszFileName)
{
    char letters[] = " 123456789ABCDEF";
    char hletters[] = "абвгдежзийклмнопрстуфхцчшщьыъэюя";
    //	fstream f( lpszFileName );
    FILE *f = fopen(lpszFileName, "wb");
    if (!f)
        return;
    CAIMap &map = theWorldServer.aiMap;
    int nCounter = 0, nCounterM = 0;
    for (int y = 0; y < map.YSize(); y++)
    {
        CString szLine;
        for (int x = 0; x < map.XSize(); x++)
        {
            int nTerrain = (map.map[y][x].dwTerrains >> (nUnit * 4)) & 15;
            int nHTerrain = (map.map[y][x].nHeight / 6) & 31;
            char szTmp[2] = { 0, 0 };
            szTmp[0] = letters[nTerrain];
            szLine += szTmp;
            szTmp[0] = hletters[nHTerrain];
            szLine += szTmp;
            int tx = x / N_AI_GRAPH_CELL_SIZE;
            int ty = y / N_AI_GRAPH_CELL_SIZE;
            if (aiGraph.places[nUnit][ty][tx].nX + tx * N_AI_GRAPH_CELL_SIZE == x &&
                aiGraph.places[nUnit][ty][tx].nY + ty * N_AI_GRAPH_CELL_SIZE == y)
                szLine += "*";
            else
                szLine += " ";
            if (!mapObj[y][x].bEmpty)
                nCounter++;
            if (!mapObj[y][x].bEmpty && mapObj[y][x].bManyObj)
                nCounterM++;
        }
        szLine += "\n";
        fprintf(f, "%s", (const char *)szLine);
    }
    fprintf(f, "Total tiles = %i\n", map.XSize() * map.YSize());
    fprintf(f, "With one object %i\n", nCounter);
    fprintf(f, "With multiple objects %i\n", nCounterM);
    fclose(f);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::OrientObject(CMapObject *pObject, const CQuaternion &q)
{
    RemoveObject(pObject);
    pObject->SetOrientation(q);
    AddObject(pObject);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIMap::OrientObject(CMapObject *pObject, const CPlot3D &axisZ)
{
    RemoveObject(pObject);
    pObject->SetOrientation(axisZ);
    AddObject(pObject);
}
/////////////////////////////////////////////////////////////////////////////////////
SAITileInfo &CAIMap::GetTileInfo(const CPlot3D &pos)
{
    ASSERT(tilesInfo.GetSize());
    int nX = Float2AITile(pos.x), nY = Float2AITile(pos.y);
    if (!TileInBounds(nX, nY))
        return tilesInfo[0];
    return tilesInfo[mapObj[nY][nX].nTerrainIndex];
}
/////////////////////////////////////////////////////////////////////////////////////
float CAIMap::GetTerrainGrad(const CPlot3D &ptPos)
{
    int nX = Float2AITile(ptPos.x), nY = Float2AITile(ptPos.y);
    if (nX < 1 || nY < 1 || nX > XSize() - 2 || nY > YSize() - 2)
        return 0;
    int nRes = 0;
    int nH = map[nY][nX].nHeight;
    nRes = Max(nRes, abs(((int)map[nY + 1][nX].nHeight) - nH));
    nRes = Max(nRes, abs(((int)map[nY - 1][nX].nHeight) - nH));
    nRes = Max(nRes, abs(((int)map[nY][nX + 1].nHeight) - nH));
    nRes = Max(nRes, abs(((int)map[nY][nX - 1].nHeight) - nH));
    return nRes / fpZMult;
}
/////////////////////////////////////////////////////////////////////////////////////
// constants for accounting of opacity during map tracing
static float fpOpacities[5] = { 0, 0.98f, 0.99f, 1 };
/////////////////////////////////////////////////////////////////////////////////////
// trace line, return visibility
float CAIMap::TraceRelief(const CPlot3D &src, const CPlot3D &dst)
{
    int nLeng, nX, nY, nDX, nDY, nZ, nDZ, i;
    float fpVisibility;
    // initialize line dx/dy for tracing on terrain
    CPlot3D dif = dst - src;
    float fpLeng = fabs(dif) / (FP_CL_MP_SIZE / 2), fpLeng1;
    if (fpLeng == 0)
        return 1;
    fpLeng1 = 1 / fpLeng;
    nLeng = Float2Int(fpLeng);
    nX = Float2Int(src.x * 0x10000 / (FP_CL_MP_SIZE / 2));
    nY = Float2Int(src.y * 0x10000 / (FP_CL_MP_SIZE / 2));
    nZ = GetMapHeight(src.z) << 16;
    nDX = Float2Int(dif.x * 0x10000 * fpLeng1 / (FP_CL_MP_SIZE / 2));
    nDY = Float2Int(dif.y * 0x10000 * fpLeng1 / (FP_CL_MP_SIZE / 2));
    nDZ = Float2Int((GetMapHeight(dst.z) - (nZ >> 16)) * 0x10000 * fpLeng1);

    if (!TileInBounds(nX >> 16, nY >> 16) ||
        !TileInBounds((nX + nDX * nLeng) >> 16, (nY + nDY * nLeng) >> 16))
        return 0;

    fpVisibility = 1;
    // trace it
    nX += nDX;
    nY += nDY;
    nZ += nDZ; // make one step to the future
    for (i = 0; i < nLeng; i++)
    {
        int nH = nZ >> 16;
        SAITile &tile = map[nY >> 16][nX >> 16];
        SAIObjTile &otile = mapObj[nY >> 16][nX >> 16];
        if (((int)tile.nHeight) >= nH)
            return 0;
        if (!otile.bEmpty)
        {
            if (otile.bManyObj)
            {
                {
                    CAIObjectsCell *pCell = mapObjects.GetCellByTile(nX >> 16, nY >> 16);
                    int nIdx;
                    nIdx = otile.nFirstCellIndex;
                    while (nIdx >= 0)
                    {
                        SAICellObjectTile &t = pCell->objectTiles[nIdx];
                        if (t.nMaxHeight >= nH && t.nMinHeight <= nH)
                        {
                            fpVisibility *= fpOpacities[t.nVisibility];
                            if (fpVisibility < 1e-4f)
                                return 1e-4f;
                        }
                        nIdx = t.nNext;
                    }
                }
            }
            else
            {
                if (int(otile.nMaxObjectHeight) >= nH && int(otile.nMinObjectHeight) <= nH)
                {
                    fpVisibility *= fpOpacities[otile.nObjVisibility];
                    if (fpVisibility < 1e-4f)
                        return 1e-4f;
                }
            }
        }
        nX += nDX;
        nY += nDY;
        nZ += nDZ;
    }
    return fpVisibility;
}
/////////////////////////////////////////////////////////////////////////////////////
float CAIMap::TraceReliefDH(const CPlot3D &src, const CPlot3D &dst)
{
    int nLeng, nX, nY, nDX, nDY, i;
    // initialize line dx/dy for tracing on terrain
    CPlot3D dif = dst - src;
    float fpLeng = fabs(dif) / (FP_CL_MP_SIZE / 2), fpLeng1;
    if (fpLeng == 0)
        return 1;
    fpLeng1 = 1 / fpLeng;
    nLeng = Float2Int(fpLeng);
    nX = Float2Int(src.x * 0x10000 / (FP_CL_MP_SIZE / 2));
    nY = Float2Int(src.y * 0x10000 / (FP_CL_MP_SIZE / 2));
    nDX = Float2Int(dif.x * 0x10000 * fpLeng1 / (FP_CL_MP_SIZE / 2));
    nDY = Float2Int(dif.y * 0x10000 * fpLeng1 / (FP_CL_MP_SIZE / 2));

    if (!TileInBounds(nX >> 16, nY >> 16) ||
        !TileInBounds((nX + nDX * nLeng) >> 16, (nY + nDY * nLeng) >> 16))
        return 100;

    unsigned short nPrevH = map[nY >> 16][nX >> 16].nHeight; // nMinH = 0xffff, nMaxH = 0;
    int nRes = 0;
    // trace it
    nX += nDX;
    nY += nDY; // make one step to the future
    for (i = 0; i < nLeng; i++)
    {
        SAITile &tile = map[nY >> 16][nX >> 16];
        int nDH = int(tile.nHeight) - int(nPrevH);
        nRes = Max(nRes, abs(nDH));
        nPrevH = tile.nHeight;
        // nMinH = Min( nMinH, tile.nHeight );
        // nMaxH = Max( nMaxH, tile.nHeight );
        nX += nDX;
        nY += nDY;
    }
    return nRes / fpZMult; //(nMaxH - nMinH) / fpZMult;
}
/////////////////////////////////////////////////////////////////////////////////////
// определить все тайлы из которых видно требуемый объект
void CAIMap::TraceVisionToArea(const CPlot3D &ptSrc, float fpSenseRadius,
                               const CPlot3D &ptVisionDir, float fpVisionArc, float fpTargetH,
                               SVisibilityMask &res)
{
    // VERY DUMB, like me ;)
    int nRadius, x, y;
    int nCenterX = Float2AITile(ptSrc.x);
    int nCenterY = Float2AITile(ptSrc.y);
    nRadius = Float2AITile(fpSenseRadius) + 1;
    res.Extend(nCenterX, nCenterY, nRadius);
    bool bOmitDirection = fpVisionArc <= -1;
    bool bOmitTrace = fpTargetH < -100;
    for (y = nCenterY - nRadius; y < nCenterY + nRadius; y++)
    {
        for (x = nCenterX - nRadius; x < nCenterX + nRadius; x++)
        {
            int nCurX = x - res.nShiftX;
            int nCurY = y - res.nShiftY;
            // NEED much faster code
            if (!TileInBounds(x, y))
                continue;
            CPlot3D ptTarget(AITile2Meter(x), AITile2Meter(y), 0);
            CPlot3D ptToTarget = ptTarget - ptSrc;
            float fpDist = (float)sqrt(sqr(ptToTarget.x) + sqr(ptToTarget.y));
            if (fpSenseRadius < fpDist)
                continue;
            if (!bOmitDirection)
            {
                // check up direction
                if ((ptToTarget * ptVisionDir) < fpVisionArc * fpDist)
                    continue;
            }
            if (!bOmitTrace)
            {
                ptTarget.z = GetTileHeight(x, y) + fpTargetH;
                float fpTrSenseRadius = fpSenseRadius;
                fpTrSenseRadius *= TraceRelief(ptSrc, ptTarget);
                if (fpTrSenseRadius < fpDist)
                    continue;
            }
            res.data[nCurY][nCurX] = 1;
        }
    }
}
/////////////////////////////////////////////////////////////////////////////////////
