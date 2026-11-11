#include "Pak.h"
#include <iostream>
int wmain(int argc, wchar_t **argv)
{
    try
    {
        if (argc == 3 && std::wstring(argv[1]) == L"verify")
        {
            ei::pak::Verify(argv[2]);
            return 0;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"unpack")
        {
            ei::pak::Unpack(argv[2], argv[3]);
            return 0;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"pack" && std::wstring(argv[2]) == L"--cache")
        {
            std::filesystem::path source = std::filesystem::absolute(argv[3]).lexically_normal();
            auto output = source;
            output += L".pak";
            ei::pak::Pack(source, output, source.parent_path() / L".eipak-cache");
            return 0;
        }
        if (argc == 4 && std::wstring(argv[1]) == L"pack")
        {
            ei::pak::Pack(argv[2], argv[3]);
            return 0;
        }
        if (argc == 6 && std::wstring(argv[1]) == L"pack" && std::wstring(argv[2]) == L"--cache")
        {
            ei::pak::Pack(argv[4], argv[5], argv[3]);
            return 0;
        }
        std::cerr << "pack <folder> <out.pak>\npack --cache <folder>\npack --cache <cache-folder> "
                     "<folder> <out.pak>\nunpack <in.res|in.pak> <folder>\nverify <in.pak>\n";
        return 2;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
