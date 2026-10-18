#include "StdAfx.h"
#include "CameraTerrainCollision.h"
#include "Camera.h"
#include "WorldClient.h"
#include "Terrain.h"
#include "GeneralSettings.h"
#include <cmath>
#include <cfloat>

// Adapted from addon d3b0896: vertical avoidance,
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
const float ComfortSmoothTime = 0.5f;
const float CollisionSmoothTime = 0.12f;
const float MotionSmoothTime = 0.08f;
const float PanReleaseTime = 0.32f; // native zero threshold settles typical pans in about 1 s
const float UrgencySmoothTime = 0.08f;
const float PredictionTime = 0.6f;
const float MaxPredictionDistance = 24.0f;
const float PredictionClearance = 0.5f;
const int PredictionSamples = 4;
const int MaxPredictionProbes = 64;
const int NavigationRadius = 8;
const int MaxProbeCount = 1024;
const DWORD AuthoredCameraFlags = CAM_FPS_MODE | CAM_USE_PLACEMENTS | CAM_CANT_SCROLL;

struct ArmState
{
    CWorldClient *world = nullptr;
    void *terrain = nullptr;
    CCamera *camera = nullptr;
    LONGLONG time = 0;
    float focusHeight = 0.0f;
    float focusHeightVelocity = 0.0f;
    float collisionUrgency = 0.0f;
    CPlot3D inputFocus;
    CPlot3D inputEye;
    CPlot3D focusVelocity;
    CPlot3D eyeVelocity;
    CPlot3D focus;
};

ArmState orbitArm;
ArmState extendedArm;
ArmState campOrbitArm;
ArmState campExtendedArm;
struct FrameClock
{
    void *terrain = nullptr;
    LONGLONG time = 0;
    float remaining = 0.0f, seconds = 0.0f;
    bool eligible = false;
};
FrameClock frameClock;

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

struct TimeSource
{
    bool performance;
    LONGLONG frequency, last;

    TimeSource()
    {
        LARGE_INTEGER rate, value;
        performance = QueryPerformanceFrequency(&rate) && rate.QuadPart > 0 &&
            QueryPerformanceCounter(&value);
        frequency = performance ? rate.QuadPart : 1000;
        last = performance ? value.QuadPart : static_cast<LONGLONG>(GetTickCount64());
    }
};

TimeSource &Timer()
{
    static TimeSource source;
    return source;
}

LONGLONG ClockTicks()
{
    TimeSource &source = Timer();
    LARGE_INTEGER value;
    if (!source.performance)
        source.last = static_cast<LONGLONG>(GetTickCount64());
    else if (QueryPerformanceCounter(&value))
        source.last = value.QuadPart;
    // A transient counter failure freezes time; never mix millisecond ticks
    // into an already selected performance-counter domain.
    return source.last;
}

double ElapsedSeconds(LONGLONG now, LONGLONG previous)
{
    // The game's x87 precision can round even double computations to float
    // precision. Subtract integer ticks first so uptime cannot quantize dt.
    const LONGLONG ticks = now - previous;
    return static_cast<double>(ticks) / static_cast<double>(Timer().frequency);
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
    // Retain momentum when a changing terrain target crosses the response.
    // Only real contact may constrain height/velocity, not a preferred altitude.
}

float HorizontalLength(const CPlot3D &point)
{
    return sqrtf(point.x * point.x + point.y * point.y);
}

void UpdateMotion(CPlot3D &velocity, const CPlot3D &current,
    const CPlot3D &previous, float seconds)
{
    if (seconds <= 0.00001f)
        return;
    CPlot3D measured = (current - previous) * (1.0f / seconds);
    measured.z = 0.0f;
    const float blend = 1.0f - expf(-seconds / MotionSmoothTime);
    velocity += (measured - velocity) * blend;
}

