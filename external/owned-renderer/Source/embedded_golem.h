#pragma once
#include <array>
#include <cstdint>
namespace owned {
struct EmbeddedGolemField {float x=0,y=0,z=0,r=0,born=0,end=0,style=0;};
struct EmbeddedGolemShot {float x=0,y=0,z=0,ex=0,ey=0,ez=0,born=0,end=0,hand=0,stage=0;std::array<float,4> q{};};
struct EmbeddedGolemNode {std::array<float,3> position{};std::array<float,4> rotation{};bool valid=false;};
struct EmbeddedGolemReflection {float born=0,end=0;int kind=0;std::array<float,3> start{},target{};};
struct EmbeddedGolemInput {
 std::uint64_t world_epoch=0,incarnation=0;int boss_id=0,tick=0,stage=0,phase=0;
 bool active=false,walls=false,dead=false,throw_launched=false,hard=false;int throw_count=0;
 float timer=0,target_x=0,target_y=0,beam_ex=0,beam_ey=0,aim_x=0,aim_y=0,ring_radius=0;
 std::array<float,3> base{},camera{};std::array<float,16> world_to_clip{};
 std::array<EmbeddedGolemField,256> fields{};std::array<EmbeddedGolemShot,8> shots{};
 std::array<std::array<float,2>,2> casts{};std::array<float,2> release{},ready{};
 std::array<EmbeddedGolemReflection,64> reflections{};int reflection_used=0;
 float detach_born=0,death_born=0;int detach_from=0,detach_to=0;
 std::array<EmbeddedGolemNode,30> detached_pose{},death_pose{};
 std::array<float,3> death_base{};
 std::array<EmbeddedGolemNode,3> nodes{}; // current head, rear A, rear B
 // Synchronous source terrain query. Not retained or called after submission.
 float (*ground)(float,float)=nullptr;
};
struct EmbeddedGolemCache {std::uint64_t serial=0;std::array<std::uint32_t,512> slots{};};
}
