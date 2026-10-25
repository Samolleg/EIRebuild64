// Remote D3D11 device, its DXGI device/adapter/factory and the swap chain
// (x86 side). Creation calls never wait: the client assigns the handle and
// the host reports a failed creation in host.log. Capabilities come from the
// snapshot the host wrote at connect (Control::caps).
#include "x64_remote_device.h"
#include "x64_remote_objects.h"

#include <cstdio>
#include <cstdlib>

namespace owned { void bridge_report_event(const char* event, const char* detail) noexcept; void retained_publication_attach() noexcept; }

namespace x64remote {

// ---- shared helpers (x64_remote_objects.h) ------------------------------------------

namespace {
struct Block {
    std::uint32_t width, height, bytes;
};

Block format_block(DXGI_FORMAT format) noexcept {
    const unsigned f = static_cast<unsigned>(format);
    if (f >= 1 && f <= 4) return {1, 1, 16};
    if (f >= 5 && f <= 8) return {1, 1, 12};
    if (f >= 9 && f <= 22) return {1, 1, 8};
    if (f >= 23 && f <= 47) return {1, 1, 4};
    if (f >= 48 && f <= 59) return {1, 1, 2};
    if (f >= 60 && f <= 65) return {1, 1, 1};
    if (f == 67) return {1, 1, 4};
    if (f == 68 || f == 69) return {2, 1, 4};
    if ((f >= 70 && f <= 72) || (f >= 79 && f <= 81)) return {4, 4, 8};
    if ((f >= 73 && f <= 78) || (f >= 82 && f <= 84) || (f >= 94 && f <= 99)) return {4, 4, 16};
    if (f == 85 || f == 86 || f == 115) return {1, 1, 2};
    if (f >= 87 && f <= 93) return {1, 1, 4};
    unsupported("a DXGI format without a known texel size");
    return {1, 1, 4};
}

std::mutex g_private_mutex;
} // namespace

Layout layout_of(DXGI_FORMAT format, std::uint32_t width, std::uint32_t height, std::uint32_t depth) noexcept {
    const Block block = format_block(format);
    Layout layout;
    layout.row_bytes = (width + block.width - 1) / block.width * block.bytes;
    layout.rows = (height + block.height - 1) / block.height;
    layout.depth = depth ? depth : 1;
    layout.row_pitch = align4(layout.row_bytes);
    return layout;
}

PrivateData::~PrivateData() {
    for (auto& entry : entries_)
        if (entry.object) entry.object->Release();
}

HRESULT PrivateData::get(REFGUID guid, UINT* size, void* data) noexcept {
    if (!size) return E_INVALIDARG;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : entries_) {
        if (entry.guid != guid) continue;
        if (entry.object) {
            if (!data) { *size = sizeof(IUnknown*); return S_OK; }
            if (*size < sizeof(IUnknown*)) return DXGI_ERROR_MORE_DATA;
            entry.object->AddRef();
            *static_cast<IUnknown**>(data) = entry.object;
            *size = sizeof(IUnknown*);
            return S_OK;
        }
        if (!data) { *size = static_cast<UINT>(entry.bytes.size()); return S_OK; }
        if (*size < entry.bytes.size()) return DXGI_ERROR_MORE_DATA;
        std::memcpy(data, entry.bytes.data(), entry.bytes.size());
        *size = static_cast<UINT>(entry.bytes.size());
        return S_OK;
    }
    *size = 0;
    return DXGI_ERROR_NOT_FOUND;
}

HRESULT PrivateData::set(REFGUID guid, UINT size, const void* data, const IUnknown* object) noexcept try {
    IUnknown* drop = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.guid == guid; });
        if (found != entries_.end()) {
            drop = found->object;
            entries_.erase(found);
        }
        if (object || (data && size)) {
            Entry entry{guid, {}, const_cast<IUnknown*>(object)};
            if (object) entry.object->AddRef();
            else entry.bytes.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + size);
            entries_.push_back(std::move(entry));
        }
    }
    if (drop) drop->Release();
    return S_OK;
} catch (...) {
    return E_OUTOFMEMORY;
}

PrivateData* lazy_private_data(std::unique_ptr<PrivateData>& slot, bool create) noexcept {
    std::lock_guard<std::mutex> lock(g_private_mutex);
    if (!slot && create) slot.reset(new (std::nothrow) PrivateData);
    return slot.get();
}

namespace {
// Free map blocks of this thread (maps open and close on one thread). They
// live for the process: the pool is bounded by the largest maps open at once.
thread_local std::vector<MapBlock> t_blocks;
}

MapBlock acquire_block(std::uint32_t bytes, bool watched) noexcept {
    std::size_t best = t_blocks.size();
    for (std::size_t i = 0; i < t_blocks.size(); ++i)
        if (t_blocks[i].watched == watched && t_blocks[i].capacity >= bytes &&
            (best == t_blocks.size() || t_blocks[i].capacity < t_blocks[best].capacity))
            best = i;
    if (best < t_blocks.size()) {
        const MapBlock block = t_blocks[best];
        t_blocks.erase(t_blocks.begin() + static_cast<std::ptrdiff_t>(best));
        return block;
    }
    MapBlock block;
    block.watched = watched;
    if (watched) {
        block.capacity = (std::max(bytes, 1u) + 0xffffu) & ~0xffffu; // allocation granularity
        block.base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, block.capacity, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
    } else {
        block.capacity = (std::max(bytes, 1u) + 0xfffu) & ~0xfffu;
        block.base = static_cast<std::uint8_t*>(std::malloc(block.capacity));
    }
    if (!block.base) return {};
    count_shadow(block.capacity);
    return block;
}

void release_block(const MapBlock& block) noexcept try {
    if (block.base) t_blocks.push_back(block);
} catch (...) {}

Staging::~Staging() {
    std::free(data);
}

bool Staging::reserve(std::uint32_t size) noexcept {
    if (data && bytes >= size) return true;
    std::free(data);
    data = static_cast<std::uint8_t*>(std::malloc(size ? size : 1u));
    bytes = data ? size : 0;
    valid = false;
    return data != nullptr;
}

ResourceState* state_of(ID3D11Resource* resource) noexcept {
    if (!resource || tag_of(resource)->magic != kObjectMagic) return nullptr;
    D3D11_RESOURCE_DIMENSION dimension{};
    resource->GetType(&dimension);
    switch (dimension) {
    case D3D11_RESOURCE_DIMENSION_BUFFER: return &static_cast<RBuffer*>(static_cast<ID3D11Buffer*>(resource))->state_;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D: return &static_cast<RTexture2D*>(static_cast<ID3D11Texture2D*>(resource))->state_;
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D: return &static_cast<RTexture3D*>(static_cast<ID3D11Texture3D*>(resource))->state_;
    default: return nullptr;
    }
}

