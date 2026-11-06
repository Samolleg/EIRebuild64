#pragma once
#include "RPGSystem.h"
#include "ModAttackEvents.h"
namespace ModGolemReflect {
constexpr int Slots=64;
struct Delivery {
    CUnit* source=nullptr;CUnit* target=nullptr;
    ModAttackEvents::Actor author,boss;
    SRPGAttackPortion portion{};
    bool candidate=false,accepted=false,projectile=false;
};
Delivery Begin(CUnit* target,CUnit* source,const SRPGAttackPortion&);
void Complete(Delivery&);
void Tick();
void Reset();
}
