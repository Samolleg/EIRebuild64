#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <atlbase.h>
#include <msxml6.h>
#include <wincrypt.h>
#include "SettingsProfile.h"
#include <filesystem>
#include <mutex>
#include <map>
#include <set>
#include <stdexcept>
#include <limits>
#include <cstring>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")

namespace SettingsProfile {
namespace {
namespace fs = std::filesystem;
void Check(HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("ReStarter XML operation failed"); }
struct Com {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Com() { if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) Check(hr); }
    ~Com() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
std::wstring Fold(std::wstring s) {
    for (auto& c : s) { if (c == L'/') c = L'\\'; c = towlower(c); }
    return s;
}
std::wstring Attr(IXMLDOMElement* e, const wchar_t* name) {
    CComVariant v; Check(e->getAttribute(CComBSTR(name), &v));
    if (v.vt != VT_BSTR) throw std::runtime_error("Missing ReStarter value attribute");
    return std::wstring(v.bstrVal, SysStringLen(v.bstrVal));
}
using Doc = CComPtr<IXMLDOMDocument2>;
Doc Load(const fs::path& path) {
    Doc doc; Check(doc.CoCreateInstance(__uuidof(DOMDocument60)));
    Check(doc->put_async(VARIANT_FALSE)); Check(doc->put_validateOnParse(VARIANT_FALSE));
    Check(doc->put_resolveExternals(VARIANT_FALSE)); Check(doc->put_preserveWhiteSpace(VARIANT_TRUE));
    Check(doc->setProperty(CComBSTR(L"ProhibitDTD"), CComVariant(true)));
    VARIANT_BOOL ok = VARIANT_FALSE;
    if (fs::exists(path)) Check(doc->load(CComVariant(path.c_str()), &ok));
    else Check(doc->loadXML(CComBSTR(L"<?xml version=\"1.0\" encoding=\"utf-8\"?><profile version=\"1\"/>"), &ok));
    if (!ok) throw std::runtime_error("Cannot parse ReStarter profile (existing file is preserved)");
    CComPtr<IXMLDOMElement> root; Check(doc->get_documentElement(&root));
    CComBSTR tag; if (!root) throw std::runtime_error("Missing ReStarter profile root");
    Check(root->get_tagName(&tag));
    if (std::wstring(tag, tag.Length()) != L"profile" || Attr(root,L"version") != L"1")
        throw std::runtime_error("Unsupported ReStarter profile version");
    return doc;
}
struct Value { std::wstring section, name, text; int kind; };
using Key = std::pair<std::wstring,std::wstring>;
using Values = std::map<Key,Value>;
Values Read(Doc& doc) {
    Values out; CComPtr<IXMLDOMNodeList> nodes; Check(doc->selectNodes(CComBSTR(L"/profile/value"), &nodes));
    long count = 0; Check(nodes->get_length(&count));
    for (long i=0; i<count; ++i) {
        CComPtr<IXMLDOMNode> n; Check(nodes->get_item(i,&n)); CComQIPtr<IXMLDOMElement> e(n);
        Value v; v.section=Attr(e,L"section"); v.name=Attr(e,L"name");
        auto kind=Attr(e,L"kind"); size_t end=0; v.kind=std::stoi(kind,&end);
        if (end!=kind.size()) throw std::runtime_error("Invalid ReStarter kind");
        CComBSTR text; Check(e->get_text(&text)); v.text=std::wstring(text, text.Length());
        if (!out.emplace(Key{Fold(v.section),Fold(v.name)},v).second)
            throw std::runtime_error("Duplicate ReStarter setting");
    }
    return out;
}
std::vector<char> Unbase64(const std::wstring& s) {
    if(s.empty()) return {};
    DWORD n=0;
    if(!CryptStringToBinaryW(s.c_str(),DWORD(s.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,nullptr,&n,nullptr,nullptr))
        throw std::runtime_error("Invalid ReStarter base64");
    std::vector<char> data(n);
    if(!CryptStringToBinaryW(s.c_str(),DWORD(s.size()),CRYPT_STRING_BASE64|CRYPT_STRING_STRICT,
        reinterpret_cast<BYTE*>(data.data()),&n,nullptr,nullptr)) throw std::runtime_error("Invalid ReStarter base64");
    data.resize(n); return data;
}
std::wstring Base64(const std::vector<char>& data) {
    if(data.empty()) return {};
    DWORD n=0; const auto* bytes=reinterpret_cast<const BYTE*>(data.data());
    if(!CryptBinaryToStringW(bytes,DWORD(data.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,nullptr,&n))
        throw std::runtime_error("ReStarter base64 encoding failed");
    std::wstring s(n,L'\0');
    if(!CryptBinaryToStringW(bytes,DWORD(data.size()),CRYPT_STRING_BASE64|CRYPT_STRING_NOCRLF,&s[0],&n))
        throw std::runtime_error("ReStarter base64 encoding failed");
    s.resize(n && s[n-1]==L'\0'?n-1:n); return s;
}
std::vector<char> Bytes(const std::wstring& s, bool terminate) {
    std::vector<char> b((s.size()+(terminate?1:0))*sizeof(wchar_t));
    if(!b.empty()) memcpy(b.data(),s.c_str(),b.size()); return b;
}
bool Decode(const Value& v,std::vector<char>& data,uint32_t& type) {
    try {
        switch(v.kind) {
        case 1: case 2: type=v.kind; data=Bytes(v.text,true); return true;
        case -1: case 3: type=v.kind==-1?REG_NONE:REG_BINARY; data=Unbase64(v.text); return true;
        case 7:
            type=REG_MULTI_SZ; data=Unbase64(v.text); if(data.size()%2) return false;
            data.resize(data.size()+4,0); return true; // XML stores UTF-16 strings joined by NUL.
        case 4: case 11: {
            size_t end=0; auto n=std::stoll(v.text,&end);
            if(end!=v.text.size() || (v.kind==4 && (n<INT32_MIN || n>UINT32_MAX))) return false;
            type=v.kind; auto u=static_cast<uint64_t>(n); data.resize(v.kind==4?4:8);
            memcpy(data.data(),&u,data.size()); return true;
        }
        default: return false;
        }
    } catch(const std::exception&) { return false; }
}
Value Encode(const std::wstring& section,const std::wstring& name,const std::vector<char>& data,uint32_t type) {
    Value v{section,name,L"",int(type)};
    switch(type) {
    case REG_NONE: v.kind=-1; v.text=Base64(data); break;
    case REG_BINARY: v.text=Base64(data); break;
    case REG_DWORD: case REG_QWORD: {
        if(data.size()!=static_cast<size_t>(type==REG_DWORD?4:8)) throw std::runtime_error("Invalid settings integer size");
        int64_t n=0; memcpy(&n,data.data(),data.size());
        v.text=type==REG_DWORD?std::to_wstring(static_cast<int32_t>(n)):std::to_wstring(n); break;
    }
    case REG_SZ: case REG_EXPAND_SZ: case REG_MULTI_SZ: {
        if(data.size()%2) throw std::runtime_error("Invalid settings string size");
        auto b=data;
        // RegSetMultiString uses one terminator per entry; also accept the Win32 double terminator.
        while(b.size()>=2 && !b[b.size()-1] && !b[b.size()-2]) b.resize(b.size()-2);
        if(type==REG_MULTI_SZ) v.text=Base64(b);
        else { v.text.resize(b.size()/2); if(!b.empty()) memcpy(&v.text[0],b.data(),b.size()); }
        break;
    }
    default: throw std::runtime_error("Unsupported settings write type");
    }
    return v;
}
struct State {
    std::mutex mutex;
    bool initialized=false;
    fs::path target, source;
    std::string fallback;
    Values values;
    void Init() {
        if(initialized) return;
        wchar_t exe[32768]={}; auto n=GetModuleFileNameW(nullptr,exe,_countof(exe));
        if(!n || n==_countof(exe)) throw std::runtime_error("Cannot resolve profile location");
        fs::path root=fs::path(exe).parent_path().parent_path();
        target=root/L"restarter64-settings.xml";
        int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        if(!argv) throw std::runtime_error("Cannot parse -profile");
        struct Free { LPWSTR* p; ~Free(){LocalFree(p);} } free{argv};
        bool explicitPath=false;
        for(int i=1;i<argc;++i) if(wcscmp(argv[i],L"-profile")==0) {
            if(explicitPath || i+1>=argc || !argv[i+1][0] || argv[i+1][0]==L'-')
                throw std::runtime_error("Expected one -profile path");
            target=fs::absolute(argv[++i]); explicitPath=true;
        }
        const auto filename = Fold(target.filename().wstring());
        if (filename == L"restarter-settings.xml" || filename == L"restarter-defaults.xml")
            throw std::runtime_error("EIRebuild64 cannot write an x86 profile");
        source=target; fallback=explicitPath?"explicit_profile":"profile";
        if(!fs::exists(source)) {
            source=root/L"restarter64-defaults.xml";
            fallback=fs::exists(source)?"restarter64-defaults.xml":"embedded_engine_defaults";
        }
        Com com; auto doc=Load(source); values=Read(doc);
        initialized=true;
    }
    bool Save(Doc& doc) {
        // Fail rather than overwrite an unparseable file; caller reloads the latest profile before mutation.
        CComBSTR xml; Check(doc->get_xml(&xml));
        int size=WideCharToMultiByte(CP_UTF8,0,xml,xml.Length(),nullptr,0,nullptr,nullptr);
        if(!size) return false;
        std::string bytes(size,'\0'); WideCharToMultiByte(CP_UTF8,0,xml,xml.Length(),&bytes[0],size,nullptr,nullptr);
        auto tmp=target; tmp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        HANDLE file=CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) return false;
        DWORD written=0; bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr) && written==bytes.size();
        if(ok) ok=FlushFileBuffers(file)!=FALSE; CloseHandle(file);
        if(ok) {
            // Readers/scanners can briefly deny rename on Windows. Never fall back to truncating the destination.
            for(unsigned attempt=0;attempt<50;++attempt) {
                ok=MoveFileExW(tmp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
                if(ok) break;
                const DWORD error=GetLastError();
                if(error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION && error!=ERROR_ACCESS_DENIED) break;
                Sleep(4);
            }
        }
        if(!ok) { DeleteFileW(tmp.c_str()); return false; }
        values=Read(doc); source=target; fallback="profile"; return true;
    }
};
State& Store() { static State s; return s; }
std::wstring Join(const std::wstring& a,const std::wstring& b) { return a.empty()?b:b.empty()?a:a+L"\\"+b; }
}
std::wstring Wide(const char* s) {
    if(!s || !*s) return {}; int n=MultiByteToWideChar(CP_ACP,0,s,-1,nullptr,0);
    std::wstring out(n,L'\0'); MultiByteToWideChar(CP_ACP,0,s,-1,&out[0],n); out.resize(n-1); return out;
}
std::string Ansi(const std::wstring& s) {
    if(s.empty()) return {}; int n=WideCharToMultiByte(CP_ACP,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(n,'\0'); WideCharToMultiByte(CP_ACP,0,s.data(),int(s.size()),&out[0],n,nullptr,nullptr); return out;
}
std::wstring Section(const std::wstring& key) {
    const std::wstring prefixes[]={L"Software\\Gipat.ru\\EI_Starter",L"Software\\Nival Interactive\\EvilIslands"};
    const auto folded=Fold(key);
    for(unsigned i=0;i<2;++i) {
        auto p=Fold(prefixes[i]);
        if(folded==p) return i?L"EvilIslands":L"";
        if(folded.compare(0,p.size()+1,p+L"\\")==0)
            return Join(i?L"EvilIslands":L"",key.substr(p.size()+1));
    }
    return key;
}
void Initialize() { auto& s=Store(); std::lock_guard<std::mutex> lock(s.mutex); s.Init(); }
std::wstring Path() { auto& s=Store(); std::lock_guard<std::mutex> lock(s.mutex); s.Init(); return s.target.wstring(); }
std::string Summary() {
    auto& s=Store(); std::lock_guard<std::mutex> lock(s.mutex); s.Init();
    return "path=\""+Ansi(s.source.wstring())+"\" values="+std::to_string(s.values.size())+
        " defaults=engine_per_missing_value fallback="+s.fallback+" write_path=\""+Ansi(s.target.wstring())+"\"";
}
bool GetRaw(const std::wstring& section,const std::wstring& name,std::vector<char>& data,uint32_t& type) {
    auto& s=Store(); std::lock_guard<std::mutex> lock(s.mutex); s.Init();
    auto v=s.values.find({Fold(section),Fold(name)}); return v!=s.values.end() && Decode(v->second,data,type);
}
bool SetRaw(const std::wstring& section,const std::wstring& name,const std::vector<char>& data,uint32_t type) {
    auto& s=Store(); std::lock_guard<std::mutex> lock(s.mutex); s.Init();
    try {
        Com com; auto doc=Load(fs::exists(s.target)?s.target:s.source); Read(doc);
        Value v=Encode(section,name,data,type); CComPtr<IXMLDOMElement> root; Check(doc->get_documentElement(&root));
        CComPtr<IXMLDOMNodeList> nodes; Check(doc->selectNodes(CComBSTR(L"/profile/value"),&nodes));
        long count=0; Check(nodes->get_length(&count)); CComPtr<IXMLDOMElement> element;
        for(long i=0;i<count;++i) {
            CComPtr<IXMLDOMNode> n; Check(nodes->get_item(i,&n)); CComQIPtr<IXMLDOMElement> e(n);
            if(Fold(Attr(e,L"section"))==Fold(section) && Fold(Attr(e,L"name"))==Fold(name)) { element=e; break; }
        }
        if(!element) {
            Check(doc->createElement(CComBSTR(L"value"),&element));
            Check(element->setAttribute(CComBSTR(L"section"),CComVariant(section.c_str())));
            Check(element->setAttribute(CComBSTR(L"name"),CComVariant(name.c_str())));
            CComPtr<IXMLDOMNode> added; Check(root->appendChild(element,&added));
        }
        Check(element->setAttribute(CComBSTR(L"kind"),CComVariant(std::to_wstring(v.kind).c_str())));
        Check(element->put_text(CComBSTR(v.text.c_str()))); return s.Save(doc);
    } catch(const std::exception&) { return false; }
}
uint32_t Dword(const std::wstring& section,const std::wstring& name,uint32_t fallback) {
    std::vector<char> data; uint32_t type=0,value=fallback;
    if(GetRaw(section,name,data,type) && type==REG_DWORD && data.size()==4) memcpy(&value,data.data(),4); return value;
}
bool SetDword(const std::wstring& section,const std::wstring& name,uint32_t value) {
    std::vector<char> data(4); memcpy(data.data(),&value,4); return SetRaw(section,name,data,REG_DWORD);
}
std::wstring String(const std::wstring& section,const std::wstring& name,const std::wstring& fallback) {
    std::vector<char> data; uint32_t type=0;
    if(!GetRaw(section,name,data,type) || (type!=REG_SZ && type!=REG_EXPAND_SZ) || data.size()%2) return fallback;
    std::wstring text(data.size()/2,L'\0'); if(!data.empty()) memcpy(&text[0],data.data(),data.size());
    while(!text.empty() && !text.back()) text.pop_back(); return text;
}
std::vector<std::wstring> MultiString(const std::wstring& section,const std::wstring& name) {
    std::vector<char> data; uint32_t type=0; std::vector<std::wstring> out;
    if(!GetRaw(section,name,data,type) || type!=REG_MULTI_SZ || data.size()%2) return out;
    std::wstring text(data.size()/2,L'\0'); if(!data.empty()) memcpy(&text[0],data.data(),data.size());
    for(size_t i=0;i<text.size();) { size_t end=text.find(L'\0',i); if(end==std::wstring::npos) break;
        if(end>i) out.push_back(text.substr(i,end-i)); i=end+1; } return out;
}
std::vector<std::wstring> Sections(const std::wstring& base) {
    auto& s=Store(); std::lock_guard<std::mutex> lock(s.mutex); s.Init(); std::set<std::wstring> out;
    auto prefix=Fold(base); if(!prefix.empty()) prefix+=L'\\';
    for(const auto& p:s.values) if(p.first.first.compare(0,prefix.size(),prefix)==0) {
        auto rest=p.second.section.substr(prefix.size()); auto part=rest.substr(0,rest.find(L'\\')); if(!part.empty()) out.insert(part);
    } return {out.begin(),out.end()};
}
std::vector<std::wstring> Entries(const std::wstring& section) {
    auto& s=Store(); std::lock_guard<std::mutex> lock(s.mutex); s.Init(); std::vector<std::wstring> out;
    for(const auto& p:s.values) if(p.first.first==Fold(section)) out.push_back(p.second.name); return out;
}
bool Delete(const std::wstring& section,const std::wstring* name) {
    auto& s=Store(); std::lock_guard<std::mutex> lock(s.mutex); s.Init();
    try {
        Com com; auto doc=Load(fs::exists(s.target)?s.target:s.source); Read(doc);
        CComPtr<IXMLDOMElement> root; Check(doc->get_documentElement(&root));
        CComPtr<IXMLDOMNodeList> nodes; Check(doc->selectNodes(CComBSTR(L"/profile/value"),&nodes)); long count=0; Check(nodes->get_length(&count));
        bool found=false; auto key=Fold(section);
        for(long i=count-1;i>=0;--i) {
            CComPtr<IXMLDOMNode> n; Check(nodes->get_item(i,&n)); CComQIPtr<IXMLDOMElement> e(n); auto sec=Fold(Attr(e,L"section"));
            if(sec==key && (!name || Fold(Attr(e,L"name"))==Fold(*name))) {
                CComPtr<IXMLDOMNode> removed; Check(root->removeChild(n,&removed)); found=true;
            } else if(!name && sec.compare(0,key.size()+1,key+L"\\")==0) return false; // RegDeleteKey fails for subkeys.
        }
        return found && s.Save(doc);
    } catch(const std::exception&) { return false; }
}
}