namespace {
// Format, mip count, subresource count and level-0 extent of a texture.
struct TextureShape {
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    std::uint32_t mips = 1, count = 1, width = 1, height = 1, depth = 1;
};

bool texture_shape(ID3D11Resource* resource, TextureShape& shape) noexcept {
    D3D11_RESOURCE_DIMENSION dimension{};
    resource->GetType(&dimension);
    if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        const auto& d = static_cast<RTexture2D*>(static_cast<ID3D11Texture2D*>(resource))->desc_;
        shape = {d.Format, d.MipLevels, d.MipLevels * d.ArraySize, d.Width, d.Height, 1};
        return d.MipLevels != 0;
    }
    if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
        const auto& d = static_cast<RTexture3D*>(static_cast<ID3D11Texture3D*>(resource))->desc_;
        shape = {d.Format, d.MipLevels, d.MipLevels, d.Width, d.Height, d.Depth};
        return d.MipLevels != 0;
    }
    return false;
}
} // namespace

bool subresource_layout(ID3D11Resource* resource, std::uint32_t subresource, Layout& layout, std::uint32_t* offset,
                        std::uint32_t* total) noexcept {
    D3D11_RESOURCE_DIMENSION dimension{};
    resource->GetType(&dimension);
    if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        const auto& desc = static_cast<RBuffer*>(static_cast<ID3D11Buffer*>(resource))->desc_;
        layout = {desc.ByteWidth, 1, 1, desc.ByteWidth};
        if (offset) *offset = 0;
        if (total) *total = desc.ByteWidth;
        return subresource == 0;
    }
    TextureShape shape;
    if (!texture_shape(resource, shape) || subresource >= shape.count) return false;
    std::uint32_t at = 0;
    for (std::uint32_t s = 0; s < shape.count; ++s) {
        const std::uint32_t mip = s % shape.mips;
        const Layout l = layout_of(shape.format, mip_extent(shape.width, mip), mip_extent(shape.height, mip),
                                   mip_extent(shape.depth, mip));
        if (s == subresource) { layout = l; if (offset) *offset = at; }
        at += l.bytes();
    }
    if (total) *total = at;
    return true;
}

namespace {
// Copies rows x slices from a caller's pitches into tight rows.
void pack_rows(std::uint8_t* out, const std::uint8_t* source, const Layout& layout, std::uint32_t rows, std::uint32_t slices,
               std::uint32_t source_row_pitch, std::uint32_t source_depth_pitch) noexcept {
    for (std::uint32_t z = 0; z < slices; ++z)
        for (std::uint32_t y = 0; y < rows; ++y)
            std::memcpy(out + (std::size_t(z) * rows + y) * layout.row_pitch,
                        source + std::size_t(z) * source_depth_pitch + std::size_t(y) * source_row_pitch, layout.row_bytes);
}

void write_update(std::uint16_t context, std::uint32_t handle, std::uint32_t subresource, const std::uint32_t* box,
                  const Layout& layout, std::uint32_t rows, std::uint32_t slices, const std::uint8_t* source,
                  std::uint32_t source_row_pitch, std::uint32_t source_depth_pitch) noexcept {
    const std::uint32_t bytes = layout.row_pitch * rows * slices;
    Update update{handle, subresource, box ? 1u : 0u, layout.row_pitch, layout.row_pitch * rows, bytes, 0, 0, {}, {}};
    if (box) std::memcpy(update.box, box, sizeof(update.box));
    Writer w(Op::update_subresource, context, sizeof(update) + bytes);
    w.put(update);
    pack_rows(w.take(bytes), source, layout, rows, slices, source_row_pitch, source_depth_pitch);
    count_update(bytes);
}
} // namespace

void send_update(std::uint16_t context, ID3D11Resource* resource, std::uint32_t subresource, const D3D11_BOX* box,
                 const void* data, std::uint32_t row_pitch, std::uint32_t depth_pitch) noexcept {
    const std::uint32_t handle = handle_of(resource);
    const auto* source = static_cast<const std::uint8_t*>(data);
    const std::uint32_t chunk = chunk_bytes();
    D3D11_RESOURCE_DIMENSION dimension{};
    resource->GetType(&dimension);
    if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) {
        const auto& desc = static_cast<RBuffer*>(static_cast<ID3D11Buffer*>(resource))->desc_;
        const std::uint32_t left = box ? box->left : 0u, right = box ? box->right : desc.ByteWidth;
        if (right <= left) return;
        const std::uint32_t total = right - left;
        if (total > chunk && (desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER)) { unsupported("constant buffer update over a ring chunk"); return; }
        for (std::uint32_t at = 0; at < total; at += chunk) {
            const std::uint32_t bytes = (std::min)(chunk, total - at);
            const Layout layout{bytes, 1, 1, bytes};
            const std::uint32_t part[6] = {left + at, 0, 0, left + at + bytes, 1, 1};
            write_update(context, handle, 0, box || bytes != total ? part : nullptr, layout, 1, 1, source + at, bytes, bytes);
        }
        return;
    }
    TextureShape shape;
    if (!texture_shape(resource, shape) || subresource >= shape.count) return;
    const std::uint32_t mip = subresource % shape.mips;
    std::uint32_t region[6] = {0, 0, 0, mip_extent(shape.width, mip), mip_extent(shape.height, mip), mip_extent(shape.depth, mip)};
    if (box) { region[0] = box->left; region[1] = box->top; region[2] = box->front; region[3] = box->right; region[4] = box->bottom; region[5] = box->back; }
    if (region[3] <= region[0] || region[4] <= region[1] || region[5] <= region[2]) return;
    const Block block = format_block(shape.format);
    const Layout layout = layout_of(shape.format, region[3] - region[0], region[4] - region[1], region[5] - region[2]);
    if (layout.bytes() <= chunk) {
        write_update(context, handle, subresource, box ? region : nullptr, layout, layout.rows, layout.depth, source, row_pitch, depth_pitch);
        return;
    }
    // Row bands of whole block rows, slice by slice.
    const std::uint32_t band = (std::max)(1u, chunk / layout.row_pitch);
    for (std::uint32_t z = 0; z < layout.depth; ++z)
        for (std::uint32_t row = 0; row < layout.rows; row += band) {
            const std::uint32_t rows = (std::min)(band, layout.rows - row);
            const std::uint32_t part[6] = {region[0], region[1] + row * block.height, region[2] + z, region[3],
                                           (std::min)(region[4], region[1] + (row + rows) * block.height), region[2] + z + 1};
            write_update(context, handle, subresource, part, layout, rows, 1,
                         source + std::size_t(z) * depth_pitch + std::size_t(row) * row_pitch, row_pitch, depth_pitch);
        }
}

UINT STDMETHODCALLTYPE RQuery::GetDataSize() {
    switch (desc_.Query) {
    case D3D11_QUERY_EVENT: return sizeof(BOOL);
    case D3D11_QUERY_OCCLUSION: return sizeof(UINT64);
    case D3D11_QUERY_TIMESTAMP: return sizeof(UINT64);
    case D3D11_QUERY_TIMESTAMP_DISJOINT: return sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT);
    case D3D11_QUERY_PIPELINE_STATISTICS: return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS);
    case D3D11_QUERY_OCCLUSION_PREDICATE: return sizeof(BOOL);
    case D3D11_QUERY_SO_STATISTICS: return sizeof(D3D11_QUERY_DATA_SO_STATISTICS);
    case D3D11_QUERY_SO_OVERFLOW_PREDICATE: return sizeof(BOOL);
    default: return 0;
    }
}

