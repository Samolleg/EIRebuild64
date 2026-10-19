#pragma once
#include "model_preview_composition.h"

// Mechanical source API conversion; all semantic placements/depth ordering live
// in the renderer. Keep the source quaternion primitive and its evaluation order.
inline std::array<float,4> SourcePreviewQuaternion(
    const std::array<owned::PreviewRotation,3>& rotations,unsigned count) {
    const auto value=[](const owned::PreviewRotation& r) {
        return CQuaternion(CPlot3D(r.axis[0],r.axis[1],r.axis[2]),r.angle);
    };
    auto q=value(rotations[0]);
    for(unsigned i=1;i<count;++i)q=q*value(rotations[i]);
    std::array<float,4> result;
    q.GetComponents(result[0],result[1],result[2],result[3]);return result;
}
inline void SourcePreviewDepth(CRenderer* renderer,const std::array<long,4>* depth,
    unsigned count,float value) {
    D3DRECT rects[6];
    for(unsigned i=0;i<count;++i)rects[i]={depth[i][0],depth[i][1],depth[i][2],depth[i][3]};
    renderer->Clear(count,count?rects:nullptr,D3DCLEAR_ZBUFFER,0,value,0);
}
inline void SourcePreviewRect(CInterface3D* ui,std::array<long,4>& depth,bool local) {
    CRect r(depth[0],depth[1],depth[2],depth[3]);
    if(local)ui->VirtualToScreenLocal(r);else ui->VirtualToScreen(r);
    depth={r.left,r.top,r.right,r.bottom};
}
