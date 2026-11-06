#pragma once
#include "SaveSchema.h"
/////////////////////////////////////////////////////////////////////////////////////
#include "Plot.h"
#include "spline.h"
#include "plot.h"
#include "aiPathFinder.h"
#include "SaveLoader.h"

/**********************************************************************
 *<
        FILE: aiRoute.h

        DESCRIPTION:  Route of unit

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

/////////////////////////////////////////////////////////////////////////////////////
class CUpdateHistory;
class CWorldBase;
class CUnit;

/////////////////////////////////////////////////////////////////////////////////////
const short N_RT_WATER_FLAG = 1;
const short N_RT_NORUN = 2;
const WORD N_RT_SPEED = 512;
struct SRouteUnit
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    union
    {
        struct
        {
            WORD wX, wY; // may become floats someday...
        };
        struct
        {
            short nX, nY;
        };
    };
    CPlot2D point = CPlot2D(0,0); // exact waypoint, in the existing route units
    WORD terrain = 1; // Detour material area, independent of legacy object cells
    float supportZ = 0; // world support height, never command distance
    WORD wSpeed256 = N_RT_SPEED; // *N_RT_SPEED base speed multiplier
    WORD wFlags = 0;    // флаги того, что юнит идет по мосту, по воде, etc

    SRouteUnit()
    {
        wX = 0;
        wY = 0;
    }
    SRouteUnit(WORD wNx, WORD wNy)
    {
        wX = wNx;
        wY = wNy;
        point=CPlot2D(float(wNx)+.5f,float(wNy)+.5f);
    }
    SRouteUnit Move(EAIDirection dir);
    void SetFlags(BOOL bWater, BOOL bNoRun)
    {
        wFlags = bWater * N_RT_WATER_FLAG + bNoRun * N_RT_NORUN;
    }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};
typedef CList<SRouteUnit> CRouteList;

inline EAIDirection GetRouteDirection(SRouteUnit &src, SRouteUnit &dst)
{
    return AINumberToDir[(dst.nY - src.nY) * 3 + (dst.nX - src.nX) + 4];
}

inline int Is8Connected(const SRouteUnit &a, const SRouteUnit &b)
{
    return abs(int(a.wX) - int(b.wX)) <= 1 && abs(int(a.wY) - int(b.wY)) <= 1;
}
/////////////////////////////////////////////////////////////////////////////////////
// тайловый маршрут
class CAIRoute
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    CRouteList route;

public:
    CAIRoute()
        : route(128)
    {
    }
    void Clear() { route.RemoveAll(); }
    void Copy(const CAIRoute &src);
    CAIRoute(const CAIRoute &a)
        : route(128)
    {
        Copy(a);
    }
    CAIRoute &operator=(const CAIRoute &a)
    {
        Clear();
        Copy(a);
        return *this;
    }
    CAIRoute &operator+=(const CAIRoute &a)
    {
        Copy(a);
        return *this;
    }
    CAIRoute Invert();
    void EraseDuplicates();
    void Turn(float pivotX,float pivotY,float radians);
    BOOL Is8Connected();
    void AddHeadUnit(WORD wX, WORD wY) { route.AddHead(SRouteUnit(wX, wY)); }
    void AddTailUnit(WORD wX, WORD wY) { route.AddTail(SRouteUnit(wX, wY)); }
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};

/////////////////////////////////////////////////////////////////////////////////////
// описание маршрута дл€ передачи по сети и выполнени€ CRouteWalker`om, помимо маршрута
// включает в себ€ точные начальное и конечное положени€, а так же начальный угол поворота юнита
// также используетс€ дл€ описани€ вырожденного движени€ - поворота, в этом случае route не
// содержит узлов, а конечное направление содержитс€ в finish
class CAIFullRoute
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
public:
    enum EMovement
    {
        Stand,
        Rotate,
        Walk,
        Attack,
        AttackPoint
    };

private:
    EMovement moveType;
    CPlot2D start;
    float fpStartAngle;
    int nStartSegment;
    union
    {
        struct
        {
            float fpFinishX, fpFinishY;
        } walk;
        struct
        {
            float fpFinalAngle;
            float fpAngleSpeed;
        } rotate;
        struct
        {
            int nTargetID;
            int nHitSegment;
            int nTurnFinishSegment;
        } attack;
        struct
        {
            float fpFinalAngle;
            int nHitSegment;
            int nTurnFinishSegment;
        } attack1;
    };

public:
    CAIRoute route;

    int GetStartSegment() const { return nStartSegment; }
    EMovement GetMoveType() const { return moveType; }
    BOOL IsMoving() const { return moveType == Walk; }
    //
    bool SetStartPos(const CPlot3D &pos, const CPlot3D &vec, int _nStartSegment);
    void MakeRotation(const CPlot3D &dstVec, float fpAngSpeed = 0);
    void MakeAttackMotion(int _nTargetID, int _nHitSegment, int _nTurnFinish);
    void MakeAttackPointMotion(const CPlot3D &dstVec, int _nHitSegment, int _nTurnFinish);
    void MakeMovement(float _fpFinishX, float _fpFinishY);
    void DoNotTurnToAttack(int nWhomID);
    void Freeze(int nStep);
    void UpdateTo(CUpdateHistory &pkt);
    void UpdateFrom(CUpdateHistory &pkt);
    void Clear();
    void SetSpeed(WORD wSpeed256 = N_RT_SPEED);
    void Turn(float pivotX,float pivotY,float radians);
    void Face(float heading);
    CPlot3D GetFinalPoint(); // result is in meters
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
    friend class CRouteWalker;
};

/////////////////////////////////////////////////////////////////////////////////////
// Walker &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
inline float Distance(CPlot2D &a, CPlot2D &b)
{
    return float(sqrt(sqr(a.x - b.x) + sqr(a.y - b.y)));
}

/////////////////////////////////////////////////////////////////////////////////////
// один сегмент маршрута
struct SRouteSegment
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    CPlot2D start, startV;
    CPlot2D finish, finishV;
    float fpAlpha,
        fpAlphaInc; // alpha - maximal alpha for the segment, alphainc - alpha increment per segment
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};

/////////////////////////////////////////////////////////////////////////////////////
// waypoint маршрута ( должны сто€ть на каждом тайле вдоль маршрута, сам путь проходит от одного
// waypoint к другому )
struct SWalkerWaypoint
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    CPlot2D point, speed; // point in ai tiles, speed is an ort
    float fpSpeed;        // speed in ai tiles per second
    int nTileX, nTileY;   // тайл, который должен быть свободен, при перемещении юнита к этой точке
    float GetAngle() { return float(atan2(speed.y, speed.x)); }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};

// represents routes turning point
struct SWalkerTempPoint
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
    CPlot2D point; // point in ai tiles
    int nNumber;   // number of tiles, following this point to the next turning point
    SWalkerTempPoint() {}
    SWalkerTempPoint(float fpX, float fpY)
    {
        point.x = fpX;
        point.y = fpY;
        nNumber = 0;
    }
    void Load(CSave &) = delete;
    void Store(CSave &) = delete;
};

/////////////////////////////////////////////////////////////////////////////////////
// используетс€ дл€ ускорени€ повтор€ющихс€ запросов
struct SRouteSegmentCache
{
    SRouteSegment segment;
    SPolynom3 polyX, polyY;
    float fpStartTime, fpFinishTime;
    int nSegment;

    void Clear() { nSegment = -1; }
    void CalcPolys();
    void GetPos(float fpTime, CPlot3D &pos, CPlot3D &vec, float &fpSpeed);
    bool IsValid(float fpTime) const
    {
        return nSegment != -1 && fpTime >= fpStartTime && fpTime <= fpFinishTime;
    }
    bool IsValid() const { return nSegment != -1; }
    SRouteSegmentCache() { Clear(); }
};
/////////////////////////////////////////////////////////////////////////////////////
// класс, осуществл€ющий перемещение по маршруту
class CRouteWalker
{
public:
    void Load(SaveSchema::Reader &);
    void Store(SaveSchema::Writer &) const;
private:
    CAArray<SWalkerWaypoint> points;
    CAArray<SWalkerTempPoint> tpoints;
    CAArray<float, float> speeds;
    float fpBaseSpeed; // units characteristics
    float fpAngularSpeed;
    float fpActiveAngularSpeed = 0; // реальна€ углова€ скорость поворота, используетс€ дл€ медленных
                                // поворотов
    float fpStartAngle = 0, fpDeltaAngle = 0;
    SRouteSegmentCache cache;

    void Clear();
    void MarkPoint(float fpX, float fpY) { tpoints.Add(SWalkerTempPoint(fpX, fpY)); }
    void AddSpeed(float fpSpeed)
    {
        ASSERT(tpoints.GetSize() > 0);
        tpoints[tpoints.GetSize() - 1].nNumber++;
        speeds.Add(fpSpeed);
    }
    void MakeSegment(CPlot2D &start, CPlot2D &finish, int nCount, float fpStartMargin,
                     float fpFinishMargin, int &nSpeedsCounter);
    void LimitAngularSpeed(float fpMaxAngSpeed);
    void CreateWaypointList();
    // set tiles, locks for which is required for every point on the path
    void SetCheckTiles(CAIFullRoute &route);
    void StartRotation(float fpX, float fpY, float fpAngSpeed = 0);
    bool GetRotation(float fpTime, CPlot3D &vec);
    bool GetMovement(float fpTime, bool &bRotating, CPlot3D &pos, CPlot3D &vec, float &fpSpeed,
                     int &nTileX, int &nTileY, const CAIRoute &support, float *supportZ);
    void GetSegment(int N, SRouteSegment &segment)
    {
        segment.start = points[N].point;
        segment.startV = points[N].speed;
        segment.finish = points[N + 1].point;
        segment.finishV = points[N + 1].speed;
        segment.fpAlpha = Distance(segment.start, segment.finish);
        segment.fpAlphaInc = points[N].fpSpeed;
    }
    int GetSegmentsNumber() { return points.GetSize() - 1; }

public:
    CRouteWalker()
    {
        Clear();
        fpBaseSpeed = 0.1f;
        fpAngularSpeed = 0.1f;
    }
    void CreateWalkerRoute(CAIFullRoute &route);
    void RestartAt(CAIFullRoute& route,CUnit* unit,int tick);
    // time in segments, speed in tiles per segment, nTileX, nTileY - tile which should be free for
    // this motion nTileX == -1 if no lock check is required return FALSE in case of
    // movement/rotation if action is over
    bool GetPosition(CWorldBase *pWorld, CAIFullRoute &route, float fpTime, bool &bRotating,
                     CPlot3D &pos, CPlot3D &vec, float &fpSpeed, int &nTileX, int &nTileY, float *supportZ = nullptr);
    // новые скорости перемещени€, если при этом не перерасчитать путь, то юнит переместитс€ скачком
    void SetSpeeds(float fpMoveSpeed, float fpNewAngSpeed)
    {
        cache.Clear();
        fpBaseSpeed = fpMoveSpeed;
        fpAngularSpeed = fpNewAngSpeed;
    }
    // load&store
    void Load(CSave &file) = delete;
    void Store(CSave &file) = delete;
};
