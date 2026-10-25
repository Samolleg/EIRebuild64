#pragma once
// Proxy D3D11 objects of the remote device (x86 side, internal to
// x64_remote_device.cpp and x64_remote_context.cpp). Each object carries a
// client-assigned handle and the description it was created with, so
// GetDesc, GetType and GetResource never leave the process.

#include "x64_remote_link.h"

#include <d3d11_1.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace x64remote {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kObjectMagic = 0x6a624f52u; // 'RObj'

// Directly after the vtable pointer of every proxy D3D11 object (all the
// interfaces are single-inheritance chains without data members).
struct Tag {
    std::uint32_t magic;
    std::uint32_t handle;
};

inline const Tag* tag_of(const void* object) noexcept {
    return reinterpret_cast<const Tag*>(static_cast<const std::uint8_t*>(object) + sizeof(void*));
}

// The handle of a proxy passed back in by the renderer. A foreign object (a
// real D3D11 object slipped into the remote device) is a programming error:
// reported once and sent as null, so it shows as a missing visual.
inline std::uint32_t handle_of(const void* object) noexcept {
    if (!object) return 0;
    const Tag* tag = tag_of(object);
    if (tag->magic != kObjectMagic) {
        unsupported("a non-remote D3D11 object was passed to the remote device");
        return 0;
    }
    return tag->handle;
}

inline std::uint32_t align4(std::uint32_t value) noexcept { return (value + 3u) & ~3u; }
inline std::uint32_t mip_extent(std::uint32_t extent, std::uint32_t mip) noexcept { return (std::max)(1u, extent >> mip); }

// One subresource region as tight rows of format blocks.
struct Layout {
    std::uint32_t row_bytes = 0; // bytes of one row of blocks
    std::uint32_t rows = 0;      // block rows per depth slice
    std::uint32_t depth = 1;
    std::uint32_t row_pitch = 0; // client pitch: row_bytes rounded up to 4
    std::uint32_t bytes() const noexcept { return row_pitch * rows * depth; }
};
// width/height/depth in texels.
Layout layout_of(DXGI_FORMAT format, std::uint32_t width, std::uint32_t height, std::uint32_t depth) noexcept;

// ---- private data ---------------------------------------------------------------

class PrivateData {
public:
    ~PrivateData();
    HRESULT get(REFGUID guid, UINT* size, void* data) noexcept;
    HRESULT set(REFGUID guid, UINT size, const void* data, const IUnknown* object) noexcept;

private:
    struct Entry {
        GUID guid;
        std::vector<std::uint8_t> bytes;
        IUnknown* object;
    };
    std::mutex mutex_;
    std::vector<Entry> entries_;
};
PrivateData* lazy_private_data(std::unique_ptr<PrivateData>& slot, bool create) noexcept;

// The remote device (one per process); children hand it out in GetDevice.
ID3D11Device* remote_device_add_ref() noexcept;

// ---- device children --------------------------------------------------------------

template <class I> class Child : public I {
public:
    Tag tag_{kObjectMagic, 0};

    Child() noexcept { tag_.handle = allocate_handle(); }
    // A handle the creator owns (the swap chain's back buffer).
    Child(std::uint32_t handle, bool owns) noexcept : owns_handle_(owns) { tag_.handle = handle; }
    virtual ~Child() {
        destroy_now();
        tag_.magic = 0;
    }
    std::uint32_t handle() const noexcept { return tag_.handle; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || implements(riid)) {
            *out = static_cast<I*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (!left) delete this;
        return left;
    }
    ULONG references() const noexcept { return references_.load(); }
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** device) override {
        if (device) *device = remote_device_add_ref();
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* size, void* data) override {
        PrivateData* store = lazy_private_data(private_, false);
        if (!store) { if (size) *size = 0; return DXGI_ERROR_NOT_FOUND; }
        return store->get(guid, size, data);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT size, const void* data) override {
        PrivateData* store = lazy_private_data(private_, true);
        return store ? store->set(guid, size, data, nullptr) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* object) override {
        PrivateData* store = lazy_private_data(private_, true);
        return store ? store->set(guid, 0, nullptr, object) : E_OUTOFMEMORY;
    }

protected:
    virtual bool implements(REFIID riid) const noexcept = 0;
    // Writes the destroy record (once) and returns the ring position after it.
    std::int64_t destroy_now() noexcept {
        if (!owns_handle_ || !tag_.handle) return 0;
        std::int64_t end = 0;
        {
            Writer w(Op::destroy, 0, 4);
            w.u32(tag_.handle);
            end = w.end_position();
        }
        free_handle(tag_.handle);
        owns_handle_ = false;
        return end;
    }
    bool owns_handle_ = true;

