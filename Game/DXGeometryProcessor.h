#pragma once

#include "Plot.h"
#include "TLPipelineFPU.h"
#include "Base3D.h"
// --------------------------------------------------------------------------------------------
const DWORD MAT_STACK_SCRNSPACE2 = 0;
const DWORD MAT_STACK_SCRNSPACE = 1;
const DWORD MAT_STACK_PROJSPACE = 2;
const DWORD MAT_STACK_WORLDSPACE = 3;
const DWORD MAT_STACK_CAMERASPACE = 4;
const DWORD MAT_STACK_VIEW2CLIP = 5;
const DWORD MAT_STACK_PROJECTION = 6;
const DWORD MAT_STACK_CLIPSCALE = 7;
const DWORD MAT_STACK_VIEWSCALE = 8;
const DWORD MAT_STACK_CAMERA = 9;
const DWORD MAT_STACK_SHADOWMAIN = 10;
const DWORD MAT_STACK_SHADOW1 = 11;
const DWORD MAT_STACK_SHADOW2 = 12;
const DWORD MAT_STACK_SHADOW3 = 13;
const DWORD MAT_STACK_TEMPORARY = 14;

const DWORD MAT_NUM_STACKS = 16;
const DWORD MAT_MAX_NUM_MATRICES = 16;
// --------------------------------------------------------------------------------------------
struct SMatrixStack
{
public:
    SHMatrix *matrices;
    SHMatrix topMatrix;
    DWORD dwNumMatrices;

public:
    SMatrixStack()
    {
        matrices = mm_malloc<SHMatrix>(MAT_MAX_NUM_MATRICES);
        dwNumMatrices = 0;
    }
    SMatrixStack(const SMatrixStack &) = delete;
    SMatrixStack(SMatrixStack &&) = delete;
    SMatrixStack &operator=(const SMatrixStack &) = delete;
    SMatrixStack &operator=(SMatrixStack &&) = delete;
    ~SMatrixStack() { mm_free(matrices); }

    SHMatrix &operator[](int nIndex) { return matrices[nIndex]; }
};
// --------------------------------------------------------------------------------------------
//
// --------------------------------------------------------------------------------------------
class CDXGeometryProcessor
{
private:
    LPTLPIPELINE
    lpGeometryProcessor;         // pointer to geometry processor, acquired from transfor library
    HINSTANCE hTransformLibrary; // transform library handle
    //
    SMatrixStack mstacks[MAT_NUM_STACKS];

public:
    CDXGeometryProcessor();
    ~CDXGeometryProcessor();
    //
    LPTLPIPELINE GetGeometryProcessor() const { return lpGeometryProcessor; }
    //
    int CheckVisibility(const SBoundingVolume &bv);
    int TransformVertices(SXYZW4 *pSrc, STLVertex1 *pDst, BYTE *pClips, DWORD *speculars,
                          const int nNumSrc, const SBoundingVolume *pBV);
    int TransformVertices(SXYZW4 *pSrc, STLVertex1 *pDst, BYTE *pClips, const int nNumSrc,
                          const SBoundingVolume *pBV);
    int TransformVertices(SXYZW4 *pSrc, SXYZW *pDst, BYTE *pClips, const int nNumSrc,
                          const SBoundingVolume *pBV);
    int TransformVertices(SXYZW *pSrc, SXYZW *pDst, BYTE *pClips, const int nNumSrc,
                          const SBoundingVolume *pBV);
    int TransformVertices2Projection(SXYZW4 *pSrc, SXYZW *pDst, BYTE *pClips,
                                     const int nNumSrcVertices);
    int TransformVertices2Projection(SXYZW4 *pSrc, SXYZW4 *pDst, BYTE *pClips,
                                     const int nNumSrcVertices);
    int TransformVertices2Projection(SXYZW *pSrc, SXYZW *pDst, BYTE *pClips,
                                     const int nNumSrcVertices, const SBoundingVolume *pBV);
    // creation of the different matrices from pipeline (projection, camera, VS, clip scale)
    void CreateProjectionMatrix(SHMatrix &matrix, float fov, float fAspect, float fNearPlane,
                                float fFarPlane);
    void CreateProjectionMatrix2(SHMatrix &matrix, float fov, float fAspect, float fNearPlane,
                                 float fFarPlane);
    void CreateCameraMatrix(SHMatrix &matrix, LPBASE3D lpCamera);
    void CreateCameraMatrix2(SHMatrix &matrix, LPBASE3D lpCamera);
    void CreateCameraMatrix(SHMatrix &matrix, const CPlot3D &ptFrom, const CPlot3D &ptTo,
                            const CPlot3D &ptUp);
    void CreateCameraMatrix2(SHMatrix &matrix, const CPlot3D &ptFrom, const CPlot3D &ptTo,
                             const CPlot3D &ptUp);
    void CreateViewportScaleMatrix(SHMatrix &matrix, const RECT &rcRect);
    void CreateClipScaleMatrix(SHMatrix &matrix);
    //
    void CreateShadowProjectionMatrix(SHMatrix &matrix, const CPlot3D &direction);
    void CreateShadowProjectionMatrixWithCorrection(SHMatrix &matrix, const CPlot3D &direction);
    void CreateDirectionCorrection(const CPlot3D &direction, CQuaternion &quat);
    //
    void ClearMatrixStack(const DWORD dwMatrixStackID);
    void ClearAllMatrixStacks();
    void PushMatrix(const SHMatrix &matrix, const DWORD dwMatrixStackID);
    void PopMatrix(const DWORD dwMatrixStackID);
    void PushMainMatrix(const SHMatrix &matrix);
    void PopMainMatrix();
    DWORD GetMatrixDepth(const DWORD dwMatrixStackID) const
    {
        return mstacks[dwMatrixStackID].dwNumMatrices;
    }
    bool IsHaveMatrix(const DWORD dwMatrixStackID) const
    {
        return (mstacks[dwMatrixStackID].dwNumMatrices != 0);
    }
    const SHMatrix &GetMatrix(const DWORD dwMatrixStackID) const;
    const SHMatrix GetWorldToScreenMatrix();
};

