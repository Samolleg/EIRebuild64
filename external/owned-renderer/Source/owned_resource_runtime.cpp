#include "owned_resource_runtime.h"
#ifdef EID3D11_EMBEDDED
#include "embedded_host.h"
#endif
#include "camp_inventory.h"

#include "owned_asset_loader.h"
#include "retained_static_scene.h"
#ifndef EID3D11_EMBEDDED
#include "retained_publication.h"
#endif
#include "neighbour_maps.h"
#include "neighbour_client.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <map>
#include <memory>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace owned {

struct OwnedAnimationLibraryOwner {
    ResourceIdentity outer_identity;
};

namespace {

struct VisualAnimationLibrary final : OwnedAnimationLibraryOwner {
    struct Clip {
        AnimationClipAsset asset;
        bool ready = false;
    };
    MemoryResourceArchive archive;
    std::shared_ptr<const std::vector<std::uint8_t>> slot_reservation;
    // The fixed archive index gives stable addresses for aliasing consumers.
    // A clip never owns the library, so no shared-ownership cycle is possible.
    std::vector<Clip> clips;
};

// Weak dictionaries are identity indexes, not admission budgets. Reclaim a
// small fixed amount of expired bookkeeping on each miss so cleanup remains
// amortized even when a load publishes thousands of live values at once.
template<class Key, class Value>
struct WeakPool {
    using Map = std::map<Key, Value>;
    Map values;
    typename Map::iterator cleanup = values.end();

    template<class Expired>
    void reclaim(Expired expired) {
        constexpr std::size_t work = 8;
        if (values.empty()) {
            cleanup = values.end();
            return;
        }
        for (std::size_t scanned = 0; scanned < work && !values.empty(); ++scanned) {
            // Erasing the previous item, or scanning a live singleton, may
            // leave the cursor at end(). Normalize it before every access.
            if (cleanup == values.end()) cleanup = values.begin();
            const auto current = cleanup++;
            if (expired(current->second)) {
                values.erase(current);
                if (values.empty()) cleanup = values.end();
            }
        }
    }

    void erase(const Key& key) {
        const auto found = values.find(key);
        if (found == values.end()) return;
        if (cleanup == found) ++cleanup;
        values.erase(found);
        if (values.empty()) cleanup = values.end();
    }

    void clear() {
        values.clear();
        cleanup = values.end();
    }
};

struct RuntimeState {
    struct TextureValue {
        std::weak_ptr<const TextureCpuAsset> asset;
        std::uint64_t serial = 0;
        bool source_available = true;
    };
    struct PendingLoad {
        std::uint64_t transaction = 0;
        std::uint64_t generation = 0;
        std::string resource;
        std::string terrain_resource;
        MobSourceRole role = MobSourceRole::secondary;
        OwnedResourceRequestToken token{};
        bool owner_finished = false;
    };

