#pragma once
class CUnit;
struct SRPGAttackPortion;
namespace ModCurseEncounter {
void Action(CUnit* boss,CUnit* other,int operation);
void Contact(CUnit* attacker,CUnit* target);
// Join the existing damage owner, preserving the portion identity used by
// prepared attacks. Only Curse-owned power/to-hit fields are restored.
class DamageScope {
    SRPGAttackPortion& portion;
    float power,toHit;
    bool changed=false;
public:
    DamageScope(CUnit* target,CUnit* author,SRPGAttackPortion& attack);
    ~DamageScope();
    DamageScope(const DamageScope&)=delete;
    DamageScope& operator=(const DamageScope&)=delete;
};
void Draw();
void ArmVisual() noexcept;
void ResetVisual() noexcept;
}
