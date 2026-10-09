/**********************************************************************
 *<
        FILE: aiGraph.cpp

        DESCRIPTION:  AI graph implementation

        CREATED BY: Mikhail Semyonov

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

#include "StdAfx.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiGraph.h"
#include "unit.h"
#include "aiMap.h"
#include "ModCliTool.h"

#ifdef __AI_GRAPHGENERATOR
#include <iostream>
#endif
/////////////////////////////////////////////////////////////////////////////////////
#ifndef _DEBUG
#pragma optimize("agw", on)
#endif
/////////////////////////////////////////////////////////////////////////////////////
// CAIGraphPathFinder &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::SetGraph(CAIGraph *pGra, bool bBackWave)
{
    int x, y;
    pGraph = pGra;
    bBack = bBackWave;
    waves.SetSizes(pGra->vertices[0].GetXSize(), pGra->vertices[0].GetYSize());
    for (y = 0; y < waves.GetYSize(); y++)
    {
        for (x = 0; x < waves.GetXSize(); x++)
        {
            waves[y][x].bPermanent = false;
            waves[y][x].nCost = N_INFINITE;
            waves[y][x].nX = x;
            waves[y][x].nY = y;
            waves[y][x].pNext = NULL;
            waves[y][x].pPrev = NULL;
            waves[y][x].backDir = NULL_DIR;
        }
    }
    pDead = NULL;
    bStarted = false;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::ClearWaveUnits(SAIGraphNode *pTile)
{
    while (pTile)
    {
        SAIGraphNode *pNxt = pTile->pNext;
        pTile->nCost = N_INFINITE;
        pTile->pNext = 0;
        pTile->pPrev = 0;
        pTile->backDir = NULL_DIR;
        pTile->bPermanent = false;
        pTile = pNxt;
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::AddVertice(SAIGraphNode *pNode)
{
    int &nCost = pNode->nCost;
    PAIGraphNode &pFirst = pFirst4[pNode->nX & (N_GRAPH_LIST_SPLIT_NUMBER - 1)];

    if (pNode == pFirst)
        return;
    // remove from list if its already in it
    if (pNode->pNext)
        pNode->pNext->pPrev = pNode->pPrev;
    if (pNode->pPrev)
        pNode->pPrev->pNext = pNode->pNext;

    ASSERT(pDead != pNode);
    // if ( pDead == pNode) pDead = pNode;
    pNode->pNext = NULL;
    pNode->pPrev = NULL;
    // insert into list on new place, keeps list sorted
    if (pFirst == NULL)
    {
        pNode->pNext = pFirst;
        pFirst = pNode;
        ASSERT(pNode != pNode->pNext);
    }
    else if (pFirst->nCost >= nCost)
    {
        pNode->pNext = pFirst;
        pFirst->pPrev = pNode;
        pFirst = pNode;
        ASSERT(pNode != pNode->pNext);
    }
    else
    {
        SAIGraphNode *pTry = pFirst;
        while (pTry->pNext && pTry->pNext->nCost < nCost)
            pTry = pTry->pNext;

        ASSERT(pTry->pNext != pNode);
        ASSERT(pTry != pTry->pNext);

        pNode->pNext = pTry->pNext;
        if (pTry->pNext)
            pTry->pNext->pPrev = pNode;
        pTry->pNext = pNode;
        pNode->pPrev = pTry;

        ASSERT(pNode != pNode->pNext);
    }
}

/*inline int UnderEstimate( SPTWave *pSrc, SPTWave *pFinal ) // for alpha-star optimization
{
        return ManhDistance( pSrc->nX, pSrc->nY, pFinal->nX, pFinal->nY ) * 100;
}*/

