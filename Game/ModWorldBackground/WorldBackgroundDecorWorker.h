#pragma once
#include "WorldBackgroundDecor.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>

#pragma pack(push,8)
namespace WorldBackground {
// Single ownership hand-off: no second 128 MiB prepared-model buffer.
// This worker never touches renderer APIs, engine globals, or the caller's Scene.
class DecorWorker {
public:
    struct Result {std::unique_ptr<Decor::Scene> scene;SectorView view;uint32_t epoch=0;uint64_t micros=0;std::string error;};
    using Prepare=std::function<void(Decor::Scene&,const SectorView&,const std::function<bool()>&)>;
private:
    Prepare prepare;std::atomic<bool> stop{false};std::mutex mutex;std::condition_variable wake;
    Result pending,completed;bool busy=false;std::thread thread;
    void Run(){
        for(;;){Result job;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stop.load()||pending.scene!=nullptr;});
                if(stop)return;job=std::move(pending);}
            const auto start=std::chrono::steady_clock::now();
            try{prepare(*job.scene,job.view,[&]{return stop.load();});}
            catch(const std::exception& e){job.error=e.what();}
            catch(...){job.error="Unknown decoration preparation failure";}
            job.micros=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
            {std::lock_guard<std::mutex> lock(mutex);completed=std::move(job);busy=false;}
        }
    }
public:
    explicit DecorWorker(Prepare fn):prepare(std::move(fn)),thread([this]{Run();}){}
    ~DecorWorker(){Stop();}
    DecorWorker(const DecorWorker&)=delete;DecorWorker& operator=(const DecorWorker&)=delete;
    void Stop(){stop=true;wake.notify_one();if(thread.joinable())thread.join();}
    bool Submit(std::unique_ptr<Decor::Scene>& scene,const SectorView& view,uint32_t epoch){
        if(!scene||stop)return false;
        {std::lock_guard<std::mutex> lock(mutex);
            // Exactly one Scene exists across caller, job and result.
            if(busy||pending.scene||completed.scene)return false;busy=true;
            pending.scene=std::move(scene);pending.view=view;pending.epoch=epoch;}
        wake.notify_one();return true;
    }
    Result Poll(){std::lock_guard<std::mutex> lock(mutex);return std::move(completed);}
};
}
#pragma pack(pop)
