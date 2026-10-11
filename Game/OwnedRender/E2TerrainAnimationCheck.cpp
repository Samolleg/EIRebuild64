#include "E2TerrainAnimation.h"
#include "engine_bridge.h"
#include "embedded_sky.h"
#include <array>
#include <cstdio>
#include <vector>

namespace {
unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (false)
struct Tile { int nCurPhase, nPhasesTotal, nAnimProbability; };
struct Rng {
    unsigned value = 1;
    std::vector<int> calls;
    int Get() { value = value * 1664525u + 1013904223u; int n = 1 + value % 1000; calls.push_back(n); return n; }
};
}
int main() {
    int owner = 0;
    auto& sky = owned::embedded_sky();
    CHECK(!OwnedRender::E2TerrainAnimationOwned());
    owned::bridge_set_native_terrain_animation_owned(true);
    CHECK(!OwnedRender::E2TerrainAnimationOwned());
    CHECK(sky.attach(&owner));
    CHECK(OwnedRender::E2TerrainAnimationOwned());
    sky.configure(false); // Sky quality does not control terrain animation.
    CHECK(OwnedRender::E2TerrainAnimationOwned());
    owned::bridge_set_native_terrain_animation_owned(false);
    CHECK(!OwnedRender::E2TerrainAnimationOwned());
    owned::bridge_set_native_terrain_animation_owned(true);
    sky.detach(&owner);
    CHECK(!OwnedRender::E2TerrainAnimationOwned());
    // Mirror Renderer::release, which revokes the request before detaching.
    owned::bridge_set_native_terrain_animation_owned(false);
    CHECK(sky.attach(&owner));
    CHECK(!OwnedRender::E2TerrainAnimationOwned());
    sky.detach(&owner);

    std::array<Tile, 7> actual{{{0,4,0},{0,5,500},{1,3,1000},{0,1,1000},
        {0,2,-1},{0,6,999},{0,2,1001}}};
    auto reference = actual;
    Rng a, b;
    unsigned changed = 0, expected = 0;
    // Compare against the existing game's branch and RNG ordering, including
    // disabled intervals and repeated frame-zero phases. No GPU or game launch.
    for (int segment = 0; segment < 1000; ++segment) {
        const bool enabled = segment % 17 != 0;
        if (segment % 3 == 0 && enabled)
            for (auto& tile : reference) {
                if (tile.nCurPhase == 0)
                    if (tile.nAnimProbability < 1000)
                        if (b.Get() > tile.nAnimProbability) continue;
                tile.nCurPhase = (tile.nCurPhase + 1) % tile.nPhasesTotal;
                ++expected;
            }
        CHECK(OwnedRender::E2AdvanceTerrainTiles(actual.data(), actual.size(), segment,
            enabled, [&] { return a.Get(); }, [&] { ++changed; }));
        CHECK(a.calls == b.calls && changed == expected);
        for (std::size_t i = 0; i < actual.size(); ++i)
            CHECK(actual[i].nCurPhase == reference[i].nCurPhase);
    }
    int rngCalls = 0, revisions = 0;
    auto rng = [&] { ++rngCalls; return 1; };
    auto revision = [&] { ++revisions; };
    Tile invalid[]{{0,3,500},{3,3,1000}};
    CHECK(!OwnedRender::E2AdvanceTerrainTiles(invalid, 2, 0, true, rng, revision));
    CHECK(!rngCalls && !revisions && invalid[0].nCurPhase == 0);
    invalid[1] = {0,0,1000};
    CHECK(!OwnedRender::E2AdvanceTerrainTiles(invalid, 2, 1, false, rng, revision));
    invalid[1] = {-1,3,1000};
    CHECK(!OwnedRender::E2AdvanceTerrainTiles(invalid, 2, 0, true, rng, revision));
    CHECK(!OwnedRender::E2AdvanceTerrainTiles(static_cast<Tile*>(nullptr), 1, 0, true, rng, revision));
    CHECK(!OwnedRender::E2AdvanceTerrainTiles(invalid, 65537, 0, true, rng, revision));
    CHECK(OwnedRender::E2AdvanceTerrainTiles(static_cast<Tile*>(nullptr), 0, 0, true, rng, revision));
    CHECK(!rngCalls && !revisions && invalid[0].nCurPhase == 0);
    std::printf("E2 terrain animation: %u checks PASS\n", checks);
    return 0;
}
