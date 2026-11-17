// Material classes for owned character and equipment shading (pseudo-PBR
// S1): the data table, the item catalog, reviewed region maps, the composed
// atlas profile, and the renderer's per-texture profile resources.
#include "material_library.h"

#include "engine_bridge.h"
#include "owned_asset_loader.h"
#include "owned_resource_runtime.h"
#include "owned_resource_store.h"
#include "renderer.h"
#include "renderer_internal.h"
#include "retained_static_scene.h"
#include "small_json.h"

#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <exception>

namespace owned {
namespace {

std::wstring material_folder() {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&material_folder), &module) ||
        !GetModuleFileNameW(module, path, MAX_PATH)) return {};
    std::wstring folder = path;
    const auto slash = folder.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    return folder.substr(0, slash + 1) + L"eid3d_materials\\";
}

bool read_file(const std::wstring& path, std::vector<std::uint8_t>& bytes, std::size_t limit) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") || !file) return false;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    bool ok = size > 0 && std::size_t(size) <= limit;
    if (ok) { bytes.resize(std::size_t(size)); ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size(); }
    std::fclose(file);
    return ok;
}

std::wstring widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

std::string lower(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    return text;
}

// items.idb: nested records {id byte, length (byte, or dword when odd) / 2, bytes}.
struct Span { const std::uint8_t* data = nullptr; std::size_t size = 0; unsigned id = 0; };
std::uint32_t word(const std::uint8_t* value) noexcept { std::uint32_t r; std::memcpy(&r, value, 4); return r; }
bool children(Span parent, std::vector<Span>& result) {
    result.clear();
    std::size_t at = 0;
    while (at < parent.size) {
        if (parent.size - at < 2 || result.size() >= 65536) return false;
        const unsigned id = parent.data[at++];
        std::uint32_t encoded = parent.data[at];
        if (encoded & 1) { if (parent.size - at < 4) return false; encoded = word(parent.data + at) - 1; at += 4; }
        else ++at;
        const std::size_t bytes = encoded / 2;
        if (bytes > parent.size - at) return false;
        result.push_back({parent.data + at, bytes, id});
        at += bytes;
    }
    return true;
}
Span field(const std::vector<Span>& fields, unsigned id) noexcept {
    for (const auto& value : fields) if (value.id == id) return value;
    return {};
}
std::string text(Span value) {
    std::string result(value.data ? reinterpret_cast<const char*>(value.data) : "", value.size);
    while (!result.empty() && result.back() == 0) result.pop_back();
    return lower(result);
}
// Armor display type -> texture family kind.
std::string armor_kind(const std::string& type) {
    static const char* const table[][2] = {{"helm","hl"},{"plate","pl"},{"leggings","lg"},{"shirt","sh"},
        {"pants","pt"},{"boots","bt"},{"gloves","gl"}};
    for (const auto& row : table) if (type == row[0]) return row[1];
    return {};
}
// Serialized weapon type -> texture family kind. The addon's two-handed
// families (9-15) are distinct texture sets with the same display types.
std::string weapon_kind(std::uint32_t type) {
    static const char* const table[] = {"sw","ax","dg","sp","hm","bw","cb",nullptr,nullptr,
        "sd","tp","sf","ts","ta","th","tf"};
    return type < std::size(table) && table[type] ? table[type] : std::string();
}

// One texel's alpha 0..1 under the uploader's rule for each source format.
float source_alpha(const TextureAlphaPixels& source, std::uint32_t x, std::uint32_t y) noexcept {
    if (source.format == TextureAlphaFormat::bc1) return texture_alpha_texel_cut(source, x, y) ? 0.f : 1.f;
    if (source.format == TextureAlphaFormat::bc2) {
        const auto* block = source.data + (y / 4) * source.pitch + (x / 4) * 16;
        const unsigned texel = (y % 4) * 4 + x % 4;
        return float((block[texel / 2] >> ((texel & 1) * 4)) & 15) / 15.f;
    }
    if (source.format != TextureAlphaFormat::packed) return texture_alpha_texel_cut(source, x, y) ? 0.f : 1.f;
    if (!source.alpha_mask) return 1.f;
    std::uint32_t raw = 0;
    std::memcpy(&raw, source.data + std::size_t(y) * source.pitch + std::size_t(x) * source.pixel_bytes, source.pixel_bytes);
    std::uint32_t mask = source.alpha_mask;
    unsigned shift = 0;
    while (!(mask & 1)) { ++shift; mask >>= 1; }
    return float((raw & source.alpha_mask) >> shift) / float(mask);
}

// source_alpha for one layer's many texels: a packed format's alpha shift
// and every value of its alpha field (at most 8 bits) are worked out once,
// with the same arithmetic, so each texel is a load and a table read.
class AlphaReader {
public:
    explicit AlphaReader(const TextureAlphaPixels& source) noexcept : source_(source) {
        if (source.format != TextureAlphaFormat::packed || !source.alpha_mask) return;
        std::uint32_t mask = source.alpha_mask;
        while (!(mask & 1)) { ++shift_; mask >>= 1; }
        if (mask > 255) return;
        for (std::uint32_t v = 0; v <= mask; ++v) table_[v] = float(v) / float(mask);
        fast_ = true;
    }
    float operator()(std::uint32_t x, std::uint32_t y) const noexcept {
        if (!fast_) return source_alpha(source_, x, y);
        std::uint32_t raw = 0;
        const auto* at = source_.data + std::size_t(y) * source_.pitch + std::size_t(x) * source_.pixel_bytes;
        if (source_.pixel_bytes == 4) std::memcpy(&raw, at, 4);
        else std::memcpy(&raw, at, source_.pixel_bytes);
        return table_[(raw & source_.alpha_mask) >> shift_];
    }
private:
    const TextureAlphaPixels& source_;
    bool fast_ = false;
    unsigned shift_ = 0;
    float table_[256]{};
};

// std::lround(v * 255) of a clamped 0..1 value without the CRT call: v * 255
// is a float in 0..255, so its double plus .5 is exact and truncates to the
// same half-away-from-zero rounding.
inline std::uint8_t unorm8(float v) noexcept {
    const float scaled = std::clamp(v, 0.f, 1.f) * 255.f;
    return std::uint8_t(int(double(scaled) + .5));
}

// Threads for one profile build (it already runs off the render thread):
// half the logical cores, at most 8; none extra on 4 cores or fewer.
unsigned profile_threads() noexcept {
    const unsigned cores = std::thread::hardware_concurrency();
    return cores <= 4 ? 1u : std::clamp(cores / 2, 1u, 8u);
}

// task(0..count-1) over up to `workers` threads, this one included; the
// extra threads run below normal priority so they never compete with the
// game and render threads. A thread that cannot start leaves its share here.
template <class Task> void run_parallel(unsigned count, unsigned workers, const Task& task) {
    workers = std::max(1u, std::min(workers, count));
    std::exception_ptr failure;
    std::mutex failure_lock;
    const auto share = [&](unsigned first) {
        try { for (unsigned at = first; at < count; at += workers) task(at); }
        catch (...) { std::lock_guard<std::mutex> guard(failure_lock); if (!failure) failure = std::current_exception(); }
    };
    std::vector<std::thread> threads;
    unsigned started = 1;
    for (; started < workers; ++started) {
        try {
            threads.emplace_back([&share, started] {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                share(started);
            });
        } catch (...) { break; }
    }
    share(0);
    for (unsigned rest = started; rest < workers; ++rest) share(rest);
    for (auto& thread : threads) thread.join();
    if (failure) std::rethrow_exception(failure);
}

// An 8-bit palette or grey PNG: its raw indices (WIC, no conversion).
bool decode_index_png(const std::vector<std::uint8_t>& bytes, std::uint32_t& width, std::uint32_t& height,
                      std::vector<std::uint8_t>& indices) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Uninit { bool active; ~Uninit() { if (active) CoUninitialize(); } } uninit{SUCCEEDED(init)};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    WICPixelFormatGUID format{};
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetPixelFormat(&format)) ||
        FAILED(frame->GetSize(&width, &height)) || !width || !height || width > 4096 || height > 4096 ||
        (format != GUID_WICPixelFormat8bppIndexed && format != GUID_WICPixelFormat8bppGray)) return false;
    indices.resize(std::size_t(width) * height);
    return SUCCEEDED(frame->CopyPixels(nullptr, width, static_cast<UINT>(indices.size()), indices.data()));
}

} // namespace

