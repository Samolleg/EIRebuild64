#include "StdAfx.h"
#include "SaveSchema.h"
static const char LOCAL_FILE[] = __FILE__;
/////////////////////////////////////////////////////////////////////////////////////
#include "aiRoute.h"
#include "mapFormat.h"
#include "spline.h"

#include "NetUpdate.h"
#include "GameBase.h"
#include "World.h"
#include "WorldServer.h"
#include "Unit.h"
// #include "console.h"
/**********************************************************************
 *<
        FILE: aiRoute.cpp

        DESCRIPTION:  Route of unit implementation

        CREATED BY: Ivanov Evgeny

 *>	Copyright (c) 1998, Nival Enterntainemnt
 **********************************************************************/

/////////////////////////////////////////////////////////////////////////////////////
#ifndef _DEBUG
#pragma optimize("agw", on)
#endif
/////////////////////////////////////////////////////////////////////////////////////
// SRouteUnit &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
SRouteUnit SRouteUnit::Move(EAIDirection dir)
{
    switch (dir)
    {
    case DOWNLEFT:
        return SRouteUnit(wX - 1, wY - 1);
    case DOWN:
        return SRouteUnit(wX, wY - 1);
    case DOWNRIGHT:
        return SRouteUnit(wX + 1, wY - 1);
    case RIGHT:
        return SRouteUnit(wX + 1, wY);
    case LEFT:
        return SRouteUnit(wX - 1, wY);
    case UPLEFT:
        return SRouteUnit(wX - 1, wY + 1);
    case UP:
        return SRouteUnit(wX, wY + 1);
    case UPRIGHT:
        return SRouteUnit(wX + 1, wY + 1);
    }
    ASSERT("Wrong dir" && FALSE);
    return *this;
}

/////////////////////////////////////////////////////////////////////////////////////
// CAIRoute &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
void CAIRoute::Copy(const CAIRoute &src)
{
    POSITION pos = src.route.GetHeadPosition();
    while (pos)
    {
        route.AddTail(src.route.GetNext(pos));
    }
}

/////////////////////////////////////////////////////////////////////////////////////
CAIRoute CAIRoute::Invert()
{
    CAIRoute res;
    POSITION pos = route.GetHeadPosition();
    while (pos)
    {
        res.route.AddHead(route.GetNext(pos));
    }
    return res;
}

