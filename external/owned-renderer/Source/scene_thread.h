#pragma once
// Scene thread (F10 / harness option render_thread, default on).
//
// While the option is on, this thread owns the Renderer: it runs every piece
// of scene preparation the bridge hands it (visual values, bounds, render
// visibility, LOD, shadow caster selection, retained binding decisions, draw
// list building), records the frame on the D3D11 immediate context and
// presents it. The game thread only reads game memory, copies what the frame
// consumes into commands and queues them here (RENDERER_ARCHITECTURE.md,
// "Scene thread").
//
// Ownership: commands run in queue order on this thread only. A game-thread
// call into the Renderer that has not been converted to a command first
// waits for this thread to drain the queue (sync); this thread then stays
// parked until the game thread queues again, so the two never touch the
// Renderer at the same time. The game thread is the only producer.
//
// Pipelining: at most one presenting frame is queued or executing. The game
// thread waits in wait_present_slot() only while the previous frame has not
// presented yet; everything else it queues streams in while this thread works.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include "stall_watchdog.h"
#include "frame_accounting.h"
#include "frame_waits.h"

namespace owned {

// One event per queued command over 20 ms in play: its type (the lambda's
// enclosing function), so a long scene_command frame is named
// (renderer_device.cpp).
void report_slow_scene_command(const char* name, const char* site, double ms) noexcept;

class SceneThread {
public:
    SceneThread() = default;
    SceneThread(const SceneThread&) = delete;
    SceneThread& operator=(const SceneThread&) = delete;
    ~SceneThread() { stop(); }

    bool start(void (*releaseLocals)() noexcept = nullptr) noexcept {
        if (thread_) return true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = false;
            busy_ = false;
            presents_pending_ = 0;
            last_present_ = S_OK;
        }
        release_locals_ = releaseLocals;
        exited_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!exited_) return false;
        thread_ = CreateThread(nullptr, 0, &SceneThread::entry, this, 0, &thread_id_);
        if (!thread_) {
            CloseHandle(exited_);
            exited_ = nullptr;
            thread_id_ = 0;
            return false;
        }
        return true;
    }

    // Runs everything still queued, then ends the thread. Game thread only.
    // Waits for the thread itself to end, so its thread-local state (bridge
    // frame values, snapshots holding D3D resources) is destroyed before the
    // caller tears the renderer down. At process exit Windows has already
    // ended the thread and its handle is signalled.
    void stop() noexcept {
        if (!thread_) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        CloseHandle(exited_);
        thread_ = nullptr;
        exited_ = nullptr;
        thread_id_ = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        for (Command* command : queue_) delete command;
        queue_.clear();
    }

    bool running() const noexcept { return thread_ != nullptr; }
    bool on_thread() const noexcept { return thread_id_ && GetCurrentThreadId() == thread_id_; }
    DWORD thread_id() const noexcept { return thread_id_; }

    // Queues a callable. present marks the command that ends a frame.
    template <class F> bool post(F&& function, bool present = false, const char* site = nullptr) noexcept {
        Command* command = nullptr;
        try {
            command = new Callable<std::decay_t<F>>(std::forward<F>(function));
        } catch (...) {
            return false;
        }
        command->present = present;
        command->site = site;
        bool wake = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            try { queue_.push_back(command); } catch (...) { delete command; return false; }
            if (present) ++presents_pending_;
            wake = waiting_;
        }
        if (wake) wake_.notify_one();
        return true;
    }

    // Returns once everything queued has run; the thread then stays parked
    // until the next post.
    void sync() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return queue_.empty() && !busy_; });
    }

    bool idle() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty() && !busy_;
    }

    // Returns once no presenting command is queued or running.
    void wait_present_slot() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return presents_pending_ == 0; });
    }

    // Set by the present command on this thread before it returns.
    void set_present_result(HRESULT result) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        last_present_ = result;
    }
    HRESULT last_present_result() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_present_;
    }

private:
    struct Command {
        virtual ~Command() = default;
        virtual void run() noexcept = 0;
        virtual const char* name() const noexcept = 0;
        bool present = false;
        const char* site = nullptr;  // the posting function (scene_post), static storage
    };
    template <class F> struct Callable final : Command {
        explicit Callable(F&& value) : function(std::move(value)) {}
        explicit Callable(const F& value) : function(value) {}
        const char* name() const noexcept override { return typeid(F).name(); }
        void run() noexcept override {
            try {
                function();
            } catch (...) {
            }
        }
        F function;
    };

    static DWORD WINAPI entry(void* self) {
        static_cast<SceneThread*>(self)->run();
        return 0;
    }

    void run() noexcept {
        stall_watch::slot_index = 1;
        frame_acct::set_root(frame_acct::scene_other);
        for (;;) {
            Command* command = nullptr;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (queue_.empty()) {
                    busy_ = false;
                    done_.notify_all();
                    waiting_ = true;
                    {
                        const frame_acct::Scope idle(frame_acct::scene_idle);
                        const frame_waits::Scope inputWait(frame_waits::idle);
                        wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                    }
                    waiting_ = false;
                    if (queue_.empty()) break;  // stop requested and drained
                }
                command = queue_.front();
                queue_.pop_front();
                busy_ = true;
            }
            // No blanket bracket: a command runs the world and frame work the
            // game thread ran before, under the same brackets (frame,
            // flush_native, present) on this thread's slot. A bracket over a
            // whole world command also covered the driver's first-draw
            // pipeline compiles at a menu load (seconds, and on the game
            // thread never watched), and each such report wrote a 1 GB dump.
            {
                if (!frame_waits::frame.active) frame_waits::begin();
                const frame_waits::Scope record(frame_waits::record);
                const frame_acct::Scope running(frame_acct::scene_command);
                const std::int64_t begin = frame_acct::now();
                command->run();
                const double ms = double(frame_acct::now() - begin) * frame_acct::ms_per_tick();
                if (ms > 20.0 && frame_acct::g_in_play.load(std::memory_order_relaxed))
                    report_slow_scene_command(command->name(), command->site, ms);
            }
            const bool present = command->present;
            delete command;
            if (present) {
                frame_waits::end_render();
                std::lock_guard<std::mutex> lock(mutex_);
                if (presents_pending_) --presents_pending_;
                done_.notify_all();
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
        done_.notify_all();
        // A world snapshot can own the last reference to a character physics
        // worker. Its destructor joins another thread. Retire it here before
        // Windows calls TLS destructors under the loader lock.
        if (release_locals_) release_locals_();
        SetEvent(exited_);
    }

    void (*release_locals_)() noexcept = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE exited_ = nullptr;
    DWORD thread_id_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable done_;
    std::deque<Command*> queue_;
    bool stop_ = false;
    bool busy_ = false;
    bool waiting_ = false;
    unsigned presents_pending_ = 0;
    HRESULT last_present_ = S_OK;
};

}  // namespace owned
