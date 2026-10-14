#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace owned {
// A foreign chain may still reach our stub. Its forwarding state stays alive
// until the entry is verifiably back at the saved owner.
template<class Restore>
bool restore_owned_hook(std::uint8_t* entry,const std::uint8_t* original,
    std::size_t bytes,const void* hook,bool call,Restore restore) noexcept {
    if(!entry||!original||bytes<5)return false;
    if(std::memcmp(entry,original,bytes)==0)return true;
    if(entry[0]!=(call?0xe8:0xe9))return false;
    std::int32_t relative=0;std::memcpy(&relative,entry+1,4);
    if(entry+5+relative!=hook)return false;
    return restore(entry,original,bytes)&&std::memcmp(entry,original,bytes)==0;
}
} // namespace owned
