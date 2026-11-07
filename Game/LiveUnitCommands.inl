// Included inside LiveObserver's private namespace, after CommandPoint.
// Only MCP requests allocate observations. Event callbacks retain scalars for
// explicitly watched IDs; there is no new frame poll or saved game state.
struct HitObservation {
    bool present=false;
    int attacker=-1,target=-1,tick=0,outcome=0,critical=-1;
    float damage=0;
};
struct UnitObservation {
    HitObservation received,dealt;
    std::string refusal;
    unsigned refusalCode=0;
    int refusalTick=0;
};
std::unordered_map<int,UnitObservation> unitObservations;
std::uint64_t observationEpoch=0;
void WatchUnit(int id) {
    const auto epoch=owned::bridge_current_world_epoch();
    if(observationEpoch!=epoch){unitObservations.clear();observationEpoch=epoch;}
    if(unitObservations.find(id)!=unitObservations.end())return;
    if(unitObservations.size()>=UnitLimit)throw std::runtime_error("MCP watched-unit limit reached for this world (200)");
    unitObservations.emplace(id,UnitObservation{});
}
const char* RefusalReason(unsigned code) {
    switch(code) {
    case 11:return "no_way_to_attack";case 12:return "target_too_high";
    case 13:return "cannot_change_pose";case 14:return "path_not_found";
    case 15:return "cannot_cast";case 16:return "cannot_teleport";
    case 17:return "science_failed";case 18:return "cannot_see_target";
    case 19:return "spell_too_complex";case 20:return "cannot_run_overloaded";
    case 21:return "cannot_run_injured";case 22:return "cannot_tame";
    case 23:return "cannot_change_weapon";case 24:return "cannot_link_spell";
    case 25:return "spell_link_aborted";case 26:return "cannot_charm";
    case 27:return "cannot_start_rest";case 29:return "out_of_stamina";
    default:return nullptr;
    }
}
OrderReceipt* PendingReceipt(int id,int packet) noexcept {
    if(!active||active->cancelled||active->readyToFinish||active->phase!=1)return nullptr;
    for(auto& receipt:active->orders)
        if(receipt.id==id&&receipt.packet==packet&&!receipt.dispatched)return &receipt;
    return nullptr;
}
void RememberRefusal(int id,const std::string& reason,unsigned code=0) {
    if(observationEpoch!=owned::bridge_current_world_epoch())return;
    auto found=unitObservations.find(id);
    if(found!=unitObservations.end()) {
        found->second.refusal=reason;found->second.refusalCode=code;
        found->second.refusalTick=theWorldServer.GetSegmentTimeCounter();
    }
}
json HitJson(const HitObservation& hit) {
    if(!hit.present)return nullptr;
    return {{"attacker",hit.attacker},{"target",hit.target},{"tick",hit.tick},
        {"damage",hit.damage},{"outcome",hit.outcome},{"critical",hit.critical}};
}
const char* CommandLabel(int command) {
    static const char* names[]={"idle","move","rotate","attack","cast_point","cast_unit",
        "skill","animation","animation_cycle","none","trap","rest"};
    return command>=0&&command<int(sizeof(names)/sizeof(names[0]))?names[command]:"unknown";
}
json CommandJson(const SUnitCommand& command) {
    return {{"type",CommandLabel(command.cmd)},{"code",int(command.cmd)},
        {"target",command.GetTargetID()},{"point",Point(command.ptTarget)},
        {"slot",command.nQSpellNum},{"run",command.bDoItFast}};
}
json CombatJson(CUnit* unit) {
    WatchUnit(unit->GetID());
    const auto& observed=unitObservations.at(unit->GetID());
    json result={{"tick",theWorldServer.GetSegmentTimeCounter()},{"last_received",HitJson(observed.received)},{"last_dealt",HitJson(observed.dealt)},
        {"observation_scope","since_first_query_or_order_in_this_world"},
        {"last_refusal",observed.refusal.empty()?json(nullptr):json{{"reason",observed.refusal},
            {"code",observed.refusalCode},{"tick",observed.refusalTick}}},
        {"animation",{{"start",unit->GetStartAnimationId()},{"cycle",unit->GetCycleAnimationId()},
            {"extended",unit->HasExtendedAnimationId()}}}};
    auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
    if(server) {
        result["current_order"]=CommandJson(server->GetScriptCommand());
        result["next_order"]=CommandJson(server->GetNextScriptCommand());
        result["failed"]=server->IsCmdFailed();
        result["attack_cooldown_ticks"]=(std::max)(0,LiveSnapshotAccess::AttackReady(*server)-theWorldServer.GetSegmentTimeCounter());
    }
    return result;
}
int PositiveId(const json& value) {
    if(!value.is_number_integer()||value<=0||value>INT_MAX)throw std::runtime_error("id must be a positive 32-bit integer");
    return value.get<int>();
}
std::vector<int> ActorIds(const json& params) {
    if(params.contains("id")==params.contains("ids"))throw std::runtime_error("supply exactly one of id or ids");
    std::vector<int> ids;
    if(params.contains("id"))ids.push_back(PositiveId(params.at("id")));
    else {
        const auto& values=params.at("ids");
        if(!values.is_array()||values.empty()||values.size()>16)throw std::runtime_error("ids must contain 1..16 distinct unit IDs");
        for(const auto& value:values) {
            const int id=PositiveId(value);
            if(std::find(ids.begin(),ids.end(),id)!=ids.end())throw std::runtime_error("duplicate actor id");
            ids.push_back(id);
        }
    }
    return ids;
}
std::string ActorError(int id,bool acting=true) {
    auto* client=theWorld.GetPlayer();auto* player=theWorldServer.GetPlayer(0);
    auto* unit=theWorldServer.GetUnit(id);auto* visible=theWorld.GetUnit(id);
    if(!client||!player||!unit||unit->IsInvalid()||unit->IsUnreferenced())return "unit_not_found";
    if(unit->GetOwner()!=player||!player->unitsUnderControl.Seek(id)||!visible||
       visible->GetOwner()!=client->GetPlayer()||!client->IsUnitUnderControl(visible))return "unit_not_under_player_control";
    if(!acting)return {};
    if(unit->IsDead())return "unit_dead";
    if(unit->CheckFlag(UNIT_BLOCKED))return "unit_blocked";
    if(theWorldServer.curInterface!=IFACE_MISSION||player->curInterface!=IFACE_MISSION||
       client->GetPlayer()->curInterface!=IFACE_MISSION||client->GetStatus()!=CPlayerClient::CPS_ACTIVE)return "mission_not_active";
    auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
    if(!server||!server->logic.GetPlayer())return "unit_has_no_player_logic";
    return {};
}
int Selector(const json& value,const char* prefix,int maximum) {
    if(value.is_number_integer()) {
        if(value>=0&&value<maximum)return value.get<int>();
    } else if(value.is_string()) {
        const auto name=value.get<std::string>();const std::string start=std::string(prefix)+":";
        if(name.rfind(start,0)==0&&name.size()>start.size()&&name.size()<=start.size()+2) {
            int slot=0;
            for(size_t i=start.size();i<name.size();++i){if(name[i]<'0'||name[i]>'9')throw std::runtime_error("invalid slot selector");slot=slot*10+name[i]-'0';}
            if(slot<maximum)return slot;
        }
    }
    throw std::runtime_error("invalid slot selector; use an id returned by unit_abilities");
}
CUnit* QueryUnit(int id) {
    auto* unit=theWorldServer.GetUnit(id);
    if(!unit||unit->IsInvalid()||unit->IsUnreferenced())throw std::runtime_error("unit_not_found");
    return unit;
}
CPlot3D MissionPoint(const json& params) {
    auto point=CommandPoint(params);
    // UI ground picking returns the actual terrain/bridge height, not zero.
    if(!params.contains("z"))point.z=theWorld.GetBridgeZValue(point.x,point.y);
    return point;
}
CUnit* CommandTarget(const json& params) {
    auto* target=QueryUnit(PositiveId(params.at("target")));
    auto* visible=theWorld.GetUnit(target->GetID());
    auto* player=Player();
    if(!visible||visible->IsInvalid()||visible->IsUnreferenced()||!player||(!player->visibleObjects.Seek(target->GetID())&&!player->unitsUnderControl.Seek(target->GetID())))
        throw std::runtime_error("target_not_visible_to_player");
    return target;
}
std::string ItemCode(CItem* item) {
    const auto* shape=item->GetShapeEntry();
    std::string code=item->GetTypeID()==ITID_ITEM_SPELL?"spell container":shape?shape->GetCode():"";
    if(item->GetTypeID()==ITID_ITEM_WEAPON||item->GetTypeID()==ITID_ITEM_ARMOR||item->GetMaterial()!=0)
        code+="."+std::string(item->GetMaterialCode());
    if(auto* spell=item->GetSpell(0)) {
        code+=" ["+std::string(spell->GetPrototype()->szCodeName);
        if(spell->GetRunesCount()) {
            code+="{";
            for(int i=0;i<spell->GetRunesCount();++i) {
                const auto* rune=theWorldServer.GetSpellManager()->GetModifier(static_cast<unsigned char>(spell->GetRunes()[i])-1);
                if(i)code+=";";
                if(rune)code+=rune->GetCodeName();
            }
            code+="}";
        }
        code+="]";
    }
    return code;
}
json ItemJson(CItem* item,const std::string& selector) {
    CString name,description,type;
    if(auto* text=theWorld.GetTextManager())text->GetItemNameAndDesc(item,name,description,type);
    return {{"id",selector},{"object_id",item->GetID()},{"name",Text(name)},{"code",ItemCode(item)},
        {"type",Text(type)},{"class",item->GetGameClassName()},{"count",item->GetCount()},
        {"mana",item->GetMP()},{"max_mana",item->GetMaxMP()},{"broken",item->IsBroken()}};
}
int AbilitySlot(CUnit* unit,const json& value,bool quick) {
    const int count=quick?int(QI_MAX_ITEMS):int(unit->GetSpellSlotCount());
    const char* prefix=quick?"quick":"spell";
    if(!value.is_string()||value.get_ref<const std::string&>().rfind(std::string(prefix)+":",0)==0)
        return Selector(value,prefix,count);
    const auto name=value.get<std::string>();
    if(name.empty()||name.size()>512)throw std::runtime_error("ability_name_must_contain_1..512_UTF8_bytes");
    int found=-1;
    for(int slot=0;slot<count;++slot) {
        CItem* item=quick?static_cast<CItem*>(unit->GetQItem(slot)):static_cast<CItem*>(unit->GetQSpellItem(slot));
        auto* spell=quick?(item?item->GetSpell(0):nullptr):unit->GetQSpell(slot);
        if(!spell)continue;
        const auto code=Text(spell->GetPrototype()->szCodeName);
        const auto row=item?ItemJson(item,""):json{{"name",code}};
        if(name!=code&&name!=row["name"].get<std::string>())continue;
        if(found>=0)throw std::runtime_error("ambiguous_ability_name; use_spell_or_quick_slot_selector");
        found=slot;
    }
    if(found<0)throw std::runtime_error("ability_name_not_found_in_current_slots");
    return found;
}
json SpellAvailability(CUnit* unit,int slot,bool quick,CUnit* target,const CPlot3D* point) {
    auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
    CItemQuick* item=quick?unit->GetQItem(slot):nullptr;
    auto* spell=quick?(item?item->GetSpell(0):nullptr):unit->GetQSpell(slot);
    json reasons=json::array();
    const auto actor=ActorError(unit->GetID());if(!actor.empty())reasons.push_back(actor);
    if(!spell){reasons.push_back("no_spell");return {{"available",false},{"reasons",reasons}};}
    const bool consumable=item&&item->GetQuickItemType()==IDB_SCROLL;
    float cost=spell->GetManaCost();int cooldown=0;bool skill=false,pointSkill=false;
    auto* mod=ModRuntime::Current();ModSkills::Config config;
    if(!quick&&mod) {
        skill=mod->SkillConfig(spell,config);pointSkill=mod->PointSkill(spell);
        if(skill) {
            cost=config.Cost(cost);cooldown=mod->SkillCooldownSnapshot(*unit,spell);
            auto* weapon=unit->GetCurrentQWeapon();
            if(!weapon||!config.Weapon(weapon->GetWeaponType()))reasons.push_back("incompatible_weapon");
            const int left=ModStats::LeftHand(*unit);
            auto* off=left>=0&&left!=int(unit->GetCurrentWeaponSlotIndex())?unit->GetQWeapon(left):nullptr;
            if(!config.allowDualWield&&off&&off->GetWeaponType()!=RPGWST_HANDS1)reasons.push_back("dual_wield_not_allowed");
            const auto proficiency=config.requiredArchery>=0?RPGASKL_ARCHERY:RPGASKL_MELEE;
            if(!server||server->GetRPGStats()->GetAutoSkill(proficiency)<config.Requirement(float(spell->GetComplexity())))reasons.push_back("insufficient_skill");
            if(cooldown>0)reasons.push_back("cooldown");
        } else if(server) {
            const auto& stats=*server->GetRPGStats();
            const bool natural=mod->SpellAddon().Feature18()==4;
            cost*=natural?stats.GetSpellAddonManaBonusMultiplier():stats.GetManaBonusMultiplier();
            if(natural&&stats.rpgNAWWeaponType==RPGWST_MAGIC)cost=0;
        }
    }
    const float mana=quick?item->GetMP():server&&server->GetMapObjectServer()?server->GetMapObjectServer()->GetCurrentMP():unit->GetMP();
    float requiredMana=cost;
    if(!quick&&!skill&&server) {
        auto* stats=server->GetRPGStats();
        if(unit->IsHero()&&!stats->CanCastSpell(spell->GetMagicSubtype(),spell->GetComplexity()))reasons.push_back("spell_too_complex");
        // Read the result of SyncNpcMagicState without changing UNIT_FEEBLED.
        bool feebled=unit->CheckFlag(UNIT_FEEBLED)!=0;
        if(mod&&mod->SpellAddon().Feature29()==1) {
            const int effectSlot=unit->HasSpellEffect(SPELL_ID_FEEBLEMIND);
            feebled=effectSlot&&stats->rpgNAWWeaponType!=RPGWST_MAGIC;
            auto* probe=unit->GetQSpell(effectSlot);
            if(!feebled)feebled=probe&&!unit->IsHero()&&
                double(probe->GetManaCost())*stats->GetSpellAddonManaBonusMultiplier()>mana;
        } else if(mod&&!unit->IsHero())
            feebled=unit->HasSpellEffect(SPELL_ID_FEEBLEMIND)&&stats->rpgNAWWeaponType!=RPGWST_MAGIC;
        if(feebled)reasons.push_back("feeblemind");
        if(!mod)cost*=stats->GetManaBonusMultiplier();
        const bool natural=mod&&mod->SpellAddon().Feature18()==4;
        requiredMana=(natural?stats->rpgNAWWeaponType!=RPGWST_MAGIC:unit->IsHero())?cost:0;
        // The ordered LS mana check remains in force for NPC/companion slots,
        // including a natural MAGIC caster that bypasses only native f18.
        if(mod&&(!unit->IsHero()||mod->CompanionSkillAI(*unit))&&slot<8)
            requiredMana=(std::max)(requiredMana,(std::max)(0.f,spell->GetManaCost()*stats->GetSpellAddonManaBonusMultiplier()-0.0001f));
    }
    if(!consumable&&requiredMana>mana)reasons.push_back(quick?"item_mana":"mana");
    if(quick&&(!item||item->GetCount()<=0))reasons.push_back("item_empty");
    if(quick&&item->GetQuickItemType()!=IDB_WAND&&!consumable)reasons.push_back("item_has_no_mission_use_command");
    if(target) {
        if(target->IsDead())reasons.push_back("target_dead");
        else if(consumable&&spell->IsOnUnit()&&target!=unit)reasons.push_back("scroll_requires_self_target");
        else if(!pointSkill&&!spell->IsValidTarget(unit,target))reasons.push_back("invalid_spell_target");
        if(!spell->IsOnUnit()&&!spell->IsOnTile()&&!pointSkill)reasons.push_back("unsupported_target_kind");
    } else if(point&&!spell->IsOnTile()&&!pointSkill)reasons.push_back("spell_requires_unit_target");
    float range=spell->GetRange();
    if(skill&&server)range=server->GetCurrentWeaponRange();
    const CPlot3D* destination=point;CPlot3D targetPoint;
    if(target){targetPoint=target->GetCP();destination=&targetPoint;}
    json distance=nullptr,inRange=nullptr;
    if(destination) {
        const auto& origin=unit->GetCP();const double dx=destination->x-origin.x,dy=destination->y-origin.y;
        distance=std::sqrt(dx*dx+dy*dy);inRange=distance.get<double>()<=range;
    }
    const bool ready=reasons.empty();
    json hereReasons=reasons;
    if(!destination)hereReasons.push_back("target_not_supplied");
    else if(!inRange.get<bool>())hereReasons.push_back("out_of_range");
    return {{"available",ready},{"reasons",reasons},{"can_execute_here",hereReasons.empty()},
        {"execution_reasons",hereReasons},{"mana_cost",consumable?0.f:cost},{"required_mana",consumable?0.f:requiredMana},{"mana",mana},
        {"cooldown_ticks",cooldown},{"cooldown_seconds",cooldown/15.0},{"range",range},
        {"distance",distance},{"in_range",inRange},{"on_unit",bool(spell->IsOnUnit())},
        {"on_point",bool(spell->IsOnTile()||pointSkill)},{"offensive",spell->IsOffensive()},
        {"approaches_target",!pointSkill},{"code",Text(spell->GetPrototype()->szCodeName)}};
}
bool Abilities(Job& job,Clock::time_point deadline) {
    const auto& params=job.request.at("params");const int id=PositiveId(params.at("id"));auto* unit=QueryUnit(id);
    if(!job.phase) {
        WatchUnit(id);job.world=&theWorldServer;job.worldId=theWorldServer.GetRTID();job.phase=1;
        job.orderEpoch=owned::bridge_current_world_epoch();
        job.response={{"ok",true},{"id",id},{"weapons",json::array()},{"spells",json::array()},
            {"quick_items",json::array()},{"armor",json::array()},{"inventory",json::array()},{"truncated",false},
            {"world_epoch",job.orderEpoch},{"consistency","per_frame_live_slots"},
            {"controlled",ActorError(id,false).empty()}};
    }
    if(!SameWorld(job,theWorldServer)||job.orderEpoch!=owned::bridge_current_world_epoch())throw std::runtime_error("world_changed_during_abilities");
    CUnit* target=nullptr;CPlot3D point;const CPlot3D* destination=nullptr;
    if(params.contains("target"))target=CommandTarget(params);
    if(params.contains("x")||params.contains("y")) {
        if(target)throw std::runtime_error("supply target or x and y");
        point=MissionPoint(params);destination=&point;
    }
    auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific());
    if(!job.response.contains("attack")&&server) {
        const int cooldown=(std::max)(0,LiveSnapshotAccess::AttackReady(*server)-theWorldServer.GetSegmentTimeCounter());
        const float range=server->GetCurrentWeaponRange();
        auto* weapon=unit->GetCurrentQWeapon();
        json reasons=json::array();const auto actor=ActorError(id);
        if(!actor.empty())reasons.push_back(actor);
        if(target&&(target==unit||target->IsDead()))reasons.push_back("invalid_attack_target");
        if(target&&!LiveSnapshotAccess::AttackHeight(*server,target))reasons.push_back("target_too_high");
        const bool available=reasons.empty();const auto eligibility=reasons;json distance=nullptr,inRange=nullptr;
        json reach=nullptr;
        if(target) {
            const auto delta=target->GetCP()-unit->GetCP();distance=std::sqrt(double(delta.x)*delta.x+double(delta.y)*delta.y);
            reach=LiveSnapshotAccess::AttackReach(*server,target);
            inRange=distance.get<double>()<=reach.get<float>();
        }
        if(!target)reasons.push_back("unit_target_not_supplied");
        else if(!inRange.get<bool>())reasons.push_back("out_of_range");
        if(cooldown>0)reasons.push_back("cooldown");
        job.response["attack"]={{"available",available},{"reasons",eligibility},{"can_execute_here",reasons.empty()},
            {"execution_reasons",reasons},{"natural",weapon==nullptr},{"weapon_type",int(unit->GetCurrentWeaponType())},
            {"weapon",weapon?json("weapon:"+std::to_string(unit->GetCurrentWeaponSlotIndex())):json(nullptr)},
            {"range",range},{"reach",reach},{"range_basis","weapon_or_natural_extension; reach_includes_bodies_and_spacing"},
            {"path_checked",false},
            {"distance",distance},{"in_range",inRange},{"cooldown_ticks",cooldown},{"approaches_target",true}};
    }
    const int spells=(std::min)(int(unit->GetSpellSlotCount()),32);
    while(Clock::now()<deadline) {
        const int slot=int(job.item++);
        if(job.phase==1) {
            if(slot>=QI_MAX_WEAPONS){job.phase=2;job.item=0;continue;}
            auto* weapon=unit->GetStoredQWeapon(slot);if(!weapon)continue;
            auto row=ItemJson(weapon,"weapon:"+std::to_string(slot));
            row["slot"]=slot;row["selected"]=unit->GetCurrentWeaponSlotIndex()==DWORD(slot);
            row["order"]="weapon:"+std::to_string(slot);row["available"]=ActorError(id).empty()&&!weapon->IsBroken();
            row["reasons"]=json::array();
            const auto actor=ActorError(id);if(!actor.empty())row["reasons"].push_back(actor);
            if(weapon->IsBroken())row["reasons"].push_back("weapon_broken");
            if(server){row["current_weapon_range"]=server->GetCurrentWeaponRange();row["attack_cooldown_ticks"]=(std::max)(0,LiveSnapshotAccess::AttackReady(*server)-theWorldServer.GetSegmentTimeCounter());}
            job.response["weapons"].push_back(std::move(row));
        } else if(job.phase==2) {
            if(slot>=spells){job.phase=3;job.item=0;continue;}
            auto* spell=unit->GetQSpell(slot);auto* item=unit->GetQSpellItem(slot);if(!spell)continue;
            auto row=item?ItemJson(item,"spell:"+std::to_string(slot)):json{{"id","spell:"+std::to_string(slot)},{"name",Text(spell->GetPrototype()->szCodeName)}};
            row["slot"]=slot;row["availability"]=SpellAvailability(unit,slot,false,target,destination);
            job.response["spells"].push_back(std::move(row));
        } else if(job.phase==3) {
            if(slot>=QI_MAX_ITEMS){job.phase=4;job.item=0;continue;}
            auto* item=unit->GetQItem(slot);if(!item)continue;
            auto row=ItemJson(item,"quick:"+std::to_string(slot));row["slot"]=slot;
            row["quick_type"]=int(item->GetQuickItemType());row["availability"]=SpellAvailability(unit,slot,true,target,destination);
            job.response["quick_items"].push_back(std::move(row));
        } else if(job.phase==4) {
            if(slot>=RPG_NUM_ARMOR_TYPES){job.phase=5;job.item=0;continue;}
            auto* armor=unit->GetQArmor(slot);if(!armor)continue;
            auto row=ItemJson(armor,"armor:"+std::to_string(slot));row["slot"]=slot;
            row["mission_equip"]=false;row["service_equip"]="unit_equip";
            job.response["armor"].push_back(std::move(row));
        } else {
            auto* player=theWorldServer.GetPlayer(0);
            if(!player||!player->IsMainHero(unit))return true;
            if(slot>=200){job.response["truncated"]=size_t(player->items.GetCount())>200;return true;}
            // ID bookmark avoids retaining a list iterator across game frames.
            auto pos=player->items.GetHeadPosition();int index=0;
            while(pos&&index++<slot)player->items.GetNext(pos);
            if(!pos)return true;
            auto* item=player->items.GetNext(pos);if(!item)continue;
            auto row=ItemJson(item,"inventory:"+std::to_string(item->GetID()));
            row["mission_use"]=false;row["service_equip"]="unit_equip";
            job.response["inventory"].push_back(std::move(row));
        }
    }
    return false;
}
bool SquadRead(Job& job,Clock::time_point deadline) {
    auto* client=CommandClient();auto* player=client->GetPlayer();
    if(!job.phase) {
        job.world=&theWorldServer;job.worldId=theWorldServer.GetRTID();job.phase=1;
        job.orderEpoch=owned::bridge_current_world_epoch();
        job.response={{"ok",true},{"player",player->GetPlayerNumber()},{"main_hero",nullptr},
            {"members",json::array()},{"selected",json::array()},{"truncated",false},{"world_epoch",job.orderEpoch},
            {"leader_scope","main_hero_and_per_unit_follow_leader; no_single_global_leader"}};
    }
    if(!SameWorld(job,theWorldServer)||job.orderEpoch!=owned::bridge_current_world_epoch())throw std::runtime_error("world_changed_during_squad");
    const auto& objects=theWorldServer.dynamicObjs;
    auto it=job.unitScanStarted?objects.ObserverFind(int(job.unitNextKey)):objects.begin();const auto end=objects.end();
    if(job.unitScanStarted&&!job.unitScanFinished&&it==end)throw std::runtime_error("squad_cursor_removed_during_query");
    while(it!=end&&!job.unitScanFinished&&Clock::now()<deadline) {
        CMapObject* object=it->second;++it;job.unitScanStarted=true;job.unitScanFinished=it==end;
        if(!job.unitScanFinished)job.unitNextKey=it->first;
        if(!object||object->WhoAmI()!=HPC_UNIT||object->IsInvalid()||object->IsUnreferenced())continue;
        auto* unit=static_cast<CUnit*>(object);
        if(!Squad(unit,player)&&ActorError(unit->GetID(),false)!="")continue;
        auto row=UnitRow(unit,theWorldServer,player);auto* visible=theWorld.GetUnit(unit->GetID());
        row["controlled"]=ActorError(unit->GetID(),false).empty();row["selected"]=visible&&visible->CheckFlag(OBJ_SELECTED);
        row["leader"]=nullptr;row["aggression"]=int(unit->GetAggression());
        if(auto* server=dynamic_cast<CUnitServer*>(unit->GetUnitSpecific()))
            if(auto* leader=server->logic.GetPlayerLeaderExact())row["leader"]=leader->GetID();
        if(player->IsMainHero(unit))job.response["main_hero"]=unit->GetID();
        if(row["selected"].get<bool>())job.response["selected"].push_back(unit->GetID());
        job.response["members"].push_back(std::move(row));
        if(job.response["members"].size()>=UnitLimit){job.response["truncated"]=true;return true;}
    }
    return it==end||job.unitScanFinished;
}
bool AwaitPlayerOrder(Job& job) {
    if(job.orderEpoch!=owned::bridge_current_world_epoch())throw std::runtime_error("world_changed_while_waiting_for_order; command_not_retried");
    const bool expired=Clock::now()>=job.orderExpires;
    for(auto& receipt:job.orders) {
        if(!receipt.dispatched&&!expired)return false;
        if(!receipt.dispatched&&receipt.error.empty())receipt.error="server_did_not_dispatch_order_before_deadline; command_not_retried";
    }
    bool ok=true;json results=json::array();
    for(const auto& receipt:job.orders) {
        const bool accepted=receipt.dispatched&&receipt.error.empty();ok&=accepted;
        results.push_back({{"id",receipt.id},{"ok",accepted},{"packet",receipt.packet},{"dispatched",receipt.dispatched},
            {"error",receipt.error.empty()?json(nullptr):json(receipt.error)}});
    }
    job.response["ok"]=ok;job.response["status"]=ok?"accepted":"error";job.response["results"]=std::move(results);
    if(!ok) {
        const auto bad=std::find_if(job.orders.begin(),job.orders.end(),[](const auto& r){return !r.error.empty();});
        job.response["error"]=bad==job.orders.end()?"order_rejected":bad->error;
        job.response["partial"]=std::any_of(job.orders.begin(),job.orders.end(),[](const auto& r){return r.dispatched&&r.error.empty();});
    }
    return true;
}
bool PlayerUnitCommand(Job& job) {
    if(job.phase)return AwaitPlayerOrder(job);
    const auto& params=job.request.at("params");const auto& method=job.method;
    auto* client=CommandClient();const auto ids=ActorIds(params);
    CArray<CUnit*,CUnit*> actors;
    for(int id:ids) {
        WatchUnit(id);
        auto& observed=unitObservations.at(id);observed.refusal.clear();observed.refusalCode=0;observed.refusalTick=0;
        const auto error=ActorError(id);
        if(!error.empty()){RememberRefusal(id,error);throw std::runtime_error(error);}
        actors.Add(theWorld.GetUnit(id));
    }
    const bool run=params.value("run",false);CUnit* target=nullptr;CPlot3D point;
    int slot=-1,packet=0;std::string order;
    if(method=="unit_move") {point=CommandPoint(params);point.z=theWorldServer.aiMap.GetTileHeight(point);packet=PID_CMD_MOVETO;}
    else if(method=="unit_attack") {
        target=CommandTarget(params);if(target->IsDead())throw std::runtime_error("target_dead");
        if(std::find(ids.begin(),ids.end(),target->GetID())!=ids.end())throw std::runtime_error("cannot_attack_self_or_selected_actor");
        packet=PID_CMD_ATTACK;
    } else if(method=="unit_cast"||method=="unit_use_item") {
        if(ids.size()!=1)throw std::runtime_error("cast and item commands require one id");
        auto* unit=QueryUnit(ids[0]);const bool quick=method=="unit_use_item";
        slot=AbilitySlot(unit,params.at(quick?"item":"spell"),quick);
        auto* item=quick?unit->GetQItem(slot):nullptr;auto* spell=quick?(item?item->GetSpell(0):nullptr):unit->GetQSpell(slot);
        if(!spell)throw std::runtime_error("no_spell_in_slot");
        if(params.contains("target")&&(params.contains("x")||params.contains("y")))throw std::runtime_error("supply target or x and y");
        if(params.contains("target"))target=CommandTarget(params);
        else if(params.contains("x")||params.contains("y"))point=MissionPoint(params);
        else if(quick&&!spell->IsOffensive())target=unit;
        else throw std::runtime_error("explicit_target_or_point_required");
        auto availability=SpellAvailability(unit,slot,quick,target,target?nullptr:&point);
        if(!availability["available"].get<bool>())throw std::runtime_error(availability["reasons"][0].get<std::string>());
        if(target&&!spell->IsOnUnit()) {point=target->GetCP();target=nullptr;}
        packet=target?PID_CMD_CASTTARGET:PID_CMD_CASTPOINT;
        if(quick) {
            auto* visible=actors[0]->GetQItem(slot);
            if(!visible||visible->GetQuickItemType()!=item->GetQuickItemType()||visible->GetSubtypeID()!=item->GetSubtypeID()||visible->GetCount()!=item->GetCount())
                throw std::runtime_error("client_item_slot_not_synchronized");
        }
    } else if(method=="unit_order") {
        order=params.at("order").get<std::string>();
        if(order=="stop")packet=PID_CMD_MOVETO;
        else if(order=="loot") {
            if(ids.size()!=1)throw std::runtime_error("loot requires one actor; the UI chooses one executor");
            target=CommandTarget(params);if(!target->IsDead())throw std::runtime_error("loot_target_must_be_dead");
            packet=PID_CMD_SKILL;
        }
        else if(order=="run"||order=="walk"||order=="crawl"||order=="sneak"||order=="lay")packet=PID_CMD_POSE;
        else if(order=="aggressive"||order=="stealth")packet=PID_CMD_AGGRESSION;
        else if(order=="follow"||order=="unfollow") {
            packet=PID_CMD_FOLLOW;if(order=="follow") {target=CommandTarget(params);if(target->IsDead())throw std::runtime_error("follow_target_dead");}
        } else if(order.rfind("weapon:",0)==0) {
            slot=Selector(order,"weapon",QI_MAX_WEAPONS);packet=PID_CMD_SELWEAPON;
            for(int id:ids)if(!QueryUnit(id)->GetQWeapon(slot)||!theWorld.GetUnit(id)->GetQWeapon(slot))throw std::runtime_error("weapon_slot_empty_or_not_synchronized");
        } else if(order=="select") {
            // Selection is client UI state, with the same selection journal.
            theWorld.ResetSelection();for(int id:ids)theWorld.SelectObject(id);
            job.response={{"ok",true},{"status","done"},{"ids",ids},{"path","client_ui_selection"},{"truncated",false}};return true;
        } else throw std::runtime_error("unknown_order; see MCP README for supported UI commands");
    } else throw std::runtime_error("unknown_unit_command");
    if(target)WatchUnit(target->GetID());
    // Validate the entire group before enqueuing anything. Receipts are scalars,
    // tied to this job and world epoch; no game pointers cross frames.
    job.orderEpoch=owned::bridge_current_world_epoch();job.orderExpires=Clock::now()+std::chrono::milliseconds(3000);
    for(int id:ids)job.orders.push_back({id,packet,false,{}});
    job.phase=1;job.response={{"ok",true},{"status","queued"},{"ids",ids},{"path","client_to_server"},
        {"world_epoch",job.orderEpoch},{"truncated",false}};
    if(ids.size()==1)job.response["id"]=ids[0];
    if(method=="unit_move") {client->UnitCommandMoveTo(actors,point,run);job.response["target"]=Point(point);}
    else if(method=="unit_attack")client->UnitCommandAttack(actors,theWorld.GetUnit(target->GetID()),RPGHL_ALL,run);
    else if(method=="unit_cast"||method=="unit_use_item") {
        if(method=="unit_cast") {
            if(target)client->UnitCommandCast(slot,actors[0],theWorld.GetUnit(target->GetID()),run);
            else client->UnitCommandCast(slot,actors[0],point,run);
        } else {
            if(target)client->UnitCommandWand(slot,actors[0],theWorld.GetUnit(target->GetID()),run);
            else client->UnitCommandWand(slot,actors[0],point,run);
        }
    } else for(int i=0;i<actors.GetSize();++i) {
        auto* actor=actors[i];
        if(order=="stop") {auto where=QueryUnit(actor->GetID())->GetCP();CArray<CUnit*,CUnit*> one;one.Add(actor);client->UnitCommandMoveTo(one,where,FALSE);}
        else if(packet==PID_CMD_POSE)client->UnitCommandPose(actor,order=="run"?UP_RUN:order=="walk"?UP_STAND:order=="lay"?UP_LAY:UP_CRAWL);
        else if(packet==PID_CMD_AGGRESSION)client->UnitCommandAggression(actor,order=="aggressive"?PAS_AGGRESSIVE:PAS_STEALTH);
        else if(packet==PID_CMD_SELWEAPON)client->UnitCommandSelWeapon(actor,slot);
        else if(packet==PID_CMD_SKILL) {CArray<CUnit*,CUnit*> one;one.Add(actor);client->UnitCommandLoot(one,theWorld.GetUnit(target->GetID()),run);}
        else client->UnitFollow(actor,target?theWorld.GetUnit(target->GetID()):nullptr);
    }
    return false;
}
