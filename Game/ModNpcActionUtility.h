#pragma once

// Relative utility only. Native legality/range/effect checks remain authoritative.
// No role input: any unit may use any permitted action it actually possesses.
namespace NpcUtility {
enum class Action { Debuff, Slow, Stun, CombatBuff, Heal };
struct Context {
    bool moving;
    bool castCommand;
    bool threatensAlly;
    bool engaged;
    bool threatened;
    float health;
};
constexpr float Bonus(Action action, Context c) {
    return action == Action::Slow ? (c.moving ? (c.threatensAlly ? 1.45f : 0.75f) : 0.10f) :
        action == Action::Stun ? (c.castCommand ? 1.65f : c.threatensAlly ? 1.30f : 0.40f) :
        action == Action::Debuff ? (c.threatensAlly ? 1.10f : 0.35f) :
        action == Action::CombatBuff ? (c.engaged ? 1.15f : 0.25f) :
        (0.65f + 2.75f * (1.0f - c.health) + (c.threatened && c.health < 0.4f ? 0.75f : 0.0f));
}
static_assert(Bonus(Action::Slow, {true,false,true,false,false,1}) > Bonus(Action::Slow, {false,false,true,false,false,1}), "moving pursuer");
static_assert(Bonus(Action::Slow, {true,false,true,false,false,1}) > Bonus(Action::Slow, {true,false,false,false,false,1}), "ally pressure");
static_assert(Bonus(Action::Stun, {false,true,true,false,false,1}) > Bonus(Action::Stun, {false,false,true,false,false,1}), "cast command opportunity");
static_assert(Bonus(Action::Debuff, {false,false,true,false,false,1}) > Bonus(Action::Debuff, {false,false,false,false,false,1}), "active enemy");
static_assert(Bonus(Action::CombatBuff, {false,false,false,true,false,1}) > Bonus(Action::CombatBuff, {false,false,false,false,false,1}), "engaged ally");
static_assert(Bonus(Action::Heal, {false,false,false,false,true,0.2f}) > Bonus(Action::Heal, {false,false,false,false,false,0.2f}), "emergency healing");
static_assert(Bonus(Action::Heal, {false,false,false,false,false,0.2f}) > Bonus(Action::Heal, {false,false,false,false,false,0.8f}), "missing health");
}
