#pragma once
// The x64 host's D3D12 executor as the shared Renderer sees it
// (renderer_backend=d3d12, core mode only; Host/d3d12/d3d12_frame.cpp).
// The x86 DLL never creates one: Renderer::d3d12_ stays null there and every
// D3D11 path runs unchanged.
//
// Both executors consume the same records: the Renderer still prepares the
// queued UI quads, their fixed states, the surface and overlay pixels; this
// executor records them into its D3D12 frame instead of the D3D11 context.
// What it cannot draw yet is omitted visibly (the world marker colour, one
// log line per name, the F11 list), never drawn through D3D11.
#include "fixed_state_descs.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ImDrawData;
struct ID3D12Device;
struct IDXGIAdapter;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;

namespace owned {

class OverlayRenderer;  // debug_overlay.h

// A texture the D3D12 executor owns (defined in the host). Shared ownership:
// whoever still names it (a surface, an overlay cache entry, a queued quad)
// keeps it, and its release waits for the frames that read it.
struct D3D12Texture;
struct ProfileTimestamps;  // render_profiler.h
using D3D12TextureRef = std::shared_ptr<D3D12Texture>;
// Immutable allocation shape, readable without commands or a live executor.
struct D3D12TextureShape {
    UINT width{}, height{}, mips{}, slices{};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};
D3D12TextureShape d3d12_texture_shape(const D3D12Texture& texture) noexcept;
// Count each live surface allocation once, even when snapshots share it.
void d3d12_account_surface(D3D12Texture& texture, std::uint64_t bytes) noexcept;
std::uint64_t d3d12_surface_bytes() noexcept;

// The shared UI programs (renderer_device.cpp kShader and its neighbours).
enum class D3D12UiProgram : std::uint8_t { game, interface_scaled, interface_text, backdrop, count };

struct D3D12NativeState;

// One UI draw over the pass's vertices (the Renderer's 32-byte Vertex).
struct D3D12UiDraw {
    const D3D12Texture* texture = nullptr;  // t0
    D3D12UiProgram program = D3D12UiProgram::game;
    FixedBlend blend{};
    D3D11_SAMPLER_DESC sampler{};           // s0
    bool triangle_list = false;             // else a strip
    // Synchronous embedded fixed-function ingress, using the game shaders.
    const D3D12NativeState* fixed_state = nullptr;
    D3DPRIMITIVETYPE fixed_topology = D3DPT_TRIANGLELIST;
    bool scissor = false;
    RECT scissor_rect{};                    // output pixels
    UINT constants = 0;                     // its block in D3D12UiPass::constants
    UINT first_vertex = 0, vertex_count = 0;
};
struct D3D12UiPass {
    const void* vertices = nullptr;
    UINT vertex_bytes = 0, vertex_stride = 0;
    const void* constants = nullptr;        // constant_blocks blocks of constant_block_bytes (b0)
    UINT constant_block_bytes = 0, constant_blocks = 0;
    const D3D12UiDraw* draws = nullptr;
    std::size_t draw_count = 0;
};

// A buffer the D3D12 executor owns (defined in the host), shared like
// D3D12Texture. Native passes read and write them through the slots below.
struct D3D12Buffer;
using D3D12BufferRef = std::shared_ptr<D3D12Buffer>;
enum : UINT {
    kD3D12BufferUnordered = 1,  // also a u-slot target (compute writes)
    kD3D12BufferIndirect = 2,   // also indirect draw arguments
    // A typed shader view (HLSL Buffer<T>) instead of raw/structured: stride
    // 4 reads as R32_UINT, stride 16 as R32G32B32A32_FLOAT.
    kD3D12BufferTyped = 4,
};

// A draw, dispatch or mip pass left out because the frame's descriptor or
// sampler ring was exhausted: a success code like S_FALSE (nothing was
// recorded), but not a pipeline still compiling. The executor names every
// such omission itself, so callers name only S_FALSE.
constexpr HRESULT kD3D12Dropped = 2;
inline bool d3d12_not_done(HRESULT hr) noexcept { return hr == S_FALSE || hr == kD3D12Dropped; }

// The fixed-function state of one native draw (fixed_state_descs.h values).
// The bindless index the executor keeps as a null 2D view for the whole
// session: a native draw without a D3D12 texture references it (samples zeros,
// as an unbound D3D11 slot); no texture ever takes it.
constexpr std::uint32_t kD3D12NullTextureIndex = 0;

struct D3D12NativeState {
    FixedBlend blend{};
    D3D11_DEPTH_STENCIL_DESC depth{};
    D3D11_RASTERIZER_DESC raster{};
    // custom bits: kCustomBlendOver, a full colour blend at target 0 in place
    // of `blend` (the post passes' additive and alpha-over blends that keep
    // the target alpha); kCustomBlendMotionTargets, targets 1 and 2 (object
    // motion RG, its depth R) blended by the output alpha, so alpha 0 keeps
    // them (the fur motion variants). Whole words only: the state is a
    // pipeline key, byte for byte.
    static constexpr UINT kCustomBlendOver = 1, kCustomBlendMotionTargets = 2;
    struct CustomBlend {
        UINT custom = 0, source = D3D11_BLEND_ONE, destination = D3D11_BLEND_ZERO, op = D3D11_BLEND_OP_ADD;
        UINT source_alpha = D3D11_BLEND_ONE, destination_alpha = D3D11_BLEND_ZERO, op_alpha = D3D11_BLEND_OP_ADD;
        UINT write_mask = D3D11_COLOR_WRITE_ENABLE_ALL;
    } custom_blend{};
};

struct ShaderProgram;  // shader_cache.h

// A one-off upload prepared off the render thread (defined in the host): its
// destination resource, created, and an upload buffer holding every byte in
// the copy's layout. finish_buffer / finish_texture make it a D3D12Buffer /
// D3D12Texture and record its copies; one never finished is simply released
// (the GPU never saw it).
struct D3D12PreparedUpload;
using D3D12PreparedUploadRef = std::shared_ptr<D3D12PreparedUpload>;

// Preparation of those uploads: any thread, several at once. It holds the
// device and session identity (shared ownership), so a worker may keep it
// past the executor. A replacement executor rejects its prepared uploads.
class D3D12Preparer {
public:
    virtual ~D3D12Preparer() = default;
    // A buffer of bytes bytes (16-rounded) holding data.
    virtual HRESULT prepare_buffer(UINT64 bytes, const void* data, D3D12PreparedUploadRef& out) noexcept = 0;
    // A shader-readable texture of mips levels: levels[m] is level m's first
    // row (block row for block formats), pitches[m] bytes apart.
    virtual HRESULT prepare_texture(UINT width, UINT height, DXGI_FORMAT format, UINT mips, const void* const* levels,
                                    const UINT* pitches, D3D12PreparedUploadRef& out) noexcept = 0;
    // Several such textures placed in one heap with one upload buffer: two
    // allocations for the batch instead of two per texture (the cost of a
    // batch of terrain pages). out gets one entry per texture.
    struct TextureLevels {
        UINT width = 0, height = 0, mips = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        const void* const* levels = nullptr;
        const UINT* pitches = nullptr;
    };
    virtual HRESULT prepare_texture_batch(const TextureLevels* textures, std::size_t count,
                                          std::vector<D3D12PreparedUploadRef>& out) noexcept = 0;
};

class D3D12Frame {
public:
    virtual ~D3D12Frame() = default;
    // The preparer of off-render-thread uploads (null when the device failed).
    virtual std::shared_ptr<D3D12Preparer> preparer() noexcept = 0;
    // Render thread: the prepared resource with its views (as create_buffer /
    // create_texture make them), its copies recorded in stream order, the
    // upload memory released once this frame finished. The upload is spent.
    // Uploads from another executor return DXGI_ERROR_DEVICE_RESET unconsumed.
    virtual HRESULT finish_buffer(D3D12PreparedUpload& upload, UINT stride, UINT flags, D3D12BufferRef& out) = 0;
    virtual HRESULT finish_texture(D3D12PreparedUpload& upload, D3D12TextureRef& out) = 0;
    // A shader-readable texture (B8G8R8A8, BC1-3 or the 16-bit owned formats).
    virtual HRESULT create_texture(UINT width, UINT height, DXGI_FORMAT format, D3D12TextureRef& out, UINT mips = 1) = 0;
    // A texture array (shader-readable as Texture2DArray, even with one slice).
    // A current executor texture's size, levels and format. Returns false
    // for another executor's allocation, including surviving old snapshots.
    virtual bool texture_info(const D3D12Texture& texture, UINT& width, UINT& height, UINT& mips, DXGI_FORMAT& format) const noexcept = 0;
    virtual HRESULT create_texture_array(UINT width, UINT height, DXGI_FORMAT format, UINT mips, UINT slices, D3D12TextureRef& out) = 0;
    // A texture compute programs write (u0-u7) and shaders read: 2D, 3D when
    // depth > 0, or a 2D array of slices (read as Texture2DArray); its
    // unordered view is mip 0. Created writable.
    // not_zeroed: its memory is not cleared at creation (D3D12_HEAP_FLAG_CREATE_NOT_ZEROED,
    // where the runtime takes it): only for a texture whose every read texel is written first.
    virtual HRESULT create_storage_texture(UINT width, UINT height, UINT depth, DXGI_FORMAT format, D3D12TextureRef& out,
                                           UINT mips = 1, UINT slices = 1, bool not_zeroed = false) = 0;
    // Whether compute can write the format (typed UAV store): a storage
    // texture of it, and so generate_mips.
    virtual bool storage_supported(DXGI_FORMAT format) noexcept = 0;
    // D3D11 GenerateMips for a 2D storage texture or one array slice, by
    // downsample (t0 -> u0, 8x8 groups; d3d12_mip_program, or
    // d3d12_mip_array_program for an array). Clears the slots. S_FALSE: not ready.
    // count > 1: slices [slice, slice + count) at once (d3d12_mip_array_range_program).
    virtual HRESULT generate_mips(D3D12Texture& texture, const ShaderProgram& downsample, UINT slice = 0, UINT count = 1) = 0;
    // A GPU copy of one subresource (D3D11CalcSubresource numbering), stream order.
    virtual HRESULT copy_texture(D3D12Texture& destination, UINT destination_subresource, D3D12Texture& source,
                                 UINT source_subresource) = 0;
    // An offscreen colour target (shader-readable; B8G8R8A8 for the camp
    // popup, R11G11B10 for the world) with its own depth: D24S8 (readable as
    // R24_UNORM), D32 (readable as R32_FLOAT) or none (UNKNOWN); format
    // UNKNOWN makes a depth-only target. set_target(null) returns to the back
    // buffer and the interface depth; present returns there as well. world:
    // the target is the world's (an omitted world pass shows the marker colour
    // there).
    virtual HRESULT create_target(UINT width, UINT height, D3D12TextureRef& out,
                                  DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM,
                                  DXGI_FORMAT depth_format = DXGI_FORMAT_D24_UNORM_S8_UINT) = 0;
    // Several colour targets (up to 8, one format each) with the depth of
    // `depth` (null: none, no depth test) for the next draws; set_target
    // returns to one target. A null entry after the first binds a null view
    // (that output's writes are discarded, as an unbound D3D11 slot); the
    // world target keeps its world role (omitted-pass marker) in a set.
    virtual void set_targets(D3D12Texture* const* colors, UINT count, D3D12Texture* depth) noexcept = 0;
    // Any target's colour or a storage texture's level (mip), cleared now; the bound targets stay.
    virtual void clear_texture(D3D12Texture& target, const float color[4], UINT mip = 0) noexcept = 0;
    // An integer-format storage texture's mip 0, cleared now to values.
    virtual void clear_texture_uint(D3D12Texture& target, const UINT values[4]) noexcept = 0;
    // One target's depth into another's of the same size and format, in
    // stream order; the destination stays shader-readable (set_depth_resource).
    virtual HRESULT copy_depth(D3D12Texture& destination, D3D12Texture& source) = 0;
    // A target's depth as a shader input (t0-t127).
    virtual void set_depth_resource(UINT slot, const D3D12Texture* target) noexcept = 0;
    // D24S8 stencil plane as Texture2D<uint>; other depth formats bind null.
    virtual void set_stencil_resource(UINT slot, const D3D12Texture* target) noexcept = 0;
    // A depth array (shadow maps): D16 or D32 slices, readable as Texture2DArray
    // (plain: one slice read as Texture2D, the rain shelter map).
    virtual HRESULT create_depth_array(UINT side, UINT slices, D3D12TextureRef& out,
                                       DXGI_FORMAT depth_format = DXGI_FORMAT_D32_FLOAT, bool plain = false) = 0;
    // Depth-only draws (no colour target) into one slice, viewport and
    // scissor the rect; set_target or set_targets returns to colour targets.
    // color: one colour target (create_target without depth, the array's
    // size) beside the slice, e.g. the rain shelter bake's foliage mask; the
    // draws' pipelines then take its format (shadow maps bind none).
    virtual void set_depth_target(D3D12Texture* array, UINT slice, const RECT& viewport, D3D12Texture* color = nullptr) noexcept = 0;
    // An optional rect clears only one atlas cell, including its gutter.
    virtual void clear_depth_slice(D3D12Texture& array, UINT slice, float depth, const RECT* rect = nullptr) noexcept = 0;
    // Bindless (M2a): the texture's persistent shader-visible heap index
    // (allocated on first use, freed with the texture), or UINT32_MAX when the
    // bindless region is full: that draw is omitted (named) and the region
    // doubles before the next frame.
    virtual std::uint32_t bindless_index(D3D12Texture& texture) noexcept = 0;
    virtual void set_target(D3D12Texture* target, bool world = false) noexcept = 0;
    // The offscreen target: its colour (when color is set), its depth when asked.
    virtual void clear_target(const float* color, bool depth) noexcept = 0;
    // A target's own depth (not bound now): the world depth the D3D11 frame
    // shares with the interface depth at full render scale.
    virtual void clear_depth_of(D3D12Texture& target, float depth, UINT8 stencil) noexcept = 0;
    virtual bool texture_matches(const D3D12Texture& texture, UINT width, UINT height, DXGI_FORMAT format) const noexcept = 0;
    // rect in texels of mip (4-aligned for block formats); data is the rect's
    // first row (block row), pitch bytes apart. Recorded in order with the draws.
    // For a texture array, mip is the subresource: mip + slice * mips.
    virtual HRESULT write_texture(D3D12Texture& texture, const RECT& rect, const void* data, UINT pitch, UINT mip = 0) = 0;