bool MaterialLibrary::load_classes(const std::string& document, std::string& error) {
    using namespace small_json;
    try {
        const auto root = Reader(document).root();
        for (const auto& entry : root.field("classes").members) {
            const auto& value = entry.second;
            const double index = value.field("index").num();
            if (index < 1 || index > 254 || index != std::floor(index)) { error = "classes.json: class index outside 1..254"; return false; }
            auto& target = classes_[std::size_t(index)];
            if (target.defined) { error = "classes.json: duplicate class index"; return false; }
            target.defined = true;
            target.name = entry.first;
            target.roughness = float(value.field("roughness").num());
            target.metal = float(value.field("metal").num());
            target.f0 = float(value.field("f0").num());
            if (value.members.count("relief")) target.relief = float(value.field("relief").num());
            if (value.members.count("sheen")) target.sheen = float(value.field("sheen").num());
            if (value.members.count("weave")) target.weave = std::clamp(float(value.field("weave").num()), 0.f, 1.f);
            if (value.members.count("inherit_primary"))
                target.inherit_primary = value.field("inherit_primary").text == "true";
            if (value.members.count("roughness_min")) target.roughness_min = float(value.field("roughness_min").num());
            if (value.members.count("relief_min")) target.relief_min = float(value.field("relief_min").num());
            if (value.members.count("strands")) target.strands = value.field("strands").text == "true";
            if (value.members.count("bare")) target.bare = value.field("bare").text == "true";
            if (value.members.count("coat")) target.coat = std::clamp(float(value.field("coat").num()), 0.f, 7.9f);
            if (value.members.count("gem")) target.gem = value.field("gem").text == "true";
            if (value.members.count("crystal")) target.crystal = value.field("crystal").text == "true";
            // The crystal marker is its F0 (profile b .4), as the gem's is .12 and up.
            if (target.metal < .5f && (target.f0 >= .09f && target.f0 < .11f) != target.crystal) {
                error = "classes.json: " + entry.first + (target.crystal ? ": a crystal needs f0 .10" : ": f0 .09 to .11 is reserved for crystals");
                return false;
            }
            if (target.crystal && target.metal >= .5f) { error = "classes.json: " + entry.first + ": a crystal is a dielectric"; return false; }
            // The shader tells a gem by its F0 (profile F0 is f0 x 4, at most .25).
            if (target.metal < .5f && (target.f0 >= .12f) != target.gem) {
                error = "classes.json: " + entry.first + (target.gem ? ": a gem needs f0 .12 to .25" : ": f0 .12 and up is reserved for gems");
                return false;
            }
            if (target.gem && (target.metal >= .5f || target.f0 > .25f)) { error = "classes.json: " + entry.first + ": a gem is a dielectric with f0 .12 to .25"; return false; }
            if (value.members.count("tint")) {
                const auto& tint = value.field("tint").list();
                if (tint.size() != 2) { error = "classes.json: tint must be [min, max] degrees"; return false; }
                target.tint_min = float(tint[0].num());target.tint_max = float(tint[1].num());
            }
            class_index_[entry.first] = std::uint8_t(index);
        }
        const auto named = [&](const std::string& name, std::uint8_t& index) {
            const auto found = class_index_.find(name);
            if (found == class_index_.end()) return false;
            index = found->second;
            return true;
        };
        for (const auto& entry : root.field("items").members) {
            std::uint8_t index = 0;
            if (!named(entry.second.str(), index)) { error = "classes.json: item maps to an unknown class"; return false; }
            items_[lower(entry.first)] = index;
        }
        if (!named(root.field("skin").str(), skin_) || !named(root.field("fallback").str(), fallback_)) {
            error = "classes.json: skin or fallback class missing"; return false;
        }
        if (root.members.count("metal_diffuse_keep"))
            diffuse_keep_ = std::clamp(float(root.field("metal_diffuse_keep").num()), 0.f, 1.f);
        if (root.members.count("metal_mask")) {
            const auto& mask = root.field("metal_mask");
            auto& m = metal_mask_;
            m.saturation = float(mask.field("saturation").num());
            m.cloth_saturation = float(mask.field("cloth_saturation").num());
            const auto& brown = mask.field("brown_hue").list();
            if (brown.size() != 2) { error = "classes.json: metal_mask brown_hue must be [min, max]"; return false; }
            m.brown_hue_min = float(brown[0].num()); m.brown_hue_max = float(brown[1].num());
            m.min_value = float(mask.field("min_value").num());
            if (!named(mask.field("leather").str(), m.leather) || !named(mask.field("cloth").str(), m.cloth)) {
                error = "classes.json: metal_mask names an unknown class"; return false;
            }
            m.defined = true;
        }
    } catch (const std::exception& problem) {
        error = std::string("classes.json: ") + problem.what();
        return false;
    }
    return true;
}

void load_terrain_roughness(std::array<float, 8>& roughness) {
    static const char* const names[8] = {"ground", "hard", "grass", "snow", "ice", "sand", "mud", "flat"};
    roughness = {.92f, .78f, .95f, .55f, .25f, .88f, .6f, .78f};
    std::vector<std::uint8_t> bytes;
    const std::wstring folder = material_folder();
    if (folder.empty() || !read_file(folder + L"classes.json", bytes, 4u << 20)) {
        bridge_report_event("materials", "terrain roughness: eid3d_materials\\classes.json missing, class defaults kept");
        return;
    }
    try {
        const std::string document(bytes.begin(), bytes.end());
        const auto root = small_json::Reader(document).root();
        if (!root.members.count("terrain")) return;
        const auto& terrain = root.field("terrain");
        std::array<float, 8> read = roughness;
        for (unsigned k = 0; k < 8; ++k)
            if (terrain.members.count(names[k])) read[k] = std::clamp(float(terrain.field(names[k]).num()), .05f, 1.f);
        roughness = read;
    } catch (const std::exception& problem) {
        const std::string text = std::string("terrain roughness: classes.json ") + problem.what() + ", class defaults kept";
        bridge_report_event("materials", text.c_str());
    }
}

bool MaterialLibrary::load_items(const std::vector<std::uint8_t>& bytes) {
    std::vector<Span> roots, tables, rows, fields;
    if (!children({bytes.data(), bytes.size()}, roots) || !children(field(roots, 1), tables) ||
        !children(field(tables, 1), rows)) return false;
    for (const auto& row : rows) {
        if (!children(row, fields)) return false;
        auto type = text(field(fields, 1)), code = text(field(fields, 2));
        if (type.empty() || code.empty()) return false;
        materials_.push_back({std::move(type), std::move(code)});
    }
    for (unsigned table : {2u, 3u}) {
        if (!children(field(tables, table), rows)) return false;
        auto& shapes = table == 2 ? weapon_ : armor_;
        for (const auto& row : rows) {
            if (!children(row, fields)) return false;
            const auto first = field(fields, 5), second = field(fields, 6), type_id = field(fields, 2);
            if (first.size != 4 || second.size != 4 || (table == 2 && type_id.size != 4)) return false;
            shapes.push_back({text(field(fields, 1)), text(field(fields, 3)), word(first.data), word(second.data), table == 2 ? word(type_id.data) : 0u});
        }
    }
    return !materials_.empty() && !armor_.empty();
}

bool MaterialLibrary::load_units(const std::string& document, std::string& error, bool scenery) {
    const char* const file = scenery ? "scenery.json" : "units.json";
    using namespace small_json;
    try {
        const auto root = Reader(document).root();
        const auto named = [&](const std::string& name, std::uint8_t& index) {
            const auto found = class_index_.find(name);
            if (found == class_index_.end()) { error = std::string(file) + ": unknown class " + name; return false; }
            index = found->second;
            return true;
        };
        for (const auto& entry : root.field("textures").members) {
            UnitTexture unit;
            const auto& value = entry.second;
            if (value.kind == Value::object) {
                if (!named(value.field("class").str(), unit.primary)) return false;
                if (value.members.count("parts")) {
                    unit.model = lower(value.field("model").str());
                    for (const auto& part : value.field("parts").members) {
                        std::uint8_t index = 0;
                        if (!named(part.second.str(), index)) return false;
                        unit.parts.emplace_back(part.first, index);
                    }
                }
            } else if (!named(value.str(), unit.primary)) return false;
            const std::string key = lower(entry.first);
            if (scenery) { if (units_.count(key)) continue; scenery_.insert(key); }  // units.json wins
            units_[key] = std::move(unit);
        }
        if (!scenery && root.members.count("skins"))
            for (const auto& entry : root.field("skins").members) {
                std::uint8_t index = 0;
                if (!named(entry.second.str(), index)) return false;
                unit_skins_[lower(entry.first)] = index;
            }
    } catch (const std::exception& problem) {
        error = std::string(file) + ": " + problem.what();
        return false;
    }
    return true;
}

bool MaterialLibrary::open_layers(std::string& note) const {
    if (layers_open_) return true;
    ResourceStoreConfig mounts;
    ResourceFailure failure;
    if (!owned_resource_runtime_mounts(mounts) || !layers_.open(mounts, failure)) { note = "game archives unavailable"; return false; }
    layers_open_ = true;
    return true;
}

bool MaterialLibrary::load(std::string& error) {
    folder_ = material_folder();
    std::vector<std::uint8_t> bytes;
    if (folder_.empty() || !read_file(folder_ + L"classes.json", bytes, 4u << 20)) {
        error = "eid3d_materials\\classes.json missing";
        return false;
    }
    if (!load_classes(std::string(bytes.begin(), bytes.end()), error)) return false;
    if (!read_file(folder_ + L"units.json", bytes, 4u << 20)) {
        error = "eid3d_materials\\units.json missing";
        return false;
    }
    if (!load_units(std::string(bytes.begin(), bytes.end()), error)) return false;
    // Optional: without scenery.json world objects keep the S2 response.
    if (read_file(folder_ + L"scenery.json", bytes, 4u << 20) &&
        !load_units(std::string(bytes.begin(), bytes.end()), error, true)) return false;
    ResourceStoreConfig mounts;
    ResourceStore store;
    ResourceFailure failure;
    ResourceBlob blob;
    if (!owned_data_resource_mounts(mounts) || !store.open(mounts, failure) ||
        !store.resolve("items.idb", blob, failure) || !blob.bytes || !load_items(*blob.bytes)) {
        error = "items.idb unavailable in the active database archives";
        return false;
    }
    return true;
}

