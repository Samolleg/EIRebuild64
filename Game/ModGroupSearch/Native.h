#pragma once
#include "GroupSearchMovement.h"
class CUnit;
class CUnitServer;
class CAIUnitLogic;

namespace ModGroupSearch {
// Producer must persist home/phase before exposing an intent. Membership is
// supplied from the current active-set batch, never inferred by this adapter.
bool IsBriefingZone();
void Reset();
void Update();
void SetActive(const std::set<GroupSearch::Identity>& units);
bool Request(CUnit* unit, const GroupSearch::Intent& intent);
bool CommandDuty(void* duty, CAIUnitLogic& logic);
bool QueryPath(CUnitServer* server, GroupSearch::Point wanted,
               GroupSearch::Point& endpoint, bool& partial, int& cost);
struct Feedback {
    GroupSearch::Identity unit;
    uint32_t serial;
    GroupSearch::Action action;
    int tick;
};
std::vector<Feedback> ReadFeedback();
std::set<GroupSearch::Identity> Revoked();
void Forget(GroupSearch::Identity unit);
}
