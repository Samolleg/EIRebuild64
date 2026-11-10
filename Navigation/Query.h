#pragma once
#include "Build.h"
namespace Navigation {
// A zero class mask uses the profile's representative material class.
bool Project(const Mesh& mesh,Point point,float horizontal,float vertical,Location& result,float height=0,unsigned short excludedFlags=0);
// Nearest Detour polygon for sampled approach endpoints, retaining source support.
bool NearestSupport(const Mesh&,Point,float horizontal,float vertical,Location&,float height,unsigned short excludedFlags);
bool StartLocation(const Mesh& mesh,Point point,Location& result,float height=0,unsigned short excludedFlags=0);
// Placement/load resolve by position and height; the old surface only breaks ties.
bool ResolveLocation(const Mesh&,Point point,const Location* hint,Location& result,float height,unsigned short excludedFlags=0);
bool ClosestLocation(const Mesh&,Point,Location&,float height,unsigned short excludedFlags);
Route Find(const Mesh& mesh,const Request& request);
bool RandomReachable(const Mesh&,Point source,Point center,float radius,float height,float (*random)(),Point& result,unsigned short excludedFlags=0,const Location* address=nullptr,RandomShape shape=RandomShape::Circle);
bool Constrain(const Mesh& mesh,Point from,Point desired,Location& result,float height=0,const char** reason=nullptr,const Location* address=nullptr,unsigned short excludedFlags=0);
// A rewindable view lets the game consume its route list without copying it.
struct PathView {
    virtual ~PathView()=default;
    virtual void Reset()=0;
    virtual bool Next(Point&)=0;
};
bool Follow(const Mesh&,Point from,Point desired,PathView& path,Location& result,float height,const char** reason=nullptr,const Location* address=nullptr,unsigned short excludedFlags=0);
// Advance along every crossed corner, including multiple corners in one tick.
bool Follow(const Mesh&,Point from,Point desired,const std::vector<Point>& path,Location& result,float height,const char** reason=nullptr,const Location* address=nullptr,unsigned short excludedFlags=0);
}
