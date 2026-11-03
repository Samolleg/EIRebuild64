#pragma once
#include "owned_resource_store.h"
#include <mutex>
#include <stdexcept>

namespace owned {
// Permanent, rate-limited failures in the host's game journal.
void embedded_report_render_failure(const char* event, const char* reason) noexcept;
// Immutable for the lifetime of an embedded session. Configure before starting
// workers or creating the renderer; no registry or module-discovery fallback.
struct EmbeddedHostPaths {
    ResourceStoreConfig mounts;
    ResourceStoreConfig text_mounts, data_mounts;
    std::wstring renderer_config;
    std::wstring world_map_layouts;
};
class EmbeddedHostConfiguration {
public:
    void configure(EmbeddedHostPaths paths) {
        if (!absolute(paths.renderer_config) || !absolute(paths.world_map_layouts) ||
            paths.mounts.loose_roots.empty() || paths.mounts.archives.empty())
            throw std::invalid_argument("incomplete embedded renderer host paths");
        for (const auto& path : paths.mounts.loose_roots)
            if (!absolute(path)) throw std::invalid_argument("relative embedded loose root");
        for (const auto& path : paths.mounts.archives)
            if (!absolute(path)) throw std::invalid_argument("relative embedded archive");
        for (const auto* family : {&paths.text_mounts, &paths.data_mounts})
            for (const auto& path : family->archives)
                if (!absolute(path)) throw std::invalid_argument("relative embedded family archive");
        std::lock_guard<std::mutex> guard(lock_);
        if (configured_) throw std::logic_error("embedded renderer already configured");
        paths_ = std::move(paths);
        configured_ = true;
    }
    EmbeddedHostPaths snapshot() const {
        std::lock_guard<std::mutex> guard(lock_);
        if (!configured_) throw std::logic_error("embedded renderer host not configured");
        return paths_;
    }
private:
    static bool absolute(const std::wstring& path) {
        return path.find(L'\0') == std::wstring::npos &&
            ((path.size() > 3 && ((path[0] >= L'A' && path[0] <= L'Z') ||
              (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' &&
              (path[2] == L'\\' || path[2] == L'/')) ||
             (path.size() > 4 && path[0] == L'\\' && path[1] == L'\\'));
    }
    mutable std::mutex lock_;
    EmbeddedHostPaths paths_;
    bool configured_ = false;
};
inline EmbeddedHostConfiguration& embedded_host_configuration() {
    static EmbeddedHostConfiguration value;
    return value;
}
}
