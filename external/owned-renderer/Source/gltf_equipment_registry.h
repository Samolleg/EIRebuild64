#pragma once
#include "gltf_equipment.h"
#include "../../../SDKs/JSON/json.hpp"
#include <stdexcept>
#include <algorithm>
#include <set>

namespace owned::gltf {
struct ItemBinding {
    std::string resource, socket, slot;
    bool weapon = false, arrow = false, skinned = false;
};
// Pure cold selection, also used by tests. A missing shape is not registered;
// a registered shape must describe every selected material and inventory slot.
inline std::vector<ItemBinding> item_bindings(const nlohmann::json& registry,
                                             const VisualEquipmentItem& item,
                                             const std::vector<VisualEquipmentItem>* outfit=nullptr)
{
    const auto require=[](bool v,const char* why) {
        if (!v) throw std::runtime_error(std::string("glTF equipment: ")+why);
    };
    const auto& items=registry.at("items");
    require(items.is_object(),"items object");
    auto found=items.find(item.shape);
    if(found==items.end()) return {};
    const auto& list=found->at("materials").at(item.material).at(item.slot);
    require(list.is_array() && list.size()<=16,"attachment array limit");
    std::vector<ItemBinding> result;
    for(const auto& entry:list) {
        const auto matches=[&](const char* key) {
            const auto& condition=entry.at(key);
            require(condition.is_object() && condition.at("slot").is_string() && condition.at("shape").is_string(),"equipment condition");
            const auto slot=condition.at("slot").get<std::string>();
            const auto shape=condition.at("shape").get<std::string>();
            require(slot.rfind("armor_",0)==0 && !shape.empty() && shape.size()<=128,"equipment condition target");
            const auto match=[&](const VisualEquipmentItem& other) {
                return other.kind==VisualEquipmentItem::Kind::armor && other.slot==slot && other.shape==shape &&
                    (!condition.contains("material") || condition.at("material")==other.material);
            };
            // Conditions read the immutable inventory, never other conditions.
            return outfit ? std::any_of(outfit->begin(),outfit->end(),match) : match(item);
        };
        if(entry.contains("requiresEquipment") && !matches("requiresEquipment"))continue;
        if(entry.contains("unlessEquipment") && matches("unlessEquipment"))continue;
        require(entry.at("profile")=="human.v1","profile");
        ItemBinding binding;
        binding.slot=item.slot;
        binding.resource=entry.at("resource").get<std::string>();
        const auto& path=binding.resource;
        require(path.rfind("models/items/",0)==0 && path.size()>17 &&
            path.substr(path.size()-4)==".glb" && path.find("..") == std::string::npos &&
            path.find(':')==std::string::npos && path.find('\\')==std::string::npos,
            "item resource key");
        const auto mode=entry.value("binding", "socket");
        require(mode=="socket" || mode=="skin","binding mode");
        binding.skinned=mode=="skin";
        require(!binding.skinned || (item.kind==VisualEquipmentItem::Kind::armor &&
            !entry.contains("socket")),"skin binding requires armor without socket");
        binding.socket=binding.skinned ? "" : entry.at("socket").get<std::string>();
        const auto& socket=binding.socket;
        // The decoder checks the profile, and the body bind checks presence.
        // Internal helper nodes (#...) and paths are never attachment targets.
        require(binding.skinned || (!socket.empty() && socket.size()<=128 && std::all_of(socket.begin(),socket.end(),[](char c) {
            return (c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='.' || c=='-';
        })),"attachment node");
        const auto visibility=entry.value("visibility",item.kind==VisualEquipmentItem::Kind::weapon ? "weapon" : "always");
        require(visibility=="weapon" || visibility=="arrow" || visibility=="always","visibility");
        binding.weapon=visibility=="weapon" || visibility=="arrow";
        binding.arrow=visibility=="arrow";
        require(!binding.skinned || visibility=="always", "skin visibility");
        for(const auto& prior:result)
            require(prior.resource!=binding.resource || prior.socket!=binding.socket,"duplicate attachment");
        result.push_back(std::move(binding));
    }
    return result;
}

inline std::vector<ItemBinding> outfit_bindings(const nlohmann::json& registry,
    const std::vector<VisualEquipmentItem>& equipment,const std::string& body_resource)
{
    std::vector<ItemBinding> result;
    std::set<std::string> covered;
    for(const auto& item:equipment) {
        const auto bindings=item_bindings(registry,item,&equipment);
        for(const auto& binding:bindings)if(binding.skinned)covered.insert(item.slot);
        result.insert(result.end(),bindings.begin(),bindings.end());
    }
    if(!registry.contains("baseLayers"))return result;
    const auto& layers=registry.at("baseLayers");
    if(!layers.is_object())throw std::runtime_error("glTF equipment: base layers object");
    auto body=layers.find(body_resource);if(body==layers.end())return result;
    if(!body->is_array() || body->size()>8)throw std::runtime_error("glTF equipment: base layer limit");
    std::set<std::string> slots;
    for(const auto& layer:*body) {
        const auto slot=layer.at("slot").get<std::string>();
        if(slot.rfind("base_",0)!=0 || !slots.insert(slot).second)throw std::runtime_error("glTF equipment: base layer slot");
        const auto& hidden=layer.at("hiddenBySlots");
        if(!hidden.is_array() || hidden.size()>8)throw std::runtime_error("glTF equipment: base layer coverage");
        bool hide=false;
        for(const auto& value:hidden) {
            const auto key=value.get<std::string>();
            if(key.rfind("armor_",0)!=0)throw std::runtime_error("glTF equipment: coverage slot");
            hide|=covered.count(key)!=0;
        }
        if(hide)continue;
        // Reuse strict resource/skin validation; base layers are ordinary skins.
        VisualEquipmentItem item;item.kind=VisualEquipmentItem::Kind::armor;
        item.shape="base";item.material="base";item.slot=slot;
        nlohmann::json minimal;minimal["items"]["base"]["materials"]["base"][slot]=nlohmann::json::array({layer});
        const auto parsed=item_bindings(minimal,item);
        if(parsed.size()!=1 || !parsed[0].skinned)throw std::runtime_error("glTF equipment: base layer skin required");
        result.push_back(parsed[0]);
    }
    return result;
}
} // namespace owned::gltf
