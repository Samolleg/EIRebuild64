// Pipelines built ahead on workers through a pipeline library; see d3d12_pipelines.h.
#include "d3d12_pipelines.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>

namespace remote { void host_log(const char* format, ...); }

namespace d3d12 {

Pipelines::~Pipelines() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    work_ready_.notify_all();
    for (auto& worker : workers_) worker.join();
}

bool Pipelines::open(ID3D12Device* device, const std::wstring& path, unsigned workers, std::string& note, std::string& reason,
                     std::atomic<std::uint64_t>* startup_progress) {
    device_ = device;
    path_ = path;
    startup_progress_ = startup_progress;
    ComPtr<ID3D12Device1>& device1 = device1_;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&device1));
    if (FAILED(hr)) { reason = hresult_text("QueryInterface(ID3D12Device1) for the pipeline library", hr); return false; }
    {
        std::ifstream in(path, std::ios::binary);
        if (in) library_bytes_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (!library_bytes_.empty()) {
        hr = device1->CreatePipelineLibrary(library_bytes_.data(), library_bytes_.size(), IID_PPV_ARGS(&library_));
        if (FAILED(hr)) {
            // Another driver, adapter or a damaged file: start empty and rewrite it.
            note = hresult_text("the saved pipeline library could not be used; CreatePipelineLibrary", hr) +
                   (hr == D3D12_ERROR_DRIVER_VERSION_MISMATCH ? " (driver changed)"
                    : hr == D3D12_ERROR_ADAPTER_NOT_FOUND     ? " (adapter changed)"
                                                              : "") +
                   "; starting an empty library";
            library_bytes_.clear();
            dirty_ = true;
        }
    }
    if (!library_) {
        hr = device1->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&library_));
        if (FAILED(hr)) { reason = hresult_text("CreatePipelineLibrary (empty)", hr); return false; }
    }
    workers = (std::max)(1u, workers);
    for (unsigned i = 0; i < workers; ++i) workers_.emplace_back([this] { work(); });
    return true;
}

std::uint32_t Pipelines::enqueue(std::unique_ptr<Entry> entry) {
    std::uint32_t id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::uint32_t i = 0; i < entries_.size(); ++i)
            if (entries_[i]->name == entry->name) return i;
        id = static_cast<std::uint32_t>(entries_.size());
        entries_.push_back(std::move(entry));
        queue_.push_back(id);
        ++outstanding_;
        publish_startup_progress();
    }
    work_ready_.notify_one();
    return id;
}

std::uint32_t Pipelines::request(const std::wstring& name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc) {
    auto entry = std::make_unique<Entry>();
    entry->name = name;
    entry->graphics = desc;
    return enqueue(std::move(entry));
}

std::uint32_t Pipelines::request(const std::wstring& name, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc) {
    auto entry = std::make_unique<Entry>();
    entry->name = name;
    entry->compute = true;
    entry->compute_desc = desc;
    return enqueue(std::move(entry));
}

void Pipelines::work() {
    for (;;) {
        Entry* entry = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_ready_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            entry = entries_[queue_.front()].get();
            queue_.pop_front();
        }
        build(*entry);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entry->done = true;
            --outstanding_;
            publish_startup_progress();
        }
        work_done_.notify_all();
    }
}

// Load from the library, else create and store. Library calls are
// serialised; the driver compile (Create*PipelineState) runs in parallel.
void Pipelines::build(Entry& entry) {
    LARGE_INTEGER frequency{}, begin{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin);
    const auto elapsed = [&] {
        LARGE_INTEGER end{};
        QueryPerformanceCounter(&end);
        entry.milliseconds = double(end.QuadPart - begin.QuadPart) * 1000.0 / double(frequency.QuadPart);
    };
    ComPtr<ID3D12PipelineState> pipeline;
    HRESULT hr;
    {
        std::lock_guard<std::mutex> lock(library_mutex_);
        hr = entry.compute ? library_->LoadComputePipeline(entry.name.c_str(), &entry.compute_desc, IID_PPV_ARGS(&pipeline))
                           : library_->LoadGraphicsPipeline(entry.name.c_str(), &entry.graphics, IID_PPV_ARGS(&pipeline));
    }
    if (SUCCEEDED(hr)) {
        entry.pipeline = pipeline;
        ++loaded_;
        elapsed();
        return;
    }
    // E_INVALIDARG: not in the library, or stored with another description.
    hr = entry.compute ? device_->CreateComputePipelineState(&entry.compute_desc, IID_PPV_ARGS(&pipeline))
                       : device_->CreateGraphicsPipelineState(&entry.graphics, IID_PPV_ARGS(&pipeline));
    if (FAILED(hr)) {
        char name[160];
        WideCharToMultiByte(CP_UTF8, 0, entry.name.c_str(), -1, name, sizeof(name), nullptr, nullptr);
        entry.error = hresult_text(entry.compute ? "CreateComputePipelineState" : "CreateGraphicsPipelineState", hr) +
                      " for " + name;
        return;
    }
    pipeline->SetName(entry.name.c_str());
    entry.pipeline = pipeline;
    ++created_;
    elapsed();
    // Also kept in the loaded library: save() falls back to writing that one
    // when the driver refuses a pipeline for the new library.
    std::lock_guard<std::mutex> lock(library_mutex_);
    (void)library_->StorePipeline(entry.name.c_str(), pipeline.Get());
    dirty_ = true;
}

