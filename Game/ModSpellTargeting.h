#pragma once
#include "RPGSystemConsts.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace ModSpellTargeting {
inline bool StockOffensive(int id) {
    switch(id) {
    case SPELL_ID_FIREARROW:case SPELL_ID_LIGHTNING:case SPELL_ID_ACIDRAY:
    case SPELL_ID_FIREBALL:case SPELL_ID_INVOKE_LGHTNG:case SPELL_ID_COLUMN_ACID:
    case SPELL_ID_FIREWALL:case SPELL_ID_WALL_LGHTNG:case SPELL_ID_POISONFOG:
    case SPELL_ID_LICHDOM:case SPELL_ID_STENCH:case SPELL_ID_STUN:
    case SPELL_ID_WEAKEN:case SPELL_ID_FEEBLEMIND:case SPELL_ID_SLOW:
    case SPELL_ID_CHARM:case SPELL_ID_SHRINK:case SPELL_ID_RICKMAGIC:
    case SPELL_ID_CURSEMAGIC:return true;
    }
    return false;
}
inline bool Offensive(int rawID,bool selfCast,bool particles) {
    // The native wire ID is 54. This tree's legacy SPELL_ID_TEST is 47;
    // changing the global enum would also change damage/save consumers.
    if(StockOffensive(rawID)||(selfCast&&rawID==54))return true;
    if(!particles)return false;
    switch(rawID) {
    case SPELL_ID_VAMP:case SPELL_ID_VISIONFOG:
    case SPELL_ID_FIREARROWDARK:case SPELL_ID_FIREARROWLIGHT:
    case SPELL_ID_LIGHTNINGDARK:case SPELL_ID_LIGHTNINGLIGHT:
    case SPELL_ID_ACIDRAYDARK:case SPELL_ID_ACIDRAYLIGHT:
    case SPELL_ID_FIREBALLDARK:case SPELL_ID_FIREBALLLIGHT:
    case SPELL_ID_INVOKE_LGHTNGDARK:case SPELL_ID_INVOKE_LGHTNGLIGHT:
    case SPELL_ID_COLUMN_ACIDDARK:case SPELL_ID_COLUMN_ACIDLIGHT:
    case SPELL_ID_FIREWALLDARK:case SPELL_ID_FIREWALLLIGHT:
    case SPELL_ID_WALL_LGHTNGDARK:case SPELL_ID_WALL_LGHTNGLIGHT:
    case SPELL_ID_POISONFOGDARK:case SPELL_ID_POISONFOGLIGHT:return true;
    }
    return false;
}
struct Point {float x,y,z;};
inline Point PreviewCenter(bool present,bool visible,bool dead,Point unit,Point terrain) {
    return present&&visible&&!dead?unit:terrain;
}
inline bool PreviewSelected(const std::array<bool,8>& flags,int slot,float radius,Point center) {
    for(bool flag:flags)if(!flag)return false;
    return slot>=0&&slot<16&&radius>0.05f&&(center.x!=0.0f||center.y!=0.0f);
}
struct Vertex {float x,y,z,w;};
struct Ring {std::array<Vertex,64> vertices;std::array<uint16_t,192> indices;};
template<class Height> Ring AreaRing(Point center,float radius,Height height) {
    Ring ring={};
    const float half=(std::max)(0.056f,(std::min)(0.126f,radius*0.028f));
    const float inner=(std::max)(0.01f,radius-half),outer=radius+half;
    for(int i=0;i<32;++i) {
        const float angle=(2.0f*3.1415926536f*i)/32;
        const float c=cosf(angle),s=sinf(angle),radii[2]={outer,inner};
        for(int edge=0;edge<2;++edge) {
            auto& v=ring.vertices[i*2+edge];
            v.x=center.x+c*radii[edge];v.y=center.y+s*radii[edge];
            v.z=height(v.x,v.y)+0.04f;v.w=1.0f;
        }
        const int next=(i+1)%32;
        const uint16_t idx[6]={uint16_t(i*2),uint16_t(next*2),uint16_t(next*2+1),uint16_t(i*2),uint16_t(next*2+1),uint16_t(i*2+1)};
        for(int j=0;j<6;++j)ring.indices[i*6+j]=idx[j];
    }
    return ring;
}
template<class UnitCommand,class PointCommand>
void ActiveSelfCast(bool point,UnitCommand unitCommand,PointCommand pointCommand) {
    // Each closure captures the spell-bar owner, never the selected party.
    if(point)pointCommand();else unitCommand();
}
}
