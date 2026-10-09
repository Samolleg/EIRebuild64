#pragma once
#include "ModNativeCrtRandom.h"
#include "ModStatRules.h"
// Check2hAnimation, addon source 39454a3. Engine accessors adapted; no client casts.
namespace ModAnimationRules {
static const char *animTwoSword="2hs", *animTwoAxeHammer="2ha", *animDuals="dat";
inline const SADEntry *Check2hAnimation(CUnit *Unit, CString AnimName, DWORD &dwAnimationFlags)
{
    const SADEntry *pEntry = 0;
	int weaponType = -1;
	CItemWeapon* RightWeapon = NULL;
	if(Unit->GetCurrentWeaponSlotIndex() >=0 && Unit->GetCurrentWeaponSlotIndex() < 4)
		RightWeapon = Unit->GetQWeapon(Unit->GetCurrentWeaponSlotIndex());
	CString finalAnimName, clearAnimName;
	clearAnimName = AnimName;
	if (RightWeapon != NULL)
    {
		ERPGWeaponTypes RightWeaponType = RightWeapon->GetWeaponType();
		int left = ModStats::LeftHand(*Unit);
		CItemWeapon* LeftWeapon = left >= 0 ? Unit->GetQWeapon(left) : nullptr;
		ERPGWeaponTypes LeftWeaponType = RPGWST_SWORD;
		if(LeftWeapon)
			LeftWeaponType = LeftWeapon->GetWeaponType();
		bool dual = RightWeaponType == LeftWeaponType;
		bool twinStaffs = false;
		if (LeftWeapon)
			twinStaffs = (dual && (LeftWeaponType == RPGWST_HANDS3 || LeftWeaponType == RPGWST_HANDS7));
		weaponType = Unit->GetQWeapon(Unit->GetCurrentWeaponSlotIndex())->GetShapeEntry()->dwTypeID;
		if (AnimName.Left(3) == animTwoSword || AnimName.Left(3) == animTwoAxeHammer)
		{
			clearAnimName = AnimName.Mid(3);
		}
		else
			clearAnimName = AnimName;
		if (AnimName == "") {
			pEntry = Unit->GetAnimationEntry(AnimName.GetString());
			if (!pEntry)
				pEntry = Unit->GetAnimationEntry("cidle10");
			return pEntry;
		}
		finalAnimName = clearAnimName;
		CString sname = clearAnimName.Left(7);
		if ((weaponType == RPGWST_HANDS3 || weaponType == RPGWST_HANDS7) && !twinStaffs) {
			if (AnimName.Left(3) == animDuals){
				if(weaponType == RPGWST_HANDS3)
					finalAnimName = "uattack18";
				else
					finalAnimName = "uattack18";
			}
			else if (finalAnimName.Mid(3).Left(7) == "uattack" || finalAnimName.Left(7) == "uattack") {
				if (weaponType == RPGWST_HANDS3)
					finalAnimName = "uattack18";
				else
					finalAnimName = "uattack18";
			}
			else {
				finalAnimName = clearAnimName;
			}
			pEntry = Unit->GetAnimationEntry(finalAnimName.GetString());
			if (!pEntry)
				pEntry = Unit->GetAnimationEntry(finalAnimName.GetString());
			if (finalAnimName.Left(4) == "crun")
				pEntry = Unit->GetAnimationEntry("crun01");
			else if (finalAnimName.Left(5) == "cwalk" && finalAnimName.Left(7) != "cwalk03")
				pEntry = Unit->GetAnimationEntry("cwalk02");
			if (finalAnimName == "2h" || finalAnimName == "")
				pEntry = Unit->GetAnimationEntry("cidle08");
			if (!pEntry)
				pEntry = Unit->GetAnimationEntry("cidle10");
			return pEntry;
		}
		else if ((weaponType == RPGWST_HANDS5 || weaponType == RPGWST_HANDS6 ) && Unit->GetUserUnitStats()->cPerks[29] == 0 && left == -1)
		{
			if (AnimName.Left(3) == animDuals)
				finalAnimName = "uattack04";
			if (finalAnimName.Mid(3).Left(7) == "uattack" || finalAnimName.Left(7) == "uattack") {
				int rand = ModNativeCrtRandom::Random(2);
				if (rand)
					finalAnimName = "2hauattack04";
				else
					finalAnimName = "2hauattack05";
				pEntry = Unit->GetAnimationEntry(finalAnimName.GetString());
			}
			else {
				clearAnimName.Format("%s%s", animTwoAxeHammer, finalAnimName);
				finalAnimName = clearAnimName;
			}
				if (clearAnimName.Mid(3).Left(7) == "uattack") {
					int rand = ModNativeCrtRandom::Random(2);
					if (rand)
						pEntry = Unit->GetAnimationEntry("2hauattack04");
					else
						pEntry = Unit->GetAnimationEntry("2hauattack05");
				}
				else if (clearAnimName.Mid(3).Left(6) == "crun01")
					pEntry = Unit->GetAnimationEntry("2hacrun01");
				else if (clearAnimName.Mid(3).Left(6) == "crun02")
					pEntry = Unit->GetAnimationEntry("2hacrun01");
				else if (clearAnimName.Mid(3).Left(6) == "crun04")
					pEntry = Unit->GetAnimationEntry("2hacrun04");
				else if (clearAnimName.Mid(3).Left(6) == "crun05")
					pEntry = Unit->GetAnimationEntry("2hacrun04");
				else if (clearAnimName.Mid(3).Left(7) == "cwalk03")
					pEntry = Unit->GetAnimationEntry("cwalk03");
				else if (clearAnimName.Mid(3).Left(5) == "cwalk" && clearAnimName.Mid(3).Left(7) != "cwalk03")
					pEntry = Unit->GetAnimationEntry("2hacwalk02");
				else if (clearAnimName.Left(5) == "cwalk")
					pEntry = Unit->GetAnimationEntry("2hacwalk02");
				else if (clearAnimName == "2hs2h" || clearAnimName == "2hacidle02" || clearAnimName == "2hacidle07" || clearAnimName == "2hacidle10" || clearAnimName == "2hacidle11" ||
					clearAnimName == "2hacidle14"  || clearAnimName == "2hacidle15"  ||
					clearAnimName == "2hacidle16" || clearAnimName == "2hacidle19" || clearAnimName == "2hacidle21")
					pEntry = Unit->GetAnimationEntry("2hacidle08");
				else if (clearAnimName == "2hacidle22")
					pEntry = Unit->GetAnimationEntry("2hacidle20");
				else if (clearAnimName == "2hauspecial22")
					pEntry = Unit->GetAnimationEntry("uspecial22");
				else if (clearAnimName == "2hauspecial04")
					pEntry = Unit->GetAnimationEntry("uspecial04");
				else if (clearAnimName == "2hauspecial05")
					pEntry = Unit->GetAnimationEntry("uspecial05");
				else if (clearAnimName == "2hauspecial06")
					pEntry = Unit->GetAnimationEntry("uspecial06");
				else if (clearAnimName == "2hauspecial07")
					pEntry = Unit->GetAnimationEntry("uspecial07");
				else if (clearAnimName == "2hauspecial24")
					pEntry = Unit->GetAnimationEntry("uspecial24");
				else
					pEntry = Unit->GetAnimationEntry(finalAnimName.GetString());
				if(!pEntry)
					pEntry = Unit->GetAnimationEntry(clearAnimName.Mid(3).GetString());
				if (clearAnimName == "2h" || clearAnimName == "")
					pEntry = Unit->GetAnimationEntry("2hacidle08");
				if (!pEntry)
					pEntry = Unit->GetAnimationEntry("cidle10");
				return pEntry;
		}
		else if (weaponType == RPGWST_HANDS4 && Unit->GetUserUnitStats()->cPerks[29] == 0 && left == -1)
		{
			if (AnimName.Left(3) == animDuals)
				finalAnimName = "uattack04";
			if (finalAnimName.Mid(3).Left(7) == "uattack" || finalAnimName.Left(7) == "uattack") {
				int rand = ModNativeCrtRandom::Random(2);
				if (rand)
					finalAnimName = "2hsuattack04";
				else
					finalAnimName = "2hsuattack05";
				pEntry = Unit->GetAnimationEntry(finalAnimName.GetString());
			}
			else {
				clearAnimName.Format("%s%s", animTwoSword, finalAnimName);
				finalAnimName = clearAnimName;
			}
			if (clearAnimName.Mid(3).Left(7) == "uattack") {
				int rand = ModNativeCrtRandom::Random(2);
				if(rand)
					pEntry = Unit->GetAnimationEntry("2hsuattack04");
				else
					pEntry = Unit->GetAnimationEntry("2hsuattack05");
			}
			else if (clearAnimName.Mid(3).Left(6) == "crun01")
				pEntry = Unit->GetAnimationEntry("2hscrun01");
			else if (clearAnimName.Mid(3).Left(6) == "crun02")
				pEntry = Unit->GetAnimationEntry("2hscrun01");
			else if (clearAnimName.Mid(3).Left(6) == "crun04")
				pEntry = Unit->GetAnimationEntry("2hscrun04");
			else if (clearAnimName.Mid(3).Left(6) == "crun05")
				pEntry = Unit->GetAnimationEntry("2hscrun04");
			else if (clearAnimName.Mid(3).Left(7) == "cwalk03")
				pEntry = Unit->GetAnimationEntry("cwalk03");
			else if (clearAnimName.Mid(3).Left(5) == "cwalk" && clearAnimName.Mid(3).Left(7) != "cwalk03")
				pEntry = Unit->GetAnimationEntry("2hscwalk02");
			else if (clearAnimName == "2hs2h" || clearAnimName == "2hscidle02" || clearAnimName == "2hscidle07" || clearAnimName == "2hscidle10" || clearAnimName == "2hscidle11" ||
				clearAnimName == "2hscidle14" || clearAnimName == "2hscidle15" ||
				clearAnimName == "2hscidle16" || clearAnimName == "2hscidle19" || clearAnimName == "2hscidle21")
				pEntry = Unit->GetAnimationEntry("2hscidle08");
			else if (clearAnimName == "2hscidle22")
				pEntry = Unit->GetAnimationEntry("2hscidle20");
			else if (clearAnimName == "2hsuspecial22")
				pEntry = Unit->GetAnimationEntry("uspecial22");
			else if (clearAnimName == "2hsuspecial04")
				pEntry = Unit->GetAnimationEntry("uspecial04");
			else if (clearAnimName == "2hsuspecial05")
				pEntry = Unit->GetAnimationEntry("uspecial05");
			else if (clearAnimName == "2hsuspecial06")
				pEntry = Unit->GetAnimationEntry("uspecial06");
			else if (clearAnimName == "2hsuspecial07")
				pEntry = Unit->GetAnimationEntry("uspecial07");
			else if (clearAnimName == "2hsuspecial24")
				pEntry = Unit->GetAnimationEntry("uspecial24");
			else
				pEntry = Unit->GetAnimationEntry(finalAnimName.GetString());
			if (!pEntry)
				pEntry = Unit->GetAnimationEntry(clearAnimName.Mid(3).GetString());
			if (clearAnimName == "2h" || clearAnimName == "")
				pEntry = Unit->GetAnimationEntry("2hscidle08");
			if (!pEntry)
				pEntry = Unit->GetAnimationEntry("cidle10");
			return pEntry;
		}
		else if (LeftWeapon && (sname == "uattack" || AnimName.Left(3) == animDuals))
		{
			if (LeftWeaponType == RPGWST_HANDS1)
			{
					if (RightWeaponType == RPGWST_SWORD || RightWeaponType == RPGWST_AXE || RightWeaponType == RPGWST_HAMMER)
						clearAnimName = "uattack05";
					else if (RightWeaponType == RPGWST_DAGGER)
						clearAnimName = "uattack08";
					else if (RightWeaponType == RPGWST_SPEAR)
						clearAnimName = "uattack06";
					else if (RightWeaponType == RPGWST_BOW)
						clearAnimName = "uattack02";
					else if (RightWeaponType == RPGWST_CROSSBOW)
						clearAnimName = "uattack14";
					else if (RightWeaponType == RPGWST_HANDS3)
						clearAnimName = "uattack06";
					else if (RightWeaponType == RPGWST_HANDS7)
						clearAnimName = "uattack18";
					else if (RightWeaponType == RPGWST_HANDS2)
						clearAnimName = "uattack06";
					else if (RightWeaponType == RPGWST_HANDS4)
						clearAnimName = "uattack05";
					else if (RightWeaponType == RPGWST_HANDS5)
						clearAnimName = "uattack05";
					else if (RightWeaponType == RPGWST_HANDS6)
						clearAnimName = "uattack05";
					else
						clearAnimName = "uattack05";
				pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
				return pEntry;
			}
			else
			{
				bool twinSwords = (dual && LeftWeaponType == RPGWST_SWORD);
				bool twinAxes = (dual && LeftWeaponType == RPGWST_AXE);
				bool twinHammers = (dual && LeftWeaponType == RPGWST_HAMMER);
				bool twinDaggers = (dual && LeftWeaponType == RPGWST_DAGGER);
				bool twinPikes = (dual && LeftWeaponType == RPGWST_SPEAR);
				bool rCrush = (RightWeaponType == RPGWST_SWORD || RightWeaponType == RPGWST_AXE || RightWeaponType == RPGWST_HAMMER ||
					RightWeaponType == RPGWST_HANDS4 || RightWeaponType == RPGWST_HANDS5 || RightWeaponType == RPGWST_HANDS6);
				bool rPike = (RightWeaponType == RPGWST_SPEAR || RightWeaponType == RPGWST_HANDS2);
				bool rDagg = (RightWeaponType == RPGWST_DAGGER);
				bool lCrush = (LeftWeaponType == RPGWST_SWORD || LeftWeaponType == RPGWST_AXE || LeftWeaponType == RPGWST_HAMMER ||
					LeftWeaponType == RPGWST_HANDS4 || LeftWeaponType == RPGWST_HANDS5 || LeftWeaponType == RPGWST_HANDS6);
				bool lPike = (LeftWeaponType == RPGWST_SPEAR || LeftWeaponType == RPGWST_HANDS2);
				bool lDagg = (LeftWeaponType == RPGWST_DAGGER);
				bool IdenticalWeight = (ModRuntime::Current()->InfoProperty("GetWeaponPropsInside", RightWeapon->GetShapeEntry()->szName, 7) == ModRuntime::Current()->InfoProperty("GetWeaponPropsInside", LeftWeapon->GetShapeEntry()->szName, 7));
				clearAnimName = "";
				if (twinSwords)
					if (IdenticalWeight) clearAnimName = "dat01sws44";
					else				 clearAnimName = "dat01crcr37";
				else if (twinAxes)
					if (IdenticalWeight) clearAnimName = "dat01axes42";
					else				 clearAnimName = "dat01crcr37";
				else if (twinHammers)
					if (IdenticalWeight) clearAnimName = "dat01hams24";
					else				 clearAnimName = "dat01crcr37";
				else if (twinDaggers)
					if (IdenticalWeight) clearAnimName = "dat01dagg75";
					else				 clearAnimName = "dat01sprs37";
				else if (twinPikes)
					if (IdenticalWeight) clearAnimName = "dat01sprs37";
					else				 clearAnimName = "dat01sprs37";
				else if (twinStaffs)
					if (IdenticalWeight) clearAnimName = "dat01st237";
					else				 clearAnimName = "dat01st237";
				else
				{
					if (rCrush && lCrush)
						clearAnimName = "dat01crcr37";
					else if (rPike && lPike)
						clearAnimName = "dat01sprs37";
					else if (rCrush && lPike)
						clearAnimName = "dat01crst37";
					else if (rPike && lCrush)
						clearAnimName = "dat01stcr37";
					else if (rDagg && lPike)
						clearAnimName = "dat01st237";
					else if (rPike && lDagg)
						clearAnimName = "dat01sprs37";
					else if (rDagg && lDagg)
						clearAnimName = "dat01st237";
					else
						clearAnimName = "dat01crcr37";
				}
				pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
				if (clearAnimName == "2h" || clearAnimName == "")
					pEntry = Unit->GetAnimationEntry("cidle10");
				return pEntry;
			}
		}
		else
		{
			if (AnimName.Left(3) == animTwoSword || AnimName.Left(3) == animTwoAxeHammer )
			{
				pEntry = Unit->GetAnimationEntry(finalAnimName.GetString());
				if (RightWeaponType == RPGWST_HANDS2 && (clearAnimName.Left(7) == "uattack" || clearAnimName.Mid(3).Left(7) == "uattack")) {
					clearAnimName = "uattack17";
					pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
				}
                else if (RightWeaponType == RPGWST_HANDS5 && (clearAnimName.Left(7) == "uattack" || clearAnimName.Mid(3).Left(7) == "uattack"))
                {
                    clearAnimName = "uattack05";
                    pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
                }
			}
			else if (AnimName.Left(3) == animDuals)
			{
				if (RightWeaponType == RPGWST_SWORD || RightWeaponType == RPGWST_AXE || RightWeaponType == RPGWST_HAMMER)
					clearAnimName = "uattack05";
				else if(RightWeaponType == RPGWST_DAGGER)
					clearAnimName = "uattack08";
				else if(RightWeaponType == RPGWST_SPEAR)
					clearAnimName = "uattack13";
				else if(RightWeaponType == RPGWST_BOW)
					clearAnimName = "uattack02";
				else if(RightWeaponType == RPGWST_CROSSBOW)
					clearAnimName = "uattack14";
				else if (RightWeaponType == RPGWST_HANDS2)
					clearAnimName = "uattack17";
                else if (RightWeaponType == RPGWST_HANDS3)
                    clearAnimName = "uattack06";
                else if (RightWeaponType == RPGWST_HANDS7)
                    clearAnimName = "uattack18";
                else if (RightWeaponType == RPGWST_HANDS4)
                    clearAnimName = "uattack05";
                else if (RightWeaponType == RPGWST_HANDS5)
                    clearAnimName = "uattack05";
                else if (RightWeaponType == RPGWST_HANDS6)
                    clearAnimName = "uattack05";
                                else
					clearAnimName = "uattack05";
				pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
			}
			else{
				pEntry = Unit->GetAnimationEntry(finalAnimName.GetString());
				if (RightWeaponType == RPGWST_HANDS2 && (clearAnimName.Left(7) == "uattack" || clearAnimName.Mid(3).Left(7) == "uattack")){
						clearAnimName = "uattack17";
						pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
				}
			}
			if(clearAnimName == "2h" || clearAnimName == "" )
				pEntry = Unit->GetAnimationEntry("cidle10");
			if (twinStaffs) {
				if (!pEntry)
					pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
				if (clearAnimName.Left(4) == "crun")
					pEntry = Unit->GetAnimationEntry("crun01");
				else if (clearAnimName.Left(5) == "cwalk" && clearAnimName.Left(7) != "cwalk03")
					pEntry = Unit->GetAnimationEntry("cwalk02");
				if (clearAnimName == "2h" || clearAnimName == "")
					pEntry = Unit->GetAnimationEntry("cidle08");
			}
			if (RightWeaponType == RPGWST_HANDS2 ||
				RightWeaponType == RPGWST_HANDS4 ||
				RightWeaponType == RPGWST_HANDS5 ||
				RightWeaponType == RPGWST_HANDS6) {
				if (!pEntry)
					pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
				if (clearAnimName.Left(4) == "crun")
					pEntry = Unit->GetAnimationEntry("crun01");
				else if (clearAnimName.Left(4) == "scross18")
					pEntry = Unit->GetAnimationEntry("scross17");
				else if (clearAnimName.Left(4) == "scross13")
					pEntry = Unit->GetAnimationEntry("scross14");
				else if (clearAnimName.Left(5) == "cwalk" && clearAnimName.Left(7) != "cwalk03")
					pEntry = Unit->GetAnimationEntry("cwalk02");
				if (clearAnimName == "2h" || clearAnimName == "")
					pEntry = Unit->GetAnimationEntry("cidle08");
				if (clearAnimName.Left(7) == "uattack" || clearAnimName.Mid(3).Left(7) == "uattack")
                {
                    if (RightWeaponType == RPGWST_HANDS2)
                        clearAnimName = "uattack17";
                    else if (RightWeaponType == RPGWST_HANDS3)
                        clearAnimName = "uattack06";
                    else if (RightWeaponType == RPGWST_HANDS7)
                        clearAnimName = "uattack18";
                    else if (RightWeaponType == RPGWST_HANDS4)
                        clearAnimName = "uattack05";
                    else if (RightWeaponType == RPGWST_HANDS5)
                        clearAnimName = "uattack05";
                    else if (RightWeaponType == RPGWST_HANDS6)
                        clearAnimName = "uattack05";
                    pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
                }
            }
			return pEntry;
		}
	}
    else
    {
	    if (Unit->GetTypeID() == MOTID_HUMAN)
			if(clearAnimName.Left(7) == "uattack" || clearAnimName.Left(3) == animDuals)
				clearAnimName = "uattack12";
			else {
				if (clearAnimName.Left(3) == animTwoSword || clearAnimName.Left(3) == animTwoAxeHammer)
				{
					clearAnimName = clearAnimName.Mid(3);
				}
			}
		pEntry = Unit->GetAnimationEntry(clearAnimName.GetString());
    }
    return pEntry;
}
}
