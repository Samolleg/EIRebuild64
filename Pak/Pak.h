#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ei::pak {
using Bytes = std::vector<std::uint8_t>;
struct Entry
{
    std::string name;
    std::uint64_t offset = 0, stored = 0, size = 0, hash = 0;
    std::uint32_t method = 0;
};
// Legacy byte names, ASCII case folding and backslash separators.
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
} // namespace ei::pak
