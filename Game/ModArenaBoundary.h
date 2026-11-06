#pragma once
#include "ModEncounter.h"
#include "../Navigation/Types.h"

namespace ModArena {
// One immutable phase snapshot for a synchronous plan/step. No tile changes,
// physical objects or persistent process state; releasing GS opens the exit.
struct Boundary {
    bool rectangle=false,seal=false,ram=false;
    float left=28,right=100,bottom=26,top=62,body=0,cx=0,cy=0,radius=0;
    float ox=0,oy=0,dx=0,dy=0;
    bool Active() const { return rectangle||seal; }
    float Depth(Navigation::Point p) const {
        const float r=body+.06f;
        return (std::min)((std::min)(p.x-left-r,right-r-p.x),(std::min)(p.y-bottom-r,top-r-p.y));
    }
    int Block(Navigation::Point from,Navigation::Point to) const {
        if(!Active())return 0;
        if(!Navigation::Finite(from)||!Navigation::Finite(to))return 1;
        if(rectangle) {
            const float before=Depth(from),after=Depth(to);
            if(before>=0?after<0:after<before-.00001f)return 1;
        }
        if(ram&&std::fabs((to.x-ox)*dy-(to.y-oy)*dx)>.42f)return 2;
        if(seal) {
            const double x=from.x-cx,y=from.y-cy,vx=to.x-from.x,vy=to.y-from.y,len=vx*vx+vy*vy;
            if(len<1e-12)return 0;
            const double t=(std::max)(0.,(std::min)(1.,-(x*vx+y*vy)/len));
            const double old=x*x+y*y,closest=(x+t*vx)*(x+t*vx)+(y+t*vy)*(y+t*vy);
            if(old<radius*radius?closest<old-1e-6:closest<radius*radius)return 1;
        }
        return 0;
    }
    bool Goal(Navigation::Point from,Navigation::Point to) const {
        if(!Active())return true;
        Boundary endpoint=*this;endpoint.seal=false;
        if(endpoint.Block(from,to))return false;
        if(!seal)return true;
        const float before=std::hypot(from.x-cx,from.y-cy),after=std::hypot(to.x-cx,to.y-cy);
        return before<radius?after>=before:after>=radius;
    }
    bool Route(const Navigation::Route& route,Navigation::Point from) const {
        if(!Active())return true;
        for(const auto& node:route.points) {
            const auto end=node.position;const auto start=from;
            const int count=(std::max)(1,int(std::ceil(std::hypot(end.x-from.x,end.y-from.y)/.25f)));
            for(int i=1;i<=count;++i) {
                const float f=float(i)/count;
                const Navigation::Point next{start.x+(end.x-start.x)*f,start.y+(end.y-start.y)*f,start.z+(end.z-start.z)*f};
                if(Block(from,next))return false;from=next;
            }
        }
        return true;
    }
};
inline Boundary MovementBoundary(CUnit* unit) {
    Boundary result;auto& world=theWorldServer;
    if((!ModEncounter::Arena(world)&&!ModEncounter::Curse(world))||!ModInvincible::UnitValid(unit,true)||
        ModInvincible::World(unit)!=&world||unit->GetUserUnitStats()->cLocomotion!=0)return result;
    const bool curse=ModEncounter::CurseActive(world),final=!ModEncounter::Curse(world)&&ModEncounter::Read(world,"FGL.Active")==1;
    if(!ModEncounter::Hero(world,unit)&&!(final&&unit->GetID()==990400))return result;
    result.body=unit->GetCollisionDistance();
    if(!std::isfinite(result.body)||result.body<0||result.body>(final?8.f:2.f))result.body=.4f;
    if(curse){result.rectangle=true;result.left=54;result.right=106;result.bottom=46;result.top=102;return result;}
    if(ModEncounter::Curse(world))return Boundary{};
    auto* boss=world.GetUnit(1000105);
    if(!final&&!ModInvincible::UnitValid(boss,true))return Boundary{};
    result.rectangle=final||ModEncounter::Read(world,"EFC.Walls")==1;
    result.seal=!final&&ModEncounter::Read(world,"A62.Active")==1&&ModEncounter::Read(world,"A62.SealBroken")==0;
    if(result.seal){result.cx=boss->GetCP().x;result.cy=boss->GetCP().y;result.radius=3.4f+.02f+result.body;}
    result.ram=final&&unit->GetID()==990400&&ModEncounter::Read(world,"FGL.Phase")==3;
    if(result.ram){result.ox=ModEncounter::Read(world,"FGL.OX");result.oy=ModEncounter::Read(world,"FGL.OY");result.dx=ModEncounter::Read(world,"FGL.DX");result.dy=ModEncounter::Read(world,"FGL.DY");}
    return result;
}
inline void Blocked(const Boundary& boundary,Navigation::Point at,int reason) {
    if(!boundary.ram)return;
    auto& world=theWorldServer;
    ModEncounter::Set(world,"FGL.BlockX",at.x);ModEncounter::Set(world,"FGL.BlockY",at.y);
    ModEncounter::Set(world,"FGL.BlockReason",float(reason));ModEncounter::Set(world,"FGL.Blocked",1);
}
} // namespace ModArena