// The layer's class: its item (user data tag matched to the exact layer name)
// with any reviewed region map; an untagged item layer by its own name; any
// other layer (skin, hair, wounds) is skin.
MaterialLibrary::LayerClass MaterialLibrary::layer_class(const std::string& layer,
    const std::vector<std::uint8_t>& user_data, std::uint32_t width, std::uint32_t height) const {
    LayerClass result{skin_, nullptr};
    std::string name = lower(layer);
    for (const char* extension : {".mmp", ".x4", ".x2"}) {
        const std::size_t n = std::strlen(extension);
        if (name.size() >= n && name.compare(name.size() - n, n, extension) == 0) name.resize(name.size() - n);
    }
    // A base skin layer <model>skin_NN takes its model's class (skeleton bone).
    if (const auto at = name.find("skin_"); at != std::string::npos && at > 0)
        if (const auto found = unit_skins_.find(name.substr(0, at)); found != unit_skins_.end()) result.primary = found->second;
    if (user_data.size() % 12) return untagged_layer_class(layer, name, result, width, height);
    for (std::size_t at = 0; at + 12 <= user_data.size(); at += 12) {
        const auto type = word(user_data.data() + at), item = word(user_data.data() + at + 4);
        if ((type != 0x3004 && type != 0x3005) || item == UINT32_MAX) continue;
        const auto& shapes = type == 0x3005 ? armor_ : weapon_;
        const unsigned shape = item >> 16, material = item & 0xffffu;
        if (shape >= shapes.size() || material >= materials_.size()) continue;
        const auto& s = shapes[shape];
        const auto& m = materials_[material];
        const auto kind = type == 0x3005 ? armor_kind(s.type) : weapon_kind(s.type_id);
        if (kind.empty()) continue;
        auto first = std::to_string(s.first);
        if (first.size() < 2) first = '0' + first;
        const std::string tail = '.' + m.code + '.' + std::to_string(s.second);
        const std::string suffix = kind + '_' + first + tail;
        if (name.size() < suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        auto found = items_.find(m.type + '/' + m.code);
        if (found == items_.end()) found = items_.find(m.type + "/*");
        result.primary = found != items_.end() ? found->second : fallback_;
        result.regions = region_map(name.substr(0, name.size() - tail.size()), m.type, m.code, s.second, width, height);
        return result;
    }
    return untagged_layer_class(layer, name, result, width, height);
}

// An item layer the user data names no item for (the addon's attachment
// atlases tag the helm and weapon but leave the shield slot empty): its own
// name <model><kind>_<NN>.<code>.<variant> gives the material. One code
// names two types (br: bone and bronze); the items.idb shapes of that
// <kind>_<NN>.<variant> name their material type and decide (sw_13 "ancient
// bronze sword": Metal). Only where the shapes do not decide does the
// family's own texture set, by which type's codes the same family and variant
// exist in (unhumasd_08 is bb/bg/bs/br: bone; unhumasd_10 is am/br/ir/mr/mt: metal).
MaterialLibrary::LayerClass MaterialLibrary::untagged_layer_class(const std::string& layer,
    const std::string& name, LayerClass result, std::uint32_t width, std::uint32_t height) const {
    const auto first_dot = name.find('.');
    const auto second_dot = first_dot == std::string::npos ? std::string::npos : name.find('.', first_dot + 1);
    if (second_dot == std::string::npos || name.find('.', second_dot + 1) != std::string::npos) return result;
    const std::string family = name.substr(0, first_dot), code = name.substr(first_dot + 1, second_dot - first_dot - 1);
    const std::string variant_text = name.substr(second_dot + 1);
    const auto underscore = family.rfind('_');
    if (underscore == std::string::npos || underscore < 2 || underscore + 1 >= family.size() || variant_text.empty() ||
        variant_text.find_first_not_of("0123456789") != std::string::npos ||
        family.find_first_not_of("0123456789", underscore + 1) != std::string::npos) return result;
    const std::string kind = family.substr(underscore - 2, 2);
    static const char* const kinds[] = {"hl","pl","lg","sh","pt","bt","gl","sw","ax","dg","sp","hm","bw","cb","sd","tp","sf","ts","ta","th","tf"};
    if (std::none_of(std::begin(kinds), std::end(kinds), [&](const char* k) { return kind == k; })) return result;
    std::vector<std::string> types;
    for (const auto& m : materials_)
        if (m.code == code && std::find(types.begin(), types.end(), m.type) == types.end()) types.push_back(m.type);
    if (types.empty()) return result;
    std::string type = types.front();
    std::vector<std::string> shape_types;   // the code's types the family's item shapes are made of
    if (types.size() > 1) {
        const auto number = std::uint32_t(std::stoul(family.substr(underscore + 1)));
        const auto variant = std::uint32_t(std::stoul(variant_text));
        for (const auto* shapes : {&weapon_, &armor_})
            for (const auto& s : *shapes)
                if ((shapes == &armor_ ? armor_kind(s.type) : weapon_kind(s.type_id)) == kind && s.first == number &&
                    s.second == variant && std::find(types.begin(), types.end(), s.material) != types.end() &&
                    std::find(shape_types.begin(), shape_types.end(), s.material) == shape_types.end())
                    shape_types.push_back(s.material);
        if (shape_types.size() == 1) type = shape_types.front();
    }
    if (types.size() > 1 && shape_types.size() != 1) {
        // The layer's own file name past the variant (".x2.mmp") names its siblings.
        const std::string original = lower(layer);
        const auto at = original.find(name);
        const std::string suffix = at == std::string::npos ? std::string() : original.substr(at + name.size());
        std::string note;
        std::size_t best = 0;
        bool tie = false;
        if (open_layers(note)) for (const auto& candidate : types) {
            std::size_t count = 0;
            for (const auto& m : materials_) {
                if (m.type != candidate || m.code == code) continue;
                ResourceLocation location;
                ResourceFailure failure;
                if (layers_.locate(family + '.' + m.code + '.' + variant_text + suffix, location, failure)) ++count;
            }
            if (count > best) { best = count;type = candidate;tie = false; }
            else if (count == best && count) tie = true;
        }
        if (!best || tie) {
            char line[200];
            std::snprintf(line, sizeof(line), "layer %s: code %s names several material types and neither the item shapes nor the family's textures decide; skin class kept", layer.c_str(), code.c_str());
            bridge_report_event("materials", line);
            return result;
        }
    }
    auto found = items_.find(type + '/' + code);
    if (found == items_.end()) found = items_.find(type + "/*");
    result.primary = found != items_.end() ? found->second : fallback_;
    result.regions = region_map(family, type, code, std::uint32_t(std::stoul(variant_text)), width, height);
    return result;
}

// Lookup order: <family>.<code>.<variant>@WxH, <family>.<code>.<variant>,
// <family>.<variant>@WxH, <family>.<variant>. A family map's sidecar may
// restrict it to an item type and codes.
std::shared_ptr<const MaterialLibrary::RegionMap> MaterialLibrary::region_map(const std::string& family,
    const std::string& type, const std::string& code, std::uint32_t variant,
    std::uint32_t width, std::uint32_t height) const {
    const std::string size = '@' + std::to_string(width) + 'x' + std::to_string(height);
    const std::string coded = family + '.' + code + '.' + std::to_string(variant);
    const std::string plain = family + '.' + std::to_string(variant);
    for (const auto& key : {coded + size, coded, plain + size, plain})
        if (auto map = read_region_map(key, type, code)) return map;
    // Other humanoid models (unhusk skeletons, unhufe, unor*) wear the same
    // equipment families, often the human art byte for byte: the human
    // <kind>_NN maps apply (the @WxH key and the sidecar checks still decide).
    const auto underscore = family.rfind('_');
    if (underscore != std::string::npos && underscore >= 8 && family.compare(0, 2, "un") == 0 &&
        family.compare(0, 6, "unhuma") != 0)
        return region_map("unhuma" + family.substr(underscore - 2), type, code, variant, width, height);
    return nullptr;
}

std::shared_ptr<const MaterialLibrary::RegionMap> MaterialLibrary::read_region_map(const std::string& key,
    const std::string& type, const std::string& code) const {
    const std::string cache_key = key + '|' + type + '|' + code;
    if (const auto found = region_cache_.find(cache_key); found != region_cache_.end()) return found->second;
    // Decoded maps are kept (a map is at most its art's size, one byte a texel);
    // past 256 the oldest go, so the cache stays bounded.
    if (region_cache_.size() >= 256) region_cache_.erase(region_cache_.begin());
    auto& slot = region_cache_[cache_key];
    slot = read_region_map_uncached(key, type, code);
    return slot;
}

std::shared_ptr<const TextureCpuAsset> MaterialLibrary::cached_layer(const std::string& name) const {
    if (const auto found = layer_cache_.find(name); found != layer_cache_.end()) return found->second;
    ResourceFailure failure;
    ResourceLocation location;
    ResourceBlob blob;
    auto texture = std::make_shared<TextureCpuAsset>();
    if (!layers_.locate(name, location, failure) || !layers_.read(location, blob, failure) ||
        !parse_texture_asset(blob, *texture, failure) || texture->mips.empty()) return nullptr; // the game skips it too
    // The most recent 96 layers (an equipment set's art is a handful).
    if (layer_cache_order_.size() >= 96) {
        layer_cache_.erase(layer_cache_order_.front());
        layer_cache_order_.erase(layer_cache_order_.begin());
    }
    layer_cache_order_.push_back(name);
    std::shared_ptr<const TextureCpuAsset> value = std::move(texture);
    layer_cache_[name] = value;
    return value;
}

std::shared_ptr<const MaterialLibrary::RegionMap> MaterialLibrary::read_region_map_uncached(const std::string& key,
    const std::string& type, const std::string& code) const {
    std::vector<std::uint8_t> bytes;
    if (!read_file(folder_ + L"maps\\" + widen(key) + L".png", bytes, 16u << 20)) return nullptr;
    std::vector<std::uint8_t> sidecar;
    if (read_file(folder_ + L"maps\\" + widen(key) + L".json", sidecar, 1u << 20)) {
        try {
            const std::string document(sidecar.begin(), sidecar.end());
            const auto root = small_json::Reader(document).root();
            if (root.members.count("type") && lower(root.field("type").str()) != type) return nullptr;
            if (root.members.count("codes")) {
                bool listed = false;
                for (const auto& value : root.field("codes").list()) listed |= lower(value.str()) == code;
                if (!listed) return nullptr;
            }
        } catch (...) {
            char line[200];
            std::snprintf(line, sizeof(line), "region map %s: unreadable sidecar; map ignored", key.c_str());
            bridge_report_event("materials", line);
            return nullptr;
        }
    }
    auto map = std::make_shared<RegionMap>();
    if (!decode_index_png(bytes, map->width, map->height, map->classes)) {
        char line[200];
        std::snprintf(line, sizeof(line), "region map %s: not an 8-bit indexed PNG; map ignored", key.c_str());
        bridge_report_event("materials", line);
        return nullptr;
    }
    return map;
}

bool MaterialLibrary::scenery_listed(const std::string& texture) const {
    return scenery_.count(lower(texture)) != 0;
}

bool MaterialLibrary::build_fire_burn_profile(const std::string& texture, std::uint32_t& width,
    std::uint32_t& height, std::vector<std::uint8_t>& labels) const {
    if (const auto map = read_region_map("burn_" + lower(texture), "", "")) {
        if (std::any_of(map->classes.begin(), map->classes.end(), [](auto c) { return c > 3; })) return false;
        width = map->width; height = map->height; labels = map->classes;
        return true;
    }
    if (!scenery_listed(texture)) return false;
    std::vector<std::uint8_t> rgba, relief, classes;
    std::string note;
    if (!build_unit_profile(texture, width, rgba, relief, classes, note)) return false;
    height = width; labels.resize(classes.size());
    bool combustible = false;
    for (std::size_t i = 0; i < classes.size(); ++i) {
        // Use the same reviewed regions as shading: stone and metal in a
        // mixed wood/stone atlas stay inert, independent of material quality.
        labels[i] = classes_[classes[i]].name == "scenery_wood" ? 3 : 0;
        combustible = combustible || labels[i] != 0;
    }
    return combustible;
}

std::array<std::uint32_t, 8> MaterialLibrary::strand_classes() const noexcept {
    std::array<std::uint32_t, 8> bits{};
    for (std::size_t index = 0; index < classes_.size(); ++index)
        if (classes_[index].defined && classes_[index].strands) bits[index / 32] |= 1u << (index % 32);
    return bits;
}

std::array<std::uint32_t, 8> MaterialLibrary::crystal_classes() const noexcept {
    std::array<std::uint32_t, 8> bits{};
    for (std::size_t index = 0; index < classes_.size(); ++index)
        if (classes_[index].defined && classes_[index].crystal) bits[index / 32] |= 1u << (index % 32);
    return bits;
}

std::array<std::uint32_t, 64> MaterialLibrary::coat_classes() const noexcept {
    std::array<std::uint32_t, 64> bytes{};
    for (std::size_t index = 0; index < 256; ++index) {
        const bool defined = index < classes_.size() && classes_[index].defined;
        const float coat = !defined ? 1.f : classes_[index].bare ? 0.f : classes_[index].coat;
        bytes[index / 4] |= std::uint32_t(std::lround(coat * 32.f)) << (8 * (index % 4));
    }
    return bytes;
}

bool MaterialLibrary::metal_mask_texels(const TextureCpuAsset& texture, std::size_t mip, const TextureAlphaPixels& alpha,
                                        const MaterialClass& metal_class, std::vector<std::uint8_t>& off,
                                        std::uint32_t& metal, std::uint32_t& leather, std::uint32_t& cloth) const {
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint8_t> rgb;
    if (!texture_rgb_pixels(texture, mip, w, h, rgb) || w != alpha.width || h != alpha.height) return false;
    const auto& m = metal_mask_;
    const bool tinted = metal_class.tint_max >= metal_class.tint_min;
    off.assign(std::size_t(w) * h, 0);
    const AlphaReader alpha_at(alpha);
    float unit[256];
    for (unsigned v = 0; v < 256; ++v) unit[v] = v / 255.f;
    // Whether a texel's brightest and darkest channel bytes pass the value
    // and saturation tests below (most metal art is grey and stops here).
    std::vector<std::uint8_t> coloured(256 * 256, 0);
    for (unsigned hb = 0; hb < 256; ++hb) for (unsigned lb = 0; lb <= hb; ++lb) {
        const float high = unit[hb], range = high - unit[lb];
        const float saturation = high > 0 ? range / high : 0.f;
        coloured[hb * 256 + lb] = !(high < m.min_value || saturation <= m.saturation);
    }
    for (std::uint32_t y = 0; y < h; ++y) for (std::uint32_t x = 0; x < w; ++x) {
        if (alpha_at(x, y) < .5f) continue;
        ++metal;
        const std::size_t i = std::size_t(y) * w + x;
        const std::uint8_t* p = rgb.data() + i * 3;
        if (!coloured[std::max({p[0], p[1], p[2]}) * 256u + std::min({p[0], p[1], p[2]})]) continue;
        const float r = unit[p[0]], g = unit[p[1]], b = unit[p[2]];
        const float high = std::max({r, g, b}), low = std::min({r, g, b}), range = high - low;
        const float saturation = high > 0 ? range / high : 0.f;
        if (high < m.min_value || saturation <= m.saturation) continue;
        const float hue = 60.f * (high == r ? std::fmod((g - b) / range + 6.f, 6.f) : high == g ? (b - r) / range + 2.f : (r - g) / range + 4.f);
        // A tint may run past 360 to wrap through red ([270, 420]: magenta to orange).
        if (tinted && ((hue >= metal_class.tint_min && hue <= metal_class.tint_max) ||
                       (hue + 360.f >= metal_class.tint_min && hue + 360.f <= metal_class.tint_max))) continue;
        if (hue >= m.brown_hue_min && hue <= m.brown_hue_max) { off[i] = m.leather; ++leather; }
        else if (saturation >= m.cloth_saturation) { off[i] = m.cloth; ++cloth; }
    }
    return true;
}

bool MaterialLibrary::build_profile(const MaterialComposition& composition, std::uint32_t side, bool metal_mask,
                                    std::vector<std::uint8_t>& rgba, std::vector<std::uint8_t>& relief,
                                    std::vector<std::uint8_t>& classes, std::string& note) const {
    const std::uint32_t dimension = composition.dimension;
    if (!side || !dimension || composition.layers.empty()) return false;
    const auto encode = [](const MaterialClass& value) {
        return std::array<float, 5>{value.roughness, value.metal, std::clamp(value.f0 * 4.f, 0.f, 1.f), value.weave_or_sheen(), value.relief};
    };
    const auto skin = encode(classes_[skin_]);
    std::array<std::array<float, 5>, 256> encoded;  // every class' surface values
    for (std::size_t c = 0; c < classes_.size(); ++c) encoded[c] = encode(classes_[c]);
    std::vector<float> profile(std::size_t(side) * side * 5);
    for (std::size_t at = 0; at < profile.size(); at += 5) std::copy(skin.begin(), skin.end(), profile.begin() + at);
    classes.assign(std::size_t(side) * side, skin_);
    if (!open_layers(note)) return false;
    const bool packed = composition.slots[0] >= 0 || composition.slots[1] >= 0 ||
                        composition.slots[2] >= 0 || composition.slots[3] >= 0;

    // The usable layers in composition order: placement, mip, alpha, class,
    // metal mask, and the lookup tables their sampling reads.
    struct LayerWork {
        std::shared_ptr<const TextureCpuAsset> texture;
        const std::string* name = nullptr;
        std::uint32_t x = 0, y = 0, width = 0, height = 0;
        std::size_t mip = 0;
        TextureAlphaPixels alpha;
        LayerClass layer;
        std::array<float, 5> primary{};
        bool masked = false, mask_ok = false;
        std::vector<std::uint8_t> off;
        std::uint32_t metal = 0, leather = 0, cloth = 0;
        double mip_scale = 1;
        unsigned block = 1;
        // Per region-map value: the surface it gives a texel (null: the
        // layer's class, or the metal mask's), the class it gives the block
        // centre (0: none), and its class for the strand test.
        std::array<std::array<float, 5>, 256> inherited;
        const float* region_value[256];
        std::uint8_t region_centre[256], region_bare[256];
        // The region-map texel of each layer column and row (the map may be
        // smaller or larger than the mip); each profile column's first layer
        // column (-1: outside the layer).
        std::vector<std::uint32_t> map_column;
        std::vector<std::size_t> map_row;
        std::vector<int> column_start;
    };
    std::vector<LayerWork> work;
    work.reserve(composition.layers.size());
    const double source_per_profile = double(dimension) / side;
    std::uint32_t x = 0, y = 0, classified = 0;
    for (std::size_t index = 0; index < composition.layers.size(); ++index) {
        const auto& name = composition.layers[index];
        auto layer_texture = cached_layer(name);
        if (!layer_texture) continue; // the game skips it too
        const TextureCpuAsset& texture = *layer_texture;
        // Packed attachment atlases (addon PutLayersWeapons): each listed layer
        // fills its 512 quadrant; an unlisted one keeps the previous placement.
        if (packed) {
            if (composition.slots[0] == int(index)) x = 0, y = 512;
            else if (composition.slots[1] == int(index)) x = 512, y = 512;
            else if (composition.slots[2] == int(index)) x = 0, y = 0;
            else if (composition.slots[3] == int(index)) x = 512, y = 0;
        }
        const std::uint32_t width = texture.width, height = texture.height;
        if (x > dimension || width > dimension - x || y > dimension || height > dimension - y) continue;
        // The mip whose texel matches one profile texel (area sampling from the mips).
        std::size_t mip = 0;
        while (mip + 1 < texture.mips.size() && double(1u << (mip + 1)) <= source_per_profile) ++mip;
        TextureAlphaPixels alpha;
        if (!texture_alpha_pixels(texture, mip, alpha) || !alpha.width || !alpha.height) continue;
        const auto layer = layer_class(name, composition.user_data, width, height);
        // Diagnostic: a figure layer with no region map (once per name): which
        // model, item kind and code the resolver saw, so unmapped equipment shows.
        if (!layer.regions && lower(name).find("skin_") == std::string::npos && unmapped_layers_.insert(lower(name)).second) {
            char line[240];
            std::snprintf(line, sizeof(line), "layer %s: no region map (%ux%u, class %s)", name.c_str(), width, height,
                          classes_[layer.primary].name.c_str());
            bridge_report_event("materials", line);
        }
        if (layer.primary != skin_) ++classified;
        work.emplace_back();
        auto& w = work.back();
        w.texture = std::move(layer_texture);w.name = &name;
        w.x = x;w.y = y;w.width = width;w.height = height;w.mip = mip;w.alpha = alpha;w.layer = layer;
        w.primary = encode(classes_[layer.primary]);
        // Metal masks: a metal layer's texels its reviewed region map (if
        // any) leaves at the item class; the map's own classes win.
        w.masked = metal_mask && metal_mask_.defined && classes_[layer.primary].metal >= .5f;
        w.mip_scale = double(alpha.width) / width;            // mip texels per source texel
        w.block = std::max(1u, unsigned(std::lround(source_per_profile * w.mip_scale)));
    }
    if (!classified) { note = "no item layer"; return false; }

    // The metal layers' masks, one thread each (each reads only its layer).
    std::vector<LayerWork*> metal_layers;
    for (auto& w : work) if (w.masked) metal_layers.push_back(&w);
    run_parallel(unsigned(metal_layers.size()), profile_threads(), [&](unsigned at) {
        auto& w = *metal_layers[at];
        w.mask_ok = metal_mask_texels(*w.texture, w.mip, w.alpha, classes_[w.layer.primary], w.off, w.metal, w.leather, w.cloth);
    });
    std::uint32_t mask_layers = 0, mask_metal = 0, mask_leather = 0, mask_cloth = 0;
    std::string mask_detail;  // per metal layer: name, region map, reference, counts
    for (const auto* w : metal_layers) {
        const auto& metal_class = classes_[w->layer.primary];
        char line[200];
        if (w->mask_ok) {
            ++mask_layers;mask_metal += w->metal;mask_leather += w->leather;mask_cloth += w->cloth;
            char tint[32] = "grey";
            if (metal_class.tint_max >= metal_class.tint_min)
                std::snprintf(tint, sizeof(tint), "tint h%.0f-%.0f", metal_class.tint_min, metal_class.tint_max);
            std::snprintf(line, sizeof(line), "; %s%s %s %s leather %u cloth %u of %u", w->name->c_str(),
                          w->layer.regions ? " (map)" : "", metal_class.name.c_str(), tint, w->leather, w->cloth, w->metal);
        } else std::snprintf(line, sizeof(line), "; %s colour unreadable", w->name->c_str());
        mask_detail += line;
    }

    for (auto& w : work) {
        for (unsigned r = 0; r < 256; ++r) {
            const auto& c = classes_[r];
            w.region_value[r] = nullptr;w.region_centre[r] = 0;w.region_bare[r] = w.layer.primary;
            if (!r || !c.defined) continue;
            if (c.inherit_primary) {
                auto& value = w.inherited[r];
                value = w.primary;
                value[0] = std::max(value[0], c.roughness_min), value[4] = std::max(value[4], c.relief_min);
                w.region_value[r] = value.data();
            } else {
                w.region_value[r] = encoded[r].data();w.region_centre[r] = std::uint8_t(r);w.region_bare[r] = std::uint8_t(r);
            }
        }
        if (const RegionMap* map = w.layer.regions.get()) {
            w.map_column.resize(w.alpha.width);w.map_row.resize(w.alpha.height);
            for (std::uint32_t tx = 0; tx < w.alpha.width; ++tx)
                w.map_column[tx] = std::min(map->width - 1, std::uint32_t((tx + .5) / w.alpha.width * map->width));
            for (std::uint32_t ty = 0; ty < w.alpha.height; ++ty)
                w.map_row[ty] = std::size_t(std::min(map->height - 1, std::uint32_t((ty + .5) / w.alpha.height * map->height))) * map->width;
        }
        w.column_start.resize(side);
        for (std::uint32_t px = 0; px < side; ++px) {
            const double sx = (px + .5) * source_per_profile - w.x - source_per_profile * .5;
            w.column_start[px] = sx < -.5 || sx >= w.width - .5 ? -1 : int(sx * w.mip_scale);
        }
    }

    // Area sampling. Profile rows are independent: each thread takes every
    // n-th row and applies the layers to it in composition order. A row's
    // texels are summed side by side, block row by block row; each texel
    // keeps its own order (rows, then columns of its block).
    const unsigned threads = std::max(1u, std::min(profile_threads(), side / 16));
    std::vector<std::vector<float>> thread_sums(threads, std::vector<float>(std::size_t(side) * 6));
    std::vector<std::vector<std::uint8_t>> thread_centre(threads, std::vector<std::uint8_t>(side)),
                                           thread_bare(threads, std::vector<std::uint8_t>(side));
    run_parallel(threads, threads, [&](unsigned thread) {
        auto& sums = thread_sums[thread];
        auto& row_centre = thread_centre[thread];
        auto& row_bare = thread_bare[thread];
        for (std::uint32_t py = thread; py < side; py += threads) for (const auto& w : work) {
            // This profile row's source rows within the layer.
            const double sy = (py + .5) * source_per_profile - w.y - source_per_profile * .5;
            if (sy < -.5 || sy >= w.height - .5) continue;
            const int my = int(sy * w.mip_scale);
            const AlphaReader alpha_at(w.alpha);
            const unsigned block = w.block, centre = block / 2;
            const RegionMap* map = w.layer.regions.get();
            const std::uint8_t primary_class = w.layer.primary;
            // Per texel: coverage and the five surface sums; the class at the
            // block's centre; and any opaque texel of the block in a class
            // without strands (bone, eyes, cloth beside a pelt): the fur pass
            // reads one class texel per 4-8 layer texels, so a centre-only
            // class let strands grow over thin bare art (fangs, muzzle edge).
            std::fill(sums.begin(), sums.end(), 0.f);
            std::fill(row_centre.begin(), row_centre.end(), primary_class);
            std::fill(row_bare.begin(), row_bare.end(), std::uint8_t(0));
            for (unsigned j = 0; j < block; ++j) {
                const std::uint32_t ty = std::uint32_t(std::clamp(my + int(j), 0, int(w.alpha.height) - 1));
                const std::uint8_t* region_line = map ? map->classes.data() + w.map_row[ty] : nullptr;
                const std::uint8_t* off_line = w.off.empty() ? nullptr : w.off.data() + std::size_t(ty) * w.alpha.width;
                for (std::uint32_t px = 0; px < side; ++px) {
                    const int mx = w.column_start[px];
                    if (mx < 0) continue;
                    float* stored = sums.data() + std::size_t(px) * 6;
                    float sum[6] = {stored[0], stored[1], stored[2], stored[3], stored[4], stored[5]};
                    std::uint8_t texel_class = row_centre[px], bare_class = row_bare[px];
                    for (unsigned k = 0; k < block; ++k) {
                        const std::uint32_t tx = std::uint32_t(std::clamp(mx + int(k), 0, int(w.alpha.width) - 1));
                        const float opacity = alpha_at(tx, ty);
                        const bool at_centre = j == centre && k == centre;
                        // A clear texel adds nothing (+0) to the sums; only the
                        // block centre's class still counts.
                        if (opacity == 0.f && !at_centre) continue;
                        const float* value = w.primary.data();
                        std::uint8_t region = 0;
                        if (region_line && (region = region_line[w.map_column[tx]], w.region_value[region])) {
                            value = w.region_value[region];
                            if (at_centre && w.region_centre[region]) texel_class = w.region_centre[region];
                        } else if (off_line) {
                            if (const std::uint8_t c = off_line[tx]) {
                                value = encoded[c].data();
                                if (at_centre) texel_class = c;
                            }
                        }
                        sum[0] += opacity;
                        for (unsigned c = 0; c < 5; ++c) sum[1 + c] += value[c] * opacity;
                        if (opacity >= .5f && !bare_class) {
                            const std::uint8_t sample_class = region_line ? w.region_bare[region] : primary_class;
                            if (!classes_[sample_class].strands) bare_class = sample_class;
                        }
                    }
                    for (unsigned c = 0; c < 6; ++c) stored[c] = sum[c];
                    row_centre[px] = texel_class;row_bare[px] = bare_class;
                }
            }
            const float samples = float(block * block);
            for (std::uint32_t px = 0; px < side; ++px) {
                if (w.column_start[px] < 0) continue;
                const float* sum = sums.data() + std::size_t(px) * 6;
                std::uint8_t texel_class = row_centre[px];
                if (row_bare[px] && classes_[texel_class].strands) texel_class = row_bare[px];
                const float coverage = sum[0] / samples;
                float* target = profile.data() + (std::size_t(py) * side + px) * 5;
                for (unsigned c = 0; c < 5; ++c) {
                    const float previous = packed ? skin[c] : target[c];
                    target[c] = previous * (1 - coverage) + sum[1 + c] / samples;
                }
                auto& texel = classes[std::size_t(py) * side + px];
                if (coverage >= .5f) texel = texel_class;
                else if (packed) texel = skin_;
            }
        }
    });
    if (!mask_detail.empty()) {
        char line[160];
        std::snprintf(line, sizeof(line), "metal mask: %u layer(s), %u of %u metal texels reclassified (leather %u, cloth %u)",
                      mask_layers, mask_leather + mask_cloth, mask_metal, mask_leather, mask_cloth);
        note = line + mask_detail;
    }
    rgba.resize(std::size_t(side) * side * 4);relief.resize(std::size_t(side) * side);
    for (std::size_t i = 0; i < relief.size(); ++i) {
        for (unsigned c = 0; c < 4; ++c) rgba[i * 4 + c] = unorm8(profile[i * 5 + c]);
        relief[i] = unorm8(profile[i * 5 + 4] * .5f); // stored halved: class relief runs 0..2
    }
    return true;
}

bool MaterialLibrary::build_unit_profile(const std::string& texture, std::uint32_t& side,
    std::vector<std::uint8_t>& rgba, std::vector<std::uint8_t>& relief, std::vector<std::uint8_t>& classes,
    std::string& note) const {
    const auto found = units_.find(lower(texture));
    if (found == units_.end()) { note = "not listed in units.json or scenery.json"; return false; }
    const auto& unit = found->second;
    // A reviewed region map maps\<texture>.png (labels; 0 = the unit's class)
    // separates painted regions (a dragon's scales, horns, wing membranes, eyes).
    const auto map = read_region_map(lower(texture), std::string(), std::string());
    side = map ? 256u : unit.parts.empty() ? 4u : 128u;
    classes.assign(std::size_t(side) * side, unit.primary);
    if (map) for (std::uint32_t y = 0; y < side; ++y) for (std::uint32_t x = 0; x < side; ++x) {
        const std::uint8_t region = map->classes[std::size_t(std::min(map->height - 1, y * map->height / side)) * map->width +
                                                 std::min(map->width - 1, x * map->width / side)];
        if (region && classes_[region].defined) classes[std::size_t(y) * side + x] = region;
    }
    // Listed parts: their triangles' UV footprint, read from the model's own FIGs.
    if (!unit.parts.empty()) {
        ResourceFailure failure;
        ResourceBlob model_blob;
        MemoryResourceArchive model;
        if (!open_layers(note) || !layers_.resolve(unit.model + ".mod", model_blob, failure) || !model.open(model_blob, failure)) {
            bridge_report_event("materials", ("unit profile " + texture + ": " + unit.model + ".mod unreadable; parts keep the texture's class").c_str());
        } else for (const auto& part : unit.parts) {
            ResourceBlob blob;
            FigureCpuAsset figure;
            if (!model.resolve(part.first, blob, failure) || !parse_figure_asset(blob, figure, failure)) {
                bridge_report_event("materials", ("unit profile " + texture + ": part " + part.first + " unreadable in " + unit.model + ".mod").c_str());
                continue;
            }
            // The table names primary textures: a part drawn with the secondary one is not on it.
            if (!figure_primary_texture(figure.texture_number)) continue;
            for (std::size_t i = 0; i + 2 < figure.indices.size(); i += 3) {
                float u[3], v[3];
                bool valid = true;
                for (int k = 0; k < 3 && valid; ++k) {
                    const auto c = figure.indices[i + k];
                    valid = c < figure.components.size() && figure.components[c].texture < figure.texture_coordinates.size();
                    if (!valid) break;
                    const auto& uv = figure.texture_coordinates[figure.components[c].texture];
                    u[k] = uv.u * float(side); v[k] = uv.v * float(side);
                }
                if (!valid) continue;
                const float area = (u[1] - u[0]) * (v[2] - v[0]) - (u[2] - u[0]) * (v[1] - v[0]);
                if (!std::isfinite(area) || std::abs(area) < 1e-6f) continue;
                const int x0 = std::max(0, int(std::floor(std::min({u[0], u[1], u[2]}))));
                const int x1 = std::min(int(side) - 1, int(std::ceil(std::max({u[0], u[1], u[2]}))));
                const int y0 = std::max(0, int(std::floor(std::min({v[0], v[1], v[2]}))));
                const int y1 = std::min(int(side) - 1, int(std::ceil(std::max({v[0], v[1], v[2]}))));
                for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
                    const float px = x + .5f, py = y + .5f;
                    const float w0 = ((u[1] - px) * (v[2] - py) - (u[2] - px) * (v[1] - py)) / area;
                    const float w1 = ((u[2] - px) * (v[0] - py) - (u[0] - px) * (v[2] - py)) / area;
                    if (w0 >= -.02f && w1 >= -.02f && 1 - w0 - w1 >= -.02f) classes[std::size_t(y) * side + x] = part.second;
                }
            }
        }
    }
    rgba.resize(classes.size() * 4);relief.resize(classes.size());
    for (std::size_t i = 0; i < classes.size(); ++i) {
        const auto& c = classes_[classes[i]];
        const float value[5] = {c.roughness, c.metal, std::clamp(c.f0 * 4.f, 0.f, 1.f), c.weave_or_sheen(), c.relief};
        for (int k = 0; k < 4; ++k) rgba[i * 4 + k] = std::uint8_t(std::lround(std::clamp(value[k], 0.f, 1.f) * 255.f));
        relief[i] = std::uint8_t(std::lround(std::clamp(value[4] * .5f, 0.f, 1.f) * 255.f)); // halved, as above
    }
    return true;
}

