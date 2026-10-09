#pragma once
#include <string>
#include <cstdio>

namespace ModCampEquipment {
// The native forward loop intentionally advances after a removal.
template<class Size,class IsShield,class Remove>
inline void ReplaceShields(bool incomingShield,Size size,IsShield shield,Remove remove)
{
    if (incomingShield) for (int i=0;i<size();++i) if (shield(i)) remove(i);
}
struct PrimarySnapshot {
    int strength,dexterity,intelligence;
    PrimarySnapshot(float s,float d,float i):strength(int(s)),dexterity(int(d)),intelligence(int(i)) {}
    void Restore(float& s,float& d,float& i) const { s=float(strength);d=float(dexterity);i=float(intelligence); }
};
inline std::string ShellName(char side,int texture)
{
    char name[32];std::snprintf(name,sizeof(name),"%c_shell.armor%02d",side,texture);return name;
}
template<class Remove> inline void ClearShells(Remove remove)
{
    for(int n=0;n<31;++n){remove(ShellName('r',n));remove(ShellName('l',n));}
}
template<class Add> inline void ArmorShells(int type,int texture,Add add)
{
    if(type==2&&(texture==1||texture==2||texture==3||texture==6||texture==16||texture==23||texture==24))
        {add(ShellName('r',texture));add(ShellName('l',texture));}
}
template<class Item> inline int AddQuick(Item** slots,int count,Item* item)
{
    if(!item)return -1;
    for(int i=0;i<count;++i)if(!slots[i]){slots[i]=item;item->AddRef();return i;}
    return -1;
}
template<class Item> inline bool RemoveQuick(Item** slots,int count,int index)
{
    if(index<0||index>=count)return false;
    if(slots[index])slots[index]->Release();
    for(int i=index;i<count-1;++i)slots[i]=slots[i+1];
    slots[count-1]=nullptr;return true;
}
// ERPGWeaponTypes HANDS2=10, HANDS4=12, HANDS5=13, HANDS6=14.
inline const char* IdlePose(int type,bool twohanded)
{
    if(twohanded&&type==12)return "2hscidle08";
    if(twohanded&&(type==13||type==14))return "2hacidle08";
    if(twohanded&&type==10)return "uspecial10";
    return "cidle09";
}
}