double Pipelines::milliseconds(std::uint32_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return id < entries_.size() && entries_[id]->done ? entries_[id]->milliseconds : 0.0;
}

ID3D12PipelineState* Pipelines::get(std::uint32_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return id < entries_.size() && entries_[id]->done ? entries_[id]->pipeline.Get() : nullptr;
}

std::string Pipelines::failure(std::uint32_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return id < entries_.size() && entries_[id]->done ? entries_[id]->error : std::string();
}

void Pipelines::wait_all() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!work_done_.wait_for(lock, std::chrono::seconds(20), [this] { return outstanding_ == 0; })) {
        const auto outstanding = outstanding_;
        lock.unlock();
        remote::host_log("d3d12: pipeline compilation still pending after 20 s: %u requests; waiting before using or releasing PSO inputs", outstanding);
        lock.lock();
    }
}

ID3D12PipelineState* Pipelines::wait(std::uint32_t id) {
    Entry* entry = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (id >= entries_.size()) return nullptr;
        entry = entries_[id].get();
        if (entry->done) return entry->pipeline.Get();
        const auto queued = std::find(queue_.begin(), queue_.end(), id);
        if (queued == queue_.end()) {
            // A worker is building it: wait for that one.
            work_done_.wait(lock, [entry] { return entry->done; });
            return entry->pipeline.Get();
        }
        queue_.erase(queued);  // still queued: built here, now
    }
    build(*entry);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entry->done = true;
        --outstanding_;
        publish_startup_progress();
    }
    work_done_.notify_all();
    return entry->pipeline.Get();
}

void Pipelines::publish_startup_progress() {
    if (startup_progress_) {
        const std::uint64_t total = entries_.size();
        startup_progress_->store(((total - outstanding_) << 32) | total, std::memory_order_relaxed);
    }
}

void Pipelines::end_startup_progress() {
    std::lock_guard<std::mutex> lock(mutex_);
    startup_progress_ = nullptr;
}

std::uint32_t Pipelines::requested() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<std::uint32_t>(entries_.size());
}

bool Pipelines::idle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outstanding_ == 0;
}

bool Pipelines::save(std::string& reason, bool* written) {
    if (written) *written = false;
    std::vector<std::pair<const wchar_t*, ID3D12PipelineState*>> ready;  // entries_ never shrink: the pointers stay valid
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : entries_)
            if (entry->done && entry->pipeline) ready.emplace_back(entry->name.c_str(), entry->pipeline.Get());
    }
    std::lock_guard<std::mutex> lock(library_mutex_);
    if (!dirty_ || !device1_ || !library_) return true;  // unchanged: the file already holds them
    ComPtr<ID3D12PipelineLibrary> fresh;
    HRESULT hr = device1_->CreatePipelineLibrary(nullptr, 0, IID_PPV_ARGS(&fresh));
    if (FAILED(hr)) { reason = hresult_text("CreatePipelineLibrary (to save)", hr); return false; }
    std::uint32_t refused = 0;
    for (const auto& pipeline : ready)
        if (FAILED(fresh->StorePipeline(pipeline.first, pipeline.second))) ++refused;
    // A refused pipeline would be missing from the new library: write the
    // loaded library instead (the file's pipelines plus this session's new
    // ones), so the file never loses a pipeline in use.
    ID3D12PipelineLibrary* const written_library = refused ? library_.Get() : fresh.Get();
    std::vector<char> bytes(written_library->GetSerializedSize());
    hr = written_library->Serialize(bytes.data(), bytes.size());
    if (FAILED(hr)) { reason = hresult_text("ID3D12PipelineLibrary::Serialize", hr); return false; }
    const std::wstring temporary = path_ + L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) { reason = "cannot write the pipeline library file"; return false; }
    }
    if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        reason = hresult_text("MoveFileEx (pipeline library)", HRESULT_FROM_WIN32(GetLastError()));
        return false;
    }
    dirty_ = false;
    if (written) *written = true;
    saved_count_ = static_cast<std::uint32_t>(ready.size());  // in either library
    save_refused_ = refused;
    saved_bytes_ = bytes.size();
    return true;
}

} // namespace d3d12