private:
    std::atomic<ULONG> references_{1};
    std::unique_ptr<PrivateData> private_;
};

// x86 memory one dynamic Map writes into, valid until its Unmap and then
// reused for other maps (no copy of a dynamic resource persists here).
// Watched blocks (unranged maps of large buffers) send only touched pages.
struct MapBlock {
    std::uint8_t* base = nullptr;
    std::uint32_t capacity = 0;
    bool watched = false;
};
MapBlock acquire_block(std::uint32_t bytes, bool watched) noexcept;
void release_block(const MapBlock& block) noexcept;

// Staging readbacks: the renderer reads `data`, filled from the shared
// readback area or a prefetch slot.
struct Staging {
    std::uint8_t* data = nullptr;
    std::uint32_t bytes = 0;
    std::uint32_t slot = kPrefetchSlots; // prefetch slot in flight
    std::uint32_t requested = 0;  // prefetch serial in flight (0: none)
    std::uint32_t request_sub = 0, request_writes = 0;
    bool valid = false;           // data holds valid_sub as of valid_writes
    std::uint32_t valid_sub = 0, valid_writes = 0;
    Staging() = default;
    Staging(const Staging&) = delete;
    Staging& operator=(const Staging&) = delete;
    ~Staging();
    bool reserve(std::uint32_t size) noexcept;
    bool in_flight() const noexcept { return requested && prefetch_slot(slot).serial != requested; }
};

// State shared by the three resource classes.
struct ResourceState {
    D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
    std::uint32_t writes = 0; // copies/updates recorded into it (staging validity)
    std::unique_ptr<Staging> staging;
    bool mapped = false;
    std::uint32_t mapped_sub = 0;
    D3D11_MAP mapped_type = D3D11_MAP_WRITE_DISCARD;
    MapBlock block;                 // while a WRITE_* map is open
    std::uint32_t range_offset = 0; // declared range of that map (bytes 0: none)
    std::uint32_t range_bytes = 0;
};

template <class I> class Resource : public Child<I> {
public:
    using Child<I>::Child;
    ~Resource() override {
        // A prefetch slot is reused only after the destroy record: the host
        // drops the prefetch there and never writes the slot for it again.
        if (state_.staging && state_.staging->requested) {
            this->destroy_now();
            free_prefetch_slot(state_.staging->slot);
        }
    }
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* dimension) override { if (dimension) *dimension = state_.dimension; }
    void STDMETHODCALLTYPE SetEvictionPriority(UINT) override {}
    UINT STDMETHODCALLTYPE GetEvictionPriority() override { return DXGI_RESOURCE_PRIORITY_NORMAL; }
    ResourceState state_;
};

class RBuffer final : public Resource<ID3D11Buffer> {
public:
    explicit RBuffer(const D3D11_BUFFER_DESC& desc) noexcept : desc_(desc) {
        state_.dimension = D3D11_RESOURCE_DIMENSION_BUFFER;
        state_.usage = desc.Usage;
    }
    // An object the host created under a handle this process allocated
    // (neighbour pieces); its destroy record releases it there.
    RBuffer(const D3D11_BUFFER_DESC& desc, std::uint32_t handle) noexcept : Resource(handle, true), desc_(desc) {
        state_.dimension = D3D11_RESOURCE_DIMENSION_BUFFER;
        state_.usage = desc.Usage;
    }
    void STDMETHODCALLTYPE GetDesc(D3D11_BUFFER_DESC* desc) override { if (desc) *desc = desc_; }
    D3D11_BUFFER_DESC desc_;

private:
    bool implements(REFIID riid) const noexcept override {
        return riid == __uuidof(ID3D11Buffer) || riid == __uuidof(ID3D11Resource);
    }
};

class RTexture2D final : public Resource<ID3D11Texture2D> {
public:
    explicit RTexture2D(const D3D11_TEXTURE2D_DESC& desc) noexcept : desc_(desc) { init(); }
    // owns: false for the swap chain's back buffer, true for an object the
    // host created under a handle this process allocated (neighbour pieces).
    RTexture2D(const D3D11_TEXTURE2D_DESC& desc, std::uint32_t handle, bool owns = false) noexcept
        : Resource(handle, owns), desc_(desc) { init(); }
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE2D_DESC* desc) override { if (desc) *desc = desc_; }
    D3D11_TEXTURE2D_DESC desc_;

