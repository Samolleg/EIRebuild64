#pragma once
#include <string>
#include <cstring>
namespace ModItemUiPolicy {
template<class Get> inline bool Advanced(const char* key,Get get)
{
    if(!get("useAdvancedDescriptions"))return false;
    // Native LS_UseWeaponActiveSpells is permanently true, separately from
    // companion controls and the independent extended-slot registration.
    if(!strcmp(key,"useLS_SetUnitS"))return get(key)||get("JMVExtendedSpellSlots")||true;
    return get(key)!=0;
}
template<class Resolve> inline std::string Instruction(bool enabled,bool prototype,bool figure,
    const char* code,const char* fallback,Resolve resolve)
{
    if(!enabled||!code)return fallback;
    const std::string name=resolve(code,1);
    const std::string value=resolve(code,figure?2:3);
    if(_stricmp(code,name.c_str()))return fallback;
    return prototype&&figure?std::string("initqi3item"):value;
}
inline bool FirstDeploy(bool campReturn,bool prevent,const char* zone,bool nearStart)
{return !campReturn&&!prevent&&_strnicmp(zone,"zone1",5)==0&&nearStart;}
struct Icon {float z,size;};
inline Icon ItemIcon(bool advanced){return advanced?Icon{4,150}:Icon{3,120};}
inline int NameInset(bool advancedConsider){return advancedConsider?0:5;}
}
