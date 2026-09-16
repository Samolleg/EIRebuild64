from pathlib import Path
r=Path('work/eirebuild-local');p=r/'Game/ModRuntime.cpp';b=p.read_bytes();nl=b'\r\n' if b'\r\n' in b else b'\n'
def edit(a,c):
 global b
 x=a.encode().replace(b'\n',nl);y=c.encode().replace(b'\n',nl);assert b.count(x)==1,(a[:45],b.count(x));b=b.replace(x,y)
edit('void SelectWeapon(int slot){dwCurrentWeapon=slot;}','void SelectWeapon(int slot){dwCurrentWeapon=slot;}\n            void BindArmor(CItemArmor*p){lpArmors[0]=p;}')
edit('''            equippedUnit.SetID(1000000001);''','''            // Fixture uses the real authoritative armor slot; no Wear/world/UI side effects are simulated.
            require(theItemDatabase.armors.GetNumEntries()>0,"No armor fixture");
            CItemArmor armor;armor.SetTypeID(ITID_ITEM_ARMOR);armor.SetSubtypeID(0);equippedUnit.BindArmor(&armor);
            struct DetachArmor {EffectUnit&u;~DetachArmor(){u.BindArmor(nullptr);}} detachArmor{equippedUnit};
            equippedUnit.SetID(1000000001);''')
# Nonempty armor can change stealth from table so derive real sum oracle (all duplicate rows) separately in Lua.
edit('''int flow=int(luaNumber("return PerkType.Flow")),armory=int(luaNumber("return PerkType.Armory"));''','''int flow=int(luaNumber("return PerkType.Flow")),armory=int(luaNumber("return PerkType.Armory"));
            lua_pushstring(lua,theItemDatabase.armors.GetEntry(0)->szName);lua_setglobal(lua,"__r14_armor_name");
            lua_pushstring(lua,theItemDatabase.materials.GetEntry(0)->szName);lua_setglobal(lua,"__r14_material_name");
            double sightBonus=luaNumber("local t=require('table_indexes'); local v=0; for _,r in ipairs(t.ArmorProp[__r14_armor_name] or {}) do v=v+(r[ArmorPropType.Sight] or 0) end; for _,r in ipairs(t.MaterialProp[__r14_material_name] or {}) do v=v+(r[MaterialPropType.Sight] or 0) end; return v");
            double hearingBonus=luaNumber("local t=require('table_indexes'); local v=0; for _,r in ipairs(t.ArmorProp[__r14_armor_name] or {}) do v=v+(r[ArmorPropType.Hearing] or 0) end; for _,r in ipairs(t.MaterialProp[__r14_material_name] or {}) do v=v+(r[MaterialPropType.Hearing] or 0) end; return v");
            int staffType=int(luaNumber("return WeaponType.Staff")),largeStaffType=int(luaNumber("return WeaponType.StaffBig"));
            int staffIndex=-1;for(int i=0;i<theItemDatabase.weapons.GetNumEntries();i++)if(theItemDatabase.weapons.GetEntry(i)->dwTypeID==staffType){staffIndex=i;break;}
            require(staffIndex>=0,"No real staff fixture");''')
edit('fabs(prepared.percepts[RPGS_SIGHT].fpEffectiveValue-.5f)<.0001 && fabs(prepared.percepts[RPGS_HEARING].fpEffectiveValue-.75f)<.0001','fabs(prepared.percepts[RPGS_SIGHT].fpEffectiveValue-(.5f+sightBonus))<.0001 && fabs(prepared.percepts[RPGS_HEARING].fpEffectiveValue-(.75f+hearingBonus))<.0001')
edit('''                preparedCases++;
            }''','''                const float itemRates[]={.001f,.00115f,.0013f,.0015f},staffRates[]={.001f,.00125f,.0015f,.002f};
                bool isStaff=equipped.GetShapeEntry()->dwTypeID==staffType||equipped.GetShapeEntry()->dwTypeID==largeStaffType;
                require(fabs(armor.GetManaRegenRate()-itemRates[level])<.000001,"Armor regeneration did not commit");
                require(fabs(equipped.GetManaRegenRate()-(isStaff?staffRates[level]:itemRates[level]))<.000001,"Weapon regeneration mismatch");
                equipped.SetSubtypeID(DWORD(staffIndex)<<16);package.PrepareUnitStats(prepared,result,equippedUnit,"unused-hero-prototype");
                require(fabs(equipped.GetManaRegenRate()-staffRates[level])<.000001,"Staff-specific regeneration mismatch");
                equipped.SetSubtypeID(DWORD(weaponIndex)<<16);
                preparedCases++;
            }
            run("__r14_armor_name=nil; __r14_material_name=nil");''')
edit('float rate=jewelry.GetManaRegenRate();','float rate=jewelry.GetManaRegenRate(),armorRate=armor.GetManaRegenRate(),weaponRate=equipped.GetManaRegenRate();')
edit('jewelry.GetManaRegenRate()==rate,"Stat/equipment stage partially committed"','jewelry.GetManaRegenRate()==rate && armor.GetManaRegenRate()==armorRate && equipped.GetManaRegenRate()==weaponRate,"Stat/equipment stage partially committed"')
p.write_bytes(b)