// ---- Renderer: per-texture profile resources ----------------------------

int Renderer::material_level() const noexcept {
    return std::clamp(configured_integer(debug_overlay_.config(), "material_quality", 2), 0, 3);
}

int Renderer::fur_mode() const noexcept {
    return material_level() <= 0 ? 0 : configured_integer(debug_overlay_.config(), "fur_mode", 0);
}

// A figure texture value registered for colour with its game texture name.
void Renderer::note_material_texture(const std::string& name, std::uint64_t backing, bool unit, bool surface) noexcept try {
    if (name.empty() || !backing) return;
    const auto [found, added] = material_profiles_.try_emplace(backing);
    if (added) ++material_instances_revision_;  // records of this texture become profiled instances
    auto& entry = found->second;
    entry.surface = entry.surface || surface;
    if (unit && !entry.unit) { entry.unit = true;material_profiles_dirty_ = true; }
    if (entry.name == name) return;
    entry.name = name;entry.serial = 0;entry.pending = {};entry.ticket.cancel();entry.srv.Reset();entry.relief_srv.Reset();entry.class_srv.Reset();entry.profile12.reset();entry.class12.reset();entry.burn12.reset();entry.burn_tried=false;++material_profiles_revision_;entry.strands = false;entry.coat = false;entry.failed = false;
    material_profiles_dirty_ = true;
} catch (...) {}

