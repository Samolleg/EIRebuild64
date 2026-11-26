#pragma once
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

template<class Model> struct CampPreviewSelection {
    std::shared_ptr<Model> model;
    bool useFigure=false;
};

// The UI snapshots the actual stock, and the worker prepares each distinct
// key once. Seal the batch before presenting the shop. Ready entries from the
// previous visit remain available while the new assortment is being prepared.
template<class Key,class Request,class Model,class Loader>
class CampPreviewCache {
    struct Entry {
        Request request;
        std::shared_ptr<Model> model;
        bool ready=false,used=true;
    };
    struct Lane {
        Loader loader;
        std::deque<Entry*> queue;
        std::condition_variable work;
        std::thread thread;
    };
    std::array<Lane,4> lanes;
    const unsigned laneCount=(std::clamp)(std::thread::hardware_concurrency()/2,1u,4u);
    std::map<Key,Entry> entries;
    std::size_t pending=0;
    std::mutex mutex;
    std::condition_variable finished;
    bool stop=false;


    void run(Lane& lane) {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        std::unique_lock<std::mutex> lock(mutex);
        for (;;) {
            lane.work.wait(lock,[&]{return stop || !lane.queue.empty();});
            if(stop) return;
            auto* entry=lane.queue.front();lane.queue.pop_front();
            lock.unlock();
            auto model=lane.loader(entry->request);
            lock.lock();
            entry->model=std::move(model);
            entry->ready=true;
            --pending;finished.notify_all();
        }
    }
    void stopWorkers() {
        {std::lock_guard<std::mutex> guard(mutex);stop=true;}
        for(auto& lane:lanes) lane.work.notify_one();
        for(auto& lane:lanes) if(lane.thread.joinable()) lane.thread.join();
    }
public:
    CampPreviewCache() {
        try {
            for(unsigned i=0;i<laneCount;++i)
                lanes[i].thread=std::thread([this,i]{run(lanes[i]);});
        } catch(...) {stopWorkers();throw;}
    }
    ~CampPreviewCache() {stopWorkers();}
    void begin() {
        std::lock_guard<std::mutex> guard(mutex);
        for(auto& pair:entries) pair.second.used=false;
    }
    std::size_t finish() {
        std::unique_lock<std::mutex> lock(mutex);
        finished.wait(lock,[&]{return !pending;});
        for(auto it=entries.begin();it!=entries.end();) {
            if(!it->second.used) it=entries.erase(it);else ++it;
        }
        for(unsigned i=0;i<laneCount;++i) lanes[i].loader.idle();
        return entries.size();
    }
    template<class Describe>
    CampPreviewSelection<Model> get(Key key,bool preload,Describe describe) {
        std::unique_lock<std::mutex> lock(mutex);
        auto found=entries.find(key);
        if(found==entries.end()) {
            Entry entry;
            entry.request=describe();
            found=entries.emplace(key,std::move(entry)).first;
            auto& lane=lanes[Loader::affinity(found->second.request)%laneCount];
            lane.queue.push_back(&found->second);++pending;lane.work.notify_one();
        }
        auto& entry=found->second;
        entry.used=true;
        if(preload) return {};
        // Also cover an item just created in an already-open constructor.
        finished.wait(lock,[&]{return entry.ready;});
        return {entry.model,!entry.model};
    }
};
