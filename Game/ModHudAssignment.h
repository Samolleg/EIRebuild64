#pragma once
#include "ItemDatabase.h"

namespace ModHudAssignment {
// Display the same effective manual skill as addon, without changing mechanics.
template<class Stats> inline float ManualSkill(const Stats& stats,int skill){
    switch(skill){
    case RPGMSKL_SCIENCE:case RPGMSKL_STEALING:
        return stats.cManualSkills[skill]+stats.GetDexterityBonus();
    case RPGMSKL_TAME:return stats.cManualSkills[skill]+stats.GetIntelligenceBonus();
    default:return 0;
    }
}
// Dedicated mission HUD model names from native GetI3DFigureNameW.
inline const char* WeaponPrefix(int type){
    switch(type){
    case RPGWST_SWORD:return "sw";case RPGWST_AXE:return "ax";
    case RPGWST_DAGGER:return "dg";case RPGWST_SPEAR:return "sp";
    case RPGWST_HAMMER:return "hm";case RPGWST_BOW:return "bw";
    case RPGWST_CROSSBOW:return "cb";case RPGWST_HANDS1:return "sd";
    case RPGWST_HANDS4:return "ts";case RPGWST_HANDS5:return "ta";
    case RPGWST_HANDS2:return "tp";case RPGWST_HANDS6:return "th";
    case RPGWST_HANDS3:return "sf";case RPGWST_HANDS7:return "tf";
    default:return nullptr;
    }
}
inline CString WeaponModel(const SIDBWeaponShape& shape){
    CString name;const char* prefix=WeaponPrefix(int(shape.dwTypeID));
    if(prefix)name.Format("initwe%s%dweapon",prefix,shape.dwTextureTypeIndex);
    return name;
}
inline const char* SpellHotkey(int slot){
    static const char* names[]={"Q","W","E","R","T","Y","U","I",
        "Alt+Q","Alt+W","Alt+E","Alt+R","Alt+T","Alt+Y","Alt+U","Alt+I"};
    return slot>=0&&slot<16?names[slot]:nullptr;
}
inline void AppendSpellHotkey(CString& name,int slot){
    if(const char* key=SpellHotkey(slot)){CString text;text.Format("  ( %s )",key);name+=text;}
}
inline int SpellSlots(bool extended){return extended?16:8;}
struct Placement {int left,top,x,y,size,canvas;};
inline Placement SpellPlacement(int slot,int count,bool extended,bool hd){
    const int column=slot/8,row=slot%8;
    const int columnCount=column==0?(count<8?count:8):(count-8);
    const int size=hd?512:64;
    return {extended?760-column*40:760,
        extended?510-(columnCount-row)*40:510-(count-slot)*40,
        ((extended&&slot>=8?0:2)+row/4)*size,(row%4)*size,size,hd?2048:256};
}
}
