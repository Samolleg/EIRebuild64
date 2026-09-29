#pragma once
#include "GroupSearchPolicy.h"
#include <cmath>
#include <limits>

#pragma pack(push, 8)
namespace GroupSearch
{
// Only a selected native DUTY CommandUnit may execute these decisions. This
// class neither selects motivations nor owns native pointers/command queues.
enum class Order { None, Move, Hold, Look, Release };
enum class Action { Native, Yield, Move, Stop, Turn, Keep, Arrived, Failed, Released };
struct Intent
{
    Order order = Order::None;
    uint32_t serial = 0;
    Point target;
    float tolerance = 1.5f;
    float angle = 0;
    bool requireNativeArrival = false;
    float nativeEndpointTolerance = 0.25f; // complete native route, never a partial path
    bool run = false; // transient command pace, derived from expedition phase
};
struct MotionInput
{
    int tick = 0;
    bool dutySelected = false;
    bool eligible = false;
    bool protectedCommand = false;
    bool moving = false;
    bool turning = false;
    bool failed = false;
    bool pathAtTarget = false;
    Point position;
};
inline bool Finite(Point p)
{ return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline float Distance2(Point a, Point b)
{ float x=a.x-b.x, y=a.y-b.y; return x*x+y*y; }

class Movement
{
    Intent current;
    bool haveOrder = false;
    bool acquired = false;
    bool issued = false;
    bool paused = false;
    bool failed = false;
    bool sawExactPath = false;
    int lastTick = -1;
    int stalled = 0;
    int retries = 0;
    int retryWait = 0;
    Point progressAnchor;
    const char *reason = "none";
public:
    // ~15 active seconds without progress, three route attempts, at least one
    // second between attempts. Combat/sleep gaps do not spend these budgets.
    static constexpr int StallTicks = 273;
    static constexpr int RetryTicks = 19;
    static constexpr int MaxAttempts = 3;
    bool OwnsCommand() const { return acquired; }
    bool IsPaused() const { return paused; }
    bool HasFailed() const { return failed; }
    const Intent &Current() const { return current; }
    const char *Reason() const { return reason; }
    void Reset() { *this = Movement(); }

    Action Step(const Intent &wanted, const MotionInput &in)
    {
        // Never stop, reset or rewrite an attack, skill, cast, fear, scripted
        // animation, foreign motivation or ineligible unit, including release.
        if (!in.dutySelected || !in.eligible || in.protectedCommand)
        {
            paused = acquired;
            lastTick = in.tick;
            return acquired ? Action::Yield : Action::Native;
        }
        if (wanted.order == Order::None || wanted.order == Order::Release)
        {
            const bool had = acquired;
            Reset();
            return had ? Action::Released : Action::Native;
        }
        if (!Finite(in.position) || !Finite(wanted.target) ||
            !std::isfinite(wanted.tolerance) || wanted.tolerance <= 0 ||
            !std::isfinite(wanted.angle) || !std::isfinite(wanted.nativeEndpointTolerance) ||
            wanted.nativeEndpointTolerance<=0)
        {
            // Bad intent is NOT a permission to write invalid coordinates.
            return acquired ? Action::Failed : Action::Native;
        }
        const bool changed = !haveOrder || wanted.serial != current.serial ||
            wanted.order != current.order || wanted.target.x != current.target.x ||
            wanted.target.y != current.target.y || wanted.target.z != current.target.z ||
            wanted.tolerance != current.tolerance || wanted.angle != current.angle ||
            wanted.requireNativeArrival != current.requireNativeArrival ||
            wanted.nativeEndpointTolerance != current.nativeEndpointTolerance || wanted.run != current.run;
        if (changed)
        {
            current = wanted; haveOrder = true; issued = false; failed = false;
            sawExactPath=false;
            stalled = retries = retryWait = 0;
            progressAnchor = in.position;
        }
        // A gap means the selected duty did not own the intermediate ticks.
        const bool interrupted = lastTick >= 0 && (in.tick < lastTick || in.tick-lastTick > 2);
        const int elapsed = lastTick >= 0 && in.tick > lastTick &&
            in.tick - lastTick <= 2 && !paused ? in.tick-lastTick : 0;
        lastTick = in.tick;
        if (paused || interrupted)
        { issued = false; paused = false; retries = retryWait = stalled = 0; progressAnchor=in.position;sawExactPath=false; }
        acquired = true;
        if (failed) return Action::Failed;
        if (wanted.order == Order::Hold)
        {
            if (!issued) { issued = true; return Action::Stop; }
            return Action::Keep;
        }
        if (wanted.order == Order::Look)
        {
            if (!issued) { issued = true; return Action::Turn; }
            return Action::Keep;
        }
        const float distance2 = Distance2(in.position, wanted.target);
        // Native L1StandStill clears path on completion. Remember a matching
        // endpoint while that path is live, scoped to this order and ownership.
        if(in.moving && in.pathAtTarget)sawExactPath=true;
        if (!std::isfinite(distance2)) { failed = true; return Action::Failed; }
        // Compare like-for-like native AI coordinates. Terrain/world elevation
        // is not unit GetCP().z; native pathfinding owns ledges and walkability.
        if (distance2 <= wanted.tolerance*wanted.tolerance &&
            std::fabs(in.position.z-wanted.target.z) <= 2.0f &&
            (!wanted.requireNativeArrival || distance2<0.000001f ||
                (issued && !in.moving && !in.turning && !in.failed && sawExactPath)))
        {
            issued = true; reason="arrival";
            return Action::Arrived;
        }
        // A real detour can temporarily increase distance to the goal. Measure
        // displacement, not Euclidean approach; expedition deadline bounds loops.
        if (Distance2(in.position,progressAnchor) >= 0.25f)
        { progressAnchor=in.position; stalled = 0; }
        else stalled += elapsed;
        retryWait = (std::max)(0, retryWait-elapsed);
        if (!issued || (!in.moving && !in.turning) || in.failed || stalled >= StallTicks)
        {
            if (issued && retryWait > 0) return Action::Keep;
            reason=!issued?"new-order":in.failed?"native-failed":
                (!in.moving&&!in.turning)?"stopped-short":"no-displacement";
            if (retries >= MaxAttempts) { failed = true; return Action::Failed; }
            ++retries; issued = true; stalled = 0; retryWait = RetryTicks;sawExactPath=false;
            return Action::Move;
        }
        return Action::Keep;
    }
};

// The very same dispatch is used by the game adapter and the headless fake
// command queue. Yield/Native must have NO command side effects.
template<class Commands>
Action Execute(Movement &movement, const Intent &intent,
               const MotionInput &input, Commands &commands)
{
    const Action action = movement.Step(intent, input);
    switch (action)
    {
    case Action::Move: commands.Move(intent.target); break;
    case Action::Turn: commands.Turn(intent.angle); break;
    case Action::Stop: case Action::Arrived: case Action::Failed:
    case Action::Released: commands.Stop(); break;
    default: break;
    }
    return action;
}
}
#pragma pack(pop)