    std::mutex lock;
    std::uint32_t users = 0;
    std::uint64_t generation = 0;
    std::atomic<bool> polling{false};
    std::shared_ptr<OwnedResourceLoader> loader;
    ResourceStoreConfig mounts;
    ResourceStoreConfig text_mounts;
    bool mounts_adopted = false; // x64 host: mounts from the game process, no loader
    std::vector<PendingLoad> pending;
    PendingLoad active;
    RetainedStaticScene& scene = retained_visual_scene();
    ResourceStore selected_resources;
    std::uint64_t resolver_generation = 1;
    WeakPool<std::string, std::weak_ptr<const ModelCpuAsset>> visual_models;
    WeakPool<std::string, std::weak_ptr<const FigureCpuAsset>> visual_figures;
    WeakPool<std::string, TextureValue> visual_textures;
    std::uint64_t visual_texture_serial = 0;
    // If a failed generated publication cannot reserve its negative identity,
    // the scope can no longer prove that disk bytes are current. Keep it
    // fail-closed until the normal runtime lifetime reset.
    bool visual_texture_sources_available = true;
    WeakPool<std::string, std::weak_ptr<const AnimationDatabaseAsset>> visual_databases;
    WeakPool<std::string, std::weak_ptr<VisualAnimationLibrary>> visual_clips;
    WeakPool<std::uint64_t, std::weak_ptr<const OwnedVisualSurface>> visual_surfaces;
#ifdef EID3D11_TESTING
    OwnedVisualClipReuseTestStats clip_reuse;
    std::shared_ptr<OwnedResourceLoader> test_loader_pin;
#endif
};

RuntimeState& runtime() {
    // Process teardown can enter DllMain with leaked DirectDraw interfaces.
    // Keep this trivial holder alive and stop/join only through normal Release.
    static RuntimeState* state = new RuntimeState();
    return *state;
}

bool same_resource_identity(const ResourceIdentity& left,
                            const ResourceIdentity& right) noexcept {
    return left.container_incarnation == right.container_incarnation &&
        left.absolute_offset == right.absolute_offset &&
        left.bytes == right.bytes &&
        left.canonical_name == right.canonical_name;
}

bool canonical_name_matches(const std::string& canonical,
                            const std::string& selected) noexcept {
    if (canonical.size() != selected.size()) return false;
    for (std::size_t index = 0; index < canonical.size(); ++index) {
        const auto input = static_cast<unsigned char>(selected[index]);
        if (input >= 0x80u || input == 0 || input == ':') return false;
        char folded = static_cast<char>(input == '/' ? '\\' : input);
        if (folded >= 'A' && folded <= 'Z')
            folded = static_cast<char>(folded + ('a' - 'A'));
        if (canonical[index] != folded) return false;
    }
    return true;
}

std::shared_ptr<const AnimationClipAsset> visual_clip_from_library(
    RuntimeState& state, std::shared_ptr<VisualAnimationLibrary> library,
    const std::string& name, ResourceFailure& failure) {
    ResourceIdentity identity;
    if (!library->archive.locate(name,identity,failure)) return nullptr;
    auto& slot=library->clips[identity.entry_index];
    if (!slot.ready) {
        ResourceBlob member;
        if (!library->archive.resolve(name,member,failure,&state.selected_resources)) return nullptr;
#ifdef EID3D11_TESTING
        ++state.clip_reuse.member_copies;
        ++state.clip_reuse.clip_decodes;
#endif
        if (!decode_animation_clip(member,slot.asset,failure,&state.selected_resources)) return nullptr;
        slot.ready = true;
    }
    failure = {};
    return std::shared_ptr<const AnimationClipAsset>(std::move(library),&slot.asset);
}

void bind_visual_library(const std::shared_ptr<const AnimationDatabaseAsset>& database,
    std::uint64_t source_generation, const std::shared_ptr<VisualAnimationLibrary>& library) {
    if (!database || !source_generation || !library) return;
    AnimationLibraryBinding binding;
    binding.database_identity = database->identity;
    binding.owner = std::static_pointer_cast<OwnedAnimationLibraryOwner>(library);
    binding.source_generation = source_generation;
    database->library_binding = std::move(binding);
}

bool copy_visual_bytes(void* owned, const void* borrowed, std::uint32_t size) noexcept {
    __try { std::memcpy(owned, borrowed, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool reserve_texture_metadata(RuntimeState& state, TextureCpuAsset& texture,
                              ResourceFailure& failure) {
    if (texture.mips.capacity() >
        ((std::numeric_limits<std::size_t>::max)() - texture.user_data.capacity()) /
            sizeof(MmpMipLevel)) {
        failure={ResourceFailureCode::archive_bounds,
            "texture retained metadata reservation overflow"};
        return false;
    }
    const auto bytes=texture.user_data.capacity()+texture.mips.capacity()*sizeof(MmpMipLevel);
    texture.byte_reservation=state.selected_resources.allocate_immutable_bytes(0,failure,bytes);
    return static_cast<bool>(texture.byte_reservation);
}

template<class Asset, class Decode>
std::shared_ptr<const Asset> visual_asset(
    WeakPool<std::string, std::weak_ptr<const Asset>>& pool,
    const std::string& key, ResourceFailure& failure, Decode decode,
    VisualAssetLookupDiagnostic* diagnostic = nullptr) {
    if (diagnostic) *diagnostic = {};
    const auto found = pool.values.find(key);
    if (found != pool.values.end()) if (auto pinned = found->second.lock()) {
        if (diagnostic) diagnostic->cache_hit = true;
        return pinned;
    }
    if (diagnostic) diagnostic->cache_miss = true;
    pool.reclaim([](const auto& value) { return value.expired(); });
    try {
        auto result = std::make_shared<Asset>();
        struct DecodeClock {
            VisualAssetLookupDiagnostic* diagnostic;
            LARGE_INTEGER begin{};
            explicit DecodeClock(VisualAssetLookupDiagnostic* value) noexcept : diagnostic(value) {
                if (diagnostic) {
                    diagnostic->decode_attempted = true;
                    QueryPerformanceCounter(&begin);
                }
            }
            ~DecodeClock() noexcept {
                if (!diagnostic || begin.QuadPart <= 0) return;
                LARGE_INTEGER end{};
                QueryPerformanceCounter(&end);
                if (end.QuadPart >= begin.QuadPart)
                    diagnostic->decode_ticks += end.QuadPart - begin.QuadPart;
            }
        };
        bool decoded = false;
        {
            DecodeClock decode_clock(diagnostic);
            decoded = decode(*result);
        }
        if (!decoded) {
            pool.erase(key);
            return nullptr;
        }
        pool.values.insert_or_assign(key, result);
        return result;
    } catch (const std::bad_alloc&) {
        pool.erase(key);
        failure = {ResourceFailureCode::archive_bounds, "visual asset allocation failed"};
    } catch (const std::length_error&) {
        pool.erase(key);
        failure = {ResourceFailureCode::archive_bounds, "visual asset allocation failed"};
    }
    return nullptr;
}

#ifndef EID3D11_EMBEDDED
// Core mode, game process (retained_visual_textures_to_host): the recorder
// that defines archive textures and map sources on the x64 host while this
// process keeps slim stand-ins. Null otherwise.
std::atomic<RetainedPublicationRecorder*> g_core_host_recorder{nullptr};

// Core mode: `full` is defined on the host now (its source bytes go with it)
// and this process keeps a stand-in without source bytes, which the stream
// names as that definition (RetainedPublicationRecorder::asset_alias). Mip 0's
// cut bits stay for the game-process alpha readers (sway classification).
// Null: keep `full` (no recorder, or the stand-in could not be made).
std::shared_ptr<const TextureCpuAsset> texture_host_stand_in(
    const std::shared_ptr<const TextureCpuAsset>& full) noexcept {
    auto* recorder = g_core_host_recorder.load(std::memory_order_acquire);
    if (!recorder || !full->source || full->mips.empty()) return nullptr;
    std::shared_ptr<const TextureCpuAsset> result;
    try {
        auto stand_in = std::make_shared<TextureCpuAsset>(*full);
        stand_in->source.reset();
        if (!(full->opaque_mips & 1u)) {
            TextureAlphaPixels alpha;
            if (!texture_alpha_pixels(*full, 0, alpha)) return nullptr;
            stand_in->alpha_cut_mask = texture_alpha_cut_mask(alpha);
            if (!stand_in->alpha_cut_mask) return nullptr;
        }
        result = std::move(stand_in);
    } catch (...) { return nullptr; }
    if (!recorder->asset_alias(full, result)) return nullptr;
    return result;
}

// Core mode (S5b): the loader's map source is defined on the host as it is,
// and this process keeps a copy whose terrain has no vertices, tiles, slope
// tables or page bytes. Per sector it keeps what the game half reads: the
// descriptors (bounds, materials, counts) and the height fields of every
// vertex (TerrainSectorCpuAsset::heights) for sample_terrain_height. The
// stream names the copies as the full definitions. Null: keep `full`.
std::shared_ptr<const OwnedMapSnapshot> core_slim_source(
    const std::shared_ptr<const OwnedMapSnapshot>& full) noexcept {
    auto* recorder = g_core_host_recorder.load(std::memory_order_acquire);
    if (!recorder || !full || !full->terrain) return nullptr;
    std::shared_ptr<const OwnedMapSnapshot> snapshot;
    std::shared_ptr<const TerrainCpuAsset> terrain;
    try {
        auto slim = std::make_shared<TerrainCpuAsset>(*full->terrain);
        for (auto& sector : slim->sectors) {
            sector.heights.reserve(sector.vertices.size());
            for (const auto& vertex : sector.vertices) sector.heights.push_back(terrain_height_vertex(vertex));
            std::vector<TerrainVertex>().swap(sector.vertices);
            std::vector<TerrainTileDescriptor>().swap(sector.tiles);
            std::vector<std::uint32_t>().swap(sector.base_slope_normals);
            std::vector<std::uint32_t>().swap(sector.base_facet_flatness);
        }
        for (auto& page : slim->pages) page.release_upload_bytes();
        terrain = std::move(slim);
        auto copy = std::make_shared<OwnedMapSnapshot>(*full);
        copy->terrain = terrain;
        snapshot = std::move(copy);
    } catch (...) { return nullptr; }
    if (!recorder->asset_alias(full, snapshot) || !recorder->asset_alias(full->terrain, terrain)) return nullptr;
    return snapshot;
}

#endif // !EID3D11_EMBEDDED

template<class Decode>
std::shared_ptr<const TextureCpuAsset> visual_texture_asset(RuntimeState& state,
    const std::string& key, bool generated, GeneratedTextureEvent event,
    ResourceFailure& failure, Decode decode) {
    if (!state.visual_texture_sources_available) {
        failure = {ResourceFailureCode::parse, "visual texture publication scope unavailable"};
        return nullptr;
    }
    auto found = state.visual_textures.values.find(key);
    const bool replacement=generated && event!=GeneratedTextureEvent::current;
    if(found==state.visual_textures.values.end() || replacement ||
        found->second.asset.expired()) {
        state.visual_textures.reclaim([](const auto& value) {
            // A failed writer's negative publication must survive until a
            // later completion. Dropping it could resurrect stale disk bytes.
            return value.source_available && value.asset.expired();
        });
        found=state.visual_textures.values.find(key);
    }
    if (found == state.visual_textures.values.end()) {
        try {
            found=state.visual_textures.values.emplace(
                key,RuntimeState::TextureValue{}).first;
        } catch (const std::bad_alloc&) {
            if (generated && event == GeneratedTextureEvent::failed)
                state.visual_texture_sources_available = false;
            failure={ResourceFailureCode::archive_bounds,"visual texture allocation failed"};
            return nullptr;
        } catch (const std::length_error&) {
            if (generated && event == GeneratedTextureEvent::failed)
                state.visual_texture_sources_available = false;
            failure={ResourceFailureCode::archive_bounds,"visual texture allocation failed"};
            return nullptr;
        }
    }
    auto& source=found->second;
    if (generated && (!source.serial || event!=GeneratedTextureEvent::current)) {
        source.asset.reset();
        source.source_available=false;
        if (state.visual_texture_serial==UINT64_MAX) {
            failure={ResourceFailureCode::archive_bounds,"visual texture generation limit"};
            return nullptr;
        }
        source.serial=++state.visual_texture_serial;
        source.source_available=event!=GeneratedTextureEvent::failed;
    }
    if (!source.source_available) {
        failure={ResourceFailureCode::file_read,"generated texture has no successful completed source"};
        return nullptr;
    }
    if (auto pinned=source.asset.lock()) { failure={}; return pinned; }
    try {
        auto result=std::make_shared<TextureCpuAsset>();
        if (!decode(*result)) {
            if (!generated) state.visual_textures.erase(key);
            return nullptr;
        }
        result->value_serial=source.serial;
        std::shared_ptr<const TextureCpuAsset> value=std::move(result);
        // Archive members only: generated values keep their replacement path.
#ifndef EID3D11_EMBEDDED
        if(!generated)if(auto stand_in=texture_host_stand_in(value))value=std::move(stand_in);
#endif
        source.asset=value;
        failure={};
        return value;
    } catch (const std::bad_alloc&) {
        if (!generated) state.visual_textures.erase(key);
        failure={ResourceFailureCode::archive_bounds,"visual texture allocation failed"};
    } catch (const std::length_error&) {
        if (!generated) state.visual_textures.erase(key);
        failure={ResourceFailureCode::archive_bounds,"visual texture allocation failed"};
    }
    return nullptr;
}

bool enabled() {
    wchar_t value[8]{};
    return !(GetEnvironmentVariableW(L"EI_RENDERER_OWNED_LOADER", value,
                                    static_cast<DWORD>(std::size(value))) == 1 &&
        value[0] == L'0');
}

std::wstring child_path(const std::wstring& root, const std::wstring& child) {
    return root + (root.empty() || root.back() == L'\\' || root.back() == L'/'
        ? L"" : L"\\") + child;
}

bool full_path(const std::wstring& input, std::wstring& output) {
    const DWORD size = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (!size) return false;
    output.resize(size);
    const DWORD copied = GetFullPathNameW(input.c_str(), size, output.data(), nullptr);
    if (!copied || copied >= size) return false;
    output.resize(copied);
    return true;
}

#ifndef EID3D11_EMBEDDED
bool registry_path(const std::wstring& key, const wchar_t* name,
                   const wchar_t* default_value, std::wstring& path) {
    HKEY handle = nullptr;
    std::wstring value(default_value);
    const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0,
                                     KEY_QUERY_VALUE | KEY_WOW64_32KEY, &handle);
    if (opened == ERROR_SUCCESS) {
        DWORD type = 0, size = 0;
        auto result = RegQueryValueExW(handle, name, nullptr, &type, nullptr, &size);
        if (result == ERROR_SUCCESS && type == REG_SZ &&
            size >= sizeof(wchar_t) && size % sizeof(wchar_t) == 0) {
            try { value.resize(size / sizeof(wchar_t)); }
            catch (...) { RegCloseKey(handle); throw; }
            result = RegQueryValueExW(handle, name, nullptr, &type,
                reinterpret_cast<BYTE*>(value.data()), &size);
            RegCloseKey(handle);
            if (result != ERROR_SUCCESS || type != REG_SZ ||
                size % sizeof(wchar_t) || !size || size > value.size()*sizeof(wchar_t) ||
                value[size/sizeof(wchar_t)-1] != L'\0') return false;
            value.resize(size/sizeof(wchar_t)-1);
        } else {
            RegCloseKey(handle);
            if (result != ERROR_FILE_NOT_FOUND) return false;
        }
    } else if (opened != ERROR_FILE_NOT_FOUND) return false;
    if (value.empty()) { path.clear(); return true; }
    return full_path(value, path);
}

#endif

bool read_mount_config(const std::wstring& path, std::vector<std::uint8_t>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool success = false;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= MAXDWORD) {
        try {
            bytes.resize(static_cast<std::size_t>(size.QuadPart));
            DWORD read = 0;
            success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                               &read, nullptr) && read == bytes.size();
        } catch (...) { CloseHandle(file); throw; }
    }
    CloseHandle(file);
    return success;
}

// The config.reg [RES] families the owned mounts follow.
enum Family : unsigned { kFigures, kTextures, kTexts, kRedress, kMenus, kFamilies };
constexpr const char* kFamilyNames[kFamilies] = {"figures.res", "textures.res", "texts.res", "redress.res", "menus.res"};

// Compiled REG files have little-endian counts/offsets and length-prefixed
// strings. Decode the figure and texture managers' families (kFamilyNames)
// and the owned spell-text family; do not map archives or retain
// configuration bytes in the x86 process.
bool visual_archive_lists(const std::vector<std::uint8_t>& bytes,
    std::array<std::vector<std::wstring>, kFamilies>& lists, std::array<bool, kFamilies>& ignore_base) {
    const auto span = [&](std::size_t at, std::size_t count) {
        return at <= bytes.size() && count <= bytes.size()-at;
    };
    const auto u16 = [&](std::size_t at) -> std::uint16_t {
        return static_cast<std::uint16_t>(bytes[at] | (bytes[at+1] << 8));
    };
    const auto u32 = [&](std::size_t at) -> std::uint32_t {
        return bytes[at] | (std::uint32_t(bytes[at+1]) << 8) |
            (std::uint32_t(bytes[at+2]) << 16) | (std::uint32_t(bytes[at+3]) << 24);
    };
    const auto named = [&](std::size_t at, std::size_t length, const char* name) {
        return length == std::strlen(name) && span(at, length) &&
            _memicmp(bytes.data()+at, name, length) == 0;
    };
    if (!span(0, 6) || u32(0) != 0x45ab3efb) return false;
    const auto sections = u16(4);
    if (!span(6, std::size_t(sections)*6)) return false;
    bool found_res = false;
    for (std::size_t s = 0; s < sections; ++s) {
        const std::size_t section = u32(8+s*6);
        if (!span(section, 4)) return false;
        const auto count = u16(section), length = u16(section+2);
        if (!span(section+4, length)) return false;
        if (!named(section+4, length, "RES")) continue;
        if (found_res) return false;
        found_res = true;
        const auto table = section+4+length;
        if (!span(table, std::size_t(count)*6)) return false;
        std::array<bool, kFamilies> seen{};
        for (std::size_t v = 0; v < count; ++v) {
            const std::size_t offset = u32(table+v*6+2);
            if (!span(section, offset)) return false;
            const auto value = section+offset;
            if (!span(value, 3)) return false;
            const auto name_length = u16(value+1);
            if (!span(value+3, name_length)) return false;
            int family = -1;
            for (int f = 0; f < int(kFamilies); ++f)
                if (named(value+3, name_length, kFamilyNames[f])) family = f;
            if (family < 0) continue;
            if (seen[family]) return false;
            seen[family] = true;
            const auto type = bytes[value];
            if (type != 2 && type != 0x82) return false;
            std::size_t cursor = value+3+name_length, amount = 1;
            if (type == 0x82) {
                if (!span(cursor, 2)) return false;
                amount = u16(cursor); cursor += 2;
            }
            for (std::size_t item = 0; item < amount; ++item) {
                if (!span(cursor, 2)) return false;
                const auto string_length = u16(cursor); cursor += 2;
                if (!string_length || !span(cursor, string_length)) return false;
                const auto* text = reinterpret_cast<const char*>(bytes.data()+cursor);
                if (std::memchr(text, 0, string_length)) return false;
                if (text[0] == '-') ignore_base[family] = true;
                else {
                    const int size = MultiByteToWideChar(CP_ACP, 0, text, string_length, nullptr, 0);
                    if (!size) return false;
                    std::wstring path(size, L'\0');
                    if (MultiByteToWideChar(CP_ACP, 0, text, string_length, path.data(), size) != size)
                        return false;
                    lists[family].push_back(std::move(path));
                }
                cursor += string_length;
            }
        }
    }
    return true;
}

bool configuration(const wchar_t* game_registry_key, ResourceStoreConfig& mounts,
                   ResourceFailure& failure, ResourceStoreConfig& text_mounts) {
#ifdef EID3D11_EMBEDDED
    (void)game_registry_key;
    mounts = embedded_host_configuration().snapshot().mounts;
    text_mounts = mounts;
#else
    // The mounts a player install has: the active game's registry paths and
    // the mod's config.reg, in the game's per-manager order.
    {
        if (!game_registry_key) {
            failure = {ResourceFailureCode::parse, "active game resource registry selection unavailable"};
            return false;
        }
        const auto key = child_path(game_registry_key, L"Path Settings");
        std::wstring work, resources, addon;
        // Only an EI Starter startup loads the addon named by AddonPath; the
        // stock root (pure GOG game) never does, whatever Starter left there.
        if (!registry_path(key, L"Work path", L".\\", work) ||
            !registry_path(key, L"Resources path", L".\\Res\\", resources) ||
            (registry_root_loads_starter_addon(game_registry_key) &&
             !registry_path(L"Software\\Gipat.Ru\\EI_Starter\\Settings", L"AddonPath", L"", addon))) {
            failure = {ResourceFailureCode::parse, "game/addon resource paths could not be read"};
            return false;
        }
        // CD-ROM Path owns movies/speech, not the owned map/figure/texture mounts.
        std::vector<std::uint8_t> config;
        if (!addon.empty() && !read_mount_config(child_path(addon, L"config.reg"), config)) {
            failure = {ResourceFailureCode::file_read, "active mod config.reg could not be read"};
            return false;
        }
        if (!derive_owned_resource_mounts(work, resources, addon, config, mounts, failure, &text_mounts))
            return false;
    }
    // A configured archive that is missing stays in the game's list, empty
    // (CResourceManager::AddArchiveOf ignores the open result): drop it.
    for (std::size_t index = mounts.archives.size(); index-- > 0;) {
        const auto attributes = GetFileAttributesW(mounts.archives[index].c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        mounts.archives.erase(mounts.archives.begin() + index);
        if (!mounts.archive_kinds.empty()) mounts.archive_kinds.erase(mounts.archive_kinds.begin() + index);
    }
#endif
    if (mounts.loose_roots.empty() || mounts.archives.empty()) {
        failure = {ResourceFailureCode::parse, "owned resource mount list is empty"};
        return false;
    }
    // Loose roots are search locations, not required containers. A mod may
    // supply all maps while the base game has no Maps directory. ResourceStore
    // skips absent loose files and continues through the same ordered mounts;
    // rejecting an unused root here also disables unrelated UI textures.
    return true;
}

void log_loader_failure(const char* reason) noexcept {
    static std::atomic<bool> reported{false};
    if (reported.exchange(true)) return;
    char line[512]{};
    std::snprintf(line, sizeof(line), "owned_loader_unavailable: %s; owned visuals omitted\n", reason);
    OutputDebugStringA(line);
    wchar_t module[MAX_PATH]{};
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&log_loader_failure), &self)) return;
    const auto copied = GetModuleFileNameW(self, module, MAX_PATH);
    if (!copied || copied >= MAX_PATH) return;
    auto* slash = std::wcsrchr(module, L'\\');
    if (!slash) return;
    constexpr wchar_t filename[] = L"owned_renderer.log";
    if (std::size(filename) > std::size(module)-std::size_t(slash+1-module)) return;
    std::memcpy(slash+1, filename, sizeof(filename));
    HANDLE file = CreateFileW(module, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, line, static_cast<DWORD>(std::strlen(line)), &written, nullptr);
    CloseHandle(file);
}

