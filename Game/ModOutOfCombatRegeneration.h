#pragma once
class CUnit;
class CUnitServer;
namespace ModOutOfCombatRegeneration {
bool IsLivingHostile(CUnit* candidate, int playerNumber);
void Reset();
void Forget(CUnitServer* server);
void Update(CUnit* unit);
bool IsSafe(CUnit* unit);
float GetMultiplier(CUnit* unit);
}
