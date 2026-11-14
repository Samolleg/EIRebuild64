// A neighbour piece on the remote link (neighbour_piece_wire.h). One visit
// function per type serves both directions, so the writer and the reader
// cannot drift apart.
#include "neighbour_piece_wire.h"

#include "liquid_surf.h"

#include <cstring>
#include <map>
#include <type_traits>

namespace owned {

using Microsoft::WRL::ComPtr;

namespace {

constexpr std::uint32_t kWireMagic = 0x5045494eu; // 'NIEP'
constexpr std::uint32_t kWireVersion = 8; // 8: source border material, no extra opacity pulse

// Plain record types travel as bytes; both builds must agree on their sizes.
template<class T> constexpr bool plain = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;
struct PlainSize { const char* name; std::uint32_t bytes; };
using LodStats = decltype(NeighbourPiece::lod_stats);
const PlainSize kPlainSizes[] = {
    {"NeighbourBounds", sizeof(NeighbourBounds)},
    {"NativePrebuiltMesh", sizeof(NativePrebuiltMesh)},
    {"NeighbourPieceTerrainDraw", sizeof(NeighbourPieceTerrainDraw)},
    {"NeighbourPieceFigure", sizeof(NeighbourPieceFigure)},
    {"NeighbourPieceDecoration", sizeof(NeighbourPieceDecoration)},
    {"NeighbourPieceTexture::Mip", sizeof(NeighbourPieceTexture::Mip)},
    {"LodStats", sizeof(LodStats)},
    {"GcTileGpu", sizeof(GcTileGpu)},
    {"emissive::Tile", sizeof(emissive::Tile)},
    {"ambient::LiquidLevelInputs::Vertex", sizeof(ambient::LiquidLevelInputs::Vertex)},
    {"ambient::LiquidLevelInputs::Tile", sizeof(ambient::LiquidLevelInputs::Tile)},
    {"D3D11_BUFFER_DESC", sizeof(D3D11_BUFFER_DESC)},
    {"D3D11_TEXTURE2D_DESC", sizeof(D3D11_TEXTURE2D_DESC)},
    {"D3D11_SHADER_RESOURCE_VIEW_DESC", sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC)},
};
static_assert(plain<NativePrebuiltMesh> && plain<NeighbourPieceTerrainDraw> && plain<NeighbourPieceFigure> &&
    plain<NeighbourPieceDecoration> && plain<NeighbourPieceTexture::Mip> && plain<LodStats> && plain<GcTileGpu> &&
    plain<emissive::Tile> && plain<ambient::LiquidLevelInputs::Vertex> && plain<ambient::LiquidLevelInputs::Tile> && plain<NeighbourBounds>,
    "wire records must be plain values");

struct Writer {
    std::vector<std::uint8_t>& out;
    std::vector<IUnknown*>& objects;
    std::map<IUnknown*, std::uint32_t> index;
    std::vector<NeighbourWireObject> table;
    std::string failure;
    bool ok = true;
    static constexpr bool reading = false;

    void raw(const void* data, std::size_t bytes) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        out.insert(out.end(), p, p + bytes);
    }
    template<class T> void pod(T& value) { static_assert(plain<T>); raw(&value, sizeof(T)); }
    void size(std::size_t& value) { std::uint64_t v = value; pod(v); }
    void str(std::string& value) { std::uint64_t n = value.size(); pod(n); raw(value.data(), value.size()); }
    template<class T> void vec(std::vector<T>& value) {
        static_assert(plain<T>);
        std::uint64_t n = value.size(); pod(n); raw(value.data(), value.size() * sizeof(T));
    }
    void bits(std::vector<bool>& value) {
        std::vector<std::uint8_t> bytes(value.begin(), value.end());
        vec(bytes);
    }
    // A GPU object: its table index (UINT32_MAX for none).
    template<class I> void object(ComPtr<I>& value) {
        std::uint32_t at = UINT32_MAX;
        if (value) at = add(value.Get());
        pod(at);
    }
    std::uint32_t add(IUnknown* object) {
        ComPtr<IUnknown> unknown;
        object->QueryInterface(IID_PPV_ARGS(&unknown));
        const auto found = index.find(unknown.Get());
        if (found != index.end()) return found->second;
        NeighbourWireObject entry;
        // The object goes out as its own interface pointer (ID3D11Buffer*,
        // ID3D11Texture2D*, ID3D11ShaderResourceView*): the host's handle
        // table casts entries to that interface, and a runtime object's
        // IUnknown identity is not necessarily the same address.
        IUnknown* typed = nullptr;
        ComPtr<ID3D11Buffer> buffer; ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11ShaderResourceView> view;
        if (SUCCEEDED(unknown.As(&buffer))) {
            entry.kind = NeighbourWireObjectKind::buffer; buffer->GetDesc(&entry.buffer); typed = buffer.Get();
        } else if (SUCCEEDED(unknown.As(&texture))) {
            entry.kind = NeighbourWireObjectKind::texture2d; texture->GetDesc(&entry.texture); typed = texture.Get();
        } else if (SUCCEEDED(unknown.As(&view))) {
            entry.kind = NeighbourWireObjectKind::srv; view->GetDesc(&entry.view); typed = view.Get();
            ComPtr<ID3D11Resource> resource; view->GetResource(&resource);
            entry.parent = add(resource.Get()); // its resource precedes it in the table
        } else { ok = false; failure = "unsupported GPU object kind"; return UINT32_MAX; }
        const auto at = static_cast<std::uint32_t>(table.size());
        table.push_back(entry);
        typed->AddRef();
        objects.push_back(typed);
        index.emplace(unknown.Get(), at);
        return at;
    }
};

