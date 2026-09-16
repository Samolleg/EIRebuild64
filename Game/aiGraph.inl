#ifndef __AI_GRAPH_INL_
#define __AI_GRAPH_INL_
/**********************************************************************
 *<
        FILE: AIGraph.INL

        DESCRIPTION:  Inline functions for pathfinding

        CREATED BY: Mikhail Semyonov

 *>	Copyright (c) 1999, Nival Enterntainemnt
 **********************************************************************/
#include "tools.h"
#include "mapFormat.h"
/////////////////////////////////////////////////////////////////////////////////////
// Directions
enum EAIDirection
{
    NULL_DIR = 0,
    DOWN,
    DOWNLEFT,
    LEFT,
    UPLEFT,
    UP,
    UPRIGHT,
    RIGHT,
    DOWNRIGHT
};

/////////////////////////////////////////////////////////////////////////////////////
// possible kinds of locks
enum EAILock
{
    AI_LOCK_NO,
    AI_LOCK_STATIC,
    AI_LOCK_DYNAMIC
};

///////////////////////////////////////////////////////////////////
// some list types
class CMapObject;
class CUnit;
typedef CList<CMapObject *, CMapObject *> CMapObjectList;
typedef CList<CUnit *, CUnit *> CUnitList;

/////////////////////////////////////////////////////////////////////////////////////
// количество видов юнитов, во многих местах уже прописано hard 8, look aimap/aipathfinder
const int N_AI_UNIT_KINDS = 8;
// размер клетки для графа, зависит от максимального поля поиска для CWavePathFinder
const int N_AI_GRAPH_CELL_SIZE = 8;

const int AI_N_OM_CELL_SIZE = 32; // Size of mapObjects cell

//////////// float coordinate to AI tile coordinate ////////////
inline int Float2AITile(float fpCoord)
{
    return Float2Int(fpCoord * (1 / (FP_CL_MP_SIZE / 2)) - 0.5f);
}

inline float Meter2AITile(float fpCoord)
{
    return fpCoord * (1 / (FP_CL_MP_SIZE / 2));
}

inline float AITile2Meter(float fpCoord)
{
    return fpCoord * (FP_CL_MP_SIZE / 2);
}
#endif
