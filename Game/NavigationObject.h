#pragma once
#include "MapObject.h"
#include "aiMap.h"

inline bool NavigationObject(const CMapObject* object) {
    const auto kind=object->WhoAmI();
    return kind==HPC_TREE||kind==HPC_STONE||kind==HPC_BUILDING||kind==HPC_STUFF_OBJECT||kind==HPC_TORCH||kind==HPC_LEVER||
        (kind>=HPC_ITEM&&kind<=HPC_ITEM_SPELL)||kind==HPC_TRAP||kind==HPC_MAGICTRAP;
}

// One transform for offline initial obstacles and live updates.
inline std::vector<Navigation::Solid> NavigationBoxes(CMapObject* object,CAIMap& map,bool visual=false) {
    object->CreateCurrentPlacement();std::vector<Navigation::Solid> boxes;
    for(INT_PTR i=0;i<object->fbvs.GetSize();++i) {
        const auto& bv=object->fbvs[i];if(!visual&&(bv.IsIgnored()||bv.IsBridge()||bv.GetPassability()!=AI_PASS_NO))continue;
        Navigation::Solid box;auto base=object->GetCP();base.z+=map.GetOriginalTileHeight(base);
        for(int j=0;j<8;++j) {
            CPlot3D corner((j&1)?(std::max)(bv.ptMin.x,bv.ptMax.x):(std::min)(bv.ptMin.x,bv.ptMax.x),
                (j&2)?(std::max)(bv.ptMin.y,bv.ptMax.y):(std::min)(bv.ptMin.y,bv.ptMax.y),
                (j&4)?(std::max)(bv.ptMin.z,bv.ptMax.z):(std::min)(bv.ptMin.z,bv.ptMax.z));
            corner=object->RotateVector(corner+bv.ptBone+bv.ptCenter)+base;box.corners[j]={corner.x,corner.y,corner.z};
        }
        boxes.push_back(box);
    }
    return boxes;
}
