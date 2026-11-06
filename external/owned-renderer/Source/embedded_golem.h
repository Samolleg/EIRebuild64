#pragma once
#include <array>
#include <cstdint>
namespace owned {
struct EmbeddedGolemField {float x=0,y=0,z=0,r=0,born=0,end=0,style=0;};
struct EmbeddedGolemShot {float x=0,y=0,z=0,ex=0,ey=0,ez=0,born=0,end=0,hand=0,stage=0;std::array<float,4> q{};};
struct EmbeddedGolemNode {std::array<float,3> position{};std::array<float,4> rotation{};bool valid=false;};
struct EmbeddedGolemInput {
 std::uint64_t world_epoch=0,incarnation=0;int boss_id=0,tick=0,stage=0,phase=0;
 bool active=false,walls=false,dead=false,throw_launched=false;int throw_count=0;
 float timer=0,target_x=0,target_y=0,beam_ex=0,beam_ey=0,aim_x=0,aim_y=0,ring_radius=0;
 std::array<float,3> base{},camera{};std::array<float,16> world_to_clip{};
 std::array<EmbeddedGolemField,256> fields{};std::array<EmbeddedGolemShot,8> shots{};
 std::array<std::array<float,2>,2> casts{};std::array<float,2> release{},ready{};
 std::array<EmbeddedGolemNode,3> nodes{}; // current head, rear A, rear B
 // Synchronous source terrain query. Not retained or called after submission.
 float (*ground)(float,float)=nullptr;
};
struct EmbeddedGolemCache {std::uint64_t serial=0;std::array<std::uint32_t,512> slots{};};
}
