#pragma once
#include <array>
#include <memory>
#include <string>

namespace owned::gltf {
using SecondaryPoint = std::array<float,3>;
constexpr unsigned body_parts=8, hair_groups=4, hair_points=5;
inline constexpr float secondary_limit=.25f;
inline constexpr double secondary_step=1./120.;
struct SecondaryTargets {
    std::array<SecondaryPoint,hair_groups*hair_points> hair{};
};
struct SecondaryPose {
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
