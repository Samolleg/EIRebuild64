#pragma once
#include "NavigationWorld.h"
#include "SaveSchema.h"
/**********************************************************************
 *<
        FILE: aiMap.H

        DESCRIPTION:  Description of AI map

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "SimplyArray.h"
#include "aiScanUnits.h"
#include "aiRoute.h"
#include "aiPathFinder.h"
#include "aiObjects.h"
#include "aiGraph.h"
#include "sArray.h"
#include "shared.h"
#include <unordered_map>

// --------------------------------------- Constants
// -------------------------------------------------------

const int N_AI_MAX_HEIGHT = 0x1FF;        // max integer height in SAITile
const int N_AI_MAX_DEPTH = 0x3E;          // max depth in SAITile
const int N_AI_MAX_OBJECT_HEIGHT = 1020;  // max obstacle height for ai map objects
const int N_AI_MAX_WAVE_SEARCH_LENG = 24; // максимальный путь в тайлах, рассчитываемый волной
const int N_AI_MAX_SEGMENT_SIZE = 32;     // максимальное количество узлов в одном сегменте
const int N_AI_MAX_DISTANCE_FROM_GRAPH_PATH =
    10;                               // не должно быть больше 2*N_AI_GRAPH_CELL_SIZE
                                      // и должно быть много меньше N_MAP_WAVE_SEARCH_SIZE
const int N_AI_LOCK_MATRIX_SIZE = 16; // размер матрицы для блокирования проходов
const float FP_AI_MAX_LOCK_RADIUS =
    2; // радиус максимального блокирующего элемента, связан с N_AI_LOCK_MATRIX_SIZE

const int N_AI_LOCK_BLK_PER_MATRIX = (N_AI_LOCK_MATRIX_SIZE / 4 + 1);
const int N_AI_LOCK_HASH_SIZE = 1024; // must be power of 2

const int AI_N_ROAD_TYPE_ID = 7; // attentively watch after aiinfo.res
const int AI_N_SAND_TYPE_ID = 3; // attentively watch after aiinfo.res

/////////////////////////////////////////////////////////////////////////////////////
struct SRectRegion
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    int nLowX, nHighX, nLowY, nHighY;

    void Clear()
    {
        nLowX = 10000;
        nHighX = 0;
        nLowY = 10000;
        nHighY = 0;
    }
    SRectRegion() { Clear(); }
    SRectRegion(int nLx, int nHx, int nLy, int nHy)
        : nLowX(nLx)
        , nHighX(nHx)
        , nLowY(nLy)
        , nHighY(nHy)
    {
    }
    void Grow(int nGrow)
    {
        nLowX -= nGrow;
        nHighX += nGrow;
        nLowY -= nGrow;
        nHighY += nGrow;
    }
    void Union(const SRectRegion &a)
    {
        nLowX = Min(nLowX, a.nLowX);
        nHighX = Max(nHighX, a.nHighX);
        nLowY = Min(nLowY, a.nLowY);
        nHighY = Max(nHighY, a.nHighY);
    }
    void Add(int nX, int nY)
    {
        nLowX = Min(nLowX, nX);
        nHighX = Max(nHighX, nX);
        nLowY = Min(nLowY, nY);
        nHighY = Max(nHighY, nY);
    }
    bool IsIn(int nX, int nY) { return nX >= nLowX && nX <= nHighX && nY >= nLowY && nY <= nHighY; }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};

/////////////////////////////////////////////////////////////////////////////////////
// информация о текущем состоянии процесса поиска и выполнения пути для некоего юнита
struct SAIPath
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    CAIFullRoute curSegment; // текущий сегмент пути

    // CAIRoute graphRoute;                // маршрут по графу
    CAIRoute toGo;         // осталось пройти эту часть пути
    CPlot2D ptFinish;      // точка назначения рассчитанного маршрута в AITile`s
    CPlot3D ptDestination = CPlot3D(0, 0, 0); // изначальная точка назначения ( a wish point )
    bool bFinished;        // если TRUE curSegment - последний сегмент пути
                           // bRouteFailed показывает удалось ли дойти до намеченной цели, если
                           // подходы к цели были непроходимы, то считается что route был успешным
    bool bRouteFailed;
    // bTryAnyPath - если стоит этот флажок, то пытаться дойти до цели до
    // исчерпания всех возможностей ( разница заключается в том, что если bTryAnyPath, то всегда
    // используется bTryWalkAround в aiMap::FindGraphPath, а иначе только в первый раз
    bool bTryAnyPath;
    // признак того, что поиск выполняется первый раз, если этот флаг установлен, то обход
    // ищется в любом случае, иначе только если стоит bTryAnyPath
    bool bFirstTry;
    // список вершин графа, которые не должны быть использованы при поиске пути по графу
    CAIGraphVertices excludeVertices;
    // насколько сложно по этому пути рассекать, вычисляется routeOptimizer`om
    int nPathCost;
    void NotMoving(CUnit *pUnit);

public:
    void Clear();
    SAIPath() { Clear(); }
    bool IsPathFinished() { return bFinished; }
    // создать вырожденный путь - разворот на месте
    void CreateRotation(int nStartSegment, CUnit *pUnit, const CPlot3D &vecDst,
                        float fpAngSpeed = 0);
    void CreateAttack(int nStartSegment, CUnit *pUnit, int nTargetID, int nHitSegment,
                      int nTurnFinishSegment);
    void CreatePointAttack(int nStartSegment, CUnit *pUnit, const CPlot3D &vecDst, int nHitSegment,
                           int nTurnFinishSegment);
    void CreateStand(CUnit *pUnit);
    void SetTryAnyPath() { bTryAnyPath = TRUE; }
    void ClearCalcedPath() { toGo.Clear(); }
    void GetWholeRoute(CAIFullRoute &res);
    CPlot3D GetFinalPoint()
    {
        return CPlot3D(AITile2Meter(ptFinish.x), AITile2Meter(ptFinish.y), 0);
    }
    int GetSegmentLength();
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

/////////////////////////////////////////////////////////////////////////////////////
// информация о тайле необходимая для pathfinding
#pragma pack(push, 1)
struct SAITile
{
    // terrain info for different kind of units, представляет собой 8*4 бита, по 4 бита на
    // каждый вид юнита
    DWORD dwTerrains;
    unsigned short nHeight;
    WORD nDepth : 6;
    WORD bBridge : 1;
    WORD bWaterFlag : 1;
    BYTE nLocked; // блокировки от стоящих ( неидущих ) юнитов

    int GetTerrainCost(SRouteCostsInfo &info)
    {
        return info.pTerrainCosts[(dwTerrains >> info.nShift) & 15];
    }
    // функции для работы с блокировками
    // void Lock( BYTE nAdd ) { ASSERT( int(nLocked) + nAdd < 255 ); nLocked += nAdd; }
    // void Unlock( BYTE nSub ) { ASSERT( nLocked >= nSub ); nLocked -= nSub; }
    DWORD IsLocked(BYTE nCheck) { return nLocked > nCheck; }
    void Clear()
    {
        nDepth = 0;
        dwTerrains = 0;
        nHeight = 0;
        bBridge = 0;
        nLocked = 0;
        bWaterFlag = 0;
    }
    SAITile() { Clear(); }
};
#pragma pack(pop)

/////////////////////////////////////////////////////////////////////////////////////
// описание террейна и объектов, стоящих на тайле ( если он один, то записывается в эту структуру,
// иначе см aiObjects
#pragma pack(push, 1)
struct SAIObjTile
{
    union
    {
        struct
        {
            DWORD nTerrainIndex : 5;
            DWORD nObjVisibility : 2;
            DWORD bManyObj : 1;
            DWORD bEmpty : 1;
            DWORD nObjPassability : 3;
            DWORD nMaxObjectHeight : 10;
            DWORD nMinObjectHeight : 10;
        };
        struct
        {
            DWORD nTerrainIndex1 : 5;
            DWORD nObjVisibility : 2;
            DWORD bManyObj1 : 1;
            DWORD bEmpty1 : 1;
            DWORD nFirstCellIndex : 20; // номер первой структуры SAICellObjectTile в CAIObjectsCell
                                        // для этого тайла
        };
    };
    void Clear()
    {
        bEmpty = 1;
        bManyObj = 0;
        nTerrainIndex = 0;
    }
    SAIObjTile() { Clear(); }
};
#pragma pack(pop)

/////////////////////////////////////////////////////////////////////////////////////
// AI Object placement information
struct SAIOneTile
{
    int nX, nY;
    int nHMin, nHMax;
    char nPassability, nVisibility;
    char nEdgeMark; // nEdgeMark == 1 для тайлов на границе объектов, раньше было нужно
    int nNext;
    SAIOneTile() = default;
    SAIOneTile(int nNx, int nNy, int nHMn, int nHMx, int nPass, int nVis)
        : nX(nNx)
        , nY(nNy)
        , nHMin(nHMn)
        , nHMax(nHMx)
        , nEdgeMark(0)
        , nPassability(nPass)
        , nVisibility(nVis)
    {
    }
};

/////////////////////////////////////////////////////////////////////////////////////
// совокупность тайлов, занимаемых одним объектом
struct SAIObjectPlacement
{
    CSimplyArray<SAIOneTile> tiles;
    int nHash[16];  // для ускорения поиска тайлов
    bool bHashUsed; // для ускорения очистки если nHash не юзали :)
    SAIObjectPlacement() { bHashUsed = TRUE; }
    void Clear()
    {
        tiles.SetSize(0);
        if (bHashUsed)
        {
            for (int i = 0; i < 16; i++)
                nHash[i] = -1;
        }
    }
    void AddTileNoCheck(const SAIOneTile &tile) { tiles.Add(tile); }
    // функции проверяют есть ли такой тайл уже в списках, если да информация о нем модифицируется
    // иначе добавляется новый
    void AddMaxTile(int nX, int nY, int nH, int nPassability, int nVisibility);
    void AddMinTile(int nX, int nY, int nH, int nPassability, int nVisibility);
    void SetEdge(int nX, int nY);
    void CalcBoundingRectangle(SRectRegion &area);
    void CalcCoveringSquare(CAIMap &map, float fpX, float fpY, float fpR, int &nLx, int &nHx,
                            int &nLy, int &nHy);
    void CreateDangerCircle(CAIMap &map, const CPlot3D &ptCenter, float fpRadius);
    void CreateDangerWall(CAIMap &map, const CPlot3D &ptCenter, float fpRadius,
                          const CPlot3D &ptWall);
};

/////////////////////////////////////////////////////////////////////////////////////
// Tile information
// информация о типе террейна
struct SAITileInfo
{
    char pszName[256];
    int nSpeed, nCostMultiplier;
    float fpFootstepStrengths[3]; // footstep strength multiplier logs [0] - vision [1] - smell [2]
                                  // - sound
    DWORD dwTerrains;             // маска проходимостей для тайла такого вида без объектов и воды
};

/////////////////////////////////////////////////////////////////////////////////////
// wide units locks & checks information
struct SUnitLockOffset
{
    int x, y;
    SUnitLockOffset(int nx, int ny)
        : x(nx)
        , y(ny)
    {
    }
    SUnitLockOffset() = default;
};
typedef CArray<SUnitLockOffset> CUnitCovering;

/////////////////////////////////////////////////////////////////////////////////////
// Lock matrix related
class CLockMatrix : public CObjectBase
{
    OBJECT_BASIC_METHODS(CLockMatrix);

public:
    char nLocks[N_AI_LOCK_MATRIX_SIZE][N_AI_LOCK_MATRIX_SIZE];
    float fpRadius;
    CLockMatrix()
    {
        memset(nLocks, 0, sizeof(nLocks));
        fpRadius = -1;
    }
    CLockMatrix(float _fpRadius);
};
/////////////////////////////////////////////////////////////////////////////////////
class CLockMatrixDesc : public CSharedDesc<CLockMatrix>
{
    float fpRadius;
    void RecalcHash() { SetHashValue(*((int *)(&fpRadius))); }

public:
    void SetRadius(float _fpRadius)
    {
        fpRadius = _fpRadius;
        RecalcHash();
    }
    float GetRadius() const { return fpRadius; }
    bool operator==(const CLockMatrixDesc &a) const { return fpRadius == a.fpRadius; }
};
/////////////////////////////////////////////////////////////////////////////////////
class CLockMatrixManager : public CSharedManager<CLockMatrixDesc, CLockMatrix>
{
protected:
    virtual CLockMatrix *NewObject(CLockMatrixDesc &desc)
    {
        return new CLockMatrix(desc.GetRadius());
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// блок, содержащий локи для использования в aimap
struct SAILockBlock
{
    SAILockBlock *pNext;
    union
    {
        struct
        {
            short nX, nY;
        }; // координаты блока
        int nCoords;
    };
    union
    {
        char nLocks[4][4]; // сами локи
        DWORD dwLocks[4];
    };
    void *pOwner; // владелец локов

    void SetCoord(int x, int y) { nCoords = (y << 16) + x; }
    void ClearLocks() { memset(nLocks, 0, sizeof(nLocks)); }
    void AddLocks(SAILockBlock &block);
    bool IsEmpty();
};
/////////////////////////////////////////////////////////////////////////////////////
// менеджер для поддержки локов, обеспечивает блокировку по максимальному значению
class CAILockManager
{
    CList<SAILockBlock> blocks;
    SAILockBlock *pHash[N_AI_LOCK_HASH_SIZE];
    SAILockBlock *pDead;

    void FreeBlock(SAILockBlock *pTrash)
    {
        pTrash->pNext = pDead;
        pDead = pTrash;
    }
    SAILockBlock *AllocBlock();

protected:
    virtual void ReplaceBlock(SAILockBlock &block) = 0;
    virtual void AddBlock(SAILockBlock &block) = 0;

public:
    CAILockManager()
        : blocks(1024)
    {
        memset(pHash, 0, sizeof(pHash));
        pDead = 0;
    }
    void Lock(void *_pOwner, CLockMatrix *pMatrix, int nX, int nY);
    void Unlock(void *_pOwner, int nX, int nY);
};
/////////////////////////////////////////////////////////////////////////////////////
class CAIStaticLockManager : public CAILockManager
{
    CAIMap &map;

protected:
    virtual void ReplaceBlock(SAILockBlock &block);
    virtual void AddBlock(SAILockBlock &block);

public:
    CAIStaticLockManager(CAIMap &_map)
        : map(_map)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
class CAIDynamicLockManager : public CAILockManager
{
    CAIMap &map;

protected:
    virtual void ReplaceBlock(SAILockBlock &block);
    virtual void AddBlock(SAILockBlock &block);

public:
    CAIDynamicLockManager(CAIMap &_map)
        : map(_map)
    {
    }
};
/////////////////////////////////////////////////////////////////////////////////////
// Terrain passability calculations
struct STerrainTile;
/////////////////////////////////////////////////////////////////////////////////////
// structure for KeepTerrain functionality
struct SAIKeepTerrain
{
    unsigned short nHeight;
    unsigned char nDepth;
    unsigned char nTerrainIndex;
};

/////////////////////////////////////////////////////////////////////////////////////
// keeps information about height, depth and terrainindex
class CAITerrainMemory
{
    typedef std::unordered_map<int, SAIKeepTerrain> CStorage;
    CStorage data;
    int MakeKey(short nX, short nY) const { return (((int)nX) << 16) + (int)nY; }

public:
    void Save(short nX, short nY, const SAITile &tile, const SAIObjTile &objTile);
    void Restore(short nX, short nY, SAITile &tile, SAIObjTile &objTile);
    int GetSavedHeight(short nX, short nY) const;
};

/////////////////////////////////////////////////////////////////////////////////////
// описание карты игры для ai
class CAIMap
{
    friend class SaveSchema::Slot;
    bool bIsLoaded;
    bool bDelayedConvert;    // не преобразовывать проходимости
    bool bEnableGraphRecalc; // перестраивать граф при добавлении/уничтожении объектов
    SRectRegion convertRegion;
    int nTerrainCosts[16]; // скорости и стоимости для 16 видов террейна
    int nTerrainFlatCosts[16];
    int nTerrainSpeeds[16];
    CArray<int, int> nElevationMultipliers;             // *1024
    CArray<int, int> nElevationMultipliersFly;          // *1024
    CSimplyArray<SAITileInfo, SAITileInfo &> tilesInfo; // tiles information, on every kind of tile
    CAITerrainMemory terrainKeeper;

    float fpZMult;                  // coefficient for transforming height
                                    // we must multiply tile height on this value to get
                                    // actual height
    CAIObjects mapObjects;          // MapObjects which must be used by pathfinding functions
    SAIObjectPlacement place;       // var to be used with functions handling object placement
    SAIObjectPlacement placeBridge; // var to be used with functions handling object placement
    CArray2D<char> dLocks;          // локи от движущихся юнитов
    CUnitCovering aStar3, aStar5, aStar7, aLock1, aLock2, aLock3;
    CAIStaticLockManager staticLocks;
    CAIDynamicLockManager dynamicLocks;

    SRouteOptimizer routeOptimizer;
    CAIGraph aiGraph;
    NavigationWorld navigation;
    CWavePathFinder wave, waveBack;
    int nLastDangerAddTime; // Segment on which last dangerous area was added to map

    // one tile based operations
    bool AddObjectTile(SAIOneTile &tile); //, bool bForce = FALSE );
    bool RemoveObjectTile(SAIOneTile &tile);
    bool TileHasBridge(int x, int y, int *pnBridgeHeight = 0);
    // add/remove object presented as set of tiles it occupies
    bool AddRemoveMapObject(SAIObjectPlacement &place, bool bRemove);
    bool AddRemoveBridge(SAIObjectPlacement &place, bool bRemove);
    // convert height from float to map object height
    int GetIntObjectHeight(float fpHeight);
    // convert depth from meters to map object height
    int DepthFloatToInt(float fpDepth);
    // adding/removal of units
    CLockMatrix *GetUnitLockMatrix(float fpRadius);
    bool AddUnit(CUnit *pUnit, int nX, int nY);
    bool RemoveUnit(CUnit *pUnit, int nX, int nY);
    // calc set of tiles occupied by an object and call appropriate function
    // for use from CAIObjects only, foreigners should call AddObject/RemoveObject instead
    bool AddRemoveMOLowLevel(CMapObject *pObject, bool bRemove = FALSE);
    // сохранить/восстановить террейн в точке nX, nY, используется для мостов
    // применяется примитивная схема
    void KeepOriginalTerrain(int nX, int nY, bool bSave);

    // координаты региона( включительно ) для пересчета проходимостей
    void ConvertObjMapToPassabilities(const SRectRegion &area);
    // функция, реально выполняющая преобразование, предыдущая используется для накопления областей
    void RealConvertObjMapToPassabilities(int nLowX, int nHighX, int nLowY, int nHighY);
    // вернет террейн в точке nX, nY проходим для nUnitClass
    bool TerrainIsPassable(int nUnitClass, int nX, int nY)
    {
        return ((map[nY][nX].dwTerrains >> (nUnitClass * 4)) & 15) != 0;
    }
    // функция не проверяет лежат ли указанные координаты в допустимом диапазоне
    bool TileIsDangerous(int nX, int nY);
    // вычислить проходимость одного тайла, возвращает класс террейна
    int CalcOneTilePassability(STerrainTile &terra, int nUnitClass, int nUnitHeight, int x, int y);
    void CalcDefaultPassabilities();
    // проставить непроходимость для тайлов с резкими перепадами по высоте между соседними
    void SetUnpassableDueToTerrainGradient(int nLowX, int nHighX, int nLowY, int nHighY);
    // перенести области напроходимости на nDX, nDY nLowX etc - куда переносить ( регион назначения
    // ) , nDx - смещения используется tempMap в качестве хранилища проходимостей
    void ShiftUnpassableTerrain(int nKind, int nDX, int nDY, int nLowX, int nHighX, int nLowY,
                                int nHighY);

    // вспомогательные функции для поиска путей
    bool BiWaveFindPath(CAIRoute &res, int *nResCost);
    bool FindPath(CUnit *pUnit, int nSrcX, int nSrcY, int nDstX, int nDstY, CAIRoute &res,
                  int *nResCost = NULL);
    bool FindPath(int nAIClass, int nSrcX, int nSrcY, int nDstX, int nDstY, CAIRoute &res,
                  int *nResCost = NULL);
    void ClearMapForLongBackTrace(CAIRoute &route);
    bool FindGraphPath(CUnit *pUnit, int nSrcX, int nSrcY, int nDstX, int nDstY, SAIPath &res,
                       bool bTryWalkAround, int nMaxPathLength);
    // найти маршрут, заканчивающийся в nX, nY по backdirs из tempMap
    void BackTracePath(int nX, int nY, CAIRoute &res);
    // посчитать скорости для маршрута
    void CalcSpeeds(CUnit *pUnit, CAIRoute &route);
    // создать один сегмент пути по информации из path
    void CreatePathSegment(CUnit *pUnit, SAIPath &path);
    // находит достижимую точку рядом с finish, заодно переводит finish в тайлы
    // эта функция имеет посторонний эффект - она переводит finish из метров в ai тайлы
    bool FindPathDestination(CUnit *pUnit, CPlot3D &finish, bool bAccountColor,
                             float fpMinDistance);

    // инициализация данных проходимости
    void InitializeBasicTerrainCosts();
    // инициализация областей блокирования/проверки для различных типоразмеров юнитов
    void ConstructAreas();

public:
    CArray2D<SAITile> map;
    CArray2D<SAIObjTile> mapObj;
    CArray2D<char> tempMap; // промежуточная переменная для вычисления проходимостей terrain`ов
                            // и для backtrace длинных путей
    CAIMarkManager marks;

    CAIMap();
    NavigationWorld& Navigation() { return navigation; }
    BYTE NavigationBridgeArea() const { return BYTE((tilesInfo[AI_N_ROAD_TYPE_ID].dwTerrains>>12)&15); }
    const char* NavigationLegacyRejection(int unitClass,int x,int y); // Baker report only.
    bool NavRoute(CUnit* unit, Navigation::Point from, Navigation::Point goal, CAIRoute& route, float maxLength=1.e6f, float goalRadius=0, int aiClass=3, Navigation::Location* source=nullptr, Navigation::GoalPolicy goalPolicy=Navigation::GoalPolicy::Exact);
    BOOL Load(LPCSTR pszMapName);
    void Clear();
    int XSize() const { return map.GetXSize(); }
    int YSize() const { return map.GetYSize(); }
    bool SetGraphRebuild(bool bGraph)
    {
        bool bRes = bEnableGraphRecalc;
        bEnableGraphRecalc = false; // X64NAV01: the old graph is never rebuilt for navigation.
        return bRes;
    }

    void Segment(int nCurSegment) { marks.Segment(nCurSegment); }

    void BuildGraph() { aiGraph.Build(this); }
    void StoreGraph(CSave &file) { aiGraph.Store(file); }
    void SetBuildGraphCallBack(AIGraphBuilderCallBack newCallBack)
    {
        aiGraph.SetBuildCallBack(newCallBack);
    }
    bool LoadGraph(CSave &file) { return aiGraph.Load(file); }
    // начать операцию по доставке pUnit из src в dst, текущие ЦУ записываются в res
    // осуществляет поиск обходного пути если проблема возникла сразу в начале пути или если
    // установлен флаг bTryAnyPath
    bool NewPath(CUnit *pUnit, const CPlot3D &ptDst, SAIPath &res, float fpMaxPathLength = 1e6f,
                 float fpMinDistance = 0, Navigation::GoalPolicy goalPolicy = Navigation::GoalPolicy::Exact);
    // продолжить мощный поиск пути по успешном завершении очередного сегмента
    bool ContinuePath(CUnit *pUnit, SAIPath &res);
    // получить скорости/стоимости террейнов и разворотов для юнита
    // GetRouteCostsPathFind отличается тем, что может вернуть одинаковые цены для всех террейнов
    // в случае, если нужен путь по воде etc ( юнит в этом случае должен возвращать
    void GetRouteCosts(CUnit *pUnit, SRouteCostsInfo &info);
    void GetRouteCosts(int nAIClass, SRouteCostsInfo &info);
    void GetRouteCostsPathFind(CUnit *pUnit, SRouteCostsInfo &info);
    void GetRouteSpeeds(CUnit *pUnit, SRouteCostsInfo &info);
    bool CanRun(CUnit *pUnit);
    bool CanPutUnit(CUnit *pUnit);
    bool CanPutUnit(int nAIClass, const CPlot3D &ptPos);
    // не обязательно на тот тайл, на который хотят можно и на соседние, lock check not performed
    bool CanPutUnitRelaxed(CUnit *pUnit);
    // найти позицию на которую можно поставить юнит, так чтобы эта позиция была связана с
    // ptBasePoint
    bool FindUnitPosition(CUnit *pUnit, const CPlot3D &ptBasePoint, CPlot3D &ptResPos);
    float GetTerrainGrad(const CPlot3D &ptPos);

    // some area covering functions
    CUnitCovering *GetTerrainArea(int nUnitClass);

    // delayed passability conversion ( helps loading perfomance )
    void StartDelayedConvert();
    void FinishDelayedConvert();

    bool AddDangerArea(int nTime, SAIObjectPlacement &place);
    bool RemoveDangerArea(SAIObjectPlacement &place);
    int GetLastDangerAddTime() { return nLastDangerAddTime; }
    void ClearLastDangerAddTime() { nLastDangerAddTime = 0; }
    //////////// for map objects ////////////
    // Adds map object on the map
    bool AddObject(CMapObject *pObject) { return mapObjects.AddObject(pObject); }
    //	Removes map object from the map
    bool RemoveObject(CMapObject *pObject) { return mapObjects.RemoveObject(pObject); }
    // moves object on map
    bool MoveObject(CMapObject *pObject, const CPlot3D &newCP)
    {
        return mapObjects.MoveObject(pObject, newCP);
    }
    void OrientObject(CMapObject *pObject, const CQuaternion &q);
    void OrientObject(CMapObject *pObject, const CPlot3D &axisZ);
    // операции с динамическими и статическими локами
    bool CanPutObject(CMapObject *pMapObject, int nX, int nY);
    // функция для использования из mapeditor, убирает объект, проверяет террейн и локи и ставит
    // объект обратно
    bool CanPutObject(CMapObject *pMapObject, const CPlot3D &p);
    // проверяет не мешают ли какие-либо локи поместить pUnit на ( nX, nY )
    bool NotLocked(CMapObject *pMapObject, int nX, int nY, EAILock &res);
    bool CanMoveTo(CUnit *pUnit, int nX, int nY, EAILock &res);
    // bool IsStaticLocked( int nUnitClass, int nX, int nY );
    // проверить на столкновение с другими юнитами по кругам ( TRUE если сталкивается )
    CUnit *CheckCircleCollision(CUnit *pUnit, const CPlot3D &newPos);
    // конвертировать динамические блокировки в статические и обратно в районе ptPlace
    void ConvertLocks(const CPlot3D &ptPlace, bool bLock);
    // bool IsWideUnit( int nUnitClass ) { return nUnitClass >= 5; }
    BYTE GetUnitLockCheck(int nUnitClass);
    BYTE GetUnitLockCheck(CUnit *pUnit);

    //////////// for scan functions ////////////
    // оттрасировать террейн и объекты из src в dst, возвращает прозрачность ( 1 - transparent, 0 -
    // opaque )
    float TraceRelief(const CPlot3D &src, const CPlot3D &dst);
    void TraceVisionToArea(const CPlot3D &ptSrc, float fpSenseRadius, const CPlot3D &ptVisionDir,
                           float fpVisionArc, float fpTargetH, SVisibilityMask &res);
    float TraceReliefDH(const CPlot3D &src, const CPlot3D &dst);
    // get aiObjects cell size
    int GetObjectCellSize() { return mapObjects.GetCellSize(); };
    // get list of map objects for one cell of aiObjects
    CMapObjectList *GetObjectCellList(int nCellX, int nCellY)
    {
        return mapObjects.GetCellList(nCellX, nCellY);
    }
    // get list of units for one cell of aiObjects
    CUnitList *GetUnitCellList(int nCellX, int nCellY)
    {
        return mapObjects.GetUnitCellList(nCellX, nCellY);
    }
    void GetCellCoords(const CPlot3D &pt, int &nCellX, int &nCellY)
    {
        nCellX = Float2AITile(pt.x) / GetObjectCellSize();
        nCellY = Float2AITile(pt.y) / GetObjectCellSize();
    }
    // get list of map objects that are near pObject using hash tables of aiObjects
    void GetObjectsNear(const CPlot3D &pos, CMapObjectList &list, float fpRadius)
    {
        mapObjects.GetObjectsNear(pos, list, fpRadius);
    }
    void GetUnitsNear(const CPlot3D &pos, CUnitList &list, float fpRadius)
    {
        mapObjects.GetUnitsNear(pos, list, fpRadius);
    }
    bool IsTileOnBridge(int nX, int nY) const { return map[nY][nX].bBridge; };
    // get height of tile in meters
    float GetTileHeight(int nX, int nY) const { return float(map[nY][nX].nHeight) / fpZMult; }
    float GetTileHeight(const CPlot3D &p) const;
    float GetOriginalTileHeight(const CPlot3D &p) const;
    float GetTileSurface(const CPlot3D &p) const;
    int GetTerraType(const CPlot3D &p) const;
    bool IsBridge(const CPlot3D &p) const;
    bool IsWater(const CPlot3D &p) const;
    bool IsDangerous(const CPlot3D &ptPlace);
    // convert height from meters to discrete values stored in aimap::map
    int GetMapHeight(float fpHeight) const { return Float2Int(fpHeight * fpZMult); }
    // tests tile to be in bounds
    bool TileInBounds(int nX, int nY) const;
    // for debugging
    void DrawMap(int nUnit, const char *lpszFileName);
    // get tileinfo for tile on pos
    SAITileInfo &GetTileInfo(const CPlot3D &pos);

    friend class CAIGraph;
    friend class CAIObjects;
    friend class CAIDynamicLockManager;
};

/////////////////////////////////////////////////////////////////////////////////////

inline bool CAIMap::TileInBounds(int nX, int nY) const
{
    if (nX < 0 || nY < 0 || nX >= XSize() || nY >= YSize())
        return FALSE;
    return TRUE;
}

inline int ManhattenDistance(int nSrcX, int nSrcY, int nDstX, int nDstY)
{
    int nDX = abs(nSrcX - nDstX), nDY = abs(nSrcY - nDstY);
    return ((Min(nDX, nDY) * 1448) >> 10) + abs(nDX - nDY);
}
