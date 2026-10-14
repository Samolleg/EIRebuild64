#include "StdAfx.h"
#include "CameraTerrainCollision.h"
#include "Camera.h"
#include "WorldClient.h"
#include "Terrain.h"
#include "GeneralSettings.h"
#include <cmath>
#include <cfloat>

// Adapted from addon 80acb93 / camera 0.12.0.6: vertical avoidance,
// unchanged orientation and selected distance, QPC altitude damping.
namespace CameraTerrainCollision {
namespace {


// Bound the work even for unusually large mod camera distances. Normal and
// telephoto arms retain at most half a metre between terrain probes.
const float ProbeRadius = 0.75f;
const float ArmProbeRadius = 0.25f;
const float ArmClearance = 0.25f;
const float ProbeStep = 0.5f;
const float ContactSkin = 0.05f;
const float RetractionSmoothTime = 0.12f;
const float RecoverySmoothTime = 0.35f;
const int NavigationRadius = 6;
const int MaxProbeCount = 1024;
const DWORD AuthoredCameraFlags = CAM_FPS_MODE | CAM_USE_PLACEMENTS | CAM_CANT_SCROLL;

struct ArmState
{
    CWorldClient *world = nullptr;
    void *terrain = nullptr;
    CCamera *camera = nullptr;
    double time = 0.0;
    float focusHeight = 0.0f;
    float focusHeightVelocity = 0.0f;
    CPlot3D focus;
};

ArmState orbitArm;
ArmState extendedArm;

bool Finite(float value)
{
    return std::isfinite(value);
}

bool Finite(const CPlot3D &point)
{
    return Finite(point.x) && Finite(point.y) && Finite(point.z);
}

float Clamp(float value, float low, float high)
{
    return value < low ? low : (value > high ? high : value);
}

double ClockSeconds()
{
    static const double frequency = []() {
        LARGE_INTEGER value;
        return QueryPerformanceFrequency(&value) ?
            static_cast<double>(value.QuadPart) : 0.0;
    }();
    LARGE_INTEGER value;
    if (frequency > 0.0 && QueryPerformanceCounter(&value))
        return static_cast<double>(value.QuadPart) / frequency;
    return static_cast<double>(GetTickCount64()) * 0.001;
}

void Damp(float &value, float &velocity, float target, float seconds, float smoothTime)
{
    // Exact critically damped response: changing terrain targets do not reset
    // velocity or introduce the instantaneous speed change of a first-order lerp.
    const float omega = 2.0f / smoothTime;
    const float error = value - target;
    const float change = velocity + omega * error;
    const float decay = expf(-omega * seconds);
    value = target + (error + change * seconds) * decay;
    velocity = (velocity - omega * change * seconds) * decay;
    if (error * (value - target) < 0.0f)
    {
        value = target;
        velocity = 0.0f;
    }
}

void EnforceFloor(float &value, float &velocity, float minimum)
{
    if (value < minimum)
    {
        value = minimum;
        if (velocity < 0.0f)
            velocity = 0.0f;
    }
}

struct CachedHeight
{
    int x, y;
    float height;
    bool valid;
};

struct NavigationField
{
    static const unsigned RawCacheSize = 8192;
    static const unsigned NavigationCacheSize = 4096;
    CWorldClient *world = nullptr;
    void *terrain = nullptr;
    float maxX = 0.0f, maxY = 0.0f;
    CachedHeight raw[RawCacheSize] = {};
    CachedHeight raised[NavigationCacheSize] = {};

    void Reset()
    {
        world = nullptr;
        terrain = nullptr;
        for (auto &entry : raw)
            entry.valid = false;
        for (auto &entry : raised)
            entry.valid = false;
    }

    void Bind(CWorldClient *newWorld, float newMaxX, float newMaxY)
    {
        if (world == newWorld && terrain == newWorld->GetTerrain() &&
            maxX == newMaxX && maxY == newMaxY)
            return;
        Reset();
        world = newWorld;
        terrain = newWorld->GetTerrain();
        maxX = newMaxX;
        maxY = newMaxY;
    }

    static unsigned Hash(int x, int y)
    {
        return static_cast<unsigned>(x) * 73856093u ^
            static_cast<unsigned>(y) * 19349663u;
    }

    bool Raw(int x, int y, float &height)
    {
        CachedHeight &entry = raw[Hash(x, y) & (RawCacheSize - 1)];
        if (entry.valid && entry.x == x && entry.y == y)
        {
            height = entry.height;
            return true;
        }
        height = world->GetSurfaceValue(Clamp(static_cast<float>(x), 0.0f, maxX),
            Clamp(static_cast<float>(y), 0.0f, maxY));
        if (!Finite(height))
            return false;
        entry = {x, y, height, true};
        return true;
    }

