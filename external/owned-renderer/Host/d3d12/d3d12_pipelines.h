#pragma once
// Pipeline state objects built ahead (plan 2.4): requests are created on
// worker threads, through an ID3D12PipelineLibrary kept in the host data
// folder so later runs load instead of compile. A frame asks get(); a
// pipeline that is not ready yet is created at once (wait: one hitch, named
// in the log), and only a failed one leaves its pass omitted and named.
#include "d3d12_core.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace d3d12 {

class Pipelines {
public:
    ~Pipelines();
    // Opens (or starts) the library at path and starts workers. A library a
    // different driver or adapter wrote is replaced by an empty one (note
    // says so); only device failures return false.
    // Optional startup display snapshot (completed attempts << 32 | requested),
    // published without window calls. Storage must outlive these workers.
    bool open(ID3D12Device* device, const std::wstring& path, unsigned workers, std::string& note, std::string& reason,
              std::atomic<std::uint64_t>* startup_progress = nullptr);
    void end_startup_progress();  // stops publication; the last snapshot stays visible
    // Queues a pipeline under a unique name (the same name returns the same
    // id). The description's pointers (shader bytecode, input layout, root
    // signature) must stay valid until the pipeline is ready: static tables,
    // embedded DXIL and the global root signature.
    std::uint32_t request(const std::wstring& name, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);
    std::uint32_t request(const std::wstring& name, const D3D12_COMPUTE_PIPELINE_STATE_DESC& desc);
    // The pipeline, or null while it is being built or after it failed.
    ID3D12PipelineState* get(std::uint32_t id) const;
    // The failure of a pipeline that will never be ready, else empty.
    std::string failure(std::uint32_t id) const;
    // How long a finished pipeline took to load or create (0 while building).
    double milliseconds(std::uint32_t id) const;
    // Blocks until every queued pipeline is ready or failed.
    void wait_all();
    // This pipeline now (a draw found it missing): built on the calling
    // thread when still queued, else waited for on its worker. Null only
    // when it failed (failure names why).
    ID3D12PipelineState* wait(std::uint32_t id);
    // No request is queued or building.
    bool idle() const;
    // Pipelines requested so far (ready, failed or still building).
    std::uint32_t requested() const;
    // Writes the library when pipelines were added: a new library holding
    // exactly this session's ready pipelines, so entries no run requests any
    // more (an older build's programs) do not pile up in the file. Call with
    // no requests outstanding (idle, after wait_all or at teardown).
    // written: the file was replaced (false: unchanged or failed).
    bool save(std::string& reason, bool* written = nullptr);
    // The last write: session pipelines in the file, how many the new library
    // refused (non-zero: the loaded library was written instead), file bytes.
    std::uint32_t saved_count() const noexcept { return saved_count_; }
    std::uint32_t save_refused() const noexcept { return save_refused_; }
    std::size_t saved_bytes() const noexcept { return saved_bytes_; }
    std::vector<DWORD> thread_ids() {
        std::vector<DWORD> ids;
        for (auto& worker : workers_) ids.push_back(GetThreadId(worker.native_handle()));
        return ids;
    }
    std::uint32_t loaded() const noexcept { return loaded_; }
    std::uint32_t created() const noexcept { return created_; }

private:
    struct Entry {
        std::wstring name;
        bool compute = false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC graphics{};
        D3D12_COMPUTE_PIPELINE_STATE_DESC compute_desc{};
        ComPtr<ID3D12PipelineState> pipeline;
        std::string error;
        bool done = false;
        double milliseconds = 0.0;
    };
    std::uint32_t enqueue(std::unique_ptr<Entry> entry);
    void work();
    void build(Entry& entry);
    void publish_startup_progress();  // mutex_ held

    ID3D12Device* device_ = nullptr;
    ComPtr<ID3D12Device1> device1_;
    std::uint32_t saved_count_ = 0, save_refused_ = 0;
    std::size_t saved_bytes_ = 0;
    std::vector<char> library_bytes_;  // the library reads from this for its lifetime: declared first, freed last
    ComPtr<ID3D12PipelineLibrary> library_;
    std::wstring path_;
    std::mutex library_mutex_;
    mutable std::mutex mutex_;
    std::condition_variable work_ready_, work_done_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::deque<std::uint32_t> queue_;
    std::uint32_t outstanding_ = 0;
    std::atomic<std::uint64_t>* startup_progress_ = nullptr;
    bool stop_ = false, dirty_ = false;
    std::atomic<std::uint32_t> loaded_{0}, created_{0};
    std::vector<std::thread> workers_;
};

} // namespace d3d12
