from pathlib import Path
import shutil,json,hashlib
r=Path('work/eirebuild-local');ev=r/'evidence/addon-integration/pre-r16';ev.mkdir(parents=True,exist_ok=True)
files=['Game/ModPackage.h','Game/ModPackage.cpp','Game/ModRuntime.h','Game/ModRuntime.cpp','Game/CMappedResource.h','Game/CMappedResource.cpp','Game/CResourceBase.h','Game/FileRegistry.cpp','Game/SaveLoader.h','Game/GameMain.cpp']
m={}
for f in files:
 p=r/f;d=ev/f;d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,d);m[f]=hashlib.sha256(p.read_bytes()).hexdigest()
(ev/'source.json').write_text(json.dumps(m,indent=2))
def edit(f,a,b):
 p=r/f;v=p.read_bytes();nl=b'\r\n' if b'\r\n' in v else b'\n';x=a.encode().replace(b'\n',nl);y=b.encode().replace(b'\n',nl);assert v.count(x)==1,(f,a[:35],v.count(x));p.write_bytes(v.replace(x,y))
edit('Game/ModRuntime.h','#include <nlohmann/json.hpp>','#include <nlohmann/json.hpp>\n#include <memory>\n#include <string>')
edit('Game/ModRuntime.h','CModPackage *Current();','''CModPackage *Current();
CModPackage *Resources();
std::string ConfigPath(const char*);
std::string MapPath(const char*);
class Host {
 std::unique_ptr<CModPackage> package;
 std::unique_ptr<Session> session;
public:
 explicit Host(const std::string& directory);
 ~Host();
 Host(const Host&)=delete;Host&operator=(const Host&)=delete;
 static std::unique_ptr<Host> FromCommandLine();
};''')
edit('Game/ModPackage.h','    void InitializeScript();','    bool ResourcePlan(const std::string&,std::vector<std::string>&,bool&) const;\n    void InitializeScript();')
edit('Game/ModPackage.cpp','CMappedResource &CModPackage::Open','''bool CModPackage::ResourcePlan(const std::string&name,std::vector<std::string>&paths,bool&ignore)const{
 auto key=Lower(fs::path(name).filename().string());auto it=archives.find(key);auto skip=ignoreBase.find(key);ignore=skip!=ignoreBase.end()&&skip->second;if(it!=archives.end())paths=it->second;return it!=archives.end()||ignore;
}
CMappedResource &CModPackage::Open''')
edit('Game/ModPackage.cpp','std::make_unique<CMappedResource>()','std::make_unique<CMappedResource>(true)')
edit('Game/ModRuntime.cpp','#include <filesystem>','#include <filesystem>\n#include <shellapi.h>')
edit('Game/ModRuntime.cpp','namespace { thread_local CModPackage *current = nullptr; }','namespace { thread_local CModPackage *current = nullptr;thread_local CModPackage *resources = nullptr; }')
edit('Game/ModRuntime.cpp','CModPackage *Current() { return current; }','''CModPackage *Current() { return current; }
CModPackage *Resources(){return resources;}
std::string ConfigPath(const char*path){return resources?resources->ResolveConfig(std::filesystem::path(path).filename().string(),path):path;}
std::string MapPath(const char*path){
 if(!resources)return path;std::string normalized=path;for(char&c:normalized){if(c=='/')c='\\\\';else if(c>='A'&&c<='Z')c+=32;}
 auto ext=std::filesystem::path(normalized).extension().string();if(ext!=".mpr"&&ext!=".mob"&&ext!=".mq")return path;
 auto pos=normalized.rfind("\\\\maps\\\\");std::string key=pos==std::string::npos?std::filesystem::path(normalized).filename().string():normalized.substr(pos+6);
 if(normalized.rfind("maps\\\\",0)==0)key=normalized.substr(5);return resources->ResolveMap(key,path);
}
Host::Host(const std::string&directory){
 if(resources||current)throw std::runtime_error("A mod host is already active");
 package=std::make_unique<CModPackage>();package->Load(directory);session=std::make_unique<Session>(*package);resources=package.get();
}
Host::~Host(){resources=nullptr;session.reset();package.reset();}
std::unique_ptr<Host> Host::FromCommandLine(){
 int count=0;LPWSTR*args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)throw std::runtime_error("Cannot parse command line");
 struct Free{LPWSTR*p;~Free(){LocalFree(p);}} free{args};std::string directory;bool found=false;
 for(int i=1;i<count;i++)if(wcscmp(args[i],L"--mod")==0){if(found||i+1>=count)throw std::runtime_error("Expected one --mod directory");found=true;std::filesystem::path p(args[++i]);directory=p.string();}
 return found?std::make_unique<Host>(directory):nullptr;
}''')
edit('Game/CMappedResource.h','#include <unordered_map>','#include <unordered_map>\n#include <vector>\n#include <memory>')
edit('Game/CMappedResource.h','    CMappedResource();','''    explicit CMappedResource(bool raw=false);
    bool rawResource=false,ignoreOriginal=false;
    std::vector<std::unique_ptr<CMappedResource>> overlays;
    void OpenOverlays(LPCSTR,bool);
    LPBYTE OverlayFile(LPCSTR,LPDWORD);
    virtual void GetFileList(CStringArray&,LPCSTR mask=NULL);
    virtual BOOL CheckFileExist(LPCSTR name){return IsFileExist(name);}''')