namespace {

#define REMOTE_UNUSED_HR(iface, name, params) \
    HRESULT STDMETHODCALLTYPE name params override { unsupported(#iface "::" #name); return E_NOTIMPL; }

// ---- default view descriptions (GetDesc of views created without one) ---------------

// Format and view dimension only (the per-dimension mip/slice fields stay
// zero); the host creates the real view from a null desc.
template <class Desc> void default_view(ID3D11Resource* resource, Desc& desc, bool depth_view) noexcept {
    desc = {};
    D3D11_RESOURCE_DIMENSION dimension{};
    resource->GetType(&dimension);
    using Dimension = decltype(desc.ViewDimension);
    if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        const auto& d = static_cast<RTexture2D*>(static_cast<ID3D11Texture2D*>(resource))->desc_;
        desc.Format = d.Format;
        // TEXTURE2D / TEXTURE2DARRAY: 4 / 5 for SRV, RTV and UAV; 3 / 4 for DSV.
        desc.ViewDimension = static_cast<Dimension>((d.ArraySize > 1 ? 5 : 4) - (depth_view ? 1 : 0));
    } else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
        desc.Format = static_cast<RTexture3D*>(static_cast<ID3D11Texture3D*>(resource))->desc_.Format;
        desc.ViewDimension = static_cast<Dimension>(8); // TEXTURE3D
    } else {
        desc.ViewDimension = static_cast<Dimension>(1); // BUFFER
    }
}

template <class Desc> HRESULT make_view(Op op, ID3D11Resource* resource, const Desc* desc, Desc& used) noexcept {
    if (!resource) return E_INVALIDARG;
    if (desc) used = *desc;
    else default_view(resource, used, op == Op::create_dsv);
    return S_OK;
}

class RAdapter;
class RFactory;
class RSwapChain;

// ---- the device -------------------------------------------------------------------------

class RDevice final : public ID3D11Device {
public:
    RDevice(UINT flags, RAdapter* adapter) noexcept;
    ~RDevice();

    // IUnknown (shared with the IDXGIDevice1 face)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (!left) delete this;
        return left;
    }

