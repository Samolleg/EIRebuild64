#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace OwnedRender {
// One owning storage, not a mirror of a source cache. Pinning occurs before
// eviction, including a zero-capacity draw. Clock/allocation stay in adapters.
template<class T,class Allocator,class Clock>
class ModelPreviewCache {
    struct Entry { T* object; std::uint32_t time; };
    std::map<std::string,Entry> entries_;
    std::size_t capacity_=50;
    void trim(T* current=nullptr) {
        while (entries_.size()>capacity_) {
            const auto now=Clock::now();
            auto oldest=entries_.end();
            for (auto i=entries_.begin();i!=entries_.end();++i) {
                // A millisecond tie must not evict the object just returned to
                // a non-pinning texture caller while the cache has capacity.
                if (capacity_ && i->second.object==current) continue;
                if (oldest==entries_.end() ||
                    std::uint32_t(now-i->second.time)>std::uint32_t(now-oldest->second.time)) oldest=i;
            }
            T* object=oldest->second.object;
            entries_.erase(oldest);
            Allocator::DestroyObject(object);
        }
    }
public:
    ModelPreviewCache()=default;
    ModelPreviewCache(const ModelPreviewCache&)=delete;
    ModelPreviewCache& operator=(const ModelPreviewCache&)=delete;
    ~ModelPreviewCache() { FreeObjects(); }
    void SetMaxSize(int size) { capacity_=size<0?0:std::size_t(size);trim(); }
    bool Contains(const char* name) const { return entries_.find(name)!=entries_.end(); }
    T* GetObject(const char* name) { return GetObject(name,[](T*){}); }
    template<class Pin> T* GetObject(const char* name,Pin&& pin) {
        auto found=entries_.find(name);
        if (found!=entries_.end()) {
            pin(found->second.object);found->second.time=Clock::now();return found->second.object;
        }
        T* object=Allocator::CreateObject(name);
        if (!object) return nullptr;
        try { pin(object);entries_.emplace(name,Entry{object,Clock::now()}); }
        catch (...) { Allocator::DestroyObject(object);throw; }
        trim(object);return object;
    }
    void FreeObjects() {
        // Detach before release: destruction may call back through managers.
        auto retired=std::move(entries_);entries_.clear();
        for (const auto& entry:retired) Allocator::DestroyObject(entry.second.object);
    }
};
} // namespace OwnedRender
