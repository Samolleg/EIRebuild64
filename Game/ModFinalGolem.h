#pragma once
class CUnit;
struct SRPGAttackPortion;
namespace ModFinalGolem {
// Source implementation of C19. Deliberately absent from the MOB registry
// until all hard operations are implemented. The MCP seam is service-only.
void Action(CUnit* boss,CUnit* other,int operation);
void MeleeDelivered(CUnit* source,CUnit* target,const SRPGAttackPortion& portion);
void CaptureDamageTypes(CUnit* source,const SRPGAttackPortion& portion);
float Speed(CUnit& unit,float native);
bool RamSpeed(CUnit& unit,float& speed);
float Slow(CUnit& unit);
void Draw();
void ArmVisual() noexcept;
void ResetVisual() noexcept;
}