    HRESULT STDMETHODCALLTYPE CreateBuffer(const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Buffer** out) override {
        if (!desc) return E_INVALIDARG;
        if (!out) return S_FALSE;
        auto* buffer = new (std::nothrow) RBuffer(*desc);
        if (!buffer) return E_OUTOFMEMORY;
        send_resource(Op::create_buffer, buffer, desc, sizeof(*desc), init, desc->Usage);
        *out = buffer;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D11_TEXTURE2D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out) override {
        if (!desc) return E_INVALIDARG;
        if (!out) return S_FALSE;
        D3D11_TEXTURE2D_DESC used = *desc;
        if (!used.MipLevels) used.MipLevels = full_mips(used.Width, used.Height, 1);
        auto* texture = new (std::nothrow) RTexture2D(used);
        if (!texture) return E_OUTOFMEMORY;
        send_resource(Op::create_texture2d, texture, desc, sizeof(*desc), init, desc->Usage);
        *out = texture;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateTexture3D(const D3D11_TEXTURE3D_DESC* desc, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture3D** out) override {
        if (!desc) return E_INVALIDARG;
        if (!out) return S_FALSE;
        D3D11_TEXTURE3D_DESC used = *desc;
        if (!used.MipLevels) used.MipLevels = full_mips(used.Width, used.Height, used.Depth);
        auto* texture = new (std::nothrow) RTexture3D(used);
        if (!texture) return E_OUTOFMEMORY;
        send_resource(Op::create_texture3d, texture, desc, sizeof(*desc), init, desc->Usage);
        *out = texture;
        return S_OK;
    }
    template <class Proxy, class I, class Desc> HRESULT create_view(Op op, ID3D11Resource* resource, const Desc* desc, I** out) noexcept {
        Desc used{};
        const HRESULT hr = make_view(op, resource, desc, used);
        if (FAILED(hr)) return hr;
        if (!out) return S_FALSE;
        auto* view = new (std::nothrow) Proxy(resource, used);
        if (!view) return E_OUTOFMEMORY;
        {
            Writer w(op, 0, sizeof(CreateView) + sizeof(Desc));
            w.put(CreateView{view->handle(), handle_of(resource), desc ? 1u : 0u, 0});
            Desc wire{};
            if (desc) wire = *desc;
            w.put(wire);
        }
        *out = view;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateShaderResourceView(ID3D11Resource* r, const D3D11_SHADER_RESOURCE_VIEW_DESC* d, ID3D11ShaderResourceView** o) override {
        return create_view<RSrv>(Op::create_srv, r, d, o);
    }
    HRESULT STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D11Resource* r, const D3D11_UNORDERED_ACCESS_VIEW_DESC* d, ID3D11UnorderedAccessView** o) override {
        return create_view<RUav>(Op::create_uav, r, d, o);
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTargetView(ID3D11Resource* r, const D3D11_RENDER_TARGET_VIEW_DESC* d, ID3D11RenderTargetView** o) override {
        return create_view<RRtv>(Op::create_rtv, r, d, o);
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilView(ID3D11Resource* r, const D3D11_DEPTH_STENCIL_VIEW_DESC* d, ID3D11DepthStencilView** o) override {
        return create_view<RDsv>(Op::create_dsv, r, d, o);
    }
    HRESULT STDMETHODCALLTYPE CreateInputLayout(const D3D11_INPUT_ELEMENT_DESC* elements, UINT count, const void* code, SIZE_T code_bytes,
                                                ID3D11InputLayout** out) override try {
        if (!elements || !count || !code || !code_bytes) return E_INVALIDARG;
        if (!out) return S_FALSE;
        std::vector<char> names;
        std::vector<InputElement> wire(count);
        for (UINT i = 0; i < count; ++i) {
            const auto& e = elements[i];
            wire[i] = {static_cast<std::uint32_t>(names.size()), e.SemanticIndex, static_cast<std::uint32_t>(e.Format), e.InputSlot,
                       e.AlignedByteOffset, static_cast<std::uint32_t>(e.InputSlotClass), e.InstanceDataStepRate, 0};
            const char* name = e.SemanticName ? e.SemanticName : "";
            names.insert(names.end(), name, name + std::strlen(name) + 1);
        }
        names.resize((names.size() + 7) & ~std::size_t(7), 0);
        auto* layout = new (std::nothrow) Plain<ID3D11InputLayout>;
        if (!layout) return E_OUTOFMEMORY;
        const auto bytecode = static_cast<std::uint32_t>(code_bytes);
        {
            Writer w(Op::create_input_layout, 0,
                     static_cast<std::uint32_t>(sizeof(x64remote::CreateInputLayout) + wire.size() * sizeof(InputElement) + names.size()) + bytecode);
            w.put(x64remote::CreateInputLayout{layout->handle(), count, static_cast<std::uint32_t>(names.size()), bytecode});
            w.put(wire.data(), static_cast<std::uint32_t>(wire.size() * sizeof(InputElement)));
            w.put(names.data(), static_cast<std::uint32_t>(names.size()));
            w.put(code, bytecode);
        }
        *out = layout;
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    template <class I> HRESULT create_shader(Stage stage, const void* code, SIZE_T bytes, ID3D11ClassLinkage* linkage, I** out) noexcept {
        if (!code || !bytes) return E_INVALIDARG;
        if (linkage) unsupported("shader class linkage");
        if (!out) return S_FALSE;
        if (bytes > chunk_bytes()) { unsupported("shader bytecode larger than a ring chunk"); return E_OUTOFMEMORY; }
        auto* shader = new (std::nothrow) Plain<I>;
        if (!shader) return E_OUTOFMEMORY;
        {
            Writer w(Op::create_shader, 0, sizeof(CreateShader) + static_cast<std::uint32_t>(bytes));
            w.put(CreateShader{shader->handle(), stage, static_cast<std::uint32_t>(bytes), 0});
            w.put(code, static_cast<std::uint32_t>(bytes));
        }
        *out = shader;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11VertexShader** o) override { return create_shader(vs, c, n, l, o); }
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11GeometryShader** o) override { return create_shader(gs, c, n, l, o); }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11PixelShader** o) override { return create_shader(ps, c, n, l, o); }
    HRESULT STDMETHODCALLTYPE CreateHullShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11HullShader** o) override { return create_shader(hs, c, n, l, o); }
    HRESULT STDMETHODCALLTYPE CreateDomainShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11DomainShader** o) override { return create_shader(ds, c, n, l, o); }
    HRESULT STDMETHODCALLTYPE CreateComputeShader(const void* c, SIZE_T n, ID3D11ClassLinkage* l, ID3D11ComputeShader** o) override { return create_shader(cs, c, n, l, o); }
    template <class I, class Desc> HRESULT create_state(Op op, const Desc* desc, I** out) noexcept {
        if (!desc) return E_INVALIDARG;
        if (!out) return S_FALSE;
        auto* state = new (std::nothrow) State<I, Desc>(*desc);
        if (!state) return E_OUTOFMEMORY;
        { Writer w(op, 0, 4 + sizeof(Desc)); w.u32(state->handle()); w.put(*desc); }
        *out = state;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlendState(const D3D11_BLEND_DESC* d, ID3D11BlendState** o) override { return create_state(Op::create_blend, d, o); }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilState(const D3D11_DEPTH_STENCIL_DESC* d, ID3D11DepthStencilState** o) override { return create_state(Op::create_depth_stencil, d, o); }
    HRESULT STDMETHODCALLTYPE CreateRasterizerState(const D3D11_RASTERIZER_DESC* d, ID3D11RasterizerState** o) override { return create_state(Op::create_rasterizer, d, o); }
    HRESULT STDMETHODCALLTYPE CreateSamplerState(const D3D11_SAMPLER_DESC* d, ID3D11SamplerState** o) override { return create_state(Op::create_sampler, d, o); }
    HRESULT STDMETHODCALLTYPE CreateQuery(const D3D11_QUERY_DESC* desc, ID3D11Query** out) override {
        if (!desc) return E_INVALIDARG;
        if (!out) return S_FALSE;
        auto* query = new (std::nothrow) RQuery(*desc);
        if (!query) return E_OUTOFMEMORY;
        if (query->slot_ == kNoQuerySlot) unsupported("query result storage could not grow (its shared mapping failed; see x86_remote.log)");
        { Writer w(Op::create_query, 0, sizeof(x64remote::CreateQuery)); w.put(x64remote::CreateQuery{query->handle(), static_cast<std::uint32_t>(desc->Query), desc->MiscFlags, query->slot_}); }
        *out = query;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateDeferredContext(UINT flags, ID3D11DeviceContext** out) override {
        if (flags) return E_INVALIDARG;
        if (!out) return S_FALSE;
        const unsigned index = ++deferred_contexts_;
        if (index > 0xffffu) return E_OUTOFMEMORY;
        auto* context = create_context(static_cast<std::uint16_t>(index));
        if (!context) return E_OUTOFMEMORY;
        *out = context;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckFormatSupport(DXGI_FORMAT format, UINT* support) override {
        if (!support) return E_INVALIDARG;
        *support = static_cast<unsigned>(format) < kFormats ? control().caps.format_support[format] : 0u;
        return *support ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D11_FEATURE feature, void* data, UINT size) override {
        if (!data) return E_INVALIDARG;
        const Capabilities& caps = control().caps;
        if (feature == D3D11_FEATURE_FORMAT_SUPPORT || feature == D3D11_FEATURE_FORMAT_SUPPORT2) {
            if (size != sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT)) return E_INVALIDARG;
            auto* query = static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT*>(data);
            const unsigned f = static_cast<unsigned>(query->InFormat);
            query->OutFormatSupport = f < kFormats ? (feature == D3D11_FEATURE_FORMAT_SUPPORT ? caps.format_support[f] : caps.format_support2[f]) : 0u;
            return S_OK;
        }
        const unsigned f = static_cast<unsigned>(feature);
        if (f >= kFeatures || !caps.features[f].bytes) { unsupported("CheckFeatureSupport of a feature not in the host snapshot"); return E_INVALIDARG; }
        if (caps.features[f].bytes != size) return E_INVALIDARG;
        if (FAILED(caps.features[f].hr)) return caps.features[f].hr;
        std::memcpy(data, caps.features[f].data, size);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* s, void* d) override { return private_.get(g, s, d); }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT s, const void* d) override { return private_.set(g, s, d, nullptr); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* o) override { return private_.set(g, 0, nullptr, o); }
    D3D_FEATURE_LEVEL STDMETHODCALLTYPE GetFeatureLevel() override { return static_cast<D3D_FEATURE_LEVEL>(control().caps.feature_level); }
    UINT STDMETHODCALLTYPE GetCreationFlags() override { return flags_; }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return link_removed(); }
    void STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext** out) override {
        if (!out) return;
        *out = immediate_.Get();
        (*out)->AddRef();
    }
    HRESULT STDMETHODCALLTYPE SetExceptionMode(UINT) override { unsupported("ID3D11Device::SetExceptionMode"); return E_NOTIMPL; }
    UINT STDMETHODCALLTYPE GetExceptionMode() override { return 0; }

    REMOTE_UNUSED_HR(ID3D11Device, CreateTexture1D, (const D3D11_TEXTURE1D_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Texture1D**))
    REMOTE_UNUSED_HR(ID3D11Device, CreateGeometryShaderWithStreamOutput,
                     (const void*, SIZE_T, const D3D11_SO_DECLARATION_ENTRY*, UINT, const UINT*, UINT, UINT, ID3D11ClassLinkage*, ID3D11GeometryShader**))
    REMOTE_UNUSED_HR(ID3D11Device, CreateClassLinkage, (ID3D11ClassLinkage**))
    REMOTE_UNUSED_HR(ID3D11Device, CreatePredicate, (const D3D11_QUERY_DESC*, ID3D11Predicate**))
    REMOTE_UNUSED_HR(ID3D11Device, CreateCounter, (const D3D11_COUNTER_DESC*, ID3D11Counter**))
    REMOTE_UNUSED_HR(ID3D11Device, OpenSharedResource, (HANDLE, REFIID, void**))
    REMOTE_UNUSED_HR(ID3D11Device, CheckMultisampleQualityLevels, (DXGI_FORMAT, UINT, UINT*))
    void STDMETHODCALLTYPE CheckCounterInfo(D3D11_COUNTER_INFO* info) override {
        unsupported("ID3D11Device::CheckCounterInfo");
        if (info) *info = {};
    }
    REMOTE_UNUSED_HR(ID3D11Device, CheckCounter, (const D3D11_COUNTER_DESC*, D3D11_COUNTER_TYPE*, UINT*, LPSTR, UINT*, LPSTR, UINT*, LPSTR, UINT*))

    // The DXGI face of the device (COM identity shared with the device).
    class Dxgi final : public IDXGIDevice1 {
    public:
        RDevice* owner = nullptr;
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override { return owner->QueryInterface(riid, out); }
        ULONG STDMETHODCALLTYPE AddRef() override { return owner->AddRef(); }
        ULONG STDMETHODCALLTYPE Release() override { return owner->Release(); }
        HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT s, const void* d) override { return owner->private_.set(g, s, d, nullptr); }
        HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* o) override { return owner->private_.set(g, 0, nullptr, o); }
        HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* s, void* d) override { return owner->private_.get(g, s, d); }
        HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** out) override;
        HRESULT STDMETHODCALLTYPE GetAdapter(IDXGIAdapter** adapter) override;
        REMOTE_UNUSED_HR(IDXGIDevice, CreateSurface, (const DXGI_SURFACE_DESC*, UINT, DXGI_USAGE, const DXGI_SHARED_RESOURCE*, IDXGISurface**))
        REMOTE_UNUSED_HR(IDXGIDevice, QueryResourceResidency, (IUnknown* const*, DXGI_RESIDENCY*, UINT))
        HRESULT STDMETHODCALLTYPE SetGPUThreadPriority(INT) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE GetGPUThreadPriority(INT* priority) override { if (priority) *priority = 0; return S_OK; }
        HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT latency) override {
            if (latency > 16) return DXGI_ERROR_INVALID_CALL;
            owner->frame_latency_ = latency ? latency : 3;
            Writer w(Op::set_frame_latency, 0, 4);
            w.u32(latency);
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* latency) override {
            if (!latency) return E_INVALIDARG;
            *latency = owner->frame_latency_;
            return S_OK;
        }
    };

    ComPtr<RAdapter> adapter_;

