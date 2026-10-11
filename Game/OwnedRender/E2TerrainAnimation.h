#pragma once
#include <cstddef>

namespace OwnedRender {
bool E2TerrainAnimationOwned() noexcept;

// Keep the game's phase and RNG order. Only the UV/wave vertex work transfers
// to c56. Validate the whole packet before consuming RNG or changing a phase.
template<class Tile, class Random, class Changed>
bool E2AdvanceTerrainTiles(Tile* tiles, std::size_t count, int segment,
                          bool animated, Random random, Changed changed) {
    if (count > 65536 || (count && !tiles)) return false;
    for (std::size_t i = 0; i < count; ++i)
        if (tiles[i].nCurPhase < 0 || tiles[i].nPhasesTotal <= 0 ||
            tiles[i].nCurPhase >= tiles[i].nPhasesTotal) return false;
    if (segment % 3 || !animated) return true;
    for (std::size_t i = 0; i < count; ++i) {
        auto& tile = tiles[i];
        if (tile.nCurPhase == 0 && tile.nAnimProbability < 1000 &&
            random() > tile.nAnimProbability) continue;
        tile.nCurPhase = (tile.nCurPhase + 1) % tile.nPhasesTotal;
        changed();
    }
    return true;
}
}
