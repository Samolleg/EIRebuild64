#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <new>

// The wire format interleaves LSB-first control bytes and literal bytes.
// Kept independent of the engine so the same reader can validate offline files.
namespace CheckedCruncher
{
struct Error { const char *reason = "ok"; size_t offset = 0; };
class Reader
{
    const unsigned char *bytes;
    size_t size, position = 4;
    unsigned int bits = 0, count = 0;
public:
    Reader(const unsigned char *p, size_t n) : bytes(p), size(n) {}
    size_t Position() const { return position; }
    unsigned int Read(unsigned int n)
    {
        while (count < n)
        {
            if (position == size) throw Error{"truncated_compressed_stream", position};
            bits |= unsigned(bytes[position++]) << count;
            count += 8;
        }
        unsigned int result = bits & ((1u << n) - 1);
        bits >>= n; count -= n;
        return result;
    }
    unsigned char Literal()
    {
        if (position == size) throw Error{"truncated_literal", position};
        return bytes[position++];
    }
};
inline bool Unpack(const unsigned char *input, size_t size,
                   std::vector<unsigned char> &output, Error &error)
{
    output.clear();
    try
    {
        if (size < 4) throw Error{"truncated_compressed_header", size};
        uint32_t length = uint32_t(input[0]) | uint32_t(input[1]) << 8 |
            uint32_t(input[2]) << 16 | uint32_t(input[3]) << 24;
        // A token emits at most 226 bytes; this also rejects impossible allocations
        // before touching the heap. The absolute bound protects a 32-bit process.
        if (length > 512u * 1024 * 1024 || uint64_t(length) > uint64_t(size - 4) * 226)
            throw Error{"invalid_unpacked_length", 0};
        Reader r(input, size);
        output.reserve(length);
        while (output.size() < length)
        {
            if (!r.Read(1)) { output.push_back(r.Literal()); continue; }
            unsigned int n, shift;
            if (r.Read(1))
            {
                if (r.Read(1))
                {
                    if (r.Read(1))
                    {
                        if (r.Read(1)) n = r.Read(1) ? 99 + r.Read(7) : 19 + r.Read(4);
                        else n = 6;
                    }
                    else n = r.Read(1) ? 35 + r.Read(6) : 5;
                }
                else if (r.Read(1)) n = 7 + r.Read(2);
                else n = r.Read(1) ? 11 + r.Read(3) : 3;
            }
            else n = r.Read(1) ? 4 : 2;
            if (r.Read(1)) shift = r.Read(1) ? 354 + r.Read(10) : 1378 + r.Read(12);
            else if (r.Read(1)) shift = r.Read(1) ? 98 + r.Read(8) : 34 + r.Read(6);
            else if (r.Read(1))
            {
                if (r.Read(1)) shift = r.Read(1) ? r.Read(1) : 18 + r.Read(4);
                else shift = 6 + r.Read(2);
            }
            else shift = r.Read(1) ? 2 + r.Read(2) : 10 + r.Read(3);
            // Retail wraps history addresses modulo 1024. Preserve that format.
            shift &= 1023;
            if (n > length - output.size()) throw Error{"match_exceeds_output", r.Position()};
            if (shift >= output.size() && !(shift == output.size() && output.size() < 1024))
                throw Error{"invalid_history_reference", r.Position()};
            for (unsigned int i = 0; i < n; ++i)
                output.push_back(shift == output.size() ? 0 : output[output.size() - shift - 1]);
        }
        if (r.Position() != size) throw Error{"trailing_compressed_bytes", r.Position()};
        return true;
    }
    catch (const Error &e) { error = e; }
    catch (const std::bad_alloc &) { error = Error{"allocation_failed", 0}; }
    output.clear();
    return false;
}
}