edit('Game/CResourceBase.h','    void GetFileList(','    virtual void GetFileList(')
edit('Game/CResourceBase.h','    BOOL CheckFileExist(','    virtual BOOL CheckFileExist(')
edit('Game/CMappedResource.cpp','#include "CMappedResource.h"','#include "CMappedResource.h"\n#include "ModRuntime.h"\n#include "ModPackage.h"\n#include <set>\n#include <filesystem>')
edit('Game/CMappedResource.cpp','CMappedResource::CMappedResource()','CMappedResource::CMappedResource(bool raw):rawResource(raw)')
edit('Game/CMappedResource.cpp','BOOL CMappedResource::OpenResource(LPCSTR pszFileName, BOOL _bWriteAccess)','''void CMappedResource::OpenOverlays(LPCSTR name,bool write){
 overlays.clear();ignoreOriginal=false;if(rawResource||write||!ModRuntime::Resources())return;
 std::vector<std::string> paths;ModRuntime::Resources()->ResourcePlan(name,paths,ignoreOriginal);
 for(const auto&path:paths){if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Missing mod resource: "+path);auto child=std::make_unique<CMappedResource>(true);if(!child->OpenResource(path.c_str()))throw std::runtime_error("Cannot open mod resource: "+path);overlays.push_back(std::move(child));}
}
LPBYTE CMappedResource::OverlayFile(LPCSTR name,LPDWORD size){for(auto&child:overlays)if(auto*p=child->GetFile(name,size))return p;return nullptr;}
void CMappedResource::GetFileList(CStringArray&names,LPCSTR mask){
 std::set<std::string> seen;CStringArray local;
 auto append=[&](){for(int i=0;i<local.GetSize();i++){std::string key=(LPCSTR)local[i];for(char&c:key)if(c>='A'&&c<='Z')c+=32;if(seen.insert(key).second)names.Add(local[i]);}local.RemoveAll();};
 for(auto&child:overlays){child->GetFileList(local,mask);append();}if(!ignoreOriginal){CResourceBase::GetFileList(local,mask);append();}
}
BOOL CMappedResource::OpenResource(LPCSTR pszFileName, BOOL _bWriteAccess)''')
edit('Game/CMappedResource.cpp','    bWriteAccess = _bWriteAccess;','    bWriteAccess = _bWriteAccess;\n    OpenOverlays(pszFileName,_bWriteAccess);if(ignoreOriginal)return TRUE;')
edit('Game/CMappedResource.cpp','void CMappedResource::CloseResource()\n{','void CMappedResource::CloseResource()\n{\n    overlays.clear();ignoreOriginal=false;')
edit('Game/CMappedResource.cpp','bool CMappedResource::IsFileExist(LPCSTR pszFileName)\n{','bool CMappedResource::IsFileExist(LPCSTR pszFileName)\n{\n    for(auto&child:overlays)if(child->IsFileExist(pszFileName))return true;if(ignoreOriginal)return false;')
edit('Game/CMappedResource.cpp','LPBYTE CMappedResource::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)\n{','LPBYTE CMappedResource::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)\n{\n    if(auto*p=OverlayFile(pszFileName,pnDataSize))return p;if(ignoreOriginal)return nullptr;')
edit('Game/CMappedResource.cpp','bool CMappedResource::CloseFile(LPBYTE pFileImage)\n{','bool CMappedResource::CloseFile(LPBYTE pFileImage)\n{\n    for(auto&child:overlays)if(child->CloseFile(pFileImage))return true;')
edit('Game/CMappedResource.cpp','void CMappedResource::CloseAllFiles()\n{','void CMappedResource::CloseAllFiles()\n{\n    for(auto&child:overlays)child->CloseAllFiles();')
edit('Game/CMappedResource.cpp','BOOL CWin32MappedResource::OpenResource(LPCSTR pszFileName, BOOL bWriteAccess)\n{','BOOL CWin32MappedResource::OpenResource(LPCSTR pszFileName, BOOL bWriteAccess)\n{\n    CloseResource();OpenOverlays(pszFileName,bWriteAccess);if(ignoreOriginal)return TRUE;')
edit('Game/CMappedResource.cpp','void CWin32MappedResource::CloseResource()\n{','void CWin32MappedResource::CloseResource()\n{\n    overlays.clear();ignoreOriginal=false;')
edit('Game/CMappedResource.cpp','LPBYTE CWin32MappedResource::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)\n{','LPBYTE CWin32MappedResource::GetFile(LPCSTR pszFileName, LPDWORD pnDataSize)\n{\n    if(auto*p=OverlayFile(pszFileName,pnDataSize))return p;if(ignoreOriginal)return nullptr;')
edit('Game/CMappedResource.cpp','bool CWin32MappedResource::CloseFile(LPBYTE pFileImage)\n{','bool CWin32MappedResource::CloseFile(LPBYTE pFileImage)\n{\n    for(auto&child:overlays)if(child->CloseFile(pFileImage))return true;')
edit('Game/CMappedResource.cpp','void CWin32MappedResource::CloseAllFiles()\n{','void CWin32MappedResource::CloseAllFiles()\n{\n    for(auto&child:overlays)child->CloseAllFiles();')
edit('Game/FileRegistry.cpp','#include "FileRegistry.h"','#include "FileRegistry.h"\n#include "ModRuntime.h"')
edit('Game/FileRegistry.cpp','    if (!cMappedFile.Open(pszFileName))','    std::string resolved=ModRuntime::ConfigPath(pszFileName);\n    if (!cMappedFile.Open(resolved.c_str()))')
edit('Game/SaveLoader.h','#pragma once','#pragma once\n#include "ModRuntime.h"')
edit('Game/SaveLoader.h','bIsOk = file.Open(pszFName, CFile::modeRead) != 0;','std::string resolved=ModRuntime::MapPath(pszFName);\n        bIsOk = file.Open(resolved.c_str(), CFile::modeRead) != 0;')
edit('Game/GameMain.cpp','#include "ModPackage.h"','#include "ModPackage.h"\n#include "ModRuntime.h"')
edit('Game/GameMain.cpp','    OutputDebugStringA("Hello!\\n");','''    std::unique_ptr<ModRuntime::Host> modHost;
    try { modHost=ModRuntime::Host::FromCommandLine(); }
    catch(const std::exception&e){MessageBoxA(nullptr,e.what(),"Mod startup failed",MB_OK|MB_ICONERROR);return BAD_RESULT;}
    OutputDebugStringA("Hello!\\n");''')
