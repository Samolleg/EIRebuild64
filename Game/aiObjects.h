#pragma once
/**********************************************************************
 *<
        FILE: AIObjects.H

        DESCRIPTION:  Description of map objects for AI purpouses

        CREATED BY: Mikhail Semyonov

 *>	Copyright (c) 1999, Nival Interactive
 **********************************************************************/

#include "MapObject.h"
#include "MapFormat.h"
#include "aiGraph.inl"

/////////////////////////////////////////////////////////////////////////////////////
// Classes defined in other modules
class CAIMap; // in aiMap.h

/////////////////////////////////////////////////////////////////////////////////////
// Classes defined in this module
class CAIObjects;
class CAIObjectsCell;
class CAIMapObject;

/////////////////////////////////////////////////////////////////////////////////////
// клетка на сетке, структура используется для выборки клеток, находящихся на заданном
// расстоянии от некоторой точки
struct SAIGridPoint
{
    short nDX, nDY;
};

/////////////////////////////////////////////////////////////////////////////////////
// получить указатель на список точек, находящихся на расстоянии не более fpDist,
// расстояние измеряется в клетках, nCount - количество клеток, которые попадают
// в указанный радиус
class CAIGridMesh
{
    int nPrevCount;
    float fpPrevDist;

public:
    CAIGridMesh()
    {
        fpPrevDist = -1;
        nPrevCount = 0;
    }
    SAIGridPoint *GetGridPoints(float fpDist, int &nCount);
};
/////////////////////////////////////////////////////////////////////////////////////
struct SAICellObjectTile
{
    short nMaxHeight;
    short nMinHeight;
    char nPassability;
    char nVisibility;
    short nNext;
    SAICellObjectTile() = default;
    SAICellObjectTile(int nMin, int nMax, int nPass, int nVis)
        : nMinHeight(nMin)
        , nMaxHeight(nMax)
        , nPassability(nPass)
        , nVisibility(nVis)
    {
        nNext = -1;
    }
};
typedef CList<SAICellObjectTile, SAICellObjectTile &> CCellObjectTileList;

/////////////////////////////////////////////////////////////////////////////////////
class CAIObjectsCell
{
private:
    CMapObjectList mapObjects; // list of map objects in this cell
    CUnitList units;           // list of map objects in this cell
    int nFreeTile;

public:
    CArray<SAICellObjectTile, SAICellObjectTile &> objectTiles;

    void Clear();
    CAIObjectsCell() { Clear(); }
    virtual ~CAIObjectsCell();

    // получить индекс свободного элемента в objectTiles для использования
    int GetFreeTileIndex();
    // удалить тайл с параметрами nMinH etc в списке начиная с nFirst, вернуть новый первый
    // элемент списка
    int RemoveTile(int nFirst, int nMinH, int nMaxH, int nPass, int nVis);
    // просто перенести тайл nIdx в список свободных
    void RemoveTile(int nIdx);

    bool AddObjectInCell(CMapObject *pObject);
    bool RemoveObjectFromCell(CMapObject *pObject);

    CMapObjectList *GetObjectsList() { return &mapObjects; };
    CUnitList *GetUnitsList() { return &units; };
};

/////////////////////////////////////////////////////////////////////////////////////
// Map objects
class CAIObjects
{
private:
    int nObjectsMapSizeX;
    int nObjectsMapSizeY;
    int nMapObjectsNumber;
    // map of objects
    CArray<CAIObjectsCell> objectsMap;
    CAIMap *pMap;

public:
    CAIObjects()
    {
        pMap = NULL;
        nObjectsMapSizeX = 0;
        nObjectsMapSizeY = 0;
        nMapObjectsNumber = 0;
    };
    CAIObjects(int nMapSizeX, int nMapSizeY, CAIMap *pTileMap);
    void Set(int nMapSizeX, int nMapSizeY, CAIMap *pTileMap);
    virtual ~CAIObjects() {};

    // bool RetrieveMapObjectStruct( int nX, int nY, SAIMapObject *pObjectStruct );

    //////////// Functions used by AI map ////////////
    bool AddObject(CMapObject *);
    bool RemoveObject(CMapObject *);
    bool MoveObject(CMapObject *pObject, const CPlot3D &newPlot);

    int GetCellSize() const { return AI_N_OM_CELL_SIZE; };
    CMapObjectList *GetCellList(int nCellX, int nCellY);
    CUnitList *GetUnitCellList(int nCellX, int nCellY);
    void GetObjectsNear(const CPlot3D &point, CMapObjectList &list, float fpRadius);
    void GetUnitsNear(const CPlot3D &point, CUnitList &list, float fpRadius);

    CAIObjectsCell *GetCellByTile(int nX, int nY)
    {
        return &(objectsMap[nX / AI_N_OM_CELL_SIZE + nObjectsMapSizeX * (nY / AI_N_OM_CELL_SIZE)]);
    };
    CAIMap *GetAIMap() { return pMap; };
};
