from pathlib import Path
import shutil
r=Path('work/eirebuild-local')
def edit(f,a,b):
 p=r/f;v=p.read_bytes();nl=b'\r\n' if b'\r\n' in v else b'\n';x=a.encode().replace(b'\n',nl);y=b.encode().replace(b'\n',nl);assert v.count(x)==1,(f,a[:35],v.count(x));p.write_bytes(v.replace(x,y))
for f in ['Game/Win32Classes.cpp','Game/ChunkFile.cpp','Game/ResourceManager.cpp']:
 d=r/'evidence/addon-integration/pre-r16'/f;d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(r/f,d)
edit('Game/CMappedResource.cpp','std::set<std::string> seen;CStringArray local;','names.RemoveAll();std::set<std::string> seen;CStringArray local;')
edit('Game/Win32Classes.cpp','#include "StdAfx.h"','#include "StdAfx.h"\n#include "ModRuntime.h"')
edit('Game/Win32Classes.cpp','BOOL CMappedFile::Open(LPCSTR szFileName, BOOL bWriteAccess)\n{','BOOL CMappedFile::Open(LPCSTR szFileName, BOOL bWriteAccess)\n{\n    std::string resolved=bWriteAccess?szFileName:ModRuntime::MapPath(szFileName);szFileName=resolved.c_str();')
edit('Game/ChunkFile.cpp','#include "StdAfx.h"','#include "StdAfx.h"\n#include "ModRuntime.h"')
edit('Game/ChunkFile.cpp','    return m_fFile.Open(szName, flags);','    std::string resolved=(flags&3)==CFile::modeRead?ModRuntime::MapPath(szName):std::string((LPCSTR)szName);\n    return m_fFile.Open(resolved.c_str(), flags);')
edit('Game/ResourceManager.cpp','#include "ResourceManager.h"','#include "ResourceManager.h"\n#include "ModRuntime.h"\n#include "ModPackage.h"')
edit('Game/ResourceManager.cpp','    if (_access(pszResourceFileName, 4) == -1)\n        return FALSE;','''    if (_access(pszResourceFileName, 4) == -1) {
        std::vector<std::string> paths;bool ignore=false;
        if(!ModRuntime::Resources()||!ModRuntime::Resources()->ResourcePlan(pszResourceFileName,paths,ignore)||!ignore)return FALSE;
    }''')
edit('Game/ResourceManager.cpp','BOOL CResourceManager::AddGraphicsResources(LPCSTR category)\n{','BOOL CResourceManager::AddGraphicsResources(LPCSTR category)\n{\n    if(ModRuntime::Resources())return TRUE; // The selected mod owns its ordered graphics archives.')
# Add test to existing pre-window inspector, using real native readers and config/map consumers.
edit('Game/ModPackage.cpp','bool InspectModPackageCommandLine(int &result){','''namespace {
Json CheckStartup(const std::string&directory){
 CModPackage expected;expected.Load(directory);CMappedResource native;CWin32MappedResource mapped;int entries=0;std::string base=(fs::current_path()/"res/database.res").string();
 {
  ModRuntime::Host host(directory);
  if(!ModRuntime::Current()||!ModRuntime::Resources())throw std::runtime_error("Host did not bind runtime and resources");
  if(!native.OpenResource(base.c_str())||!mapped.OpenResource(base.c_str()))throw std::runtime_error("Native database resources failed to open");
  for(const char*name:{"units.udb","items.idb","perks.pdb"}){
   std::string owner;auto bytes=expected.ReadResource("database.res",name,base,owner);DWORD n=0,m=0;auto*a=native.GetFile(name,&n);auto*b=mapped.GetFile(name,&m);
   if(!a||!b||n!=bytes.size()||m!=n||memcmp(a,bytes.data(),n)||memcmp(b,bytes.data(),n))throw std::runtime_error("Native RES override bytes differ");
   CResourceBase*poly=&native;CResourceBase*polyMapped=&mapped;if(!poly->CheckFileExist(name)||!polyMapped->CheckFileExist(name))throw std::runtime_error("Native RES existence differs");
   if(!native.CloseFile(a)||!mapped.CloseFile(b))throw std::runtime_error("Resource owner failed to close entry");entries++;
  }
  CStringArray names;names.Add("__stale__");mapped.GetFileList(names,"*.udb");if(names.GetSize()==0)throw std::runtime_error("Overlay enumeration empty");
  for(int i=0;i<names.GetSize();i++)if(names[i]=="__stale__")throw std::runtime_error("Enumeration retained stale output");
  CFileRegistry config;if(!config.Open("config\\\\ai.reg"))throw std::runtime_error("Config consumer did not resolve mod registry");
  std::string actual=ModRuntime::ConfigPath("config\\\\ai.reg"),wanted=expected.ResolveConfig("ai.reg","config\\\\ai.reg");if(actual!=wanted)throw std::runtime_error("Config consumer mismatch");
  auto mapRoot=fs::path(directory)/"Maps";bool mapTest=false;for(const auto&f:fs::recursive_directory_iterator(mapRoot))if(f.is_regular_file()&&f.path().extension()==".mpr"){
   auto relative=fs::relative(f.path(),mapRoot).string();std::string candidate="maps\\\\"+relative,resolved=ModRuntime::MapPath(candidate.c_str());if(fs::path(resolved)!=f.path())throw std::runtime_error("Map path mismatch");CFileSave file;if(!file.OpenRead(candidate.c_str()))throw std::runtime_error("Native map read failed");file.CloseFile();mapTest=true;break;}
  if(!mapTest)throw std::runtime_error("No installed map fixture");
 }
 if(ModRuntime::Current()||ModRuntime::Resources())throw std::runtime_error("Host leaked its binding");
 DWORD size=0;auto*p=mapped.GetFile("units.udb",&size);if(!p||!mapped.CloseFile(p))throw std::runtime_error("Resource handle lifetime depended on destroyed Host");
 return {{"success",true},{"hostLifecycle",true},{"nativeReaderEntries",entries},{"mappedReaderEntries",entries},{"resourceOwnership",true},{"enumeration",true},{"configConsumer",true},{"mapReadConsumer",true},{"gameplayActivated",false}};
}
}
bool InspectModPackageCommandLine(int &result){''')
edit('Game/ModPackage.cpp','if(argc<2||wcscmp(argv[1],L"--inspect-addon")!=0)','bool startup=argc>=2&&wcscmp(argv[1],L"--inspect-mod-startup")==0;\n if(argc<2||(!startup&&wcscmp(argv[1],L"--inspect-addon")!=0))')
edit('Game/ModPackage.cpp','CModPackage package;package.Load(Ansi(argv[2]));report=package.Describe();result=0;','if(startup)report=CheckStartup(Ansi(argv[2]));else{CModPackage package;package.Load(Ansi(argv[2]));report=package.Describe();}result=0;')