private:
    friend class Dxgi;
    static UINT full_mips(UINT w, UINT h, UINT d) noexcept {
        UINT mips = 1;
        while (w > 1 || h > 1 || d > 1) { w >>= 1; h >>= 1; d >>= 1; ++mips; }
        return mips;
    }
    template <class R> void send_resource(Op op, R* resource, const void* desc, std::uint32_t desc_bytes,
                                          const D3D11_SUBRESOURCE_DATA* init, D3D11_USAGE usage) noexcept;

    std::atomic<ULONG> references_{1};
    UINT flags_;
    UINT frame_latency_ = 3;
    std::atomic<unsigned> deferred_contexts_{0};
    PrivateData private_;
    Dxgi dxgi_;
    ComPtr<ID3D11DeviceContext1> immediate_;
};

RDevice* g_device = nullptr;

// Creation record plus initial data: packed into the record when it fits a
// chunk, else the resource is created empty (IMMUTABLE as DEFAULT) and each
// subresource follows as update_subresource records.
template <class R> void RDevice::send_resource(Op op, R* resource, const void* desc, std::uint32_t desc_bytes,
                                               const D3D11_SUBRESOURCE_DATA* init, D3D11_USAGE usage) noexcept {
    std::uint32_t subresources = 0, total = 0;
    if (init) {
        Layout layout;
        subresource_layout(resource, 0, layout, nullptr, &total);
        D3D11_RESOURCE_DIMENSION dimension{};
        resource->GetType(&dimension);
        if (dimension == D3D11_RESOURCE_DIMENSION_BUFFER) subresources = 1;
        else while (subresource_layout(resource, subresources, layout, nullptr, nullptr)) ++subresources;
    }
    const std::uint32_t chunk = chunk_bytes();
    const bool inline_data = init && total + subresources * sizeof(SubresourceData) <= chunk;
    if (init && !inline_data && usage == D3D11_USAGE_DYNAMIC) unsupported("dynamic resource initial data larger than a ring chunk");
    {
        const std::uint32_t payload = inline_data ? total + subresources * static_cast<std::uint32_t>(sizeof(SubresourceData)) : 0u;
        Writer w(op, 0, sizeof(CreateResource) + desc_bytes + payload);
        w.put(CreateResource{resource->handle(), inline_data ? 1u : 0u, inline_data ? subresources : 0u,
                             init && !inline_data && usage == D3D11_USAGE_IMMUTABLE ? 1u : 0u});
        w.put(desc, desc_bytes);
        if (inline_data) {
            for (std::uint32_t s = 0; s < subresources; ++s) {
                Layout layout;
                subresource_layout(resource, s, layout, nullptr, nullptr);
                w.put(SubresourceData{layout.row_pitch, layout.row_pitch * layout.rows, layout.bytes(), 0});
            }
            for (std::uint32_t s = 0; s < subresources; ++s) {
                Layout layout;
                subresource_layout(resource, s, layout, nullptr, nullptr);
                const auto* source = static_cast<const std::uint8_t*>(init[s].pSysMem);
                std::uint8_t* out = w.take(layout.bytes());
                for (std::uint32_t z = 0; z < layout.depth; ++z)
                    for (std::uint32_t y = 0; y < layout.rows; ++y)
                        std::memcpy(out + (std::size_t(z) * layout.rows + y) * layout.row_pitch,
                                    source + std::size_t(z) * init[s].SysMemSlicePitch + std::size_t(y) * init[s].SysMemPitch,
                                    layout.row_bytes);
            }
        }
    }
    count_init(total);
    if (init && !inline_data && usage != D3D11_USAGE_DYNAMIC)
        for (std::uint32_t s = 0; s < subresources; ++s)
            send_update(0, resource, s, nullptr, init[s].pSysMem, init[s].SysMemPitch, init[s].SysMemSlicePitch);
}

// ---- adapter, factory, swap chain -----------------------------------------------------------

