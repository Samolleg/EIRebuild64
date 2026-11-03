#pragma once
#include "Query.h"
#include <array>

namespace Navigation {
// The channel thread may allocate the independent query before it is handed
// to the game thread. Only the game thread attaches it to a live mesh or uses it.
class LiveQuery {
    struct State;
    std::unique_ptr<State> state;
public:
    struct Result {
        Status status = Status::Invalid;
        Location source{}, goal{};
        std::vector<Location> points;
        bool truncated = false;
    };
    LiveQuery(Point source, const Point* goal, std::unique_ptr<dtNavMeshQuery, QueryDeleter> query);
    ~LiveQuery();
    LiveQuery(const LiveQuery&) = delete;
    // One bounded stage or a small number of Detour iterations. Caller verifies
    // the mesh identity and revision before each call, between game frames.
    bool Step(const Mesh& mesh);
    const Result& Get() const noexcept;
};
}