std::string canonical_resource(std::string value) {
    for (char& byte : value) {
        if (byte == '/') byte = '\\';
        if (byte >= 'A' && byte <= 'Z') byte = static_cast<char>(byte + 32);
    }
    return value;
}

void start_next_load(RuntimeState& state) noexcept {
    if (!state.loader || state.active.transaction || state.pending.empty()) return;
    state.active = std::move(state.pending.front());
    state.pending.erase(state.pending.begin());
    OwnedMapLoadRequest request{};
    request.generation = state.active.generation;
    request.map_resource = state.active.resource;
    request.terrain_resource = state.active.terrain_resource;
    request.mounts = state.mounts;
    request.load_object_assets = false;
    // Ground-cover bark and rock art, read only from the main map source
    // (terrain_source_snapshot). A secondary MOB (an AddMob supplement) has
    // no terrain and nobody reads its textures; loading them there decoded
    // and streamed ~1.4 MB per supplement again (#203).
    if (!request.terrain_resource.empty()) request.extra_textures = {"tree02", "stone02"};
    request.prepare_terrain_shading = !request.terrain_resource.empty(); // active map terrain
    ResourceFailure failure{};
    if (!state.loader->request(request, failure, &state.active.token)) {
        state.scene.abandon_source(state.active.transaction);
        state.active = {};
        start_next_load(state);
        return;
    }
    state.polling.store(true, std::memory_order_release);
}

