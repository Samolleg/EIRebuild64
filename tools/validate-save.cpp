// Offline envelope validation. Uses precisely the checked production reader.
#include "../Game/CheckedSave.h"
#include <fstream>
#include <iterator>
#include <iostream>
int wmain(int argc, wchar_t **argv)
{
    if (argc < 2 || argc > 3) return 2;
    try
    {
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        if (!file || file.tellg() < 0 || file.tellg() > 512 * 1024 * 1024)
        { std::cerr << "invalid_file_length offset=0\n"; return 10; }
        size_t length = static_cast<size_t>(file.tellg());
        file.seekg(0);
        std::vector<unsigned char> raw(length), decoded;
        if (length && !file.read(reinterpret_cast<char *>(raw.data()), length))
        { std::cerr << "incomplete_file_read offset=0\n"; return 10; }
        uint32_t signature = length >= 4 ? CheckedSave::Word(raw.data()) : 0;
        if (signature != 0x114 && signature != 0x102 && signature != 0xDEAD)
        { std::cerr << "invalid_signature offset=0\n"; return 10; }
        CheckedCruncher::Error error;
        int version = 0;
        if (!CheckedSave::Decode(raw.data(), raw.size(), signature, 117,
                                signature == 0x114 ? 117 : 66, decoded, version, error))
        { std::cerr << error.reason << " offset=" << error.offset << '\n'; return 10; }
        if (argc == 3)
        {
            std::ofstream out(argv[2], std::ios::binary);
            if (!out.write(reinterpret_cast<const char *>(decoded.data()), decoded.size())) return 11;
        }
        std::cout << "valid payload_bytes=" << decoded.size() << '\n';
        return 0;
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 12; }
}