struct Reader {
    const std::uint8_t* at;
    const std::uint8_t* end;
    std::vector<ComPtr<IUnknown>> objects;
    std::string failure;
    bool ok = true;
    static constexpr bool reading = true;

    bool take(void* data, std::size_t bytes) {
        if (!ok || std::size_t(end - at) < bytes) { fail("truncated"); return false; }
        std::memcpy(data, at, bytes); at += bytes; return true;
    }
    void fail(const char* what) { if (ok) { ok = false; failure = what; } }
    template<class T> void pod(T& value) { static_assert(plain<T>); take(&value, sizeof(T)); }
    bool count(std::uint64_t& n, std::size_t element) {
        pod(n);
        if (ok && element && n > std::size_t(end - at) / element) { fail("length beyond the piece"); return false; }
        return ok;
    }
    void size(std::size_t& value) { std::uint64_t v = 0; pod(v); value = static_cast<std::size_t>(v); }
    void str(std::string& value) {
        std::uint64_t n = 0; if (!count(n, 1)) return;
        value.assign(reinterpret_cast<const char*>(at), static_cast<std::size_t>(n)); at += n;
    }
    template<class T> void vec(std::vector<T>& value) {
        static_assert(plain<T>);
        std::uint64_t n = 0; if (!count(n, sizeof(T))) return;
        value.resize(static_cast<std::size_t>(n));
        take(value.data(), static_cast<std::size_t>(n) * sizeof(T));
    }
    void bits(std::vector<bool>& value) {
        std::vector<std::uint8_t> bytes; vec(bytes);
        value.assign(bytes.begin(), bytes.end());
    }
    template<class I> void object(ComPtr<I>& value) {
        std::uint32_t index = UINT32_MAX; pod(index);
        value.Reset();
        if (!ok || index == UINT32_MAX) return;
        if (index >= objects.size() || !objects[index]) { fail("object index"); return; }
        if (FAILED(objects[index].As(&value))) fail("object interface");
    }
};

// Optional shared values: a presence byte, then the value.
template<class A, class T, class Visit> void optional(A& a, std::shared_ptr<T>& value, Visit visit) {
    std::uint8_t present = value ? 1 : 0;
    a.pod(present);
    if (!present) { if constexpr (A::reading) value.reset(); return; }
    if constexpr (A::reading) { auto made = std::make_shared<std::remove_const_t<T>>(); visit(a, *made); value = std::move(made); }
    else visit(a, const_cast<std::remove_const_t<T>&>(*value));
}

template<class A, class T, class Visit> void list(A& a, std::vector<T>& value, Visit visit) {
    std::uint64_t n = value.size();
    if constexpr (A::reading) { if (!a.count(n, 1)) return; value.resize(static_cast<std::size_t>(n)); }
    else a.pod(n);
    for (auto& item : value) { if (!a.ok) return; visit(a, item); }
}

template<class A> void visit_identity(A& a, ResourceIdentity& v) {
    a.str(v.canonical_name); a.pod(v.container_incarnation); a.pod(v.container_file_key);
    a.pod(v.absolute_offset); a.pod(v.bytes); a.pod(v.mount_ordinal); a.pod(v.entry_index); a.pod(v.loose_file);
}