void retire_active(RuntimeState& state) noexcept {
    if (state.loader && state.active.generation)
        state.loader->retire_generation(state.active.generation);
    state.active = {};
    state.polling.store(false, std::memory_order_release);
}

bool active_matches(const RuntimeState::PendingLoad& active,
                    std::uint64_t transaction,
                    const OwnedResourceRequestToken& token) noexcept {
    return active.transaction == transaction &&
        active.generation == token.generation &&
        active.token.generation == token.generation &&
        active.token.request_serial == token.request_serial;
}

} // namespace

bool registry_root_loads_starter_addon(const wchar_t* game_registry_key) noexcept {
    return game_registry_key &&
        _wcsicmp(game_registry_key, L"Software\\Gipat.Ru\\EI_Starter\\EvilIslands") == 0;
}

bool derive_owned_resource_mounts(const std::wstring& work_path,
    const std::wstring& resource_path, const std::wstring& addon_path,
    const std::vector<std::uint8_t>& config, ResourceStoreConfig& mounts,
    ResourceFailure& failure, ResourceStoreConfig* text_mounts) {
    mounts = {};
    failure = {};
    std::array<std::vector<std::wstring>, kFamilies> lists;
    std::array<bool, kFamilies> ignore_base{};
    if (work_path.empty() || resource_path.empty() ||
        (!addon_path.empty() && !visual_archive_lists(config, lists, ignore_base))) {
        failure = {ResourceFailureCode::parse, "active mod resource configuration is invalid"};
        return false;
    }
    ResourceStoreConfig derived;
    if (!addon_path.empty()) derived.loose_roots.push_back(child_path(addon_path, L"Maps"));
    derived.loose_roots.push_back(child_path(work_path, L"Maps"));
    // The game's two archive chains in one ordered list, each archive tagged
    // with the lookups it serves. Figure manager: the mod's figures.res and
    // menus.res values, then base figures and menus. Texture manager: base
    // textures and redress each searched after the mod's values for them,
    // then base menus, then the mod's menus values (its textures/redress
    // values follow there again but were already searched). Each compiled
    // array keeps its order: HD overrides precede lower-priority entries.
    const auto add_mod = [&](unsigned family, std::uint8_t kinds) {
        for (const auto& relative : lists[family]) {
            std::wstring path;
            if (!full_path(child_path(addon_path, relative), path)) {
                failure = {ResourceFailureCode::invalid_name, "mod archive path could not be resolved"};
                return false;
            }
            derived.archives.push_back(std::move(path));
            derived.archive_kinds.push_back(kinds);
        }
        return true;
    };
    const auto add_base = [&](unsigned family, std::uint8_t kinds) {
        if (ignore_base[family]) return;
        const std::string name = kFamilyNames[family];
        derived.archives.push_back(child_path(resource_path, std::wstring(name.begin(), name.end())));
        derived.archive_kinds.push_back(kinds);
    };
    if (!add_mod(kFigures, kArchiveFigures) || !add_mod(kMenus, kArchiveFigures) ||
        !add_mod(kTextures, kArchiveTextures)) return false;
    add_base(kFigures, kArchiveFigures);
    add_base(kTextures, kArchiveTextures);
    if (!add_mod(kRedress, kArchiveTextures)) return false;
    add_base(kRedress, kArchiveTextures);
    add_base(kMenus, kArchiveAllKinds);
    if (!add_mod(kMenus, kArchiveTextures)) return false;
    if (text_mounts) {
        *text_mounts={};
        for (const auto& relative:lists[kTexts]) {
            std::wstring path;
            if (!full_path(child_path(addon_path,relative),path)) {
                failure={ResourceFailureCode::invalid_name,"mod text archive path could not be resolved"};
                return false;
            }
            text_mounts->archives.push_back(std::move(path));
        }
        if (!ignore_base[kTexts]) text_mounts->archives.push_back(child_path(resource_path,L"texts.res"));
    }
    mounts = std::move(derived);
    return true;
}

void owned_resource_runtime_acquire(const wchar_t* game_registry_key) noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    ++state.users;
    if (state.users != 1 || !enabled()) return;
    try {
        ResourceStoreConfig mounts{},text_mounts{};
        ResourceFailure failure{};
        if (!configuration(game_registry_key, mounts, failure, text_mounts)) {
            log_loader_failure(failure.message.c_str());
            return;
        }
        auto loader = std::make_shared<OwnedResourceLoader>();
        if (!loader->start(2, failure)) {
            loader->shutdown();
            log_loader_failure(failure.message.c_str());
            return;
        }
        state.generation = 0;
        state.mounts = std::move(mounts);
        state.text_mounts = std::move(text_mounts);
        state.loader = std::move(loader);
    } catch (...) {
        log_loader_failure("owned loader startup allocation failed");
    }
}

bool owned_visual_resource_mounts(ResourceStoreConfig& mounts) {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    mounts = state.mounts;
    return !mounts.archives.empty();
}

bool owned_text_resource_mounts(ResourceStoreConfig& mounts) {
    auto& state=runtime(); std::lock_guard<std::mutex> guard(state.lock);
    mounts=state.text_mounts;
    return !mounts.archives.empty();
}

