#pragma once
#include <memory>
#include <vector>
#include <algorithm>

namespace WorldBackground {
// Worker-only ownership. Keep before publishing to another thread. The worker
// drops the last reference only after readers release theirs; they cannot cause
// large snapshot/pixel destructors on the render thread during a map's lifetime.
template<class T> class WorkerRetirement {
    std::vector<std::shared_ptr<T>> retained;
public:
    void Keep(const std::shared_ptr<T>& value){retained.push_back(value);}
    size_t Collect(){
        const size_t before=retained.size();
        retained.erase(std::remove_if(retained.begin(),retained.end(),
            [](const std::shared_ptr<T>& value){return value.use_count()==1;}),retained.end());
        return before-retained.size();
    }
    size_t Size()const{return retained.size();}
};
}
