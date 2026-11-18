#pragma once
#include "PhysicsFxCore.h"
class CWorldClient;
class CPlot3D;
namespace PhysicsFx {
class Client {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Client();~Client();
    void Reset(int tick=0);
    void Fireblast(CWorldClient&,int id,int start,const CPlot3D& point,float power,float radius);
    void Tick(CWorldClient&,bool paused);
    std::vector<Stone> Sample(float fraction,bool paused) const;
    std::uint64_t TextureSurface() const;
};
}