template <class I> class DxgiObject : public I {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --references_;
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT s, const void* d) override { return private_.set(g, s, d, nullptr); }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* o) override { return private_.set(g, 0, nullptr, o); }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* s, void* d) override { return private_.get(g, s, d); }
    virtual ~DxgiObject() = default;

protected:
    HRESULT answer(void** out, bool known) noexcept {
        if (!out) return E_POINTER;
        if (!known) { *out = nullptr; return E_NOINTERFACE; }
        *out = static_cast<I*>(this);
        AddRef();
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
    PrivateData private_;
};

class RFactory final : public DxgiObject<IDXGIFactory5> {
public:
    explicit RFactory(RAdapter* adapter) noexcept : adapter_(adapter) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        return answer(out, riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory) ||
                                     riid == __uuidof(IDXGIFactory1) || riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory3) ||
                                     riid == __uuidof(IDXGIFactory4) || riid == __uuidof(IDXGIFactory5));
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID, void** out) override { if (out) *out = nullptr; return E_NOINTERFACE; }
    HRESULT STDMETHODCALLTYPE EnumAdapters(UINT index, IDXGIAdapter** out) override;
    HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT index, IDXGIAdapter1** out) override;
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND, UINT) override { return S_OK; } // the host associates its own window
    HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND* window) override { if (window) *window = nullptr; return S_OK; }
    REMOTE_UNUSED_HR(IDXGIFactory, CreateSwapChain, (IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**))
    REMOTE_UNUSED_HR(IDXGIFactory, CreateSoftwareAdapter, (HMODULE, IDXGIAdapter**))
    BOOL STDMETHODCALLTYPE IsCurrent() override { return TRUE; }
    BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* desc,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput* output,
                                                     IDXGISwapChain1** out) override;
    REMOTE_UNUSED_HR(IDXGIFactory2, CreateSwapChainForCoreWindow, (IUnknown*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**))
    REMOTE_UNUSED_HR(IDXGIFactory2, GetSharedResourceAdapterLuid, (HANDLE, LUID*))
    REMOTE_UNUSED_HR(IDXGIFactory2, RegisterStereoStatusWindow, (HWND, UINT, DWORD*))
    REMOTE_UNUSED_HR(IDXGIFactory2, RegisterStereoStatusEvent, (HANDLE, DWORD*))
    void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD) override {}
    REMOTE_UNUSED_HR(IDXGIFactory2, RegisterOcclusionStatusWindow, (HWND, UINT, DWORD*))
    REMOTE_UNUSED_HR(IDXGIFactory2, RegisterOcclusionStatusEvent, (HANDLE, DWORD*))
    void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD) override {}
    REMOTE_UNUSED_HR(IDXGIFactory2, CreateSwapChainForComposition, (IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**))
    UINT STDMETHODCALLTYPE GetCreationFlags() override { return 0; }
    REMOTE_UNUSED_HR(IDXGIFactory4, EnumAdapterByLuid, (LUID, REFIID, void**))
    REMOTE_UNUSED_HR(IDXGIFactory4, EnumWarpAdapter, (REFIID, void**))
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(DXGI_FEATURE feature, void* data, UINT size) override {
        if (feature != DXGI_FEATURE_PRESENT_ALLOW_TEARING || !data || size != sizeof(BOOL)) return E_INVALIDARG;
        *static_cast<BOOL*>(data) = control().caps.tearing ? TRUE : FALSE;
        return S_OK;
    }

private:
    RAdapter* adapter_; // the adapter owns the factory
};