template<class A> void visit_selection(A& a, NeighbourSelection& v) {
    auto& p = v.placement;
    a.str(p.profile); a.str(p.allod); a.str(p.zone); a.str(p.mpr); a.str(p.component); a.str(p.mpr_path); a.str(p.mob_path);
    a.pod(p.x); a.pod(p.y); a.pod(p.width); a.pod(p.height);
    a.pod(v.x); a.pod(v.y);
}

template<class A> void visit_texture(A& a, NeighbourPieceTexture& v) {
    visit_identity(a, v.identity); a.pod(v.view_identity); a.pod(v.format); a.pod(v.mip_count); a.pod(v.opaque_mips);
    a.vec(v.mips); a.vec(v.bytes);
}

template<class A> void visit_ground_cover(A& a, GroundCoverNeighbourInputs& v) {
    auto& g = v.grids;
    a.pod(g.tiles_x); a.pod(g.tiles_y); a.vec(g.area); a.vec(g.types); a.vec(g.height); a.vec(g.ground); a.vec(g.emissive);
    a.pod(g.level.tiles_x); a.vec(g.level.dry_area); a.vec(g.level.liquid_area); a.vec(g.level.ground);
    a.vec(g.level.vertices); a.vec(g.level.tiles);
    a.vec(v.tiles); a.vec(v.used); a.vec(v.base_lanes); a.vec(v.trees);
    a.pod(v.families_valid);
    a.vec(v.families.signatures);
    {
        std::uint64_t n = v.families.names.size();
        if constexpr (A::reading) {
            if (!a.count(n, 1)) return;
            v.families.names.clear();
            v.families.placement_names.clear();
            for (std::uint64_t i = 0; i < n && a.ok; ++i) {
                std::uint32_t key = 0; std::string name; std::uint8_t placement = 0;
                a.pod(key); a.str(name); a.pod(placement);
                if (placement > 1) { a.ok = false; return; }
                if (v.families.names.emplace(key, std::move(name)).second && placement)
                    v.families.placement_names.insert(key);
            }
        } else {
            a.pod(n);
            for (auto& [key, name] : v.families.names) {
                auto k = key; auto s = name;
                std::uint8_t placement = v.families.placement_names.count(key) != 0;
                a.pod(k); a.str(s); a.pod(placement);
            }
        }
    }
    {
        std::vector<std::uint32_t> authored(v.families.preserve_authored.begin(), v.families.preserve_authored.end());
        a.vec(authored);
        if constexpr (A::reading) {
            v.families.preserve_authored.clear();
            v.families.preserve_authored.insert(authored.begin(), authored.end());
        }
    }
    a.str(v.families.source); a.pod(v.families.rock_hard_only);
    a.vec(v.base_slots); a.vec(v.slot_types);
    optional(a, v.blend_input, [](A& b, TerrainTileBlendInput& t) {
        b.pod(t.tiles_x); b.pod(t.tiles_y); b.pod(t.slots); b.vec(t.slot); b.vec(t.lanes); b.vec(t.wet);
        b.bits(t.steep); b.bits(t.underwater); b.vec(t.heights); b.vec(t.usage); b.bits(t.usable);
    });
    a.vec(v.contact_tiles); a.pod(v.contact_used); a.pod(v.lowest); a.pod(v.highest);
    a.pod(v.texture_count); a.pod(v.tiles_per_texture_side); a.vec(v.atlas_textures); a.pod(v.grids_valid);
}

template<class A> void visit_surf(A& a, LiquidSurfField& v) {
    a.pod(v.tiles_x); a.pod(v.tiles_y); a.vec(v.texels); a.pod(v.surf_cells); a.pod(v.max_exposure);
    a.vec(v.level); a.vec(v.impact); a.vec(v.swell_distance); a.vec(v.swell_depth); a.pod(v.impact_spots);
}

template<class A> void visit_piece(A& a, NeighbourPiece& v) {
    a.pod(v.epoch); visit_selection(a, v.selection); a.vec(v.selected); a.pod(v.welded);
    a.str(v.terrain_failure); a.pod(v.decoded_bytes); a.size(v.omitted_decorations); a.pod(v.unbuilt_decorations);
    a.str(v.decoration_summary); a.pod(v.lod_stats);
    list(a, v.identities, [](A& b, ResourceIdentity& i) { visit_identity(b, i); });
    list(a, v.textures, [](A& b, NeighbourPieceTexture& t) { visit_texture(b, t); });
    list(a, v.pages, [](A& b, NativePrebuiltPage& p) { b.object(p.buffer); b.object(p.srv); b.pod(p.bytes); });
    a.vec(v.meshes);
    list(a, v.prebuilt_textures, [](A& b, NativePrebuiltTexture& t) {
        b.str(t.key); b.pod(t.generation); b.pod(t.format); b.pod(t.width); b.pod(t.height); b.pod(t.mip_count);
        b.pod(t.bytes); b.object(t.texture); b.object(t.srv);
    });
    a.vec(v.terrain); a.vec(v.figures); a.vec(v.decorations);
    list(a, v.impostors, [](A& b, NeighbourPieceImpostor& i) {
        b.pod(i.figure); b.pod(i.texture); b.object(i.vertices); b.object(i.indices); b.object(i.vertex_view);
        b.pod(i.index_count); b.pod(i.sphere);
    });
    optional(a, v.ground_cover, [](A& b, GroundCoverNeighbourInputs& g) { visit_ground_cover(b, g); });
    optional(a, v.liquid_surf, [](A& b, LiquidSurfField& s) { visit_surf(b, s); });
}

} // namespace

