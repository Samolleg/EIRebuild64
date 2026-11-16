#pragma once
#include "../Pak/Pak.h"
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

// Catalog membership must use the exact DB/model archive, not the mod overlay
// chain. Only index bytes are read; malformed optional catalogs stay optional
// (the game's CMappedResource reports a fatal error for malformed legacy RES).
class ItemApproachArchiveIndex {
    using Names = std::set<std::string>;
    std::map<std::wstring, Names> indexes;
    static void Require(bool ok) { if (!ok) throw std::runtime_error("Invalid catalog archive index"); }
    static std::uint32_t U32(const ei::pak::Bytes& bytes, size_t at) {
        Require(at<=bytes.size() && bytes.size()-at>=4);
        return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1])<<8) |
            (std::uint32_t(bytes[at+2])<<16) | (std::uint32_t(bytes[at+3])<<24);
    }
    static Names ReadNames(const std::filesystem::path& path) {
        Names names;
        if (_wcsicmp(path.extension().c_str(), L".pak") == 0) {
            const ei::pak::Archive archive(path);
            for (const auto& entry : archive.Entries())
                if (entry.method != 2) names.insert(ei::pak::Normalize(entry.name));
            return names;
        }
        std::ifstream file(path, std::ios::binary); Require(bool(file));
        file.seekg(0,std::ios::end); const auto size=file.tellg(); Require(size>=16);
        const auto read=[&](size_t count) {
            ei::pak::Bytes bytes(count);
            file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(count));
            Require(bool(file)); return bytes;
        };
        file.seekg(0);const auto header=read(16);
        const auto count=U32(header,4),offset=U32(header,8),bytes=U32(header,12);
        const std::uint64_t table=std::uint64_t(count)*22;
        Require(U32(header,0)==0x019ce23c && count>0 && count<=1000000 && offset>=16 &&
            bytes<=32u*1024u*1024u && std::uint64_t(offset)+table+bytes<=std::uint64_t(size));
        file.seekg(offset);const auto index=read(size_t(table)+bytes);
        for(std::uint32_t i=0;i<count;++i) {
            const size_t at=size_t(i)*22;
            const auto next=U32(index,at),length=U32(index,at+4),data=U32(index,at+8),name=U32(index,at+18);
            const std::uint32_t n=index[at+16]|(std::uint32_t(index[at+17])<<8);
            Require((next==0xffffffff || next<count) && std::uint64_t(data)+length<=std::uint64_t(size) && name<=bytes && n<=bytes-name);
            names.insert(ei::pak::Normalize(std::string(reinterpret_cast<const char*>(index.data()+size_t(table)+name),n)));
        }
        return names;
    }
public:
    bool Has(const std::string& path, const std::string& member) {
        try {
            const auto physical = ei::pak::ResolveArchivePath(path);
            const auto key = physical.lexically_normal().wstring();
            auto found = indexes.find(key);
            if (found == indexes.end()) {
                Names names;
                try { names=ReadNames(physical); } catch (const std::exception&) {}
                found = indexes.emplace(key, std::move(names)).first;
            }
            return found->second.count(ei::pak::Normalize(member)) != 0;
        } catch (const std::exception&) { return false; }
    }
};