private:
    void init() noexcept {
        state_.dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
        state_.usage = desc_.Usage;
    }
    bool implements(REFIID riid) const noexcept override {
        return riid == __uuidof(ID3D11Texture2D) || riid == __uuidof(ID3D11Resource);
    }
};

class RTexture3D final : public Resource<ID3D11Texture3D> {
public:
    explicit RTexture3D(const D3D11_TEXTURE3D_DESC& desc) noexcept : desc_(desc) {
        state_.dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
        state_.usage = desc.Usage;
    }
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE3D_DESC* desc) override { if (desc) *desc = desc_; }
    D3D11_TEXTURE3D_DESC desc_;

private:
    bool implements(REFIID riid) const noexcept override {
        return riid == __uuidof(ID3D11Texture3D) || riid == __uuidof(ID3D11Resource);
    }
};

// The ResourceState of a proxy resource (null for null or foreign objects).
ResourceState* state_of(ID3D11Resource* resource) noexcept;
// Tight layout of `subresource`, its offset in a full shadow and the total.
bool subresource_layout(ID3D11Resource* resource, std::uint32_t subresource, Layout& layout,
                        std::uint32_t* offset, std::uint32_t* total) noexcept;
// Writes `bytes` tight bytes (rows of `layout`) of `subresource` as one or
// more update_subresource records on `context` (chunked by row bands).
void send_update(std::uint16_t context, ID3D11Resource* resource, std::uint32_t subresource, const D3D11_BOX* box,
                 const void* data, std::uint32_t row_pitch, std::uint32_t depth_pitch) noexcept;

// ---- views, states, shaders, queries ----------------------------------------------

template <class I, class Desc> class View final : public Child<I> {
public:
    View(ID3D11Resource* resource, const Desc& desc) noexcept : resource_(resource), desc_(desc) {}
    // A view the host created under a handle this process allocated.
    View(ID3D11Resource* resource, const Desc& desc, std::uint32_t handle) noexcept
        : Child<I>(handle, true), resource_(resource), desc_(desc) {}
    void STDMETHODCALLTYPE GetResource(ID3D11Resource** resource) override {
        if (!resource) return;
        *resource = resource_.Get();
        if (*resource) (*resource)->AddRef();
    }
    void STDMETHODCALLTYPE GetDesc(Desc* desc) override { if (desc) *desc = desc_; }

private:
    bool implements(REFIID riid) const noexcept override { return riid == __uuidof(I) || riid == __uuidof(ID3D11View); }
    ComPtr<ID3D11Resource> resource_;
    Desc desc_;
};
using RSrv = View<ID3D11ShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>;
using RUav = View<ID3D11UnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>;
using RRtv = View<ID3D11RenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>;
using RDsv = View<ID3D11DepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>;

template <class I, class Desc> class State final : public Child<I> {
public:
    explicit State(const Desc& desc) noexcept : desc_(desc) {}
    void STDMETHODCALLTYPE GetDesc(Desc* desc) override { if (desc) *desc = desc_; }

private:
    bool implements(REFIID riid) const noexcept override { return riid == __uuidof(I); }
    Desc desc_;
};

template <class I> class Plain final : public Child<I> {
private:
    bool implements(REFIID riid) const noexcept override { return riid == __uuidof(I); }
};

class RCommandList final : public Child<ID3D11CommandList> {
public:
    UINT STDMETHODCALLTYPE GetContextFlags() override { return 0; }

private:
    bool implements(REFIID riid) const noexcept override { return riid == __uuidof(ID3D11CommandList); }
};

class RQuery final : public Child<ID3D11Query> {
public:
    explicit RQuery(const D3D11_QUERY_DESC& desc) noexcept : desc_(desc), slot_(allocate_query_slot()) {}
    ~RQuery() override { free_query_slot(slot_); }
    UINT STDMETHODCALLTYPE GetDataSize() override;
    void STDMETHODCALLTYPE GetDesc(D3D11_QUERY_DESC* desc) override { if (desc) *desc = desc_; }
    D3D11_QUERY_DESC desc_;
    std::uint32_t slot_;           // kNoQuerySlot: no slot (the device is lost)
    std::uint64_t end_serial_ = 0; // global serial of the last End

private:
    bool implements(REFIID riid) const noexcept override {
        return riid == __uuidof(ID3D11Query) || riid == __uuidof(ID3D11Asynchronous);
    }
};

// Immediate (index 0) or deferred context proxy.
ID3D11DeviceContext1* create_context(std::uint16_t index) noexcept;

} // namespace x64remote
