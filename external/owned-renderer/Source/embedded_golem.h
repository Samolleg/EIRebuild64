#pragma once
#include <array>
#include <cstdint>
#include <memory>
namespace owned {
struct EmbeddedGolemField {float x=0,y=0,z=0,r=0,born=0,end=0,style=0;};
struct EmbeddedGolemShot {float x=0,y=0,z=0,ex=0,ey=0,ez=0,born=0,end=0,hand=0,stage=0;std::array<float,4> q{};};
struct EmbeddedGolemNode {std::array<float,3> position{};std::array<float,4> rotation{};bool valid=false;};
struct EmbeddedGolemReflection {float born=0,end=0;int kind=0;std::array<float,3> start{},target{};};
inline constexpr const char* EmbeddedRubyBones[]={"root","cage0","cage1","cage2","cage3","core","hip0","shin0","paw0","jaw00","jaw01","hip1","shin1","paw1","jaw10","jaw11","hip2","shin2","paw2","hip3","shin3","paw3","shaft","crown"};
struct EmbeddedRubyActor {bool present=false,dead=false;std::array<EmbeddedGolemNode,24> nodes{};std::array<float,3> base{};};
struct EmbeddedGolemInput {
 std::uint64_t world_epoch=0,incarnation=0;int boss_id=0,tick=0,stage=0,phase=0;
 bool active=false,walls=false,dead=false,throw_launched=false,hard=false;int throw_count=0;
 float timer=0,target_x=0,target_y=0,beam_ex=0,beam_ey=0,aim_x=0,aim_y=0,ring_radius=0;
 std::array<float,3> base{},camera{};std::array<float,16> world_to_clip{};
 std::array<EmbeddedGolemField,256> fields{};std::array<EmbeddedGolemShot,8> shots{};
 std::array<std::array<float,2>,2> casts{};std::array<float,2> release{},ready{};
 std::array<EmbeddedGolemReflection,64> reflections{};int reflection_used=0;
 float slam_born=0,slam_x=0,slam_y=0,slam_angle=0,grow_born=0,obelisk_x=0,obelisk_y=0,pulse_born=0,impact_born=0;int role=0;
 std::array<EmbeddedRubyActor,2> ruby{};
 float detach_born=0,death_born=0;int detach_from=0,detach_to=0;
 std::array<EmbeddedGolemNode,30> detached_pose{},death_pose{};
 std::array<float,3> death_base{};
 std::array<EmbeddedGolemNode,3> nodes{}; // current head, rear A, rear B
 // Synchronous source terrain query. Not retained or called after submission.
 float (*ground)(float,float)=nullptr;
};
struct EmbeddedGolemCache {std::shared_ptr<void> settled;float settled_born=-1;std::uint64_t serial=0;std::array<std::uint32_t,512> slots{};};
}
