// Remote immediate/deferred context proxy: each call becomes a record; the
// bound state is mirrored here (holding references, as the runtime's
// bindings do) so every *Get* call is answered without a round trip.
#include "x64_remote_objects.h"
#include "x64_remote_device.h"

#include <cstdio>

namespace x64remote {
namespace {

constexpr UINT kCbSlots = D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT;
constexpr UINT kSrvSlots = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
constexpr UINT kSamplerSlots = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
constexpr UINT kUavSlots = D3D11_1_UAV_SLOT_COUNT;
constexpr UINT kVbSlots = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
constexpr UINT kViewports = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
constexpr UINT kRtvSlots = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

struct StageBindings {
    ComPtr<ID3D11DeviceChild> shader;
    ComPtr<ID3D11Buffer> cbs[kCbSlots];
    UINT cb_first[kCbSlots]{};
    UINT cb_num[kCbSlots]{};
    ComPtr<ID3D11ShaderResourceView> srvs[kSrvSlots];
    ComPtr<ID3D11SamplerState> samplers[kSamplerSlots];
};

struct Bindings {
    StageBindings stage[stage_count];
    ComPtr<ID3D11UnorderedAccessView> uavs[kUavSlots];
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vbs[kVbSlots];
    UINT strides[kVbSlots]{};
    UINT offsets[kVbSlots]{};
    ComPtr<ID3D11Buffer> ib;
    DXGI_FORMAT ib_format = DXGI_FORMAT_UNKNOWN;
    UINT ib_offset = 0;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ComPtr<ID3D11RasterizerState> rasterizer;
    D3D11_VIEWPORT viewports[kViewports]{};
    UINT viewport_count = 0;
    D3D11_RECT scissors[kViewports]{};
    UINT scissor_count = 0;
    ComPtr<ID3D11RenderTargetView> rtvs[kRtvSlots];
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11BlendState> blend;
    FLOAT blend_factor[4]{1, 1, 1, 1};
    UINT sample_mask = 0xffffffffu;
    ComPtr<ID3D11DepthStencilState> depth_stencil;
    UINT stencil_ref = 0;
};

// Clamps [start, start + count) to N slots; returns the usable count.
template <std::size_t N> UINT clamp_slots(UINT start, UINT count) noexcept {
    return start >= N ? 0u : (std::min)(count, static_cast<UINT>(N - start));
}

template <class T, std::size_t N> void bind(ComPtr<T> (&slots)[N], UINT start, UINT count, T* const* items) noexcept {
    for (UINT i = 0; i < count; ++i) slots[start + i] = items ? items[i] : nullptr;
}

template <class T, std::size_t N> void read(const ComPtr<T> (&slots)[N], UINT start, UINT count, T** out) noexcept {
    if (!out) return;
    for (UINT i = 0; i < count; ++i) {
        out[i] = start + i < N ? slots[start + i].Get() : nullptr;
        if (out[i]) out[i]->AddRef();
    }
}

template <class T> void write_handles(Writer& w, T* const* items, UINT count) noexcept {
    for (UINT i = 0; i < count; ++i) w.u32(items ? handle_of(items[i]) : 0u);
}

std::uint32_t page_round(std::uint32_t bytes) noexcept { return (bytes + 4095u) & ~4095u; }

// Unranged maps of dynamic buffers above this use a write-watched block
// (VirtualAlloc, 64 KB address granularity); smaller ones travel whole.
constexpr std::uint32_t kWatchedBytes = 16 * 1024;

std::atomic<std::uint32_t> g_prefetch_serial{0};

#define REMOTE_UNUSED(name, params) \
    void STDMETHODCALLTYPE name params override { unsupported("ID3D11DeviceContext::" #name); }

class RContext final : public Child<ID3D11DeviceContext1> {
public:
    // Immediate: index 0, no handle. Deferred: its own handle and index.
    explicit RContext(std::uint16_t index) noexcept : Child(index ? allocate_handle() : 0u, index != 0), index_(index) {
        if (index_) { Writer w(Op::create_deferred, 0, 8); w.u32(handle()); w.u32(index_); }
    }

    // ---- shader stages -------------------------------------------------------
    void set_shader(Stage stage, ID3D11DeviceChild* shader, UINT instances) noexcept {
        if (instances) unsupported("class instances on Set*Shader");
        { Writer w(Op::set_shader, index_, 8); w.u32(stage); w.u32(handle_of(shader)); }
        state_.stage[stage].shader = shader;
    }
    template <class T> void get_shader(Stage stage, T** shader, UINT* instances) noexcept {
        if (instances) *instances = 0;
        if (!shader) return;
        *shader = static_cast<T*>(state_.stage[stage].shader.Get());
        if (*shader) (*shader)->AddRef();
    }
    void set_srvs(Stage stage, UINT start, UINT count, ID3D11ShaderResourceView* const* views) noexcept {
        count = clamp_slots<kSrvSlots>(start, count);
        { Writer w(Op::set_srvs, index_, 16 + 4 * count); w.put(Slots{stage, start, count, 0}); write_handles(w, views, count); }
        bind(state_.stage[stage].srvs, start, count, views);
    }
    void set_samplers(Stage stage, UINT start, UINT count, ID3D11SamplerState* const* samplers) noexcept {
        count = clamp_slots<kSamplerSlots>(start, count);
        { Writer w(Op::set_samplers, index_, 16 + 4 * count); w.put(Slots{stage, start, count, 0}); write_handles(w, samplers, count); }
        bind(state_.stage[stage].samplers, start, count, samplers);
    }
    void set_cbs(Stage stage, UINT start, UINT count, ID3D11Buffer* const* buffers) noexcept {
        count = clamp_slots<kCbSlots>(start, count);
        { Writer w(Op::set_cbs, index_, 16 + 4 * count); w.put(Slots{stage, start, count, 0}); write_handles(w, buffers, count); }
        bind(state_.stage[stage].cbs, start, count, buffers);
        for (UINT i = 0; i < count; ++i) { state_.stage[stage].cb_first[start + i] = 0; state_.stage[stage].cb_num[start + i] = 4096; }
    }
    void set_cbs1(Stage stage, UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* first, const UINT* num) noexcept {
        count = clamp_slots<kCbSlots>(start, count);
        {
            Writer w(Op::set_cbs1, index_, 16 + 12 * count);
            w.put(Slots{stage, start, count, first && num ? 1u : 0u});
            write_handles(w, buffers, count);
            for (UINT i = 0; i < count; ++i) w.u32(first && num ? first[i] : 0u);
            for (UINT i = 0; i < count; ++i) w.u32(first && num ? num[i] : 4096u);
        }
        bind(state_.stage[stage].cbs, start, count, buffers);
        for (UINT i = 0; i < count; ++i) {
            state_.stage[stage].cb_first[start + i] = first && num ? first[i] : 0;
            state_.stage[stage].cb_num[start + i] = first && num ? num[i] : 4096;
        }
    }
    void get_cbs1(Stage stage, UINT start, UINT count, ID3D11Buffer** buffers, UINT* first, UINT* num) noexcept {
        read(state_.stage[stage].cbs, start, count, buffers);
        for (UINT i = 0; i < count; ++i) {
            const bool in = start + i < kCbSlots;
            if (first) first[i] = in ? state_.stage[stage].cb_first[start + i] : 0;
            if (num) num[i] = in ? state_.stage[stage].cb_num[start + i] : 0;
        }
    }

    void STDMETHODCALLTYPE VSSetConstantBuffers(UINT s, UINT n, ID3D11Buffer* const* b) override { TimedCall timed_call; set_cbs(vs, s, n, b); }
    void STDMETHODCALLTYPE PSSetConstantBuffers(UINT s, UINT n, ID3D11Buffer* const* b) override { TimedCall timed_call; set_cbs(ps, s, n, b); }
    void STDMETHODCALLTYPE HSSetConstantBuffers(UINT s, UINT n, ID3D11Buffer* const* b) override { TimedCall timed_call; set_cbs(hs, s, n, b); }
    void STDMETHODCALLTYPE DSSetConstantBuffers(UINT s, UINT n, ID3D11Buffer* const* b) override { TimedCall timed_call; set_cbs(ds, s, n, b); }
    void STDMETHODCALLTYPE CSSetConstantBuffers(UINT s, UINT n, ID3D11Buffer* const* b) override { TimedCall timed_call; set_cbs(cs, s, n, b); }
    void STDMETHODCALLTYPE GSSetConstantBuffers(UINT s, UINT n, ID3D11Buffer* const* b) override { TimedCall timed_call; set_cbs(gs, s, n, b); }
    void STDMETHODCALLTYPE VSSetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView* const* v) override { TimedCall timed_call; set_srvs(vs, s, n, v); }
    void STDMETHODCALLTYPE PSSetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView* const* v) override { TimedCall timed_call; set_srvs(ps, s, n, v); }
    void STDMETHODCALLTYPE HSSetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView* const* v) override { TimedCall timed_call; set_srvs(hs, s, n, v); }
    void STDMETHODCALLTYPE DSSetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView* const* v) override { TimedCall timed_call; set_srvs(ds, s, n, v); }
    void STDMETHODCALLTYPE CSSetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView* const* v) override { TimedCall timed_call; set_srvs(cs, s, n, v); }
    void STDMETHODCALLTYPE GSSetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView* const* v) override { TimedCall timed_call; set_srvs(gs, s, n, v); }
    void STDMETHODCALLTYPE VSSetSamplers(UINT s, UINT n, ID3D11SamplerState* const* p) override { TimedCall timed_call; set_samplers(vs, s, n, p); }
    void STDMETHODCALLTYPE PSSetSamplers(UINT s, UINT n, ID3D11SamplerState* const* p) override { TimedCall timed_call; set_samplers(ps, s, n, p); }
    void STDMETHODCALLTYPE HSSetSamplers(UINT s, UINT n, ID3D11SamplerState* const* p) override { TimedCall timed_call; set_samplers(hs, s, n, p); }
    void STDMETHODCALLTYPE DSSetSamplers(UINT s, UINT n, ID3D11SamplerState* const* p) override { TimedCall timed_call; set_samplers(ds, s, n, p); }
    void STDMETHODCALLTYPE CSSetSamplers(UINT s, UINT n, ID3D11SamplerState* const* p) override { TimedCall timed_call; set_samplers(cs, s, n, p); }
    void STDMETHODCALLTYPE GSSetSamplers(UINT s, UINT n, ID3D11SamplerState* const* p) override { TimedCall timed_call; set_samplers(gs, s, n, p); }
    void STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader* p, ID3D11ClassInstance* const*, UINT n) override { TimedCall timed_call; set_shader(vs, p, n); }
    void STDMETHODCALLTYPE PSSetShader(ID3D11PixelShader* p, ID3D11ClassInstance* const*, UINT n) override { TimedCall timed_call; set_shader(ps, p, n); }
    void STDMETHODCALLTYPE HSSetShader(ID3D11HullShader* p, ID3D11ClassInstance* const*, UINT n) override { TimedCall timed_call; set_shader(hs, p, n); }
    void STDMETHODCALLTYPE DSSetShader(ID3D11DomainShader* p, ID3D11ClassInstance* const*, UINT n) override { TimedCall timed_call; set_shader(ds, p, n); }
    void STDMETHODCALLTYPE GSSetShader(ID3D11GeometryShader* p, ID3D11ClassInstance* const*, UINT n) override { TimedCall timed_call; set_shader(gs, p, n); }
    void STDMETHODCALLTYPE CSSetShader(ID3D11ComputeShader* p, ID3D11ClassInstance* const*, UINT n) override { TimedCall timed_call; set_shader(cs, p, n); }

    void STDMETHODCALLTYPE VSGetConstantBuffers(UINT s, UINT n, ID3D11Buffer** b) override { TimedCall timed_call; read(state_.stage[vs].cbs, s, n, b); }
    void STDMETHODCALLTYPE PSGetConstantBuffers(UINT s, UINT n, ID3D11Buffer** b) override { TimedCall timed_call; read(state_.stage[ps].cbs, s, n, b); }
    void STDMETHODCALLTYPE HSGetConstantBuffers(UINT s, UINT n, ID3D11Buffer** b) override { TimedCall timed_call; read(state_.stage[hs].cbs, s, n, b); }
    void STDMETHODCALLTYPE DSGetConstantBuffers(UINT s, UINT n, ID3D11Buffer** b) override { TimedCall timed_call; read(state_.stage[ds].cbs, s, n, b); }
    void STDMETHODCALLTYPE GSGetConstantBuffers(UINT s, UINT n, ID3D11Buffer** b) override { TimedCall timed_call; read(state_.stage[gs].cbs, s, n, b); }
    void STDMETHODCALLTYPE CSGetConstantBuffers(UINT s, UINT n, ID3D11Buffer** b) override { TimedCall timed_call; read(state_.stage[cs].cbs, s, n, b); }
    void STDMETHODCALLTYPE VSGetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView** v) override { TimedCall timed_call; read(state_.stage[vs].srvs, s, n, v); }
    void STDMETHODCALLTYPE PSGetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView** v) override { TimedCall timed_call; read(state_.stage[ps].srvs, s, n, v); }
    void STDMETHODCALLTYPE HSGetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView** v) override { TimedCall timed_call; read(state_.stage[hs].srvs, s, n, v); }
    void STDMETHODCALLTYPE DSGetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView** v) override { TimedCall timed_call; read(state_.stage[ds].srvs, s, n, v); }
    void STDMETHODCALLTYPE GSGetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView** v) override { TimedCall timed_call; read(state_.stage[gs].srvs, s, n, v); }
    void STDMETHODCALLTYPE CSGetShaderResources(UINT s, UINT n, ID3D11ShaderResourceView** v) override { TimedCall timed_call; read(state_.stage[cs].srvs, s, n, v); }
    void STDMETHODCALLTYPE VSGetSamplers(UINT s, UINT n, ID3D11SamplerState** p) override { TimedCall timed_call; read(state_.stage[vs].samplers, s, n, p); }
    void STDMETHODCALLTYPE PSGetSamplers(UINT s, UINT n, ID3D11SamplerState** p) override { TimedCall timed_call; read(state_.stage[ps].samplers, s, n, p); }
    void STDMETHODCALLTYPE HSGetSamplers(UINT s, UINT n, ID3D11SamplerState** p) override { TimedCall timed_call; read(state_.stage[hs].samplers, s, n, p); }
    void STDMETHODCALLTYPE DSGetSamplers(UINT s, UINT n, ID3D11SamplerState** p) override { TimedCall timed_call; read(state_.stage[ds].samplers, s, n, p); }
    void STDMETHODCALLTYPE GSGetSamplers(UINT s, UINT n, ID3D11SamplerState** p) override { TimedCall timed_call; read(state_.stage[gs].samplers, s, n, p); }
    void STDMETHODCALLTYPE CSGetSamplers(UINT s, UINT n, ID3D11SamplerState** p) override { TimedCall timed_call; read(state_.stage[cs].samplers, s, n, p); }
    void STDMETHODCALLTYPE VSGetShader(ID3D11VertexShader** p, ID3D11ClassInstance**, UINT* n) override { TimedCall timed_call; get_shader(vs, p, n); }
    void STDMETHODCALLTYPE PSGetShader(ID3D11PixelShader** p, ID3D11ClassInstance**, UINT* n) override { TimedCall timed_call; get_shader(ps, p, n); }
    void STDMETHODCALLTYPE HSGetShader(ID3D11HullShader** p, ID3D11ClassInstance**, UINT* n) override { TimedCall timed_call; get_shader(hs, p, n); }
    void STDMETHODCALLTYPE DSGetShader(ID3D11DomainShader** p, ID3D11ClassInstance**, UINT* n) override { TimedCall timed_call; get_shader(ds, p, n); }
    void STDMETHODCALLTYPE GSGetShader(ID3D11GeometryShader** p, ID3D11ClassInstance**, UINT* n) override { TimedCall timed_call; get_shader(gs, p, n); }
    void STDMETHODCALLTYPE CSGetShader(ID3D11ComputeShader** p, ID3D11ClassInstance**, UINT* n) override { TimedCall timed_call; get_shader(cs, p, n); }

    void STDMETHODCALLTYPE VSSetConstantBuffers1(UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* c) override { TimedCall timed_call; set_cbs1(vs, s, n, b, f, c); }
    void STDMETHODCALLTYPE PSSetConstantBuffers1(UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* c) override { TimedCall timed_call; set_cbs1(ps, s, n, b, f, c); }
    void STDMETHODCALLTYPE HSSetConstantBuffers1(UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* c) override { TimedCall timed_call; set_cbs1(hs, s, n, b, f, c); }
    void STDMETHODCALLTYPE DSSetConstantBuffers1(UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* c) override { TimedCall timed_call; set_cbs1(ds, s, n, b, f, c); }
    void STDMETHODCALLTYPE GSSetConstantBuffers1(UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* c) override { TimedCall timed_call; set_cbs1(gs, s, n, b, f, c); }
    void STDMETHODCALLTYPE CSSetConstantBuffers1(UINT s, UINT n, ID3D11Buffer* const* b, const UINT* f, const UINT* c) override { TimedCall timed_call; set_cbs1(cs, s, n, b, f, c); }
    void STDMETHODCALLTYPE VSGetConstantBuffers1(UINT s, UINT n, ID3D11Buffer** b, UINT* f, UINT* c) override { TimedCall timed_call; get_cbs1(vs, s, n, b, f, c); }
    void STDMETHODCALLTYPE PSGetConstantBuffers1(UINT s, UINT n, ID3D11Buffer** b, UINT* f, UINT* c) override { TimedCall timed_call; get_cbs1(ps, s, n, b, f, c); }
    void STDMETHODCALLTYPE HSGetConstantBuffers1(UINT s, UINT n, ID3D11Buffer** b, UINT* f, UINT* c) override { TimedCall timed_call; get_cbs1(hs, s, n, b, f, c); }
    void STDMETHODCALLTYPE DSGetConstantBuffers1(UINT s, UINT n, ID3D11Buffer** b, UINT* f, UINT* c) override { TimedCall timed_call; get_cbs1(ds, s, n, b, f, c); }
    void STDMETHODCALLTYPE GSGetConstantBuffers1(UINT s, UINT n, ID3D11Buffer** b, UINT* f, UINT* c) override { TimedCall timed_call; get_cbs1(gs, s, n, b, f, c); }
    void STDMETHODCALLTYPE CSGetConstantBuffers1(UINT s, UINT n, ID3D11Buffer** b, UINT* f, UINT* c) override { TimedCall timed_call; get_cbs1(cs, s, n, b, f, c); }

    void STDMETHODCALLTYPE CSSetUnorderedAccessViews(UINT start, UINT count, ID3D11UnorderedAccessView* const* views,
                                                     const UINT* initial) override { TimedCall timed_call;
        count = clamp_slots<kUavSlots>(start, count);
        {
            Writer w(Op::set_uavs, index_, 16 + 8 * count);
            w.put(Slots{cs, start, count, initial ? 1u : 0u});
            write_handles(w, views, count);
            for (UINT i = 0; i < count; ++i) w.u32(initial ? initial[i] : 0xffffffffu);
        }
        bind(state_.uavs, start, count, views);
    }
    void STDMETHODCALLTYPE CSGetUnorderedAccessViews(UINT s, UINT n, ID3D11UnorderedAccessView** v) override { TimedCall timed_call; read(state_.uavs, s, n, v); }

    // ---- input assembler -----------------------------------------------------
    void STDMETHODCALLTYPE IASetInputLayout(ID3D11InputLayout* layout) override { TimedCall timed_call;
        { Writer w(Op::ia_layout, index_, 4); w.u32(handle_of(layout)); }
        state_.layout = layout;
    }
    void STDMETHODCALLTYPE IAGetInputLayout(ID3D11InputLayout** layout) override { TimedCall timed_call;
        if (!layout) return;
        *layout = state_.layout.Get();
        if (*layout) (*layout)->AddRef();
    }
    void STDMETHODCALLTYPE IASetVertexBuffers(UINT start, UINT count, ID3D11Buffer* const* buffers, const UINT* strides,
                                              const UINT* offsets) override { TimedCall timed_call;
        count = clamp_slots<kVbSlots>(start, count);
        {
            Writer w(Op::ia_vertex_buffers, index_, 16 + 12 * count);
            w.put(Slots{0, start, count, 0});
            write_handles(w, buffers, count);
            for (UINT i = 0; i < count; ++i) w.u32(strides ? strides[i] : 0u);
            for (UINT i = 0; i < count; ++i) w.u32(offsets ? offsets[i] : 0u);
        }
        bind(state_.vbs, start, count, buffers);
        for (UINT i = 0; i < count; ++i) {
            state_.strides[start + i] = strides ? strides[i] : 0;
            state_.offsets[start + i] = offsets ? offsets[i] : 0;
        }
    }
    void STDMETHODCALLTYPE IAGetVertexBuffers(UINT start, UINT count, ID3D11Buffer** buffers, UINT* strides, UINT* offsets) override { TimedCall timed_call;
        read(state_.vbs, start, count, buffers);
        for (UINT i = 0; i < count; ++i) {
            const bool in = start + i < kVbSlots;
            if (strides) strides[i] = in ? state_.strides[start + i] : 0;
            if (offsets) offsets[i] = in ? state_.offsets[start + i] : 0;
        }
    }
    void STDMETHODCALLTYPE IASetIndexBuffer(ID3D11Buffer* buffer, DXGI_FORMAT format, UINT offset) override { TimedCall timed_call;
        { Writer w(Op::ia_index_buffer, index_, 12); w.u32(handle_of(buffer)); w.u32(format); w.u32(offset); }
        state_.ib = buffer;
        state_.ib_format = format;
        state_.ib_offset = offset;
    }
    void STDMETHODCALLTYPE IAGetIndexBuffer(ID3D11Buffer** buffer, DXGI_FORMAT* format, UINT* offset) override { TimedCall timed_call;
        if (buffer) { *buffer = state_.ib.Get(); if (*buffer) (*buffer)->AddRef(); }
        if (format) *format = state_.ib_format;
        if (offset) *offset = state_.ib_offset;
    }
    void STDMETHODCALLTYPE IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology) override { TimedCall timed_call;
        Writer w(Op::ia_topology, index_, 4);
        w.u32(topology);
        state_.topology = topology;
    }
    void STDMETHODCALLTYPE IAGetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY* topology) override { TimedCall timed_call; if (topology) *topology = state_.topology; }

    // ---- draws and dispatches ------------------------------------------------
    void STDMETHODCALLTYPE Draw(UINT vertices, UINT start) override { TimedCall timed_call;
        Writer w(Op::draw, index_, 8); w.u32(vertices); w.u32(start);
    }
    void STDMETHODCALLTYPE DrawIndexed(UINT indices, UINT start, INT base) override { TimedCall timed_call;
        Writer w(Op::draw_indexed, index_, 12); w.u32(indices); w.u32(start); w.put(base);
    }
    void STDMETHODCALLTYPE DrawInstanced(UINT vertices, UINT instances, UINT start, UINT start_instance) override { TimedCall timed_call;
        Writer w(Op::draw_instanced, index_, 16); w.u32(vertices); w.u32(instances); w.u32(start); w.u32(start_instance);
    }
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT indices, UINT instances, UINT start, INT base, UINT start_instance) override { TimedCall timed_call;
        Writer w(Op::draw_indexed_instanced, index_, 20);
        w.u32(indices); w.u32(instances); w.u32(start); w.put(base); w.u32(start_instance);
    }
    void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer* args, UINT offset) override { TimedCall timed_call;
        Writer w(Op::draw_instanced_indirect, index_, 8); w.u32(handle_of(args)); w.u32(offset);
    }
    void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer* args, UINT offset) override { TimedCall timed_call;
        Writer w(Op::draw_indexed_instanced_indirect, index_, 8); w.u32(handle_of(args)); w.u32(offset);
    }
    void STDMETHODCALLTYPE Dispatch(UINT x, UINT y, UINT z) override { TimedCall timed_call;
        Writer w(Op::dispatch, index_, 12); w.u32(x); w.u32(y); w.u32(z);
    }
    void STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer* args, UINT offset) override { TimedCall timed_call;
        Writer w(Op::dispatch_indirect, index_, 8); w.u32(handle_of(args)); w.u32(offset);
    }

    // ---- queries ---------------------------------------------------------------
    static RQuery* query_of(ID3D11Asynchronous* async) noexcept {
        return async && tag_of(async)->magic == kObjectMagic ? static_cast<RQuery*>(static_cast<ID3D11Query*>(async)) : nullptr;
    }
    void STDMETHODCALLTYPE Begin(ID3D11Asynchronous* async) override { TimedCall timed_call;
        RQuery* query = query_of(async);
        if (!query) return;
        Writer w(Op::query_begin, index_, 4);
        w.u32(query->handle());
    }
    void STDMETHODCALLTYPE End(ID3D11Asynchronous* async) override { TimedCall timed_call;
        RQuery* query = query_of(async);
        if (!query) return;
        query->end_serial_ = next_query_serial();
        Writer w(Op::query_end, index_, 16);
        w.u32(query->handle());
        w.u32(query->slot_);
        w.put(query->end_serial_);
    }
    HRESULT STDMETHODCALLTYPE GetData(ID3D11Asynchronous* async, void* data, UINT size, UINT) override { TimedCall timed_call;
        RQuery* query = query_of(async);
        if (!query) return E_INVALIDARG;
        if (query->slot_ == kNoQuerySlot) return S_FALSE; // the device was lost at its creation
        if (!query->end_serial_) return DXGI_ERROR_INVALID_CALL;
        const QuerySlot& slot = query_slot(query->slot_);
        if (slot.serial == query->end_serial_) {
            MemoryBarrier();
            if (data && size) std::memcpy(data, slot.data, (std::min)(size, slot.bytes));
            return S_OK;
        }
        publish(); // a caller polling without a flush must still see progress
        return S_FALSE;
    }

    // ---- output merger and rasterizer ----------------------------------------------
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT count, ID3D11RenderTargetView* const* views, ID3D11DepthStencilView* dsv) override { TimedCall timed_call;
        count = (std::min)(count, kRtvSlots);
        {
            Writer w(Op::om_targets, index_, 8 + 4 * count);
            w.u32(count);
            w.u32(handle_of(dsv));
            write_handles(w, views, count);
        }
        for (UINT i = 0; i < kRtvSlots; ++i) state_.rtvs[i] = i < count && views ? views[i] : nullptr;
        state_.dsv = dsv;
    }
    void STDMETHODCALLTYPE OMGetRenderTargets(UINT count, ID3D11RenderTargetView** views, ID3D11DepthStencilView** dsv) override { TimedCall timed_call;
        read(state_.rtvs, 0, count, views);
        if (dsv) { *dsv = state_.dsv.Get(); if (*dsv) (*dsv)->AddRef(); }
    }
    void STDMETHODCALLTYPE OMSetBlendState(ID3D11BlendState* blend, const FLOAT factor[4], UINT mask) override { TimedCall timed_call;
        static const FLOAT kOnes[4]{1, 1, 1, 1};
        const FLOAT* used = factor ? factor : kOnes;
        {
            Writer w(Op::om_blend, index_, 28);
            w.u32(handle_of(blend));
            w.u32(factor ? 1u : 0u);
            w.put(used, 16);
            w.u32(mask);
        }
        state_.blend = blend;
        std::memcpy(state_.blend_factor, used, 16);
        state_.sample_mask = mask;
    }
    void STDMETHODCALLTYPE OMGetBlendState(ID3D11BlendState** blend, FLOAT factor[4], UINT* mask) override { TimedCall timed_call;
        if (blend) { *blend = state_.blend.Get(); if (*blend) (*blend)->AddRef(); }
        if (factor) std::memcpy(factor, state_.blend_factor, 16);
        if (mask) *mask = state_.sample_mask;
    }
    void STDMETHODCALLTYPE OMSetDepthStencilState(ID3D11DepthStencilState* state, UINT ref) override { TimedCall timed_call;
        { Writer w(Op::om_depth_stencil, index_, 8); w.u32(handle_of(state)); w.u32(ref); }
        state_.depth_stencil = state;
        state_.stencil_ref = ref;
    }
    void STDMETHODCALLTYPE OMGetDepthStencilState(ID3D11DepthStencilState** state, UINT* ref) override { TimedCall timed_call;
        if (state) { *state = state_.depth_stencil.Get(); if (*state) (*state)->AddRef(); }
        if (ref) *ref = state_.stencil_ref;
    }
    void STDMETHODCALLTYPE RSSetState(ID3D11RasterizerState* state) override { TimedCall timed_call;
        { Writer w(Op::rs_state, index_, 4); w.u32(handle_of(state)); }
        state_.rasterizer = state;
    }
    void STDMETHODCALLTYPE RSGetState(ID3D11RasterizerState** state) override { TimedCall timed_call;
        if (!state) return;
        *state = state_.rasterizer.Get();
        if (*state) (*state)->AddRef();
    }
    void STDMETHODCALLTYPE RSSetViewports(UINT count, const D3D11_VIEWPORT* viewports) override { TimedCall timed_call;
        count = viewports ? (std::min)(count, kViewports) : 0u;
        Writer w(Op::rs_viewports, index_, 4 + count * sizeof(D3D11_VIEWPORT));
        w.u32(count);
        if (count) w.put(viewports, count * sizeof(D3D11_VIEWPORT));
        state_.viewport_count = count;
        if (count) std::memcpy(state_.viewports, viewports, count * sizeof(D3D11_VIEWPORT));
    }
    void STDMETHODCALLTYPE RSGetViewports(UINT* count, D3D11_VIEWPORT* viewports) override { TimedCall timed_call;
        if (!count) return;
        if (viewports)
            for (UINT i = 0; i < *count && i < kViewports; ++i) viewports[i] = i < state_.viewport_count ? state_.viewports[i] : D3D11_VIEWPORT{};
        *count = state_.viewport_count;
    }
    void STDMETHODCALLTYPE RSSetScissorRects(UINT count, const D3D11_RECT* rects) override { TimedCall timed_call;
        count = rects ? (std::min)(count, kViewports) : 0u;
        Writer w(Op::rs_scissors, index_, 4 + count * sizeof(D3D11_RECT));
        w.u32(count);
        if (count) w.put(rects, count * sizeof(D3D11_RECT));
        state_.scissor_count = count;
        if (count) std::memcpy(state_.scissors, rects, count * sizeof(D3D11_RECT));
    }
    void STDMETHODCALLTYPE RSGetScissorRects(UINT* count, D3D11_RECT* rects) override { TimedCall timed_call;
        if (!count) return;
        if (rects)
            for (UINT i = 0; i < *count && i < kViewports; ++i) rects[i] = i < state_.scissor_count ? state_.scissors[i] : D3D11_RECT{};
        *count = state_.scissor_count;
    }

    // ---- copies, updates, clears -----------------------------------------------------
    static void note_write(ID3D11Resource* resource) noexcept {
        if (ResourceState* state = state_of(resource)) ++state->writes;
    }
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource* dst, UINT dst_sub, UINT x, UINT y, UINT z, ID3D11Resource* src,
                                                 UINT src_sub, const D3D11_BOX* box) override { TimedCall timed_call;
        CopyRegion region{handle_of(dst), dst_sub, x, y, z, handle_of(src), src_sub, box ? 1u : 0u, {}};
        if (box) { region.box[0] = box->left; region.box[1] = box->top; region.box[2] = box->front;
                   region.box[3] = box->right; region.box[4] = box->bottom; region.box[5] = box->back; }
        { Writer w(Op::copy_region, index_, sizeof(region)); w.put(region); }
        note_write(dst);
    }
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource* dst, ID3D11Resource* src) override { TimedCall timed_call;
        { Writer w(Op::copy_resource, index_, 8); w.u32(handle_of(dst)); w.u32(handle_of(src)); }
        note_write(dst);
    }
    void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource* dst, UINT sub, const D3D11_BOX* box, const void* data, UINT row_pitch,
                                             UINT depth_pitch) override { TimedCall timed_call;
        if (!dst || !data) return;
        send_update(index_, dst, sub, box, data, row_pitch, depth_pitch);
        note_write(dst);
    }
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView* view, const FLOAT color[4]) override { TimedCall timed_call;
        Writer w(Op::clear_rtv, index_, 20); w.u32(handle_of(view)); w.put(color, 16);
    }
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* view, const UINT values[4]) override { TimedCall timed_call;
        Writer w(Op::clear_uav_uint, index_, 20); w.u32(handle_of(view)); w.put(values, 16);
    }
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* view, const FLOAT values[4]) override { TimedCall timed_call;
        Writer w(Op::clear_uav_float, index_, 20); w.u32(handle_of(view)); w.put(values, 16);
    }
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView* view, UINT flags, FLOAT depth, UINT8 stencil) override { TimedCall timed_call;
        Writer w(Op::clear_dsv, index_, 16); w.u32(handle_of(view)); w.u32(flags); w.put(depth); w.u32(stencil);
    }
    void STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView* view) override { TimedCall timed_call;
        Writer w(Op::generate_mips, index_, 4); w.u32(handle_of(view));
    }

    // ---- maps ----------------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE Map(ID3D11Resource* resource, UINT sub, D3D11_MAP type, UINT flags,
                                  D3D11_MAPPED_SUBRESOURCE* out) override { TimedCall timed_call; TimedMap timed_map;
        ResourceState* state = state_of(resource);
        Layout layout;
        std::uint32_t offset = 0, total = 0;
        if (!state || !out || !subresource_layout(resource, sub, layout, &offset, &total)) return E_INVALIDARG;
        if (state->mapped) { unsupported("Map of a resource that is already mapped"); return DXGI_ERROR_INVALID_CALL; }
        if (type == D3D11_MAP_WRITE_DISCARD || type == D3D11_MAP_WRITE_NO_OVERWRITE) {
            if (state->usage != D3D11_USAGE_DYNAMIC) { unsupported("Map(WRITE_*) of a non-dynamic resource"); return E_INVALIDARG; }
            const bool buffer = state->dimension == D3D11_RESOURCE_DIMENSION_BUFFER;
            const bool ranged = buffer && hint_resource_ == resource && hint_bytes_ && hint_offset_ < total;
            state->range_offset = ranged ? hint_offset_ : 0;
            state->range_bytes = ranged ? (std::min)(hint_bytes_, total - hint_offset_) : 0;
            hint_resource_ = nullptr;
            if (!ranged && type == D3D11_MAP_WRITE_NO_OVERWRITE)
                unsupported("Map(NO_OVERWRITE) without declare_map_range (bytes around the write could be stale)");
            const std::uint32_t bytes = buffer ? (ranged ? state->range_bytes : total) : layout.bytes();
            state->block = acquire_block(bytes, buffer && !ranged && total > kWatchedBytes);
            if (!state->block.base) return E_OUTOFMEMORY;
            // A ranged map's block holds only the range: the renderer writes at
            // pData + offset, which lands at the block start.
            out->pData = state->block.base - state->range_offset;
            out->RowPitch = layout.row_pitch;
            out->DepthPitch = layout.row_pitch * layout.rows;
            (void)offset;
            state->mapped = true;
            state->mapped_sub = sub;
            state->mapped_type = type;
            return S_OK;
        }
        if (type == D3D11_MAP_READ && state->usage == D3D11_USAGE_STAGING) return map_read(resource, *state, sub, layout, flags, out);
        unsupported("Map other than WRITE_DISCARD/WRITE_NO_OVERWRITE (dynamic) or READ (staging)");
        return E_NOTIMPL;
    }

    HRESULT map_read(ID3D11Resource* resource, ResourceState& state, UINT sub, const Layout& layout, UINT flags,
                     D3D11_MAPPED_SUBRESOURCE* out) noexcept {
        if (!state.staging) state.staging.reset(new (std::nothrow) Staging);
        if (!state.staging) return E_OUTOFMEMORY;
        Staging& s = *state.staging;
        Readback request{handle_of(resource), sub, flags, layout.rows, layout.row_bytes, layout.row_pitch,
                         layout.row_pitch * layout.rows, layout.depth, 0, layout.bytes(), kPrefetchSlots, 0, 0, 0};
        if (s.requested && !s.in_flight()) {
            // A finished prefetch: small ones wait in their slot, large ones
            // are ready on the GPU and come through the readback area.
            const PrefetchSlot& slot = prefetch_slot(s.slot);
            HRESULT hr = S_OK;
            if (slot.bytes == kPrefetchLarge) {
                Readback whole = request;
                whole.subresource = s.request_sub;
                if (s.request_sub != sub) {
                    Layout requested_layout;
                    subresource_layout(resource, s.request_sub, requested_layout, nullptr, nullptr);
                    whole = {request.resource, s.request_sub, 0, requested_layout.rows, requested_layout.row_bytes,
                             requested_layout.row_pitch, requested_layout.row_pitch * requested_layout.rows,
                             requested_layout.depth, 0, requested_layout.bytes(), kPrefetchSlots, 0, 0, 0};
                }
                whole.map_flags = 0;
                hr = fetch(whole, s);
            } else {
                MemoryBarrier();
                std::memcpy(s.data, prefetch_data(s.slot), (std::min)(slot.bytes, s.bytes));
            }
            free_prefetch_slot(s.slot);
            s.slot = kPrefetchSlots;
            s.requested = 0;
            s.valid = SUCCEEDED(hr);
            s.valid_sub = s.request_sub;
            s.valid_writes = s.request_writes;
        }
        const auto hand_out = [&] {
            out->pData = s.data;
            out->RowPitch = layout.row_pitch;
            out->DepthPitch = layout.row_pitch * layout.rows;
            state.mapped = true;
            state.mapped_sub = sub;
            state.mapped_type = D3D11_MAP_READ;
            return S_OK;
        };
        if (s.valid && s.valid_sub == sub && s.valid_writes == state.writes) return hand_out();
        if (flags & D3D11_MAP_FLAG_DO_NOT_WAIT) {
            if (s.in_flight()) { publish(); return DXGI_ERROR_WAS_STILL_DRAWING; }
            if (!s.reserve(layout.bytes())) return E_OUTOFMEMORY;
            s.slot = allocate_prefetch_slot();
            if (s.slot >= kPrefetchSlots) { publish(); return DXGI_ERROR_WAS_STILL_DRAWING; } // all slots in flight
            s.valid = false;
            std::uint32_t serial = ++g_prefetch_serial;
            if (!serial) serial = ++g_prefetch_serial;
            s.requested = serial;
            s.request_sub = sub;
            s.request_writes = state.writes;
            request.slot = s.slot;
            request.serial = serial;
            { Writer w(Op::prefetch, index_, sizeof(request)); w.put(request); }
            publish();
            count_prefetch();
            return DXGI_ERROR_WAS_STILL_DRAWING;
        }
        // Blocking. The host completes an in-flight prefetch of this resource
        // (into its slot) before answering, so the slot is free afterwards.
        if (!s.reserve(layout.bytes())) return E_OUTOFMEMORY;
        const HRESULT hr = fetch(request, s);
        if (s.requested) {
            free_prefetch_slot(s.slot);
            s.slot = kPrefetchSlots;
            s.requested = 0;
        }
        if (FAILED(hr)) { s.valid = false; return hr; }
        s.valid = true;
        s.valid_sub = sub;
        s.valid_writes = state.writes;
        return hand_out();
    }

    // Synchronous readback through the shared area, in area-sized pieces.
    HRESULT fetch(Readback request, Staging& s) noexcept {
        const std::uint32_t total = request.bytes, area = readback_bytes();
        if (!s.reserve(total)) return E_OUTOFMEMORY;
        HRESULT hr = S_OK;
        for (std::uint32_t at = 0; at < total && SUCCEEDED(hr); at += area) {
            request.first_byte = at;
            request.bytes = (std::min)(area, total - at);
            const std::uint64_t serial = next_reply_serial();
            request.reply = serial;
            { Writer w(Op::readback, index_, sizeof(request)); w.put(request); }
            hr = wait_reply(serial);
            if (SUCCEEDED(hr)) std::memcpy(s.data + at, readback_area(), request.bytes);
            end_reply();
        }
        count_readback(0);
        return hr;
    }

    void STDMETHODCALLTYPE Unmap(ID3D11Resource* resource, UINT sub) override { TimedCall timed_call; TimedMap timed_map;
        ResourceState* state = state_of(resource);
        if (!state || !state->mapped || state->mapped_sub != sub) return;
        state->mapped = false;
        if (state->mapped_type == D3D11_MAP_READ) return;
        Layout layout;
        std::uint32_t offset = 0;
        if (!subresource_layout(resource, sub, layout, &offset, nullptr)) return;
        const std::uint32_t resource_handle = handle_of(resource);
        const MapBlock block = state->block;
        state->block = {};
        if (state->dimension != D3D11_RESOURCE_DIMENSION_BUFFER) {
            const std::uint32_t bytes = layout.bytes();
            if (bytes > chunk_bytes()) unsupported("dynamic texture subresource larger than a ring chunk");
            else {
                Writer w(Op::map_write, index_, sizeof(MapWrite) + sizeof(Range) + bytes);
                w.put(MapWrite{resource_handle, sub, static_cast<std::uint32_t>(state->mapped_type), 0, 1, layout.row_pitch,
                               layout.rows, layout.row_bytes});
                w.put(Range{0, bytes});
                w.put(block.base, bytes);
                count_map(bytes);
            }
        } else {
            send_buffer_ranges(resource_handle, *state, block, layout.row_bytes);
        }
        release_block(block);
        (void)offset;
    }

    std::uint16_t index() const noexcept { return index_; }
    // The next Map of `resource` writes only [offset, offset + bytes).
    void declare_range(ID3D11Resource* resource, UINT offset, UINT bytes) noexcept {
        hint_resource_ = resource;
        hint_offset_ = offset;
        hint_bytes_ = bytes;
    }

    // The declared range, else the touched pages of a watched block, else
    // the whole buffer. Range offsets are buffer offsets.
    void send_buffer_ranges(std::uint32_t resource_handle, ResourceState& state, const MapBlock& block, std::uint32_t size) noexcept try {
        thread_local std::vector<Range> ranges;
        thread_local std::vector<void*> pages;
        ranges.clear();
        // Block address of buffer offset 0 (a ranged block starts at its range).
        const std::uint8_t* origin = block.base - state.range_offset;
        bool all = !block.watched && !state.range_bytes;
        if (state.range_bytes) {
            ranges.push_back({state.range_offset, state.range_bytes});
        } else if (block.watched) {
            const std::uint32_t region = page_round(size);
            pages.resize(region / 4096u);
            ULONG_PTR count = pages.size();
            ULONG granularity = 0;
            if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, block.base, region, pages.data(), &count, &granularity) != 0) {
                // Identical result: the whole block holds every written byte, and
                // after a DISCARD the unwritten ones are undefined anyway.
                remote_warning("GetWriteWatch failed (whole dynamic buffer sent)");
                all = true;
            } else {
                for (ULONG_PTR i = 0; i < count; ++i) {
                    const auto page = static_cast<std::uint32_t>(static_cast<std::uint8_t*>(pages[i]) - block.base);
                    if (page >= size) continue;
                    const std::uint32_t end = (std::min)(page + static_cast<std::uint32_t>(granularity), size);
                    if (!ranges.empty() && ranges.back().offset + ranges.back().bytes == page) ranges.back().bytes = end - ranges.back().offset;
                    else ranges.push_back({page, end - page});
                }
            }
        }
        if (all) ranges.assign(1, Range{0, size});
        if (ranges.empty() && state.mapped_type == D3D11_MAP_WRITE_NO_OVERWRITE) return;
        // Records of at most a chunk each; after the first, NO_OVERWRITE so
        // a DISCARD is not repeated.
        const std::uint32_t chunk = chunk_bytes();
        std::size_t next = 0;
        std::uint32_t carried = 0; // bytes of ranges[next] already sent
        std::uint32_t map_type = static_cast<std::uint32_t>(state.mapped_type);
        do {
            std::vector<Range> batch;
            std::uint32_t bytes = 0;
            while (next < ranges.size() && bytes < chunk) {
                const Range& r = ranges[next];
                const std::uint32_t take = (std::min)(r.bytes - carried, chunk - bytes);
                batch.push_back({r.offset + carried, take});
                bytes += take;
                carried += take;
                if (carried == r.bytes) { ++next; carried = 0; }
            }
            {
                Writer w(Op::map_write, index_, static_cast<std::uint32_t>(sizeof(MapWrite) + batch.size() * sizeof(Range)) + bytes);
                w.put(MapWrite{resource_handle, 0, map_type, 0, static_cast<std::uint32_t>(batch.size()), 0, 0, 0});
                for (const Range& r : batch) w.put(r);
                for (const Range& r : batch) w.put(origin + r.offset, r.bytes);
            }
            count_map(bytes);
            map_type = D3D11_MAP_WRITE_NO_OVERWRITE;
        } while (next < ranges.size());
    } catch (...) {
        unsupported("dynamic buffer upload allocation failed");
    }

    // ---- command lists and misc ------------------------------------------------------
    void STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList* list, BOOL restore) override { TimedCall timed_call;
        { Writer w(Op::execute_list, index_, 8); w.u32(handle_of(list)); w.u32(restore ? 1u : 0u); }
        if (!restore) reset_state();
    }
    HRESULT STDMETHODCALLTYPE FinishCommandList(BOOL restore, ID3D11CommandList** out) override { TimedCall timed_call;
        if (!index_) return DXGI_ERROR_INVALID_CALL;
        auto* list = new (std::nothrow) RCommandList;
        if (!list) return E_OUTOFMEMORY;
        { Writer w(Op::finish_list, index_, 8); w.u32(list->handle()); w.u32(restore ? 1u : 0u); }
        if (!restore) reset_state();
        if (out) *out = list;
        else list->Release();
        return S_OK;
    }
    void STDMETHODCALLTYPE ClearState() override { TimedCall timed_call;
        { Writer w(Op::clear_state, index_, 0); }
        reset_state();
    }
    void STDMETHODCALLTYPE Flush() override { TimedCall timed_call;
        { Writer w(Op::flush, index_, 0); }
        publish();
    }
    D3D11_DEVICE_CONTEXT_TYPE STDMETHODCALLTYPE GetType() override { TimedCall timed_call;
        return index_ ? D3D11_DEVICE_CONTEXT_DEFERRED : D3D11_DEVICE_CONTEXT_IMMEDIATE;
    }
    UINT STDMETHODCALLTYPE GetContextFlags() override { TimedCall timed_call; return 0; }

    // ---- not used by the renderer -----------------------------------------------------
    REMOTE_UNUSED(SetPredication, (ID3D11Predicate*, BOOL))
    REMOTE_UNUSED(OMSetRenderTargetsAndUnorderedAccessViews,
                  (UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*))
    REMOTE_UNUSED(SOSetTargets, (UINT, ID3D11Buffer* const*, const UINT*))
    REMOTE_UNUSED(DrawAuto, ())
    REMOTE_UNUSED(CopyStructureCount, (ID3D11Buffer*, UINT, ID3D11UnorderedAccessView*))
    REMOTE_UNUSED(SetResourceMinLOD, (ID3D11Resource*, FLOAT))
    FLOAT STDMETHODCALLTYPE GetResourceMinLOD(ID3D11Resource*) override { unsupported("ID3D11DeviceContext::GetResourceMinLOD"); return 0; }
    REMOTE_UNUSED(ResolveSubresource, (ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT))
    REMOTE_UNUSED(GetPredication, (ID3D11Predicate**, BOOL*))
    REMOTE_UNUSED(OMGetRenderTargetsAndUnorderedAccessViews,
                  (UINT, ID3D11RenderTargetView**, ID3D11DepthStencilView**, UINT, UINT, ID3D11UnorderedAccessView**))
    REMOTE_UNUSED(SOGetTargets, (UINT, ID3D11Buffer**))
    REMOTE_UNUSED(CopySubresourceRegion1, (ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*, UINT))
    REMOTE_UNUSED(UpdateSubresource1, (ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT, UINT))
    REMOTE_UNUSED(DiscardResource, (ID3D11Resource*))
    REMOTE_UNUSED(DiscardView, (ID3D11View*))
    REMOTE_UNUSED(SwapDeviceContextState, (ID3DDeviceContextState*, ID3DDeviceContextState**))
    REMOTE_UNUSED(ClearView, (ID3D11View*, const FLOAT[4], const D3D11_RECT*, UINT))
    REMOTE_UNUSED(DiscardView1, (ID3D11View*, const D3D11_RECT*, UINT))

