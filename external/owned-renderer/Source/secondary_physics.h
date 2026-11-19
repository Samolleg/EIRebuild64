#pragma once
#include <array>
#include <memory>
#include <string>

namespace owned::gltf {
using SecondaryPoint = std::array<float,3>;
constexpr unsigned body_parts=8, hair_groups=4, hair_points=5;
inline constexpr float secondary_limit=.25f;
inline constexpr double secondary_step=1./120.;
struct SecondarySegment { SecondaryPoint pivot{}, end{}, lateral{}; };
struct SecondaryTargets {
    // Four joints on each side, ordered base, middle, tip, glute.
    std::array<SecondarySegment,body_parts> body{};
    std::array<SecondaryPoint,hair_groups*hair_points> hair{};
    bool has_hair=false;
};
struct SecondaryDelta {
    SecondaryPoint shift{};
    std::array<float,4> rotation{0,0,0,1};
};
struct SecondaryPose {
    std::array<SecondaryDelta,body_parts> body{};
    std::array<SecondaryPoint,hair_groups*hair_points> hair{};
};
struct SecondaryPhysics {
    virtual ~SecondaryPhysics()=default;
    virtual SecondaryPose advance(double clock,const SecondaryTargets& targets,bool reset)=0;
    virtual bool ready() const {return true;}
    virtual const std::string& error() const=0;
};
using SecondaryFactory=std::shared_ptr<SecondaryPhysics>(*)();
inline SecondaryFactory secondary_factory=nullptr;
}
namespace PhysicsFx {std::shared_ptr<owned::gltf::SecondaryPhysics> CreateCharacterPhysics();}
