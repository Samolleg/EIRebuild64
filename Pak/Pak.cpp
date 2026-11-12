#include "Pak.h"
#define NOMINMAX
#include <windows.h>
#include <zstd.h>
#define XXH_STATIC_LINKING_ONLY
#include <common/xxhash.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <bcrypt.h>
#include <locale.h>
#include <ctime>
#include "../SDKs/JSON/json.hpp"
#pragma comment(lib, "bcrypt.lib")

namespace ei::pak {
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace {
constexpr char magic[8] = { 'E', 'I', 'P', 'A', 'K', '\r', '\n', 0 };
constexpr std::uint64_t headerBytes = 72, recordBytes = 48;
std::runtime_error Error(const std::string &message)
{
    return std::runtime_error(message);
}
void Require(bool condition, const std::string &message)
{
    if (!condition)
        throw Error(message);
}
std::uint64_t Number(const std::uint8_t *p, unsigned n)
{
    std::uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i)
        value |= std::uint64_t(p[i]) << (8 * i);
    return value;
}
void Put(std::ostream &stream, std::uint64_t value, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        stream.put(char(value >> (8 * i)));
}
Bytes ReadAt(std::ifstream &stream, std::uint64_t offset, std::uint64_t bytes)
{
    Require(bytes <= SIZE_MAX && bytes <= INT64_MAX && offset <= INT64_MAX,
            "read exceeds address space");
    Bytes result(static_cast<size_t>(bytes));
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    Require(bool(stream), "seek failed");
    if (bytes)
        stream.read(reinterpret_cast<char *>(result.data()), static_cast<std::streamsize>(bytes));
    Require(bool(stream), "truncated file");
    return result;
}
Bytes ReadAt(HANDLE stream, uint64_t offset, uint64_t bytes)
{
    Require(bytes<=SIZE_MAX&&offset<=INT64_MAX,"read exceeds address space");LARGE_INTEGER at;at.QuadPart=offset;Require(SetFilePointerEx(stream,at,nullptr,FILE_BEGIN)!=0,"seek failed");Bytes result(static_cast<size_t>(bytes));
    for(size_t done=0;done<result.size();){DWORD n=0;DWORD wanted=static_cast<DWORD>(std::min<size_t>(result.size()-done,1<<20));Require(::ReadFile(stream,result.data()+done,wanted,&n,nullptr)&&n==wanted,"truncated file");done+=n;}return result;
}Bytes ReadFile(const fs::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    Require(bool(stream), "cannot open " + path.string());
    return ReadAt(stream, 0, fs::file_size(path));
}
std::uint64_t Align(std::uint64_t n)
{
    Require(n <= UINT64_MAX - 7, "offset overflow");
    return (n + 7) & ~std::uint64_t(7);
}
bool Signature(const Bytes &bytes, std::initializer_list<unsigned> sig)
{
    return bytes.size() >= sig.size() && std::equal(sig.begin(), sig.end(), bytes.begin());
}
bool Stored(const Bytes &b)
{
    return Signature(b, { 0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c }) ||
           Signature(b, { 0x49, 0x44, 0x33 }) ||
           (b.size() > 1 && b[0] == 0xff && (b[1] & 0xe0) == 0xe0) ||
           Signature(b, { 0x50, 0x4b, 3, 4 }) || Signature(b, { 0x1f, 0x8b }) ||
           Signature(b, { 0x89, 0x50, 0x4e, 0x47, 13, 10, 26, 10 }) ||
           Signature(b, { 0xff, 0xd8, 0xff }) || Signature(b, { 0x28, 0xb5, 0x2f, 0xfd }) ||
           Signature(b, { 0x4f, 0x67, 0x67, 0x53 });
}
Bytes Compress(const Bytes &source)
{
    Bytes result(ZSTD_compressBound(source.size()));
    const auto size = ZSTD_compress(result.data(), result.size(), source.data(), source.size(), 3);
    Require(!ZSTD_isError(size), ZSTD_getErrorName(size));
    result.resize(size);
    return result;
}
Bytes Decode(const Bytes &stored, const Entry &e)
{
    if (!e.method)
    {
        Require(stored.size() == e.size, "stored size mismatch");
        return stored;
    }
    Require(e.method == 1 && e.size <= SIZE_MAX && e.size <= UINT32_MAX,
            "unsupported method or size");
    Require(ZSTD_getFrameContentSize(stored.data(), stored.size()) == e.size,
            "zstd frame size mismatch");
    Require(ZSTD_findFrameCompressedSize(stored.data(), stored.size()) == stored.size(),
            "zstd trailing data");
    Bytes bytes(static_cast<size_t>(e.size));
    const auto size = ZSTD_decompress(bytes.data(), bytes.size(), stored.data(), stored.size());
    Require(!ZSTD_isError(size) && size == e.size, "zstd decompression failed");
    return bytes;
}
class Lock
{
    HANDLE handle=INVALID_HANDLE_VALUE;
public:
    explicit Lock(const fs::path& path) {
        auto lock=path;lock+=L".lock";
        fs::create_directories(fs::absolute(path).parent_path());
        handle=CreateFileW(lock.c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        Require(handle!=INVALID_HANDLE_VALUE,"archive is locked: "+path.u8string());
        auto text=Json{{"pid",GetCurrentProcessId()},{"application","EIRebuild64Pack/Pak"},{"utc",std::time(nullptr)}}.dump();
        DWORD written=0;
        if(!SetEndOfFile(handle)||!WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)||written!=text.size()||!FlushFileBuffers(handle)) {
            CloseHandle(handle);handle=INVALID_HANDLE_VALUE;throw Error("cannot write lock metadata");
        }
    }
    ~Lock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    Lock(const Lock&)=delete;Lock& operator=(const Lock&)=delete;
};

void Flush(const fs::path& path);
void NoPending(fs::path directory)
{
    directory=fs::absolute(directory);
    for(;;) {
        for(const auto& f:fs::directory_iterator(directory))
            Require(!(f.path().filename().u8string().rfind("pak-txn-",0)==0&&f.path().extension()==L".json"),"pending transaction; recover first");
        auto parent=directory.parent_path();if(parent==directory)break;directory=parent;
    }
}// Each writer owns an exclusive staging file. Rename publishes a complete file.
class Output
{
    fs::path staging, target;
    std::unique_ptr<Lock> lock;
    bool archive;

public:
    std::ofstream stream;
    explicit Output(const fs::path &path, bool pak=false)
        : target(path), archive(pak)
    {
        fs::create_directories(fs::absolute(target).parent_path());
        if(archive) {
            if(_wcsicmp(target.extension().c_str(),L".pak")==0) {lock=std::make_unique<Lock>(target);NoPending(fs::absolute(target).parent_path());staging=target;staging+=L".tmp";Require(!fs::exists(staging),"stale staging file; recover first");}
            else staging=target;
        }
        for (unsigned i = 0;!archive;++i)
        {
            staging = target;
            staging += ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(i);
            HANDLE h = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                CloseHandle(h);
                break;
            }
            Require(GetLastError() == ERROR_FILE_EXISTS, "cannot create staging file");
        }
        stream.open(staging, std::ios::binary | std::ios::trunc);
        Require(bool(stream), "cannot write staging file");
    }
    ~Output()
    {
        stream.close();
        std::error_code ec;
        if(!staging.empty())fs::remove(staging, ec);
    }
    void Commit()
    {
        stream.flush();
        Require(bool(stream), "write failed");
        stream.close();
        Flush(staging);
        if(archive)Verify(staging);
        if(staging!=target)Require(MoveFileExW(staging.c_str(), target.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                "cannot publish " + target.string());
        staging.clear();
    }
};
void Write(std::ostream &out, const Bytes &bytes)
{
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    Require(bool(out), "write failed");
}
void Save(const fs::path &path, const Bytes &bytes)
{
    Output out(path);
    Write(out.stream, bytes);
    out.Commit();
}
std::string CacheKey(std::uint64_t hash, std::uint64_t size)
{
    std::ostringstream s;
    s << "zstd-1.5.7-l3-v1-" << std::hex << std::setw(16) << std::setfill('0') << hash << '-'
      << size;
    return s.str();
}
std::string Hex(const void* data, size_t size)
{
    static const char digits[] = "0123456789ABCDEF"; std::string out;
    for(size_t i=0;i<size;++i) { auto c=static_cast<const unsigned char*>(data)[i]; out+=digits[c>>4];out+=digits[c&15]; } return out;
}
Bytes Unhex(const std::string& text)
{
    Require(text.size()%2==0,"odd hex length"); Bytes out;
    for(size_t i=0;i<text.size();i+=2) {
        auto digit=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;};
        int a=digit(text[i]),b=digit(text[i+1]);Require(a>=0&&b>=0,"invalid hex");out.push_back(static_cast<uint8_t>(a*16+b));
    } return out;
}
Bytes Sha(const Bytes& bytes)
{
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA provider failed");
    struct Guard { BCRYPT_ALG_HANDLE a; BCRYPT_HASH_HANDLE& h; ~Guard(){if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);} } guard{algorithm,hash};
    Require(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"SHA create failed");
    for(size_t i=0;i<bytes.size();) { auto n=static_cast<ULONG>(std::min<size_t>(bytes.size()-i,1<<20));Require(BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()+i),n,0)>=0,"SHA update failed");i+=n; }
    Bytes result(32);Require(BCryptFinishHash(hash,result.data(),32,0)>=0,"SHA finish failed");return result;
}
std::wstring Wide(const std::string& text)
{
    if(text.empty())return {};int n=MultiByteToWideChar(1251,0,text.data(),int(text.size()),nullptr,0);Require(n>0,"CP1251 decode failed");std::wstring out(n,0);MultiByteToWideChar(1251,0,text.data(),int(text.size()),out.data(),n);return out;
}
std::string Legacy(const std::wstring& text)
{
    if(text.empty())return {};BOOL used=FALSE;int n=WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,&used);Require(n>0&&!used,"not CP1251");std::string out(n,0);WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,text.data(),int(text.size()),out.data(),n,nullptr,&used);Require(!used,"not CP1251");return out;
}
std::string DiskName(const std::string& name)
{
    std::string out; size_t start=0;
    while(start<name.size()) {
        auto end=name.find('\\',start);if(end==std::string::npos)end=name.size();
        auto part=name.substr(start,end-start);auto lower=part;for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;
        auto stem=lower.substr(0,lower.find('.'));
        bool reserved=stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||(stem.size()==4&&(stem.substr(0,3)=="com"||stem.substr(0,3)=="lpt")&&stem[3]>='1'&&stem[3]<='9');
        size_t trailing=part.find_last_not_of(". ");trailing=trailing==std::string::npos?0:trailing+1;
        for(size_t i=0;i<part.size();++i) { unsigned char c=part[i];if(c<32||c=='%'||c=='/'||std::string("<>:\"|?*").find(char(c))!=std::string::npos||i>=trailing||(i==0&&reserved)||(start==0&&lower=="_deleted.txt"&&i==0))out+="%"+Hex(&c,1);else out+=char(c); }
        if(end<name.size())out+='\\';start=end+1;
    } return out;
}
std::string ResourceName(const std::wstring& disk)
{
    auto text=Legacy(disk);std::string out;
    for(size_t i=0;i<text.size();++i){if(text[i]=='%'){Require(i+2<text.size(),"incomplete name escape");auto b=Unhex(text.substr(i+1,2));out+=char(b[0]);i+=2;}else out+=text[i];}
    Normalize(out);return out;
}
// Directory spelling follows the first occurrence in archive record order.
std::string CanonicalName(const std::string& name, std::map<std::string,std::string>& prefixes)
{
    std::string result;size_t start=0;
    for(size_t at=name.find('\\');at!=std::string::npos;at=name.find('\\',start)) {
        auto raw=name.substr(0,at);
        auto inserted=prefixes.emplace(Normalize(raw),result+name.substr(start,at-start));
        result=inserted.first->second+'\\';start=at+1;
    }
    return result+name.substr(start);
}
fs::path Destination(const fs::path &root, const std::string &name)
{
    Normalize(name);Require(name.front()!=char(92)&&name.back()!=char(92)&&name.find(std::string(2,char(92)))==std::string::npos,"empty name component");fs::path relative(Wide(DiskName(name)));fs::path current=fs::absolute(root);
    Require(!fs::is_symlink(fs::symlink_status(current)),"symlink output directory");
    for(const auto& part:relative){Require(part!=L".."&&part!=L".","unsafe name");current/=part;Require(!fs::is_symlink(fs::symlink_status(current)),"symlink output entry");} return current;
}} // namespace