private:
    bool implements(REFIID riid) const noexcept override {
        return riid == __uuidof(ID3D11DeviceContext) || riid == __uuidof(ID3D11DeviceContext1);
    }
    void reset_state() noexcept {
        std::unique_ptr<Bindings> fresh(new (std::nothrow) Bindings);
        if (!fresh) return;
        std::swap(*state_holder_, *fresh); // the old bindings release with `fresh`
    }
    std::uint16_t index_;
    ID3D11Resource* hint_resource_ = nullptr; // compared only: the next Map's declared range
    UINT hint_offset_ = 0, hint_bytes_ = 0;
    std::unique_ptr<Bindings> state_holder_{new Bindings};
    Bindings& state_ = *state_holder_;
};

} // namespace

void declare_map_range(ID3D11DeviceContext* context, ID3D11Resource* resource, UINT offset, UINT bytes) noexcept {
    if (!remote_active() || !context || tag_of(context)->magic != kObjectMagic) return;
    static_cast<RContext*>(static_cast<ID3D11DeviceContext1*>(context))->declare_range(resource, offset, bytes);
}

HRESULT dlss_evaluate(ID3D11DeviceContext* context, const DlssEvaluate& e) noexcept {
    if (!remote_active()) return E_NOTIMPL;
    if (!context || tag_of(context)->magic != kObjectMagic) return E_INVALIDARG;
    auto* remote = static_cast<RContext*>(static_cast<ID3D11DeviceContext1*>(context));
    if (remote->index() != 0) return E_INVALIDARG; // NGX preserves only the immediate context's state
    DlssEvaluateRecord record{};
    record.color = handle_of(e.color);
    record.depth = handle_of(e.depth);
    record.motion = handle_of(e.motion);
    record.output = handle_of(e.output);
    record.exposure = handle_of(e.exposure);
    record.jitter_x = e.jitter_x;
    record.jitter_y = e.jitter_y;
    record.mv_scale_x = e.mv_scale_x;
    record.mv_scale_y = e.mv_scale_y;
    record.render_w = e.render_w;
    record.render_h = e.render_h;
    record.out_w = e.out_w;
    record.out_h = e.out_h;
    record.mode = e.mode;
    record.sharpness = e.sharpness;
    record.flags = (e.reset ? dlss_reset : 0u) | (e.hdr ? dlss_hdr : 0u) | (e.depth_inverted ? dlss_depth_inverted : 0u);
    { Writer w(Op::dlss_evaluate, 0, sizeof(record)); w.put(record); }
    return link_removed() ? DXGI_ERROR_DEVICE_REMOVED : S_OK;
}

void dlss_release() noexcept {
    if (!remote_active()) return;
    Writer w(Op::dlss_release, 0, 0);
}

ID3D11DeviceContext1* create_context(std::uint16_t index) noexcept {
    return new (std::nothrow) RContext(index);
}

} // namespace x64remote
