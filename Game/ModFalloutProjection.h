#pragma once

#include "Camera.h"
#include "DXGeometryProcessor.h"

// The physical camera exists only while the world is rendered. Simulation, UI,
// and persistent camera placement continue to use the logical orbit position.
class CModFalloutTelephotoFrame
{
    CCamera &camera;
    CPlot3D logicalCP;
    bool active = false;

public:
    CModFalloutTelephotoFrame(CCamera &camera, bool enabled, float multiplier)
        : camera(camera), logicalCP(camera.GetCP())
    {
        if (!enabled || multiplier <= 1.0001f || camera.GetDistance() <= 0.0f)
            return;
        camera.SetCP(logicalCP - camera.GetZAxis() *
                                  (camera.GetDistance() * (multiplier - 1.0f)));
        active = true;
    }

    ~CModFalloutTelephotoFrame() { Reset(); }
    bool IsActive() const { return active; }
    const CPlot3D &LogicalCP() const { return logicalCP; }

    void Reset()
    {
        if (active)
        {
            camera.SetCP(logicalCP);
            active = false;
        }
    }
};

// Push the same projection scale onto the native matrix stacks used by the
// installed addon. Exit restores their exact pre-scope depths, including when
// an inner draw/picking path left an extra matrix behind.
class CModFalloutProjectionScope
{
    CDXGeometryProcessor *gpu;
    DWORD ids[5] = {};
    DWORD depths[5] = {};
    DWORD count = 0;
    bool active = false;

    static unsigned int &ScopeDepth()
    {
        static unsigned int depth = 0;
        return depth;
    }

public:
    CModFalloutProjectionScope(CDXGeometryProcessor *gpu, bool enabled,
                               float multiplier, const DWORD *stackIds, DWORD stackCount)
        : gpu(gpu)
    {
        if (!enabled || !gpu || !stackIds || !stackCount || stackCount > 5 ||
            ScopeDepth() != 0 ||
            multiplier <= 1.0001f)
            return;
        for (DWORD i = 0; i < stackCount; ++i)
        {
            if (stackIds[i] >= MAT_NUM_STACKS)
                return;
            const DWORD depth = gpu->GetMatrixDepth(stackIds[i]);
            if (!depth || depth >= MAT_MAX_NUM_MATRICES)
                return;
            ids[i] = stackIds[i];
            depths[i] = depth;
        }
        SHMatrix scale = {};
        scale._11 = multiplier;
        scale._22 = multiplier;
        scale._33 = 1.0f;
        scale._44 = 1.0f;
        for (DWORD i = 0; i < stackCount; ++i)
            gpu->PushMatrix(scale, ids[i]);
        count = stackCount;
        active = true;
        ++ScopeDepth();
    }

    ~CModFalloutProjectionScope()
    {
        if (!active)
            return;
        for (DWORD i = 0; i < count; ++i)
            while (gpu->GetMatrixDepth(ids[i]) > depths[i])
                gpu->PopMatrix(ids[i]);
        --ScopeDepth();
    }

    bool IsActive() const { return active; }
    CModFalloutProjectionScope(const CModFalloutProjectionScope &) = delete;
    CModFalloutProjectionScope &operator=(const CModFalloutProjectionScope &) = delete;
};
