#pragma once
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

// One CPU loader per open camp screen. Only copied resource names cross the
// thread boundary; rendering and the first GPU submission stay on the UI thread.
template<class Key,class Request,class Model,class Loader>
class CampPreviewCache {
    enum class State { pending, loading, done };
    struct Entry {
        Request request;
        std::shared_ptr<Model> model;
        std::uint64_t frame=0, order=0;
        State state=State::pending;
        bool shown=false;
    };
    Loader loader;
    const std::size_t budget;
    std::map<Key,Entry> entries;
    std::size_t bytes=0;
    std::uint64_t frame=0,order=0,publishedFrame=UINT64_MAX;
    bool stop=false;
    std::mutex lock;
    std::condition_variable wake;
    std::thread worker;

    void run() {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        std::unique_lock<std::mutex> guard(lock);
        while (!stop) {
            auto next=entries.end(), oldest=entries.end();
            for(auto it=entries.begin();it!=entries.end();++it) {
                const auto& entry=it->second;
                const bool recent=frame-entry.frame<=1;
                if(recent && entry.state==State::pending && (next==entries.end() ||
                   entry.frame>next->second.frame || (entry.frame==next->second.frame && entry.order<next->second.order)))
                    next=it;
                if(!recent && entry.model && (oldest==entries.end() || entry.frame<oldest->second.frame))
                    oldest=it;
            }
            if(bytes>budget && oldest!=entries.end()) {
                auto retired=std::move(oldest->second.model);
                bytes-=retired->bytes;
                entries.erase(oldest);
                guard.unlock(); retired.reset(); guard.lock();
                continue;
            }
            if(next==entries.end()) {wake.wait(guard);continue;}
            auto& entry=next->second;
            entry.state=State::loading;
            guard.unlock();
            auto model=loader(entry.request);
            guard.lock();
            entry.model=std::move(model);
            if(entry.model) bytes+=entry.model->bytes;
            entry.state=State::done;
        }
    }
public:
    explicit CampPreviewCache(std::size_t byteBudget):budget(byteBudget),worker([this]{run();}) {}
    ~CampPreviewCache() {
        {std::lock_guard<std::mutex> guard(lock);stop=true;}
        wake.notify_one();
        worker.join();
    }
    template<class Describe>
    std::shared_ptr<Model> find(Key key,std::uint64_t now,Describe describe) {
        std::lock_guard<std::mutex> guard(lock);
        const bool newFrame=now!=frame;
        frame=now;
        auto inserted=entries.try_emplace(key);
        auto& entry=inserted.first->second;
        if(inserted.second) {entry.request=describe();entry.order=++order;}
        const bool revisit=now-entry.frame>1;
        entry.frame=now;
        if(inserted.second || (entry.state==State::pending && revisit) || (newFrame && bytes>budget)) wake.notify_one();
        if(!entry.model || entry.shown) return entry.model;
        if(publishedFrame==now) return {}; // Amortize cold GPU uploads across frames.
        publishedFrame=now;entry.shown=true;
        return entry.model;
    }
};