float CollisionUrgency(float clearanceDeficit)
{
    const float t = Clamp(clearanceDeficit, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
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

struct ArmHeights
{
    float preferred, minimum;
};

bool MeasureArm(const TerrainProbe &probe, const CPlot3D &focus,
    const CPlot3D &eye, float anchorHeight, bool focusAlreadySafe,
    bool prediction, ArmHeights &heights)
{
    float focusSurface, rawFocusSurface;
    if (!Finite(eye) || !probe.Surface(focus, 0.0f, focusSurface) ||
        !probe.Surface(focus, 0.0f, rawFocusSurface, false))
        return false;
    const CPlot3D arm = eye - focus;
    const float length = sqrtf(arm.x * arm.x + arm.y * arm.y + arm.z * arm.z);
    if (!Finite(length) || length < 0.0001f || length > MaxProbeCount * ProbeStep)
        return false;
    int probes = static_cast<int>(ceilf(length / ProbeStep));
    if (prediction && probes > MaxPredictionProbes)
        probes = MaxPredictionProbes;

    heights.preferred = focus.z;
    if (!focusAlreadySafe && focusSurface + anchorHeight > heights.preferred)
        heights.preferred = focusSurface + anchorHeight;
    // This is an absolute geometric floor, not a nonnegative lift from the
    // native anchor. A rising native anchor must not bypass safe height damping.
    heights.minimum = rawFocusSurface + ContactSkin;
    for (int i = 1; i <= probes; ++i)
    {
        const float fraction = static_cast<float>(i) / probes;
        const CPlot3D point = focus + arm * fraction;
        float raisedFloor, rawFloor;
        // The broad navigation field supplies preview width. Keep the current
        // arm's complete clearance cone and dense raw safety trace unchanged.
        const float radius = prediction ? 0.0f : ArmProbeRadius * fraction;
        if (!probe.Surface(point, radius, raisedFloor) ||
            !probe.Surface(point, 0.0f, rawFloor, false))
            return false;
        const float preferred = raisedFloor + ArmClearance * fraction - arm.z * fraction;
        const float required = rawFloor + ContactSkin * fraction - arm.z * fraction;
        if (preferred > heights.preferred)
            heights.preferred = preferred;
        if (required > heights.minimum)
            heights.minimum = required;
    }
    float eyeSurface, rawEyeSurface;
    if (!probe.Surface(eye, ProbeRadius, eyeSurface) ||
        !probe.Surface(eye, ProbeRadius, rawEyeSurface, false))
        return false;
    const float preferredEyeHeight = eyeSurface + probe.clearance - arm.z;
    const float requiredEyeHeight = rawEyeSurface + ContactSkin - arm.z;
    if (preferredEyeHeight > heights.preferred)
        heights.preferred = preferredEyeHeight;
    if (requiredEyeHeight > heights.minimum)
        heights.minimum = requiredEyeHeight;
    return Finite(heights.preferred) && Finite(heights.minimum);
}

bool Resolve(const TerrainProbe &probe, CCamera *camera, ArmState &state,
    CPlot3D focus, CPlot3D desiredEye, bool focusAlreadySafe = false)
{
    float anchorHeight = camera->GetOrbitAnchorHeight();
    if (!Finite(anchorHeight) || anchorHeight < ArmClearance)
        anchorHeight = ArmClearance;
    ArmHeights current;
    if (!MeasureArm(probe, focus, desiredEye, anchorHeight, focusAlreadySafe, false, current))
        return false;
    // GetTickCount holds the same value for several high-FPS frames, then
    // advances by an entire clock tick. Damping must advance every update,
    // alongside the native pan, rather than moving altitude in those steps.
    const LONGLONG now = ClockTicks();
    const double elapsed = ElapsedSeconds(now, state.time);
    const bool fresh = state.camera != camera || state.world != probe.world ||
        state.terrain != probe.world->GetTerrain() || elapsed < 0.0 || elapsed > 1.0 ||
        HorizontalLength(focus - state.inputFocus) > MaxPredictionDistance * 2.0f;
    const float seconds = fresh ? 0.0f :
        Clamp(static_cast<float>(elapsed), 0.0f, 0.1f);
    const float motionSeconds = fresh ? 0.0f : static_cast<float>(elapsed);

    float desiredHeight = current.preferred;
    float previewTimes[PredictionSamples] = {};
    float previewMinimums[PredictionSamples] = {};
    int previews = 0;
    if (fresh)
    {
        state.focusVelocity = CPlot3D();
        state.eyeVelocity = CPlot3D();
        state.collisionUrgency = 0.0f;
    }
    else
    {
        // Displacement spans the actual interval, even when the spring step
        // is capped after a stall. Dividing by that cap exaggerates pan speed.
        UpdateMotion(state.focusVelocity, focus, state.inputFocus, motionSeconds);
        UpdateMotion(state.eyeVelocity, desiredEye, state.inputEye, motionSeconds);
        const float focusSpeed = HorizontalLength(state.focusVelocity);
        const float eyeSpeed = HorizontalLength(state.eyeVelocity);
        const float speed = focusSpeed > eyeSpeed ? focusSpeed : eyeSpeed;
        if (Finite(speed) && speed > 0.01f)
        {
            // Keep time honest when limiting distance: at high pan speed the
            // 24 m horizon is reached sooner, so the spring must react sooner.
            const float horizon = Clamp(MaxPredictionDistance / speed, 0.0f, PredictionTime);
            for (int i = 1; i <= PredictionSamples; ++i)
            {
                const float ahead = horizon * i / PredictionSamples;
                ArmHeights preview;
                if (!MeasureArm(probe, focus + state.focusVelocity * ahead,
                    desiredEye + state.eyeVelocity * ahead, anchorHeight,
                    focusAlreadySafe, true, preview))
                    continue;
                const float previewHeight = preview.preferred > preview.minimum + PredictionClearance ?
                    preview.preferred : preview.minimum + PredictionClearance;
                if (previewHeight > desiredHeight)
                    desiredHeight = previewHeight;
                previewTimes[previews] = ahead;
                previewMinimums[previews++] = preview.minimum;
            }
        }
    }

    // The raised envelope is a preferred translation height, not an invisible
    // wall that shortens the arm. Preserve native zoom and the complete camera
    // orientation; use one altitude for focus and eye so no aim compensation
    // can tilt a short arm toward vertical.
    if (fresh)
    {
        state.focusHeight = desiredHeight;
        state.focusHeightVelocity = 0.0f;
    }
    else
    {
        float normalHeight = state.focusHeight, normalVelocity = state.focusHeightVelocity;
        Damp(normalHeight, normalVelocity, desiredHeight, seconds, ComfortSmoothTime);
        float urgency = CollisionUrgency(current.minimum + ContactSkin - normalHeight);
        for (int i = 0; i < previews; ++i)
        {
            float predictedHeight = state.focusHeight, predictedVelocity = state.focusHeightVelocity;
            Damp(predictedHeight, predictedVelocity, desiredHeight, previewTimes[i], ComfortSmoothTime);
            const float risk = CollisionUrgency(previewMinimums[i] + ContactSkin - predictedHeight);
            if (risk > urgency)
                urgency = risk;
        }
        const float blend = 1.0f - expf(-seconds / UrgencySmoothTime);
        state.collisionUrgency += (urgency - state.collisionUrgency) * blend;
        const float smoothTime = ComfortSmoothTime +
            (CollisionSmoothTime - ComfortSmoothTime) * state.collisionUrgency;
        Damp(state.focusHeight, state.focusHeightVelocity, desiredHeight, seconds, smoothTime);
    }
    EnforceFloor(state.focusHeight, state.focusHeightVelocity, current.minimum);
    const float lift = state.focusHeight - focus.z;
    state.inputFocus = focus;
    state.inputEye = desiredEye;
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
    frameClock = FrameClock();
    DiscardCampState();
    orbitArm = ArmState();
    extendedArm = ArmState();
    navigation.Reset();
}

void BeginFrame(bool active, DWORD quantum, float coefficient)
{
    const LONGLONG now = ClockTicks();
    const bool eligible = active && theWorld.GetTerrain() &&
        !theCamera.CheckFlag(AuthoredCameraFlags) && quantum > 0 &&
        Finite(coefficient) && coefficient > 0.0f;
    const double elapsed = ElapsedSeconds(now, frameClock.time);
    const bool continuous = eligible && frameClock.eligible &&
        frameClock.terrain == theWorld.GetTerrain() && elapsed >= 0.0;
    frameClock.remaining = frameClock.seconds = 0.0f;
    if (continuous)
    {
        const double limit = static_cast<double>(quantum) * 0.005;
        const double seconds = elapsed < limit ? elapsed : limit;
        frameClock.remaining = static_cast<float>(seconds * 1000.0 * coefficient);
        frameClock.seconds = static_cast<float>(seconds);
    }
    frameClock.terrain = theWorld.GetTerrain();
    frameClock.time = now;
    frameClock.eligible = eligible;
}

void AdvanceCamera(CCamera *camera, float nativeStep)
{
    if (frameClock.eligible && camera == &theCamera &&
        frameClock.terrain == theWorld.GetTerrain() &&
        !camera->CheckFlag(AuthoredCameraFlags))
    {
        const float step = frameClock.remaining, seconds = frameClock.seconds;
        frameClock.remaining = frameClock.seconds = 0.0f;
        camera->DampReleasedPan(seconds, PanReleaseTime);
        camera->Step(step);
    }
    else
    {
        frameClock = FrameClock();
        camera->Step(nativeStep);
    }
}

void SaveCampState(CWorldClient *world, CCamera *camera)
{
    campOrbitArm = world && orbitArm.world == world && orbitArm.camera == camera &&
        orbitArm.terrain == world->GetTerrain() ? orbitArm : ArmState();
    campExtendedArm = world && extendedArm.world == world && extendedArm.camera == camera &&
        extendedArm.terrain == world->GetTerrain() ? extendedArm : ArmState();
}

void RestoreCampState(CWorldClient *world, CCamera *camera)
{
    orbitArm = world && campOrbitArm.world == world && campOrbitArm.camera == camera &&
        campOrbitArm.terrain == world->GetTerrain() ? campOrbitArm : ArmState();
    extendedArm = world && campExtendedArm.world == world && campExtendedArm.camera == camera &&
        campExtendedArm.terrain == world->GetTerrain() ? campExtendedArm : ArmState();
    const LONGLONG now = ClockTicks();
    ArmState *arms[] = { &orbitArm, &extendedArm };
    for (ArmState *arm : arms)
    {
        // Keep the exact lifted altitude, without charging camp time to the
        // spring or predicting movement from an input that has now stopped.
        arm->time = now;
        arm->focusHeightVelocity = 0.0f;
        arm->collisionUrgency = 0.0f;
        arm->focusVelocity = CPlot3D();
        arm->eyeVelocity = CPlot3D();
    }
    frameClock = FrameClock();
}

void DiscardCampState()
{
    campOrbitArm = ArmState();
    campExtendedArm = ArmState();
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
