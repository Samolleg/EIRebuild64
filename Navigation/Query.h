#pragma once
#include "Build.h"
namespace Navigation {
bool Project(const Mesh& mesh,Point point,float horizontal,float vertical,Location& result);
Route Find(const Mesh& mesh,const Request& request);
bool Constrain(const Mesh& mesh,Point from,Point desired,Location& result);
}