// Before a colour flush: (re)build the profiles whose composition changed,
// once per change (a new texture, a new layer list, the level turned on).
namespace {
// One background thread builds composed atlas profiles, newest first (the
// library's caches and layer store are not shared between builds). A
// promise's future never blocks the render thread on destruction; a task
// whose entry was superseded or erased before it starts is skipped.
class MaterialProfileWorker {
public:
    static MaterialProfileWorker& instance() { static MaterialProfileWorker worker; return worker; }
    bool post(std::function<void()> task) {
        try {
            std::lock_guard<std::mutex> guard(lock_);
            if (!started_) { std::thread(&MaterialProfileWorker::run, this).detach(); started_ = true; }
            queue_.push_back(std::move(task));
        } catch (...) { return false; }
        wake_.notify_one();
        return true;
    }
    std::mutex& library_lock() { return library_lock_; }
private:
    void run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> guard(lock_);
                wake_.wait(guard, [this] { return !queue_.empty(); });
                // Newest first: the look on screen now (the equip just made)
                // is built next; earlier atlases still drain when it is idle.
                task = std::move(queue_.back());
                queue_.pop_back();
            }
            try { task(); } catch (...) {}
        }
    }
    std::mutex lock_, library_lock_;
    std::condition_variable wake_;
    std::deque<std::function<void()>> queue_;
    bool started_ = false;
};
}  // namespace