    bool Raised(int x, int y, float &height)
    {
        CachedHeight &entry = raised[Hash(x, y) & (NavigationCacheSize - 1)];
        if (entry.valid && entry.x == x && entry.y == y)
        {
            height = entry.height;
            return true;
        }
        float base;
        if (!Raw(x, y, base))
            return false;
        height = base;
        const float radiusSquared = static_cast<float>(NavigationRadius * NavigationRadius);
        for (int dx = -NavigationRadius; dx <= NavigationRadius; ++dx)
            for (int dy = -NavigationRadius; dy <= NavigationRadius; ++dy)
            {
                const int distanceSquared = dx * dx + dy * dy;
                if (distanceSquared == 0 || distanceSquared >= radiusSquared)
                    continue;
                float nearby;
                if (!Raw(x + dx, y + dy, nearby))
                    return false;
                if (nearby <= base)
                    continue;
                const float t = distanceSquared / radiusSquared;
                const float remaining = 1.0f - t;
                // Quintic falloff has zero first/second derivatives at the
                // centre and boundary. Nearby banks fill a narrow hollow;
                // distant peaks disappear without a cutoff-height step.
                const float weight = remaining * remaining * remaining *
                    (1.0f + 3.0f * t + 6.0f * t * t);
                const float candidate = base + (nearby - base) * weight;
                if (candidate > height)
                    height = candidate;
            }
        entry = {x, y, height, true};
        return true;
    }

    static void Weights(float t, float (&weights)[4])
    {
        const float square = t * t, cube = square * t;
        const float remaining = 1.0f - t;
        weights[0] = remaining * remaining * remaining / 6.0f;
        weights[1] = (3.0f * cube - 6.0f * square + 4.0f) / 6.0f;
        weights[2] = (-3.0f * cube + 3.0f * square + 3.0f * t + 1.0f) / 6.0f;
        weights[3] = cube / 6.0f;
    }

    bool Height(float x, float y, float actualHeight, float &height)
    {
        const int cellX = static_cast<int>(floorf(x));
        const int cellY = static_cast<int>(floorf(y));
        float weightsX[4], weightsY[4];
        Weights(x - cellX, weightsX);
        Weights(y - cellY, weightsY);
        float curved = 0.0f;
        for (int dx = 0; dx < 4; ++dx)
            for (int dy = 0; dy < 4; ++dy)
            {
                float sample;
                if (!Raised(cellX + dx - 1, cellY + dy - 1, sample))
                    return false;
                curved += sample * weightsX[dx] * weightsY[dy];
            }
        // Cubic B-spline interpolation avoids cell-edge kinks. Its positive
        // weights cannot overshoot neighbouring heights; this fresh raw query
        // additionally guarantees that smoothing never cuts down real terrain.
        height = curved > actualHeight ? curved : actualHeight;
        return Finite(height);
    }
};

NavigationField navigation;

struct TerrainProbe
{
    CWorldClient *world;
    float maxX, maxY, clearance;

    bool Create(CWorldClient *newWorld, CCamera *camera)
    {
        if (!newWorld || newWorld != &theWorld || !newWorld->GetTerrain() ||
            !camera || camera != &theCamera || camera->CheckFlag(AuthoredCameraFlags))
            return false;

        // Shipped GetZValue clamps against these terrain bounds. GetSurfaceValue
        // does not clamp, so every query below must do so explicitly.
        maxX = newWorld->GetTerrain()->GetMaxX() - 1.0f;
        maxY = newWorld->GetTerrain()->GetMaxY() - 1.0f;
        if (!Finite(maxX) || !Finite(maxY) || maxX < 0.0f || maxY < 0.0f)
            return false;
        world = newWorld;
        navigation.Bind(world, maxX, maxY);
        clearance = 1.0f;
        if (Finite(theSettings.fpCameraMinHeight))
            clearance = Clamp(theSettings.fpCameraMinHeight, 1.0f, 4.0f);
        return true;
    }