void owned_resource_runtime_poll() noexcept {
    RuntimeState& state = runtime();
    if (!state.polling.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> guard(state.lock);
    if (!state.loader || !state.active.transaction ||
        !state.active.owner_finished)
        return;
    std::shared_ptr<const OwnedMapSnapshot> snapshot;
    if (!state.loader->try_publish(state.active.token, snapshot) || !snapshot)
        return;
    ResourceFailure failure{};
    write_owned_loader_receipt(*snapshot, failure);
#ifndef EID3D11_EMBEDDED
    if (auto slim = core_slim_source(snapshot)) snapshot = std::move(slim);
#endif
    if (!snapshot->mob ||
        (state.active.role == MobSourceRole::main && !snapshot->terrain) ||
        !state.scene.publish_source(state.active.transaction, snapshot)) {
        state.scene.abandon_source(state.active.transaction);
    }
    retire_active(state);
    start_next_load(state);
}

void owned_resource_runtime_release() noexcept {
    std::shared_ptr<OwnedResourceLoader> loader;
    {
        RuntimeState& state = runtime();
        std::lock_guard<std::mutex> guard(state.lock);
        if (!state.users || --state.users != 0) return;
        loader = std::move(state.loader);
        state.generation = 0;
        state.mounts = {};
        state.text_mounts = {};
        state.pending.clear();
        state.active = {};
        state.scene.reset();
        state.selected_resources.close();
        state.visual_models.clear();
        state.visual_surfaces.clear();
        state.visual_figures.clear();
        state.visual_textures.clear();
        state.visual_texture_sources_available = true;
        state.visual_databases.clear();
        state.visual_clips.clear();
        if (++state.resolver_generation == 0) ++state.resolver_generation;
        state.polling.store(false, std::memory_order_release);
    }
#ifndef EID3D11_EMBEDDED
    camp_inventory_shutdown_icons();
#endif
    if (loader) { neighbour_client_stop(); loader->shutdown(); }
}

bool retained_static_runtime_requested() noexcept {
    return enabled();
}

bool owned_resource_runtime_mounts(ResourceStoreConfig& mounts) noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    if ((!state.loader && !state.mounts_adopted) || state.mounts.archives.empty()) return false;
    try { mounts = state.mounts; } catch (...) { return false; }
    return true;
}

void owned_resource_runtime_adopt_mounts(const ResourceStoreConfig& mounts,
    const ResourceStoreConfig& text_mounts) noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    try {
        state.mounts = mounts;
        state.text_mounts = text_mounts;
        state.mounts_adopted = true;
    } catch (...) {
        state.mounts = {};
        state.text_mounts = {};
        state.mounts_adopted = false;
    }
}

namespace {
std::atomic<std::int64_t> g_main_begin_qpc{0}, g_main_owned_wait_qpc{0};
std::int64_t qpc_now() noexcept {LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;}
}
MainLoadTiming retained_static_main_load_timing() noexcept {
    return {g_main_begin_qpc.load(std::memory_order_acquire),g_main_owned_wait_qpc.load(std::memory_order_acquire)};
}

std::uint64_t retained_static_begin_source(
    MobSourceRole role, const char* name, const char* selected_mob) noexcept {
    if (role == MobSourceRole::main) {
        g_main_owned_wait_qpc.store(0, std::memory_order_relaxed);
        g_main_begin_qpc.store(qpc_now(), std::memory_order_release);
    }
    RuntimeState& state = runtime();
    // The neighbour stream starts after state.lock is released: starting it
    // stops the previous stream, whose decoders read the published source
    // under this lock (deadlock while it was held).
    struct NeighbourStart {
        bool armed = false; std::string terrain; std::uint64_t epoch = 0; ResourceStoreConfig mounts;
        ~NeighbourStart() { if (armed) neighbour_client_begin(terrain, epoch, mounts); }
    } neighbour_start;
    std::lock_guard<std::mutex> guard(state.lock);
    if (!state.users || !state.loader) return 0;
    bool reused_existing = false;
    const std::uint64_t transaction = state.scene.begin_source(
        role, name, selected_mob, &reused_existing);
    if (!transaction || role != MobSourceRole::main) return transaction;

    // A main begin is the authoritative new-world boundary. Scene begin has
    // already retired its older transactions; mirror that retirement in the
    // one owned loader generation and its deferred secondary queue.
    retire_active(state);
    state.pending.clear();
    if (reused_existing || !selected_mob || !selected_mob[0])
        return transaction;
    try {
        if (state.generation == (std::numeric_limits<std::uint64_t>::max)()) {
            state.scene.abandon_source(transaction);
            return 0;
        }
        RuntimeState::PendingLoad load{};
        load.transaction = transaction;
        load.generation = ++state.generation;
        load.resource = selected_mob;
        load.terrain_resource = name;
        load.role = MobSourceRole::main;
        neighbour_start.terrain = load.terrain_resource; neighbour_start.epoch = load.generation;
        neighbour_start.mounts = state.mounts; neighbour_start.armed = true;
        OwnedMapLoadRequest request{};
        request.generation = load.generation;
        request.map_resource = load.resource;
        request.terrain_resource = load.terrain_resource;
        request.mounts = state.mounts;
        request.load_object_assets = false;
        request.extra_textures = {"tree02", "stone02"}; // ground-cover bark and rock art
        request.prepare_terrain_shading = true; // the active map's terrain is never edited
        ResourceFailure failure{};
        if (!state.loader->request(request, failure, &load.token)) {
            state.scene.abandon_source(transaction);
            return 0;
        }
        state.active = std::move(load);
        // Deliberately false: Present may reenter while the stock owner is
        // running, but completion is quarantined until finish_source succeeds.
        state.polling.store(false, std::memory_order_release);
    } catch (...) {
        state.scene.abandon_source(transaction);
        return 0;
    }
    return transaction;
}

bool retained_static_record_parser_source(
    std::uint64_t transaction, const char* name) noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return state.scene.record_parser_source(transaction, name);
}

void retained_static_record_admission(
    std::uint64_t transaction, std::uint32_t nid, std::uint32_t family,
    std::int32_t signed_id) noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    state.scene.record_admission(transaction, nid, family, signed_id);
}

bool retained_static_restore_source(MobSourceRole role, const char* terrain,
    const char* mob, const std::vector<RetainedAdmission>& members) noexcept try {
    if (!mob || !*mob || (role == MobSourceRole::main && (!terrain || !*terrain))) return false;
    auto& state = runtime();
    std::unique_lock<std::mutex> guard(state.lock);
    if (!state.users || !state.loader) return false;
    if (role == MobSourceRole::main) {
        retire_active(state); state.pending.clear();
        // Even the same map has different saved membership. Never reuse the
        // preceding world's admissions, point lights or supplemental sources.
        state.scene.reset();
    }
    if (state.generation == (std::numeric_limits<std::uint64_t>::max)()) return false;
    const auto generation = ++state.generation;
    const auto loader = state.loader;
    OwnedMapLoadRequest request{};
    request.generation = generation; request.map_resource = mob;
    if (role == MobSourceRole::main) request.terrain_resource = terrain;
    request.mounts = state.mounts; request.load_object_assets = false;
    request.extra_textures = {"tree02", "stone02"};
    request.prepare_terrain_shading = role == MobSourceRole::main;
    ResourceFailure failure{}; OwnedResourceRequestToken token{};
    if (!loader->request(request, failure, &token)) return false;
    struct RetireRequest {
        std::shared_ptr<OwnedResourceLoader> loader; std::uint64_t generation;
        ~RetireRequest() { loader->retire_generation(generation); }
    } retire{loader, generation};
    guard.unlock();
    std::shared_ptr<const OwnedMapSnapshot> snapshot;
    const auto result = loader->wait_publish(token, snapshot);
    guard.lock();
    if (result != OwnedResourceWaitResult::ready || !snapshot || !snapshot->mob ||
        (role == MobSourceRole::main && !snapshot->terrain) || state.loader != loader ||
        state.generation != generation) return false;
    const auto transaction = state.scene.begin_source(role,
        role == MobSourceRole::main ? terrain : mob, role == MobSourceRole::main ? mob : nullptr);
    if (!transaction) return false;
    if (!state.scene.record_source(transaction, mob)) {
        state.scene.abandon_source(transaction); return false;
    }
    // Saved IDs include spawned objects and other MOBs. Only the intersection
    // with this immutable source may claim its static candidates.
    std::map<std::pair<std::uint32_t,std::uint32_t>,std::int32_t> accepted;
    for (const auto& member : members)
        if (member.signed_id > 0 && member.nid == std::uint32_t(member.signed_id))
            accepted.emplace(std::make_pair(member.nid, member.family), member.signed_id);
    for (const auto& object : snapshot->objects) {
        const auto found = accepted.find({object.nid, object.family});
        if (found != accepted.end())
            state.scene.record_admission(transaction, object.nid, object.family, found->second);
    }
    std::string name, project;
    if (!state.scene.finish_source(transaction, true, true, name, &project) ||
        !state.scene.publish_source(transaction, snapshot)) {
        state.scene.abandon_source(transaction); return false;
    }
    guard.unlock();
    if (role == MobSourceRole::main) neighbour_client_begin(terrain, generation, request.mounts);
    return true;
} catch (...) { return false; }

