// Frame generation pacer; see d3d12_pacer.h.
#include "d3d12_pacer.h"

#include "d3d12_presenter.h"
#include "../reflex.h"

#include <algorithm>
#include <cmath>

namespace remote { void host_log(const char* format, ...); }

namespace d3d12 {

bool Pacer::start(Core& core, std::string& reason) {
    if (running()) return true;
    core_ = &core;
    // RTSS's D3D11-on-12 overlay also writes the swap buffers on this
    // device's main queue (the validation ERROR 907 call stack). All swap
    // buffer writes and Present must use their associated direct queue.
    queue_ = core.queue();
    HRESULT hr = S_OK;
    if (FAILED(hr = core.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
        reason = hresult_text("CreateFence (frame generation presents)", hr);
        return false;
    }
    for (auto& allocator : allocators_)
        if (FAILED(hr = core.device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) {
            reason = hresult_text("CreateCommandAllocator (frame generation presents)", hr);
            return false;
        }
    if (FAILED(hr = core.device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators_[0].Get(), nullptr, IID_PPV_ARGS(&list_)))) {
        reason = hresult_text("CreateCommandList (frame generation presents)", hr);
        return false;
    }
    list_->Close();
    event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    main_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!event_ || !main_event_) { reason = "CreateEvent (frame generation presents) failed"; return false; }
    value_ = 0;
    std::fill(std::begin(allocator_values_), std::end(allocator_values_), 0);
    std::fill(std::begin(slot_values_), std::end(slot_values_), 0);
    stop_ = pending_ = busy_ = false;
    last_done_ = last_present_ = window_start_ = 0;
    spacing_sum_ = spacing_squares_ = spacing_max_ = hold_sum_ = 0;
    spacing_count_ = hold_count_ = window_generated_ = window_real_ = 0;
    generated_after_sum_ = real_call_sum_ = 0;
    generated_after_count_ = real_call_count_ = 0;
    interval_ms_ = 0;
    stats_ = {};
    try {
        thread_ = std::thread([this] { run(); });
    } catch (...) {
        reason = "the frame generation present thread could not be started";
        return false;
    }
    return true;
}

void Pacer::stop() noexcept {
    if (thread_.joinable()) {
        drain();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
    }
    limiter_.wait(0);
    if (fence_ && value_) wait_value(value_);
    list_.Reset();
    for (auto& allocator : allocators_) allocator.Reset();
    fence_.Reset();
    queue_.Reset();
    for (HANDLE* handle : {&event_, &main_event_, &timer_})
        if (*handle) { CloseHandle(*handle); *handle = nullptr; }
    presenter_ = nullptr;
}

void Pacer::submit(Presenter& presenter, ID3D12Resource* generated, ID3D12Resource* real, std::uint64_t main_value, unsigned slot,
                   std::int64_t game_start, std::int64_t host_taken, UINT sync_interval, int frame_limit) noexcept {
    LARGE_INTEGER called{};
    QueryPerformanceCounter(&called);  // before the back-pressure wait: the host's frame is recorded
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return !pending_ && !busy_; });
    presenter_ = &presenter;
    frame_ = Frame{generated, real, main_value, slot % kSlots, game_start, host_taken, called.QuadPart, sync_interval, frame_limit};
    pending_ = true;
    wake_.notify_one();
}

void Pacer::drain() noexcept {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [this] { return !pending_ && !busy_; });
    }
    std::uint64_t value = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        value = value_;
    }
    if (value) wait_value(value);
}

void Pacer::wait_slot(unsigned slot) noexcept {
    std::uint64_t value = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        value = slot_values_[slot % kSlots];
    }
    if (value) wait_value(value);
}

Pacer::Stats Pacer::stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    Stats stats = stats_;
    stats.real_interval_ms = interval_ms_;
    return stats;
}

// The pacer fence reaching value (any thread: its own event per call).
void Pacer::wait_value(std::uint64_t value) noexcept {
    if (!fence_ || fence_->GetCompletedValue() >= value) return;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event && SUCCEEDED(fence_->SetEventOnCompletion(value, event))) WaitForSingleObject(event, 5000);
    if (event) CloseHandle(event);
}

