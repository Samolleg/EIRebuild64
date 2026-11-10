#pragma once
#include <string>
namespace ArmedTransitionFrames {
struct Clip{const char*name;int from,to;double last;};
inline constexpr Clip Clips[]={
{"ucross01",0,0,12},
{"ucross02",0,0,14},
{"ucross03",0,2,29},
{"ucross04",0,2,42},
{"ucross05",2,0,10},
{"ucross06",2,0,30},
{"ucross07",0,1,20},
{"ucross08",1,0,15},
{"ucross09",0,1,21},
{"ucross10",1,0,25},
{"ucross11",1,2,22},
{"ucross12",2,1,20},
{"scross13",0,3,1},
{"scross15",0,3,1},
{"scross17",0,3,1},
{"scross20",0,3,10},
{"scross23",1,3,8},
{"scross24",1,3,13},
{"scross25",2,4,12},
};
inline bool Resolve(const std::string&name,double frame,int&from,int&to,double&weight){
 if(!std::isfinite(frame)||frame<0)return false;for(const auto&c:Clips)if(name==c.name){from=c.from;to=c.to;double t=std::clamp(frame/std::max(c.last,1.),0.,1.);weight=t*t*(3-2*t);return true;}return false;
}
}
