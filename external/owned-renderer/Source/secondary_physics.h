#pragma once
#include <array>
#include <memory>
#include <string>

namespace owned::gltf {
using SecondaryPoints = std::array<std::array<float,3>,2>;
inline constexpr float secondary_limit = .025f;
// Host-owned PhysX implementation. No renderer device or game-object pointers.
// A retained actor owns one instance; destroying it releases its physics scene.
struct SecondaryPhysics {
    virtual ~SecondaryPhysics() = default;
    virtual SecondaryPoints advance(double clock, const SecondaryPoints& targets, bool reset) = 0;
    virtual bool ready() const {return true;}
    virtual const std::string& error() const = 0;
};
using SecondaryFactory = std::shared_ptr<SecondaryPhysics>(*)();
inline SecondaryFactory secondary_factory = nullptr;
}

namespace PhysicsFx {std::shared_ptr<owned::gltf::SecondaryPhysics> CreateCharacterPhysics();}
