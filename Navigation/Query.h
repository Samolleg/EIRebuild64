#pragma once
#include "Build.h"
namespace Navigation {
bool Project(const Mesh& mesh,Point point,float horizontal,float vertical,Location& result,float height=0,unsigned short excludedFlags=0);
bool StartLocation(const Mesh& mesh,Point point,Location& result,float height=0,unsigned short excludedFlags=0);
// Placement/load resolve by position and height; the old surface only breaks ties.
bool ResolveLocation(const Mesh&,Point point,const Location* hint,Location& result,float height,unsigned short excludedFlags=0);
Route Find(const Mesh& mesh,const Request& request);
bool RandomReachable(const Mesh&,Point source,Point center,float radius,float height,float (*random)(),Point& result,unsigned short excludedFlags=0);
bool Constrain(const Mesh& mesh,Point from,Point desired,Location& result,float height=0,const char** reason=nullptr,const Location* address=nullptr,unsigned short excludedFlags=0);
// Advance along every crossed corner, including multiple corners in one tick.
bool Follow(const Mesh&,Point from,Point desired,const std::vector<Point>& path,Location& result,float height,const char** reason=nullptr,const Location* address=nullptr,unsigned short excludedFlags=0);
}
