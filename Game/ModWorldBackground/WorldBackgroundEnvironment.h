#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <cmath>

namespace WorldBackground {
enum VisualDependency : uint32_t { VisualNone=0,VisualLight=1,VisualView=2,VisualDeviceFog=4,VisualClock=8,VisualSunPosition=16,VisualAtmosphere=32,VisualTerrainSun=64 };
// Value-only input. No engine objects or pointers cross the worker boundary.
// Light is sun RGB, direction XYZ, ambient RGB. Clock is an authoritative
// producer-supplied simulation value, NEVER renderer QPC time. Zero validity
// means unavailable, not black lighting or zero time.
struct VisualEnvironment {
    uint32_t epoch=0,valid=0;
    std::array<float,9> light={};
    std::array<float,18> view={}; // clip matrix and logical camera XY
    std::array<uint32_t,5> fogModes={};
    std::array<float,3> fogRange={}; // raw D3D state, not a CPU fog equation
    std::array<float,3> sunPosition={};
    std::array<float,3> atmosphereRange={}; // native start,end,255/(end-start)
    std::array<float,3> atmosphereProjection={}; // native P43,P34,P33
    std::array<float,3> terrainSun={}; // cached trace CA, CB, native inverse absorption depth
    std::array<int32_t,3> terrainSteps={}; // cached X/Y steps and native shadow enable
    double clock=0;
};
class VisualInvalidation {
    VisualEnvironment previous;uint64_t topology=0,revision=0;uint32_t dependencies=0;
public:
    uint64_t Update(uint32_t epoch,uint64_t selected,const VisualEnvironment& value,uint32_t used){
        if(!epoch||value.epoch!=epoch)throw std::invalid_argument("Stale visual environment");
        const uint32_t available=value.valid&used;
        auto finite=[](double n){if(!std::isfinite(n))throw std::invalid_argument("Nonfinite visual environment");};
        if(available&VisualLight)for(float n:value.light)finite(n);
        if(available&VisualView)for(float n:value.view)finite(n);
        if(available&VisualDeviceFog)for(float n:value.fogRange)finite(n);
        if(available&VisualClock)finite(value.clock);
        if(available&VisualSunPosition)for(float n:value.sunPosition)finite(n);
        if(available&VisualAtmosphere){for(float n:value.atmosphereRange)finite(n);for(float n:value.atmosphereProjection)finite(n);}
        if(available&VisualTerrainSun)for(float n:value.terrainSun)finite(n);
        bool changed=!revision||epoch!=previous.epoch||selected!=topology||used!=dependencies||available!=(previous.valid&used);
        if(available&VisualLight)changed=changed||value.light!=previous.light;
        if(available&VisualView)changed=changed||value.view!=previous.view;
        if(available&VisualDeviceFog)changed=changed||value.fogModes!=previous.fogModes||value.fogRange!=previous.fogRange;
        if(available&VisualClock)changed=changed||value.clock!=previous.clock;
        if(available&VisualSunPosition)changed=changed||value.sunPosition!=previous.sunPosition;
        if(available&VisualAtmosphere)changed=changed||value.atmosphereRange!=previous.atmosphereRange||value.atmosphereProjection!=previous.atmosphereProjection;
        if(available&VisualTerrainSun)changed=changed||value.terrainSun!=previous.terrainSun||value.terrainSteps!=previous.terrainSteps;
        previous=value;topology=selected;dependencies=used;
        if(changed){if(revision==UINT64_MAX)throw std::overflow_error("Visual revision overflow");++revision;}
        return revision;
    }
};
}
