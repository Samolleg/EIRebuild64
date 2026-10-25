// The DLL's end of the retained-scene publication stream (retained_publication.h):
// messages go into the remote device ring as Op::retained fragments.
#include "retained_publication.h"

#include "owned_resource_runtime.h"
#include "retained_static_scene.h"
#include "x64_remote_link.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <new>

namespace owned {

namespace {

// Ring fragments stay far below a record's limit (a quarter of the ring), so
// one large asset never claims most of the ring at once.
constexpr std::size_t kFragmentBytes = 1u << 20;
std::atomic<RetainedPublicationRecorder*> g_recorder{nullptr};

void ring_sink(void*, const std::uint8_t* data, std::size_t bytes, bool last) noexcept {
    const x64remote::RetainedFragment header{static_cast<std::uint32_t>(bytes), last ? 1u : 0u};
    x64remote::Writer w(x64remote::Op::retained, 0, static_cast<std::uint32_t>(sizeof(header) + bytes));
    w.put(header);
    if (bytes) w.put(data, static_cast<std::uint32_t>(bytes));
}

} // namespace

void retained_publication_attach() noexcept {
    static std::once_flag once;
    std::call_once(once, [] {
        const std::size_t fragment = (std::min)(kFragmentBytes,
            static_cast<std::size_t>(x64remote::chunk_bytes()) - sizeof(x64remote::RetainedFragment));
        // Never destroyed: scene teardown calls during process exit may still record.
        auto* recorder = new (std::nothrow) RetainedPublicationRecorder(&ring_sink, nullptr, fragment);
        g_recorder.store(recorder, std::memory_order_release);
        if (!recorder) {
            bridge_append_probe_event("retained_publication_failed", "recorder allocation");
            return;
        }
        retained_visual_scene().set_publication_recorder(recorder);
        x64remote::log("retained publication stream attached (fragments of %u bytes)",
                       static_cast<unsigned>(fragment));
    });
}

// Core mode: the stream stopped, so the host's world would diverge.
bool retained_publication_failed() noexcept {
    const auto* recorder = g_recorder.load(std::memory_order_acquire);
    return recorder && recorder->failed();
}

} // namespace owned
