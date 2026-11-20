#include "gltf_asset.h"
#include "gltf_equipment_registry.h"
#include "../../../SDKs/JSON/json.hpp"
namespace owned::gltf {
using Json = nlohmann::json;
namespace {
void require(bool valid, const std::string &reason) {
    if (!valid) throw std::runtime_error("glTF " + reason);
}
ResourceBlob read(ResourceStore &store, const std::string &name)
{
    ResourceBlob blob;
    ResourceFailure failure;
    require(store.resolve(name, blob, failure) && blob.bytes, name + ": " + failure.message);
    return blob;
}
Json json(const ResourceBlob &blob) {
    return Json::parse(blob.bytes->begin(), blob.bytes->end());
}
} // namespace
// Inventory lanes have independent resource stores and import tables. World
// previews retain their process cache and its original lock.
template<bool Inventory,class State>
static State& lookupState() {
    if constexpr(Inventory) {static thread_local State local;return local;}
    else {static State shared;return shared;}
}
// Cold inventory decoding must never take the mutex of a live character preview.
template<bool Inventory=false>
static std::shared_ptr<const Asset> select_resource(const std::string &visual, bool hero, bool &present,
    const ItemBinding* itemBinding = nullptr, std::vector<ItemBinding>* selected = nullptr,
    const std::vector<VisualEquipmentItem>* equipment = nullptr, const VisualEquipmentItem* preview = nullptr)
{
    struct State {
        std::mutex mutex;
        ResourceStore store;
        std::uint64_t generation=0;
        std::map<std::string,std::weak_ptr<const Asset>> cache;
        Json entries;
        bool registryLoaded=false;
        std::string registryIdentity;
        std::map<std::string,std::string> failures;
    };
    auto& state=lookupState<Inventory,State>();
    auto& mutex=state.mutex;auto& store=state.store;auto& generation=state.generation;
    auto& cache=state.cache;auto& entries=state.entries;auto& registryLoaded=state.registryLoaded;
    auto& registryIdentity=state.registryIdentity;auto& failures=state.failures;
    std::lock_guard<std::mutex> lock(mutex);
    present = false;
    ResourceStoreConfig mounts;
    std::uint64_t current = 0;
    ResourceFailure failure;
    if (!owned_resource_runtime_mounts(mounts, &current))
        return {};
    if (current != generation)
    {
        store.close();
        cache.clear();
        failures.clear();
        registryIdentity.clear();
        entries = Json();
        registryLoaded = false;
        require(store.open(mounts, failure), failure.message);
        generation = current;
    }
    if (!registryLoaded)
    {
        ResourceBlob registry;
        if (!store.resolve("models/registry.v1.json", registry, failure))
        {
            if (failure.code == ResourceFailureCode::not_found)
            {
                registryLoaded = true;
                return {};
            }
            throw std::runtime_error("glTF registry: " + failure.message);
        }
        entries = json(registry);
        require(entries.at("version") == 1, "registry version");
        registryIdentity = registry.identity.key();
        registryLoaded = true;
    }
    if (entries.is_null())
        return {};
    if (selected) {
        *selected = preview ? inventory_item_bindings(entries,*preview) : outfit_bindings(entries,*equipment,visual);
        return {};
    }
    const auto &characters = entries.at("characters");
    require(characters.is_object(), "registry characters");
    std::string key = visual;
    for (auto &c : key)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    // The hero alias is explicit data, following the existing host hero selector.
    if (hero && entries.contains("heroCharacter"))
        key = entries.at("heroCharacter").get<std::string>();
    const bool heroAlias = hero && entries.contains("heroCharacter");
    if (heroAlias)
        present = true;
    const auto it = characters.find(key);
    require(!heroAlias || it != characters.end(), "heroCharacter entry missing: " + key);
    if (!itemBinding && it == characters.end())
        return {};
    present = true;
    const Json itemEntry=itemBinding ? Json{{"profile","human.v1"},{"resource",itemBinding->resource}} : Json{};
    const auto &binding = itemBinding ? itemEntry : *it;
    const auto validId = [](const std::string &id) {
        if (id.empty() || !((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= '0' && id[0] <= '9')))
            return false;
        return std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
    };
    require(itemBinding || validId(key), "character id " + key);
    require(binding.at("profile") == "human.v1", "registry profile");
    const auto resource = binding.at("resource").get<std::string>();
    require(resource.rfind(itemBinding ? "models/items/" : "models/characters/", 0) == 0 && resource.size() > 4 &&
                resource.substr(resource.size() - 4) == ".glb" &&
                resource.find("..") == std::string::npos &&
                resource.find(':') == std::string::npos && resource.find('\\') == std::string::npos,
            "resource key");
    const auto cacheKey = std::to_string(generation) + "|" + registryIdentity + (itemBinding ? "|item|" : "|body|") + key;
    const auto rejected = failures.find(cacheKey);
    if (rejected != failures.end())
        throw std::runtime_error(rejected->second);
    auto found = cache.find(cacheKey);
    if (found != cache.end())
        if (auto asset = found->second.lock())
            return asset;
    try
    {
        const auto blob = read(store, resource), profile = read(store, "profiles/human.v1.json");
        auto asset = decode(blob, profile, [&](const std::string &name, const void *data, std::uint32_t size) {
            ResourceBlob pixels;
            ResourceFailure failure;
            require(store.copy_selected_memory(name, data, size, pixels, failure), failure.message);
            return pixels;
        });
        asset->resource_key = resource;
        asset->cache_key = blob.identity.key() + "|" + profile.identity.key();
        asset->cache_key += "|" + cacheKey;
        require(itemBinding ? (itemBinding->skinned ? asset->wearable : asset->rigid) :
            (!asset->rigid && !asset->wearable), "selected asset kind");
        if (asset->gpu_animation) asset->gpu_animation->key = asset->cache_key;
        const std::string detail = "model=\"" + resource +
            "\" glTF GPU animation asset loaded";
        bridge_append_probe_event("gltf_asset_loaded", detail.c_str());
        cache[cacheKey] = asset;
        return asset;
    }
    catch (const std::exception &error)
    {
        const std::string detail = "model=\"" + resource + "\" reason=\"" + error.what() + "\"";
        failures.emplace(cacheKey, error.what());
        bridge_append_probe_event("gltf_asset_rejected", detail.c_str());
        throw;
    }
}
std::shared_ptr<const Asset> select(const std::string& visual, bool hero, bool& present) {
    return select_resource(visual,hero,present);
}
struct SelectedItemTextures {
    std::map<unsigned,std::shared_ptr<const skn::Dds>> color, orm;
    std::map<unsigned,std::shared_ptr<const std::uint8_t>> material_ids;
};
template<bool Inventory>
static SelectedItemTextures select_textures(
    const ItemBinding& binding, const Asset& asset)
{
    SelectedItemTextures result;
    if(binding.textures.empty())return result;
    // Decode only selected materials, on appearance changes. Geometry remains
    // in the normal GLB cache and is never copied for a texture variant.
    using MaterialKey=std::pair<std::uintptr_t,std::uintptr_t>;
    struct State {
        std::mutex mutex;
        ResourceStore store;
        std::uint64_t generation=0;
        std::map<std::string,std::weak_ptr<const skn::Dds>> cache;
        std::map<MaterialKey,std::weak_ptr<const std::uint8_t>> material_cache;
        unsigned insertions=0;
    };
    auto& state=lookupState<Inventory,State>();
    auto& mutex=state.mutex;auto& store=state.store;auto& generation=state.generation;
    auto& cache=state.cache;auto& material_cache=state.material_cache;auto& insertions=state.insertions;
    std::lock_guard<std::mutex> lock(mutex);
    ResourceStoreConfig mounts;std::uint64_t current=0;ResourceFailure failure;
    require(owned_resource_runtime_mounts(mounts,&current),"texture mounts unavailable");
    if(current!=generation) {
        store.close();cache.clear();material_cache.clear();
        require(store.open(mounts,failure),failure.message);generation=current;
    }
    for(const auto& requested:binding.textures) {
        const unsigned index=requested.material;
        require(index<asset.geometry.materials.size() && index<asset.base_color_factors.size() &&
                !asset.geometry.materials[index].maps[0].empty(),
                "texture override requires an existing textured material");
        require(std::any_of(asset.geometry.submeshes.begin(),asset.geometry.submeshes.end(),
            [&](const skn::Submesh& mesh){return mesh.material==index;}),
            "texture override material is unused");
        const auto source=read(store,requested.resource);
        const auto& factor=asset.base_color_factors[index];
        std::string key="C"+source.identity.key();
        key.append(reinterpret_cast<const char*>(factor.data()),sizeof(float)*factor.size());
        auto selected=cache[key].lock();
        if(!selected) {
            auto decoded=decode_base_color(source,[&](const std::string& name,const void* data,std::uint32_t size) {
                ResourceBlob pixels;ResourceFailure error;
                require(store.copy_selected_memory(name,data,size,pixels,error),error.message);
                return pixels;
            },factor);
            selected=std::make_shared<const skn::Dds>(std::move(decoded));cache[key]=selected;
        }
        result.color.emplace(index,std::move(selected));
        if(!requested.metallic_roughness.empty()) {
            require(!asset.geometry.materials[index].maps[2].empty(),
                    "metallic/roughness override requires an existing map");
            const auto orm_source=read(store,requested.metallic_roughness);
            const auto orm_key="M"+orm_source.identity.key();
            auto orm=cache[orm_key].lock();
            if(!orm) {
                auto decoded=decode_metallic_roughness(orm_source,[&](const std::string& name,const void* data,std::uint32_t size) {
                    ResourceBlob pixels;ResourceFailure error;
                    require(store.copy_selected_memory(name,data,size,pixels,error),error.message);
                    return pixels;
                });
                orm=std::make_shared<const skn::Dds>(std::move(decoded));cache[orm_key]=orm;
            }
            const MaterialKey material_key={reinterpret_cast<std::uintptr_t>(&asset.geometry.materials[index]),
                                             reinterpret_cast<std::uintptr_t>(orm.get())};
            auto identity=material_cache[material_key].lock();
            if(!identity) {
                identity=std::make_shared<const std::uint8_t>(0);
                material_cache[material_key]=identity;
            }
            result.orm.emplace(index,std::move(orm));
            result.material_ids.emplace(index,std::move(identity));
        }
        if(++insertions%64==0) {
            for(auto it=cache.begin();it!=cache.end();)
                if(it->second.expired())it=cache.erase(it);else ++it;
            for(auto it=material_cache.begin();it!=material_cache.end();)
                if(it->second.expired())it=material_cache.erase(it);else ++it;
        }
    }
    return result;
}
template<bool Inventory>
static std::vector<EquipmentAsset> load_equipment(const std::vector<ItemBinding>& bindings) {
    std::vector<EquipmentAsset> result;
    bool present=false;
    for(const auto& binding:bindings) {
        auto asset=select_resource<Inventory>(binding.resource,false,present,&binding);
        require(present && asset && (binding.skinned ? asset->wearable : asset->rigid),"item not available");
        result.push_back({asset,binding.socket,binding.slot,binding.weapon,binding.arrow});
        result.back().offset=binding.offset;
        auto textures=select_textures<Inventory>(binding,*asset);
        result.back().base_color_overrides=std::move(textures.color);
        result.back().metallic_roughness_overrides=std::move(textures.orm);
        result.back().material_identities=std::move(textures.material_ids);
    }
    return result;
}
std::vector<EquipmentAsset> select_equipment(const std::vector<VisualEquipmentItem>& equipment,const std::string& body_resource) {
    std::vector<ItemBinding> bindings;
    bool present=false;
    select_resource(body_resource,false,present,nullptr,&bindings,&equipment);
    return load_equipment<false>(bindings);
}

std::vector<EquipmentAsset> select_inventory_item(const VisualEquipmentItem& item) {
    std::vector<ItemBinding> bindings;
    bool present=false;
    select_resource<true>({},false,present,nullptr,&bindings,nullptr,&item);
    return load_equipment<true>(bindings);
}
} // namespace owned::gltf

namespace owned::skn {
std::shared_ptr<const RuntimeAsset> select_gltf(const std::string &name, bool hero, bool &present)
{
    const auto loaded = gltf::select(name, hero, present);
    if (!present) return {};
    return from_gltf(loaded);
}
std::shared_ptr<const RuntimeAsset> from_gltf(const std::shared_ptr<const gltf::Asset>& loaded)
{
    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<const RuntimeAsset>> cache;
    std::lock_guard<std::mutex> guard(mutex);
    if (auto found = cache[loaded->cache_key].lock())
        return found;
    auto asset = std::make_shared<RuntimeAsset>();
    asset->gltf_asset = loaded;
    asset->asset = loaded->geometry;
    asset->asset.vertices = loaded->rest_vertices;
    asset->textures = loaded->textures;
    asset->identity.canonical_name = loaded->cache_key;
    cache[loaded->cache_key] = asset;
    return asset;
}
} // namespace owned::skn