void retained_static_finish_source(
    std::uint64_t transaction, bool owner_success,
    bool common_parser_ran) noexcept {
    if (!transaction) return;
    RuntimeState& state = runtime();
    std::unique_lock<std::mutex> guard(state.lock);
    std::string name;
    std::string terrain_resource;
    bool reused_existing = false;
    if (!state.scene.finish_source(transaction, owner_success,
                                   common_parser_ran, name,
                                   &terrain_resource, &reused_existing)) {
        if (state.active.transaction == transaction) {
            retire_active(state);
            start_next_load(state);
        }
        return;
    }
    if (reused_existing) return;
    if (!state.loader) {
        state.scene.abandon_source(transaction);
        return;
    }
    try {
        if (state.active.transaction == transaction) {
            state.active.owner_finished = true;
            const RuntimeState::PendingLoad expected = state.active;
            const auto loader = state.loader;
            guard.unlock();
            std::shared_ptr<const OwnedMapSnapshot> snapshot;
            const std::int64_t wait_begin = qpc_now();
            const OwnedResourceWaitResult wait_result = loader->wait_publish(
                expected.token, snapshot);
            if (expected.role == MobSourceRole::main)
                g_main_owned_wait_qpc.store(qpc_now() - wait_begin, std::memory_order_release);
            guard.lock();
            if (wait_result != OwnedResourceWaitResult::ready || !snapshot ||
                !state.users || state.loader != loader ||
                !active_matches(state.active, transaction, expected.token)) {
                if (state.active.transaction == transaction) {
                    state.scene.abandon_source(transaction);
                    retire_active(state);
                    start_next_load(state);
                }
                return;
            }
            ResourceFailure failure{};
            write_owned_loader_receipt(*snapshot, failure);
#ifndef EID3D11_EMBEDDED
            if (auto slim = core_slim_source(snapshot)) snapshot = std::move(slim);
#endif
            if (!snapshot->mob || !snapshot->terrain ||
                !state.scene.publish_source(transaction, snapshot))
                state.scene.abandon_source(transaction);
            retire_active(state);
            start_next_load(state);
            return;
        }
        if (state.generation == (std::numeric_limits<std::uint64_t>::max)()) {
            state.scene.abandon_source(transaction);
            return;
        }
        RuntimeState::PendingLoad load{};
        load.transaction = transaction;
        load.generation = ++state.generation;
        load.role = terrain_resource.empty()
            ? MobSourceRole::secondary : MobSourceRole::main;
        load.resource = std::move(name);
        load.terrain_resource = std::move(terrain_resource);
        load.owner_finished = true;
        state.pending.push_back(std::move(load));
        start_next_load(state);
    } catch (...) {
        state.scene.abandon_source(transaction);
        if (state.active.transaction == transaction) {
            retire_active(state);
            start_next_load(state);
        }
    }
}

void retained_static_lifecycle_added(
    const LifecycleIdentity& identity) noexcept {
    runtime().scene.lifecycle_added(identity);
}


bool retained_visual_resolve_selected(const std::wstring& physical_file,
    const std::string& name, const void* selected, std::uint32_t size, std::uint64_t file_offset,
    ResourceBlob& blob, std::uint64_t& source_generation, ResourceFailure& failure,
    SelectedResourceReadDiagnostic* diagnostic, bool verified_read_only_mapping, bool container) {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    source_generation = state.resolver_generation;
    return state.selected_resources.resolve_selected_file(
        physical_file, name, selected, size, file_offset, blob, failure, diagnostic,
        verified_read_only_mapping, container);
}

bool retained_visual_copy_selected_memory(const std::string& name,
    const void* selected, std::uint32_t size, ResourceBlob& blob,
    std::uint64_t& source_generation, ResourceFailure& failure) {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    source_generation = state.resolver_generation;
    return state.selected_resources.copy_selected_memory(name, selected, size, blob, failure);
}

bool retained_visual_resolve_native_anm(const std::string& name,
    const void* selected, std::uint32_t size, ResourceBlob& blob,
    std::uint64_t& source_generation, ResourceFailure& failure) {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    source_generation = state.resolver_generation;
    return state.selected_resources.resolve_native_mapped_anm(name, selected, size, blob, failure);
}

std::shared_ptr<const ModelCpuAsset> retained_visual_skeleton_asset(
    const ResourceBlob& model, const ResourceBlob& bones, const std::string& name,
    ResourceFailure& failure, VisualAssetLookupDiagnostic* diagnostic) {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    const std::string key = model.identity.key() + "\n" + bones.identity.key() +
        "\n" + canonical_resource(name);
    return visual_asset(state.visual_models, key, failure, [&](ModelCpuAsset& result) {
        return decode_model_skeleton_resources(model, bones, name, result, failure);
    }, diagnostic);
}

std::shared_ptr<const FigureCpuAsset> retained_visual_figure_asset(
    const ResourceBlob& blob, ResourceFailure& failure,
    VisualAssetLookupDiagnostic* diagnostic) {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return visual_asset(state.visual_figures, blob.identity.key(), failure,
        [&](FigureCpuAsset& result) { return parse_figure_asset(blob, result, failure); },
        diagnostic);
}

std::shared_ptr<const TextureCpuAsset> retained_visual_texture_asset(
    const ResourceBlob& blob, ResourceFailure& failure) {
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return visual_texture_asset(state,blob.identity.key(),false,GeneratedTextureEvent::current,failure,
        [&](TextureCpuAsset& result) { return parse_texture_asset(blob,result,failure); });
}

std::shared_ptr<const TextureCpuAsset> retained_visual_generated_texture_asset(
    const std::string& name, const void* bytes, std::uint32_t size,
    ResourceFailure& failure, GeneratedTextureEvent event) {
    if (name.empty() || name.size()>1024) {
        failure={ResourceFailureCode::invalid_name,"generated MMP source name is invalid"};
        return nullptr;
    }
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    // Exact case-sensitive source name from the completed native writer.
    return visual_texture_asset(state,"generated-memory:"+name,true,event,failure,
        [&](TextureCpuAsset& result) {
            const auto address=reinterpret_cast<std::uintptr_t>(bytes);
            if (!bytes || size<76 || (size&3u) || size>UINTPTR_MAX-address) {
                failure={ResourceFailureCode::archive_bounds,"completed generated MMP span is invalid"};
                return false;
            }
            auto copied=state.selected_resources.allocate_immutable_bytes(size,failure);
            if (!copied) return false;
            if (!copy_visual_bytes(copied->data(),bytes,size)) {
                failure={ResourceFailureCode::file_read,"completed generated MMP bytes unavailable"};
                return false;
            }
            if (!parse_generated_texture_asset(copied,result,failure) ||
                !reserve_texture_metadata(state,result,failure)) return false;
            result.source_name=name;
            return true;
        });
}

std::shared_ptr<const AnimationDatabaseAsset> retained_visual_database_asset(
    const ResourceBlob& blob, ResourceFailure& failure) {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return visual_asset(state.visual_databases, blob.identity.key(), failure,
        [&](AnimationDatabaseAsset& result) { return parse_animation_database(blob, result, failure); });
}

std::shared_ptr<const AnimationDatabaseAsset> retained_visual_selected_database_asset(
    const std::string& name, const void* selected, std::uint32_t size,
    std::uint64_t& source_generation, ResourceFailure& failure) {
    source_generation = 0;
    // Shipped ADBs need not be a whole number of entries (the game never
    // checks); parse_animation_database reads them as the game does.
    if (size < 44u || size > 44u+88u*65536u) {
        failure = {ResourceFailureCode::archive_bounds, "native-selected ADB size"};
        return nullptr;
    }
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    ResourceBlob blob;
    if (!state.selected_resources.copy_selected_memory(name, selected, size, blob, failure))
        return nullptr;
    auto database = visual_asset(state.visual_databases, blob.identity.key(), failure,
        [&](AnimationDatabaseAsset& result) { return parse_animation_database(blob, result, failure); });
    // This is the same resolver epoch used by later figure/ANM selection.
    // The new memory-source incarnation is an identity, not another epoch.
    if (database) source_generation = state.resolver_generation;
    return database;
}

