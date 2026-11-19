// D3D12 core bring-up (EI_RENDERER_X64_D3D12_CORE=1, opt-in only): runs the
// native D3D12 core (d3d12_core.h and its neighbours) in a diagnostic child
// window above the D3D11 host's, while the D3D11 host keeps running the game
// underneath. No frame plan exists yet (DX12 plan M0), so the window shows
// no game content: a labelled diagnostic clear colour, status text and the
// last frame's per-pass GPU times as numbers and bars. Every frame is a
// small plan (text upload when it changed, clear, compute bars, overlay
// draw, optional screenshot copy) run through the barrier compiler, the
// rings, bindless descriptors, the global root signature and pipelines
// built ahead on workers.
//
// Failures are never silent: a start failure or device removal names its
// reason in d3d12_core.log beside the host and in a message box.
//
// TEMPORARY bring-up controls (remove with the bring-up window):
//   EI_RENDERER_X64_D3D12_SHOT=<frame>       writes d3d12_core_frame<frame>.bmp
//                                            through the readback ring
//   EI_RENDERER_X64_D3D12_REMOVE_AT=<frame>  removes the device at that frame
//                                            (checks the device-removed path)
//   EI_RENDERER_X64_D3D12_DEBUG=1            D3D12 debug layer
#include "d3d12_barriers.h"
#include "../../Source/render_resources.h"
#include "d3d12_core.h"
#include "d3d12_pipelines.h"
#include "d3d12_presenter.h"
#include "d3d12_rings.h"
#include "d3d12_timestamps.h"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace d3d12 {

namespace {

std::wstring g_directory;
std::atomic<bool> g_stop{false};
std::thread g_thread;

void log(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    FILE* file = nullptr;
    if (_wfopen_s(&file, (g_directory + L"d3d12_core.log").c_str(), L"a") == 0 && file) {
        std::fprintf(file, "%02u:%02u:%02u.%03u %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, text);
        std::fclose(file);
    }
}

// The named failure: log and a message box (never a silent fallback).
void fail_visibly(const char* what, const std::string& reason) {
    log("%s: %s", what, reason.c_str());
    const std::string text = std::string("EI Renderer D3D12 core (EI_RENDERER_X64_D3D12_CORE=1): ") + what + ".\n\n" + reason +
                             "\n\nThe D3D12 diagnostic window stops; details in d3d12_core.log beside the x64 host.";
    // On its own thread: the bring-up tears down and the host can exit with the box open.
    std::thread([text] {
        MessageBoxA(nullptr, text.c_str(), "EI Renderer D3D12 core", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    }).detach();
}

std::uint64_t env_number(const char* name) {
    char value[32]{};
    return GetEnvironmentVariableA(name, value, sizeof(value)) ? std::strtoull(value, nullptr, 10) : 0;
}

bool env_flag(const char* name) {
    char value[8]{};
    return GetEnvironmentVariableA(name, value, sizeof(value)) && value[0] == '1';
}

// A host-only program from the host's resources (Host\d3d12\d3d12_shaders.rc).
D3D12_SHADER_BYTECODE embedded_program(const wchar_t* name) {
    const HMODULE module = owned::render_resource_module();
    if (!module) return {};
    const HRSRC found = FindResourceW(module, name, MAKEINTRESOURCEW(10));  // RT_RCDATA
    const HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
    if (!loaded) return {};
    return {LockResource(loaded), SizeofResource(module, found)};
}

// Frame constants (b1), laid out as d3d12_diagnostic.hlsl's Frame.
struct FrameConstants {
    float viewport[2];
    float text_origin[2];
    float text_size[2];
    float bar_x, bar_y0, row_height, px_per_ms, bar_max_width, pad;
};

struct Bar { float rect[4], color[4]; };

constexpr UINT kTextWidth = 1024, kTextHeight = 512, kTextLine = 20, kPassLine0 = 7;
constexpr float kTextX = 24, kTextY = 24;

// The status text, drawn by GDI into a top-down BGRA DIB.
class StatusText {
public:
    bool create() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = kTextWidth;
        info.bmiHeader.biHeight = -static_cast<LONG>(kTextHeight);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        dc_ = CreateCompatibleDC(nullptr);
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_), nullptr, 0);
        font_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            NONANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
        if (!dc_ || !bitmap_ || !font_) return false;
        SelectObject(dc_, bitmap_);
        SelectObject(dc_, font_);
        SetBkMode(dc_, TRANSPARENT);
        SetTextColor(dc_, RGB(255, 255, 255));
        return true;
    }
    ~StatusText() {
        if (dc_) DeleteDC(dc_);
        if (bitmap_) DeleteObject(bitmap_);
        if (font_) DeleteObject(font_);
    }
    void draw(const std::vector<std::wstring>& lines) {
        RECT all{0, 0, LONG(kTextWidth), LONG(kTextHeight)};
        FillRect(dc_, &all, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        for (std::size_t i = 0; i < lines.size(); ++i)
            TextOutW(dc_, 0, int(i * kTextLine), lines[i].c_str(), int(lines[i].size()));
        GdiFlush();
    }
    const std::uint8_t* pixels() const noexcept { return pixels_; }

private:
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HFONT font_ = nullptr;
    std::uint8_t* pixels_ = nullptr;
};

// TEMPORARY bring-up: a readback ticket's back buffer copy as a 32-bit BMP.
bool write_bmp(const std::wstring& path, const ReadbackTicket& ticket, UINT width, UINT height, UINT row_pitch) {
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = LONG(width);
    info.biHeight = -LONG(height);  // top-down, as copied
    info.biPlanes = 1;
    info.biBitCount = 32;
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + width * height * 4;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    for (UINT y = 0; y < height; ++y) out.write(reinterpret_cast<const char*>(ticket.cpu + std::size_t(y) * row_pitch), width * 4);
    return static_cast<bool>(out);
}

ComPtr<ID3D12Resource> create_resource(ID3D12Device* device, const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES state,
                                       const wchar_t* name, std::string& reason) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> resource;
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) { reason = hresult_text("CreateCommittedResource", hr); return nullptr; }
    resource->SetName(name);
    return resource;
}