bool write_neighbour_piece(const NeighbourPiece& piece, std::vector<std::uint8_t>& bytes,
    std::vector<IUnknown*>& objects, std::string& failure) {
    try {
        // The body first (it fills the object table), then header + table + body.
        std::vector<std::uint8_t> body;
        std::vector<IUnknown*> collected;
        Writer writer{body, collected, {}, {}, {}, true};
        visit_piece(writer, const_cast<NeighbourPiece&>(piece));
        if (!writer.ok) {
            for (auto* object : collected) object->Release();
            failure = writer.failure;
            return false;
        }
        bytes.clear();
        std::vector<IUnknown*> none;
        Writer header{bytes, none, {}, {}, {}, true};
        std::uint32_t magic = kWireMagic, version = kWireVersion, sizes = static_cast<std::uint32_t>(std::size(kPlainSizes));
        header.pod(magic); header.pod(version); header.pod(sizes);
        for (const auto& entry : kPlainSizes) { auto b = entry.bytes; header.pod(b); }
        std::uint32_t count = static_cast<std::uint32_t>(writer.table.size());
        header.pod(count);
        for (auto& entry : writer.table) {
            header.pod(entry.kind); header.pod(entry.parent);
            header.pod(entry.buffer); header.pod(entry.texture); header.pod(entry.view);
        }
        bytes.insert(bytes.end(), body.begin(), body.end());
        objects.insert(objects.end(), collected.begin(), collected.end());
        return true;
    } catch (...) {
        failure = "piece serialization allocation failed";
        return false;
    }
}

bool read_neighbour_piece(const std::uint8_t* bytes, std::size_t size, NeighbourPiece& piece,
    const NeighbourWireWrap& wrap, std::string& failure) {
    try {
        Reader reader{bytes, bytes + size, {}, {}, true};
        std::uint32_t magic = 0, version = 0, sizes = 0;
        reader.pod(magic); reader.pod(version); reader.pod(sizes);
        if (!reader.ok || magic != kWireMagic || version != kWireVersion) { failure = "piece header (version mismatch)"; return false; }
        if (sizes != std::size(kPlainSizes)) { failure = "piece record table differs"; return false; }
        for (const auto& entry : kPlainSizes) {
            std::uint32_t bytes_there = 0; reader.pod(bytes_there);
            if (reader.ok && bytes_there != entry.bytes) { failure = std::string("piece record size differs: ") + entry.name; return false; }
        }
        std::uint32_t count = 0; reader.pod(count);
        if (!reader.ok || count > 1u << 20) { failure = "piece object table"; return false; }
        reader.objects.resize(count);
        for (std::uint32_t i = 0; i < count && reader.ok; ++i) {
            NeighbourWireObject entry;
            reader.pod(entry.kind); reader.pod(entry.parent);
            reader.pod(entry.buffer); reader.pod(entry.texture); reader.pod(entry.view);
            if (!reader.ok) break;
            IUnknown* parent = nullptr;
            if (entry.kind == NeighbourWireObjectKind::srv) {
                if (entry.parent >= i || !reader.objects[entry.parent]) { failure = "piece view parent"; return false; }
                parent = reader.objects[entry.parent].Get();
            }
            IUnknown* made = wrap(i, entry, parent);
            if (!made) { failure = "piece object could not be wrapped"; return false; }
            reader.objects[i].Attach(made);
        }
        visit_piece(reader, piece);
        if (!reader.ok) { failure = "piece body: " + reader.failure; return false; }
        if (reader.at != reader.end) { failure = "piece body: trailing bytes"; return false; }
        return true;
    } catch (...) {
        failure = "piece read allocation failed";
        return false;
    }
}

} // namespace owned
