#include "WorldBackgroundFiles.h"
#include "../MapResources.h"
#include <fstream>
#include <set>
#include <stdexcept>
#include <windows.h>

namespace WorldBackground {
namespace {
void Check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
uint16_t U16(const Blob& b,size_t p){Check(p<=b.size() && 2<=b.size()-p,"Truncated RES metadata");return uint16_t(b[p]|(uint16_t(b[p+1])<<8));}
uint32_t U32(const Blob& b,size_t p){return U16(b,p)|(uint32_t(U16(b,p+2))<<16);}
uint64_t Length(std::ifstream& f){f.seekg(0,std::ios::end);auto size=f.tellg();Check(size>=0,"Cannot stat resource");return uint64_t(size);}
Blob Range(std::ifstream& f,uint64_t offset,size_t count,uint64_t length){
    Check(offset<=length && count<=length-offset,"Truncated resource range");Blob b(count);
    f.seekg(std::streamoff(offset));if(count)f.read(reinterpret_cast<char*>(b.data()),std::streamsize(count));
    Check(bool(f),"Cannot read resource");return b;
}
bool Exists(const std::wstring& path){
    DWORD attr=GetFileAttributesW(path.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES){DWORD e=GetLastError();Check(e==ERROR_FILE_NOT_FOUND || e==ERROR_PATH_NOT_FOUND,"Resource path inaccessible");return false;}
    Check(!(attr&FILE_ATTRIBUTE_DIRECTORY),"Expected resource file");return true;
}
std::wstring SafeName(const std::string& value){
    std::string name=Key(value);Check(!name.empty()&&name.size()<=256&&name!="."&&name!="..","Invalid resource name");
    for(unsigned char c:name)Check((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.',"Unsupported resource name");
    return std::wstring(name.begin(),name.end());
}
}
Blob ReadFileBounded(const std::wstring& path,size_t limit){
    std::ifstream f(path,std::ios::binary);Check(bool(f),"Cannot open resource");uint64_t n=Length(f);
    Check(n<=limit,"Resource byte budget exceeded");return Range(f,0,size_t(n),n);
}
bool ReadArchiveMember(const std::wstring& archive,const std::string& requested,Blob& output,size_t limit){
    output.clear();std::ifstream f(archive,std::ios::binary);Check(bool(f),"Cannot open RES");uint64_t length=Length(f);
    Blob h=Range(f,0,16,length);Check(U32(h,0)==0x019ce23c,"Invalid RES signature");
    uint32_t count=U32(h,4),table=U32(h,8),namesSize=U32(h,12);
    Check(count && count<=65536 && table>=16 && namesSize<=4u*1024u*1024u,"Invalid RES metadata budget");
    size_t tableBytes=size_t(count)*22;Blob index=Range(f,table,tableBytes+namesSize,length);
    std::set<std::string> names;bool found=false;uint32_t targetOffset=0,targetSize=0;
    for(uint32_t i=0;i<count;++i){
        size_t p=size_t(i)*22;uint32_t size=U32(index,p+4),offset=U32(index,p+8),no=U32(index,p+18);uint16_t nl=U16(index,p+16);
        Check(nl && no<=namesSize && nl<=namesSize-no && offset>=16 && offset<=table && size<=table-offset,"Invalid RES entry");
        std::string name(reinterpret_cast<const char*>(index.data()+tableBytes+no),nl);
        Check(name.find('\0')==std::string::npos && names.insert(Key(name)).second,"Invalid or duplicate RES name");
        if(Key(name)==Key(requested)){found=true;targetOffset=offset;targetSize=size;}
    }
    if(!found)return false;
    Check(targetSize<=limit,"RES member byte budget exceeded");output=Range(f,targetOffset,targetSize,length);return true;
}
Blob ProfileFiles::LayoutMap(const std::string& requestedProfile,const std::string& path,bool mob)const{
    Check(Key(requestedProfile)==Key(profile)&&!profile.empty(),"Layout resource profile mismatch");
    Check(!path.empty()&&path.size()<32768&&path.find('\0')==std::string::npos,"Invalid layout path");
    const auto relative=MapResources::Key(path);
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative.data(),int(relative.size()),nullptr,0);
    Check(count>0,"Invalid UTF8 layout path");std::wstring wide(size_t(count),L'\0');
    Check(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,relative.data(),int(relative.size()),&wide[0],count)==count,"Invalid UTF8 layout path");
    const auto suffix=mob?".mob":".mpr";
    Check(relative.size()>=4&&relative.substr(relative.size()-4)==suffix,"Wrong layout map extension");
    Check(mapRoots.size()==1,"Layout requires one Maps64 provider");
    return ReadFileBounded(mapRoots.front()+L"/"+wide,(mob?64u:128u)*1024u*1024u);
}
Blob ProfileFiles::Mpr(const Identity& identity)const{
    Check(Key(identity.profile)==Key(profile)&&!Key(profile).empty(),"Resource profile mismatch");
    std::wstring name=SafeName(identity.mpr);if(name.size()<4 || name.substr(name.size()-4)!=L".mpr")name+=L".mpr";
    for(const auto& root:mapRoots){std::wstring path=root+L"/"+name;if(Exists(path))return ReadFileBounded(path,128u*1024u*1024u);}
    throw std::runtime_error("Missing neighbor MPR");
}
Blob ProfileFiles::Mob(const std::string& requestedProfile,const std::string& member)const{
    Check(Key(requestedProfile)==Key(profile)&&!Key(profile).empty(),"MOB profile mismatch");
    std::wstring name=SafeName(member);if(name.size()<4||name.substr(name.size()-4)!=L".mob")name+=L".mob";
    for(const auto& root:mapRoots){const auto path=root+L"/"+name;if(Exists(path))return ReadFileBounded(path,64u*1024u*1024u);}
    throw std::runtime_error("Missing neighbor decoration MOB");
}
Blob ProfileFiles::MaterialFile(const std::string& requestedProfile,const std::string& member)const{
    Check(Key(requestedProfile)==Key(profile)&&!Key(profile).empty(),"Material profile mismatch");SafeName(member);
    for(const auto& archive:materialArchives){Blob result;if(Exists(archive)&&ReadArchiveMember(archive,member,result,4096))return result;}
    return {}; // Native material manager uses default diffuse0.3 when member is absent.
}
Blob ProfileFiles::Figure(const std::string& requestedProfile,const std::string& member)const{
    Check(Key(requestedProfile)==Key(profile)&&!Key(profile).empty(),"Figure profile mismatch");SafeName(member);
    for(const auto& archive:figureArchives){Blob result;if(Exists(archive)&&ReadArchiveMember(archive,member,result,32u*1024u*1024u))return result;}
    throw std::runtime_error("Missing neighbor figure");
}
Blob ProfileFiles::Texture(const std::string& requestedProfile,const std::string& member)const{
    Check(Key(requestedProfile)==Key(profile)&&!Key(profile).empty(),"Texture profile mismatch");SafeName(member);
    for(const auto& archive:textureArchives){Blob result;if(Exists(archive)&&ReadArchiveMember(archive,member,result,64u*1024u*1024u))return result;}
    throw std::runtime_error("Missing neighbor texture");
}
}