class RAdapter final : public DxgiObject<IDXGIAdapter3> {
public:
    RAdapter() noexcept { factory_.Attach(new (std::nothrow) RFactory(this)); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        return answer(out, riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIAdapter) ||
                                     riid == __uuidof(IDXGIAdapter1) || riid == __uuidof(IDXGIAdapter2) || riid == __uuidof(IDXGIAdapter3));
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** out) override {
        if (!factory_) { if (out) *out = nullptr; return E_OUTOFMEMORY; }
        return factory_->QueryInterface(riid, out);
    }
    HRESULT STDMETHODCALLTYPE EnumOutputs(UINT, IDXGIOutput** out) override {
        unsupported("IDXGIAdapter::EnumOutputs");
        if (out) *out = nullptr;
        return DXGI_ERROR_NOT_FOUND;
    }
    template <class Desc> void fill(Desc& desc) const noexcept {
        const AdapterInfo& a = control().caps.adapter;
        std::memcpy(desc.Description, a.description, sizeof(desc.Description));
        desc.VendorId = a.vendor;
        desc.DeviceId = a.device;
        desc.SubSysId = a.subsys;
        desc.Revision = a.revision;
        const auto clamp = [](std::uint64_t v) { return static_cast<SIZE_T>((std::min)(v, std::uint64_t(SIZE_MAX))); };
        desc.DedicatedVideoMemory = clamp(a.dedicated_video);
        desc.DedicatedSystemMemory = clamp(a.dedicated_system);
        desc.SharedSystemMemory = clamp(a.shared_system);
        desc.AdapterLuid.LowPart = a.luid_low;
        desc.AdapterLuid.HighPart = a.luid_high;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_ADAPTER_DESC* desc) override { if (!desc) return E_INVALIDARG; *desc = {}; fill(*desc); return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_ADAPTER_DESC1* desc) override {
        if (!desc) return E_INVALIDARG;
        *desc = {};
        fill(*desc);
        desc->Flags = control().caps.adapter.flags;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc2(DXGI_ADAPTER_DESC2* desc) override {
        if (!desc) return E_INVALIDARG;
        *desc = {};
        fill(*desc);
        desc->Flags = control().caps.adapter.flags;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CheckInterfaceSupport(REFGUID guid, LARGE_INTEGER* version) override {
        if (guid != __uuidof(IDXGIDevice) && guid != __uuidof(ID3D10Device)) return DXGI_ERROR_UNSUPPORTED;
        if (version) version->QuadPart = control().caps.adapter.umd_version;
        return S_OK;
    }
    REMOTE_UNUSED_HR(IDXGIAdapter3, RegisterHardwareContentProtectionTeardownStatusEvent, (HANDLE, DWORD*))
    void STDMETHODCALLTYPE UnregisterHardwareContentProtectionTeardownStatus(DWORD) override {}
    HRESULT STDMETHODCALLTYPE QueryVideoMemoryInfo(UINT node, DXGI_MEMORY_SEGMENT_GROUP group, DXGI_QUERY_VIDEO_MEMORY_INFO* info) override {
        if (!info || node) return E_INVALIDARG;
        const Control& c = control();
        MemoryInfo m{};
        for (int attempt = 0; attempt < 64; ++attempt) {
            const long before = c.stats_sequence;
            if (before & 1) { YieldProcessor(); continue; }
            MemoryBarrier();
            m = group == DXGI_MEMORY_SEGMENT_GROUP_LOCAL ? c.local : c.non_local;
            MemoryBarrier();
            if (c.stats_sequence == before) break;
        }
        info->Budget = m.budget;
        info->CurrentUsage = m.usage;
        info->AvailableForReservation = m.available_reservation;
        info->CurrentReservation = m.reservation;
        return S_OK;
    }
    REMOTE_UNUSED_HR(IDXGIAdapter3, SetVideoMemoryReservation, (UINT, DXGI_MEMORY_SEGMENT_GROUP, UINT64))
    REMOTE_UNUSED_HR(IDXGIAdapter3, RegisterVideoMemoryBudgetChangeNotificationEvent, (HANDLE, DWORD*))
    void STDMETHODCALLTYPE UnregisterVideoMemoryBudgetChangeNotification(DWORD) override {}

private:
    ComPtr<RFactory> factory_;
};

HRESULT STDMETHODCALLTYPE RFactory::EnumAdapters(UINT index, IDXGIAdapter** out) {
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    if (index) return DXGI_ERROR_NOT_FOUND;
    adapter_->AddRef();
    *out = adapter_;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RFactory::EnumAdapters1(UINT index, IDXGIAdapter1** out) {
    if (!out) return E_INVALIDARG;
    *out = nullptr;
    if (index) return DXGI_ERROR_NOT_FOUND;
    adapter_->AddRef();
    *out = adapter_;
    return S_OK;
}

class RSwapChain final : public DxgiObject<IDXGISwapChain1> {
public:
    RSwapChain(HWND window, const DXGI_SWAP_CHAIN_DESC1& desc) noexcept : window_(window), desc_(desc) {
        handle_ = allocate_handle();
        back_handle_ = allocate_handle();
        CreateSwapChain wire{handle_, back_handle_, reinterpret_cast<std::uintptr_t>(window), desc.Width, desc.Height,
                             static_cast<std::uint32_t>(desc.Format), desc.Stereo ? 1u : 0u, desc.SampleDesc.Count,
                             desc.SampleDesc.Quality, desc.BufferUsage, desc.BufferCount, static_cast<std::uint32_t>(desc.Scaling),
                             static_cast<std::uint32_t>(desc.SwapEffect), static_cast<std::uint32_t>(desc.AlphaMode), desc.Flags};
        { Writer w(Op::create_swap_chain, 0, sizeof(wire)); w.put(wire); }
        back_.Attach(new (std::nothrow) RTexture2D(back_desc(), back_handle_));
    }
    ~RSwapChain() override {
        back_.Reset();
        for (const std::uint32_t handle : {back_handle_, handle_}) {
            { Writer w(Op::destroy, 0, 4); w.u32(handle); }
            free_handle(handle);
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        return answer(out, riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
                                     riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1));
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** out) override;
    HRESULT STDMETHODCALLTYPE Present(UINT interval, UINT flags) override {
        if (flags & DXGI_PRESENT_TEST) return link_removed() ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
        const std::uint64_t serial = ++presents_;
        {
            Writer w(Op::present, 0, 24);
            w.u32(handle_); w.u32(interval); w.u32(flags); w.u32(0); w.put(serial);
        }
        publish();
        report_frame();
        wait_presents(serial);
        check_host();
        return link_removed() ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index, REFIID riid, void** out) override {
        if (!out) return E_INVALIDARG;
        *out = nullptr;
        if (index) unsupported("IDXGISwapChain::GetBuffer of a buffer other than 0");
        return back_ ? back_->QueryInterface(riid, out) : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fullscreen, IDXGIOutput*) override {
        if (fullscreen) { unsupported("exclusive fullscreen"); return DXGI_ERROR_UNSUPPORTED; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fullscreen, IDXGIOutput** output) override {
        if (fullscreen) *fullscreen = FALSE;
        if (output) *output = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* desc) override {
        if (!desc) return E_INVALIDARG;
        *desc = {};
        desc->BufferDesc.Width = desc_.Width;
        desc->BufferDesc.Height = desc_.Height;
        desc->BufferDesc.Format = desc_.Format;
        desc->SampleDesc = desc_.SampleDesc;
        desc->BufferUsage = desc_.BufferUsage;
        desc->BufferCount = desc_.BufferCount;
        desc->OutputWindow = window_;
        desc->Windowed = TRUE;
        desc->SwapEffect = desc_.SwapEffect;
        desc->Flags = desc_.Flags;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) override {
        if (back_ && back_->references() > 1) {
            // Identical result: DXGI refuses this call the same way.
            remote_warning("ResizeBuffers while the back buffer is still referenced (refused, as DXGI does)");
            return DXGI_ERROR_INVALID_CALL;
        }
        RECT client{};
        if ((!width || !height) && GetClientRect(window_, &client)) {
            if (!width) width = static_cast<UINT>(client.right - client.left);
            if (!height) height = static_cast<UINT>(client.bottom - client.top);
        }
        if (count) desc_.BufferCount = count;
        if (format != DXGI_FORMAT_UNKNOWN) desc_.Format = format;
        desc_.Width = (std::max)(1u, width);
        desc_.Height = (std::max)(1u, height);
        desc_.Flags = flags;
        {
            Writer w(Op::resize_buffers, 0, 24);
            w.u32(handle_); w.u32(count); w.u32(desc_.Width); w.u32(desc_.Height); w.u32(format); w.u32(flags);
        }
        if (back_) back_->desc_ = back_desc();
        return link_removed() ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
    }
    REMOTE_UNUSED_HR(IDXGISwapChain, ResizeTarget, (const DXGI_MODE_DESC*))
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** output) override {
        unsupported("IDXGISwapChain::GetContainingOutput");
        if (output) *output = nullptr;
        return DXGI_ERROR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* stats) override {
        if (!stats) return E_INVALIDARG;
        const Control& c = control();
        for (int attempt = 0; attempt < 64; ++attempt) {
            const long before = c.stats_sequence;
            if (before & 1) { YieldProcessor(); continue; }
            MemoryBarrier();
            stats->PresentCount = c.present_count;
            stats->PresentRefreshCount = c.present_refresh_count;
            stats->SyncRefreshCount = c.sync_refresh_count;
            stats->SyncQPCTime.QuadPart = c.sync_qpc_time;
            stats->SyncGPUTime.QuadPart = c.sync_gpu_time;
            MemoryBarrier();
            if (c.stats_sequence == before) return stats->PresentCount ? S_OK : DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
        }
        return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
    }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* count) override {
        if (!count) return E_INVALIDARG;
        *count = static_cast<UINT>(presents_);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* desc) override { if (!desc) return E_INVALIDARG; *desc = desc_; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* desc) override {
        if (!desc) return E_INVALIDARG;
        *desc = {};
        desc->Windowed = TRUE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* window) override { if (!window) return E_INVALIDARG; *window = window_; return S_OK; }
    REMOTE_UNUSED_HR(IDXGISwapChain1, GetCoreWindow, (REFIID, void**))
    REMOTE_UNUSED_HR(IDXGISwapChain1, Present1, (UINT, UINT, const DXGI_PRESENT_PARAMETERS*))
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** output) override { if (output) *output = nullptr; return S_OK; }
    REMOTE_UNUSED_HR(IDXGISwapChain1, SetBackgroundColor, (const DXGI_RGBA*))
    REMOTE_UNUSED_HR(IDXGISwapChain1, GetBackgroundColor, (DXGI_RGBA*))
    REMOTE_UNUSED_HR(IDXGISwapChain1, SetRotation, (DXGI_MODE_ROTATION))
    REMOTE_UNUSED_HR(IDXGISwapChain1, GetRotation, (DXGI_MODE_ROTATION*))

private:
    D3D11_TEXTURE2D_DESC back_desc() const noexcept {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = desc_.Width;
        d.Height = desc_.Height;
        d.MipLevels = d.ArraySize = 1;
        d.Format = desc_.Format;
        d.SampleDesc = desc_.SampleDesc;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = ((desc_.BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT) ? D3D11_BIND_RENDER_TARGET : 0u) |
                      ((desc_.BufferUsage & DXGI_USAGE_SHADER_INPUT) ? D3D11_BIND_SHADER_RESOURCE : 0u);
        return d;
    }
    HWND window_;
    DXGI_SWAP_CHAIN_DESC1 desc_;
    std::uint32_t handle_ = 0, back_handle_ = 0;
    ComPtr<RTexture2D> back_;
    std::uint64_t presents_ = 0;
};

