#pragma once
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
namespace ModNpcEquipment {
struct Resources { float hp,mp; };
// LuaScripts::RedistributeHPMP: owner presence, not personage ID or IsHero().
template<class HeroHP,class HeroMP,class NpcHP,class NpcMP,class ModifiedHP>
Resources BaseResources(bool hasOwner,HeroHP heroHP,HeroMP heroMP,NpcHP npcHP,NpcMP npcMP,ModifiedHP modifiedHP)
{
    const float hp=modifiedHP(hasOwner?heroHP():npcHP());
    const float mp=hasOwner?heroMP():npcMP();
    return {hp,mp};
}
inline int Truncate(float value)
{
    if(!std::isfinite(value)||double(value)<std::numeric_limits<int>::lowest()||
       double(value)>(std::numeric_limits<int>::max)())
        throw std::runtime_error("NPC equipment integer overflow");
    return int(value);
}
inline int Capacity(float base,float jewelry){return Truncate(base+base*jewelry);}
inline float Mana(float base,float overload){return base*overload;}
// Native WearAll post-wrapper: no persistent cache or name-based deduplication.
template<class Lookup,class Create,class AddUnit,class FindServer,class AddServer,class Notify>
void GrantQuicks(Lookup lookup,Create create,AddUnit addUnit,FindServer findServer,AddServer addServer,Notify notify)
{
    for(int property=2;property<6;++property)
    {
        const std::string name=lookup(property);
        if(name.empty())continue;
        auto* item=create(name.c_str());
        if(!item)continue;
        addUnit(item);
        auto* server=findServer();
        if(server){addServer(server,item);notify();}
    }
}
}
