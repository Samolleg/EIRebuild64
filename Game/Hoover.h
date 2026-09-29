#pragma once

#include "GameBase.h"
#include "Item.h"
#include "Unit.h"
#include "World.h"

class CWorldServer;

// --------------------------------------------------------------------------------------------
class CHoover : public CGameBase
{
private:
public:
    CHoover();
    virtual ~CHoover();
    // create/destroy functions
    virtual BOOL Create();
    virtual void Destroy();
    // function adds a reference on each item from allItems list
    BOOL Hoover(CWorld *pWorld, CUnit *lpUnit, CList<CItem *, CItem *> &allItems,
                CList<CItem *, CItem *> &newItems, CList<CSpell *, CSpell *> &newSpells,
                int &nHooveredMoney);
    int HooverCarriedLoot(CWorldServer *world, CUnit *carrier,
                          CList<CItem *, CItem *> &allItems,
                          CList<CItem *, CItem *> &newItems, int &money);
    BOOL HooverAddon(CWorld *pWorld, CUnit *lpUnit, CList<CItem *, CItem *> &allItems,
                     CList<CItem *, CItem *> &newItems, CList<CSpell *, CSpell *> &newSpells,
                     int &nHooveredMoney, int scienceSkill, float dexterity, bool questOnly);
    // class identification functions
    virtual LPCSTR GetGameClassName() const { return "CHoover"; };
    virtual EClassIDs WhoAmI() const { return HPC_HOOVER; };
};
