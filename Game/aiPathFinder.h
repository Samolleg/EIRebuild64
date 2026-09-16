#pragma once
/**********************************************************************
 *<
        FILE: aiPathFinder.H

        DESCRIPTION:  finding paths

        CREATED BY: Andy Gulin

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/

#include "sArray.h"
#include "aiGraph.inl"

/////////////////////////////////////////////////////////////////////////////////////
// Some EAIDirection functions &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
extern EAIDirection AINumberToDir[9];

inline EAIDirection GetDirection(int nSrcX, int nSrcY, int nDstX, int nDstY);
inline bool IsDiagonalDirection(EAIDirection dir);
inline EAIDirection InvertDirection(EAIDirection dir);
inline int DirectionDifference(EAIDirection dir1, EAIDirection dir2);
inline EAIDirection GetDirection(float fpX, float fpY);

/////////////////////////////////////////////////////////////////////////////////////
// таблицы для рассчета скорости и стоимости маршрута
struct SRouteCostsInfo
{
    int *pTerrainCosts;   // указатель на таблицу стоимостей прохода по разным террейнам
    int *pElevationCosts; // указатель на таблицу множителей для различных перепадов высот
    int nShift; // бит, с которого начинается индекс террейна для данного юнита в dwTerrains
    float fpRotationSpeed;  // radians per segment
    float fpRotationSpeed1; // 1 / radians per segment
    float
        fpBaseSpeed; // base unit speed, ai tiles per segment ( valid only if GetSpeeds was called )
};

/////////////////////////////////////////////////////////////////////////////////////
// структура для поиска пути волной
struct SPTWave
{
    int nCost;
    SPTWave *pNext, *pPrev;
    short nX, nY;
    char bCanPass, bPermanent, backDir;
};
typedef SPTWave *PPTWave;

/////////////////////////////////////////////////////////////////////////////////////
const int N_INFINITE = 0x7fffffff;
const int N_WAVE_LIST_SPLIT_NUMBER = 4; // must be power of 2
const int N_ROUTE_OPTIMIZE_TURN_COST = 60;
const int N_MAP_WAVE_SEARCH_SIZE = 32;

/////////////////////////////////////////////////////////////////////////////////////
// класс, осуществляющий поиск путей с помощью волны
class CAIMap;
struct SAITile;
class CAIRoute;
class CUnit;

class CWavePathFinder
{
    bool bStarted, bLocksSaved;
    CArray2D<SPTWave> waves;
    // массив для временного хранения блокировок
    CArray2D<char> savedLocks;
    CAIMap *pMap;
    // массив для обращения к тайлам aimap, соответсвующим waves
    CArray<SAITile *, SAITile *> tiles;
    // список использованных узлов с фиксированной ценой
    SPTWave *pDead;
    SPTWave *pFirst4[N_WAVE_LIST_SPLIT_NUMBER]; // *pFinalTile,
    SPTWave *pSrc;                              // starting tile for waves
    int nXPos, nYPos;                           // relative position of waves on pMap
    int nCurrentCost;                           // price for pSrc
    SRouteCostsInfo costsInfo;                  // CostsInfo to calculate costs of routes
    int nCurrentUnitClass;                      // AI unit class
    bool bBack;                                 // direction of search
    BYTE nLockCheck;                            // LockCheck constant (depends on unit size)

    // получить время перехода из pSrc в pDest
    int GetPassTime(SPTWave *pDest, int nMult);
    // очистить элементы из списка pTile
    void ClearWaveUnits(SPTWave *pTile);
    // добавить новый элемент в список активных
    void AddActiveNode(SPTWave *pDst, int nCost); // actually its inline
                                                  // найти новый исходный элемент
    void FindNewSrc();
    // попробовать пойти в направлении pDst
    void TryStep(SPTWave *pDst, EAIDirection backDir, int nMult);

    // посчитать смещения окна по конечным точкам маршрута
    bool CalcOffsets(int nSrcX, int nSrcY, int nDstX, int nDstY);
    // инициализировать tiles[]
    void InitializeTiles();
    // добавить первый узел nSrcX, nSrcY в списки, очистить списки активных, получить costsInfo
    void StartWave(int nSrcX, int nSrcY);
    // установить зависящие от класса параметры из различных источников
    void SetUnitInfo(CUnit *pUnit);
    void SetUnitInfo(int nAIClass);
    // начать поиск пути из Src в Dst
    bool StartSearch(int nSrcX, int nSrcY, int nDstX, int nDstY);
    // начать поиск путей во все точки квадрата, описываемого Low, High, включительно из точки Src
    bool StartSquare(int nLowX, int nLowY, int nHighX, int nHighY, int nSrcX, int nSrcY);

public:
    void SetMap(CAIMap *pMp, bool bBackWave);
    // изменить направление поиска
    void SetBackWaveFlag(bool bBackWave) { bBack = bBackWave; }
    CWavePathFinder(CAIMap *pMp = NULL)
    {
        pMap = NULL;
        if (pMp)
            SetMap(pMp, false);
    }
    // начать поиск пути из Src в Dst, версия для интерфейса
    bool StartSearch(CUnit *pUnit, int nSrcX, int nSrcY, int nDstX, int nDstY);
    bool StartSearch(int nAIClass, int nSrcX, int nSrcY, int nDstX, int nDstY);
    // начать поиск путей во все точки квадрата, описываемого Low, High, включительно из точки Src
    // версия для интерфейса
    bool StartSquare(CUnit *pUnit, int nLowX, int nLowY, int nHighX, int nHighY, int nSrcX,
                     int nSrcY);
    bool StartSquare(int nAIClass, int nLowX, int nLowY, int nHighX, int nHighY, int nSrcX,
                     int nSrcY);
    // продолжить поиск с некоторого состояния, используется при поиске по графу для сохранения
    // информации о расстояниях до нескольких точек вместо одной...
    bool ContinueSearch(int nSrcX, int nSrcY, int nDstX, int nDstY, bool bStartFromBorders);
    // скопировать backdirs для всех узлов в большую карту
    void CopyBackDirs();
    // совершить один шаг распространения волны
    void NextWave();
    // получить текущую позицию
    void GetPosition(int &nX, int &nY, int &nCost);
    // получить стоимость пути из точки (x,y), N_INFINITE если путь не существует
    int GetPermanentCost(int nX, int nY);
    // найти расстояния до всех точек квадрата
    void RunSquare();
    // запустить односторонюю волну до точки (x,y) и подождать пока она подойдет на nFinishDist к
    // Dst, вернет TRUE if Ok
    bool RunHalfPath(int nDstX, int nDstY, int nFinishDist);
    // если удалось дотрекать до nDstX, nDstY вернет TRUE
    bool RunSimpleOneWave(int nDstX, int nDstY);
    // запомнить состояния залоченности тайлов и обнулить их если стоит флаг
    void SaveLocks(bool bClear);
    // восстановить локи тайлов
    void RestoreLocks();
    void CleanUp();
    void BackTraceRoute(int nX, int nY, CAIRoute &res);
};

/////////////////////////////////////////////////////////////////////////////////////
// вспомогательные структуры для оптимизатора маршрутов
struct SAIOptRouteNode
{
    EAIDirection backDir;
    SAIOptRouteNode *pPrev;
};

struct SAIOptNodesPool // with fixed size for perfomance
{
    CArray<SAIOptRouteNode, SAIOptRouteNode &> pool;
    int nCurrent;
    SAIOptRouteNode *pCurrent;

    void Init(int nMaxSize)
    {
        pool.SetSize(nMaxSize);
        nCurrent = 0;
        pCurrent = pool.GetData();
    }
    SAIOptRouteNode *Get()
    {
        ASSERT(nCurrent++ < pool.GetSize());
        return pCurrent++;
    }
};

struct SAIOptEdge
{
    int nCost;
    SAIOptRouteNode *pNode;

    void Clear()
    {
        nCost = 0;
        pNode = NULL;
    }
};

struct SAIOptState
{
    SAIOptEdge edges[9];

    void Clear()
    {
        for (int i = 0; i < 9; i++)
            edges[i].Clear();
    }
    void SetInfinite()
    {
        for (int i = 0; i < 9; i++)
            edges[i].nCost = N_INFINITE;
    }
};

struct SAIOptBuildEdge
{
    int nSrcX, nSrcY, nDstX, nDstY;
    int nNumber;
    SAIOptBuildEdge() = default;
    SAIOptBuildEdge(int nSx, int nSy, int nDx, int nDy)
    {
        nSrcX = nSx;
        nSrcY = nSy;
        nDstX = nDx;
        nDstY = nDy;
    }
    int IsEqual(const SAIOptBuildEdge &a) const
    {
        return nSrcX == a.nSrcX && nSrcY == a.nSrcY && nDstX == a.nDstX && nDstY == a.nDstY;
    }
    int IsNext(const SAIOptBuildEdge &next) const
    {
        return nDstX == next.nSrcX && nDstY == next.nSrcY;
    }
    EAIDirection GetDir();
};

struct SAIOptBuildEdgesList
{
    CArray<SAIOptBuildEdge> edges;

    void Clear() { edges.SetSize(0); }
    void Build(EAIDirection dir, bool bFinal);
    void Shift(int nDX, int nDY);
    void BuildHorizontalEdge(int nDirX, int nDirY, int nSideX, int nSideY);
    void BuildDiagonalEdge(int nDirX, int nDirY, int nSideX1, int nSideY1);
    void BuildStar(bool bFinal);
};

struct SAIOptCopy
{
    int nSrcEdge, nDstEdge;
};

struct SAIOptCostCalc
{
    int nSrcEdge, nDstEdge;
    int nTileX, nTileY;
    int nTileDX, nTileDY; // direction of movement from tile
    int nTurnLeng;
    int nPathLeng; // length of path on this tile, 1024 = side of tile
    EAIDirection backDir;
};

struct SAIOptStage
{
    CArray<SAIOptCopy, SAIOptCopy &> copyList;
    CArray<SAIOptCostCalc, SAIOptCostCalc &> interList; // 2 reduce lists
    CArray<SAIOptCostCalc, SAIOptCostCalc &> localList;

    void Clear()
    {
        copyList.SetSize(0);
        interList.SetSize(0);
        localList.SetSize(0);
    }
    void BuildOptStage(SAIOptBuildEdgesList &src, SAIOptBuildEdgesList &dst);
};

/////////////////////////////////////////////////////////////////////////////////////
struct SRouteOptimizer
{
    BYTE nLockCheck; // LockCheck constant (depends on unit size)
    int nUnitClass;
    CAIMap *pMap;
    int nCurX, nCurY;
    SRouteCostsInfo costsInfo;

    void TryToReduce(SAIOptCostCalc &r, SAIOptEdge &srcEdge, SAIOptEdge &dstEdge);
    void CalcNextStage(SAIOptState &src, SAIOptState &dst, SAIOptStage &stage);
    // returns path cost
    int OptimizeRoute(CUnit *pUnit, CAIMap *pAIMap, CAIRoute &route, EAIDirection startDir,
                      float fpFinishCentered);
};

/////////////////////////////////////////////////////////////////////////////////////
inline EAIDirection GetDirection(int nSrcX, int nSrcY, int nDstX, int nDstY)
{
    return AINumberToDir[(nDstY - nSrcY) * 3 + (nDstX - nSrcX) + 4];
}

inline bool IsDiagonalDirection(EAIDirection dir)
{
    return dir == UPLEFT || dir == DOWNLEFT || dir == UPRIGHT || dir == DOWNRIGHT;
}

inline EAIDirection InvertDirection(EAIDirection dir)
{
    if (dir == NULL_DIR)
        return dir;
    int d = dir - 4;
    if (d < 1)
        d += 8;
    return EAIDirection(d);
}

inline int DirectionDifference(EAIDirection dir1, EAIDirection dir2)
{
    int d = abs(int(dir1) - int(dir2));
    if (d > 4)
        d = 8 - d;
    return d;
}

inline EAIDirection GetDirection(float fpX, float fpY)
{
    int nAngle = 6 - Float2Int(float(atan2(fpY, fpX) * 4 / 3.1415926f));
    while (nAngle < 0)
        nAngle += 8;
    while (nAngle > 7)
        nAngle -= 8;
    return EAIDirection(nAngle + 1);
}