void Renderer::prepare_fire_burn_materials() noexcept try {
    if (!d3d12_ || !fire_burn_bound_ || !material_library_) return;
    // The lookup describes renderer-owned textures, not engine objects or
    // draw history. Backing replacement/retirement owns map lifetime too.
    std::vector<std::uint32_t> table(1024, 0);
    const auto add = [&](std::uint64_t backing, std::uint32_t descriptor) {
        const auto found = material_profiles_.find(backing);
        if (found == material_profiles_.end() || found->second.unit || descriptor == UINT_MAX) return;
        auto& entry = found->second;
        if (!entry.burn_tried) {
            entry.burn_tried = true;
            std::uint32_t width = 0, height = 0;
            std::vector<std::uint8_t> labels;
            bool classified = false;
            {
                std::lock_guard<std::mutex> guard(MaterialProfileWorker::instance().library_lock());
                classified = material_library_->build_fire_burn_profile(entry.name, width, height, labels);
            }
            if (classified && (FAILED(d3d12_->create_texture(width, height, DXGI_FORMAT_R8_UINT, entry.burn12)) ||
                FAILED(d3d12_->write_texture(*entry.burn12, RECT{0,0,LONG(width),LONG(height)}, labels.data(), width)))) {
                entry.burn12.reset();
                bridge_report_event("fire_burn_material_unavailable", entry.name.c_str());
            }
        }
        if (!entry.burn12) return;
        const auto index = d3d12_->bindless_index(*entry.burn12);
        if (index == UINT_MAX) return;
        if (table.size() <= descriptor) table.resize((std::size_t(descriptor) / 1024 + 1) * 1024, 0);
        table[descriptor] = index;
    };
    for (const auto& item : native_owned_textures_)
        if (item.second.d3d12) add(item.second.backing, d3d12_->bindless_index(*item.second.d3d12));
    // Surface-backed retained statics can own their texture directly.
    for (const auto& record : native_instance_records_)
        if (record.occupied && record.command.texture12) {
            const auto found = material_profiles_.find(record.command.material_backing);
            if (found != material_profiles_.end() && found->second.surface)
                add(record.command.material_backing, record.command.instance.padding[0]);
        }
    if (table == fire_material_table_ && fire_material_table12_) return;
    D3D12TextureRef texture;
    const UINT height = UINT(table.size() / 1024);
    if (FAILED(d3d12_->create_texture(1024, height, DXGI_FORMAT_R32_UINT, texture)) ||
        FAILED(d3d12_->write_texture(*texture, RECT{0,0,1024,LONG(height)}, table.data(), 1024 * sizeof(std::uint32_t)))) {
        fire_material_table12_.reset();
        bridge_report_event("fire_burn_material_unavailable", "descriptor table");
        return;
    }
    fire_material_table12_ = std::move(texture); fire_material_table_ = std::move(table);
} catch (...) { fire_material_table12_.reset(); }