/////////////////////////////////////////////////////////////////////////////////////
void CAIRoute::EraseDuplicates()
{
    WORD wPrevX = 0xffff, wPrevY = 0xffff;
    POSITION pos = route.GetHeadPosition(), prevPos;
    while (pos)
    {
        BOOL bErase;
        prevPos = pos;
        SRouteUnit &r = route.GetNext(pos);
        bErase = r.wX == wPrevX && r.wY == wPrevY;
        wPrevX = r.wX;
        wPrevY = r.wY;
        if (bErase)
            route.RemoveAt(prevPos);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
BOOL CAIRoute::Is8Connected()
{
    POSITION pos = route.GetHeadPosition();
    while (pos)
    {
        SRouteUnit &r1 = route.GetNext(pos);
        if (!pos)
            break;
        SRouteUnit &r2 = route.GetAt(pos);
        if (abs(int(r1.wX) - int(r2.wX)) > 1 || abs(int(r1.wY) - int(r2.wY)) > 1)
            return FALSE;
    }
    return TRUE;
}
/////////////////////////////////////////////////////////////////////////////////////
// SRouteSegmentCache &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
void SRouteSegmentCache::CalcPolys()
{
    polyX.Spline22(segment.start.x, segment.startV.x, segment.finish.x, segment.finishV.x,
                   segment.fpAlpha);
    polyY.Spline22(segment.start.y, segment.startV.y, segment.finish.y, segment.finishV.y,
                   segment.fpAlpha);
}
/////////////////////////////////////////////////////////////////////////////////////
void SRouteSegmentCache::GetPos(float fpTime, CPlot3D &pos, CPlot3D &vec, float &fpSpeed)
{
    fpSpeed = segment.fpAlphaInc;
    // float fpAlpha = segment.fpAlpha - ( fpTime - fpStartTime ) * segment.fpAlphaInc;
    float fpAlpha = (fpTime - fpStartTime) * segment.fpAlphaInc;
    pos.x = polyX.Calc(fpAlpha);
    vec.x = polyX.CalcDer(fpAlpha);
    pos.y = polyY.Calc(fpAlpha);
    vec.y = polyY.CalcDer(fpAlpha);
}
/////////////////////////////////////////////////////////////////////////////////////
// CRouteWalker &&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&&
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::Clear()
{
    cache.Clear();
    points.Clear();  // SetSize( 0 );
    tpoints.Clear(); // SetSize( 0 );
    speeds.Clear();  // SetSize( 0 );
}
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::LimitAngularSpeed(float fpMaxAngSpeed)
{
    for (int i = 0; i < points.GetSize() - 1; i++)
    {
        float fpAng1, fpAng2, fpAngDif, fpTime, fpDist, fpTimeAng;
        fpAng1 = points[i].GetAngle();
        fpAng2 = points[i + 1].GetAngle();
        fpAngDif = fabs(fpAng1 - fpAng2);
        while (fpAngDif > 2 * FP_PI)
            fpAngDif -= 2 * FP_PI;
        if (fpAngDif > FP_PI)
            fpAngDif = 2 * FP_PI - fpAngDif;
        fpDist = Distance(points[i].point, points[i + 1].point);
        fpTime = fpDist / points[i].fpSpeed;
        fpTimeAng = fpAngDif / fpMaxAngSpeed;
        if (fpTimeAng > fpTime)
            points[i].fpSpeed = fpDist / fpTimeAng;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::MakeSegment(CPlot2D &start, CPlot2D &finish, int nCount, float fpStartMargin,
                               float fpFinishMargin, int &nSpeedsCounter)
{
    int i;
    SWalkerWaypoint a;

    float fpLeng = Distance(start, finish);
    if (fpLeng == 0)
        fpLeng = 0.00001f;
    a.speed.x = (finish.x - start.x) / fpLeng;
    a.speed.y = (finish.y - start.y) / fpLeng;
    a.nTileX = -1;
    a.nTileY = -1;
    if (fpStartMargin != 0 && fpFinishMargin != 0 && nCount == 1)
    {
        a.point = start;
        a.fpSpeed = fpBaseSpeed * speeds[nSpeedsCounter] / N_RT_SPEED;
        points.Add(a);
        a.point = finish;
        points.Add(a);
        nSpeedsCounter++;
        return;
    }
    // начальные и конечные интервалы считаются по 0.5
    float fpLen1 = nCount + fpStartMargin - fpFinishMargin;
    ASSERT(fpLen1 != 0);
    fpLen1 = 1 / fpLen1;
    for (i = 0; i < nCount; i++)
    {
        float fpPart = 0.5f + i + fpStartMargin;
        // if ( i == 0 )
        // fpPart += fpStartMargin;
        // if ( i == nCount - 1 )
        // fpPart += fpFinishMargin;
        fpPart *= fpLen1;
        a.point.x = start.x * (1 - fpPart) + finish.x * fpPart;
        a.point.y = start.y * (1 - fpPart) + finish.y * fpPart;
        a.fpSpeed = fpBaseSpeed * speeds[nSpeedsCounter] / N_RT_SPEED;
        points.Add(a);
        nSpeedsCounter++;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::CreateWaypointList()
{
    int nSpeedsCounter = 0;
    for (int i = 0; i < tpoints.GetSize() - 1; i++)
    {
        float fpStart = 0, fpFinish = 0;
        if (i == 0)
            fpStart = -0.5f;
        if (i == tpoints.GetSize() - 2)
            fpFinish = 0.5f;
        MakeSegment(tpoints[i].point, tpoints[i + 1].point, tpoints[i].nNumber, fpStart, fpFinish,
                    nSpeedsCounter);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::SetCheckTiles(CAIFullRoute &route)
{
    POSITION pos;
    CAIRoute &r = route.route;
    int nCurrentPoint = 0;
    ASSERT((r.route.GetCount() == 1 && points.GetSize() == 2) ||
           points.GetSize() == r.route.GetCount());
    pos = r.route.GetHeadPosition();
    while (pos)
    {
        SRouteUnit &cur = r.route.GetNext(pos);
        if (nCurrentPoint >= points.GetSize())
            break;
        // ASSERT( cur.nX == Float2Int( points[ nCurrentPoint ].point.x )
        //&& cur.nY == Float2Int( points[ nCurrentPoint ].point.y ) );
        points[nCurrentPoint].nTileX = cur.nX;
        points[nCurrentPoint].nTileY = cur.nY;
        nCurrentPoint++;
    }
    if (r.route.GetCount() == 1)
    {
        points[points.GetSize() - 1].nTileX = r.route.GetHead().nX;
        points[points.GetSize() - 1].nTileY = r.route.GetHead().nY;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::CreateWalkerRoute(CAIFullRoute &route)
{
    POSITION pos, fpos;
    Clear();
    CAIRoute &r = route.route;

    fpStartAngle = route.fpStartAngle;
    switch (route.GetMoveType())
    {
    case CAIFullRoute::Stand:
    case CAIFullRoute::Attack:
        return;
    case CAIFullRoute::AttackPoint:
        StartRotation((float)cos(route.attack1.fpFinalAngle),
                      (float)sin(route.attack1.fpFinalAngle), 0);
        return;
    case CAIFullRoute::Rotate:
        StartRotation((float)cos(route.rotate.fpFinalAngle), (float)sin(route.rotate.fpFinalAngle),
                      route.rotate.fpAngleSpeed);
        return;
    case CAIFullRoute::Walk:
        break;
    default:
        ASSERT(0 && "Unknown route type");
        break;
    };
    // normal movement
    EAIDirection dir, prevDir = NULL_DIR;
    MarkPoint(route.start.x, route.start.y);
    for (pos = r.route.GetHeadPosition(); pos;)
    {
        SRouteUnit &cur = r.route.GetNext(pos);
        AddSpeed(cur.wSpeed256);
        if (!pos)
            break;
        SRouteUnit &next = r.route.GetAt(pos);
        fpos = pos;
        r.route.GetNext(fpos);
        dir = GetRouteDirection(cur, next);
        switch (dir)
        {
        case UPLEFT:
            MarkPoint(cur.nX, cur.nY + 1.0f);
            break;
        case UPRIGHT:
            MarkPoint(cur.nX + 1.0f, cur.nY + 1.0f);
            break;
        case DOWNLEFT:
            MarkPoint(cur.nX, cur.nY);
            break;
        case DOWNRIGHT:
            MarkPoint(cur.nX + 1.0f, cur.nY);
            break;
        }
        if ((prevDir != NULL_DIR && DirectionDifference(dir, prevDir) > 1) ||
            (fpos && DirectionDifference(dir, GetRouteDirection(next, r.route.GetAt(fpos))) > 1))
        {
            switch (dir)
            {
            case LEFT:
                MarkPoint(cur.nX, cur.nY + 0.5f);
                break;
            case RIGHT:
                MarkPoint(cur.nX + 1.0f, cur.nY + 0.5f);
                break;
            case DOWN:
                MarkPoint(cur.nX + 0.5f, cur.nY);
                break;
            case UP:
                MarkPoint(cur.nX + 0.5f, cur.nY + 1.0f);
                break;
            }
        }
        prevDir = dir;
    }
    MarkPoint(route.walk.fpFinishX, route.walk.fpFinishY);
    CreateWaypointList();
    LimitAngularSpeed(fpAngularSpeed);
    // skip starting rotation if unit is facing right direction
    if (cos(atan2(points[0].speed.y, points[0].speed.x) - fpStartAngle) > 0.9)
    {
        fpDeltaAngle = 0;
        points[0].speed.x = (float)cos(fpStartAngle);
        points[0].speed.y = (float)sin(fpStartAngle);
    }
    else
        StartRotation(points[0].speed.x, points[0].speed.y);
    SetCheckTiles(route);
}
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::StartRotation(float fpX, float fpY, float fpAngSpeed)
{
    float fpAngle;
    if (fpX == 0 && fpY == 0)
    {
        fpDeltaAngle = 0;
        return;
    }
    // если задана угловая скорость, то использовать ее, иначе скорость по умолчанию
    if (fpAngSpeed)
        fpActiveAngularSpeed = fpAngSpeed;
    else
        fpActiveAngularSpeed = fpAngularSpeed;
    fpAngle = float(atan2(fpY, fpX));
    fpDeltaAngle = fpAngle - fpStartAngle;
    while (fpDeltaAngle > FP_PI)
        fpDeltaAngle -= 2 * FP_PI;
    while (fpDeltaAngle < -FP_PI)
        fpDeltaAngle += 2 * FP_PI;
}
/////////////////////////////////////////////////////////////////////////////////////
// получить направление поворота по прошествии fpTime от начала
bool CRouteWalker::GetRotation(float fpTime, CPlot3D &vec)
{
    bool bRv;
    float fpAngle, fpCurTime;
    fpCurTime = fabs(fpDeltaAngle / fpActiveAngularSpeed); // time to rotate
    if (fpTime <= fpCurTime)
    {
        bRv = true;
        fpAngle = fpStartAngle + fpTime * fpActiveAngularSpeed * Sign(fpDeltaAngle);
    }
    else
    {
        bRv = false;
        fpAngle = fpStartAngle + fpDeltaAngle;
    }
    vec.x = float(cos(fpAngle));
    vec.y = float(sin(fpAngle));
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
// получить направление поворота и, возможно, позицию юнита через fpTime после начала
// движения
bool CRouteWalker::GetMovement(float fpTime, bool &bRotating, CPlot3D &pos, CPlot3D &vec,
                               float &fpSpeed, int &nTileX, int &nTileY)
{
    // check for cache
    if (cache.IsValid(fpTime))
    {
        bRotating = false;
        cache.GetPos(fpTime, pos, vec, fpSpeed);
        nTileX = points[cache.nSegment + 1].nTileX;
        nTileY = points[cache.nSegment + 1].nTileY;
        return true;
    }
    //
    float fpCurTime;
    int nSegment;
    if (!cache.IsValid() || cache.fpStartTime > fpTime)
    {
        bRotating = true;
        // первая фаза - поворот
        fpActiveAngularSpeed = fpAngularSpeed;
        fpCurTime = fabs(fpDeltaAngle / fpActiveAngularSpeed); // time to rotate
        if (fpTime <= fpCurTime)
        {
            GetRotation(fpTime, vec);
            return true;
        }
        nSegment = 0;
    }
    else
    {
        fpCurTime = cache.fpStartTime;
        nSegment = cache.nSegment;
    }
    // если поворот уже произошел, то ищем сегмент на котором находится юнит и возвращаем информацию
    // по этому сегменту
    bRotating = false;
    float fpSaveCache = cache.fpStartTime;
    for (int i = nSegment; i < GetSegmentsNumber(); i++)
    {
        cache.fpStartTime = fpCurTime;
        SRouteSegment &segment = cache.segment;

        GetSegment(i, segment);
        ASSERT(cache.segment.fpAlphaInc > 0.0001);
        fpCurTime += segment.fpAlpha / segment.fpAlphaInc;
        if (fpTime <= fpCurTime)
        {
            // interval fits, fill in cache
            cache.CalcPolys();
            cache.fpFinishTime = fpCurTime;
            cache.nSegment = i;
            //
            cache.GetPos(fpTime, pos, vec, fpSpeed);
            nTileX = points[cache.nSegment + 1].nTileX;
            nTileY = points[cache.nSegment + 1].nTileY;
            return true;
        }
    }
    cache.fpStartTime = fpSaveCache;
    // юнит оказывается прошел весь маршрут, вернем последнюю точку маршрута
    fpSpeed = 0;
    pos.x = points[points.GetSize() - 1].point.x;
    pos.y = points[points.GetSize() - 1].point.y;
    vec.x = points[points.GetSize() - 1].speed.x;
    vec.y = points[points.GetSize() - 1].speed.y;
    vec.z = 0;
    nTileX = points[points.GetSize() - 1].nTileX;
    nTileY = points[points.GetSize() - 1].nTileY;
    return false;
}
/////////////////////////////////////////////////////////////////////////////////////
// NOTE, can be speed up immensely by caching...
bool CRouteWalker::GetPosition(CWorldBase *pWorld, CAIFullRoute &route, float fpTime,
                               bool &bRotating, CPlot3D &pos, CPlot3D &vec, float &fpSpeed,
                               int &nTileX, int &nTileY)
{
    bool bRv = true;
    nTileX = -1;
    nTileY = -1;
    fpSpeed = 0.1f;
    vec.z = 0;
    bRotating = false;
    if (fpTime < route.nStartSegment)
        fpTime = route.nStartSegment;
    pos.x = route.start.x;
    pos.y = route.start.y;
    switch (route.GetMoveType())
    {
    case CAIFullRoute::Stand:
        vec.x = float(cos(route.fpStartAngle));
        vec.y = float(sin(route.fpStartAngle));
        break;
    case CAIFullRoute::Rotate:
        bRotating = true;
        bRv = GetRotation(fpTime - route.nStartSegment, vec);
        // NConsole::game << "time = " << fpTime - route.nStartSegment << "  vec = " << vec.x << ";"
        // << vec.y << endl;
        break;
    case CAIFullRoute::Walk:
        bRv = GetMovement(fpTime - route.nStartSegment, bRotating, pos, vec, fpSpeed, nTileX,
                          nTileY);
        break;
    case CAIFullRoute::AttackPoint:
        bRotating = GetRotation(fpTime - route.nStartSegment, vec);
        break;
    case CAIFullRoute::Attack: {
        CMapObject *pTarget = pWorld->GetMapObject(route.attack.nTargetID);
        if (pTarget)
        {
            CPlot3D ptTarget;
            if (fpTime > route.attack.nHitSegment)
                ptTarget = pTarget->GetTargetCP();
            else
                ptTarget = pTarget->GetFuturePosition(route.attack.nHitSegment);
            ptTarget.x = Meter2AITile(ptTarget.x);
            ptTarget.y = Meter2AITile(ptTarget.y);
            if (fpTime < route.attack.nTurnFinishSegment &&
                route.attack.nTurnFinishSegment != route.nStartSegment)
            {
                // turn to target so when time will come unit will face the target
                float fpNeedAngle = (float)atan2(ptTarget.y - route.start.y,
                                                 ptTarget.x - route.start.x);
                float fpDAngle = fpNeedAngle - route.fpStartAngle;
                while (fpDAngle > FP_PI)
                    fpDAngle -= 2 * FP_PI;
                while (fpDAngle < -FP_PI)
                    fpDAngle += 2 * FP_PI;
                float fpAngle = route.fpStartAngle +
                                fpDAngle * (fpTime - route.nStartSegment) /
                                    (route.attack.nTurnFinishSegment - route.nStartSegment);
                vec.x = float(cos(fpAngle));
                vec.y = float(sin(fpAngle));
            }
            else
            {
                // unit should be oriented to target, so it is :) (even if target changed its way)
                vec.x = ptTarget.x - route.start.x;
                vec.y = ptTarget.y - route.start.y;
                float fpLeng = (float)sqrt(sqr(vec.x) + sqr(vec.y));
                if (fpLeng != 0)
                    vec *= 1 / fpLeng;
            }
        }
        else
        {
            // ASSERT(0&&"Happen to execute attacking motion without target" );
            vec.x = float(cos(route.fpStartAngle));
            vec.y = float(sin(route.fpStartAngle));
        }
    }
    break;
    }
    return bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::Load(CSave &file)
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
void CRouteWalker::Store(CSave &file)
{
    SaveSchema::RequireDynamicSchema();
}

/////////////////////////////////////////////////////////////////////////////////////
// CAIFullRoute implementation
/////////////////////////////////////////////////////////////////////////////////////
// return true if position changed
bool CAIFullRoute::SetStartPos(const CPlot3D &pos, const CPlot3D &look, int _nStartSegment)
{
    bool bRv;
    float fpAngle, fpStartX, fpStartY;
    float fpDirX, fpDirY, fpDx, fpDy;
    fpDirX = look.x;
    fpDirY = look.y;
    double fpM = sqrt(sqr(fpDirX) + sqr(fpDirY));
    if (fpM > 0)
    {
        fpDx = float(fpDirX / fpM);
        fpDy = float(fpDirY / fpM);
        fpAngle = float(atan2(fpDy, fpDx));
    }
    else
    {
        fpDx = 0;
        fpDy = 0;
        fpAngle = 0;
    }
    fpStartX = Meter2AITile(pos.x);
    fpStartY = Meter2AITile(pos.y);

    bRv = moveType == Stand && fpStartAngle == fpAngle && start.x == fpStartX &&
          start.y == fpStartY;
    moveType = Stand;
    start.x = fpStartX;
    start.y = fpStartY;
    fpStartAngle = fpAngle;
    nStartSegment = _nStartSegment;
    route.Clear();
    return !bRv;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::MakeRotation(const CPlot3D &dstVec, float fpAngSpeed)
{
    moveType = Rotate;
    rotate.fpFinalAngle = (float)atan2(dstVec.y, dstVec.x);
    rotate.fpAngleSpeed = fpAngSpeed;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::MakeAttackMotion(int _nTargetID, int _nHitSegment, int _nTurnFinish)
{
    moveType = Attack;
    attack.nTargetID = _nTargetID;
    attack.nHitSegment = _nHitSegment;
    attack.nTurnFinishSegment = _nTurnFinish;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::MakeAttackPointMotion(const CPlot3D &dstVec, int _nHitSegment, int _nTurnFinish)
{
    moveType = AttackPoint;
    attack1.fpFinalAngle = (float)atan2(dstVec.y, dstVec.x);
    attack1.nHitSegment = _nHitSegment;
    attack1.nTurnFinishSegment = _nTurnFinish;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::MakeMovement(float _fpFinishX, float _fpFinishY)
{
    moveType = Walk;
    walk.fpFinishX = _fpFinishX;
    walk.fpFinishY = _fpFinishY;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::DoNotTurnToAttack(int nWhomID)
{
    if (moveType != Attack)
        return;
    if (attack.nTargetID == nWhomID)
        attack.nTargetID = -1;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::Freeze(int nStep)
{
    nStartSegment += nStep;
    if (moveType == Attack)
    {
        attack.nHitSegment += nStep;
        attack.nTurnFinishSegment += nStep;
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::SetSpeed(WORD wSpeed256)
{
    for (POSITION pos = route.route.GetHeadPosition(); pos;)
        route.route.GetNext(pos).wSpeed256 = wSpeed256;
}
/////////////////////////////////////////////////////////////////////////////////////
CPlot3D CAIFullRoute::GetFinalPoint()
{
    if (moveType != Walk)
        return CPlot3D(AITile2Meter(start.x), AITile2Meter(start.y), 0);
    return CPlot3D(AITile2Meter(walk.fpFinishX), AITile2Meter(walk.fpFinishY), 0);
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::Clear()
{
    moveType = Stand;
    start = CPlot2D(0, 0);
    memset(&attack, 0, sizeof(attack));
    fpStartAngle = 0;
    nStartSegment = 0;
    route.Clear();
}
/////////////////////////////////////////////////////////////////////////////////////
struct SUpdateRouteUnit
{
    WORD wXDir : 2;
    WORD wYDir : 2;
    WORD wFlags : 2;
    WORD wSpeed : 10;
};
inline void FormUpdateUnit(SUpdateRouteUnit &res, SRouteUnit &u, short &nPrevX, short &nPrevY)
{
    res.wFlags = u.wFlags;
    res.wSpeed = Min((WORD)1023, u.wSpeed256);
    switch (u.nX - nPrevX)
    {
    case -1:
        res.wXDir = 0;
        break;
    case 0:
        res.wXDir = 1;
        break;
    case 1:
        res.wXDir = 2;
        break;
    default:
        res.wXDir = 3;
        break;
    }
    switch (u.nY - nPrevY)
    {
    case -1:
        res.wYDir = 0;
        break;
    case 0:
        res.wYDir = 1;
        break;
    case 1:
        res.wYDir = 2;
        break;
    default:
        res.wYDir = 3;
        break;
    }
    nPrevX = u.nX;
    nPrevY = u.nY;
}
/////////////////////////////////////////////////////////////////////////////////////
inline void WriteUpdateUnit(CUpdateHistory &pkt, SUpdateRouteUnit &res, SRouteUnit &u)
{
    pkt.WriteDirect(&res, 2);
    if (res.wXDir == 3)
        pkt.WriteDirect(&u.nX, 2);
    if (res.wYDir == 3)
        pkt.WriteDirect(&u.nY, 2);
}
/////////////////////////////////////////////////////////////////////////////////////
inline void ReadUpdateUnit(CUpdateHistory &pkt, SRouteUnit &u, short &nPrevX, short &nPrevY)
{
    SUpdateRouteUnit res;
    pkt.ReadDirect(&res, 2);
    u.wFlags = res.wFlags;
    u.wSpeed256 = res.wSpeed;
    switch (res.wXDir)
    {
    case 0:
        u.nX = nPrevX - 1;
        break;
    case 1:
        u.nX = nPrevX;
        break;
    case 2:
        u.nX = nPrevX + 1;
        break;
    case 3:
        pkt.ReadDirect(&u.nX, 2);
        break;
    }
    switch (res.wYDir)
    {
    case 0:
        u.nY = nPrevY - 1;
        break;
    case 1:
        u.nY = nPrevY;
        break;
    case 2:
        u.nY = nPrevY + 1;
        break;
    case 3:
        pkt.ReadDirect(&u.nY, 2);
        break;
    }
    nPrevX = u.nX;
    nPrevY = u.nY;
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::UpdateTo(CUpdateHistory &pkt)
{
    pkt << BYTE(moveType);
    pkt << BYTE(route.route.GetCount() >> 8);
    pkt << BYTE(route.route.GetCount() & 255);
    pkt.WritePos(start);
    pkt.WriteFloat(fpStartAngle, -FP_2PI, FP_2PI);
    pkt << nStartSegment;
    pkt << attack.nTargetID;
    pkt << attack.nHitSegment;
    pkt << attack.nTurnFinishSegment;
    //
    SUpdateRouteUnit updateUnit;
    POSITION pos = route.route.GetHeadPosition();
    short nPrevX = 0, nPrevY = 0;
    while (pos)
    {
        SRouteUnit &unit = route.route.GetNext(pos);
        FormUpdateUnit(updateUnit, unit, nPrevX, nPrevY);
        WriteUpdateUnit(pkt, updateUnit, unit);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::UpdateFrom(CUpdateHistory &pkt)
{
    int nCount;
    BYTE cNum, hPart, lPart;
    pkt >> cNum;
    moveType = EMovement(cNum);
    pkt >> hPart;
    pkt >> lPart;
    nCount = (((int)hPart) << 8) + lPart;
    pkt.ReadPos(start);
    pkt.ReadFloat(fpStartAngle, -FP_2PI, FP_2PI);
    pkt >> nStartSegment;
    pkt >> attack.nTargetID;
    pkt >> attack.nHitSegment;
    pkt >> attack.nTurnFinishSegment;
    route.route.RemoveAll();
    //
    short nPrevX = 0, nPrevY = 0;
    for (int i = 0; i < nCount; i++)
    {
        SRouteUnit &unit = route.route.GetAt(route.route.AddTail(SRouteUnit()));
        ReadUpdateUnit(pkt, unit, nPrevX, nPrevY);
    }
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::Load(CSave &file)
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
void CAIFullRoute::Store(CSave &file)
{
    SaveSchema::RequireDynamicSchema();
}
/////////////////////////////////////////////////////////////////////////////////////
