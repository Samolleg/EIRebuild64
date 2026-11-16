#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cwchar>

namespace ei::pak {
// Keep a logical archive in its original mount position. A selected corrupt
// PAK is an error, never a reason to silently read the old RES instead.
inline std::filesystem::path ResolveArchivePath(const std::filesystem::path& path)
{
    if (_wcsicmp(path.extension().c_str(), L".res") != 0 || std::filesystem::is_directory(path))
        return path;
    auto pak = path;
    pak.replace_extension(L".pak");
    return std::filesystem::is_regular_file(pak) ? pak : path;
}
using Bytes = std::vector<std::uint8_t>;
struct Entry
{
    std::string name;
    std::uint64_t offset = 0, stored = 0, size = 0, hash = 0;
    std::uint32_t method = 0;
};
// Legacy byte names; CP1251 CRT case folding, identical to RES with Font CharSet .1251.
std::string Normalize(const std::string &name);
std::uint64_t Hash(const Bytes &bytes);
class Archive
{
    struct State;
    std::shared_ptr<State> state;

public:
    explicit Archive(const std::filesystem::path &path);
    const std::vector<Entry> &Entries() const;
    const Entry *Find(const std::string &name) const;
    Bytes Read(const Entry &entry, bool verify) const;
};
void Pack(const std::filesystem::path &directory, const std::filesystem::path &output,
          const std::filesystem::path &cache = {});
void Unpack(const std::filesystem::path &input, const std::filesystem::path &output);
void Verify(const std::filesystem::path &input);
void List(const std::filesystem::path& path);
void Extract(const std::filesystem::path& path,const std::string& name,const std::filesystem::path& output);
void Apply(const std::filesystem::path& path,const std::filesystem::path& changes);
void Transaction(const std::filesystem::path& changes);
void Recover(const std::filesystem::path& directory);
} // namespace ei::pak
