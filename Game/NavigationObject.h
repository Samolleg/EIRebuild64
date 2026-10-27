#pragma once
#include "MapObject.h"

inline bool NavigationObject(const CMapObject* object) {
    const auto kind=object->WhoAmI();
    return kind!=HPC_UNIT&&!(kind>=HPC_ITEM&&kind<=HPC_ITEM_SPELL)&&kind!=HPC_TRAP&&kind!=HPC_MAGICTRAP;
}
