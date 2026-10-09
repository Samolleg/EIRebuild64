#pragma once

namespace ModPlayerNumbers {
// Scripts.cpp::SetMaxPlayerNumber: use the server's manager, even when the
// caller creates a client or temporary player. Do not renumber existing owners.
template<class Player, class Manager, class Iterator>
void Assign(Player* newPlayer, int newNumber, Manager* manager, Iterator& iter)
{
    if (!manager)
    {
        newPlayer->SetPlayerNumber(newNumber);
        return;
    }
    int maxPlayerNumber = -1;
    for (auto* player = static_cast<Player*>(iter.GetFirst(manager)); player;
         player = static_cast<Player*>(iter.GetNext()))
    {
        if (player != newPlayer && player->GetPlayerNumber() > maxPlayerNumber)
            maxPlayerNumber = player->GetPlayerNumber();
    }
    newPlayer->SetPlayerNumber(maxPlayerNumber >= 0 ? maxPlayerNumber + 1 : newNumber);
}
}
