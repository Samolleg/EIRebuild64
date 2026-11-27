#pragma once
#include <string>

class CPlayer;
class CUnitServer;
struct SRPGUnitStats;

struct BaseCompanion {
    int id;
    std::string key;
    std::string remainderKey;
};

void CreditCompanion(CPlayer &player, const BaseCompanion &companion, float heroShare,
                     CUnitServer *unit, SRPGUnitStats *fallback = nullptr);
