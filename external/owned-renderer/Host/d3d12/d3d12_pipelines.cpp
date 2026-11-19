#include "d3d12_pipelines.h"
#include <new>

namespace remote { void host_log(const char* format, ...); }

namespace d3d12 {
Pipelines::~Pipelines() {
    if (set_) set_->destroy();
}
bool Pipelines::open(ID3D12Device* device, const std::wstring& path, unsigned workers,
                     std::string& note, std::string& reason, std::atomic<std::uint64_t>* startup_progress) {
    if (set_) { reason = "Pipeline library already open"; return false; }
    EiPipelineHostV1 host{};
    host.context = startup_progress;
    if (startup_progress) host.progress = [](void* user, UINT64 value) noexcept {
        static_cast<std::atomic<std::uint64_t>*>(user)->store(value, std::memory_order_relaxed);
    };
    host.pending = [](UINT outstanding) noexcept {
        remote::host_log("d3d12: pipeline compilation still pending after 20 s: %u requests; waiting before using or releasing PSO inputs", outstanding);
    };
    char message[512]{}, error[512]{};
    const HRESULT hr = EiOpenPipelines12V1(device, path.c_str(), workers, host, message, sizeof(message),
                                           error, sizeof(error), &set_);
    note = message; reason = error;
    return SUCCEEDED(hr);
}
void Pipelines::end_startup_progress() { if (set_) set_->endStartup(); }
std::uint32_t Pipelines::request(const std::wstring& name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    UINT id;
    if (FAILED(set_->graphics(name.c_str(), &desc, &id))) throw std::bad_alloc();
    return id;
}
std::uint32_t Pipelines::request(const std::wstring& name, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc) {
    UINT id;
    if (FAILED(set_->compute(name.c_str(), &desc, &id))) throw std::bad_alloc();
    return id;
}
ID3D12PipelineState* Pipelines::get(std::uint32_t id) const { return set_ ? set_->get(id) : nullptr; }
ID3D12PipelineState* Pipelines::wait(std::uint32_t id) { return set_ ? set_->wait(id) : nullptr; }
void Pipelines::wait_all() { if (set_) set_->waitAll(); }
bool Pipelines::idle() const { return !set_ || set_->idle(); }
std::uint32_t Pipelines::requested() const { return stats().requested; }
double Pipelines::milliseconds(std::uint32_t id) const { return set_ ? set_->milliseconds(id) : 0.0; }
std::string Pipelines::failure(std::uint32_t id) const {
    char error[512]{};
    if (set_) set_->failure(id, error, sizeof(error));
    return error;
}
bool Pipelines::save(std::string& reason, bool* written) {
    char error[512]{};
    BOOL changed = FALSE;
    const bool ok = !set_ || set_->save(error, sizeof(error), &changed);
    if (written) *written = changed != FALSE;
    reason = error;
    return ok;
}
std::vector<DWORD> Pipelines::thread_ids() {
    std::vector<DWORD> result(set_ ? set_->threadIds(nullptr, 0) : 0);
    if (!result.empty()) set_->threadIds(result.data(), static_cast<UINT>(result.size()));
    return result;
}
} // namespace d3d12