    bool Surface(const CPlot3D &point, float radius, float &floor,
        bool raisedHeight = true) const
    {
        if (!Finite(point))
            return false;
        floor = -FLT_MAX;
        // A wide footprint protects the eye's near view. It must not inflate
        // every point of the focus-to-eye ray by the camera's comfort height.
        const int extent = radius > 0.0f ? 1 : 0;
        for (int x = -extent; x <= extent; ++x)
            for (int y = -extent; y <= extent; ++y)
            {
                const float px = Clamp(point.x + x * radius, 0.0f, maxX);
                const float py = Clamp(point.y + y * radius, 0.0f, maxY);
                float height = world->GetSurfaceValue(px, py);
                if (!Finite(height))
                    return false;
                float navigationHeight;
                if (raisedHeight && navigation.Height(px, py, height, navigationHeight))
                    height = navigationHeight;
                if (height > floor)
                    floor = height;
            }
        return Finite(floor);
    }

};

bool Resolve(const TerrainProbe &probe, CCamera *camera, ArmState &state,
    CPlot3D focus, CPlot3D desiredEye, bool focusAlreadySafe = false)
{
    float focusSurface, rawFocusSurface;
    if (!Finite(desiredEye) || !probe.Surface(focus, 0.0f, focusSurface) ||
        !probe.Surface(focus, 0.0f, rawFocusSurface, false))
        return false;
    const CPlot3D arm = desiredEye - focus;
    const float length = sqrtf(arm.x * arm.x + arm.y * arm.y + arm.z * arm.z);
    if (!Finite(length) || length < 0.0001f || length > MaxProbeCount * ProbeStep)
        return false;
    const int probes = static_cast<int>(ceilf(length / ProbeStep));
    if (probes > MaxProbeCount)
        return false;
    // GetTickCount holds the same value for several high-FPS frames, then
    // advances by an entire clock tick. Damping must advance every update,
    // alongside the native pan, rather than moving altitude in those steps.
    const double now = ClockSeconds();
    const double elapsed = now - state.time;
    const bool fresh = state.camera != camera || state.world != probe.world ||
        state.terrain != probe.world->GetTerrain() || elapsed < 0.0 || elapsed > 1.0;
    const float seconds = fresh ? 0.0f :
        Clamp(static_cast<float>(elapsed), 0.0f, 0.1f);

    float anchorHeight = camera->GetOrbitAnchorHeight();
    if (!Finite(anchorHeight) || anchorHeight < ArmClearance)
        anchorHeight = ArmClearance;
    float preferredLift = focusAlreadySafe ? 0.0f :
        Clamp(focusSurface + anchorHeight - focus.z, 0.0f, FLT_MAX);
    float requiredLift = Clamp(rawFocusSurface + ContactSkin - focus.z, 0.0f, FLT_MAX);
    for (int i = 1; i <= probes; ++i)
    {
        const float fraction = static_cast<float>(i) / probes;
        const CPlot3D point = focus + arm * fraction;
        float raisedFloor, rawFloor;
        if (!probe.Surface(point, ArmProbeRadius * fraction, raisedFloor) ||
            !probe.Surface(point, 0.0f, rawFloor, false))
            return false;
        const float preferred = raisedFloor + ArmClearance * fraction - point.z;
        const float required = rawFloor + ContactSkin * fraction - point.z;
        if (preferred > preferredLift)
            preferredLift = preferred;
        if (required > requiredLift)
            requiredLift = required;
    }
    float eyeSurface, rawEyeSurface;
    if (!probe.Surface(desiredEye, ProbeRadius, eyeSurface) ||
        !probe.Surface(desiredEye, ProbeRadius, rawEyeSurface, false))
        return false;
    const float preferredEyeLift = eyeSurface + probe.clearance - desiredEye.z;
    const float requiredEyeLift = rawEyeSurface + ContactSkin - desiredEye.z;
    if (preferredEyeLift > preferredLift)
        preferredLift = preferredEyeLift;
    if (requiredEyeLift > requiredLift)
        requiredLift = requiredEyeLift;

    // The raised envelope is a preferred translation height, not an invisible
    // wall that shortens the arm. Preserve native zoom and the complete camera
    // orientation; use one altitude for focus and eye so no aim compensation
    // can tilt a short arm toward vertical.
    const float desiredHeight = focus.z + preferredLift;
    if (fresh)
    {
        state.focusHeight = desiredHeight;
        state.focusHeightVelocity = 0.0f;
    }
    else
        Damp(state.focusHeight, state.focusHeightVelocity, desiredHeight, seconds,
            desiredHeight > state.focusHeight ? RetractionSmoothTime : RecoverySmoothTime);
    const float minimumHeight = focus.z + requiredLift;
    EnforceFloor(state.focusHeight, state.focusHeightVelocity, minimumHeight);
    const float lift = state.focusHeight - focus.z;
    focus.z += lift;
    desiredEye.z += lift;
    state.world = probe.world;
    state.terrain = probe.world->GetTerrain();
    state.camera = camera;
    state.time = now;
    state.focus = focus;

    camera->SetCP(desiredEye);
    return true;
}

} // namespace

void Reset()
{
    orbitArm = ArmState();
    extendedArm = ArmState();
    navigation.Reset();
}

void GuardEye(CWorldClient *world, CCamera *camera)
{
    TerrainProbe probe;
    float floor;
    if (!probe.Create(world, camera) || !probe.Surface(camera->GetCP(), ProbeRadius, floor, false) ||
        camera->GetCP().z >= floor + ContactSkin)
        return;
    CPlot3D position = camera->GetCP();
    position.z = floor + ContactSkin;
    camera->SetCP(position);
}

void ConstrainOrbit(CCamera *camera)
{
    TerrainProbe probe;
    if (!probe.Create(&theWorld, camera) ||
        !Resolve(probe, camera, orbitArm, camera->GetAnchorPoint(), camera->GetCP()))
    {
        orbitArm = ArmState();
        GuardEye(&theWorld, camera);
    }
}

void ConstrainExtendedArm(CCamera *camera)
{
    TerrainProbe probe;
    if (!probe.Create(&theWorld, camera))
    {
        extendedArm = ArmState();
        return;
    }
    const bool hasOrbitFocus = orbitArm.camera == camera && orbitArm.world == &theWorld &&
        orbitArm.terrain == theWorld.GetTerrain();
    const CPlot3D focus = hasOrbitFocus ? orbitArm.focus : camera->GetAnchorPoint();
    if (!Resolve(probe, camera, extendedArm, focus, camera->GetCP(), hasOrbitFocus))
    {
        extendedArm = ArmState();
        GuardEye(&theWorld, camera);
    }
}
} // namespace CameraTerrainCollision