HRESULT STDMETHODCALLTYPE RFactory::CreateSwapChainForHwnd(IUnknown* device, HWND window, const DXGI_SWAP_CHAIN_DESC1* desc,
                                                           const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen, IDXGIOutput*,
                                                           IDXGISwapChain1** out) {
    if (!device || !window || !desc || !out) return E_INVALIDARG;
    if (fullscreen && !fullscreen->Windowed) unsupported("exclusive fullscreen swap chain");
    DXGI_SWAP_CHAIN_DESC1 used = *desc;
    RECT client{};
    if ((!used.Width || !used.Height) && GetClientRect(window, &client)) {
        if (!used.Width) used.Width = static_cast<UINT>(client.right);
        if (!used.Height) used.Height = static_cast<UINT>(client.bottom);
    }
    auto* chain = new (std::nothrow) RSwapChain(window, used);
    if (!chain) return E_OUTOFMEMORY;
    *out = chain;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RSwapChain::GetParent(REFIID riid, void** out) {
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIDevice> dxgi;
    if (!g_device || FAILED(g_device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter))) {
        if (out) *out = nullptr;
        return E_FAIL;
    }
    return adapter->GetParent(riid, out);
}

HRESULT STDMETHODCALLTYPE RSwapChain::GetDevice(REFIID riid, void** out) {
    if (!g_device) { if (out) *out = nullptr; return E_FAIL; }
    return g_device->QueryInterface(riid, out);
}

RDevice::RDevice(UINT flags, RAdapter* adapter) noexcept : adapter_(adapter), flags_(flags) {
    dxgi_.owner = this;
    immediate_.Attach(create_context(0));
}

RDevice::~RDevice() {
    immediate_.Reset();
    if (g_device == this) g_device = nullptr;
}

HRESULT STDMETHODCALLTYPE RDevice::QueryInterface(REFIID riid, void** out) {
    if (!out) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Device)) {
        *out = static_cast<ID3D11Device*>(this);
    } else if (riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDevice) || riid == __uuidof(IDXGIDevice1)) {
        *out = static_cast<IDXGIDevice1*>(&dxgi_);
    } else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE RDevice::Dxgi::GetParent(REFIID riid, void** out) {
    return owner->adapter_ ? owner->adapter_->QueryInterface(riid, out) : E_FAIL;
}

HRESULT STDMETHODCALLTYPE RDevice::Dxgi::GetAdapter(IDXGIAdapter** adapter) {
    if (!adapter) return E_INVALIDARG;
    *adapter = owner->adapter_.Get();
    if (!*adapter) return E_FAIL;
    (*adapter)->AddRef();
    return S_OK;
}

} // namespace

bool dlss_caps(DlssCaps& out) noexcept {
    out = {};
    if (!remote_active()) {
        strncpy_s(out.reason, "the 64-bit render host is off", _TRUNCATE);
        return false;
    }
    const Capabilities& caps = control().caps;
    out.available = caps.dlss_available != 0;
    std::memcpy(out.reason, caps.dlss_reason, sizeof(out.reason));
    out.reason[sizeof(out.reason) - 1] = 0;
    return out.available;
}

bool host_presented() noexcept {
    return remote_active() && control().presents_done >= 1;
}

bool dlss_render_size(UINT out_w, UINT out_h, int mode, UINT& render_w, UINT& render_h) noexcept try {
    render_w = render_h = 0;
    if (!remote_active() || !control().caps.dlss_available) return false;
    struct Entry { UINT out_w, out_h; int mode; UINT render_w, render_h; };
    static std::mutex mutex;
    static std::vector<Entry> cache;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (const Entry& entry : cache)
            if (entry.out_w == out_w && entry.out_h == out_h && entry.mode == mode) {
                render_w = entry.render_w;
                render_h = entry.render_h;
                return render_w && render_h;
            }
    }
    const std::uint64_t serial = next_reply_serial();
    { Writer w(Op::dlss_query, 0, sizeof(DlssQuery)); w.put(DlssQuery{out_w, out_h, mode, 0, serial}); }
    const HRESULT hr = wait_reply(serial);
    const UINT width = SUCCEEDED(hr) ? control().reply_values[0] : 0u;
    const UINT height = SUCCEEDED(hr) ? control().reply_values[1] : 0u;
    end_reply();
    if (FAILED(hr) || !width || !height) {
        log("dlss_render_size %ux%u mode %d failed: %08lx", out_w, out_h, mode, static_cast<unsigned long>(hr));
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    cache.push_back({out_w, out_h, mode, width, height});
    render_w = width;
    render_h = height;
    return true;
} catch (...) {
    return false;
}

ID3D11Device* remote_device_add_ref() noexcept {
    if (g_device) g_device->AddRef();
    return g_device;
}

HRESULT create_remote_device(HWND game_window, UINT flags, ID3D11Device** device, D3D_FEATURE_LEVEL* level,
                             ID3D11DeviceContext** context) noexcept {
    if (!device || !context) return E_INVALIDARG;
    *device = nullptr;
    *context = nullptr;
    char error[256]{};
    if (!link_start(game_window, flags, error, sizeof(error))) {
        log("remote device unavailable: %s", error);
        owned::bridge_report_event("x64_remote_start_failed", error);
        remote_start_failed(error);
        return DXGI_ERROR_UNSUPPORTED;
    }
    owned::retained_publication_attach(); // the host's retained-scene replica (S2d)
    ComPtr<RAdapter> adapter;
    adapter.Attach(new (std::nothrow) RAdapter);
    auto* created = adapter ? new (std::nothrow) RDevice(flags, adapter.Get()) : nullptr;
    if (!created) return E_OUTOFMEMORY;
    g_device = created;
    if (level) *level = created->GetFeatureLevel();
    created->GetImmediateContext(context);
    *device = created;
    return S_OK;
}

} // namespace x64remote
