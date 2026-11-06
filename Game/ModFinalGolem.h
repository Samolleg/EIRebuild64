#pragma once
class CUnit;
class CObject3D;
struct SRPGAttackPortion;
namespace ModFinalGolem {
// Full public C19 implementation: normal/hard operations 0..28.
void Action(CUnit* boss,CUnit* other,int operation);
void MeleeDelivered(CUnit* source,CUnit* target,const SRPGAttackPortion& portion);
void CaptureDamageTypes(CUnit* source,const SRPGAttackPortion& portion);
float Speed(CUnit& unit,float native);
bool RamSpeed(CUnit& unit,float& speed);
float Slow(CUnit& unit);
void CollapseTick(bool clearing);
void RestoreCollapse();
void CommitCollapse();
void CaptureVisualState(bool death,int from,int to);
void Died(CUnit& unit);
bool IsRubyActor(const CObject3D& object);
bool PrepareAppearance(CObject3D& object);
void Draw();
void ArmVisual() noexcept;
void ResetVisual() noexcept;
}