void Renderer::prepare_material_profiles() noexcept try {
    const int level = material_level();
    if (level != material_prepared_level_) { material_prepared_level_ = level;material_profiles_dirty_ = true;++material_profiles_revision_; }
    // F10 material_metal_mask: a change rebuilds every composed profile.
    const bool metal_mask = configured_boolean(debug_overlay_.config(), "material_metal_mask", true);
    if (metal_mask != material_prepared_metal_mask_) {
        material_prepared_metal_mask_ = metal_mask;material_profiles_dirty_ = true;
        for (auto& item : material_profiles_) if (item.second.serial != UINT64_MAX) item.second.serial = 0;
    }
    // F10 material_world or material_scenery off: scenery profiles stop
    // binding (D3D12 indices refresh); material_scenery alone is the A/B.
    const bool world = configured_boolean(debug_overlay_.config(), "material_world", true) &&
                       configured_boolean(debug_overlay_.config(), "material_scenery", true);
    if (world != material_world_on_) { material_world_on_ = world;++material_profiles_revision_; }
    const bool burning = d3d12_ && fire_burn_bound_;
    if ((!level && !burning) || material_profiles_.empty()) return;
    const std::uint64_t revision = retained_visual_scene().material_composition_revision();
    if (!material_profiles_dirty_ && revision == material_checked_revision_) {
        if (burning) prepare_fire_burn_materials();
        return;
    }
    material_profiles_dirty_ = false;material_checked_revision_ = revision;
    if (!material_library_tried_) {
        auto library = std::make_shared<MaterialLibrary>();
        std::string error;
        // The host adopts the game's mounts after it starts: wait for them.
        ResourceStoreConfig mounts;
        if (!owned_data_resource_mounts(mounts)) { material_profiles_dirty_ = true; return; }
        material_library_tried_ = true;
        if (library->load(error)) material_library_ = std::move(library);
        else {
            material_notice_ = "Material shading off: " + error;
            bridge_report_event("materials", material_notice_.c_str());
        }
    }
    if (!material_library_) return;
    if (burning) prepare_fire_burn_materials();
    if (!level) return;
    // One build at a time: the library's layer store and caches are shared.
    std::mutex& build_lock = MaterialProfileWorker::instance().library_lock();
    for (auto& item : material_profiles_) {
        auto& entry = item.second;
        MaterialComposition composition;
        std::uint32_t side = 0;
        std::vector<std::uint8_t> pixels, relief, classes;
        std::string note;
        const bool composed = entry.serial != UINT64_MAX &&
                              retained_visual_scene().material_composition(entry.name, composition);
        if (entry.pending.valid() && composed && composition.serial != entry.serial) {
            // Latest wins: a newer layer list replaces the build in flight.
            // A queued task is skipped; a running one finishes unread.
            entry.pending = {};entry.ticket.cancel();
        }
        if (entry.pending.valid()) {
            // A background build: upload it once ready; keep checking until then.
            if (entry.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                material_profiles_dirty_ = true;
                continue;
            }
            auto built = entry.pending.get();
            material_profiles_dirty_ = true;  // a newer layer list may be waiting
            {
                // Build times: a running summary every 50 builds.
                static unsigned builds = 0;
                static double total_ms = 0, max_ms = 0;
                ++builds;total_ms += built.build_ms;max_ms = std::max(max_ms, built.build_ms);
                if (builds % 50 == 0) {
                    char line[160];
                    std::snprintf(line, sizeof(line), "build %u: %.1f ms (mean %.1f ms, max %.1f ms over %u builds)", builds,
                                  built.build_ms, total_ms / builds, max_ms, builds);
                    bridge_report_event("material_profile_time", line);
                }
            }
            if (built.serial != entry.serial) continue;
            if (!built.ok) { entry.failed = true;continue; }
            side = built.side;pixels = std::move(built.pixels);relief = std::move(built.relief);classes = std::move(built.classes);
            if (!built.note.empty()) bridge_report_event("materials", ("atlas " + entry.name + ": " + built.note).c_str());
        } else if (composed) {
            // A composed atlas: from its layer list, once per layer list.
            if (entry.serial == composition.serial) continue;
            entry.serial = composition.serial;entry.srv.Reset();entry.relief_srv.Reset();entry.class_srv.Reset();entry.profile12.reset();entry.class12.reset();entry.burn12.reset();entry.burn_tried=false;++material_profiles_revision_;entry.strands = false;entry.coat = false;entry.failed = false;
            // Packed attachment atlases (weapons, shields: one per armed
            // unit) at up to 512: their 128-texel orc and redress art put a
            // mace haft ~2.5 texels wide at 256, and the head's gloss
            // filtered over the wooden haft (user: plasticky handle).
            const bool packed = composition.slots[0] >= 0 || composition.slots[1] >= 0 ||
                                composition.slots[2] >= 0 || composition.slots[3] >= 0;
            side = std::min<std::uint32_t>(composition.dimension, packed ? 512u : 256u);
            // Until it is uploaded the atlas draws without a profile, as
            // between the reset above and the upload before.
            std::shared_ptr<const MaterialLibrary> library = material_library_;
            auto promise = std::make_shared<std::promise<MaterialProfileGpu::Built>>();
            entry.pending = promise->get_future();
            entry.ticket.serial = std::make_shared<std::atomic<std::uint64_t>>(composition.serial);
            const bool posted = MaterialProfileWorker::instance().post(
                [library, promise, ticket = entry.ticket.serial, composition = std::move(composition), side, metal_mask, &build_lock]() {
                    MaterialProfileGpu::Built built;
                    built.serial = composition.serial;built.side = side;
                    try {
                        std::lock_guard<std::mutex> guard(build_lock);
                        // Superseded or erased before it started: nobody reads it.
                        if (ticket->load() != composition.serial) {
                            promise->set_value(std::move(built));
                            return;
                        }
                        const auto start = std::chrono::steady_clock::now();
                        built.ok = library->build_profile(composition, side, metal_mask, built.pixels, built.relief,
                                                          built.classes, built.note);
                        built.build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    } catch (...) { built.ok = false; }
                    promise->set_value(std::move(built));
                });
            if (!posted) { entry.pending = {};entry.failed = true; }
            material_profiles_dirty_ = true;
            continue;
        } else {
            // A unit's fixed model texture (units.json) or a listed scenery
            // texture (scenery.json): from the table, once. Other world
            // object textures take the S2 path and no profile.
            constexpr std::uint64_t kUnitTable = UINT64_MAX;  // never a composition serial
            if (entry.serial == kUnitTable || !(entry.unit || material_library_->scenery_listed(entry.name))) continue;
            entry.serial = kUnitTable;entry.scenery = !entry.unit;entry.srv.Reset();entry.relief_srv.Reset();entry.class_srv.Reset();entry.profile12.reset();entry.class12.reset();entry.burn12.reset();entry.burn_tried=false;++material_profiles_revision_;entry.strands = false;entry.coat = false;entry.failed = false;
            bool unit_built = false;
            {
                std::lock_guard<std::mutex> guard(build_lock);
                unit_built = material_library_->build_unit_profile(entry.name, side, pixels, relief, classes, note);
            }
            if (!unit_built) {
                entry.failed = true;
                bridge_report_event("materials", ("unit texture " + entry.name + ": " + note +
                    "; drawn without a material profile (magenta in material_view 2)").c_str());
                continue;
            }
        }
        if (d3d12_) {
            if (FAILED(upload_material_profile_d3d12(entry, side, pixels, relief))) { entry.failed = true;continue; }
            // The fur pass's class texture: an atlas with any strand texel
            // (fur armour) or any bare texel (no creature coat there).
            const auto strand_bits = material_library_->strand_classes();
            const auto coat_bytes = material_library_->coat_classes();
            // A crystal class anywhere: the record takes a crystal instance (native_material_instance).
            {const auto crystal_bits = material_library_->crystal_classes();bool crystal = false;
             for (const auto value : classes) crystal = crystal || ((crystal_bits[value / 32] >> (value % 32)) & 1u) != 0;
             if (crystal != entry.crystal) { entry.crystal = crystal;++material_instances_revision_; }}
            entry.strands = entry.coat = false;
            for (const auto value : classes) {
                entry.strands = entry.strands || ((strand_bits[value / 32] >> (value % 32)) & 1u) != 0;
                entry.coat = entry.coat || ((coat_bytes[value / 4] >> (8 * (value % 4))) & 255u) != 32u;
            }
            if (entry.strands || entry.coat) {
                D3D12TextureRef class_texture;
                const RECT rect{0, 0, LONG(side), LONG(side)};
                if (SUCCEEDED(d3d12_->create_texture(side, side, DXGI_FORMAT_R8_UINT, class_texture)) &&
                    SUCCEEDED(d3d12_->write_texture(*class_texture, rect, classes.data(), side))) {
                    entry.class12 = std::move(class_texture);profiler_.add_upload(classes.size());
                } else entry.strands = entry.coat = false;
            }
            ++material_profiles_built_;++material_profiles_revision_;
            continue;
        }
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = side;desc.MipLevels = 0;desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count = 1;desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture)) ||
            FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, entry.srv.ReleaseAndGetAddressOf()))) {
            entry.srv.Reset();entry.failed = true;continue;
        }
        context_->UpdateSubresource(texture.Get(), 0, nullptr, pixels.data(), side * 4, 0);
        context_->GenerateMips(entry.srv.Get());
        profiler_.add_upload(pixels.size());
        // The relief strength beside it (R8, mipped like the profile; t38).
        desc.Format = DXGI_FORMAT_R8_UNORM;
        ComPtr<ID3D11Texture2D> relief_texture;
        if (FAILED(device_->CreateTexture2D(&desc, nullptr, &relief_texture)) ||
            FAILED(device_->CreateShaderResourceView(relief_texture.Get(), nullptr, entry.relief_srv.ReleaseAndGetAddressOf()))) {
            entry.srv.Reset();entry.relief_srv.Reset();entry.failed = true;continue;
        }
        context_->UpdateSubresource(relief_texture.Get(), 0, nullptr, relief.data(), side, 0);
        context_->GenerateMips(entry.relief_srv.Get());
        profiler_.add_upload(relief.size());
        // The class texture beside it (point-read, no mips) and whether any
        // texel grows strands (the fur pass skips atlases without any) or is
        // bare (no creature coat there).
        const auto strand_bits = material_library_->strand_classes();
        const auto coat_bytes = material_library_->coat_classes();
        // A crystal class anywhere: the record takes a crystal instance (native_material_instance).
        {const auto crystal_bits = material_library_->crystal_classes();bool crystal = false;
         for (const auto value : classes) crystal = crystal || ((crystal_bits[value / 32] >> (value % 32)) & 1u) != 0;
         if (crystal != entry.crystal) { entry.crystal = crystal;++material_instances_revision_; }}
        entry.strands = entry.coat = false;
        for (const auto value : classes) {
            entry.strands = entry.strands || ((strand_bits[value / 32] >> (value % 32)) & 1u) != 0;
            entry.coat = entry.coat || ((coat_bytes[value / 4] >> (8 * (value % 4))) & 255u) != 32u;
        }
        D3D11_TEXTURE2D_DESC class_desc{};
        class_desc.Width = class_desc.Height = side;class_desc.MipLevels = 1;class_desc.ArraySize = 1;
        class_desc.Format = DXGI_FORMAT_R8_UINT;class_desc.SampleDesc.Count = 1;class_desc.Usage = D3D11_USAGE_IMMUTABLE;
        class_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA class_data{classes.data(), side, 0};
        ComPtr<ID3D11Texture2D> class_texture;
        if (FAILED(device_->CreateTexture2D(&class_desc, &class_data, &class_texture)) ||
            FAILED(device_->CreateShaderResourceView(class_texture.Get(), nullptr, entry.class_srv.ReleaseAndGetAddressOf()))) {
            entry.class_srv.Reset();entry.strands = entry.coat = false;
        } else profiler_.add_upload(classes.size());
        ++material_profiles_built_;
    }
} catch (...) {}

