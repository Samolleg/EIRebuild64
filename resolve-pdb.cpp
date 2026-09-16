#include <windows.h>
#include <dia2.h>
#include <cstdio>
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")
int wmain(int argc,wchar_t**argv){
 CoInitialize(nullptr);HMODULE dll=LoadLibraryW(L"C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\DIA SDK\\bin\\msdia140.dll");
 auto get=(HRESULT(WINAPI*)(REFCLSID,REFIID,void**))GetProcAddress(dll,"DllGetClassObject");IClassFactory*f=nullptr;IDiaDataSource*source=nullptr;
 if(!get||FAILED(get(__uuidof(DiaSource),IID_IClassFactory,(void**)&f))||FAILED(f->CreateInstance(nullptr,__uuidof(IDiaDataSource),(void**)&source)))return 2;
 HRESULT hr=source->loadDataFromPdb(argv[1]);if(FAILED(hr)){printf("pdb error %lx\n",hr);return 3;}IDiaSession*s=nullptr;source->openSession(&s);
 for(int i=2;i<argc;i++){DWORD rva=wcstoul(argv[i],nullptr,16);IDiaSymbol*symbol=nullptr;printf("%08lx ",rva);if(SUCCEEDED(s->findSymbolByRVA(rva,SymTagFunction,&symbol))&&symbol){BSTR name=nullptr;symbol->get_name(&name);printf("%ls ",name);SysFreeString(name);symbol->Release();}IDiaEnumLineNumbers*lines=nullptr;s->findLinesByRVA(rva,1,&lines);if(lines){IDiaLineNumber*line;ULONG fetched=0;if(lines->Next(1,&line,&fetched)==S_OK){DWORD n;line->get_lineNumber(&n);IDiaSourceFile*file=nullptr;line->get_sourceFile(&file);BSTR name=nullptr;file->get_fileName(&name);printf("%ls:%lu",name,n);SysFreeString(name);file->Release();line->Release();}lines->Release();}puts("");}
 return 0;
}
