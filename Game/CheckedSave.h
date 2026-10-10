#pragma once
#include "CheckedCruncher.h"

namespace CheckedSave
{
inline uint32_t Word(const unsigned char *p)
{ return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
inline uint32_t Crc(const unsigned char *p, size_t size)
{
    uint32_t crc = 0;
    for (size_t i = 0; size - i >= 4; i += 4)
    { crc ^= Word(p + i); crc *= (crc & 255) | 3; }
    return crc;
}
inline bool Decode(const unsigned char *p, size_t size, uint32_t signature,
                   int maximumVersion, int minimumVersion,
                   std::vector<unsigned char> &output, int &version, CheckedCruncher::Error &error)
{
    output.clear();
    auto fail = [&](const char *reason, size_t offset) { error = {reason, offset}; return false; };
    if (size < 8) return fail("truncated_save_header", size);
    if (Word(p) != signature) return fail("invalid_signature", 0);
    version = static_cast<int>(Word(p + 4));
    if (version < minimumVersion || version > maximumVersion) return fail("unsupported_version", 4);
    size_t start = 8;
    uint32_t crc = 0;
    bool packed = version > 100;
    if (version > 107)
    {
        if (size < 12) return fail("truncated_crc", size);
        crc = Word(p + 8); start = 12;
    }
    if (version > 113)
    {
        if (size < 13) return fail("truncated_compression_flag", size);
        if (p[12] > 1) return fail("invalid_compression_flag", 12);
        packed = p[12] != 0; start = 13;
    }
    if (packed)
    {
        if (!CheckedCruncher::Unpack(p + start, size - start, output, error))
        { error.offset += start; return false; }
    }
    else
    {
        if (size - start > 512u * 1024 * 1024) return fail("invalid_raw_length", start);
        try { output.assign(p + start, p + size); }
        catch (const std::bad_alloc &) { return fail("allocation_failed", start); }
    }
    if (version > 107 && Crc(output.data(), output.size()) != crc)
    { output.clear(); return fail("crc_mismatch", 8); }
    return true;
}
}
