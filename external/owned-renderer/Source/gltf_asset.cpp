#include "gltf_asset.h"
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
std::shared_ptr<const Asset> select(const std::string &visual, bool hero, bool &present)
{
    static std::mutex mutex;
    static ResourceStore store;
    static std::uint64_t generation = 0;
    static std::map<std::string, std::weak_ptr<const Asset>> cache;
    static Json entries;
    static bool registryLoaded = false;
    static std::string registryIdentity;
    static std::map<std::string, std::string> failures;
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
    if (it == characters.end())
        return {};
    present = true;
    const auto &binding = *it;
    const auto validId = [](const std::string &id) {
        if (id.empty() || !((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= '0' && id[0] <= '9')))
            return false;
        return std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
    };
    require(validId(key), "character id " + key);
    require(binding.at("profile") == "human.v1", "registry profile");
    const auto resource = binding.at("resource").get<std::string>();
    require(resource.rfind("models/characters/", 0) == 0 && resource.size() > 4 &&
                resource.substr(resource.size() - 4) == ".glb" &&
                resource.find("..") == std::string::npos &&
                resource.find(':') == std::string::npos && resource.find('\\') == std::string::npos,
            "resource key");
    const auto cacheKey = std::to_string(generation) + "|" + registryIdentity + "|" + key;
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
        asset->cache_key = blob.identity.key() + "|" + profile.identity.key();
        asset->cache_key += "|" + cacheKey;
        const std::string detail = "model=\"" + resource +
            "\" glTF animation not wired: static idle frame";
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
} // namespace owned::gltf

namespace owned::skn {
std::shared_ptr<const RuntimeAsset> select_gltf(const std::string &name, bool hero, bool &present)
{
    const auto loaded = gltf::select(name, hero, present);
    if (!present)
        return {};
    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<const RuntimeAsset>> cache;
    std::lock_guard<std::mutex> guard(mutex);
    if (auto found = cache[loaded->cache_key].lock())
        return found;
    auto asset = std::make_shared<RuntimeAsset>();
    asset->gltf_asset = loaded;
    asset->asset = loaded->geometry;
    asset->textures = loaded->textures;
    asset->identity.canonical_name = loaded->cache_key;
    cache[loaded->cache_key] = asset;
    return asset;
}
} // namespace owned::skn