ID3D11ShaderResourceView* Renderer::material_profile_srv(std::uint64_t backing) noexcept {
    if (material_profiles_.empty()) return nullptr;
    const auto found = material_profiles_.find(backing);
    if (found == material_profiles_.end() || (found->second.scenery && !material_world_on_)) return nullptr;
    return found->second.srv.Get();
}

ID3D11ShaderResourceView* Renderer::material_relief_srv(std::uint64_t backing) noexcept {
    const auto found = material_profiles_.find(backing);
    return found == material_profiles_.end() || (found->second.scenery && !material_world_on_) ? nullptr : found->second.relief_srv.Get();
}

ID3D11ShaderResourceView* Renderer::material_class_srv(std::uint64_t backing, bool& strands, bool& coat) noexcept {
    strands = coat = false;
    const auto found = material_profiles_.find(backing);
    if (found == material_profiles_.end() || !found->second.class_srv) return nullptr;
    strands = found->second.strands; coat = found->second.coat;
    return found->second.class_srv.Get();
}

const D3D12Texture* Renderer::material_class_d3d12(std::uint64_t backing, bool& strands, bool& coat) noexcept {
    strands = coat = false;
    const auto found = material_profiles_.find(backing);
    if (found == material_profiles_.end() || !found->second.class12) return nullptr;
    strands = found->second.strands; coat = found->second.coat;
    return found->second.class12.get();
}

std::array<std::uint32_t, 8> Renderer::material_strand_classes() const noexcept {
    return material_library_ ? material_library_->strand_classes() : std::array<std::uint32_t, 8>{};
}

std::array<std::uint32_t, 64> Renderer::material_coat_classes() const noexcept {
    return material_library_ ? material_library_->coat_classes() : std::array<std::uint32_t, 64>{};
}

// A profile lives exactly as long as its texture value: an entry goes when
// its backing left the owned texture cache (replaced, retired, or evicted
// under the VRAM budget). Not drawing is no reason: a warm retained part is
// noted only at its cold registration, so an entry dropped while the unit
// was off screen or behind a full-screen UI never came back (magenta).
// Backings are never reused, so a stale entry is never bound.
void Renderer::trim_material_profiles() noexcept try {
    if (material_profiles_.empty() || native_frame_index_ % 120 != 0) return;
    std::unordered_set<std::uint64_t> live;
    live.reserve(native_owned_textures_.size());
    for (const auto& item : native_owned_textures_) live.insert(item.second.backing);
    for (auto i = material_profiles_.begin(); i != material_profiles_.end();)
        if (i->second.surface || live.count(i->first)) ++i;
        else { i = material_profiles_.erase(i);++material_profiles_revision_; }
} catch (...) {}

// D3D12 (pseudo-PBR S1): batches span textures there, so the profile is not
// a per-batch binding (t37/t38) but each draw's own bindless index.
std::uint32_t Renderer::material_profile_index_d3d12(std::uint64_t backing) noexcept {
    if (!d3d12_ || !backing || material_profiles_.empty() || material_level() <= 0) return 0;
    const auto found = material_profiles_.find(backing);
    if (found == material_profiles_.end() || !found->second.profile12 || (found->second.scenery && !material_world_on_)) return 0;
    const std::uint32_t index = d3d12_->bindless_index(*found->second.profile12);
    return index == UINT32_MAX ? 0 : index;  // a full bindless region: unprofiled (magenta in view 2) until it grows
}

// Material instances (renderer.h): a texture that gained a profile entry
// after its records were registered (another view of the same texture named
// it later) moves those records to the profiled instance, so their batches
// draw the program with the S1 code. Once per new entry, before the layout.
void Renderer::refresh_native_material_instances() noexcept {
    if (material_instances_checked_ == material_instances_revision_) return;
    material_instances_checked_ = material_instances_revision_;
    for (auto& record : native_instance_records_) {
        const std::uint8_t instance = record.command.material_instance, domain = instance & 7u;
        if (!record.occupied || !domain) continue;
        const std::uint64_t backing = d3d12_ ? record.command.material_backing :
            (record.command.state_bucket < native_state_buckets_.size() ? native_state_buckets_[record.command.state_bucket].material_key.texture_backing : 0);
        const auto profile = backing ? material_profiles_.find(backing) : material_profiles_.end();
        if (profile == material_profiles_.end()) continue;
        // Profiled; a crystal profile moves units and world objects to the
        // crystal domains (and back when the profile loses its crystal class).
        std::uint8_t wanted = domain;
        if (profile->second.crystal) wanted = domain == 1u ? kNativeCrystalUnitDomain : domain == 2u ? kNativeCrystalWorldDomain : domain;
        else wanted = domain == kNativeCrystalUnitDomain ? 1u : domain == kNativeCrystalWorldDomain ? 2u : domain;
        const std::uint8_t updated = std::uint8_t(wanted | 8u);
        if (updated == instance) continue;
        record.command.material_instance = updated;
        native_layout_changed();
    }
}

// Retained records keep their instance values across frames: when the
// profile set changed, every record's index is brought up to date once and
// the changed ones upload again.
void Renderer::refresh_material_profile_indices_d3d12() noexcept {
    if (!d3d12_ || material_profiles_indexed_ == material_profiles_revision_) return;
    material_profiles_indexed_ = material_profiles_revision_;
    for (UINT index = 0; index < native_instance_records_.size(); ++index) {
        auto& record = native_instance_records_[index];
        if (!record.occupied || !record.command.material_backing) continue;
        const std::uint32_t profile = material_profile_index_d3d12(record.command.material_backing);
        if (record.command.instance.padding[1] == profile) continue;
        record.command.instance.padding[1] = profile;record.uploaded = false;
        if (record.references && !record.scheduled) { native_instance_dirty_.push_back(index);record.scheduled = true; }
    }
}

// The profile and its relief as one two-slice RGBA8 array with the full mip
// chain (box filtered here: the executor has no GenerateMips for it).
HRESULT Renderer::upload_material_profile_d3d12(MaterialProfileGpu& entry, std::uint32_t side,
                                                const std::vector<std::uint8_t>& pixels, const std::vector<std::uint8_t>& relief) noexcept try {
    if (!side || pixels.size() < std::size_t(side) * side * 4 || relief.size() < std::size_t(side) * side) return E_INVALIDARG;
    UINT mips = 1;
    for (std::uint32_t s = side; s > 1; s >>= 1) ++mips;
    D3D12TextureRef texture;
    HRESULT hr = d3d12_->create_texture_array(side, side, DXGI_FORMAT_R8G8B8A8_UNORM, mips, 2, texture);
    if (FAILED(hr)) return hr;
    std::vector<std::uint8_t> level[2];
    level[0].assign(pixels.begin(), pixels.begin() + std::size_t(side) * side * 4);
    level[1].assign(std::size_t(side) * side * 4, 0);
    for (std::size_t i = 0; i < std::size_t(side) * side; ++i) level[1][i * 4] = relief[i];
    std::uint32_t s = side;
    for (UINT mip = 0; mip < mips; ++mip) {
        const RECT rect{0, 0, LONG(s), LONG(s)};
        for (UINT slice = 0; slice < 2; ++slice) {
            if (FAILED(hr = d3d12_->write_texture(*texture, rect, level[slice].data(), s * 4, mip + slice * mips))) return hr;
            profiler_.add_upload(level[slice].size());
        }
        if (mip + 1 == mips) break;
        const std::uint32_t n = std::max<std::uint32_t>(s / 2, 1);
        for (auto& data : level) {
            std::vector<std::uint8_t> next(std::size_t(n) * n * 4);
            for (std::uint32_t y = 0; y < n; ++y)
                for (std::uint32_t x = 0; x < n; ++x)
                    for (std::uint32_t c = 0; c < 4; ++c) {
                        const std::uint32_t x0 = std::min(x * 2, s - 1), x1 = std::min(x * 2 + 1, s - 1);
                        const std::uint32_t y0 = std::min(y * 2, s - 1), y1 = std::min(y * 2 + 1, s - 1);
                        const std::uint32_t sum = data[(std::size_t(y0) * s + x0) * 4 + c] + data[(std::size_t(y0) * s + x1) * 4 + c] +
                                                  data[(std::size_t(y1) * s + x0) * 4 + c] + data[(std::size_t(y1) * s + x1) * 4 + c];
                        next[(std::size_t(y) * n + x) * 4 + c] = std::uint8_t((sum + 2) / 4);
                    }
            data.swap(next);
        }
        s = n;
    }
    entry.profile12 = std::move(texture);
    return S_OK;
} catch (...) { return E_OUTOFMEMORY; }

} // namespace owned
