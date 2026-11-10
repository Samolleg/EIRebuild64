#pragma once
// Immutable evaluator input. No addresses, hooks, or reads of game memory.
#include "../UnityBodyKernel.h"
#include <string>
#include <map>
namespace UnityGame {
inline constexpr const char* Names[]={"hp","bd","hd","lh1","rh1","lh2","rh2","lh3","rh3","ll1","rl1","ll2","rl2","ll3","rl3"};
using Geometry=std::map<std::string,std::vector<UnityBody::V>>;
struct Attachment {int driver=-1;bool oldEye=false;UnityBody::M pose;std::string name;unsigned flags=0;};
struct Actor {
 const Geometry* geometry=nullptr;
 std::array<UnityBody::M,15> poses{};
 std::array<float,3> complection{};
 std::vector<Attachment> attachments;
 std::string animation;
 double animationFrame=-1;
};
}
