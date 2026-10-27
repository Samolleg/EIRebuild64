#pragma once
#include "Build.h"
namespace Navigation {
bool Project(const Mesh& mesh,Point point,float horizontal,float vertical,Location& result,float height=0);
bool StartLocation(const Mesh& mesh,Point point,Location& result,float height=0);
Route Find(const Mesh& mesh,const Request& request);
bool RandomReachable(const Mesh&,Point source,Point center,float radius,float height,float (*random)(),Point& result);
bool Constrain(const Mesh& mesh,Point from,Point desired,Location& result,float height=0);
}
