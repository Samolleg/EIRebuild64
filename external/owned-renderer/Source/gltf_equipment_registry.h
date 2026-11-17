#pragma once
#include "gltf_equipment.h"
#include "../../../SDKs/JSON/json.hpp"
#include <stdexcept>

namespace owned::gltf {
struct ItemBinding {
    std::string resource, socket;
    bool weapon = false, arrow = false, skinned = false;
};
// Pure cold selection, also used by tests. A missing shape is not registered;
// a registered shape must describe every selected material and inventory slot.
inline std::vector<ItemBinding> item_bindings(const nlohmann::json& registry,
                                             const VisualEquipmentItem& item)
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
        require(entry.at("profile")=="human.v1","profile");
        ItemBinding binding;
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
} // namespace owned::gltf