std::string Normalize(const std::string& name)
{
    Require(!name.empty()&&name.size()<=1024,"name length must be 1..1024 bytes");
    static _locale_t locale=_create_locale(LC_CTYPE,".1251");Require(locale!=nullptr,"CP1251 locale unavailable");
    std::string result;for(unsigned char c:name){Require(c!=0,"NUL name byte");result+=char(_tolower_l(c,locale));}return result;
}std::uint64_t Hash(const Bytes &b)
{
    return XXH64(b.data(), b.size(), 0);
}
struct Archive::State
{
    HANDLE stream = INVALID_HANDLE_VALUE;
    fs::path path;
    std::vector<Entry> entries;
    mutable std::mutex mutex;
    std::set<std::string> verified;
    ~State(){if(stream!=INVALID_HANDLE_VALUE)CloseHandle(stream);}
    explicit State(const fs::path &p)
        : stream(CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr))
        , path(p)
    {
    }
};
Archive::Archive(const fs::path &path)
    : state(std::make_shared<State>(path))
{
    try
    {
        auto in = state->stream;
        Require(in!=INVALID_HANDLE_VALUE, "cannot open archive");
        LARGE_INTEGER fileSize{};Require(GetFileSizeEx(in,&fileSize)!=0&&fileSize.QuadPart>=0,"cannot read archive size");const auto bytes=static_cast<uint64_t>(fileSize.QuadPart);
        auto h = ReadAt(in, 0, 40);
        const auto version=Number(h.data()+8,4), header=Number(h.data()+12,4);
        Require(std::equal(std::begin(magic), std::end(magic), h.begin()), "invalid PAK magic");
        Require(((version==1&&header==40)||(version==0x00010001&&header==72)),
                "unsupported PAK version/header");
        const auto count = Number(h.data() + 16, 8), toc = Number(h.data() + 24, 8),
                   tocSize = Number(h.data() + 32, 8);
        Require(toc >= header && toc % 8 == 0 && toc <= bytes && tocSize == bytes - toc &&
                    count <= tocSize / recordBytes,
                "invalid PAK table bounds");
        auto table = ReadAt(in, toc, tocSize);
        if(version!=1)Require(ReadAt(in,40,32)==Sha(table),"table SHA-256 mismatch");
        size_t cursor = 0;
        std::string previous;
        std::uint64_t dataEnd = header;
        for (std::uint64_t i = 0; i < count; ++i)
        {
            Require(table.size() - cursor >= recordBytes, "truncated PAK record");
            auto *p = table.data() + cursor;
            const auto nameSize = Number(p, 4);
            Entry e;
            e.method = static_cast<std::uint32_t>(Number(p + 4, 4));
            e.offset = Number(p + 8, 8);
            e.stored = Number(p + 16, 8);
            e.size = Number(p + 24, 8);
            e.hash = Number(p + 32, 8);
            Require(Number(p + 40, 8) == 0 && nameSize <= table.size() - cursor - recordBytes,
                    "invalid PAK record/name");
            cursor += recordBytes;
            e.name.assign(reinterpret_cast<char *>(table.data() + cursor),
                          static_cast<size_t>(nameSize));
            cursor += static_cast<size_t>(nameSize);
            Require(!e.name.empty() && (previous.empty() || previous < Normalize(e.name)),
                    "noncanonical or duplicate PAK name");
            Require(e.method <= (version==1?1u:2u) && e.size <= UINT32_MAX &&
                        (e.method!=2||(e.size==0&&e.stored==0&&e.hash==0)) &&
                        (e.size ? e.stored != 0 : (e.method == 0 || e.method == 2) && e.stored == 0) &&
                        e.offset == Align(dataEnd) && e.offset <= toc &&
                        e.stored <= toc - e.offset && (e.method || e.stored == e.size),
                    "invalid PAK data bounds/method");
            dataEnd = e.offset + e.stored;
            previous = Normalize(e.name);
            state->entries.push_back(std::move(e));
        }
        Require(cursor == table.size() && Align(dataEnd) == toc, "invalid PAK table extent");
    }
    catch (const std::exception &e)
    {
        throw Error(path.string() + ": " + e.what());
    }
}
const std::vector<Entry> &Archive::Entries() const
{
    return state->entries;
}
const Entry *Archive::Find(const std::string &name) const
{
    auto key = Normalize(name);
    auto it = std::lower_bound(state->entries.begin(), state->entries.end(), key,
                               [](const Entry &e, const std::string &n) { return Normalize(e.name) < n; });
    return it != state->entries.end() && Normalize(it->name) == key ? &*it : nullptr;
}
Bytes Archive::Read(const Entry &e, bool verify) const
{
    std::lock_guard<std::mutex> lock(state->mutex);
    try
    {
        Require(e.method!=2,"entry is deleted");
        auto result = Decode(ReadAt(state->stream, e.offset, e.stored), e);
        if (verify && !state->verified.count(e.name))
        {
            Require(Hash(result) == e.hash, "XXH64 mismatch");
            state->verified.insert(e.name);
        }
        return result;
    }
    catch (const std::exception &ex)
    {
        throw Error(state->path.string() + "/" + e.name + ": " + ex.what());
    }
}

