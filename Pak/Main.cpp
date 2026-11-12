#include "Pak.h"
#include <iostream>
#include <windows.h>
int wmain(int argc, wchar_t **argv)
{
    try
    {
        if(argc==4&&std::wstring(argv[1])==L"list"&&std::wstring(argv[3])==L"--json"){ei::pak::List(argv[2]);return 0;}
        if(argc==5&&std::wstring(argv[1])==L"extract") {
            std::wstring wide=argv[3];BOOL used=FALSE;int n=WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,wide.data(),int(wide.size()),nullptr,0,nullptr,&used);if(n<=0||used)throw std::runtime_error("name is not CP1251");std::string name(n,0);WideCharToMultiByte(1251,WC_NO_BEST_FIT_CHARS,wide.data(),int(wide.size()),name.data(),n,nullptr,&used);if(used)throw std::runtime_error("name is not CP1251");
            ei::pak::Extract(argv[2],name,argv[4]);return 0;
        }
        if(argc==4&&std::wstring(argv[1])==L"apply"){ei::pak::Apply(argv[2],argv[3]);return 0;}
        if(argc==3&&std::wstring(argv[1])==L"txn"){ei::pak::Transaction(argv[2]);return 0;}
        if(argc==3&&std::wstring(argv[1])==L"recover"){ei::pak::Recover(argv[2]);return 0;}
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
        std::cerr << "list <pak> --json\nextract <pak> <name> <file>\napply <pak> <changes.json>\ntxn <changes-group.json>\nrecover <folder>\npack <folder> <out.pak>\npack --cache <folder>\npack --cache <cache-folder> "
                     "<folder> <out.pak>\nunpack <in.res|in.pak> <folder>\nverify <in.pak>\n";
        return 2;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
