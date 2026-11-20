#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <vector>

// Retain the active shop's exact draw streams. Large cold blocks can be
// reclaimed by Windows without rerunning the GLB decoder on the next scroll.
class CampPreviewStorage {
    std::size_t resident=0;
    static constexpr std::size_t residentBudget=128ull<<20;
public:
    struct Span {const void* data; std::size_t bytes;};
    using View=std::shared_ptr<const std::uint8_t>;
    View back(const Span* spans,std::size_t count) {
        std::size_t total=0;
        for(std::size_t i=0;i<count;++i) total+=spans[i].bytes;
        if(!total) return {};
        if(total<=residentBudget-resident) {resident+=total;return {};}
        wchar_t dir[MAX_PATH],path[MAX_PATH];
        const DWORD length=GetTempPathW(MAX_PATH,dir);
        if(!length || length>=MAX_PATH || !GetTempFileNameW(dir,L"EIC",0,path)) return {};
        HANDLE file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_DELETE,
                          nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(file==INVALID_HANDLE_VALUE) {DeleteFileW(path);return {};}
        bool ok=true;
        for(std::size_t i=0;i<count && ok;++i) {
            DWORD written=0;
            ok=spans[i].bytes<=MAXDWORD &&
               WriteFile(file,spans[i].data,static_cast<DWORD>(spans[i].bytes),&written,nullptr) &&
               written==spans[i].bytes;
        }
        HANDLE mapping=ok?CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr):nullptr;
        const auto* data=mapping?static_cast<const std::uint8_t*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0)):nullptr;
        if(mapping) CloseHandle(mapping);
        CloseHandle(file);
        if(!data) return {};
        return View(data,[](const std::uint8_t* p){UnmapViewOfFile(p);});
    }
    struct Image {
        owned::skn::Dds map;
        View backing;
    };
    void retain(owned::skn::Dds& map,View& backing) {
        if(!map.blob.bytes || map.mips.empty()) return;
        const auto key=map.blob.identity.key();
        auto found=images.find(key);
        if(found!=images.end()) {map=found->second.map;backing=found->second.backing;return;}
        const auto* start=map.blob.bytes->data();
        const Span span{start,map.blob.bytes->size()};
        backing=back(&span,1);
        if(backing) {
            for(auto& mip:map.mips) mip.data=backing.get()+(mip.data-start);
            map.blob.bytes.reset();
        }
        images.emplace(key,Image{map,backing});
    }
private:
    std::map<std::string,Image> images;
};