/////////////////////////////////////////////////////////////////////////////////////
static int nDX[9] = { 0, 0, -1, -1, -1, 0, 1, 1, 1 };
static int nDY[9] = { 0, -1, -1, 0, 1, 1, 1, 0, -1 };

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::TryStep(EAIDirection dir, EAIDirection backDir)
{
    int nDelay, nCost;
    nDelay = (*weights)[pSrc->nY][pSrc->nX].nWeights[dir - 1];
    if (nDelay < 0)
        return;
    SAIGraphNode *pDst = &waves[pSrc->nY + nDY[dir]][pSrc->nX + nDX[dir]];
    if (nCurrentCost >= pDst->nCost)
        return;
    nCost = nDelay + nCurrentCost; // + UnderEstimate( pDst, pFinalTile );
    if (nCost < pDst->nCost)
    {
        pDst->nCost = nCost;
        pDst->backDir = backDir;
        AddVertice(pDst); // place vertice into queue
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::StartWave(int nUnitClass)
{
    weights = &pGraph->vertices[nUnitClass];

    for (int i = 0; i < N_GRAPH_LIST_SPLIT_NUMBER; i++)
        pFirst4[i] = NULL;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIGraphPathFinder::StartSearch(int nUnitClass)
{
    ASSERT(!bStarted);
    StartWave(nUnitClass);
    bStarted = true;
    pSrc = NULL;
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIGraphPathFinder::AddStartingVertice(int nX, int nY, int nCost)
{
    ASSERT(bStarted);
    SAIGraphNode *pDst = &waves[nY][nX];
    if (nCost > pDst->nCost)
        return false;
    pDst->nCost = nCost;
    AddVertice(pDst);
    return true;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::FinishAddStartingVertices()
{
    ASSERT(bStarted);
    ASSERT(pSrc == NULL);

    FindNewSrc();
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::FindNewSrc()
{
    int nMin = N_INFINITE;
    pSrc = NULL;

    ASSERT(N_GRAPH_LIST_SPLIT_NUMBER == 8);
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
    CheckOneFirst(pFirst4[4]);
    CheckOneFirst(pFirst4[5]);
    CheckOneFirst(pFirst4[6]);
    CheckOneFirst(pFirst4[7]);
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::AddDeadVertice(int nX, int nY)
{
    SAIGraphNode *pDst = &waves[nY][nX];
    if (pDst->nCost < N_INFINITE)
        return;
    ASSERT(!pDst->pNext);
    pDst->nCost = -1;
    pDst->pNext = pDead;
    pDead = pDst;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::NextWave()
{
    ASSERT(bStarted);
    ASSERT(pSrc);

    int nHash;
    nHash = pSrc->nX & (N_GRAPH_LIST_SPLIT_NUMBER - 1);

    pSrc->bPermanent = true;
    pFirst4[nHash] = pSrc->pNext;
    if (pFirst4[nHash])
        pFirst4[nHash]->pPrev = NULL;
    // if(pDead) pDead->pPrev = pSrc;  pSrc->pPrev = NULL; // for alpha star is essential, because
    // it can reuse nodes
    pSrc->pNext = pDead;
    pDead = pSrc;

    nCurrentCost = pSrc->nCost; // - UnderEstimate( pSrc, pFinalTile );

    TryStep(DOWN, UP);
    TryStep(DOWNLEFT, UPRIGHT);
    TryStep(LEFT, RIGHT);
    TryStep(UPLEFT, DOWNRIGHT);
    TryStep(UP, DOWN);
    TryStep(UPRIGHT, DOWNLEFT);
    TryStep(RIGHT, LEFT);
    TryStep(DOWNRIGHT, UPLEFT);

    FindNewSrc();
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::GetPosition(int &nX, int &nY, int &nCost)
{
    nCost = N_INFINITE;
    if (!pSrc)
        return;
    nX = pSrc->nX;
    nY = pSrc->nY;
    nCost = pSrc->nCost;
}

/////////////////////////////////////////////////////////////////////////////////////
int CAIGraphPathFinder::GetPermanentCost(int nX, int nY)
{
    if (!waves[nY][nX].bPermanent)
        return N_INFINITE;
    return waves[nY][nX].nCost;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::BackTraceRoute(int nX, int nY, CAIRoute &res)
{
    res.Clear();
    if (nX < 0 || nX >= waves.GetXSize() || nY < 0 || nY >= waves.GetYSize())
        return;
    while (waves[nY][nX].backDir != NULL_DIR)
    {
        res.AddHeadUnit(nX, nY);
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
    res.AddHeadUnit(nX, nY);
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraphPathFinder::CleanUp()
{
    ClearWaveUnits(pDead);
    for (int i = 0; i < N_GRAPH_LIST_SPLIT_NUMBER; i++)
        ClearWaveUnits(pFirst4[i]);

#ifdef _DEBUG
    for (int y = 0; y < waves.GetYSize(); y++)
        for (int x = 0; x < waves.GetXSize(); x++)
            ASSERT(waves[y][x].nCost == N_INFINITE);
#endif
    pDead = NULL;
    bStarted = false;
}

/////////////////////////////////////////////////////////////////////////////////////
// CAIGraph implementation
/////////////////////////////////////////////////////////////////////////////////////
CAIGraph::CAIGraph()
{
    wave.SetGraph(this, false);
    waveBack.SetGraph(this, true);
    progress = NULL;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIGraph::FindPath(CAIRoute &res)
{
    bool bRv = true;
    int x, y, cost;
    while (1)
    {
        wave.NextWave();
        waveBack.NextWave();
        wave.GetPosition(x, y, cost);
        if (cost != N_INFINITE)
        {
            if (waveBack.GetPermanentCost(x, y) < N_INFINITE)
                break;
            else
            {
                waveBack.GetPosition(x, y, cost);
                if (cost != N_INFINITE)
                {
                    if (wave.GetPermanentCost(x, y) < N_INFINITE)
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
        wave.BackTraceRoute(x, y, res);
        waveBack.BackTraceRoute(x, y, backRoute);
        res += backRoute.Invert();
        res.EraseDuplicates();
    }
    return bRv;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIGraph::CheckBorderCross(int nUnitClass, int nSquareX, int nSquareY, int nPlaceX,
                                int nPlaceY)
{
    int i;
    bool bRv, bLeft, bRight;
    int nLeftX, nRightX, nUpY, nDownY;

    nLeftX = nSquareX * N_AI_GRAPH_CELL_SIZE;
    nUpY = nSquareY * N_AI_GRAPH_CELL_SIZE;
    nRightX = (nSquareX + 1) * N_AI_GRAPH_CELL_SIZE;
    nDownY = (nSquareY + 1) * N_AI_GRAPH_CELL_SIZE;

    bRv = lowWave.StartSquare(nUnitClass, nLeftX, nUpY, nRightX, nDownY, nLeftX + nPlaceX,
                              nUpY + nPlaceY);
    ASSERT(bRv);
    if (!bRv)
        return true;
    lowWave.RunSquare();
    // check for horizontal passing
    bLeft = false;
    bRight = false;
    for (i = 0; i <= N_AI_GRAPH_CELL_SIZE; i++)
    {
        if (lowWave.GetPermanentCost(nLeftX, nUpY + i) < N_INFINITE)
            bLeft = true;
        if (lowWave.GetPermanentCost(nRightX, nUpY + i) < N_INFINITE)
            bRight = true;
    }
    if (bLeft && bRight)
    {
        lowWave.CleanUp();
        return true;
    }
    // check for vertical passing
    bLeft = false;
    bRight = false;
    for (i = 0; i <= N_AI_GRAPH_CELL_SIZE; i++)
    {
        if (lowWave.GetPermanentCost(nLeftX + i, nUpY) < N_INFINITE)
            bLeft = true;
        if (lowWave.GetPermanentCost(nLeftX + i, nDownY) < N_INFINITE)
            bRight = true;
    }
    lowWave.CleanUp();
    if (bLeft && bRight)
    {
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////////////
// place graph nodes on the cells ( search most passable tile for each kind of unit from center of
// cell ) algorithm is not very efficient, but easy to code :)
void CAIGraph::FindNodePlace(CAIMap *pMap, int x, int y)
{
    int i;
    for (i = 0; i < N_AI_UNIT_KINDS; i++)
    {
        int ksize = N_AI_GRAPH_CELL_SIZE & 1, tekTerrain = 0;
        bool bFoundPassingPlace = false, bPass;
        places[i][y][x].nX = 3;
        places[i][y][x].nY = 3;
        while (ksize <= N_AI_GRAPH_CELL_SIZE)
        {
            int dx, dy, offset = (N_AI_GRAPH_CELL_SIZE - ksize) / 2;
            for (dy = offset; dy < offset + ksize; dy++)
            {
                for (dx = offset; dx < offset + ksize; dx++)
                {
                    int nTerra;
                    int sx = x * N_AI_GRAPH_CELL_SIZE + dx;
                    int sy = y * N_AI_GRAPH_CELL_SIZE + dy;
                    if (sx < 0 || sy < 0 || sx >= pMap->XSize() || sy >= pMap->YSize())
                        continue;
                    nTerra = (pMap->map[sy][sx].dwTerrains >> (i * 4)) & 15;
                    if (nTerra == 0)
                        continue;
                    if (nTerra > tekTerrain || !bFoundPassingPlace)
                    {
                        bPass = CheckBorderCross(i, x, y, dx, dy);
                        if (bPass || !bFoundPassingPlace)
                        {
                            bFoundPassingPlace = bPass;
                            tekTerrain = nTerra;
                            places[i][y][x].nX = dx;
                            places[i][y][x].nY = dy;
                        }
                    }
                }
            }
            ksize += 2;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraph::FindLinkWeights(CAIMap *pMap, int x, int y)
{
    if (ModCliTool::GeneratingGraph())
    {
        CAIRoute r;
        int dir, kind;
        for (dir = 0; dir < 8; dir++)
        {
            int nx = x + nDX[dir + 1], ny = y + nDY[dir + 1];
            if (nx < 0 || ny < 0 || nx >= XSize() || ny >= YSize())
            {
                // destination node is out of map
                for (kind = 0; kind < N_AI_UNIT_KINDS; kind++)
                    vertices[kind][y][x].nWeights[dir] = -1;
                continue;
            }
            for (kind = 0; kind < N_AI_UNIT_KINDS; kind++)
            {
                int nCost;
                int nSrcX = x * N_AI_GRAPH_CELL_SIZE + places[kind][y][x].nX;
                int nSrcY = y * N_AI_GRAPH_CELL_SIZE + places[kind][y][x].nY;
                int nDstX = nx * N_AI_GRAPH_CELL_SIZE + places[kind][ny][nx].nX;
                int nDstY = ny * N_AI_GRAPH_CELL_SIZE + places[kind][ny][nx].nY;
                if (pMap->FindPath(kind, nSrcX, nSrcY, nDstX, nDstY, r, &nCost))
                {
                    nCost /= N_GRAPH_COST_DIVISOR;
                    if (nCost > 0x7fff) // overflow check
                        nCost = 0x7fff;
                    vertices[kind][y][x].nWeights[dir] = nCost;
                }
                else
                    vertices[kind][y][x].nWeights[dir] = -1;
            }
        }
        return;
    }
// в графогенераторе используется сравнительно медленный, но более точный метод, для
// генерации графа realtime используется более быстрый вариант, но этот вариант может
// дать неверные результаты в некоторых вырожденных случаях
#ifndef __AI_GRAPHGENERATOR
    // быстрый
    for (int nKind = 0; nKind < N_AI_UNIT_KINDS; nKind++)
    {
        int nStartX = x * N_AI_GRAPH_CELL_SIZE + places[nKind][y][x].nX,
            nStartY = y * N_AI_GRAPH_CELL_SIZE + places[nKind][y][x].nY;
        lowWave.SetBackWaveFlag(false);
        lowWave.StartSquare(nKind, (x - 1) * N_AI_GRAPH_CELL_SIZE, (y - 1) * N_AI_GRAPH_CELL_SIZE,
                            (x + 2) * N_AI_GRAPH_CELL_SIZE, (y + 2) * N_AI_GRAPH_CELL_SIZE, nStartX,
                            nStartY);

        lowWave.RunSquare();

        for (int dir = 0; dir < 8; dir++)
        {
            int nPx = x + nDX[dir + 1], nPy = y + nDY[dir + 1];
            if (nPx < 0 || nPy < 0 || nPx >= XSize() || nPy >= YSize())
            {
                // destination node is out of map
                vertices[nKind][y][x].nWeights[dir] = -1;
                continue;
            }
            int nCost;
            int nTestX = nPx * N_AI_GRAPH_CELL_SIZE + places[nKind][nPy][nPx].nX,
                nTestY = nPy * N_AI_GRAPH_CELL_SIZE + places[nKind][nPy][nPx].nY;
            // lets determine path length to destination
            nCost = lowWave.GetPermanentCost(nTestX, nTestY);
            if (nCost < N_INFINITE)
            {
                nCost /= N_GRAPH_COST_DIVISOR;
                if (nCost > 0x7fff) // overflow check
                    nCost = 0x7fff;
                vertices[nKind][y][x].nWeights[dir] = nCost;
            }
            else
                vertices[nKind][y][x].nWeights[dir] = -1;
        }
        lowWave.CleanUp();
    }
#else  //__AI_GRAPHGENERATOR
       // более корректный
    CAIRoute r;
    int dir, kind;
    for (dir = 0; dir < 8; dir++)
    {
        int nx = x + nDX[dir + 1], ny = y + nDY[dir + 1];
        if (nx < 0 || ny < 0 || nx >= XSize() || ny >= YSize())
        {
            // destination node is out of map
            for (kind = 0; kind < N_AI_UNIT_KINDS; kind++)
                vertices[kind][y][x].nWeights[dir] = -1;
            continue;
        }
        for (kind = 0; kind < N_AI_UNIT_KINDS; kind++)
        {
            int nCost;
            int nSrcX = x * N_AI_GRAPH_CELL_SIZE + places[kind][y][x].nX;
            int nSrcY = y * N_AI_GRAPH_CELL_SIZE + places[kind][y][x].nY;
            int nDstX = nx * N_AI_GRAPH_CELL_SIZE + places[kind][ny][nx].nX;
            int nDstY = ny * N_AI_GRAPH_CELL_SIZE + places[kind][ny][nx].nY;
            if (pMap->FindPath(kind, nSrcX, nSrcY, nDstX, nDstY, r, &nCost))
            {
                nCost /= N_GRAPH_COST_DIVISOR;
                if (nCost > 0x7fff) // overflow check
                    nCost = 0x7fff;
                vertices[kind][y][x].nWeights[dir] = nCost;
            }
            else
                vertices[kind][y][x].nWeights[dir] = -1;
        }
    }
#endif //__AI_GRAPHGENERATOR
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraph::BuildGraphInRegion(CAIMap *pMap, int nLowX, int nHighX, int nLowY, int nHighY)
{
    int x, y;
    nLowY = Max(0, nLowY);
    nHighY = Min(nHighY, YSize());
    nLowX = Max(0, nLowX);
    nHighX = Min(nHighX, XSize());

    for (y = nLowY; y < nHighY; y++)
    {
        for (x = nLowX; x < nHighX; x++)
        {
            FindNodePlace(pMap, x, y);
        }
        if (progress)
            (*progress)(0.125f * (float(y) / YSize()));
    }
    // calc distances for neighbouring cells
    for (y = nLowY; y < nHighY; y++)
    {
        for (x = nLowX; x < nHighX; x++)
        {
            FindLinkWeights(pMap, x, y);
        }
        if (progress)
            (*progress)(0.125f + 0.875f * (float(y) / YSize()));
    }
    MakeConsistent();
    InitPathFinders();
    ColorGraph(); // this takes ~100ms for zone1 on c300
    progress = 0;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraph::Build(CAIMap *pMap)
{
    SetMap(pMap);
    BuildGraphInRegion(pMap, 0, XSize(), 0, YSize());
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraph::BuildRegion(CAIMap *pMap, int nLowX, int nHighX, int nLowY, int nHighY)
{
    BuildGraphInRegion(pMap, nLowX, nHighX, nLowY, nHighY);
}

/////////////////////////////////////////////////////////////////////////////////////
// this can be done faster by removing recursion and replacing lists with less expensive
// on allocation data structures
void CAIGraph::ColorIt(int nKind, CAIGraphVertices &src, CAIGraphVertices &dst,
                       AIGRAPHNODECOLOR wColor)
{
    dst.SetSize(0);
    for (int i = 0; i < src.GetSize(); i++)
    {
        SAIGraphVertice &node = src[i];
        int x = node.nX, y = node.nY;
        for (int nDir = 0; nDir < 8; nDir++)
        {
            if (vertices[nKind][y][x].nWeights[nDir] >= 0)
            {
                int nx = x + nDX[nDir + 1], ny = y + nDY[nDir + 1];
                if (colors[nKind][ny][nx] == 0)
                {
                    colors[nKind][ny][nx] = wColor;
                    dst.Add(SAIGraphVertice(nx, ny));
                }
            }
        }
    }
    if (dst.GetSize())
        ColorIt(nKind, dst, src, wColor);
}

/////////////////////////////////////////////////////////////////////////////////////
// для того, чтобы функция работала правильно должно быть выполнена MakeConsistent()
// ColorIt() не проверяет на выход за пределы массива
void CAIGraph::ColorGraph()
{
    int x, y, i;
    static CAIGraphVertices nodes1, nodes2;
    nodes1.RemoveAll();
    for (i = 0; i < N_AI_UNIT_KINDS; i++)
    {
        colors[i].FillZero();
        AIGRAPHNODECOLOR wColor = 1;
        for (y = 0; y < YSize(); y++)
        {
            for (x = 0; x < XSize(); x++)
            {
                if (colors[i][y][x] == 0)
                {
                    colors[i][y][x] = wColor;
                    nodes1.Add(SAIGraphVertice(x, y));
                    ColorIt(i, nodes1, nodes2, wColor);
                    wColor++;
                }
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
// проследить возможность пройти из (nX,nY) (в aiTiles) в ближайшие 9 узлов графа, если
// можно записать из в список res
void CAIGraph::TraceToNodes(int nUnitClass, bool bBack, int nX, int nY, CNodesList &res)
{
    int nDx, nDy, nCost;
    int nCellX = nX / N_AI_GRAPH_CELL_SIZE, nCellY = nY / N_AI_GRAPH_CELL_SIZE;
    nCellX = Min(XSize() - 1, nCellX);
    nCellX = Max(0, nCellX);
    nCellY = Min(YSize() - 1, nCellY);
    nCellY = Max(0, nCellY);
    res.RemoveAll();

    lowWave.SetBackWaveFlag(bBack);
    lowWave.StartSquare(nUnitClass, (nCellX - 1) * N_AI_GRAPH_CELL_SIZE,
                        (nCellY - 1) * N_AI_GRAPH_CELL_SIZE, (nCellX + 2) * N_AI_GRAPH_CELL_SIZE,
                        (nCellY + 2) * N_AI_GRAPH_CELL_SIZE, nX, nY);

    lowWave.SaveLocks(true);
    lowWave.RunSquare();
    lowWave.RestoreLocks();

    for (nDy = -1; nDy <= 1; nDy++)
    {
        int nPy = nCellY + nDy;
        if (nPy < 0 || nPy >= YSize())
            continue;
        for (nDx = -1; nDx <= 1; nDx++)
        {
            int nPx = nCellX + nDx;
            if (nPx < 0 || nPx >= XSize())
                continue;
            int x = nPx * N_AI_GRAPH_CELL_SIZE + places[nUnitClass][nPy][nPx].nX,
                y = nPy * N_AI_GRAPH_CELL_SIZE + places[nUnitClass][nPy][nPx].nY;
            nCost = lowWave.GetPermanentCost(x, y);
            if (nCost < N_INFINITE)
                res.AddTail(SReachedNode(nPx, nPy, nCost));
        }
    }
    lowWave.CleanUp();
}

/////////////////////////////////////////////////////////////////////////////////////
// функция трассирует пути из nX, nY в ближайшие узлы и смотрит их цвет
struct SColorNode
{
    AIGRAPHNODECOLOR color;
    int nCount;
    SColorNode() = default;
    SColorNode(AIGRAPHNODECOLOR col)
    {
        color = col;
        nCount = 1;
    }
};
/////////////////////////////////////////////////////////////////////////////////////
AIGRAPHNODECOLOR CAIGraph::GetColor(int nUnitClass, int nX, int nY)
{
    POSITION pos, pos1;
    CNodesList nodes;
    TraceToNodes(nUnitClass, false, nX, nY, nodes);
    // здесь предполагается, что цвет всех вершин из nodes одинаков, это может быть не так
    // если рассматривается непроходимый тайл, с которого можно попасть на две различные
    // компоненты связности, такой вырожденный случай здесь не рассматривается
    // за цвет точки выбирается цвет большего количества доступных точек
    CList<SColorNode> foundColors;

    for (pos = nodes.GetHeadPosition(); pos;)
    {
        SReachedNode &n = nodes.GetNext(pos);
        AIGRAPHNODECOLOR reachedColor = colors[nUnitClass][n.nY][n.nX];
        for (pos1 = foundColors.GetHeadPosition(); pos1;)
        {
            SColorNode &c = foundColors.GetNext(pos1);
            if (c.color == reachedColor)
            {
                c.nCount++;
                goto Found;
            }
        }
        foundColors.AddTail(SColorNode(reachedColor));
    Found:;
    }
    int nBestCount = 0;
    AIGRAPHNODECOLOR resColor = 0;
    for (pos = foundColors.GetHeadPosition(); pos;)
    {
        SColorNode &n = foundColors.GetNext(pos);
        if (n.nCount > nBestCount)
        {
            resColor = n.color;
            nBestCount = n.nCount;
        }
    }
    return resColor;
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIGraph::CheckColor(int nUnitClass, int nX, int nY, AIGRAPHNODECOLOR color,
                          CArray2D<char> &tempMap)
{
    int nDx, nDy, nCost, i;
    int nCellX = nX / N_AI_GRAPH_CELL_SIZE, nCellY = nY / N_AI_GRAPH_CELL_SIZE;
    nCellX = Min(XSize() - 1, nCellX);
    nCellX = Max(0, nCellX);
    nCellY = Min(YSize() - 1, nCellY);
    nCellY = Max(0, nCellY);

    // form list of surrounding nodes
    static CAIGraphVertices nodes;
    nodes.SetSize(0);
    for (nDy = -1; nDy <= 1; nDy++)
    {
        int nPy = nCellY + nDy;
        if (nPy < 0 || nPy >= YSize())
            continue;
        for (nDx = -1; nDx <= 1; nDx++)
        {
            int nPx = nCellX + nDx;
            if (nPx < 0 || nPx >= XSize())
                continue;
            nodes.Add(SAIGraphVertice(nPx, nPy));
        }
    }
    // check for colors of surrounding nodes
    bool bNeededColorExist = false;
    for (i = 0; i < nodes.GetSize(); i++)
    {
        SAIGraphVertice &n = nodes[i];
        if (colors[nUnitClass][n.nY][n.nX] == color)
        {
            bNeededColorExist = true;
            break;
        }
    }
    // required color is not among surrounding, fill square with information about it
    if (!bNeededColorExist)
    {
        int nStartX = nCellX * N_AI_GRAPH_CELL_SIZE;
        int nStartY = nCellY * N_AI_GRAPH_CELL_SIZE;
        for (nDy = nStartY; nDy < nStartY + N_AI_GRAPH_CELL_SIZE; nDy++)
        {
            for (nDx = nStartX; nDx < nStartX + N_AI_GRAPH_CELL_SIZE; nDx++)
                tempMap[nDy][nDx] = 2;
        }
        return false;
    }
    // trace to all neighbouring nodes to make final checks
    int nSquareLx = (nCellX - 1) * N_AI_GRAPH_CELL_SIZE;
    int nSquareLy = (nCellY - 1) * N_AI_GRAPH_CELL_SIZE;
    int nSquareHx = (nCellX + 2) * N_AI_GRAPH_CELL_SIZE;
    int nSquareHy = (nCellY + 2) * N_AI_GRAPH_CELL_SIZE;
    lowWave.SetBackWaveFlag(true);
    lowWave.StartSquare(nUnitClass, nSquareLx, nSquareLy, nSquareHx, nSquareHy, nX, nY);

    lowWave.SaveLocks(true);
    lowWave.RunSquare();
    lowWave.RestoreLocks();

    // check every surrounding node to be reached
    bNeededColorExist = false;
    for (i = 0; i < nodes.GetSize(); i++)
    {
        SAIGraphVertice &n = nodes[i];
        int nPx = n.nX, nPy = n.nY;
        int x = nPx * N_AI_GRAPH_CELL_SIZE + places[nUnitClass][nPy][nPx].nX,
            y = nPy * N_AI_GRAPH_CELL_SIZE + places[nUnitClass][nPy][nPx].nY;
        nCost = lowWave.GetPermanentCost(x, y);
        if (nCost < N_INFINITE && colors[nUnitClass][nPy][nPx] == color)
        {
            // so this node of required color is reachable - specified tile has required color
            bNeededColorExist = true;
            break;
        }
    }
    {
        // заполним карту информацией относительно цвета достигнутых в процессе поиска
        // пути тайлов - они либо подходят по цвету, либо нет
        // это можно сделать быстрее, если будет тормозить
        char cFillData = bNeededColorExist ? 1 : 2;
        for (nDy = nSquareLy; nDy < nSquareHy; nDy++)
        {
            for (nDx = nSquareLx; nDx < nSquareHx; nDx++)
            {
                if (lowWave.GetPermanentCost(nDx, nDy) < N_INFINITE)
                {
                    tempMap[nDy][nDx] = cFillData;
                }
            }
        }
    }
    lowWave.CleanUp();
    return bNeededColorExist;
}

/////////////////////////////////////////////////////////////////////////////////////
// вычислить с помощью функций нижнего уровня (lw) расстояния от nX,nY до ближайщих 9 узлов графа
// и добавить соответствующие вершины в класс поиска путей по графу w
bool CAIGraph::AddVertices(int nUnitClass, bool bBack, CAIGraphPathFinder &w, int nX, int nY)
{
    bool bRv = false;
    CNodesList nodes;
    TraceToNodes(nUnitClass, bBack, nX, nY, nodes);
    for (POSITION pos = nodes.GetHeadPosition(); pos;)
    {
        SReachedNode &n = nodes.GetNext(pos);
        if (w.AddStartingVertice(n.nX, n.nY, n.nCost / N_GRAPH_COST_DIVISOR))
            bRv = true;
    }
    w.FinishAddStartingVertices();
    return bRv;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraph::SetMap(CAIMap *pMap)
{
    int i;
    lowWave.SetMap(pMap, false);
    int nXSize = pMap->XSize() / N_AI_GRAPH_CELL_SIZE;
    int nYSize = pMap->YSize() / N_AI_GRAPH_CELL_SIZE;
    for (i = 0; i < N_AI_UNIT_KINDS; i++)
    {
        vertices[i].SetSizes(nXSize, nYSize);
        places[i].SetSizes(nXSize, nYSize);
        colors[i].SetSizes(nXSize, nYSize);
    }
    InitPathFinders();
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIGraph::FindGraphPath(int nUnitClass, int nSrcX, int nSrcY, int nDstX, int nDstY,
                             CAIRoute &res, CAIGraphVertices &exclude)
{
    int i;
    bool bRv;
    ASSERT(nUnitClass >= 0 && nUnitClass < N_AI_UNIT_KINDS);
    if (!wave.StartSearch(nUnitClass)) // pUnit ) )
        return false;
    if (!waveBack.StartSearch(nUnitClass)) // pUnit ) )
    {
        wave.CleanUp();
        return false;
    }
    // уберем нежелательные вершины
    for (i = 0; i < exclude.GetSize(); i++)
    {
        wave.AddDeadVertice(exclude[i].nX, exclude[i].nY);
        waveBack.AddDeadVertice(exclude[i].nX, exclude[i].nY);
    }

    bRv = AddVertices(nUnitClass, false, wave, nSrcX, nSrcY);
    if (bRv)
        bRv = AddVertices(nUnitClass, true, waveBack, nDstX, nDstY);
    if (bRv)
        bRv = FindPath(res);

    wave.CleanUp();
    waveBack.CleanUp();
    return bRv;
}

/////////////////////////////////////////////////////////////////////////////////////
// checks up graph weights for correctness
void CAIGraph::MakeConsistent()
{
    int x, y, i, dir, kind, invDir;
    // make sure outside links are invalid
    for (y = 0; y < vertices[0].GetYSize(); y++)
    {
        for (i = 0; i < 8; i++)
        {
            vertices[i][y][0].nWeights[1] = -1;
            vertices[i][y][0].nWeights[2] = -1;
            vertices[i][y][0].nWeights[3] = -1;
            vertices[i][y][vertices[i].GetXSize() - 1].nWeights[5] = -1;
            vertices[i][y][vertices[i].GetXSize() - 1].nWeights[6] = -1;
            vertices[i][y][vertices[i].GetXSize() - 1].nWeights[7] = -1;
        }
    }
    for (x = 0; x < vertices[0].GetXSize(); x++)
    {
        for (int i = 0; i < 8; i++)
        {
            vertices[i][0][x].nWeights[7] = -1;
            vertices[i][0][x].nWeights[0] = -1;
            vertices[i][0][x].nWeights[1] = -1;
            vertices[i][vertices[i].GetYSize() - 1][x].nWeights[3] = -1;
            vertices[i][vertices[i].GetYSize() - 1][x].nWeights[4] = -1;
            vertices[i][vertices[i].GetYSize() - 1][x].nWeights[5] = -1;
        }
    }
    // make sure all links are symmetrical
    for (y = 0; y < YSize(); y++)
    {
        for (x = 0; x < XSize(); x++)
        {
            for (dir = 0; dir < 8; dir++)
            {
                int nx = x + nDX[dir + 1], ny = y + nDY[dir + 1];
                if (nx < 0 || ny < 0 || nx >= XSize() || ny >= YSize())
                    continue;
                invDir = (dir + 4) & 7;
                for (kind = 0; kind < 8; kind++)
                {
                    // ASSERT( !( vertices[kind][y][x].nWeights[dir] < 0 &&
                    // vertices[kind][ny][nx].nWeights[invDir] > 0 ) ); ASSERT( !(
                    // vertices[kind][y][x].nWeights[dir] > 0 &&
                    // vertices[kind][ny][nx].nWeights[invDir] < 0 ) );
                    int nWeight = Min(vertices[kind][y][x].nWeights[dir],
                                      vertices[kind][ny][nx].nWeights[invDir]);
                    vertices[kind][y][x].nWeights[dir] = nWeight;
                    vertices[kind][ny][nx].nWeights[invDir] = nWeight;
                }
            }
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
bool CAIGraph::Load(CSave &file)
{
    int nKind, nXSize, nYSize;

    file.Read(nXSize);
    file.Read(nYSize);
    ASSERT(nXSize > 0 && nYSize > 0 && nXSize <= 256 && nYSize <= 256);
    if (nXSize <= 0 || nYSize <= 0 || nXSize > 256 || nYSize > 256)
        return false;

    for (nKind = 0; nKind < N_AI_UNIT_KINDS; nKind++)
    {
        vertices[nKind].SetSizes(nXSize, nYSize);
        places[nKind].SetSizes(nXSize, nYSize);
        colors[nKind].SetSizes(nXSize, nYSize);
        for (int y = 0; y < YSize(); y++)
        {
            file.Read(&vertices[nKind][y][0], nXSize * sizeof(vertices[nKind][0][0]));
            file.Read(&places[nKind][y][0], nXSize * sizeof(places[nKind][0][0]));
            file.Read(&colors[nKind][y][0], nXSize * sizeof(colors[nKind][0][0]));
        }
    }
    InitPathFinders();
    return file.IsOk();
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIGraph::Store(CSave &file)
{
    int nXSize, nYSize, nKind;
    nXSize = XSize();
    nYSize = YSize();

    file.Write(nXSize);
    file.Write(nYSize);
    for (nKind = 0; nKind < N_AI_UNIT_KINDS; nKind++)
    {
        ASSERT(vertices[nKind].GetXSize() == nXSize && vertices[nKind].GetYSize() == nYSize);
        ASSERT(places[nKind].GetXSize() == nXSize && places[nKind].GetYSize() == nYSize);
        for (int y = 0; y < YSize(); y++)
        {
            file.Write(&vertices[nKind][y][0], nXSize * sizeof(vertices[nKind][0][0]));
            file.Write(&places[nKind][y][0], nXSize * sizeof(places[nKind][0][0]));
            file.Write(&colors[nKind][y][0], nXSize * sizeof(colors[nKind][0][0]));
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////
/*
-------------- some formation stuff by MS ( he claims this to be checked heh ----------
bool CAIGraph::FindPathFormation( CArray< SAIFormationUnit, SAIFormationUnit& > *pFormation,
        WORD wDstX, WORD wDstY, CAIMap *pMap, float fpFaceAngle )
{

        int nNumUnits = pFormation->GetSize();  // Number of units in the formation

        assert( nNumUnits > 0 );
        if( nNumUnits <= 0 )
                return false;                   // empty formation

        // *-*-*-*-*-*-*-* 1) Find center unit of the formation *-*-*-*-*-*-*-*
        // Xc = ( X0 + ... + Xn )/n
        // Yc = ( Y0 + ... + Yn )/n
        int nCenterTileX = 0,
                nCenterTileY = 0;

        for ( int i = 0; i < nNumUnits ;i++ )
        {
                nCenterTileX += ( *pFormation )[i].wX;
                nCenterTileY += ( *pFormation )[i].wY;
        }

        nCenterTileX /= nNumUnits;
        nCenterTileY /= nNumUnits;

        // find center unit - one closest to the center tile
        float fpClosest = 2000.0;
        int nCenterUnit = -1;
        for ( i = 0; i < nNumUnits; i++ )
        {
                float fpDistance = Distance( (WORD) nCenterTileX, (WORD) nCenterTileY,
                        ( *pFormation )[i].wX, ( *pFormation )[i].wY );
                if( fpDistance < fpClosest )
                {
                        fpClosest = fpDistance;
                        nCenterUnit = i;
                }
        }
        assert ( nCenterUnit >= 0 );
        SAIFormationUnit *centerUnit = &( ( *pFormation )[nCenterUnit] );

        // *-*-*-*-*-*-*-* 2) Find the path CenterUnit -> Destination *-*-*-*-*-*-*-*
//******** if we can't find the path directly from the center ???

        bool rv = FindShortestPath( NULL, centerUnit->wX, centerUnit->wY,
                wDstX, wDstY, &( centerUnit->route ) );

        if( !rv )
        {
                // path from the center unit is not found
                // we must find the closest to the center unit and find the path from it
                // *-*-*-*-*-*-*-*
                return false;
        }

        // *-*-*-*-*-*-*-* 3) For every object in formation find path *-*-*-*-*-*-*-*
        for ( i = 0; i < nNumUnits; i++ )
        {
                if( i != nCenterUnit ) 	        // we have already found path for it
                        rv = FindPathForFormationObject( &( (*pFormation)[i] ),
                                centerUnit->wX, centerUnit->wY,
                                &( centerUnit->route ), pMap, fpFaceAngle );
        }

        return true;
}
......this was commented ( start)
bool CAIGraph::FindPathFormationObject( SAIFormationUnit *pUnit,
        WORD wXCenter, WORD wYCenter, const CAIRoute *pRTCenterPath,  const CAIMap *pMap, float
fpFaceAngle )
{
        // 1) construct path
        // 2) interpolate path
        // 3) test found path on passability and modify it

        // Set offset to the center of the formation
        int nOffsetX = pUnit->wX - wXCenter,  // initial offsets
                nOffsetY = pUnit->wY - wYCenter;

        // *-*-*-*-*-*-*-* For every element in centerPath *-*-*-*-*-*-*-*
        POSITION pos = pRTCenterPath->route.GetTailPosition();
        WORD wCurX = pUnit->wX,
                wCurY = pUnit->wY,
                wNextX = 0,
                wNextY = 0,
                wPrevCenterX = wXCenter,
                wPrevCenterY = wYCenter;
        while ( pos != NULL )
        {
                // Test if path between this tile and the next one is passable
                SRouteUnit *pRU = pRTCenterPath->route.GetPrev( pos );
                // test next tile to be in bounds
                //wNextX = ( pRU->wX + nOffsetX >= 0)? pRU->wX + nOffsetX: 0;
                // wNextY = ( pRU->wY + nOffsetY >= 0)? pRU->wY + nOffsetY: 0;
                // wNextX = ( wNextX < nSizeX )? wNextX: nSizeX - 1;
                // wNextY = ( wNextY < nSizeY )? wNextY: nSizeY - 1;
                EAIDirection dirTravel = GetDirection( wPrevCenterX, wPrevCenterY,
                        pRU->wX, pRU->wY );
                float fpAngle = GetAngleDirection( dirTravel ) - fpFaceAngle;
                fpAngle = ToRadian( fpAngle );  // transform to radian

                int nNextIndex = GetNextCoord( pRU->wX, pRU->wY,
                        nOffsetX, nOffsetY, 0.0 );
                wNextX = GetX( nNextIndex );
                wNextY = GetY( nNextIndex );
                // determine the closest tile
                if( GetDirection( wCurX, wCurY, wNextX, wNextY ) == NULL_DIR )
                {
//			nNextIndex = GetNextClosest( wCurX, wCurY, wNextY, wNextY );


                // tiles are not the closest

                }
                bool bDiagonal = ( wCurX != wNextX && wCurY != wNextY )? true: false;

//*** must set height in aiMap
                // if path wCur->wNext is passable
                if( pMap->GetPassTimeCoord( wCurX, wCurY, wNextX, wNextY, bDiagonal ) > 0 )
                {
                // is passable
                        wCurX = wNextX;
                        wCurY = wNextY;
                        SRouteUnit *pRU = pUnit->route.GetRouteUnit();
#ifdef __AI_ROUTE_DEBUG
nGlblRouteUsed ++;
#endif
                        pRU->wX = wCurX;
                        pRU->wY = wCurY;
                        pUnit->route.route.AddHead( pRU );
                }
                else
                {
                // is impassable
                        // Find turn way and put it in the path
                        POSITION posNew = FindFormationObjectTurnWay( wCurX, wCurY, wNextX, wNextY,
                                nOffsetX, nOffsetY,
                                pUnit, pRTCenterPath, pos, LEFT, AI_N_TURNWAY_TILE_LIMIT, pMap );
                        if ( posNew == NULL  )
                        {
                                posNew = FindFormationObjectTurnWay( wCurX, wCurY, wNextX, wNextY,
                                        nOffsetX, nOffsetY,
                                        pUnit, pRTCenterPath, pos, RIGHT, AI_N_TURNWAY_TILE_LIMIT,
pMap ); assert( posNew != NULL );
                        }
                        pos = posNew;
                        // We must set new values of wCur and wNext
                        SRouteUnit *pRUU = pUnit->route.route.GetHead(); // last save tile
                        wCurX = pRUU->wX;
                        wCurY = pRUU->wY;
                }
        }

        return true;
}
......this was commented ( end)

bool CAIGraph::FindPathForFormationObject( SAIFormationUnit *pUnit,
        WORD wXCenter, WORD wYCenter, const CAIRoute *pRTCenterPath,  const CAIMap *pMap, float
fpFaceAngle )
{
        // 1) construct path
        // 2) interpolate path
        // 3) test found path on passability and modify it

        // Set offset to the center of the formation
        int nOffsetX = pUnit->wX - wXCenter,  // initial offsets
                nOffsetY = pUnit->wY - wYCenter;

        ///////////////////////////////////////////////////////////////////
        // Construct the path

        CAIRoute tempRoute;
        // *-*-*-*-*-*-*-* For every element in centerPath *-*-*-*-*-*-*-*
        POSITION pos = pRTCenterPath->route.GetTailPosition();
        WORD wCurX = pUnit->wX,
                wCurY = pUnit->wY,
                wNextX = 0,
                wNextY = 0,
                wPrevCenterX = wXCenter,
                wPrevCenterY = wYCenter;

        while ( pos != NULL )
        {
                // Test if path between this tile and the next one is passable
                SRouteUnit *pRU = pRTCenterPath->route.GetPrev( pos );
                // if this is the first tile go further
                if( wPrevCenterX == pRU->wX && wPrevCenterY == pRU->wY )
                        continue;
                EAIDirection dirTravel = GetDirection( wPrevCenterX, wPrevCenterY,
                        pRU->wX, pRU->wY );
                float fpAngle = GetAngleDirection( dirTravel ) - fpFaceAngle;
                fpAngle = ToRadian( fpAngle );  // transform to radian

                // Get the next tile on the line aligning by bounds
                int nNextIndex = GetNextCoord( pRU->wX, pRU->wY,
                        nOffsetX, nOffsetY, fpAngle );
                wNextX = GetX( nNextIndex );
                wNextY = GetY( nNextIndex );

                // if the formation object is not moved the direction also be NULL
                // we must go further
                if( wNextX == wCurX && wNextY == wCurY )
                        continue;


                bool bTurnIsMade = true;
                float fpX = wCurX,
                        fpY = wCurY;
                int nSteps = -1,
                        nStepCount = -1;
                float fpDeltaX = 0.0,
                        fpDeltaY = 0.0;

                if( GetDirection( wCurX, wCurY, wNextX, wNextY ) == NULL_DIR )
                {

                        bTurnIsMade = false; // we must move to the destination in several steps
                        nSteps = max( abs( int( wNextX ) - int( wCurX ) ),
                                abs( int( wNextY ) - int( wCurY ) ) );
                        fpDeltaX = float( int( wNextX ) - int( wCurX ) ) / float( nSteps );
                        fpDeltaY = float( int( wNextY ) - int( wCurY ) ) / float( nSteps );
                        nStepCount = 0;
                }

                do
                {
                        // determine are current and next tiles connected
                        if( !bTurnIsMade )
                        {
                        // tiles are not close to each other, we are to choose the closest one

                                fpX += fpDeltaX;
                                fpY += fpDeltaY;

                                if( ++nStepCount >= nSteps )
                                        bTurnIsMade = true;
                        }
                        else
                        {
                                fpX = wNextX;
                                fpY = wNextY;
                        }

                        // add tile to the path
                        if( bTurnIsMade )
                        {
                                wCurX = wNextX;
                                wCurY = wNextY;
                        }
                        // save previous position of the center unit
                        wPrevCenterX = pRU->wX;
                        wPrevCenterY = pRU->wY;
                        // save formation unit new
                        SRouteUnit *pRUNew = tempRoute.GetRouteUnit();
#ifdef __AI_ROUTE_DEBUG
nGlblRouteUsed ++;
#endif
                        pRUNew->wX = Float2Int( fpX );
                        pRUNew->wY = Float2Int( fpY );
                        tempRoute.route.AddHead( pRUNew );

                } while ( !bTurnIsMade );

        } 	// while ( pos != NULL )

        // * test if the path is completely connected
        pos = tempRoute.route.GetTailPosition();
        SRouteUnit *pRUnit = tempRoute.route.GetPrev( pos );
        while( pos != NULL )
        {
                wCurX = pRUnit->wX;
                wCurY = pRUnit->wY;
                pRUnit = tempRoute.route.GetPrev( pos );
                if ( GetDirection( wCurX, wCurY, pRUnit->wX, pRUnit->wY ) == NULL_DIR )
                        assert ( 0 );

        }
        // **************

        ///////////////////////////////////////////////////////////////////
        // interpolate the path constructed
// ***************

        ///////////////////////////////////////////////////////////////////
        // test the path constructed on passability and modify it

        pos = tempRoute.route.GetTailPosition();
        wNextX = wNextY = 0;
        wCurX = pUnit->wX;
        wCurY = pUnit->wY;

        while ( pos != NULL )
        {
                SRouteUnit *pRU = tempRoute.route.GetPrev( pos );

                wNextX = pRU->wX;
                wNextY = pRU->wY;
                // test is the path Cur->Next tile passable

                bool bDiagonal = ( wCurX != wNextX && wCurY != wNextY )? true: false;

//*** must set height in aiMap
                // if path wCur->wNext is passable
                if( pMap->GetPassTimeCoord( wCurX, wCurY, wNextX, wNextY, bDiagonal ) > 0 )
                {
                // is passable then save this route unit and go further
                        SRouteUnit *pRU = pUnit->route.GetRouteUnit();
#ifdef __AI_ROUTE_DEBUG
nGlblRouteUsed ++;
#endif
                        pRU->wX = wCurX;
                        pRU->wY = wCurY;
                        pUnit->route.route.AddHead( pRU );

                        wCurX = wNextX;
                        wCurY = wNextY;
                }
                else
                {
                // is impassable
        // ****** define what is out of the center LEFT or RIGHT and start from it
                        // Find turn way and put it in the path
                        // first from left
                        POSITION posNew = FindFormationObjectTurnWay( wCurX, wCurY, wNextX, wNextY,
                                0, 0,
                                pUnit, &( tempRoute ), pos,
                                LEFT, AI_N_TURNWAY_TILE_LIMIT, pMap );
                        if ( posNew == NULL  )
                        {
                                // and then from right
                                posNew = FindFormationObjectTurnWay( wCurX, wCurY, wNextX, wNextY,
                                        0, 0,
                                        pUnit, &( tempRoute ), pos,
                                        RIGHT, AI_N_TURNWAY_TILE_LIMIT, pMap );
                                assert( posNew != NULL );
                        }
                        pos = posNew;
                        // We must set new values of wCur and wNext
                        SRouteUnit *pRUU = pUnit->route.route.GetHead(); // last save tile
                        wCurX = pRUU->wX;
                        wCurY = pRUU->wY;
                }


        }	// while ( pos != NULL )
        tempRoute.Clear();


//		bool bDiagonal = ( wCurX != wNextX && wCurY != wNextY )? true: false;

//*** must set height in aiMap
                // if path wCur->wNext is passable
//		if( pMap->GetPassTimeCoord( wCurX, wCurY, wNextX, wNextY, bDiagonal ) > 0 )
//		{
                // is passable
//			wCurX = wNextX;
//			wCurY = wNextY;
//			SRouteUnit *pRU = pUnit->route.GetRouteUnit();
//			pRU->wX = wCurX;
//			pRU->wY = wCurY;
//			pUnit->route.route.AddHead( pRU );
//		}
//		else
//		{
//		// is impassable
//			// Find turn way and put it in the path
//			POSITION posNew = FindFormationObjectTurnWay( wCurX, wCurY, wNextX, wNextY,
//				nOffsetX, nOffsetY,
//				pUnit, pRTCenterPath, pos, LEFT, AI_N_TURNWAY_TILE_LIMIT, pMap );
//			if ( posNew == NULL  )
//			{
//				posNew = FindFormationObjectTurnWay( wCurX, wCurY, wNextX, wNextY,
//					nOffsetX, nOffsetY,
//					pUnit, pRTCenterPath, pos, RIGHT, AI_N_TURNWAY_TILE_LIMIT,
pMap );
//				assert( posNew != NULL );
//			}
//			pos = posNew;
//			// We must set new values of wCur and wNext
//			SRouteUnit *pRUU = pUnit->route.route.GetHead(); // last save tile
//			wCurX = pRUU->wX;
//			wCurY = pRUU->wY;
//		}


        return true;
}

WORD CAIGraph::GetNextCoord( WORD wCenterX, WORD wCenterY,
        int nOffsetX, int nOffsetY, float fpAngle )
{
        int nNextX = -1,
                nNextY = -1;

#ifndef AI_TURN_FORMATION_WITH_FACE

        nNextX = wCenterX + nOffsetX;
        nNextY = wCenterY + nOffsetY;

#else
        // Retrieve the next position of the unit acoounting that all the formation must
        // turn with face forward

        // All angles are applied to the axis 0X from down ( as my down is really up )
        // wNextX = X0*cos( gamma ) - Y0*sin( gamma )
        // wNextY = X0*sin( gamma ) + Y0*cos( gamma )
        // fpAngle is in radian
        double fpCosAngle = cos( fpAngle ),
                fpSinAngle = sin( fpAngle );

        // gamma = angle - iniAngle
        nNextX = int( double(wCenterX) +
                double(nOffsetX) * fpCosAngle - double(nOffsetY) * fpSinAngle );
        nNextY = int( float(wCenterY) +
                double(nOffsetX) * fpSinAngle + double(nOffsetY) * fpCosAngle );
#endif

        // allign at lower bounds
        nNextX = ( nNextX >= 0 )? nNextX: 0;
        nNextY = ( nNextY >= 0 )? nNextY: 0;

        // allign at higher bound
        nNextX = ( nNextX < nSizeX )? nNextX: nSizeX - 1;
        nNextY = ( nNextY < nSizeY )? nNextY: nSizeY - 1;

        return GetIndex( nNextX, nNextY );


}

POSITION CAIGraph::FindFormationObjectTurnWay( WORD wCurX, WORD wCurY,
        WORD wNextX, WORD wNextY, int nOffsetX, int nOffsetY,
        SAIFormationUnit *pUnit, const CAIRoute *pPath,
        POSITION posPath, EAIDirection dirPassBy,
        int nTilesLimit, const CAIMap *pMap )
{
        // *-*-*-*-*-*-*-* initialization *-*-*-*-*-*-*-*
        int nCurTile = GetIndex( wCurX, wCurY );  // Current passable tile
        int nPrevTile = nCurTile;           // previous passable tile
        bool bFound = false;                // if we found a good tile

        // test this tile to be in boundaries *-*-*-*-*-*-*-*
        if( TestBounds( nCurTile, dirPassBy ) )
        {   // the search is meaningless
                // there is a boundary in the direction of passing by
                //*pNFoundTile = nCurTile;
                return false;
        }

        // Set previous direction of a tile line basing on CurTile and NextTile
        EAIDirection dirTravel = NULL_DIR;
        // Get the direction of CurTile -> NextTile
        dirTravel = GetDirection( wCurX, wCurY, wNextX, wNextY );

        // Set direction of rotation
        EAIDirection dirRotate = dirPassBy;

        // loop of finding the path *-*-*-*-*-*-*-*
        // it will end if:
        // 1) we return to our path
        // 2) we will not find the path during the limit number of tiles
        CAIRoute localRoute;
        int nTilesAdded = 0;
        do {
                //	1) define available direction to search *-*-*-*-*-*-*-*
                // down-left-up-right-down- clockwise if pass by left or pass from down
                // counterclockwise - otherwise
                bool bFoundDirection = false;
                int nDirCount = 0;
                do
                {
                        if( nDirCount++ > NUMBER_OF_DIRECTIONS ) 	  // the tile is locked from
all dirs
                        {
                                return false;
                        }

                        // test this tile to be in boundaries
                        if( TestBounds( nCurTile, dirTravel ) )
                        {
                                dirTravel = Rotate( dirTravel, dirRotate );  // rotate direction
                                continue;
                        }

                        // if we can't pass the next tile, then rotate direction
                        if( pMap->TilesCanPass( nCurTile, MoveTile( nCurTile, dirTravel ) ) == false
)
//			if( tileMap[ nCurTile + 1 ].bClosed )
                                dirTravel = Rotate( dirTravel, dirRotate );
                        else {
                        // if we can pass the next tile, move current tile to it
                                nCurTile = MoveTile( nCurTile, dirTravel );
                                bFoundDirection = true;
                        }

                        //if( bFoundDirection ) {
                        //	nNewCurTile = nCurTile;
                        //	pCurTile = &(tileMap[nCurTile]);
                        //}

                } while ( !bFoundDirection );

                // *-*-*-*-*-*-*-* The tile we move to is found *-*-*-*-*-*-*-*

                // Save tile in the route
                SRouteUnit *pRU = localRoute.GetRouteUnit();
#ifdef __AI_ROUTE_DEBUG
nGlblRouteUsed ++;
#endif
                pRU->wX = GetX( nCurTile );
                pRU->wY = GetY( nCurTile );
                localRoute.route.AddHead( pRU );
                //nPrevTile = nCurTile;

                // try to turn direction
                // not depending went we down or right we can pass by the obstacle
                // clockwise (LEFT) or counterclockwise (RIGHT)
                dirTravel = Rotate( dirTravel, ( dirRotate == LEFT ) ? RIGHT : LEFT );

                // test if we can stop the process *-*-*-*-*-*-*-*
                // 1) if we have returned to th tile line
                // 2) if Limit of tiles is exousted
                POSITION pos = FindTileInLine( pPath, posPath, nOffsetX, nOffsetY,
                        nTilesLimit, nCurTile );
                if( pos != NULL )
                {
                // yeahh, we found it
                        // copy local route to pUnit route

                        POSITION posLocal = localRoute.route.GetTailPosition();
                        while ( posLocal )
                        {
                                SRouteUnit *pRU = localRoute.route.GetPrev( posLocal ),
                                        *pRUU = pUnit->route.GetRouteUnit();
#ifdef __AI_ROUTE_DEBUG
nGlblRouteUsed ++;
#endif
                                pRUU->wX = pRU->wX;
                                pRUU->wY = pRU->wY;
                                pUnit->route.route.AddHead( pRUU );
                        }

                        localRoute.Clear();
                        return pos;
                }

                if( ++nTilesAdded >= nTilesLimit )
                {
                // oh, we haven't found
                        localRoute.Clear();
                        return NULL;
                }

                // we are looking further *-*-*-*-*-*-*-*
        } while( !bFound );

        localRoute.Clear();

        return NULL;
}

// Tries to find a tile in the route on given maximum number of tiles ahead
POSITION CAIGraph::FindTileInLine( const CAIRoute *pPath, POSITION posPath,
        int nOffsetX, int nOffsetY,
        int nLimit, int nTile )
{
        POSITION pos = posPath;

        for ( int i = 0; i < nLimit, pos != NULL; i++ )
        {
                SRouteUnit *pRU = pPath->route.GetPrev( pos );
                if( pRU->wX + nOffsetX == GetX( nTile ) &&
                        pRU->wY + nOffsetY == GetY( nTile ) )
                {
                // we have found tile in the path
                        return pos;
                }
        }

        return NULL;
}


*/