    // ---- Native passes (the native queue's records on D3D12) --------------
    // stride 0: a raw (ByteAddressBuffer) view, else structured elements of
    // stride bytes. flags: kD3D12Buffer*.
    virtual HRESULT create_buffer(UINT64 bytes, UINT stride, UINT flags, D3D12BufferRef& out) = 0;
    virtual UINT64 buffer_bytes(const D3D12Buffer& buffer) const noexcept = 0;
    // Recorded in stream order with the passes.
    virtual HRESULT write_buffer(D3D12Buffer& buffer, UINT64 offset, const void* data, UINT64 bytes) = 0;
    // The same for one-off load data (the geometry pages' meshes): its upload
    // memory comes from a separate pool, so a load's hundreds of MB never
    // grow the per-frame upload blocks.
    virtual HRESULT write_buffer_bulk(D3D12Buffer& buffer, UINT64 offset, const void* data, UINT64 bytes) = 0;
    virtual HRESULT copy_buffer(D3D12Buffer& destination, D3D12Buffer& source, UINT64 bytes) = 0;
    // The shared native HLSL's D3D11 register model, slot by slot, for the
    // next dispatch or draw: a thin per-flush translation of slots to
    // descriptors, nothing more (M2 world passes read bindless indices).
    // Unset slots read null descriptors (zero), as unbound D3D11 slots do.
    virtual void set_constants(UINT slot, const void* data, UINT bytes) = 0;  // b0-b13, copied now
    virtual void set_resource(UINT slot, const D3D12Buffer* buffer) noexcept = 0;   // t0-t127
    virtual void set_resource(UINT slot, const D3D12Texture* texture) noexcept = 0;
    virtual void set_unordered(UINT slot, const D3D12Buffer* buffer) noexcept = 0;  // u0-u7
    virtual void set_unordered(UINT slot, const D3D12Texture* texture) noexcept = 0;  // a storage texture's mip 0
    // One level of a 2D storage texture as a u-slot (FSR 2's luminance mips).
    virtual void set_unordered_level(UINT slot, D3D12Texture* texture, UINT mip) noexcept = 0;
    // Whether compute can read the format through a typed unordered view
    // (FSR 2 reads several non-32-bit formats that way).
    virtual bool unordered_load_supported(DXGI_FORMAT format) noexcept = 0;
    virtual void set_sampler(UINT slot, const D3D11_SAMPLER_DESC& sampler) noexcept = 0;  // s0-s15
    virtual void clear_slots() noexcept = 0;
    // S_FALSE: its pipeline is not ready (the caller omits the work, named).
    virtual HRESULT dispatch(const ShaderProgram& program, UINT x, UINT y, UINT z) = 0;
    // A dispatch whose group counts are three words of arguments at offset.
    virtual HRESULT dispatch_indirect(const ShaderProgram& program, D3D12Buffer& arguments, UINT offset) = 0;
    // The 32-bit index buffer of the next indexed draws (null: none).
    virtual void set_index_buffer(D3D12Buffer* indices) noexcept = 0;
    // An indexed draw from five argument words at offset (DrawIndexedInstancedIndirect).
    virtual HRESULT draw_indexed_indirect(const ShaderProgram& vertex, const ShaderProgram& pixel, const D3D12NativeState& state,
                                          const RECT* scissor, D3D12Buffer& arguments, UINT offset) = 0;
    // Every 32-bit word of the buffer set to value, in stream order.
    virtual void clear_buffer_uint(D3D12Buffer& buffer, UINT value) noexcept = 0;
    // Non-blocking readback: the buffer's first bytes copied in stream order;
    // poll_readback returns S_FALSE while in flight, then S_OK with the bytes
    // (once; the ticket is gone after). Unpolled copies are dropped past 32.
    virtual HRESULT request_readback(D3D12Buffer& source, UINT64 bytes, std::uint64_t& ticket) = 0;
    virtual HRESULT poll_readback(std::uint64_t ticket, std::vector<std::uint8_t>& data) = 0;
    // A native draw into the frame's back buffer and interface depth.
    // pixel: a program without an entry (ShaderProgram{}) draws depth only.
    // hull/domain: the tessellation stages (3-point patches) when both are set.
    virtual HRESULT draw_indirect(const ShaderProgram& vertex, const ShaderProgram& pixel, const D3D12NativeState& state,
                                  const RECT* scissor, D3D12Buffer& arguments, UINT offset,
                                  const ShaderProgram* hull = nullptr, const ShaderProgram* domain = nullptr,
                                  bool strip = false) = 0;
    // A non-indexed draw of vertex_count vertices from SV_VertexID (no vertex
    // buffer): the game effect programs (particles, ribbons, selection, glyphs,
    // ground prints). S_FALSE: the pipeline is not ready (named by the caller).
    virtual HRESULT draw(const ShaderProgram& vertex, const ShaderProgram& pixel, const D3D12NativeState& state, const RECT* scissor,
                         UINT vertex_count) = 0;
    // DrawInstanced: instance_count instances of vertex_count vertices
    // (SV_VertexID, SV_InstanceID; no vertex buffer), a triangle strip when
    // strip is set, else a list (the composite_world effects and critters).
    virtual HRESULT draw_instanced(const ShaderProgram& vertex, const ShaderProgram& pixel, const D3D12NativeState& state,
                                   const RECT* scissor, UINT vertex_count, UINT instance_count, bool strip) = 0;
    // The interface depth target: cleared at each frame's start and here.
    virtual void clear_depth(float depth, UINT8 stencil) noexcept = 0;
    // Device::Clear with rectangles (output pixels): the bound target's colour
    // (color non-null), depth and stencil inside them only, in stream order.
    virtual void clear_rectangles(const RECT* rects, UINT count, const float* color, bool depth, bool stencil,
                                  float z, UINT8 stencil_value) noexcept = 0;
    // Pipelines built so far for native passes, and their total build time.
    virtual void native_pipeline_stats(UINT& count, double& milliseconds) const noexcept = 0;
    // The game's one-shot picking frame is data: every requested pipeline
    // must finish compiling before its draws/dispatches can be copied out.
    virtual void set_hit_test_frame(bool active) noexcept = 0;
    // Copies box of what the frame drew so far to (x, y) of texture; with a
    // source, box of that offscreen target instead (the zone hit-test frame).
    virtual HRESULT capture(D3D12Texture& texture, UINT x, UINT y, const RECT& box, D3D12Texture* source = nullptr) = 0;
    virtual HRESULT draw_ui(const D3D12UiPass& pass) = 0;
    // A whole-target clear before anything is drawn this frame: the frame's
    // background colour (the world marker still wins when a world pass was
    // omitted). Ignored once the frame drew something.
    virtual void clear(const float color[4]) noexcept = 0;
    // Diagnostics (harness screenshot): the back buffer as drawn so far,
    // read back from the GPU (B8G8R8A8 rows, width * 4 bytes each). Waits
    // for the GPU; the frame then continues. S_FALSE: minimized, no image.
    virtual HRESULT read_back_buffer(std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) = 0;
    // Frame generation's inputs (fg_frame_image): from the next frame on, the
    // back buffer (set_target(null), clear, capture, read_back_buffer) is an
    // offscreen frame image of the output size, not the swap-chain buffer.
    // composite_frame_image copies it whole into the swap-chain buffer, where
    // every later back-buffer draw, capture and read of the frame goes (the
    // overlay, cursor, load reveal and gamma); present does it when nobody
    // did. Without a frame image this frame it does nothing.
    virtual void set_frame_image(bool wanted) noexcept = 0;
    virtual void composite_frame_image() noexcept = 0;
    // Frame generation presentation (frame_generation=DLSS-G). From the next
    // frame taken, set_frame_generation(1) moves presentation to a pacer
    // thread on the shared direct queue; each frame's screen (what composite_frame_image
    // copies the frame image into and where the overlay, cursor, reveal and
    // gamma draw) is an offscreen real image the pacer presents. A failure to
    // start is reported once (frame_generation_failure) and leaves it off.
    // frame_generation_active(mode): this frame presents in that mode (1: the
    // pacer, 2: the FidelityFX proxy) and has a frame image. The executor
    // changes mode at the next frame taken, so the frame on which the
    // renderer's mode changes still presents in the old one.
    // generated_image: this frame's generated image (output size, the frame
    // image's format, NGX's unordered-access output). select_generated_screen
    // (after composite_frame_image): later back-buffer draws go to the
    // generated image, which the pacer presents half a real-frame interval
    // before the real one.
    //
    // frame_generation=FSR (mode 2): the swap chain becomes the FidelityFX
    // proxy, which interpolates inside Present and paces on its own threads.
    // The screen stays the (proxy's) swap-chain buffer, drawn as without
    // frame generation; no generated image. Per frame: fsr_prepare inside
    // record_external (depth, motion, camera), then set_fsr_hudless before
    // present (the HUD-less image matching the presented colours; null: no
    // generation this frame).
    // mode: 0 off, 1 DLSS-G (pacer), 2 FSR (FidelityFX proxy).
    virtual void set_frame_generation(int mode) noexcept = 0;
    virtual bool frame_generation_active(int mode) const noexcept = 0;
    // A failure that turned `mode` off for the session (null: none).
    virtual const char* frame_generation_failure(int mode) const noexcept = 0;
    struct FsrPrepare {
        UINT render_w = 0, render_h = 0;
        float jitter_x = 0, jitter_y = 0;  // render pixels, the upscaler's
        float frame_ms = 0;
        bool reset = false;
        float near_plane = 0, far_plane = 0, fov_y = 0;
        float position[3]{}, up[3]{}, right[3]{}, forward[3]{};
    };
    virtual HRESULT fsr_prepare(ID3D12GraphicsCommandList* list, ID3D12Resource* depth, ID3D12Resource* motion, const FsrPrepare& prepare) noexcept = 0;
    virtual void set_fsr_hudless(D3D12Texture* hudless) noexcept = 0;
    // FSR: the frame generation context's VRAM (bytes, 0: unknown).
    virtual std::uint64_t fsr_vram() const noexcept = 0;
    virtual D3D12Texture* frame_image_texture() noexcept = 0;
    virtual D3D12Texture* generated_image() noexcept = 0;
    virtual void select_generated_screen() noexcept = 0;
    struct FrameGenerationStats { bool active = false; std::uint64_t generated_presents = 0, real_presents = 0; double real_interval_ms = 0; };
    virtual FrameGenerationStats frame_generation_stats() const noexcept = 0;
    // A captured interface surface, with the same tight BGRA layout and
    // synchronous completion as read_back_buffer. Restores its resource state.
    virtual HRESULT read_texture(D3D12Texture& texture, std::vector<std::uint8_t>& pixels, UINT& width, UINT& height) = 0;
    // A pass or command this backend does not draw yet: one log line per
    // name, the F11 list; world: the frame shows the world marker colour.
    virtual void omit(const char* name, bool world) noexcept = 0;
    // Work this backend did (interface packets queued, batches drawn): counted
    // by name for the session-end line beside the omitted counts.
    virtual void count(const char* name) noexcept = 0;
    // The renderer pass now recording (transition_profile_pass): a marker in
    // the frame list, so a device removal names the pass of its last
    // unfinished operation (DRED breadcrumb context).
    virtual void marker(const char* name) noexcept = 0;
    // Work a library records into the frame's command list itself (NVIDIA
    // NGX for DLSS). Each listed texture (its depth when depth is set) is
    // moved first to the state the library expects (written: unordered
    // access, else shader read) as the executor's own barriers, visible to
    // the hazard checker; record gets the list and the resources in the same
    // order. Afterwards the executor binds its heaps and root signature
    // again and forgets the list state it cached. Returns record's result.
    struct ExternalUse { D3D12Texture* texture = nullptr; bool depth = false; bool written = false; };
    using ExternalRecord = std::function<HRESULT(ID3D12GraphicsCommandList*, ID3D12Resource* const*)>;
    virtual HRESULT record_external(const char* name, const ExternalUse* uses, UINT count, const ExternalRecord& record) = 0;
    // The executor's device (DLSS initialises NGX on it).
    virtual ID3D12Device* device() noexcept = 0;
    // The adapter of that device (the VRAM budget queries its local segment).
    virtual IDXGIAdapter* adapter() noexcept = 0;
    // Waits until the GPU finished every submitted frame, the frame being
    // recorded keeping its fence value (callable mid-frame; before NGX
    // releases a feature those frames may still run).
    virtual void wait_idle() noexcept = 0;
    // frame_limit caps total output FPS, including generated frames (0: off).
    virtual HRESULT present(UINT sync_interval, int frame_limit = 0) = 0;
    virtual HRESULT resize(UINT width, UINT height) = 0;
    // "name xcount, ..." of everything omitted since the start (F11).
    virtual std::string omitted() const = 0;
    // The F10 overlay's draw lists go through the executor.
    virtual OverlayRenderer& overlay_renderer() noexcept = 0;
    // The renderer's profiler times its passes with these GPU timestamps.
    virtual ProfileTimestamps& profile_timestamps() noexcept = 0;
};

} // namespace owned
