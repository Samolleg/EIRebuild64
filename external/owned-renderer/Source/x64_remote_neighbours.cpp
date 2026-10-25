// The renderer DLL's side of the host neighbour stream (x64_remote_neighbours.h).
#include "x64_remote_neighbours.h"

#include "neighbour_piece.h"
#include "neighbour_piece_wire.h"
#include "x64_remote_objects.h"

#include <cstring>

namespace x64remote {

namespace {

void fail(const char* what, const std::string& detail = {}) noexcept {
    char reason[200];
    std::snprintf(reason, sizeof(reason), "neighbour host stream: %s%s%s", what, detail.empty() ? "" : ": ", detail.c_str());
    lose_device(reason);
}

// One synchronous request: the record, then the host's reply.
template<class Request> HRESULT request(Op op, Request value, std::uint64_t Request::* reply_field,
    const void* extra = nullptr, std::uint32_t extra_bytes = 0, void* answer = nullptr, std::uint32_t answer_bytes = 0) noexcept {
    const std::uint64_t serial = next_reply_serial();
    value.*reply_field = serial;
    {
        Writer w(op, 0, static_cast<std::uint32_t>(sizeof(Request)) + extra_bytes);
        w.put(value);
        if (extra_bytes) w.put(extra, extra_bytes);
    }
    const HRESULT hr = wait_reply(serial);
    if (SUCCEEDED(hr) && answer) std::memcpy(answer, readback_area(), answer_bytes);
    end_reply();
    return hr;
}

} // namespace

void neighbour_begin(std::uint64_t epoch, bool enabled, const std::vector<std::uint8_t>& inputs) noexcept {
    if (inputs.size() > chunk_bytes() - sizeof(NeighbourBegin)) { fail("begin inputs exceed a ring record"); return; }
    const NeighbourBegin begin{epoch, enabled ? 1u : 0u, static_cast<std::uint32_t>(inputs.size())};
    Writer w(Op::neighbour_begin, 0, static_cast<std::uint32_t>(sizeof(begin) + inputs.size()));
    w.put(begin);
    if (!inputs.empty()) w.put(inputs.data(), static_cast<std::uint32_t>(inputs.size()));
}

void neighbour_stop(std::uint64_t epoch) noexcept {
    Writer w(Op::neighbour_stop, 0, sizeof(epoch));
    w.put(epoch);
}

void neighbour_uploaded(std::uint64_t epoch) noexcept {
    Writer w(Op::neighbour_uploaded, 0, sizeof(epoch));
    w.put(epoch);
}

std::unique_ptr<owned::NeighbourPiece> neighbour_take(std::uint64_t epoch) noexcept try {
    if (link_removed()) return nullptr;
    NeighbourTakeReply taken{};
    if (FAILED(request(Op::neighbour_take, NeighbourTake{epoch, 0}, &NeighbourTake::reply, nullptr, 0, &taken, sizeof(taken)))) {
        fail("take was not answered");
        return nullptr;
    }
    if (taken.status == 0) return nullptr;
    if (taken.status != 1) { taken.failure[sizeof(taken.failure) - 1] = 0; fail("the host could not serialize a piece", taken.failure); return nullptr; }
    // Handles for the piece's GPU objects, filed by the host before any record uses them.
    std::vector<std::uint32_t> handles(taken.objects);
    for (auto& handle : handles) handle = allocate_handle();
    if (FAILED(request(Op::neighbour_bind, NeighbourBind{0, taken.objects, 0}, &NeighbourBind::reply,
            handles.data(), static_cast<std::uint32_t>(handles.size() * 4)))) {
        fail("bind was not answered");
        return nullptr;
    }
    // The piece bytes through the reply area; transient here.
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(taken.bytes));
    const std::uint32_t area = readback_bytes();
    for (std::uint64_t at = 0; at < taken.bytes;) {
        const auto part = static_cast<std::uint32_t>((std::min<std::uint64_t>)(area, taken.bytes - at));
        if (FAILED(request(Op::neighbour_read, NeighbourRead{0, at, part, 0}, &NeighbourRead::reply,
                nullptr, 0, bytes.data() + at, part))) {
            fail("read was not answered");
            return nullptr;
        }
        at += part;
    }
    std::vector<bool> wrapped(handles.size(), false);
    const auto wrap = [&](std::uint32_t index, const owned::NeighbourWireObject& object, IUnknown* parent) -> IUnknown* {
        if (index >= handles.size()) return nullptr;
        IUnknown* made = nullptr;
        switch (object.kind) {
        case owned::NeighbourWireObjectKind::buffer:
            made = static_cast<ID3D11Buffer*>(new (std::nothrow) RBuffer(object.buffer, handles[index])); break;
        case owned::NeighbourWireObjectKind::texture2d:
            made = static_cast<ID3D11Texture2D*>(new (std::nothrow) RTexture2D(object.texture, handles[index], true)); break;
        case owned::NeighbourWireObjectKind::srv: {
            ComPtr<ID3D11Resource> resource;
            if (!parent || FAILED(parent->QueryInterface(IID_PPV_ARGS(&resource)))) return nullptr;
            made = static_cast<ID3D11ShaderResourceView*>(new (std::nothrow) RSrv(resource.Get(), object.view, handles[index]));
            break;
        }
        }
        if (made) wrapped[index] = true;
        return made;
    };
    auto piece = std::make_unique<owned::NeighbourPiece>();
    std::string failure;
    const bool read = owned::read_neighbour_piece(bytes.data(), bytes.size(), *piece, wrap, failure);
    // Handles no proxy took (a failed read) still name host objects: release them there.
    for (std::size_t i = 0; i < handles.size(); ++i) {
        if (wrapped[i]) continue;
        Writer w(Op::destroy, 0, 4);
        w.u32(handles[i]);
        free_handle(handles[i]);
    }
    if (!read) { piece.reset(); fail("the piece could not be read", failure); return nullptr; }
    return piece;
} catch (...) {
    fail("piece allocation failed");
    return nullptr;
}

NeighbourStatus neighbour_status() noexcept {
    NeighbourStatus status;
    status.settled = status.remaining_known = false; // until a consistent read
    const Control& c = control();
    for (int attempt = 0; attempt < 8; ++attempt) {
        const long before = c.neighbour_sequence;
        if (before & 1) continue;
        MemoryBarrier();
        status.epoch = c.neighbour_epoch;
        status.settled = (c.neighbour_flags & 1u) != 0;
        status.remaining_known = (c.neighbour_flags & 2u) != 0;
        status.remaining = c.neighbour_remaining;
        char error[sizeof(c.neighbour_error)];
        std::memcpy(error, const_cast<const char*>(c.neighbour_error), sizeof(error));
        error[sizeof(error) - 1] = 0;
        MemoryBarrier();
        if (c.neighbour_sequence != before) continue;
        try { status.error = error; } catch (...) {}
        return status;
    }
    return status;
}

} // namespace x64remote
