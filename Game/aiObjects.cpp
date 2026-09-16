/**********************************************************************
 *<
        FILE: aiObjects.cpp

        DESCRIPTION:  AIObjects implementayion

*>	Copyright (c) 1999, Nival Interactive
 **********************************************************************/

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiMap.h"
#include "unit.h"

/////////////////////////////////////////////////////////////////////////////////////
#ifndef _DEBUG
#pragma optimize("agw", on)
#endif
/////////////////////////////////////////////////////////////////////////////////////
// monotonous function inversion with binary search
class CTableFuncInvert
{
    CArray<float, float> fpTable;
    int nPasses, nMask;

public:
    void BuildTable(float *fpData, int nSize);
    int Get(float fpVal);
};
/////////////////////////////////////////////////////////////////////////////////////
void CTableFuncInvert::BuildTable(float *fpData, int nSize)
{
    int nStepNum, i;
    nPasses = 1;
    while ((1 << nPasses) < nSize)
        nPasses++;
    fpTable.SetSize(2 << nPasses);
    nMask = (1 << nPasses) - 1;
    nStepNum = 0;
    while (nStepNum <= nPasses)
    {
        int nIdx = 1 << nStepNum;
        int nNum = nIdx;
        int nShift = nPasses == nStepNum ? 0 : (1 << (nPasses - nStepNum - 1)) - 1;
        for (i = 0; i < nNum; i++)
        {
            int nSrcIdx = (1 << (nPasses - nStepNum)) * i + nShift;
            if (nSrcIdx >= nSize)
                nSrcIdx = nSize - 1;
            fpTable[nIdx + i] = fpData[nSrcIdx];
        }
        nStepNum++;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
int CTableFuncInvert::Get(float fpVal)
{
    int nIdx = 1, i = nPasses;
    int nVal = *(int *)(&fpVal);
    int *nData = (int *)fpTable.GetData();
    for (; i > 0; i--)
    {
        nIdx = (nIdx << 1) + (nVal > nData[nIdx]);
    }
    return nIdx & nMask;
}
/////////////////////////////////////////////////////////////////////////////////////
// grid points creating & sorting
const int N_AI_MAX_GRID_DIST = 10;
/////////////////////////////////////////////////////////////////////////////////////
struct SAIGridSortPoint
{
    SAIGridPoint pnt;
    float fpDist;
    SAIGridSortPoint() = default;
    SAIGridSortPoint(int nX, int nY)
    {
        pnt.nDX = nX;
        pnt.nDY = nY;
        fpDist = 0;
    }
};

/////////////////////////////////////////////////////////////////////////////////////
static CArray<SAIGridPoint, SAIGridPoint &> aiGridPoints;
static CTableFuncInvert aiGridDist2Number;
static CArray<int, int> aiGridNumbers;
/////////////////////////////////////////////////////////////////////////////////////
struct SAIGridInit
{
    SAIGridInit();
};
static SAIGridInit aiGridInit;

/////////////////////////////////////////////////////////////////////////////////////
static int __cdecl CmpGridPoints(const void *pAv, const void *pBv)
{
    SAIGridSortPoint *pA = (SAIGridSortPoint *)pAv, *pB = (SAIGridSortPoint *)pBv;
    if (pA->fpDist > pB->fpDist)
        return 1;
    if (pA->fpDist == pB->fpDist)
        return 0;
    return -1;
}

/////////////////////////////////////////////////////////////////////////////////////
SAIGridInit::SAIGridInit()
{
    int nSize = N_AI_MAX_GRID_DIST * 2 + 1, x, y, i;
    CArray<SAIGridSortPoint, SAIGridSortPoint &> sort;
    sort.SetSize(nSize * nSize);
    aiGridPoints.SetSize(nSize * nSize);
    i = 0;
    for (y = -N_AI_MAX_GRID_DIST; y <= N_AI_MAX_GRID_DIST; y++)
    {
        for (x = -N_AI_MAX_GRID_DIST; x <= N_AI_MAX_GRID_DIST; x++)
        {
            int nrx, nry;
            float fpDist;
            nrx = x == 0 ? 0 : abs(x) - 1;
            nry = y == 0 ? 0 : abs(y) - 1;
            fpDist = (float)sqrt(sqr(nrx) + sqr(nry));
            sort[i] = SAIGridSortPoint(x, y);
            sort[i].fpDist = fpDist;
            i++;
        }
    }
    qsort(sort.GetData(), sort.GetSize(), sizeof(SAIGridSortPoint), CmpGridPoints);
    for (i = 0; i < sort.GetSize(); i++)
        aiGridPoints[i] = sort[i].pnt;
    // form arrays for back conversion distance to number of grid points
    CArray<float, float> fpDistances;
    aiGridNumbers.SetSize(0);
    fpDistances.Add(0);
    aiGridNumbers.Add(1);
    for (i = 0; i < sort.GetSize(); i++)
    {
        if (sort[i].fpDist == fpDistances[fpDistances.GetSize() - 1])
        {
            aiGridNumbers[aiGridNumbers.GetSize() - 1] = i + 1;
        }
        else
        {
            fpDistances.Add(sort[i].fpDist);
            aiGridNumbers.Add(i + 1);
        }
    }
    fpDistances.Add(1e30f);
    aiGridNumbers.Add(aiGridPoints.GetSize());
    aiGridDist2Number.BuildTable(fpDistances.GetData(), fpDistances.GetSize());
}

/////////////////////////////////////////////////////////////////////////////////////
static int GetGridPointsNumber(float fpDist)
{
    int nIdx = aiGridDist2Number.Get(fpDist);
    return aiGridNumbers[nIdx];
}

/////////////////////////////////////////////////////////////////////////////////////
static SAIGridPoint *GetGridPoints(float fpDist, int &nGridCount)
{
    nGridCount = GetGridPointsNumber(fpDist);
    return aiGridPoints.GetData();
}

/////////////////////////////////////////////////////////////////////////////////////
SAIGridPoint *CAIGridMesh::GetGridPoints(float fpDist, int &nCount)
{
    if (fpDist == fpPrevDist)
        nCount = nPrevCount;
    else
    {
        nCount = GetGridPointsNumber(fpDist);
        nPrevCount = nCount;
        fpPrevDist = fpDist;
    }
    return aiGridPoints.GetData();
}

/////////////////////////////////////////////////////////////////////////////////////
// CAIObjectsCell methods

CAIObjectsCell::~CAIObjectsCell()
{
    Clear();
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIObjectsCell::Clear()
{
    nFreeTile = -1;
    objectTiles.RemoveAll();
    mapObjects.RemoveAll();
    units.RemoveAll();
}

/////////////////////////////////////////////////////////////////////////////////////
int CAIObjectsCell::GetFreeTileIndex()
{
    int nRes;
    if (nFreeTile >= 0)
    {
        nRes = nFreeTile;
        nFreeTile = objectTiles[nRes].nNext;
    }
    else
    {
        objectTiles.SetSize(objectTiles.GetSize() + 1);
        nRes = objectTiles.GetSize() - 1;
    }
    return nRes;
}

/////////////////////////////////////////////////////////////////////////////////////
int CAIObjectsCell::RemoveTile(int nFirst, int nMinH, int nMaxH, int nPass, int nVis)
{
    bool bRemoved = false;
    short nRes = nFirst, *nPtr, nTmp;
    nPtr = &nRes;
    while (nPtr[0] >= 0)
    {
        SAICellObjectTile &cell = objectTiles[nPtr[0]];
        if (cell.nMaxHeight == nMaxH && cell.nMinHeight == nMinH && cell.nPassability == nPass &&
            cell.nVisibility == nVis)
        {
            nTmp = cell.nNext;
            cell.nNext = nFreeTile;
            nFreeTile = nPtr[0];
            nPtr[0] = nTmp;
            bRemoved = true;
            break;
        }
        nPtr = &cell.nNext;
    }
    ASSERT(bRemoved);
    return nRes;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIObjectsCell::RemoveTile(int nIdx)
{
    objectTiles[nIdx].nNext = nFreeTile;
    nFreeTile = nIdx;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIObjectsCell::AddObjectInCell(CMapObject *pObject)
{
#ifdef _DEBUG
    ASSERT(mapObjects.Find(pObject) == NULL);
#endif
    if (pObject->WhoAmI() == HPC_UNIT)
        units.AddTail((CUnit *)pObject);
    else
        mapObjects.AddTail(pObject);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIObjectsCell::RemoveObjectFromCell(CMapObject *pObject)
{
    POSITION posToRemove = NULL;
    if (pObject->WhoAmI() == HPC_UNIT)
    {
        posToRemove = units.Find((CUnit *)pObject);
        if (posToRemove == NULL)
            return false; // map object not found
        units.RemoveAt(posToRemove);
    }
    else
    {
        posToRemove = mapObjects.Find(pObject);
        if (posToRemove == NULL)
            return false; // map object not found
        mapObjects.RemoveAt(posToRemove);
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
// CAIObjects methods
/////////////////////////////////////////////////////////////////////////////////////
CAIObjects::CAIObjects(int nMapSizeX, int nMapSizeY, CAIMap *pTileMap)
{
    nObjectsMapSizeX = nMapSizeX / AI_N_OM_CELL_SIZE;
    nObjectsMapSizeY = nMapSizeY / AI_N_OM_CELL_SIZE;
    nMapObjectsNumber = 0;

    objectsMap.SetSize(nObjectsMapSizeX * nObjectsMapSizeY);
    pMap = pTileMap;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIObjects::Set(int nMapSizeX, int nMapSizeY, CAIMap *pTileMap)
{
    nObjectsMapSizeX = nMapSizeX / AI_N_OM_CELL_SIZE;
    nObjectsMapSizeY = nMapSizeY / AI_N_OM_CELL_SIZE;
    nMapObjectsNumber = 0;

    objectsMap.SetSize(nObjectsMapSizeX * nObjectsMapSizeY);
    pMap = pTileMap;
}

/////////////////////////////////////////////////////////////////////////////////////
CMapObjectList *CAIObjects::GetCellList(int nCellX, int nCellY)
{
    if (nCellX < 0 || nCellY < 0 || nCellX >= nObjectsMapSizeX || nCellY >= nObjectsMapSizeY)
        return NULL;

    return objectsMap[nCellY * nObjectsMapSizeX + nCellX].GetObjectsList();
}

/////////////////////////////////////////////////////////////////////////////////////
CUnitList *CAIObjects::GetUnitCellList(int nCellX, int nCellY)
{
    if (nCellX < 0 || nCellY < 0 || nCellX >= nObjectsMapSizeX || nCellY >= nObjectsMapSizeY)
        return NULL;

    return objectsMap[nCellY * nObjectsMapSizeX + nCellX].GetUnitsList();
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIObjects::GetObjectsNear(const CPlot3D &point, CMapObjectList &list, float fpRadius)
{
    int nGridCount;
    SAIGridPoint *pGrid = GetGridPoints(fpRadius / GetCellSize(), nGridCount);

    list.RemoveAll();

    int nUnitX = Float2AITile(point.x), nUnitY = Float2AITile(point.x);

    int nUnitCellX = nUnitX / GetCellSize(), nUnitCellY = nUnitY / GetCellSize();

    float fpCenterX = point.x, fpCenterY = point.y;

    fpRadius *= fpRadius;
    for (int i = 0; i < nGridCount; i++)
    {
        // Get map object list on the i-th cell surrounding unit
        CMapObjectList *pMOList = pMap->GetObjectCellList(nUnitCellX + pGrid[i].nDX,
                                                          nUnitCellY + pGrid[i].nDY);
        if (pMOList == NULL)
            continue;

        POSITION pos = pMOList->GetHeadPosition();
        // if this stuff will take much time some optimizations could be made about speeding it up
        while (pos)
        {
            float fpRDist;
            CMapObject *pObj = pMOList->GetNext(pos);
            float fpDist = sqr(pObj->GetCPX() - fpCenterX) + sqr(pObj->GetCPY() - fpCenterY);
            // check if object is within desired circle
            if (fpDist > fpRadius)
            {
                fpRDist = sqr(pObj->bv.fpRadius) - 2 * fpRadius * pObj->bv.fpRadius;
                if (fpDist > fpRadius + fpRDist)
                    continue;
            }
            list.AddHead(pObj);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIObjects::GetUnitsNear(const CPlot3D &point, CUnitList &list, float fpRadius)
{
    int nGridCount;
    SAIGridPoint *pGrid = GetGridPoints(fpRadius / GetCellSize(), nGridCount);

    list.RemoveAll();

    int nUnitX = Float2AITile(point.x), nUnitY = Float2AITile(point.y);

    int nUnitCellX = nUnitX / pMap->GetObjectCellSize(),
        nUnitCellY = nUnitY / pMap->GetObjectCellSize();

    float fpCenterX = point.x, fpCenterY = point.y;

    fpRadius *= fpRadius;
    for (int i = 0; i < nGridCount; i++)
    {
        // Get map object list on the i-th cell surrounding unit
        CUnitList *pList = pMap->GetUnitCellList(nUnitCellX + pGrid[i].nDX,
                                                 nUnitCellY + pGrid[i].nDY);
        if (pList == NULL)
            continue;

        POSITION pos = pList->GetHeadPosition();
        // if this stuff will take much time some optimizations could be made about speeding it up
        while (pos)
        {
            CUnit *pUnit = pList->GetNext(pos);
            float fpDist = sqr(pUnit->GetCPX() - fpCenterX) + sqr(pUnit->GetCPY() - fpCenterY);
            // check if object is within desired circle
            if (fpDist < fpRadius)
            {
                list.AddHead(pUnit);
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// FUNCTION:	AddObject
// DESCRIPTION:	Add map object in the objectsMap
// PARAMETERS:	pObject - ptr to according MapObject
// RETURNS:		true if success
///////////////////////////////////////////////////////////////////
bool CAIObjects::AddObject(CMapObject *pObject)
{
    // 1) retrieve object's coordinates
    //	CPlot3D pltObject = pObject->GetCP();
    int nObjectX = Float2AITile(pObject->GetCPX()), nObjectY = Float2AITile(pObject->GetCPY());

    // 2) calculate objects map coordinates
    int nObjectMapX = nObjectX / AI_N_OM_CELL_SIZE, nObjectMapY = nObjectY / AI_N_OM_CELL_SIZE;
    if (nObjectMapX >= nObjectsMapSizeX || nObjectMapY >= nObjectsMapSizeY || nObjectMapX < 0 ||
        nObjectMapY < 0)
        return false;

    // 3) Add object to the map
    bool rv = objectsMap[nObjectMapX + nObjectsMapSizeX * nObjectMapY].AddObjectInCell(pObject);
    if (rv)
    {
        rv = pMap->AddRemoveMOLowLevel(pObject, false);
        if (rv)
            nMapObjectsNumber++;
        else
            objectsMap[nObjectMapX + nObjectsMapSizeX * nObjectMapY].RemoveObjectFromCell(pObject);
    }
    if (rv)
    {
        pObject->SetFlag(MAPOBJ_MOVED);
        pObject->nTileMapX = nObjectX;
        pObject->nTileMapY = nObjectY;
    }
    return rv;
}

/////////////////////////////////////////////////////////////////////////////////////
// FUNCTION:	RemoveObject
// DESCRIPTION:	Removes map object from the objectsMap
// PARAMETERS:	pObject - ptr to according MapObject
// RETURNS:		true if success
///////////////////////////////////////////////////////////////////
bool CAIObjects::RemoveObject(CMapObject *pObject)
{
    bool bRv;
    // 1) retrieve object's coordinates
    // CPlot3D pltObject = pObject->GetCP();
    int nObjectX = pObject->nTileMapX, // Float2AITile( pltObject.GetX() ),
        nObjectY = pObject->nTileMapY; // Float2AITile( pltObject.GetY() );

    ASSERT(nObjectX >= 0 && nObjectY >= 0);
    // 2) calculate objects map coordinates
    int nObjectMapX = nObjectX / AI_N_OM_CELL_SIZE, nObjectMapY = nObjectY / AI_N_OM_CELL_SIZE;
    if (nObjectMapX >= nObjectsMapSizeX || nObjectMapY >= nObjectsMapSizeY)
        return false;

    // 3) Remove object from the map
    bRv = objectsMap[nObjectMapX + nObjectsMapSizeX * nObjectMapY].RemoveObjectFromCell(pObject);
    if (bRv)
    {
        bRv = pMap->AddRemoveMOLowLevel(pObject, true);
    }
    ASSERT(bRv);
    if (bRv)
    {
        pObject->LeaveMap();
        pObject->nTileMapX = -1;
        pObject->nTileMapY = -1;
    }
    return bRv;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIObjects::MoveObject(CMapObject *pObject, const CPlot3D &newPlot)
{
    bool bRv, bTemp;

    // 1) retrieve object's coordinates
    int nObjectX = pObject->nTileMapX, nObjectY = pObject->nTileMapY;
    int nNewObjectX = Float2AITile(newPlot.x), nNewObjectY = Float2AITile(newPlot.y);
    CPlot3D oldPlot = pObject->GetCP();

    ASSERT(nObjectX >= 0 && nObjectY >= 0);

    // 2) calculate old objects map coordinates
    int nObjectMapX = nObjectX / AI_N_OM_CELL_SIZE, nObjectMapY = nObjectY / AI_N_OM_CELL_SIZE;
    ASSERT(nObjectMapX < nObjectsMapSizeX && nObjectMapY < nObjectsMapSizeY);
    if (nObjectMapX >= nObjectsMapSizeX || nObjectMapY >= nObjectsMapSizeY)
        return false;
    // 2.5) calculate new object map coordinates
    int nObjectMapNewX = nNewObjectX / AI_N_OM_CELL_SIZE,
        nObjectMapNewY = nNewObjectY / AI_N_OM_CELL_SIZE;
    //	ASSERT( nObjectMapNewX < nObjectsMapSizeX && nObjectMapNewY < nObjectsMapSizeY );
    if (nObjectMapNewX >= nObjectsMapSizeX || nObjectMapNewY >= nObjectsMapSizeY)
        return false;

    bool bChangeCell = (nObjectMapNewX != nObjectMapX || nObjectMapNewY != nObjectMapY) ? true
                                                                                        : false;

    // передвинем объект на ai map
    bRv = pMap->AddRemoveMOLowLevel(pObject, true);
    if (bRv)
    {
        pObject->SetCP(newPlot);
        bRv = pMap->AddRemoveMOLowLevel(pObject, false); // , bForce );
        if (!bRv)
        {
            pObject->SetCP(oldPlot);
            bTemp = pMap->AddRemoveMOLowLevel(pObject, false);
            ASSERT(bTemp);
        }
    }

    // если на ai map передвинуть удалось поместим объект на новый cell
    if (bChangeCell && bRv)
    {
        bRv = objectsMap[nObjectMapX + nObjectsMapSizeX * nObjectMapY].RemoveObjectFromCell(
            pObject);
        ASSERT(bRv);
        bRv = objectsMap[nObjectMapNewX + nObjectsMapSizeX * nObjectMapNewY].AddObjectInCell(
            pObject);
        ASSERT(bRv);
    }
    // запишем новые координаты объекта
    if (bRv)
    {
        pObject->SetFlag(MAPOBJ_MOVED);
        pObject->nTileMapX = nNewObjectX;
        pObject->nTileMapY = nNewObjectY;
    }
    return bRv;
}
