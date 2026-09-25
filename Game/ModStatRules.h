#pragma once
#include "Unit.h"
#include "GSVars.h"
#include "Item.h"
#include "ItemDatabase.h"
#include "UnitDatabase.h"
#include <stdexcept>
// Source: installed-addon6538a636 LS_ShieldsAndWeaponGroups IsTwohandedWeapon/GetLeftHand.
// Branch structure retained; engine names and client-stat access adapted only.
namespace ModStats {
inline bool IsTwohandedWeapon(int typeID, const CUnit* unit)
{
	int perkVal = 0;
	if (unit) {
		perkVal = unit->GetUserUnitStats()->cPerks[29];
	}
	if(perkVal >= 1)
		if (
			typeID == RPGWST_BOW ||      
			typeID == RPGWST_CROSSBOW  
		)
			return true;
		else
			return false;
	else
		if (
			typeID == RPGWST_BOW ||      
			typeID == RPGWST_CROSSBOW || 
			typeID == RPGWST_HANDS4 ||   
			typeID == RPGWST_HANDS5 ||   
			typeID == RPGWST_HANDS2 ||   
			typeID == RPGWST_HANDS7 ||   
			typeID == RPGWST_HANDS6      
			)
			return true;
		else
			return false;
}
template<class WeaponAt> inline int LeftHand(const CUnit& unit,WeaponAt weaponAt)
{
	int iLeftHand = -1;
    int atFirstSlot = 0;
    int atDualSlot = 1;
    int ShieldAtSlot = -1;
	int CurrweaponAtSlot = unit.GetCurrentWeaponSlotIndex();
	ERPGWeaponTypes isShieldType = RPGWST_HANDS1;
	ERPGWeaponTypes isStaffType = RPGWST_HANDS3;
	ERPGWeaponTypes isDStaffType = RPGWST_HANDS7;
    CItemWeapon *CurWeaponItem = weaponAt(CurrweaponAtSlot);
    CItemWeapon *FirstSlotItem = weaponAt(atFirstSlot);
    CItemWeapon *DualSlotItem  = weaponAt(atDualSlot);
	for (int i = 0; i < 4; i++)
	{
		CItemWeapon* curWeapon = weaponAt(i);
		if (curWeapon == NULL)
			continue;
		else
			if (curWeapon->GetShapeEntry()->dwTypeID == isShieldType) {
				ShieldAtSlot = i;
				break;
			}
	}
	bool CurrAtFirst = CurrweaponAtSlot == atFirstSlot;
	bool CurrAtDual  = CurrweaponAtSlot == atDualSlot;
	bool HasShield = ShieldAtSlot != -1;
	if (unit.GetCurrentWeaponSlotIndex() != -1 && FirstSlotItem && DualSlotItem && CurWeaponItem)
    {
        const SIDBItemGeneralShape* CurWeaponShape = CurWeaponItem->GetShapeEntry();
		const SIDBItemGeneralShape* FirstSlotShape = FirstSlotItem->GetShapeEntry();
		const SIDBItemGeneralShape* DualSlotShape  = DualSlotItem->GetShapeEntry();
		bool Curr2Handed  = IsTwohandedWeapon(CurWeaponShape->dwTypeID, &unit);
		bool First2Handed = IsTwohandedWeapon(FirstSlotShape->dwTypeID, &unit);
		bool Dual2Handed  = IsTwohandedWeapon(DualSlotShape->dwTypeID, &unit);
		bool CurrIsShield = CurWeaponShape->dwTypeID == isShieldType;
		bool CurrIsStaff  = CurWeaponShape->dwTypeID == isStaffType || CurWeaponShape->dwTypeID == isDStaffType;
		bool FirstIsStaff = FirstSlotShape->dwTypeID == isStaffType || FirstSlotShape->dwTypeID == isDStaffType;
		bool DualIsStaff  = DualSlotShape->dwTypeID  == isStaffType || DualSlotShape->dwTypeID  == isDStaffType;
        if (!Curr2Handed)
        { 
			if (CurrIsStaff || DualIsStaff || FirstIsStaff) {
				if (CurrAtDual)
				{
					if (!Curr2Handed) {
						if (!First2Handed && FirstIsStaff && DualIsStaff)
							iLeftHand = atFirstSlot; 
						else
							iLeftHand = -1; 
					}
					else
						iLeftHand = -1; 
				}
				else
				{
					if (!Dual2Handed && DualIsStaff)
					{
						if (HasShield) 
						{
							iLeftHand = ShieldAtSlot; 
						}
						else{
							if(CurrIsStaff && DualIsStaff)
								iLeftHand = atDualSlot; 
							else
								iLeftHand = -1; 
						}
					}
					else
						if (HasShield) 
						{
							iLeftHand = ShieldAtSlot; 
						}
						else
							iLeftHand = -1; 
				}
			}
			else if (CurrIsShield)
			{
				iLeftHand = ShieldAtSlot; 
			}
			else 
			{ 
				if (CurrAtDual)
				{
					if (!Curr2Handed) {
						if (!First2Handed)
							iLeftHand = atFirstSlot; 
						else
							iLeftHand = ShieldAtSlot; 
					}
					else
						iLeftHand = -1; 
				}
				else
				{
					if (!Dual2Handed)
					{
						if (HasShield) 
						{
							iLeftHand = ShieldAtSlot; 
						}
						else
							iLeftHand = atDualSlot; 
					}
					else
						if (HasShield) 
						{
							iLeftHand = ShieldAtSlot; 
						}
						else
							iLeftHand = -1; 
				}
			}
		}
    }
	return iLeftHand;
}

inline int LeftHand(const CUnit&unit){return LeftHand(unit,[&](int slot){return unit.GetQWeapon(slot);});}

inline void Armor(SRPGCommonStats& common,CUnit&unit,bool correctedNpc){
 if(unit.GetSubtypeID()>=unsigned(theUnitDatabase.prototypes.GetNumEntries()))throw std::runtime_error("Unit prototype index out of range");
 const auto*prototype=unit.GetPrototype();if(prototype->dwRaceIndex>=unsigned(theUnitDatabase.races.GetNumEntries()))throw std::runtime_error("Unit race index out of range");
 float armor[RPG_NUM_HIT_LOCATION_TYPES][RPG_NUM_ATTACK_TYPES]={};
 auto add=[&](int location,CItemArmor*item,int layer){float values[RPG_NUM_ATTACK_TYPES];item->GetAbsorbtions(values,layer);for(int i=0;i<RPG_NUM_ATTACK_TYPES;i++)armor[location][i]+=values[i];};
 for(int slot=0;slot<RPG_NUM_ARMOR_TYPES;slot++){auto*item=unit.GetQArmor(slot);if(!item)continue;switch(slot){
 case RPGAST_HELM:add(RPGHLT_HEAD,item,0);break;
 case RPGAST_PLATE:case RPGAST_SHIRT:add(RPGHLT_BODY,item,0);add(RPGHLT_HAND,item,1);break;
 case RPGAST_LEGGINGS:case RPGAST_BOOTS:case RPGAST_PANTS:add(RPGHLT_LEG,item,0);break;
 case RPGAST_GLOVES:add(RPGHLT_HAND,item,0);break;
 }}
 const float weight[]={.1f,.3f,.3f,.3f};auto*race=theUnitDatabase.races.GetEntry(prototype->dwRaceIndex);
 bool corrected=correctedNpc && !IsIDofPersonage(unit.GetID());
 for(int type=0;type<RPG_NUM_ATTACK_TYPES;type++){float base=race->defences[type]*prototype->fpAbsorbtionMultiplier,result=base;
 for(int location=0;location<RPG_NUM_HIT_LOCATION_TYPES;location++)if(corrected)result+=armor[location][type]*weight[location];else result=base+armor[location][type]*weight[location];
 common.psAbsorbtions[type]={result,result};}
}
}
