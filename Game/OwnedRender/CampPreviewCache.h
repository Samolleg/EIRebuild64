#pragma once
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

// A null result is final: the worker resolved the item and found no GLB.
template<class Model> struct CampPreviewSelection {
    std::shared_ptr<Model> model;
    bool useFigure=false;
};

template<class Key,class Request,class Model,class Loader>
class CampPreviewCache {
    enum class State { queued, loading, ready };
    struct Entry {
        Request request;
        bool anticipated=false;
        std::shared_ptr<Model> model;
        State state=State::queued;
    };
    Loader loader;
    std::map<Key,Entry> entries;
    std::deque<Entry*> stock,nearby;
    Entry* visible=nullptr;
    std::mutex mutex;
    std::condition_variable work,finished;
    bool stop=false;
    std::thread worker;

    void run() {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        std::unique_lock<std::mutex> lock(mutex);
        for (;;) {
            work.wait(lock,[&]{return stop || visible || !nearby.empty() || !stock.empty();});
            if(stop) return;
            auto* entry=visible;
            if(entry) visible=nullptr;
            else if(!nearby.empty()) {entry=nearby.front();nearby.pop_front();}
            else {entry=stock.front();stock.pop_front();}
            if(entry->state!=State::queued) continue;
            entry->state=State::loading;
            lock.unlock();
            auto model=loader(entry->request);
            lock.lock();
            entry->model=std::move(model);
            entry->state=State::ready;
            finished.notify_one();
        }
    }
public:
    CampPreviewCache():worker([this]{run();}) {}
    ~CampPreviewCache() {
        {std::lock_guard<std::mutex> guard(mutex);stop=true;}
        work.notify_one();
        worker.join();
    }
    // All calls originate on the game's UI thread. No item or party pointer
    // leaves it. Map nodes stay put for the lifetime of this camp visit.
    template<class Describe>
    CampPreviewSelection<Model> get(Key key,bool preload,bool upcoming,Describe describe) {
        std::unique_lock<std::mutex> lock(mutex);
        auto found=entries.find(key);
        if(found==entries.end()) {
            Entry entry;
            entry.request=describe();
            found=entries.emplace(key,std::move(entry)).first;
            if(preload) {stock.push_back(&found->second);work.notify_one();}
        }
        auto& entry=found->second;
        if(preload) {
            if(upcoming && entry.state==State::queued && !entry.anticipated) {
                entry.anticipated=true;nearby.push_back(&entry);work.notify_one();
            }
            return {};
        }
        if(entry.state!=State::ready) {
            if(entry.state==State::queued) {visible=&entry;work.notify_one();}
            finished.wait(lock,[&]{return entry.state==State::ready;});
        }
        return {entry.model,!entry.model};
    }
};
