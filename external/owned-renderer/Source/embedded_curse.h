#pragma once
#include <array>
#include <cstdint>
namespace owned {
struct EmbeddedCurseActor {
    std::uint64_t incarnation=0;
    std::array<float,3> base{};
    bool present=false,channel=false;
};
// Synchronous typed scene snapshot; no script store, actor pointers or x86 ABI.
struct EmbeddedCurseInput {
    std::uint64_t world_epoch=0,incarnation=0;
    int boss_id=0,tick=0,threat=0,mage_mode=0;
    bool active=false;
    float threat_born=0,x=0,y=0,pad_x=0,pad_y=0;
    float lance_ax=0,lance_ay=0,lance_bx=0,lance_by=0;
    float impact_at=0,impact_x=0,impact_y=0,ward_at=0;
    EmbeddedCurseActor boss,mage,ward;
    std::array<EmbeddedCurseActor,12> enemies{};
    std::array<float,3> camera{};
    std::array<float,16> world_to_clip{};
    float (*ground)(float,float)=nullptr;
};
struct EmbeddedCurseCache {
    std::uint64_t serial=0;
    std::array<std::uint32_t,64> slots{};
};
}