std::shared_ptr<const AnimationClipAsset> retained_visual_clip_asset(
    const ResourceBlob& outer, const std::string& name, ResourceFailure& failure,
    const std::shared_ptr<const AnimationDatabaseAsset>& database,
    std::uint64_t source_generation) {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    const auto key = outer.identity.key();
    auto found = state.visual_clips.values.find(key);
    auto library = found == state.visual_clips.values.end() ? nullptr : found->second.lock();
    if (!library) {
        state.visual_clips.reclaim([](const auto& value) { return value.expired(); });
        try {
            library = std::make_shared<VisualAnimationLibrary>();
            library->outer_identity = outer.identity;
#ifdef EID3D11_TESTING
            ++state.clip_reuse.outer_opens;
#endif
            if (!library->archive.open(outer,failure,&state.selected_resources)) {
                state.visual_clips.erase(key);
                return nullptr;
            }
            const auto count=library->archive.entry_count();
            if (count > SIZE_MAX/sizeof(VisualAnimationLibrary::Clip)) {
                failure={ResourceFailureCode::archive_bounds,"ANM clip index exceeds address space"};
                state.visual_clips.erase(key);
                return nullptr;
            }
            library->slot_reservation = state.selected_resources.allocate_immutable_bytes(
                0,failure,count*sizeof(VisualAnimationLibrary::Clip));
            if (!library->slot_reservation) {
                state.visual_clips.erase(key);
                return nullptr;
            }
            library->clips.resize(count);
            state.visual_clips.values.insert_or_assign(key, library);
        } catch (const std::bad_alloc&) {
            state.visual_clips.erase(key);
            failure={ResourceFailureCode::archive_bounds,"visual animation library allocation failed"};
            return nullptr;
        } catch (const std::length_error&) {
            state.visual_clips.erase(key);
            failure={ResourceFailureCode::archive_bounds,"visual animation library allocation failed"};
            return nullptr;
        }
    }
    auto clip = visual_clip_from_library(state,library,name,failure);
    if (clip) bind_visual_library(database,source_generation,library);
    return clip;
}

RetainedVisualSiblingResult retained_visual_sibling_clip_asset(
    const std::shared_ptr<const AnimationDatabaseAsset>& database,
    std::uint64_t source_generation, const std::string& outer_name,
    const std::string& clip_name, std::shared_ptr<const AnimationClipAsset>& clip,
    ResourceFailure& failure) {
    clip.reset();
    failure = {};
    if (!database || !source_generation) return RetainedVisualSiblingResult::unavailable;
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    const auto& binding = database->library_binding;
    // This weak alias is installed only after the native manager's exact ANM
    // selection has been accepted. The selected-resource store pins that
    // physical override for its resolver generation. Never cross an ADB
    // incarnation, generation, or completed visual name when reusing it.
    if (source_generation != state.resolver_generation ||
        binding.source_generation != source_generation ||
        !same_resource_identity(binding.database_identity,database->identity))
        return RetainedVisualSiblingResult::unavailable;
    auto owner = binding.owner.lock();
    if (!owner || !canonical_name_matches(owner->outer_identity.canonical_name,outer_name))
        return RetainedVisualSiblingResult::unavailable;
    auto library = std::static_pointer_cast<VisualAnimationLibrary>(std::move(owner));
    clip = visual_clip_from_library(state,std::move(library),clip_name,failure);
    return clip ? RetainedVisualSiblingResult::resolved : RetainedVisualSiblingResult::rejected;
}

void retained_static_lifecycle_removed(std::int32_t signed_id) noexcept {
    runtime().scene.lifecycle_removed(signed_id);
}

std::shared_ptr<const OwnedVisualSurface> retained_visual_surface_value(
    std::uint64_t id, std::uint64_t generation) noexcept {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    const auto found = state.visual_surfaces.values.find(id);
    if (found == state.visual_surfaces.values.end()) return nullptr;
    auto value = found->second.lock();
    return value && value->generation == generation ? value : nullptr;
}

// Temporary (memory attribution): live owned immutable bytes, which include
// the retained copy of every completed visual surface.
ResourceStoreStats owned_visual_resource_stats() noexcept {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return state.selected_resources.stats();
}

#ifndef EID3D11_EMBEDDED
void retained_visual_textures_to_host(RetainedPublicationRecorder* recorder) noexcept {
    g_core_host_recorder.store(recorder, std::memory_order_release);
}
#endif

std::uint64_t owned_visual_texture_source_bytes() noexcept {
    auto& state = runtime();
    std::uint64_t bytes = 0;
    try {
        std::lock_guard<std::mutex> guard(state.lock);
        std::unordered_set<const std::vector<std::uint8_t>*> counted;
        for (const auto& item : state.visual_textures.values) {
            const auto texture = item.second.asset.lock();
            if (!texture || !texture->source || !counted.insert(texture->source.get()).second) continue;
            bytes += texture->source->size();
        }
    } catch (...) {}
    return bytes;
}

bool retained_visual_complete_surface(const OwnedVisualSurface& description,
    const std::uint8_t* pixels, std::size_t bytes,
    std::shared_ptr<const OwnedVisualSurface>& value, bool alpha_prepared) noexcept {
    value.reset();
    auto& state = runtime();
    try {
        std::lock_guard<std::mutex> guard(state.lock);
        const auto id = description.id;
        const auto found = state.visual_surfaces.values.find(id);
        if (found != state.visual_surfaces.values.end()) if (auto previous = found->second.lock()) {
            if (previous->generation > description.generation) return false;
            if (previous->generation == description.generation) {
                value = std::move(previous);
                return true;
            }
        }
        state.visual_surfaces.reclaim([](const auto& source) { return source.expired(); });
        if (!alpha_prepared && (!pixels || !bytes)) return false;
        auto next = std::make_shared<OwnedVisualSurface>(description);
        if (!alpha_prepared) {
            TextureAlphaPixels alpha;
            const bool readable = visual_surface_raw_alpha(*next, pixels, bytes, alpha);
            next->alpha_fully_opaque = readable && texture_alpha_fully_opaque(alpha);
            if (readable && !next->alpha_fully_opaque) next->alpha_cut_mask = texture_alpha_cut_mask(alpha);
        }
        state.visual_surfaces.values.insert_or_assign(id, next);
        if (!state.scene.visual_replace_surface(id, next)) {
            state.scene.visual_replace_surface(id, nullptr);
            state.visual_surfaces.erase(id);
            return false;
        }
        value = std::move(next);
        return true;
    } catch (...) { return false; }
}

void retained_visual_retire_surface(std::uint64_t id) noexcept {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    state.scene.visual_replace_surface(id, nullptr);
    state.visual_surfaces.erase(id);
}

void retained_static_positive_reset(std::uint64_t new_epoch) noexcept {
    runtime().scene.positive_lifecycle_reset(new_epoch);
}

// World change: the identity indexes keep a node per value any world ever
// published, reclaimed only 8 per miss. Expired entries go now (live values
// of the new world and fail-closed negative texture entries stay), with the
// retained scene's frame scratch sized by the previous world.
void owned_resource_runtime_release_world_bookkeeping() noexcept {
    auto& state = runtime();
    {
        std::lock_guard<std::mutex> guard(state.lock);
        const auto sweep = [](auto& pool, auto expired) {
            for (auto entry = pool.values.begin(); entry != pool.values.end();) {
                if (expired(entry->second)) entry = pool.values.erase(entry); else ++entry;
            }
            pool.cleanup = pool.values.end();
        };
        const auto weak = [](const auto& value) { return value.expired(); };
        sweep(state.visual_models, weak);
        sweep(state.visual_figures, weak);
        sweep(state.visual_databases, weak);
        sweep(state.visual_clips, weak);
        sweep(state.visual_surfaces, weak);
        sweep(state.visual_textures, [](const RuntimeState::TextureValue& value) {
            return value.source_available && value.asset.expired();
        });
    }
    state.scene.release_visual_frame_scratch();
}

RetainedStaticBindingView retained_static_binding_view() noexcept {
    return runtime().scene.binding_view();
}

RetainedSceneIdentitySnapshot retained_static_scene_identity_snapshot() {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return state.scene.identity_snapshot();
}

RetainedTerrainSourceSnapshot retained_static_terrain_source_snapshot() noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return state.scene.terrain_source_snapshot();
}

