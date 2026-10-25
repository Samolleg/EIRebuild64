#pragma once
// The neighbour_begin inputs on the remote link (x64_remote_protocol.h): what
// the game process already knows cheaply and the host's stream needs. Shared
// by the DLL's client (neighbour_client.cpp) and the host (neighbour_host.cpp).
#include "owned_resource_store.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace owned {

struct NeighbourRemoteBegin {
    std::string mpr;                // the active map's terrain resource
    std::wstring layout_file;       // world-map-layouts.json next to the loaded addon
    std::string no_sea_maps;        // water_surf_no_sea_maps
    ResourceStoreConfig mounts;     // the active game's first-wins archives
};

namespace neighbour_remote_detail {
inline void put(std::vector<std::uint8_t>& out, const void* data, std::size_t bytes) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), p, p + bytes);
}
inline void put_size(std::vector<std::uint8_t>& out, std::size_t value) {
    const auto v = static_cast<std::uint32_t>(value);
    put(out, &v, sizeof(v));
}
struct In {
    const std::uint8_t* at; const std::uint8_t* end; bool ok = true;
    bool take(void* data, std::size_t bytes) {
        if (!ok || std::size_t(end - at) < bytes) return ok = false;
        std::memcpy(data, at, bytes); at += bytes; return true;
    }
    std::size_t size(std::size_t element) {
        std::uint32_t v = 0;
        if (!take(&v, sizeof(v)) || (element && v > std::size_t(end - at) / element)) { ok = false; return 0; }
        return v;
    }
};
} // namespace neighbour_remote_detail

inline std::vector<std::uint8_t> encode_neighbour_begin(const NeighbourRemoteBegin& begin) {
    using namespace neighbour_remote_detail;
    std::vector<std::uint8_t> out;
    const auto text = [&](const std::string& s) { put_size(out, s.size()); put(out, s.data(), s.size()); };
    const auto wide = [&](const std::wstring& s) { put_size(out, s.size()); put(out, s.data(), s.size() * sizeof(wchar_t)); };
    const auto wides = [&](const std::vector<std::wstring>& list) { put_size(out, list.size()); for (const auto& s : list) wide(s); };
    text(begin.mpr); wide(begin.layout_file); text(begin.no_sea_maps);
    wides(begin.mounts.loose_roots); wides(begin.mounts.archives);
    put_size(out, begin.mounts.archive_kinds.size());
    put(out, begin.mounts.archive_kinds.data(), begin.mounts.archive_kinds.size());
    return out;
}

inline bool decode_neighbour_begin(const std::uint8_t* bytes, std::size_t size, NeighbourRemoteBegin& begin) {
    using namespace neighbour_remote_detail;
    static_assert(sizeof(wchar_t) == 2, "UTF-16 on both sides");
    In in{bytes, bytes + size};
    const auto text = [&](std::string& s) {
        const auto n = in.size(1); if (!in.ok) return;
        s.assign(reinterpret_cast<const char*>(in.at), n); in.at += n;
    };
    const auto wide = [&](std::wstring& s) {
        const auto n = in.size(sizeof(wchar_t)); if (!in.ok) return;
        s.resize(n); in.take(s.data(), n * sizeof(wchar_t));
    };
    const auto wides = [&](std::vector<std::wstring>& list) {
        const auto n = in.size(4); if (!in.ok) return;
        list.resize(n); for (auto& s : list) wide(s);
    };
    text(begin.mpr); wide(begin.layout_file); text(begin.no_sea_maps);
    wides(begin.mounts.loose_roots); wides(begin.mounts.archives);
    const auto kinds = in.size(1);
    if (in.ok) { begin.mounts.archive_kinds.resize(kinds); in.take(begin.mounts.archive_kinds.data(), kinds); }
    return in.ok && in.at == in.end;
}

} // namespace owned
