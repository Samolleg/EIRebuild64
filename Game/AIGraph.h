#pragma once
/**********************************************************************
 *<
        FILE: AIGraph.H

        DESCRIPTION:  Description of AI graph classes

        CREATED BY: Mikhail Semyonov

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "SimplyArray.h"
#include "sArray.h"
#include "aiGraph.inl"
#include "aiRoute.h"
#include "aiPathFinder.h"
#include "SaveLoader.h"

/////////////////////////////////////////////////////////////////////////////////////
const int N_GRAPH_LIST_SPLIT_NUMBER = 8; // must be power of 2
const int N_GRAPH_COST_DIVISOR = 128;

// --------------------------------------------------------------------------------------------
// struct used during graph search
// --------------------------------------------------------------------------------------------
struct SAIGraphNode
{
    int nCost;                   // cost to get here from start ( or finish )
    SAIGraphNode *pNext, *pPrev; // pointer on next and previous vertex for lists in pathfinding
    short nX, nY;                // coordinates of this node
    EAIDirection backDir;        // for backtracing
    bool bPermanent;             // cost for this node can`t be improved
};
typedef SAIGraphNode *PAIGraphNode;

// --------------------------------------------------------------------------------------------
// структура, описывающая расстояния до узлов в 8 направлениях от текущего
// --------------------------------------------------------------------------------------------
struct SAIVertex
{
    short nWeights[8]; // weights for 8 directions

    void Clear()
    {
        for (int i = 0; i < 8; i++)
            nWeights[i] = -1;
    }
    bool IsEmpty()
    {
        for (int i = 0; i < 8; i++)
            if (nWeights[i] >= 0)
                return false;
        return true;
    }
    SAIVertex() { Clear(); }
};

// --------------------------------------------------------------------------------------------
// месторасположение вершины графа внутри квадрата
// --------------------------------------------------------------------------------------------
#pragma pack(push, 1)
struct SAIVertexPlace
{
    BYTE nX : 4;
    BYTE nY : 4;
    SAIVertexPlace()
    {
        nX = 0;
        nY = 0;
    }
};
#pragma pack(pop)

// --------------------------------------------------------------------------------------------
// вершина графа и массив из них, для списка неиспользуемых вершин
// --------------------------------------------------------------------------------------------
struct SAIGraphVertice
{
    int nX, nY;
    SAIGraphVertice() = default;
    SAIGraphVertice(int x, int y)
        : nX(x)
        , nY(y)
    {
    }
    void Load(CSave &file)
    {
        file.Read(nX);
        file.Read(nY);
    }
    void Store(CSave &file)
    {
        file.Write(nX);
        file.Write(nY);
    }
};
typedef CArray<SAIGraphVertice> CAIGraphVertices;

// --------------------------------------------------------------------------------------------
// класс, реализующий элементарный шаг алгоритма поиска пути
// --------------------------------------------------------------------------------------------
class CAIGraph;
class CAIGraphPathFinder
{
    CArray2D<SAIGraphNode> waves; // по одному узлу на каждую вершину графа
    CAIGraph *pGraph;
    CArray2D<SAIVertex> *weights; // используемые веса ребер ( зависит от типа юнита )
    SAIGraphNode *pDead;          // список вершин с фиксированными весами
    SAIGraphNode *pFirst4[N_GRAPH_LIST_SPLIT_NUMBER]; // текущие активные вершины
    SAIGraphNode *pSrc; // текущий узел с наименьшим незафиксированным весом
    int nCurrentCost;   // цена до pSrc
    int nUnitIndex;     // индекс юнита, для которого ищется путь
    bool bBack;         // direction of search ( направление - можно искать путь с конца )
    bool bStarted;      // признак запущенности поиска :)

    // очистить использованные элементы
    void ClearWaveUnits(SAIGraphNode *pTile);
    // добавить вершину в списки активных
    void AddVertice(SAIGraphNode *pNode);
    // найти новую вершину с наименьшим весом среди активных
    void FindNewSrc();
    // попробовать шагнуть в направлении dir, backDir - то, что будет записано в GraphNode::backdir
    void TryStep(EAIDirection dir, EAIDirection backDir);
    // инициализировать объект - указатель на weights, очистить pFirst[]
    void StartWave(int nUnitClass);

public:
    // начальная инициализация
    void SetGraph(CAIGraph *pGra, bool bBackWave);
    CAIGraphPathFinder(CAIGraph *pGra = NULL)
    {
        bStarted = false;
        pGraph = NULL;
        if (pGra)
            SetGraph(pGra, false);
    }
    // начать поиск пути из Src в Dst
    bool StartSearch(int nUnitClass);
    // добавить исходный узел с координатами nX, nY
    bool AddStartingVertice(int nX, int nY, int nCost);
    // добавить узел, который не должен использоватся при pathfinding
    void AddDeadVertice(int nX, int nY);
    // больше новых исходных узлов добавлено не будет
    void FinishAddStartingVertices();
    // совершить один шаг распространения волны
    void NextWave();
    // получить текущую позицию
    void GetPosition(int &nX, int &nY, int &nCost);
    // получить стоимость пути из точки (x,y), N_INFINITE если путь не существует
    int GetPermanentCost(int nX, int nY);
    void CleanUp();
    void BackTraceRoute(int nX, int nY, CAIRoute &res);
};

// --------------------------------------------------------------------------------------------
// граф для ускорения pathfinding путем ограничения области поиска маршрута
// --------------------------------------------------------------------------------------------
typedef void (*AIGraphBuilderCallBack)(float fpCompleted);
typedef WORD AIGRAPHNODECOLOR;
class CAIMap;
class CAIGraph
{
    CAIGraphPathFinder wave, waveBack;
    // используется для определения расстояния от исходной  точки до ближайших вершин графа
    CWavePathFinder lowWave;
    // для прогресс - баров
    AIGraphBuilderCallBack progress;

    struct SReachedNode
    {
        int nX, nY; // координаты доступной вершины
        int nCost;  // стоимость прохода в эту вершину
        SReachedNode() = default;
        SReachedNode(int x, int y, int cost)
            : nX(x)
            , nY(y)
            , nCost(cost)
        {
        }
    };
    typedef CList<SReachedNode> CNodesList;
    // выполнить распространение волн из начала и конца до тех пор пока они сойдутся или обломится
    bool FindPath(CAIRoute &res);
    // найти достижимые из (x,y) вершины графа
    void TraceToNodes(int nUnitClass, bool bBack, int nX, int nY, CNodesList &res);
    // добавить исходные вершины из nX, nY ( nX, nY - в координатах ai тайлов )
    bool AddVertices(int nUnitClass, bool bBack, CAIGraphPathFinder &w, int nX, int nY);
    // контролировать симметричность графа, удобно для проверки
    void MakeConsistent();
    // инициализировать волноводы
    void InitPathFinders()
    {
        wave.SetGraph(this, false);
        waveBack.SetGraph(this, true);
    }
    // проверить является ли токча nPlace подходящей для квадрата nSquare для юнитов nUnitClass
    bool CheckBorderCross(int nUnitClass, int nSquareX, int nSquareY, int nPlaceX, int nPlaceY);
    // найти точку, в которой надо разместить узел графа для клетки x,y
    void FindNodePlace(CAIMap *pMap, int x, int y);
    // определить веса линков из клетки x, y
    void FindLinkWeights(CAIMap *pMap, int x, int y);
    // построить граф для узлов, расположенных в указанных границах
    void BuildGraphInRegion(CAIMap *pMap, int nLowX, int nHighX, int nLowY, int nHighY);
    // покрасить вершину (x,y), а заодно попробовать покрасить и ее соседей
    void ColorIt(int nKind, CAIGraphVertices &listSrc, CAIGraphVertices &listDst,
                 AIGRAPHNODECOLOR wColor);
    // раскрасить вершины графа ( назначить каждой компоненте связности уникальный номер)
    void ColorGraph();

public:
    CArray2D<SAIVertex> vertices[N_AI_UNIT_KINDS];
    CArray2D<SAIVertexPlace> places[N_AI_UNIT_KINDS];
    CArray2D<AIGRAPHNODECOLOR> colors[N_AI_UNIT_KINDS];

    CAIGraph();
    void SetMap(CAIMap *pMap);
    void Build(CAIMap *pMap);
    void BuildRegion(CAIMap *pMap, int nLowX, int nHighX, int nLowY, int nHighY);
    void SetBuildCallBack(AIGraphBuilderCallBack newCallBack) { progress = newCallBack; }
    int XSize() { return vertices[0].GetXSize(); }
    int YSize() { return vertices[0].GetYSize(); }
    // координаты в ai тайлах, путь возвращается в коородинатах вершин графа
    bool FindGraphPath(int nUnitClass, int nSrcX, int nSrcY, int nDstX, int nDstY, CAIRoute &res,
                       CAIGraphVertices &exclude);
    // посчитать цвет некоторого тайла, считается, что мы идем из этого тайла (а не в этот тайл)
    AIGRAPHNODECOLOR GetColor(int nUnitClass, int nX, int nY);
    // определяет имеет ли точка (nX,nY) цвет color, записывает найденную информацию
    // о каждом проверенном тайле в tempMap в виде: 0-uknonwn 1-has suitable color 2-wrongcolor
    // return true если цвета совпадают
    // считается, что мы идем в этот тайл
    bool CheckColor(int nUnitClass, int nX, int nY, AIGRAPHNODECOLOR color,
                    CArray2D<char> &tempMap);

    bool Load(CSave &file);
    void Store(CSave &file);
    friend class CAIGraphPathFinder;
};