void Pack(const fs::path &directory, const fs::path &output, const fs::path &cache)
{
    std::map<std::string, fs::path> inputs;
    std::vector<std::string> errors;
    const auto source = fs::weakly_canonical(directory);
    Require(fs::is_directory(source), "input is not a directory");
    const auto target = fs::weakly_canonical(output);
    auto within = target.lexically_relative(source);
    Require(!within.empty() && *within.begin() == "..", "output must be outside input directory");
    if (!cache.empty())
    {
        auto relative = fs::weakly_canonical(cache).lexically_relative(source);
        Require(!relative.empty() && *relative.begin() == "..",
                "cache must be outside input directory");
    }
    for (const auto &file : fs::recursive_directory_iterator(source))
    {
        try
        {
            Require(!file.is_symlink(), "symlink input");
            if (!file.is_regular_file())
                continue;
            if(file.path()==source/L"_deleted.txt")continue;
            auto name = ResourceName(file.path().lexically_relative(source).wstring());
            const auto size = file.file_size();
            Require(size <= UINT32_MAX, "oversized file");
            Require(inputs.emplace(Normalize(name), file.path()).second, "case-insensitive duplicate");
        }
        catch (const std::exception &e)
        {
            errors.push_back(file.path().string() + ": " + e.what());
        }
    }
    if (!errors.empty())
    {
        std::string message = "input validation failed";
        for (const auto &e : errors)
            message += '\n' + e;
        throw Error(message);
    }
    std::map<std::string,std::string> deleted;
    if(fs::is_regular_file(source/L"_deleted.txt")) {
        std::ifstream manifest(source/L"_deleted.txt",std::ios::binary);std::string line;
        while(std::getline(manifest,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;auto key=Normalize(line);Require(!inputs.count(key)&&deleted.emplace(key,line).second,"duplicate deleted name");inputs.emplace(key,fs::path());}
    }
    Output out(output,true);
    for (unsigned i = 0; i < headerBytes; ++i)
        out.stream.put(0);
    std::vector<Entry> entries;
    for (const auto &item : inputs)
    {
        if(item.second.empty()) {
            Entry e;e.name=deleted.at(item.first);e.method=2;
            auto position=static_cast<uint64_t>(out.stream.tellp());e.offset=Align(position);while(position++<e.offset)out.stream.put(0);entries.push_back(e);continue;
        }
        auto bytes = ReadFile(item.second);
        Require(bytes.size() <= UINT32_MAX, "input changed size during packing");
        if (bytes.empty())
            std::cerr << "warning: empty file: " << item.first << '\n';
        Entry e;
        e.name = ResourceName(item.second.lexically_relative(source).wstring());
        e.size = bytes.size();
        e.hash = Hash(bytes);
        e.method = bytes.empty() || Stored(bytes) ? 0 : 1;
        Bytes payload;
        if (e.method)
        {
            fs::path cached;
            if (!cache.empty())
                cached = cache / CacheKey(e.hash, e.size);
            if (!cached.empty() && fs::is_regular_file(cached))
            {
                try
                {
                    payload = ReadFile(cached);
                    Require(Decode(payload, e) == bytes, "cache mismatch");
                }
                catch (const std::exception &)
                {
                    payload.clear();
                }
            }
            if (payload.empty())
            {
                payload = Compress(bytes);
                if (!cached.empty())
                    Save(cached, payload);
            }
        }
        else
            payload = std::move(bytes);
        auto position = static_cast<std::uint64_t>(out.stream.tellp());
        e.offset = Align(position);
        e.stored = payload.size();
        while (position++ < e.offset)
            out.stream.put(0);
        Write(out.stream, payload);
        entries.push_back(e);
    }
    auto position = static_cast<std::uint64_t>(out.stream.tellp());
    const auto toc = Align(position);
    while (position++ < toc)
        out.stream.put(0);
    std::ostringstream table(std::ios::binary | std::ios::out);
    for (const auto &e : entries)
    {
        Put(table, e.name.size(), 4);
        Put(table, e.method, 4);
        Put(table, e.offset, 8);
        Put(table, e.stored, 8);
        Put(table, e.size, 8);
        Put(table, e.hash, 8);
        Put(table, 0, 8);
        table.write(e.name.data(), e.name.size());
    }
    auto tableText=table.str();out.stream.write(tableText.data(),tableText.size());
    out.stream.seekp(0);out.stream.write(magic,8);
    Put(out.stream,0x00010001,4);Put(out.stream,headerBytes,4);Put(out.stream,entries.size(),8);Put(out.stream,toc,8);Put(out.stream,tableText.size(),8);
    Write(out.stream,Sha(Bytes(tableText.begin(),tableText.end())));
    out.Commit();
}
void Verify(const fs::path &path)
{
    Archive a(path);
    for (const auto &e : a.Entries())
        if(e.method!=2)a.Read(e, true);
}
void Unpack(const fs::path &input, const fs::path &output)
{
    if (_wcsicmp(input.extension().c_str(), L".pak") == 0)
    {
        Archive archive(input);std::map<std::string,std::string> prefixes;
        fs::create_directories(output);
        std::string deleted;
        for (const auto &e : archive.Entries())
            if(e.method==2)deleted+=e.name+"\r\n";else Save(Destination(output,CanonicalName(e.name,prefixes)),archive.Read(e,true));
        if(!deleted.empty())Save(output/L"_deleted.txt",Bytes(deleted.begin(),deleted.end()));
        return;
    }
    std::ifstream in(input, std::ios::binary);
    Require(bool(in), "cannot open RES");
    const auto bytes = fs::file_size(input);
    auto h = ReadAt(in, 0, 16);
    Require(Number(h.data(), 4) == 0x019ce23c, "unsupported RES signature");
    const auto count = Number(h.data() + 4, 4), tableOffset = Number(h.data() + 8, 4),
               nameSize = Number(h.data() + 12, 4);
    Require(tableOffset <= bytes && count * 22 <= bytes - tableOffset &&
                nameSize <= bytes - tableOffset - count * 22,
            "RES table bounds");
    auto table = ReadAt(in, tableOffset, count * 22 + nameSize);
    std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> entries;
    std::map<std::string,std::string> prefixes;
    for (std::uint64_t i = 0; i < count; ++i)
    {
        auto *p = table.data() + i * 22;
        const auto size = Number(p + 4, 4), offset = Number(p + 8, 4), len = Number(p + 16, 2),
                   name = Number(p + 18, 4);
        if (!offset || !len)
            continue; // Unused slots do not represent files.
        Require(offset <= bytes && size <= bytes - offset && name <= nameSize &&
                    len <= nameSize - name,
                "RES entry bounds");
        std::string raw(reinterpret_cast<char *>(table.data() + count * 22 + name),
                        static_cast<size_t>(len));
        Normalize(raw); auto normalized = CanonicalName(raw,prefixes);
        auto inserted = entries.emplace(normalized, std::make_pair(offset, size));
        Require(inserted.second || inserted.first->second == std::make_pair(offset, size),
                "conflicting RES duplicate: " + raw);
    }
    for (const auto &e : entries)
        Save(Destination(output, e.first), ReadAt(in, e.second.first, e.second.second));
}
namespace {
void Flush(const fs::path& path)
{
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Require(h!=INVALID_HANDLE_VALUE,"cannot open file to flush");bool ok=FlushFileBuffers(h)!=0;CloseHandle(h);Require(ok,"flush failed");
}
void Publish(const fs::path& from,const fs::path& to)
{
    Require(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"cannot publish "+to.u8string());
}
Json ReadJson(const fs::path& file){std::ifstream in(file,std::ios::binary);Require(bool(in),"cannot open JSON");Json j;in>>j;return j;}
fs::path Absolute(const fs::path& path){return fs::absolute(path).lexically_normal();}
std::string Digest(const fs::path& path){auto hash=Sha(ReadFile(path));return Hex(hash.data(),hash.size());}
void CheckExpected(const fs::path& path,const Json& expected)
{
    if(expected.is_null()){Require(!fs::exists(path),"archive was created externally; reload before saving");return;}
    Require(expected.is_string(),"expected_sha256 must be a hex digest or null");
    auto value=expected.get<std::string>();
    Require(value.size()==64&&std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}),"expected_sha256 must be lowercase SHA-256");
    Require(fs::exists(path)&&Sha(ReadFile(path))==Unhex(value),"archive changed externally; reload before saving");
}
std::string ChangeName(const Json& change)
{
    auto bytes=Unhex(change.at("name_hex").get<std::string>());std::string name(bytes.begin(),bytes.end());Normalize(name);return name;
}
void BuildChanges(const fs::path& target,const Json& changes,const fs::path& base,const fs::path& tmp)
{
    Require(changes.is_array(),"changes must be an array");
    // Materialize one archive beside its staging file; never touch installed loose resources.
    auto work=tmp;work+=L".work";Require(!fs::exists(work),"stale work directory; recover first");fs::create_directories(work);
    struct Cleanup{fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{work};
    std::map<std::string,std::string> deleted;
    if(fs::exists(target)) {
        Archive archive(target);std::map<std::string,std::string> prefixes;
        for(const auto& e:archive.Entries())if(e.method==2)deleted.emplace(Normalize(e.name),e.name);else Save(Destination(work,CanonicalName(e.name,prefixes)),archive.Read(e,true));
    }
    std::set<std::string> seen;
    for(const auto& change:changes) {
        auto name=ChangeName(change),key=Normalize(name);Require(seen.insert(key).second,"duplicate change name");
        // Remove the old spelling too, since names are stored verbatim but compared folded.
        if(fs::exists(target)){Archive old(target);if(auto e=old.Find(name))if(e->method!=2)fs::remove(Destination(work,e->name));}
        auto op=change.at("op").get<std::string>();
        if(op=="delete"){deleted[key]=name;}
        else {
            Require(op=="put","unknown change operation");auto file=fs::u8path(change.at("file").get<std::string>());if(file.is_relative())file=base/file;
            Save(Destination(work,name),ReadFile(file));deleted.erase(key);
        }
    }
    std::string lines;for(const auto& item:deleted){Require(item.second.find_first_of("\r\n")==std::string::npos,"newline in deleted name");lines+=item.second+"\r\n";}
    if(!lines.empty())Save(work/L"_deleted.txt",Bytes(lines.begin(),lines.end()));
    Pack(work,tmp);Verify(tmp);Flush(tmp);
}
struct Target {fs::path path,tmp;Json changes;Json expected;};
std::vector<std::unique_ptr<Lock>> Locks(const std::vector<Target>& targets)
{
    std::vector<std::unique_ptr<Lock>> locks;for(const auto& t:targets)locks.push_back(std::make_unique<Lock>(t.path));return locks;
}
void SortTargets(std::vector<Target>& targets)
{
    auto key=[](const fs::path& p){auto s=p.wstring();for(auto& c:s)c=static_cast<wchar_t>(towlower(c));return s;};
    std::sort(targets.begin(),targets.end(),[&](const Target& a,const Target& b){return key(a.path)<key(b.path);});
    for(size_t i=1;i<targets.size();++i)Require(key(targets[i-1].path)!=key(targets[i].path),"duplicate target");
}
void RecoverDirectory(const fs::path& directory)
{
    Require(fs::is_directory(directory),"recovery directory missing");
    // One manifest at a time; member paths are relative to its common ancestor.
    for(auto cursor=fs::recursive_directory_iterator(directory);cursor!=fs::recursive_directory_iterator();++cursor) {
        const auto& file=*cursor;
        if(file.is_directory()&&file.path().extension()==L".work"){cursor.disable_recursion_pending();continue;}
        auto name=file.path().filename().u8string();if(name.rfind("pak-txn-",0)!=0||file.path().extension()!=L".json")continue;
        auto j=ReadJson(file.path());Require(j.at("version")==1,"unknown transaction manifest");std::vector<Target> targets;
        for(const auto& member:j.at("targets")) {
            auto p=fs::u8path(member.at("target").get<std::string>());Require(!p.empty()&&!p.is_absolute()&&!p.has_root_name()&&_wcsicmp(p.extension().c_str(),L".pak")==0,"unsafe manifest target");for(const auto& part:p)Require(part!=L".."&&part!=L".","unsafe manifest component");auto tmp=p;tmp+=L".tmp";
            Require(fs::u8path(member.at("tmp").get<std::string>())==tmp,"unsafe manifest tmp");targets.push_back({file.path().parent_path()/p,file.path().parent_path()/tmp,member});
        }
        Require(!targets.empty(),"empty manifest");SortTargets(targets);auto locks=Locks(targets);
        // Re-read after locking: another recovery may have completed while we waited.
        Require(fs::exists(file.path()),"manifest disappeared; retry recovery");
        for(const auto& t:targets){auto source=fs::exists(t.tmp)?t.tmp:t.path;Require(Digest(source)==t.changes.at("sha256").get<std::string>(),"transaction hash mismatch; preserved for investigation");Verify(source);}
        for(const auto& t:targets)if(fs::exists(t.tmp))Publish(t.tmp,t.path);
        Require(fs::remove(file.path()),"cannot remove committed manifest");
    }
    std::vector<fs::path> orphans;
    for(auto i=fs::recursive_directory_iterator(directory);i!=fs::recursive_directory_iterator();++i) {
        auto candidate=i->path();bool work=candidate.extension()==L".work";
        if(work){candidate.replace_extension();if(candidate.extension()==L".tmp")i.disable_recursion_pending();}
        if(candidate.extension()!=L".tmp")continue;auto target=candidate;target.replace_extension();if(_wcsicmp(target.extension().c_str(),L".pak")!=0)continue;
        orphans.push_back(i->path());
    }
    for(const auto& orphan:orphans){auto target=orphan;if(target.extension()==L".work")target.replace_extension();target.replace_extension();Lock lock(target);NoPending(target.parent_path());fs::remove_all(orphan);}
}
}
void List(const fs::path& path)
{
    Archive a(path);Json rows=Json::array();for(const auto& e:a.Entries())rows.push_back({{"name_hex",Hex(e.name.data(),e.name.size())},{"path",fs::path(Wide(DiskName(e.name))).generic_u8string()},{"method",e.method},{"size",e.size},{"stored",e.stored},{"hash",Hex(&e.hash,8)}});
    std::cout<<rows.dump(2)<<'\n';
}
void Extract(const fs::path& path,const std::string& name,const fs::path& output)
{
    Archive a(path);auto e=a.Find(name);Require(e&&e->method!=2,"entry not found or deleted");Save(output,a.Read(*e,true));
}
void Apply(const fs::path& path,const fs::path& changesFile)
{
    auto target=Absolute(path);Require(_wcsicmp(target.extension().c_str(),L".pak")==0,"target must be .pak");auto tmp=target;tmp+=L".tmp";Lock lock(target);NoPending(target.parent_path());
    Require(!fs::exists(tmp),"stale staging file; recover first");
    auto request=ReadJson(changesFile);CheckExpected(target,request.at("expected_sha256"));
    try{BuildChanges(target,request.at("changes"),Absolute(changesFile).parent_path(),tmp);Publish(tmp,target);}catch(...){std::error_code ec;fs::remove(tmp,ec);throw;}
}
void Transaction(const fs::path& changesFile)
{
    auto base=Absolute(changesFile).parent_path();auto j=ReadJson(changesFile);std::vector<Target> targets;
    for(const auto& item:j.at("archives")) {
        auto path=fs::u8path(item.at("pak").get<std::string>());if(path.is_relative())path=base/path;path=Absolute(path);Require(_wcsicmp(path.extension().c_str(),L".pak")==0,"target must be .pak");auto tmp=path;tmp+=L".tmp";targets.push_back({path,tmp,item.at("changes"),item.at("expected_sha256")});
    }
    Require(!targets.empty(),"empty transaction");SortTargets(targets);auto directory=targets.front().path.parent_path();for(const auto& t:targets) {
        Require(t.path.root_name()==directory.root_name(),"transaction targets must share a volume");
        for(;;){auto relative=t.path.lexically_relative(directory);if(!relative.empty()&&*relative.begin()!=L"..")break;auto parent=directory.parent_path();Require(parent!=directory,"no common transaction directory");directory=parent;}
    }
    auto locks=Locks(targets);for(const auto& t:targets){NoPending(t.path.parent_path());Require(!fs::exists(t.tmp),"stale staging file; recover first");}
    for(const auto& t:targets)CheckExpected(t.path,t.expected);
    auto manifest=directory/(L"pak-txn-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".json");bool committed=false;
    try {
        Json members=Json::array();for(const auto& t:targets){BuildChanges(t.path,t.changes,base,t.tmp);members.push_back({{"target",t.path.lexically_relative(directory).generic_u8string()},{"tmp",t.tmp.lexically_relative(directory).generic_u8string()},{"sha256",Digest(t.tmp)}});}
        auto text=Json{{"version",1},{"targets",members}}.dump(2);Save(manifest,Bytes(text.begin(),text.end()));committed=true;
        for(const auto& t:targets)Publish(t.tmp,t.path);Require(fs::remove(manifest),"cannot remove manifest");
    }catch(...){if(!committed)for(const auto& t:targets){std::error_code ec;fs::remove(t.tmp,ec);}throw;}
}
void Recover(const fs::path& directory){RecoverDirectory(Absolute(directory));}

} // namespace ei::pak
