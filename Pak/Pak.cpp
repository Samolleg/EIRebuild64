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

namespace ei::pak {
namespace fs = std::filesystem;
namespace {
constexpr char magic[8] = { 'E', 'I', 'P', 'A', 'K', '\r', '\n', 0 };
constexpr std::uint64_t headerBytes = 40, recordBytes = 48;
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
Bytes ReadFile(const fs::path &path)
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
// Each writer owns an exclusive staging file. Rename publishes a complete file.
class Output
{
    fs::path staging, target;

public:
    std::ofstream stream;
    explicit Output(const fs::path &path)
        : target(path)
    {
        fs::create_directories(fs::absolute(target).parent_path());
        for (unsigned i = 0;; ++i)
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
        fs::remove(staging, ec);
    }
    void Commit()
    {
        stream.flush();
        Require(bool(stream), "write failed");
        stream.close();
        Require(MoveFileExW(staging.c_str(), target.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0,
                "cannot publish " + target.string());
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
std::string DiskName(const std::string &name)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char c : name)
    {
        if (c == '%' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
        else
            result += char(c);
    }
    return result;
}
std::string ResourceName(const std::string &disk)
{
    std::string result;
    for (size_t i = 0; i < disk.size(); ++i)
    {
        if (disk[i] == '%' && i + 2 < disk.size())
        {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                return -1;
            };
            int hi = hex(disk[i + 1]), lo = hex(disk[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                char c = char(hi * 16 + lo);
                if (c == '%' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
                    c == '|')
                {
                    result += c;
                    i += 2;
                    continue;
                }
            }
        }
        result += disk[i];
    }
    return Normalize(result);
}
fs::path Destination(const fs::path &root, const std::string &name)
{
    fs::path relative(DiskName(Normalize(name)));
    fs::path current = fs::absolute(root);
    Require(!fs::is_symlink(fs::symlink_status(current)), "symlink output directory");
    for (const auto &part : relative)
    {
        current /= part;
        Require(!fs::is_symlink(fs::symlink_status(current)), "symlink output entry");
    }
    return current;
}
} // namespace

std::string Normalize(const std::string &name)
{
    Require(!name.empty() && name.size() <= 1024, "name length must be 1..1024 bytes");
    std::string result;
    for (unsigned char c : name)
    {
        Require(c >= 32 && c != 127 && (c < 128 || c >= 224), "unsupported name encoding");
        Require(c != ':', "invalid name character");
        if (c == '/')
            c = '\\';
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        result += char(c);
    }
    size_t start = 0;
    do
    {
        const auto end = result.find('\\', start);
        const auto part = result.substr(start, end == std::string::npos ? end : end - start);
        Require(!part.empty() && part != "." && part != ".." && part.back() != '.' &&
                    part.back() != ' ',
                "invalid name segment");
        const auto stem = part.substr(0, part.find('.'));
        Require(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" &&
                    !(stem.size() == 4 &&
                      (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
                      stem[3] >= '1' && stem[3] <= '9'),
                "reserved name");
        if (end == std::string::npos)
            break;
        start = end + 1;
    } while (true);
    return result;
}
std::uint64_t Hash(const Bytes &b)
{
    return XXH64(b.data(), b.size(), 0);
}
struct Archive::State
{
    std::ifstream stream;
    fs::path path;
    std::vector<Entry> entries;
    mutable std::mutex mutex;
    std::set<std::string> verified;
    explicit State(const fs::path &p)
        : stream(p, std::ios::binary)
        , path(p)
    {
    }
};
Archive::Archive(const fs::path &path)
    : state(std::make_shared<State>(path))
{
    try
    {
        auto &in = state->stream;
        Require(bool(in), "cannot open archive");
        const auto bytes = fs::file_size(path);
        auto h = ReadAt(in, 0, headerBytes);
        Require(std::equal(std::begin(magic), std::end(magic), h.begin()), "invalid PAK magic");
        Require(Number(h.data() + 8, 4) == 1 && Number(h.data() + 12, 4) == headerBytes,
                "unsupported PAK version/header");
        const auto count = Number(h.data() + 16, 8), toc = Number(h.data() + 24, 8),
                   tocSize = Number(h.data() + 32, 8);
        Require(toc >= headerBytes && toc % 8 == 0 && toc <= bytes && tocSize == bytes - toc &&
                    count <= tocSize / recordBytes,
                "invalid PAK table bounds");
        auto table = ReadAt(in, toc, tocSize);
        size_t cursor = 0;
        std::string previous;
        std::uint64_t dataEnd = headerBytes;
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
            Require(Normalize(e.name) == e.name && (previous.empty() || previous < e.name),
                    "noncanonical or duplicate PAK name");
            Require(e.method <= 1 && e.size <= UINT32_MAX &&
                        (e.size ? e.stored != 0 : e.method == 0 && e.stored == 0) &&
                        e.offset == Align(dataEnd) && e.offset <= toc &&
                        e.stored <= toc - e.offset && (e.method || e.stored == e.size),
                    "invalid PAK data bounds/method");
            dataEnd = e.offset + e.stored;
            previous = e.name;
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
                               [](const Entry &e, const std::string &n) { return e.name < n; });
    return it != state->entries.end() && it->name == key ? &*it : nullptr;
}
Bytes Archive::Read(const Entry &e, bool verify) const
{
    std::lock_guard<std::mutex> lock(state->mutex);
    try
    {
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
            auto name = ResourceName(file.path().lexically_relative(source).string());
            const auto size = file.file_size();
            Require(size <= UINT32_MAX, "oversized file");
            Require(inputs.emplace(name, file.path()).second, "case-insensitive duplicate");
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
    Output out(output);
    for (unsigned i = 0; i < headerBytes; ++i)
        out.stream.put(0);
    std::vector<Entry> entries;
    for (const auto &item : inputs)
    {
        auto bytes = ReadFile(item.second);
        Require(bytes.size() <= UINT32_MAX, "input changed size during packing");
        if (bytes.empty())
            std::cerr << "warning: empty file: " << item.first << '\n';
        Entry e;
        e.name = item.first;
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
    for (const auto &e : entries)
    {
        Put(out.stream, e.name.size(), 4);
        Put(out.stream, e.method, 4);
        Put(out.stream, e.offset, 8);
        Put(out.stream, e.stored, 8);
        Put(out.stream, e.size, 8);
        Put(out.stream, e.hash, 8);
        Put(out.stream, 0, 8);
        out.stream.write(e.name.data(), e.name.size());
    }
    const auto end = static_cast<std::uint64_t>(out.stream.tellp());
    out.stream.seekp(0);
    out.stream.write(magic, 8);
    Put(out.stream, 1, 4);
    Put(out.stream, headerBytes, 4);
    Put(out.stream, entries.size(), 8);
    Put(out.stream, toc, 8);
    Put(out.stream, end - toc, 8);
    out.Commit();
}
void Verify(const fs::path &path)
{
    Archive a(path);
    for (const auto &e : a.Entries())
        a.Read(e, true);
}
void Unpack(const fs::path &input, const fs::path &output)
{
    if (_wcsicmp(input.extension().c_str(), L".pak") == 0)
    {
        Archive archive(input);
        for (const auto &e : archive.Entries())
            Save(Destination(output, e.name), archive.Read(e, true));
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
        auto normalized = Normalize(raw);
        auto inserted = entries.emplace(normalized, std::make_pair(offset, size));
        Require(inserted.second || inserted.first->second == std::make_pair(offset, size),
                "conflicting RES duplicate: " + raw);
    }
    for (const auto &e : entries)
        Save(Destination(output, e.first), ReadAt(in, e.second.first, e.second.second));
}
} // namespace ei::pak
