#pragma once
#include "engine_bridge.h"

namespace owned {
// Never bridge gaps: nodes outside the supplied overrides belong to the GPU pose.
template<class Write>
HRESULT write_pose_overrides(const std::vector<NativeWorldGeometry::Pose::Override>& overrides,
    UINT count,const std::array<UINT,2>& generation,Write write) {
    using Node=NativeWorldGeometry::Pose::Node;
    std::array<Node,32> staging;
    for(size_t i=0;i<overrides.size();) {
        const UINT first=overrides[i].node;
        if(first>=count)return E_INVALIDARG;
        size_t length=0;
        do {
            staging[length]=overrides[i].value;
            staging[length].generation=generation;
            ++length;++i;
        }while(i<overrides.size() && length<staging.size() &&
            length<count-first && overrides[i].node==first+length);
        const HRESULT result=write(first,staging.data(),UINT(length));
        if(FAILED(result))return result;
    }
    return S_OK;
}
}
