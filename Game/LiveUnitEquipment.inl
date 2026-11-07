// MCP-only service equipment. Use the camp's CPersonage ownership operations
// and its deployment primitives, without removing/redeploying a live actor.
struct EquipRecipe {
    const SIDBItemGeneralShape* shape=nullptr;
    const SIDBMaterial* material=nullptr;
    const SSDBSpellPrototype* spell=nullptr;
    std::vector<unsigned char> runes;
    bool spellItem=false;
    explicit EquipRecipe(const json& value) {
        if(!value.is_string())throw std::runtime_error("item must be a GiveItem code string");
        const auto name=value.get<std::string>();
        if(name.empty()||name.size()>512||name.find('\0')!=std::string::npos)
            throw std::runtime_error("item code must contain 1..512 bytes without NUL");
        CString code=name.c_str(),spellCode;const int bracket=code.Find('[');
        if(bracket>=0) {
            if(code.Right(1)!="]")throw std::runtime_error("invalid item spell suffix");
            spellCode=code.Mid(bracket+1,code.GetLength()-bracket-2);code=code.Left(bracket);code.TrimRight();
            const int brace=spellCode.Find('{');CStringArray modifiers;
            if(brace>=0) {
                if(spellCode.Right(1)!="}")throw std::runtime_error("invalid spell rune suffix");
                CString list=spellCode.Mid(brace+1,spellCode.GetLength()-brace-2);
                NStrProc::SplitString(list,modifiers,';');spellCode=spellCode.Left(brace);
            }
            spellCode.TrimLeft();spellCode.TrimRight();
            auto* spells=theWorldServer.GetSpellManager();spell=spells->GetPrototype(spellCode);
            if(!spell)throw std::runtime_error("spell_prototype_not_found");
            for(int i=0;i<modifiers.GetSize();++i) {
                modifiers[i].TrimLeft();modifiers[i].TrimRight();if(modifiers[i].IsEmpty())continue;
                const int index=spells->GetModifierIndex(modifiers[i]);
                if(!spells->GetModifier(modifiers[i])||index<0||index>=255||runes.size()>=8)
                    throw std::runtime_error("invalid spell rune");
                runes.push_back(static_cast<unsigned char>(index+1));
            }
        }
        CStringArray parts;NStrProc::SplitString(code,parts,'.');
        if(parts.GetSize()<1||parts.GetSize()>2)throw std::runtime_error("invalid GiveItem code");
        for(int i=0;i<parts.GetSize();++i)parts[i].MakeLower();
        spellItem=parts[0]=="spell container";
        if(spellItem) {if(!spell||parts.GetSize()!=1)throw std::runtime_error("spell container requires [spell]");return;}
        auto* items=theWorldServer.GetItemManager();shape=items->GetGeneralShape(parts[0]);
        if(!shape)throw std::runtime_error("item_prototype_not_found");
        const auto type=shape->GetTypeID();
        if(type!=ITID_ITEM_WEAPON&&type!=ITID_ITEM_ARMOR&&type!=ITID_ITEM_QUICK)
            throw std::runtime_error("item_has_no_equipment_slot");
        if(parts.GetSize()==2) {
            material=items->GetMaterial(parts[1]);if(!material)throw std::runtime_error("item_material_not_found");
        } else if(type==ITID_ITEM_WEAPON||type==ITID_ITEM_ARMOR)
            throw std::runtime_error("weapon_and_armor_require_material");
    }
    bool Matches(CItem* item) const {
        if(!item||item->GetCount()<=0)return false;
        if(spellItem) {if(item->GetTypeID()!=ITID_ITEM_SPELL)return false;}
        else if(item->GetShapeEntry()!=shape||item->GetTypeID()!=shape->GetTypeID())return false;
        if(material?theWorldServer.GetItemManager()->GetMaterial(int(item->GetMaterial()))!=material:item->GetMaterial()!=0)
            return false;
        auto* current=item->GetSpell(0);
        if(!spell)return current==nullptr;
        return current&&current->GetPrototype()==spell&&current->GetRunesCount()==int(runes.size())&&
            (runes.empty()||std::memcmp(current->GetRunes(),runes.data(),runes.size())==0);
    }
};
CItem* EquipmentItem(CUnit* unit,int kind,int slot) {
    if(kind==ITID_ITEM_WEAPON)return unit->GetQWeapon(slot);
    if(kind==ITID_ITEM_ARMOR)return unit->GetQArmor(slot);
    if(kind==ITID_ITEM_QUICK)return unit->GetQItem(slot);
    return unit->GetQSpellItem(slot);
}
CItem* PersonageEquipment(CPersonage* personage,int kind,int slot) {
    if(!personage || slot<0)return nullptr;
    if(kind==ITID_ITEM_WEAPON)return slot<personage->weapons.GetSize()?personage->weapons[slot]:nullptr;
    if(kind==ITID_ITEM_QUICK)return slot<personage->items.GetSize()?personage->items[slot]:nullptr;
    if(kind==ITID_ITEM_SPELL)return slot<personage->spells.GetSize()?personage->spells[slot]:nullptr;
    if(kind==ITID_ITEM_ARMOR)for(int i=0;i<personage->armors.GetSize();++i)
        if(personage->armors[i]->GetSlotIndex()==slot)return personage->armors[i];
    return nullptr;
}
const char* EquipmentPrefix(int kind) {
    return kind==ITID_ITEM_WEAPON?"weapon":kind==ITID_ITEM_ARMOR?"armor":kind==ITID_ITEM_QUICK?"quick":"spell";
}
template<class Items,class Add,class Remove>
CItem* CampEquipmentSlot(CParty& party,Items& items,int slot,CItem* input,Add add,Remove remove) {
    auto* previous=slot<items.GetSize()?items[slot]:nullptr;
    if(previous&&previous->GetCount()>0)party.AddItemToWagon(previous);
    CItem* stored=add(input); // CPersonage::AddXXX duplicates into the party, as in camp.
    if(previous) {
        std::swap(items[slot],items[items.GetSize()-1]);remove(previous);
    }
    return stored;
}
bool EquipUnit(Job& job,Clock::time_point deadline) {
    const auto& params=job.request.at("params");const int id=PositiveId(params.at("id"));
    const auto actor=ActorError(id);if(!actor.empty())throw std::runtime_error(actor);
    auto* unit=QueryUnit(id);auto* player=theWorldServer.GetPlayer(0);auto& party=player->party;
    auto* pers=party.GetPersonage(id);auto* clientPers=Player()->party.GetPersonage(id);
    if(!pers||!clientPers)throw std::runtime_error("unit_has_no_camp_personage");
    if(job.phase) {
        if(job.orderEpoch!=owned::bridge_current_world_epoch())throw std::runtime_error("world_changed_during_equip; command_not_retried");
    } else {
        job.orderEpoch=owned::bridge_current_world_epoch();job.phase=1;job.bucket=job.item=0;
    }
    if(job.phase==2) {
        const int kind=job.response.at("kind"),slot=job.response.at("slot");
        auto* current=EquipmentItem(unit,kind,slot);
        auto* visible=EquipmentItem(theWorld.GetUnit(id),kind,slot);
        CItem* clientItem=PersonageEquipment(clientPers,kind,slot);
        if(kind==ITID_ITEM_ARMOR)visible=clientItem; // Client armor is a subtype/figure, owned item lives in its party.
        if(current&&visible&&clientItem&&current->GetID()==job.response.at("ability_item_id").get<int>()&&
           clientItem->GetRTID()!=job.response.at("client_previous_item_rtid").get<ObjectId>()&&
           ItemCode(clientItem)==job.response.at("code").get<std::string>()&&
           ItemCode(visible)==job.response.at("code").get<std::string>()&&
           (kind!=ITID_ITEM_WEAPON||theWorld.GetUnit(id)->GetCurrentWeaponSlotIndex()==DWORD(slot))) {
            auto item=ItemJson(current,std::string(EquipmentPrefix(kind))+":"+std::to_string(slot));
            job.response["item"]=std::move(item);job.response["ok"]=true;job.response["status"]="done";
            job.response["client_item_id"]=clientItem->GetID();
            job.response["synchronization_basis"]="server_runtime_id_and_new_client_slot_instance";
            job.response["selected"]=kind==ITID_ITEM_WEAPON;return true;
        }
        if(Clock::now()>=job.orderExpires)
            throw std::runtime_error("equipment_applied_but_client_not_synchronized; command_not_retried");
        return false;
    }
    EquipRecipe recipe(params.at("item"));
    CItem* found=nullptr;bool wagon=false;
    while(Clock::now()<deadline&&!found) {
        if(job.item>=200)throw std::runtime_error("equipment_inventory_scan_limit; narrow inventory to at most 200 items per source");
        if(job.bucket==0) {
            auto pos=player->items.GetHeadPosition();size_t index=0;
            while(pos&&index++<job.item)player->items.GetNext(pos);
            if(!pos){job.bucket=1;job.item=0;continue;}
            auto* item=player->items.GetNext(pos);++job.item;
            if(recipe.Matches(item))found=item;
        } else {
            if(job.item>=size_t(party.allWagonItems.GetSize()))throw std::runtime_error("item_not_found_in_inventory_or_wagon");
            auto* item=party.allWagonItems[int(job.item++)];
            if(recipe.Matches(item)){found=item;wagon=true;}
        }
    }
    if(!found)return false;
    if(found->IsBroken())throw std::runtime_error("item_broken");
    const int kind=found->GetTypeID();int count=0,maximum=0,slot=-1;
    if(params.contains("slot")) {
        const auto& value=params.at("slot");
        if(!value.is_number_integer()||value<0||value>INT_MAX)throw std::runtime_error("slot must be a nonnegative integer");
        slot=value.get<int>();
    }
    if(kind==ITID_ITEM_WEAPON) {
        count=pers->weapons.GetSize();maximum=QI_MAX_WEAPONS;
        if(static_cast<CItemWeapon*>(found)->GetWeaponType()==RPGWST_HANDS1)
            for(int i=0;i<count;++i)if(pers->weapons[i]->GetWeaponType()==RPGWST_HANDS1) {
                if(slot>=0&&slot!=i)throw std::runtime_error("shield_must_replace_existing_shield_slot");slot=i;
            }
    } else if(kind==ITID_ITEM_ARMOR) {
        count=maximum=RPG_NUM_ARMOR_TYPES;const int armorSlot=static_cast<CItemArmor*>(found)->GetSlotIndex();
        if(slot>=0&&slot!=armorSlot)throw std::runtime_error("armor_slot_does_not_match_item");slot=armorSlot;
    } else if(kind==ITID_ITEM_QUICK) {
        count=pers->items.GetSize();maximum=QI_MAX_ITEMS;
        auto* mod=ModRuntime::Current();const std::string code=found->GetShapeEntry()->GetCode();
        // Reuse the camp's limits, with the pending replacement excluded.
        std::vector<int> replaced;
        if(!ModSettingsPolicy::EquipQuick(code,mod&&mod->CombatSetting("JMVOneAmuletLimit"),
            mod&&mod->CombatSetting("JMVRingsLimit"),[&](){return count;},
            [&](int i){return i==slot?std::string():std::string(pers->items[i]->GetShapeEntry()->GetCode());},
            [&](int i){replaced.push_back(i);}))throw std::runtime_error("ring_limit");
        for(int index:replaced) {
            if(slot>=0&&slot!=index)throw std::runtime_error("amulet_must_replace_existing_amulet_slot");slot=index;
        }
    } else {count=pers->spells.GetSize();maximum=int(unit->GetSpellSlotCount());}
    if(slot<0)slot=count;
    if(slot>=maximum||slot>count)throw std::runtime_error("equipment_slot_full_or_not_contiguous");
    if(kind!=ITID_ITEM_ARMOR)for(int i=0;i<count;++i)
        if(!EquipmentItem(unit,kind,i))throw std::runtime_error("equipment_slots_not_synchronized");
    const auto* previousClientItem=PersonageEquipment(clientPers,kind,slot);
    const ObjectId previousClientIdentity=previousClientItem?previousClientItem->GetRTID():0;
    // Keep the inventory input alive until its normal removal packet is queued.
    CSmartPtr<CItem> held=found;
    CSmartPtr<CItem> input=wagon?found:party.DuplicateItem(found);
    CItem* stored=nullptr;CItem* runtime=nullptr;
    if(kind==ITID_ITEM_WEAPON) {
        stored=CampEquipmentSlot(party,pers->weapons,slot,input,
            [&](CItem* p){return pers->AddWeapon(static_cast<CItemWeapon*>(p));},[&](CItemWeapon* p){pers->RemoveWeapon(p);});
        runtime=theWorldServer.DuplicateItemWithSrc(stored);unit->RemoveWeapon(slot);
        unit->AddWeapon(static_cast<CItemWeapon*>(runtime));unit->ChangeWeapon(slot);pers->dwCurrentWeapon=DWORD(slot);
    } else if(kind==ITID_ITEM_ARMOR) {
        for(int i=0;i<pers->armors.GetSize();++i)if(pers->armors[i]->GetSlotIndex()==slot) {
            auto* old=pers->armors[i];party.AddItemToWagon(old);pers->RemoveArmor(old);break;
        }
        stored=pers->AddArmor(static_cast<CItemArmor*>(input.GetPtr()));
        pers->stats.RemoveAllArmors();
        for(int i=0;i<pers->armors.GetSize();++i)pers->stats.Wear(pers->armors[i]);
        runtime=theWorldServer.DuplicateItemWithSrc(stored);unit->Wear(static_cast<CItemArmor*>(runtime));
    } else if(kind==ITID_ITEM_QUICK) {
        stored=CampEquipmentSlot(party,pers->items,slot,input,
            [&](CItem* p){return pers->AddQuick(static_cast<CItemQuick*>(p));},[&](CItemQuick* p){pers->RemoveQuick(p);});
        runtime=theWorldServer.DuplicateItemWithSrc(stored);unit->RemoveQItem(slot);unit->AddQItem(static_cast<CItemQuick*>(runtime));
    } else {
        stored=CampEquipmentSlot(party,pers->spells,slot,input,
            [&](CItem* p){return pers->AddSpell(static_cast<CItemSpell*>(p));},[&](CItemSpell* p){pers->RemoveSpell(p);});
        runtime=theWorldServer.DuplicateItemWithSrc(stored);unit->RemoveQSpell(slot);unit->AddQSpell(static_cast<CItemSpell*>(runtime));
    }
    const int sourceId=found->GetID();
    if(wagon)party.RemoveItemFromWagon(found);
    else {
        const int remaining=found->GetCount()-1;player->RemoveItemByID(sourceId);
        if(remaining>0) {
            held->SetCount(remaining);CList<CItem*,CItem*> remainder;remainder.AddTail(held);player->AddItemsToHoover(remainder);
        }
    }
    unit->RecalcAIClass();unit->RecalcRPGStats();unit->GetUnitSpecific()->NotifyItemsUpdated();
    unit->SetUpdateMask(CMapObject::VLD_MASK_INVENTORY);unit->SetUpdated();
    static_cast<CPlayerServer*>(player->GetPlayerSpecific())->SendPartyItems();
    job.phase=2;job.orderExpires=Clock::now()+std::chrono::milliseconds(3000);
    job.response={{"ok",false},{"status","synchronizing"},{"service",true},{"id",id},{"slot",slot},{"kind",kind},
        {"code",ItemCode(runtime)},{"runtime_item_id",runtime->GetID()},{"party_item_id",stored->GetID()},
        {"ability_item_id",EquipmentItem(unit,kind,slot)->GetID()},
        {"source_item_id",sourceId},{"source",wagon?"wagon":"inventory"},{"world_epoch",job.orderEpoch},
        {"client_previous_item_rtid",previousClientIdentity},
        {"path","camp_personage_and_live_deployment"},{"truncated",false}};
    return false;
}