// A high-resolution timer to about a millisecond before, then a short spin
// (the busy wait the FidelityFX pacer documents for even spacing).
void Pacer::sleep_until(std::int64_t deadline, std::int64_t frequency, HANDLE timer) noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const std::int64_t spin = frequency / 1000;  // the last millisecond
    if (timer && deadline - now.QuadPart > spin) {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>((deadline - now.QuadPart - spin) * 10000000 / frequency);  // 100 ns units, relative
        if (SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0)) WaitForSingleObject(timer, 1000);
    }
    for (;;) {
        QueryPerformanceCounter(&now);
        if (now.QuadPart >= deadline) return;
        YieldProcessor();
    }
}

// One copy into the current swap-chain buffer on its owning queue, then Present.
bool Pacer::copy_and_present(ID3D12Resource* source, std::uint64_t& signalled, const Frame& frame) noexcept {
    presenter_->wait_frame_latency();
    const unsigned index = next_allocator_;
    next_allocator_ = (next_allocator_ + 1) % kLists;
    wait_value(allocator_values_[index]);
    ID3D12CommandAllocator* allocator = allocators_[index].Get();
    if (FAILED(allocator->Reset()) || FAILED(list_->Reset(allocator, nullptr))) return false;
    ID3D12Resource* back = presenter_->back_buffer();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = back;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list_->ResourceBarrier(1, &barrier);
    list_->CopyResource(back, source);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list_->ResourceBarrier(1, &barrier);
    if (FAILED(list_->Close())) return false;
    ID3D12CommandList* lists[] = {list_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    limiter_.wait(frame.frame_limit);
    const HRESULT hr = presenter_->present(frame.sync_interval);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (last_present_) {
        const double ms = 1000.0 * double(now.QuadPart - last_present_) / double(frequency_);
        if (ms < 100.0) { spacing_sum_ += ms; spacing_squares_ += ms * ms; spacing_max_ = (std::max)(spacing_max_, ms); ++spacing_count_; }
    }
    last_present_ = now.QuadPart;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++value_;
        signalled = value_;
    }
    queue_->Signal(fence_.Get(), signalled);
    allocator_values_[index] = signalled;
    if (FAILED(hr)) {
        static bool reported = false;
        if (!reported) { reported = true; remote::host_log("d3d12: frame generation Present failed (0x%08lX)", static_cast<unsigned long>(hr)); }
        return false;
    }
    return true;
}

