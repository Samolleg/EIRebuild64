#pragma once
#include <cstdint>
#include <vector>
#include "retained_static_scene.h"

namespace OwnedRender {
void E1RestoreSavedWorldSource(const char* terrain, const char* mob,
    const std::vector<owned::RetainedAdmission>& members) noexcept;
// One MOB load reported to the renderer's retained static scene: the source
// opens before the game's loader runs and closes with its result. The
// renderer then loads the same MOB through its own resource runtime and
// publishes the static scene that shadows, point lamps and the static fast
// path are built from. The game passes names and numeric IDs only.
class E1MobSource {
public:
    // Main zone load (MPR terrain + MOB) or a secondary MOB added later.
    E1MobSource(const char* terrain, const char* mob) noexcept;
    explicit E1MobSource(const char* secondaryMob) noexcept;
    ~E1MobSource();
    void Succeeded(bool value) noexcept { success = value; }

    // The MOB parser ran for this name inside the open source.
    static void Parsed(const char* mob) noexcept;
    // One MOB record (NID, object type) entered the world with this ID
    // (-1 when the load mode excluded it).
    static void Admitted(std::uint32_t nid, std::uint32_t type, int id) noexcept;

private:
    E1MobSource(const E1MobSource&) = delete;
    E1MobSource& operator=(const E1MobSource&) = delete;
    std::uint64_t previousTransaction = 0;
    bool previousParsed = false;
    bool success = false;
};
}