bool device_lost(HRESULT hr) noexcept {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG;
}

void run(HWND game_window) {
    log("---- D3D12 core bring-up start (game window %p)", static_cast<void*>(game_window));
    Core core;
    std::string reason;
    if (!core.create(env_flag("EI_RENDERER_X64_D3D12_DEBUG"), reason)) { fail_visibly("D3D12 could not start", reason); return; }
    char adapter[256]{};
    WideCharToMultiByte(CP_UTF8, 0, core.adapter_name().c_str(), -1, adapter, sizeof(adapter) - 1, nullptr, nullptr);
    log("device: %s, SM 6.6 and binding tier 3 present", adapter);

    UploadRing upload;
    ReadbackRing readback;
    Timestamps timestamps;
    Pipelines pipelines;
    std::string note;
    const unsigned workers = (std::max)(1u, std::thread::hardware_concurrency() / 2);
    if (!upload.create(core.device(), reason) || !readback.create(core.device(), 1u << 20, reason) ||
        !timestamps.create(core.device(), core.queue(), reason) ||
        !pipelines.open(core.device(), g_directory + L"eid3d11_d3d12_pipelines.bin", workers, note, reason)) {
        fail_visibly("the D3D12 core could not start", reason);
        return;
    }
    if (!note.empty()) log("%s", note.c_str());

    // Pipelines: queued now, built on workers; frames omit their passes until ready.
    const D3D12_SHADER_BYTECODE cs = embedded_program(L"D3D12_DIAG_CS"), vs = embedded_program(L"D3D12_DIAG_VS"),
                                ps = embedded_program(L"D3D12_DIAG_PS");
    if (!cs.BytecodeLength || !vs.BytecodeLength || !ps.BytecodeLength) {
        fail_visibly("the D3D12 core could not start", "the host has no embedded diagnostic DXIL (D3D12_DIAG_*)");
        return;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};
    compute.pRootSignature = core.root_signature();
    compute.CS = cs;
    const std::uint32_t bars_pipeline = pipelines.request(L"diag.bars.cs", compute);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics{};
    graphics.pRootSignature = core.root_signature();
    graphics.VS = vs;
    graphics.PS = ps;
    auto& blend = graphics.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_ZERO;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    graphics.SampleMask = UINT_MAX;
    graphics.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    graphics.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    graphics.RasterizerState.DepthClipEnable = TRUE;
    graphics.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    graphics.NumRenderTargets = 1;
    graphics.RTVFormats[0] = Presenter::kFormat;
    graphics.SampleDesc.Count = 1;
    const std::uint32_t overlay_pipeline = pipelines.request(L"diag.overlay.bgra8", graphics);

    // Retained resources of the bring-up: the status text texture and the
    // bars buffer (sized by the pass count; replaced when it grows).
    D3D12_RESOURCE_DESC text_desc{};
    text_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    text_desc.Width = kTextWidth;
    text_desc.Height = kTextHeight;
    text_desc.DepthOrArraySize = 1;
    text_desc.MipLevels = 1;
    text_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    text_desc.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> text = create_resource(core.device(), text_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                  L"diag status text", reason);
    const std::uint32_t text_srv = core.views().allocate();
    if (!text || text_srv == UINT32_MAX) { fail_visibly("the D3D12 core could not start", reason.empty() ? "no descriptor" : reason); return; }
    core.device()->CreateShaderResourceView(text.Get(), nullptr, core.views().persistent_cpu(text_srv)); core.views().publish(core.device(), text_srv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT text_footprint{};
    UINT64 text_bytes = 0;
    core.device()->GetCopyableFootprints(&text_desc, 0, 1, 0, &text_footprint, nullptr, nullptr, &text_bytes);
    StatusText status;
    if (!status.create()) { fail_visibly("the D3D12 core could not start", "GDI text bitmap creation failed"); return; }

    ComPtr<ID3D12Resource> bars;
    std::uint32_t bars_capacity = 0;
    const std::uint32_t bars_srv = core.views().allocate(), bars_uav = core.views().allocate();
    if (bars_srv == UINT32_MAX || bars_uav == UINT32_MAX) { fail_visibly("the D3D12 core could not start", "no descriptor"); return; }

    Presenter presenter;
    if (!presenter.create(core, game_window, reason)) { fail_visibly("the D3D12 presenter could not start", reason); return; }
    log("presenter: %ux%u flip-discard child window, pipelines on %u workers", presenter.width(), presenter.height(), workers);
    // Thread ids, to match a host crash line (thread id) to this mode's threads.
    std::string ids;
    for (const DWORD id : pipelines.thread_ids()) ids += " " + std::to_string(id);
    log("threads: bring-up %lu, window %lu, pipeline workers%s", GetCurrentThreadId(),
        GetWindowThreadProcessId(presenter.window(), nullptr), ids.c_str());

    const std::uint64_t shot_frame = env_number("EI_RENDERER_X64_D3D12_SHOT");          // TEMPORARY bring-up
    const std::uint64_t remove_frame = env_number("EI_RENDERER_X64_D3D12_REMOVE_AT");   // TEMPORARY bring-up
    ReadbackTicket shot;
    UINT shot_width = 0, shot_height = 0, shot_pitch = 0;
    std::uint64_t shot_number = 0;
    BarrierPlan plan;
    std::string omitted, last_omitted;
    ULONGLONG last_text = 0;
    LARGE_INTEGER frequency{}, last_frame{};
    QueryPerformanceFrequency(&frequency);
    double cpu_ms = 0;
    bool pipelines_reported = false;

    while (!g_stop) {
        const Presenter::State state = presenter.prepare(core, reason);
        if (state == Presenter::State::failed) {
            const std::string removal = core.removal_report();
            fail_visibly(removal.empty() ? "the D3D12 presenter failed" : "the D3D12 device was removed",
                         removal.empty() ? reason : reason + "; " + removal);
            break;
        }
        if (state == Presenter::State::minimized) { Sleep(50); continue; }  // nothing visible to draw

        ID3D12GraphicsCommandList* list = core.begin_frame(reason);
        if (!list || !timestamps.begin_frame(core, reason)) {
            const std::string removal = core.removal_report();
            fail_visibly("the D3D12 device failed", removal.empty() ? reason : reason + "; " + removal);
            break;
        }
        const std::uint64_t completed = core.completed();
        upload.retire(completed);
        if (!pipelines_reported && pipelines.get(bars_pipeline) && pipelines.get(overlay_pipeline)) {
            log("pipelines ready: %u loaded from the library, %u created", pipelines.loaded(), pipelines.created());
            std::string save_reason;
            if (!pipelines.save(save_reason)) log("pipeline library not saved: %s", save_reason.c_str());
            pipelines_reported = true;
        }
        if (shot.buffer && ReadbackRing::ready(shot, completed)) {  // TEMPORARY bring-up
            const std::wstring path = g_directory + L"d3d12_core_frame" + std::to_wstring(shot_number) + L".bmp";
            log("screenshot frame %llu: %s", static_cast<unsigned long long>(shot_number),
                write_bmp(path, shot, shot_width, shot_height, shot_pitch) ? "written" : "write failed");
            readback.release(shot);
        }
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        if (last_frame.QuadPart) cpu_ms = cpu_ms * 0.95 + 0.05 * double(now.QuadPart - last_frame.QuadPart) * 1000.0 / double(frequency.QuadPart);
        last_frame = now;

        // The last completed frame's timings drive the bars and the text.
        const std::vector<PassTiming>& timings = timestamps.last();
        const std::uint32_t bar_count = static_cast<std::uint32_t>(timings.size());
        if (bar_count > bars_capacity) {
            if (bars) core.release_later(bars);
            bars_capacity = (std::max)(bar_count, 2 * bars_capacity);
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = std::uint64_t(bars_capacity) * sizeof(Bar);
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            bars = create_resource(core.device(), desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"diag bars", reason);
            if (!bars) { fail_visibly("the D3D12 core failed", reason); break; }
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Buffer.NumElements = bars_capacity;
            srv.Buffer.StructureByteStride = sizeof(Bar);
            core.device()->CreateShaderResourceView(bars.Get(), &srv, core.views().persistent_cpu(bars_srv)); core.views().publish(core.device(), bars_srv);
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.NumElements = bars_capacity;
            uav.Buffer.StructureByteStride = sizeof(Bar);
            core.device()->CreateUnorderedAccessView(bars.Get(), nullptr, &uav, core.views().persistent_cpu(bars_uav)); core.views().publish(core.device(), bars_uav);
        }

        const ULONGLONG tick = GetTickCount64();
        // Every 250 ms, and on the screenshot frame so the shot shows that frame's numbers.
        const bool text_changed = tick - last_text >= 250 || !last_text || (shot_frame && core.frame_number() == shot_frame);
        if (text_changed) {
            last_text = tick;
            std::vector<std::wstring> lines;
            wchar_t line[256];
            lines.push_back(L"EI RENDERER  D3D12 CORE  -  DIAGNOSTIC FRAME (EI_RENDERER_X64_D3D12_CORE=1)");
            lines.push_back(L"No frame plan yet: game content is NOT drawn by D3D12. The D3D11 host runs underneath.");
            swprintf_s(line, L"adapter %s   %ux%u   frame %llu   %.2f ms CPU/frame", core.adapter_name().c_str(),
                       presenter.width(), presenter.height(), static_cast<unsigned long long>(core.frame_number()), cpu_ms);
            lines.push_back(line);
            swprintf_s(line, L"upload ring %llu KB   pipelines %u loaded / %u created   barriers: %u transitions, %u split, %u UAV",
                       static_cast<unsigned long long>(upload.capacity() >> 10), pipelines.loaded(), pipelines.created(),
                       plan.transitions, plan.splits, plan.uav_barriers);
            lines.push_back(line);
            std::wstring omitted_text(last_omitted.begin(), last_omitted.end());
            lines.push_back(omitted_text.empty() ? L"omitted passes: none" : L"omitted passes: " + omitted_text);
            swprintf_s(line, L"GPU time per pass (frame %llu)%s", static_cast<unsigned long long>(core.frame_number() >= Core::kFramesInFlight
                                                                              ? core.frame_number() - Core::kFramesInFlight : 0),
                       timestamps.last_untimed() ? L"  (some passes untimed: slot grows next time)" : L"");
            lines.push_back(line);
            lines.push_back(L"");
            for (const PassTiming& timing : timings) {
                std::wstring name(timing.name.begin(), timing.name.end());
                swprintf_s(line, L"  %-18s %8.4f ms", name.c_str(), timing.ms);
                lines.push_back(line);
            }
            status.draw(lines);
        }

        // ---- the frame's plan -------------------------------------------------------
        enum Resource : std::uint32_t { kBackBuffer, kText, kBars };
        std::vector<PlanResource> resources = {
            {presenter.back_buffer(), 1, D3D12_RESOURCE_STATE_PRESENT},
            {text.Get(), 1, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE},
            {bars.Get(), 1, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE},
        };
        omitted.clear();
        ID3D12PipelineState* bars_pso = pipelines.get(bars_pipeline);
        ID3D12PipelineState* overlay_pso = pipelines.get(overlay_pipeline);
        const bool take_shot = shot_frame && core.frame_number() == shot_frame && !shot.buffer;
        std::vector<PlanPass> passes;
        enum PassKind { kTextUpload, kClear, kBarsCompute, kOverlay, kShot };
        std::vector<PassKind> kinds;
        if (text_changed) { passes.push_back({"text_upload", {{kText, kAllSubresources, D3D12_RESOURCE_STATE_COPY_DEST, true}}}); kinds.push_back(kTextUpload); }
        passes.push_back({"clear", {{kBackBuffer, kAllSubresources, D3D12_RESOURCE_STATE_RENDER_TARGET, true}}});
        kinds.push_back(kClear);
        if (bars_pso && bar_count) {
            passes.push_back({"bars_compute", {{kBars, kAllSubresources, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true}}});
            kinds.push_back(kBarsCompute);
        } else if (!bars_pso) omitted += "bars_compute (pipeline not ready) ";
        if (overlay_pso) {
            passes.push_back({"overlay", {{kBackBuffer, kAllSubresources, D3D12_RESOURCE_STATE_RENDER_TARGET, true},
                                          {kText, kAllSubresources, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, false}}});
            if (bars) passes.back().uses.push_back({kBars, kAllSubresources, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, false});
            kinds.push_back(kOverlay);
        } else omitted += "overlay (pipeline not ready) ";
        if (take_shot) { passes.push_back({"screenshot_copy", {{kBackBuffer, kAllSubresources, D3D12_RESOURCE_STATE_COPY_SOURCE, false}}}); kinds.push_back(kShot); }
        if (omitted != last_omitted) {
            log("omitted passes: %s", omitted.empty() ? "none" : omitted.c_str());
            last_omitted = omitted;
        }
        if (!compile_barriers(resources, passes, plan, reason)) { fail_visibly("the barrier plan is malformed", reason); break; }

        FrameConstants frame{};
        frame.viewport[0] = float(presenter.width());
        frame.viewport[1] = float(presenter.height());
        frame.text_origin[0] = kTextX;
        frame.text_origin[1] = kTextY;
        frame.text_size[0] = float(kTextWidth);
        frame.text_size[1] = float(kTextHeight);
        frame.bar_x = kTextX + 300;
        frame.bar_y0 = kTextY + float(kPassLine0 * kTextLine);
        frame.row_height = float(kTextLine);
        frame.px_per_ms = 2000;  // 0.1 ms = 200 px
        frame.bar_max_width = (std::max)(16.0f, float(presenter.width()) - frame.bar_x - 24);
        UploadAllocation frame_cb, values;
        bool ok = upload.push(core, &frame, sizeof(frame), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, frame_cb, reason);
        std::uint32_t values_srv = UINT32_MAX;
        if (ok && bar_count) {
            ok = upload.allocate(core, bar_count * sizeof(float), sizeof(float), values, reason);
            values_srv = ok ? core.views().transient(1) : UINT32_MAX;
            if (ok && values_srv == UINT32_MAX) { ok = false; reason = "the transient descriptor ring is full"; }
            if (ok) {
                for (std::uint32_t i = 0; i < bar_count; ++i) reinterpret_cast<float*>(values.cpu)[i] = float(timings[i].ms);
                D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
                srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv.Buffer.FirstElement = values.offset / sizeof(float);
                srv.Buffer.NumElements = bar_count;
                srv.Buffer.StructureByteStride = sizeof(float);
                core.device()->CreateShaderResourceView(values.resource, &srv, core.views().persistent_cpu(values_srv)); core.views().publish(core.device(), values_srv);
            }
        }
        if (!ok) { fail_visibly("the D3D12 core failed", reason); break; }
        const UINT root[kRootConstantCount] = {bars_uav, bars_srv, values_srv, text_srv, bar_count, 0, 0, 0};

        for (std::size_t p = 0; p < passes.size(); ++p) {
            issue_barriers(list, plan, p);
            const std::uint32_t timer = timestamps.begin(list, passes[p].name);
            switch (kinds[p]) {
            case kTextUpload: {
                UploadAllocation source;
                if (!upload.allocate(core, text_bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, source, reason)) { ok = false; break; }
                for (UINT y = 0; y < kTextHeight; ++y)
                    std::memcpy(source.cpu + std::size_t(y) * text_footprint.Footprint.RowPitch,
                                status.pixels() + std::size_t(y) * kTextWidth * 4, kTextWidth * 4);
                D3D12_TEXTURE_COPY_LOCATION dst{text.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
                dst.SubresourceIndex = 0;
                D3D12_TEXTURE_COPY_LOCATION src{source.resource, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
                src.PlacedFootprint = text_footprint;
                src.PlacedFootprint.Offset = source.offset;
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                break;
            }
            case kClear: {
                const float purple[4] = {0.32f, 0.05f, 0.38f, 1.0f};  // diagnostic: never a game colour
                list->ClearRenderTargetView(presenter.rtv(), purple, 0, nullptr);
                break;
            }
            case kBarsCompute:
                list->SetPipelineState(bars_pso);
                list->SetComputeRoot32BitConstants(kRootConstants, kRootConstantCount, root, 0);
                list->SetComputeRootConstantBufferView(kRootFrame, frame_cb.gpu);
                list->Dispatch((bar_count + 63) / 64, 1, 1);
                break;
            case kOverlay: {
                const D3D12_CPU_DESCRIPTOR_HANDLE rtv = presenter.rtv();
                list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                const D3D12_VIEWPORT viewport{0, 0, frame.viewport[0], frame.viewport[1], 0, 1};
                const D3D12_RECT scissor{0, 0, LONG(presenter.width()), LONG(presenter.height())};
                list->RSSetViewports(1, &viewport);
                list->RSSetScissorRects(1, &scissor);
                list->SetPipelineState(overlay_pso);
                list->SetGraphicsRoot32BitConstants(kRootConstants, kRootConstantCount, root, 0);
                list->SetGraphicsRootConstantBufferView(kRootFrame, frame_cb.gpu);
                list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                // Bars only once the compute pass has filled the buffer this frame.
                const bool bars_written = bars_pso && bar_count;
                list->DrawInstanced(6, 1 + (bars_written ? bar_count : 0), 0, 0);
                break;
            }
            case kShot: {  // TEMPORARY bring-up
                D3D12_RESOURCE_DESC desc = presenter.back_buffer()->GetDesc();
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
                UINT64 bytes = 0;
                core.device()->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
                if (!readback.allocate(core, bytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, shot, reason)) { ok = false; break; }
                footprint.Offset = shot.offset;
                D3D12_TEXTURE_COPY_LOCATION dst{shot.buffer.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, {}};
                dst.PlacedFootprint = footprint;
                D3D12_TEXTURE_COPY_LOCATION src{presenter.back_buffer(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, {}};
                src.SubresourceIndex = 0;
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                shot_width = presenter.width();
                shot_height = presenter.height();
                shot_pitch = footprint.Footprint.RowPitch;
                shot_number = core.frame_number();
                break;
            }
            }
            timestamps.end(list, timer);
            if (!ok) break;
        }
        if (!ok) { fail_visibly("the D3D12 core failed", reason); break; }
        issue_barriers(list, plan, passes.size());
        timestamps.resolve(list);
        const std::uint64_t value = core.end_frame(reason);
        if (!value) {
            const std::string removal = core.removal_report();
            fail_visibly("the D3D12 device failed", removal.empty() ? reason : reason + "; " + removal);
            break;
        }
        upload.end_frame(value);

        if (remove_frame && core.frame_number() == remove_frame) {  // TEMPORARY bring-up
            ComPtr<ID3D12Device5> device5;
            if (SUCCEEDED(core.device()->QueryInterface(IID_PPV_ARGS(&device5)))) {
                log("EI_RENDERER_X64_D3D12_REMOVE_AT: removing the device at frame %llu", static_cast<unsigned long long>(remove_frame));
                device5->RemoveDevice();
            }
        }
        const HRESULT presented = presenter.present();
        if (FAILED(presented)) {
            const std::string removal = core.removal_report();
            fail_visibly(device_lost(presented) ? "the D3D12 device was removed" : "Present failed",
                         hresult_text("IDXGISwapChain::Present", presented) + (removal.empty() ? std::string() : "; " + removal));
            break;
        }
    }

    // Teardown: the GPU finishes, the library keeps what was built.
    std::string save_reason;
    pipelines.wait_all();
    if (!pipelines.save(save_reason)) log("pipeline library not saved: %s", save_reason.c_str());
    if (shot.buffer) readback.release(shot);
    // A removed device leaves the window as it was until the host exits.
    while (!g_stop && FAILED(core.removed_reason())) Sleep(100);
    presenter.destroy(core);
    log("---- D3D12 core bring-up stop after %llu frames", static_cast<unsigned long long>(core.frame_number()));
}

} // namespace

void start_core_bringup(HWND game_window, const std::wstring& directory) {
    g_directory = directory;
    g_stop = false;
    g_thread = std::thread([game_window] { run(game_window); });
}

// The host must never outlive the game: the bring-up gets 10 s to tear down
// (GPU idle, library saved), then the host exits regardless; process exit
// also closes an open failure message box (it runs on its own thread).
void stop_core_bringup() {
    g_stop = true;
    if (!g_thread.joinable()) return;
    if (WaitForSingleObject(g_thread.native_handle(), 10000) == WAIT_OBJECT_0) {
        g_thread.join();
        return;
    }
    log("teardown did not finish within 10 s; the host exits without it");
    g_thread.detach();
}

} // namespace d3d12
