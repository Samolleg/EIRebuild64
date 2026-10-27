#pragma once
#include "MapObject.h"

inline bool NavigationObject(const CMapObject* object) {
    const auto kind=object->WhoAmI();
    return kind==HPC_TREE||kind==HPC_STONE||kind==HPC_BUILDING||kind==HPC_STUFF_OBJECT||kind==HPC_TORCH||kind==HPC_LEVER||
        (kind>=HPC_ITEM&&kind<=HPC_ITEM_SPELL)||kind==HPC_TRAP||kind==HPC_MAGICTRAP;
}