void Pacer::run() noexcept {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    frequency_ = frequency.QuadPart;
    for (;;) {
        Frame frame;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return pending_ || stop_; });
            if (!pending_) return;
            frame = frame_;
            pending_ = false;
            busy_ = true;
        }
        // The real frame's GPU work (the generated image is part of it).
        ID3D12Fence* main = core_->fence();
        if (main->GetCompletedValue() < frame.main_value && SUCCEEDED(main->SetEventOnCompletion(frame.main_value, main_event_)))
            WaitForSingleObject(main_event_, 5000);
        LARGE_INTEGER done{};
        QueryPerformanceCounter(&done);
        double interval = 0;
        if (last_done_) {
            const double sample = 1000.0 * double(done.QuadPart - last_done_) / double(frequency.QuadPart);
            // A stall or pause starts the average over instead of skewing it.
            interval = sample > 0 && sample < 100.0 ? (interval_ms_ > 0 ? interval_ms_ * 0.8 + sample * 0.2 : sample) : 0;
        }
        last_done_ = done.QuadPart;
        std::uint64_t signalled = 0;
        bool generated_shown = false;
        if (frame.generated) {
            generated_shown = copy_and_present(frame.generated, signalled, frame);
            if (generated_shown) { generated_after_sum_ += 1000.0 * double(last_present_ - done.QuadPart) / double(frequency.QuadPart); ++generated_after_count_; }
            // The real image half a real-frame interval after the generated
            // one (no interval yet: at once).
            // With VSync, the swap chain's latency wait/vblank establishes
            // the spacing; adding the measured half-interval skips vblanks.
            if (generated_shown && interval > 0 && !frame.sync_interval) {
                LARGE_INTEGER shown{};
                QueryPerformanceCounter(&shown);
                sleep_until(shown.QuadPart + std::int64_t(interval * 0.5 * double(frequency.QuadPart) / 1000.0), frequency.QuadPart, timer_);
            }
        }
        LARGE_INTEGER real_start{};
        QueryPerformanceCounter(&real_start);
        const bool real_shown = copy_and_present(frame.real, signalled, frame);
        if (real_shown) reflex::real_presented(frame.game_start);
        if (real_shown && frame.game_start > 0 && frame.host_taken >= frame.game_start && frame.submitted >= frame.host_taken &&
            done.QuadPart >= frame.submitted) {
            const double to_ms = 1000.0 / double(frequency.QuadPart);
            stage_taken_sum_ += double(frame.host_taken - frame.game_start) * to_ms;
            stage_submitted_sum_ += double(frame.submitted - frame.game_start) * to_ms;
            stage_done_sum_ += double(done.QuadPart - frame.game_start) * to_ms;
            stage_real_sum_ += double(last_present_ - frame.game_start) * to_ms;
            ++stage_count_;
        }
        if (real_shown) { real_call_sum_ += 1000.0 * double(last_present_ - real_start.QuadPart) / double(frequency.QuadPart); ++real_call_count_; }
        // Measurement (EI frame generation stage): the spacing of Present
        // calls and how long the real image is held after its GPU work ends
        // (frame generation's added display latency), every 5 s in host.log.
        if (real_shown) { hold_sum_ += 1000.0 * double(last_present_ - done.QuadPart) / double(frequency.QuadPart); ++hold_count_; }
        if (!window_start_) window_start_ = done.QuadPart;
        if (double(done.QuadPart - window_start_) >= 5.0 * double(frequency.QuadPart) && spacing_count_) {
            const double mean = spacing_sum_ / double(spacing_count_);
            const double sd = std::sqrt((std::max)(0.0, spacing_squares_ / double(spacing_count_) - mean * mean));
            const double seconds = double(done.QuadPart - window_start_) / double(frequency.QuadPart);
            std::uint64_t generated = 0, real = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                generated = stats_.generated - window_generated_;
                real = stats_.real - window_real_;
                window_generated_ = stats_.generated;
                window_real_ = stats_.real;
            }
            remote::host_log("d3d12: frame generation pacing over %.1f s: presents %.1f/s (real %.1f/s, generated %.1f/s); present spacing "
                             "mean %.2f ms sd %.2f ms max %.2f ms; real interval %.2f ms; real image held %.2f ms after its GPU work "
                             "(generated presented %.2f ms after it; real copy+Present call %.2f ms)",
                             seconds, double(generated + real) / seconds, double(real) / seconds, double(generated) / seconds, mean, sd,
                             spacing_max_, interval, hold_count_ ? hold_sum_ / double(hold_count_) : 0.0,
                             generated_after_count_ ? generated_after_sum_ / double(generated_after_count_) : 0.0,
                             real_call_count_ ? real_call_sum_ / double(real_call_count_) : 0.0);
            if (stage_count_) {
                const double n = double(stage_count_);
                remote::host_log("d3d12: frame generation latency stages (means from the game's frame start, %llu frames): host takes "
                                 "the present record %.2f ms, frame recorded and handed to the pacer %.2f ms, GPU work done %.2f ms, "
                                 "real image Present returned %.2f ms",
                                 static_cast<unsigned long long>(stage_count_), stage_taken_sum_ / n, stage_submitted_sum_ / n,
                                 stage_done_sum_ / n, stage_real_sum_ / n);
            }
            stage_taken_sum_ = stage_submitted_sum_ = stage_done_sum_ = stage_real_sum_ = 0;
            stage_count_ = 0;
            spacing_sum_ = spacing_squares_ = spacing_max_ = hold_sum_ = generated_after_sum_ = real_call_sum_ = 0;
            spacing_count_ = hold_count_ = generated_after_count_ = real_call_count_ = 0;
            window_start_ = done.QuadPart;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            interval_ms_ = interval;
            if (generated_shown) ++stats_.generated;
            if (real_shown) ++stats_.real;
            slot_values_[frame.slot] = signalled;
            busy_ = false;
        }
        idle_.notify_all();
    }
}

} // namespace d3d12
