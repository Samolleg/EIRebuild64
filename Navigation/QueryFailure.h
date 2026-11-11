#pragma once
#include "Types.h"
#include <algorithm>

namespace Navigation {
// Own every semantic input: caller-owned Location pointers and mutable cost
// arrays cannot be retained across queries. Scheduling state is not a key.
struct QueryFailure {
    Request request;
    Location start, goalSupport;
    bool hasStart = false, hasGoalSupport = false;
    std::uint64_t after = 0, generation = 0, revision = 0;
    int profile = 0, aiClass = 0;
    Status status = Status::NoPath;

    static bool Cacheable(Status value)
    {
        // A time/node/allocation limit is not a fact about reachability.
        return value == Status::NoGoal || value == Status::NoPath || value == Status::TooLong;
    }
    QueryFailure(const Request& input, std::uint64_t deadline, std::uint64_t meshGeneration,
                 std::uint64_t sceneRevision, int profileId, int materialClass, Status result)
        : request(input), hasStart(input.start != nullptr), hasGoalSupport(input.goalSupport != nullptr),
          after(deadline), generation(meshGeneration), revision(sceneRevision),
          profile(profileId), aiClass(materialClass), status(result)
    {
        if (input.start) start = *input.start;
        if (input.goalSupport) goalSupport = *input.goalSupport;
        request.start = request.goalSupport = nullptr;
        request.obstacles = nullptr; request.reuseRoute = nullptr;
        request.sliced = nullptr; request.frameBudgetMs = nullptr; request.ensureProgress = false;
    }
    static bool Same(Point a, Point b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
    static bool Same(const Location& a, const Location& b)
    {
        return Same(a.position, b.position) && a.surface == b.surface && a.radius == b.radius &&
            a.height == b.height && a.area == b.area && a.polygon == b.polygon && a.generation == b.generation;
    }
    bool Matches(const Request& next, std::uint64_t now, std::uint64_t meshGeneration,
                 std::uint64_t sceneRevision, int profileId, int materialClass) const
    {
        return Cacheable(status) && !next.obstacles && !next.reuseRoute && now < after &&
            generation == meshGeneration && revision == sceneRevision && profile == profileId && aiClass == materialClass &&
            Same(request.source, next.source) && Same(request.goal, next.goal) &&
            request.height == next.height && request.goalRadius == next.goalRadius && request.maxLength == next.maxLength &&
            request.goalPolicy == next.goalPolicy && request.excludedFlags == next.excludedFlags &&
            std::equal(request.areaCosts, request.areaCosts + 16, next.areaCosts) &&
            hasStart == (next.start != nullptr) && (!hasStart || Same(start, *next.start)) &&
            hasGoalSupport == (next.goalSupport != nullptr) && (!hasGoalSupport || Same(goalSupport, *next.goalSupport));
    }
};
}
