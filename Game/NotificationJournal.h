#pragma once
#include <array>
#include <cstddef>
#include <cstring>
#include <string>

namespace NotificationJournal
{
constexpr size_t Limit = 50;
struct Boundary
{
    size_t offset = 0;
    std::array<char, 3> font = {};
    std::array<char, 5> color = {};
    bool hasFont = false, hasColor = false;
};
struct Tail
{
    size_t lines = 0, removed = 0;
    std::string text; // Allocated only when a prefix actually has to be removed.
};

// Native GetNextLexeme: BEL F digit (3 bytes), BEL C RGB (5 bytes).
// A color payload may itself contain LF; never split or count its bytes as text.
inline Tail LastLines(const char* data, size_t length)
{
    Tail result;
    if (!data || !length) return result;
    std::array<Boundary, Limit + 1> starts = {};
    Boundary style;
    size_t line = 0;
    bool content = false;
    for (size_t i = 0; i < length;)
    {
        if (data[i] == '\a' && i + 1 < length)
        {
            if (data[i+1] == 'F' && length-i >= 3)
            {
                std::memcpy(style.font.data(), data+i, 3);
                style.hasFont = true; i += 3; continue;
            }
            if (data[i+1] == 'C' && length-i >= 5)
            {
                std::memcpy(style.color.data(), data+i, 5);
                style.hasColor = true; i += 5; continue;
            }
        }
        const char ch = data[i++];
        if (ch == '\n')
        {
            if (content) ++line;
            style.offset = i;
            starts[line % starts.size()] = style;
            content = false;
        }
        else if (ch != '\r' && ch != ' ' && ch != '\t') content = true;
    }
    result.lines = line + (content ? 1 : 0);
    if (result.lines <= Limit) return result;
    result.removed = result.lines - Limit;
    const Boundary& first = starts[result.removed % starts.size()];
    // Restore inherited formatting before the first retained text, then keep the
    // original bytes, blank lines, CRLF and trailing controls exactly as authored.
    if (first.hasFont) result.text.append(first.font.data(), first.font.size());
    if (first.hasColor) result.text.append(first.color.data(), first.color.size());
    result.text.append(data + first.offset, length - first.offset);
    return result;
}
}
