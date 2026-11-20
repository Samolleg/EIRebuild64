#pragma once
#include "gltf_equipment.h"
#include "../../../SDKs/JSON/json.hpp"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace owned::gltf {
struct ItemTexture {
    unsigned material = 0;
    std::string resource;
    std::string metallic_roughness;
};
struct ItemBinding {
    std::string resource, socket, slot;
    bool weapon = false, arrow = false, skinned = false;
    std::vector<ItemTexture> textures;
    std::array<float,3> offset{};
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
    const auto& selection=found->at("materials").at(item.material).at(item.slot);
    // An explicit array is the complete material-specific model selection and
    // takes priority over the shape's common model, including an empty array.
    // An object changes textures on the explicitly declared common selection.
    require(selection.is_array() || selection.is_object(),"slot selection");
    const auto& list=selection.is_array() ? selection : found->at("defaults").at(item.slot);
    require(list.is_array() && list.size()<=16,"attachment array limit");
    std::vector<ItemBinding> result;
    std::vector<std::size_t> selected(list.size(), std::size_t(-1));
    for(std::size_t sourceIndex=0;sourceIndex<list.size();++sourceIndex) {
        const auto& entry=list[sourceIndex];
        const auto matches=[&](const char* key) {
            const auto& condition=entry.at(key);
            require(condition.is_object() && condition.at("slot").is_string() && condition.at("shape").is_string(),"equipment condition");
            const auto slot=condition.at("slot").get<std::string>();
            const auto shape=condition.at("shape").get<std::string>();
            require(slot.rfind("armor_",0)==0 && !shape.empty() && shape.size()<=128,"equipment condition target");
            const bool hasSelection=condition.contains("hasSelection");
            require(!hasSelection || (shape=="*" && condition.at("hasSelection").is_boolean() &&
                condition.at("hasSelection").get<bool>()),"equipment selection condition");
            const auto match=[&](const VisualEquipmentItem& other) {
                if(other.kind!=VisualEquipmentItem::Kind::armor || other.slot!=slot ||
                   (shape!="*" && other.shape!=shape) ||
                   (condition.contains("material") && condition.at("material")!=other.material))return false;
                if(!hasSelection)return true;
                // Check the material's complete explicit selection, or the
                // common selection when it supplies texture overrides. This
                // deliberately does not recursively evaluate other conditions.
                const auto candidate=items.find(other.shape);
                if(candidate==items.end() || !candidate->is_object())return false;
                const auto materials=candidate->find("materials");
                if(materials==candidate->end() || !materials->is_object())return false;
                const auto material=materials->find(other.material);
                if(material==materials->end() || !material->is_object())return false;
                const auto selectedSlot=material->find(other.slot);
                if(selectedSlot==material->end())return false;
                if(selectedSlot->is_array())return !selectedSlot->empty();
                if(!selectedSlot->is_object())return false;
                const auto defaults=candidate->find("defaults");
                if(defaults==candidate->end() || !defaults->is_object())return false;
                const auto common=defaults->find(other.slot);
                return common!=defaults->end() && common->is_array() && !common->empty();
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
        if(entry.contains("offset")) {
            const auto& value=entry.at("offset");
            require(!binding.skinned && value.is_array() && value.size()==3,"rigid socket offset");
            binding.offset=value.get<std::array<float,3>>();
            for(float v:binding.offset)require(std::isfinite(v),"finite socket offset");
        }
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
        selected[sourceIndex]=result.size();
        result.push_back(std::move(binding));
    }
    if(selection.is_object()) {
        require(selection.size()==1 && selection.contains("textures"),"unknown material selection fields");
        const auto& textures=selection.at("textures");
        require(textures.is_array() && textures.size()<=64,"texture override array limit");
        std::set<std::pair<unsigned,unsigned>> textureTargets;
        for(const auto& texture:textures) {
            require(texture.is_object() && texture.contains("binding") && texture.contains("material") &&
                texture.contains("baseColor") && texture.size()==(texture.contains("metallicRoughness") ? 4u : 3u),
                "texture override fields");
            require(texture.at("binding").is_number_unsigned() && texture.at("material").is_number_unsigned(),"texture indices");
            require(texture.at("binding").get<std::uint64_t>()<=UINT32_MAX &&
                texture.at("material").get<std::uint64_t>()<=UINT32_MAX,"texture index overflow");
            const auto index=texture.at("binding").get<unsigned>();
            const auto material=texture.at("material").get<unsigned>();
            const auto path=texture.at("baseColor").get<std::string>();
            const auto orm=texture.value("metallicRoughness",std::string());
            require(index<list.size(),"texture binding index");
            require(textureTargets.emplace(index,material).second,"duplicate material texture");
            const auto valid_path=[](const std::string& value) {
                return value.rfind("models/items/textures/",0)==0 && value.size()>27 &&
                    value.substr(value.size()-4)==".png" && value.find("..") == std::string::npos &&
                    value.find(':')==std::string::npos && value.find('\\')==std::string::npos;
            };
            require(valid_path(path) && (!texture.contains("metallicRoughness") || valid_path(orm)),
                "texture resource key");
            // Texture binding indices address the original list, even when an
            // outfit condition omits an earlier attachment from this selection.
            if(selected[index]!=std::size_t(-1))
                result[selected[index]].textures.push_back({material,path,orm});
        }
    }
    return result;
}

// The inventory has no character, alternate hand, projectile or outfit layers.
inline std::vector<ItemBinding> inventory_item_bindings(const nlohmann::json& registry,
                                                        VisualEquipmentItem item) {
    if (!registry.contains("items")) return {};
    const auto& items=registry.at("items");
    const auto shape=items.find(item.shape);
    if (shape==items.end()) return {};
    const auto& materials=shape->at("materials");
    const auto variant=materials.find(item.material);
    if (variant==materials.end()) return {};
    if (item.kind==VisualEquipmentItem::Kind::weapon && !variant->contains(item.slot))
        item.slot="hand_l";
    if (!variant->contains(item.slot)) return {};
    auto parts=item_bindings(registry,item);
    // A socket-mounted helmet may additionally replace the character's skinned hair.
    const bool mountedHelmet=item.slot=="armor_0" && std::any_of(parts.begin(),parts.end(),
        [](const ItemBinding& part){return part.socket=="socket_head";});
    parts.erase(std::remove_if(parts.begin(),parts.end(),[&](const ItemBinding& part){
        return part.arrow || (mountedHelmet && part.skinned) ||
               (item.kind==VisualEquipmentItem::Kind::weapon && part.socket=="socket_back" && parts.size()>1);
    }),parts.end());
    return parts;
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
