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
    bool have=false;CPlot2D previous;
    for(POSITION pos=route.GetHeadPosition();pos;) {
        POSITION current=pos;auto& node=route.GetNext(pos);
        bool duplicate=have&&Distance(previous,node.point)<.0001f;
        previous=node.point;have=true;if(duplicate)route.RemoveAt(current);
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
    Clear();fpStartAngle=route.fpStartAngle;
    switch(route.GetMoveType()) {
    case CAIFullRoute::Stand:case CAIFullRoute::Attack:return;
    case CAIFullRoute::AttackPoint:StartRotation(float(cos(route.attack1.fpFinalAngle)),float(sin(route.attack1.fpFinalAngle)),0);return;
    case CAIFullRoute::Rotate:StartRotation(float(cos(route.rotate.fpFinalAngle)),float(sin(route.rotate.fpFinalAngle)),route.rotate.fpAngleSpeed);return;
    case CAIFullRoute::Walk:break;
    default:return;
    }
    // Detour already straightens the corridor. Never spline outside it or round
    // its corners back onto the legacy 0.5 m lattice.
    for(POSITION pos=route.route.route.GetHeadPosition();pos;) {
        const auto& node=route.route.route.GetNext(pos);SWalkerWaypoint p;
        p.point=node.point;p.speed=CPlot2D(0,0);p.nTileX=node.nX;p.nTileY=node.nY;
        p.fpSpeed=(std::max)(.0001f,fpBaseSpeed*node.wSpeed256/N_RT_SPEED);points.Add(p);
    }
    for(int i=0;i<points.GetSize()-1;++i) {
        float length=Distance(points[i].point,points[i+1].point);
        if(length>.00001f)points[i].speed=(points[i+1].point-points[i].point)/length;
    }
    if(points.GetSize()>1) {
        points[points.GetSize()-1].speed=points[points.GetSize()-2].speed;
        StartRotation(points[0].speed.x,points[0].speed.y);
    } else fpDeltaAngle=0;
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
bool CRouteWalker::GetMovement(float time, bool &rotating, CPlot3D &pos, CPlot3D &vec, float &speed, int &tileX, int &tileY)
{
    if(points.GetSize()<2){speed=0;return false;}
    float angle=fpStartAngle;
    for(int i=0;i<points.GetSize()-1;++i) {
        auto& a=points[i];auto& b=points[i+1];
        float desired=a.GetAngle(),delta=desired-angle;
        while(delta>FP_PI)delta-=FP_2PI;while(delta<-FP_PI)delta+=FP_2PI;
        float turn=std::abs(delta)/(std::max)(.0001f,fpAngularSpeed);
        pos.x=a.point.x;pos.y=a.point.y;tileX=a.nTileX;tileY=a.nTileY;
        if(time<turn){rotating=true;angle+=delta*time/(std::max)(.0001f,turn);vec.x=std::cos(angle);vec.y=std::sin(angle);speed=0;return true;}
        time-=turn;angle=desired;float length=Distance(a.point,b.point),duration=length/a.fpSpeed;
        if(time<duration){float f=time/(std::max)(.00001f,duration);pos.x=a.point.x+(b.point.x-a.point.x)*f;pos.y=a.point.y+(b.point.y-a.point.y)*f;
            rotating=false;vec.x=a.speed.x;vec.y=a.speed.y;speed=a.fpSpeed;tileX=b.nTileX;tileY=b.nTileY;return true;}
        time-=duration;
    }
    auto& last=points[points.GetSize()-1];pos.x=last.point.x;pos.y=last.point.y;vec.x=last.speed.x;vec.y=last.speed.y;vec.z=0;
    tileX=last.nTileX;tileY=last.nTileY;speed=0;rotating=false;return false;
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

/////////////////////////////////////////////////////////////////////////////////////


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
        pkt.WriteDirect(&unit.point.x,4);pkt.WriteDirect(&unit.point.y,4);pkt.WriteDirect(&unit.supportZ,4);pkt.WriteDirect(&unit.terrain,2);
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
        pkt.ReadDirect(&unit.point.x,4);pkt.ReadDirect(&unit.point.y,4);pkt.ReadDirect(&unit.supportZ,4);pkt.ReadDirect(&unit.terrain,2);
    }
}
/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////