RetainedTerrainSourceIdentity retained_static_terrain_source_identity() noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return state.scene.terrain_source_identity();
}

#ifdef EID3D11_TESTING
bool owned_weak_pool_cursor_contract_for_test() noexcept {
    const auto expired=[](const auto& value) { return value.expired(); };
    WeakPool<int,std::weak_ptr<int>> pool;
    pool.reclaim(expired);

    auto one=std::make_shared<int>(1);
    pool.values.emplace(1,one);
    pool.reclaim(expired);
    pool.reclaim(expired);
    if (pool.values.size()!=1) return false;

    one.reset();
    pool.reclaim(expired);
    if (!pool.values.empty()) return false;

    auto live=std::make_shared<int>(2);
    pool.values.emplace(1,std::weak_ptr<int>{});
    pool.values.emplace(2,live);
    pool.reclaim(expired);
    pool.reclaim(expired);
    if (pool.values.size()!=1 || pool.values.begin()->first!=2) return false;

    for(int cycle=0;cycle!=32;++cycle) {
        pool.values.emplace(100+cycle,std::weak_ptr<int>{});
        pool.reclaim(expired);
        pool.erase(2);
        pool.values.insert_or_assign(2,live);
        pool.reclaim(expired);
    }
    if (pool.values.size()!=1 || pool.values.begin()->first!=2) return false;

    WeakPool<int,RuntimeState::TextureValue> textures;
    RuntimeState::TextureValue negative;
    negative.source_available=false;
    textures.values.emplace(1,negative);
    textures.values.emplace(2,RuntimeState::TextureValue{});
    textures.reclaim([](const auto& value) {
        return value.source_available && value.asset.expired();
    });
    return textures.values.size()==1 && textures.values.begin()->first==1 &&
        !textures.values.begin()->second.source_available;
}

bool owned_visual_pool_admission_contract_for_test() noexcept {
    constexpr std::size_t admitted=kVisualPartLimit+1;
    constexpr std::size_t replacements=1024;
    auto& state=runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    const auto string_key=[](std::size_t index) { return std::to_string(index); };
    const auto exercise=[&](auto& pool,auto make,auto key) {
        using Pointer=decltype(make());
        std::vector<Pointer> pins;
        pool.clear();
        pins.reserve(admitted);
        for(std::size_t index=0;index<admitted;++index) {
            pool.reclaim([](const auto& value) { return value.expired(); });
            auto value=make();
            pool.values.insert_or_assign(key(index),value);
            pins.push_back(std::move(value));
        }
        if(pool.values.size()!=admitted)return false;
        pins.clear();
        pins.reserve(replacements);
        for(std::size_t index=0;index<replacements;++index) {
            pool.reclaim([](const auto& value) { return value.expired(); });
            auto value=make();
            pool.values.insert_or_assign(key(admitted+index),value);
            pins.push_back(std::move(value));
        }
        const bool result=pool.values.size()==replacements;
        pool.clear();
        return result;
    };
    try {
        if(!exercise(state.visual_models,[]{return std::make_shared<ModelCpuAsset>();},string_key) ||
            !exercise(state.visual_figures,[]{return std::make_shared<FigureCpuAsset>();},string_key) ||
            !exercise(state.visual_databases,[]{return std::make_shared<AnimationDatabaseAsset>();},string_key) ||
            !exercise(state.visual_clips,[]{return std::make_shared<VisualAnimationLibrary>();},string_key) ||
            !exercise(state.visual_surfaces,[]{return std::make_shared<OwnedVisualSurface>();},
                [](std::size_t index){return static_cast<std::uint64_t>(index);})) return false;

        std::vector<std::shared_ptr<const TextureCpuAsset>> textures;
        state.visual_textures.clear();
        textures.reserve(admitted);
        for(std::size_t index=0;index<admitted;++index) {
            state.visual_textures.reclaim([](const auto& value) {
                return value.source_available && value.asset.expired();
            });
            auto value=std::make_shared<TextureCpuAsset>();
            RuntimeState::TextureValue source;source.asset=value;
            state.visual_textures.values.insert_or_assign(string_key(index),source);
            textures.push_back(std::move(value));
        }
        if(state.visual_textures.values.size()!=admitted)return false;
        textures.clear();
        RuntimeState::TextureValue negative;negative.source_available=false;
        state.visual_textures.values.insert_or_assign("negative",negative);
        textures.reserve(replacements);
        for(std::size_t index=0;index<replacements;++index) {
            state.visual_textures.reclaim([](const auto& value) {
                return value.source_available && value.asset.expired();
            });
            auto value=std::make_shared<TextureCpuAsset>();
            RuntimeState::TextureValue source;source.asset=value;
            state.visual_textures.values.insert_or_assign(
                "replacement-"+string_key(index),source);
            textures.push_back(std::move(value));
        }
        const auto negative_entry=state.visual_textures.values.find("negative");
        const bool result=state.visual_textures.values.size()==replacements+1 &&
            negative_entry!=state.visual_textures.values.end() &&
            !negative_entry->second.source_available;
        state.visual_textures.clear();
        return result;
    } catch (...) {
        state.visual_models.clear();
        state.visual_figures.clear();
        state.visual_textures.clear();
        state.visual_databases.clear();
        state.visual_clips.clear();
        state.visual_surfaces.clear();
        return false;
    }
}

OwnedVisualClipReuseTestStats owned_visual_clip_reuse_for_test() noexcept {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return state.clip_reuse;
}
std::array<std::size_t,6> owned_visual_pool_sizes_for_test() noexcept {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return {state.visual_models.values.size(),state.visual_figures.values.size(),
        state.visual_textures.values.size(),state.visual_databases.values.size(),
        state.visual_clips.values.size(),state.visual_surfaces.values.size()};
}
ResourceStoreStats owned_visual_byte_budget_for_test() noexcept {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    return state.selected_resources.stats();
}
void owned_visual_set_byte_limit_for_test(std::uint64_t bytes) noexcept {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    state.selected_resources.set_immutable_byte_limit_for_test(bytes);
}
bool owned_resource_runtime_wait_active_ready_for_test() noexcept {
    RuntimeState& state = runtime();
    std::shared_ptr<OwnedResourceLoader> loader;
    OwnedResourceRequestToken token{};
    {
        std::lock_guard<std::mutex> guard(state.lock);
        loader = state.loader;
        token = state.active.token;
    }
    return loader && loader->wait_ready_for_test(token);
}

void owned_resource_runtime_close_selected_for_test() noexcept {
    auto& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    state.selected_resources.close();
    if (++state.resolver_generation == 0) ++state.resolver_generation;
}

bool owned_resource_runtime_hold_active_completion_for_test() noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    if (!state.loader) return false;
    state.test_loader_pin = state.loader;
    state.loader->hold_completion_for_test();
    return true;
}

bool owned_resource_runtime_wait_completion_held_for_test() noexcept {
    RuntimeState& state = runtime();
    std::shared_ptr<OwnedResourceLoader> loader;
    {
        std::lock_guard<std::mutex> guard(state.lock);
        loader = state.loader ? state.loader : state.test_loader_pin;
    }
    return loader && loader->wait_completion_held_for_test();
}

bool owned_resource_runtime_wait_waiter_entered_for_test() noexcept {
    RuntimeState& state = runtime();
    std::shared_ptr<OwnedResourceLoader> loader;
    {
        std::lock_guard<std::mutex> guard(state.lock);
        loader = state.loader ? state.loader : state.test_loader_pin;
    }
    return loader && loader->wait_waiter_entered_for_test();
}

void owned_resource_runtime_release_completion_for_test() noexcept {
    RuntimeState& state = runtime();
    std::shared_ptr<OwnedResourceLoader> loader;
    {
        std::lock_guard<std::mutex> guard(state.lock);
        loader = state.loader ? state.loader : state.test_loader_pin;
        state.test_loader_pin.reset();
    }
    if (loader) loader->release_completion_for_test();
}

OwnedResourceRuntimeTestState owned_resource_runtime_state_for_test() noexcept {
    RuntimeState& state = runtime();
    std::lock_guard<std::mutex> guard(state.lock);
    OwnedResourceRuntimeTestState result{};
    result.active_transaction = state.active.transaction;
    result.pending_loads = state.pending.size();
    result.polling = state.polling.load(std::memory_order_relaxed);
    result.owner_finished = state.active.owner_finished;
    return result;
}
#endif

} // namespace owned
