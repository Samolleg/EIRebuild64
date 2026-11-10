#pragma once
#include <cstring>
namespace MeleeGripFrames {
inline bool LeftKnown(const char*name){return std::strcmp(name,"lh3.sword13")==0||std::strcmp(name,"lh3.axe13")==0||std::strcmp(name,"lh3.axe01")==0||std::strcmp(name,"lh3.axe02")==0||std::strcmp(name,"lh3.axe03")==0||std::strcmp(name,"lh3.axe05")==0||std::strcmp(name,"lh3.axe07")==0||std::strcmp(name,"lh3.axe08")==0||std::strcmp(name,"lh3.axe09")==0||std::strcmp(name,"lh3.axe10")==0||std::strcmp(name,"lh3.dagger01")==0||std::strcmp(name,"lh3.dagger02")==0||std::strcmp(name,"lh3.dagger03")==0||std::strcmp(name,"lh3.dagger04")==0||std::strcmp(name,"lh3.dagger05")==0||std::strcmp(name,"lh3.dagger07")==0||std::strcmp(name,"lh3.dagger08")==0||std::strcmp(name,"lh3.dagger09")==0||std::strcmp(name,"lh3.daxe00")==0||std::strcmp(name,"lh3.daxe01")==0||std::strcmp(name,"lh3.daxe02")==0||std::strcmp(name,"lh3.daxe03")==0||std::strcmp(name,"lh3.daxe04")==0||std::strcmp(name,"lh3.dsword00")==0||std::strcmp(name,"lh3.dsword01")==0||std::strcmp(name,"lh3.dsword02")==0||std::strcmp(name,"lh3.sword01")==0||std::strcmp(name,"lh3.sword02")==0||std::strcmp(name,"lh3.sword03")==0||std::strcmp(name,"lh3.sword04")==0||std::strcmp(name,"lh3.sword06")==0||std::strcmp(name,"lh3.sword08")==0||std::strcmp(name,"lh3.sword09")==0||std::strcmp(name,"lh3.sword10")==0||std::strcmp(name,"lh3.sword11")==0||std::strcmp(name,"lh3.sword12")==0;}
inline UnityBody::V LeftNormal(const char*name){
if(std::strcmp(name,"lh3.axe01")==0)return {0.17511797690592454,-2.7755575615628914e-17,-0.98454745653237874};
if(std::strcmp(name,"lh3.axe02")==0)return {0.40695626520851425,-2.0816681711721685e-17,-0.91344764393343181};
if(std::strcmp(name,"lh3.axe03")==0)return {0.53518873502701769,4.8572257327350599e-17,-0.84473251263354421};
if(std::strcmp(name,"lh3.axe05")==0)return {0.53523992377537666,1.3877787807814457e-17,-0.84470007931627977};
if(std::strcmp(name,"lh3.axe07")==0)return {-0.19870169136422183,-1.3877787807814457e-17,-0.98006001747290861};
if(std::strcmp(name,"lh3.axe08")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.axe09")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.axe10")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.dagger01")==0)return {0.5393338375530341,-2.0816681711721685e-17,-0.84209204465445353};
if(std::strcmp(name,"lh3.dagger02")==0)return {-0.56998404178110396,-1.3877787807814457e-17,-0.82165576253981021};
if(std::strcmp(name,"lh3.dagger03")==0)return {-0.51586212862660541,1.3877787807814457e-17,-0.85667161984556717};
if(std::strcmp(name,"lh3.dagger04")==0)return {0.54176917077246722,2.7755575615628914e-17,-0.84052731401217018};
if(std::strcmp(name,"lh3.dagger05")==0)return {-0.30135483188156564,3.4694469519536142e-17,-0.95351206877607675};
if(std::strcmp(name,"lh3.dagger07")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.dagger08")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.dagger09")==0)return {0.51495128531726653,3.4694469519536142e-18,-0.85721944317082288};
if(std::strcmp(name,"lh3.daxe00")==0)return {0.38844507816565232,6.9388939039072284e-18,-0.9214718776223616};
if(std::strcmp(name,"lh3.daxe01")==0)return {-0.44689816240809876,0,-0.89458483803173461};
if(std::strcmp(name,"lh3.daxe02")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.daxe03")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.daxe04")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.dsword00")==0)return {-0.2046202587333574,2.0816681711721685e-17,-0.97884143236578114};
if(std::strcmp(name,"lh3.dsword01")==0)return {0,1.7347234759768071e-17,-1};
if(std::strcmp(name,"lh3.dsword02")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.sword01")==0)return {0.48135660354722343,1.7347234759768071e-17,-0.8765248543090387};
if(std::strcmp(name,"lh3.sword02")==0)return {0.087303362765872611,-3.2959746043559335e-17,-0.99618177199232605};
if(std::strcmp(name,"lh3.sword03")==0)return {-0.54493531042449916,-2.7755575615628914e-17,-0.8384780900253479};
if(std::strcmp(name,"lh3.sword04")==0)return {-0.62626020186686082,1.3877787807814457e-17,-0.77961410938853537};
if(std::strcmp(name,"lh3.sword06")==0)return {-0.65012076578282163,1.3877787807814457e-17,-0.75983089559319572};
if(std::strcmp(name,"lh3.sword08")==0)return {-0.0027113945297322084,-1.3877787807814457e-17,-0.99999632416309647};
if(std::strcmp(name,"lh3.sword09")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.sword10")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.sword11")==0)return {-0.30144119539762326,2.7755575615628914e-17,-0.95348476952558225};
if(std::strcmp(name,"lh3.sword12")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.sword13")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
if(std::strcmp(name,"lh3.axe13")==0)return {-0.015381550169191321,1.7347234759768071e-17,-0.99988169695939166};
return {0,0,-1};
}
}
