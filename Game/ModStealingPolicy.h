#pragma once
// Bound native39454a3 LS UseSkill24695/24733, HooverNew24962.
namespace ModStealing {
inline bool CanSteal(bool hasPlayerSpecific, bool inCombat)
{
    return hasPlayerSpecific && !inCombat;
}
inline int ActorSkill(int userDexterity, int userManualSkill)
{
    const int value = userDexterity - 25 + userManualSkill;
    return value < 1 ? 1 : value;
}
// Raw lists borrow one retained reference per quest until the caller hands it
// to the player and releases the temporary list. Remove releases only unit refs.
template<class Count, class At, class Append, class Remove>
inline void CollectQuestItems(Count count, At at, Append append, Remove remove)
{
    for (int i = 0; i < count(); ++i)
        if (auto* item = at(i)) {
            item->AddRef();
            append(item);
        }
    while (count()) remove(0);
}
}
