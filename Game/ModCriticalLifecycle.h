#pragma once
#include <new>
// Bound source: active ComposeTextureDiskOld layer allocation and layer reset.
// This bridge never resets the unrelated active1 pointer-metadata registry.
namespace ModCriticalLifecycle {
template<class Package> inline void Clear(Package* package){
 if(package)package->ClearCustomCriticals();
}
template<class Allocate,class Clear> auto DiskLayer(Allocate allocate,Clear clear)->decltype(allocate()){
 try {auto* result=allocate();if(result)return result;}catch(const std::bad_alloc&){}
 clear();return nullptr;
}
}
