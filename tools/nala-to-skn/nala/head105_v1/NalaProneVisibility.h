#pragma once
// Render-only exception for an equipped bow, not a change to addon state.
#include "GamePoseReader.h"
#include "UnityWeaponCatalog.h"
namespace NalaProneVisibility {
inline bool Pose(const std::string&clip){return clip=="cidle03"||clip=="cidle06"||clip=="ccrawl01"||clip=="scrawl01"||clip=="ucross03"||clip=="ucross04"||clip=="ucross11"||clip=="ucross05"||clip=="ucross06"||clip=="ucross12"||clip=="scross25";}
inline const UnityWeaponCatalog::Spec* Bow(const UnityGame::Actor&actor){
 if(!Pose(actor.animation))return nullptr;
 for(const auto&a:actor.attachments){const auto*s=UnityWeaponCatalog::Find(a.name);if(s&&s->role==UnityWeaponCatalog::Role::Bow&&a.driver==int(7+s->side))return s;}
 return nullptr;
}
inline bool Reveal(const UnityGame::Actor&actor,const UnityGame::Attachment&a){
 const auto*bow=Bow(actor);if(!bow)return false;
 if(a.name=="quiver"||a.name=="arrows")return a.driver==1;
 const auto*member=UnityWeaponCatalog::Assembly(a.name);
 return member&&member->role==UnityWeaponCatalog::Role::Bow&&member->side==bow->side&&a.driver==int(7+bow->side);
}
inline bool Hidden(const UnityGame::Actor&actor,const UnityGame::Attachment&a){return (a.flags&0x80)&&!Reveal(actor,a);}
}
