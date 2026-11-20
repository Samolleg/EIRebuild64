#pragma once
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
    Loader loader;
    std::map<Key,Entry> entries;
    std::deque<Entry*> queue;
    std::size_t pending=0;
    std::mutex mutex;
    std::condition_variable work,finished;
    bool stop=false;
    std::thread worker;

    void run() {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        std::unique_lock<std::mutex> lock(mutex);
        for (;;) {
            work.wait(lock,[&]{return stop || !queue.empty();});
            if(stop) return;
            auto* entry=queue.front();queue.pop_front();
            lock.unlock();
            auto model=loader(entry->request);
            lock.lock();
            entry->model=std::move(model);
            entry->ready=true;
            --pending;finished.notify_all();
        }
    }
public:
    CampPreviewCache():worker([this]{run();}) {}
    ~CampPreviewCache() {
        {std::lock_guard<std::mutex> guard(mutex);stop=true;}
        work.notify_one();
        worker.join();
    }
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
        loader.idle();
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
            queue.push_back(&found->second);++pending;work.notify_one();
        }
        auto& entry=found->second;
        entry.used=true;
        if(preload) return {};
        // Also cover an item just created in an already-open constructor.
        finished.wait(lock,[&]{return entry.ready;});
        return {entry.model,!entry.model};
    }
};