// --------------------------------------------------------------------------------------------
// remove all matrices from requested matrix stack
// --------------------------------------------------------------------------------------------
inline void CDXGeometryProcessor::ClearMatrixStack(const DWORD dwMatrixStackID)
{
    mstacks[dwMatrixStackID].dwNumMatrices = 0;
}

// --------------------------------------------------------------------------------------------
// clear all matrix stacks
// --------------------------------------------------------------------------------------------
inline void CDXGeometryProcessor::ClearAllMatrixStacks()
{
    DWORD i = MAT_NUM_STACKS;
    while (i)
    {
        --i;
        ClearMatrixStack(i);
    }
}
// --------------------------------------------------------------------------------------------
// inline void CDXGeometryProcessor::PushMatrix( const SHMatrix& matrix, const DWORD dwMatrixStackID
// ) push matrix to the stack
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// pop matrix frm the stack
// --------------------------------------------------------------------------------------------
inline void CDXGeometryProcessor::PopMatrix(const DWORD dwMatrixStackID)
{
    if (mstacks[dwMatrixStackID].dwNumMatrices != 0)
        mstacks[dwMatrixStackID].dwNumMatrices--;
}

// --------------------------------------------------------------------------------------------
// push matrix to the proj and scrn stacks
// --------------------------------------------------------------------------------------------
inline void CDXGeometryProcessor::PushMainMatrix(const SHMatrix &matrix)
{
    PushMatrix(matrix, MAT_STACK_PROJSPACE);
    PushMatrix(matrix, MAT_STACK_SCRNSPACE);
}

// --------------------------------------------------------------------------------------------
// pop matrix from the proj and scrn stacks
// --------------------------------------------------------------------------------------------
inline void CDXGeometryProcessor::PopMainMatrix()
{
    PopMatrix(MAT_STACK_PROJSPACE);
    PopMatrix(MAT_STACK_SCRNSPACE);
}

// --------------------------------------------------------------------------------------------
// get matrix from particular stack
// --------------------------------------------------------------------------------------------
inline const SHMatrix &CDXGeometryProcessor::GetMatrix(const DWORD dwMatrixStackID) const
{
    const bool valid = dwMatrixStackID < MAT_NUM_STACKS &&
        mstacks[dwMatrixStackID].dwNumMatrices != 0;
    ASSERT(valid);
    // A zero matrix is a safe, deliberately invalid packet value in Release.
    // Producers reject a missing stack explicitly; never read matrices[-1].
    static const SHMatrix unavailable = {};
    if (!valid) return unavailable;
    return mstacks[dwMatrixStackID].matrices[mstacks[dwMatrixStackID].dwNumMatrices - 1];
}

inline const SHMatrix CDXGeometryProcessor::GetWorldToScreenMatrix()
{
    SHMatrix world_to_screen, proj, camera, clip, view;
    view = GetMatrix(MAT_STACK_VIEWSCALE);
    clip = GetMatrix(MAT_STACK_CLIPSCALE);
    proj = GetMatrix(MAT_STACK_PROJECTION);
    camera = GetMatrix(MAT_STACK_CAMERA);
    world_to_screen = view * clip * proj * camera; // world -> screen (scrnspace) matrix
    return world_to_screen;
}
