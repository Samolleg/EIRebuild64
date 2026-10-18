#include "StdAfx.h"
// Ported from addon d3b0896: schema 2 membership against the live item DB.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include "ItemApproachCatalog.h"
#include "ItemDatabase.h"
#include "ModRuntime.h"
#include "ModPackage.h"
#include <cstdint>

// EIClasses leaves pack(4) active. Own STL-bearing types use normal packing.
#pragma pack(push, 8)
namespace ItemApproachCatalog {
namespace {


// Bounded JSON reader shared in origin with WorldBackgroundData's reader.
// No script execution, substring model matching, or prefix classification.
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Json {
    char type = 0;
    std::string text;
    double number = -1;
    std::map<std::string, Json> fields;
    std::vector<Json> items;
    const Json& At(const char* key) const {
        static const Json empty;
        auto it = fields.find(key);
        return it == fields.end() ? empty : it->second;
    }
    bool Has(const char* key) const { return fields.count(key) != 0; }
    bool Is(const char* value) const { return type == 's' && text == value; }
    int Int() const { return type == 'n' && number >= 0 && number <= 2147483647 && floor(number) == number ? int(number) : -1; }
};
class Parser {
    const std::string& s;
    size_t p = 0, nodes = 0;
    void Space() { while (p < s.size() && (s[p]==' ' || s[p]=='\t' || s[p]=='\r' || s[p]=='\n')) ++p; }
    char Take() { Require(p < s.size(), "Truncated JSON"); return s[p++]; }
    unsigned Hex() {
        unsigned v = 0;
        for (int i=0;i<4;++i) {
            char c=Take(); unsigned d;
            if (c>='0' && c<='9') d=c-'0';
            else if (c>='a' && c<='f') d=c-'a'+10;
            else if (c>='A' && c<='F') d=c-'A'+10;
            else throw std::runtime_error("Invalid JSON escape");
            v=v*16+d;
        }
        return v;
    }
    static void Utf8(std::string& r, unsigned c) {
        if (c<128) r+=char(c);
        else if (c<2048) { r+=char(192|(c>>6)); r+=char(128|(c&63)); }
        else if (c<65536) { r+=char(224|(c>>12)); r+=char(128|((c>>6)&63)); r+=char(128|(c&63)); }
        else { r+=char(240|(c>>18)); r+=char(128|((c>>12)&63)); r+=char(128|((c>>6)&63)); r+=char(128|(c&63)); }
    }
    std::string String() {
        Require(Take()=='"', "Expected JSON string"); std::string r;
        for (;;) {
            unsigned char c=static_cast<unsigned char>(Take());
            if (c=='"') return r;
            Require(c>=32, "Control in JSON string");
            if (c!='\\') r+=char(c);
            else {
                char e=Take();
                switch(e) {
                case '"': case '\\': case '/': r+=e; break;
                case 'b': r+='\b'; break; case 'f': r+='\f'; break;
                case 'n': r+='\n'; break; case 'r': r+='\r'; break; case 't': r+='\t'; break;
                case 'u': {
                    unsigned v=Hex();
                    if (v>=0xd800 && v<=0xdbff) {
                        Require(Take()=='\\' && Take()=='u', "Missing surrogate");
                        unsigned low=Hex(); Require(low>=0xdc00 && low<=0xdfff, "Invalid surrogate");
                        v=0x10000+((v-0xd800)<<10)+(low-0xdc00);
                    } else Require(v<0xdc00 || v>0xdfff, "Unpaired surrogate");
                    Utf8(r,v); break;
                }
                default: throw std::runtime_error("Invalid JSON escape");
                }
            }
            Require(r.size()<=65536, "JSON string budget exceeded");
        }
    }
    bool Digit() const { return p<s.size() && s[p]>='0' && s[p]<='9'; }
    Json Value(unsigned depth) {
        Require(depth<=32 && ++nodes<=500000, "JSON complexity budget exceeded");
        Space(); Require(p<s.size(), "Truncated JSON"); Json v;
        char c=s[p];
        if (c=='"') { v.type='s'; v.text=String(); return v; }
        if (c=='{' || c=='[') {
            v.type=Take(); Space(); char end=c=='{'?'}':']';
            if (p<s.size() && s[p]==end) { ++p; return v; }
            for (;;) {
                Space(); std::string key;
                if (c=='{') { key=String(); Space(); Require(Take()==':', "Expected colon"); }
                Json child=Value(depth+1);
                if (c=='{') Require(v.fields.emplace(key,std::move(child)).second, "Duplicate JSON key");
                else v.items.push_back(std::move(child));
                Space(); char next=Take(); if (next==end) return v;
                Require(next==',', "Expected separator");
            }
        }
        for (const char* word : {"true", "false", "null"}) {
            size_t n=std::strlen(word);
            if (s.compare(p,n,word)==0) { p+=n; v.type=word[0]; return v; }
        }
        size_t start=p;
        if (s[p]=='-') ++p;
        Require(Digit(), "Invalid JSON number");
        if (s[p]=='0') ++p; else while (Digit()) ++p;
        if (p<s.size() && s[p]=='.') { ++p; Require(Digit(), "Invalid fraction"); while (Digit()) ++p; }
        if (p<s.size() && (s[p]=='e' || s[p]=='E')) {
            ++p; if (p<s.size() && (s[p]=='+' || s[p]=='-')) ++p;
            Require(Digit(), "Invalid exponent"); while (Digit()) ++p;
        }
        std::istringstream input(s.substr(start,p-start)); input.imbue(std::locale::classic());
        input>>v.number; Require(!input.fail() && std::isfinite(v.number), "Nonfinite JSON number");
        v.type='n'; return v;
    }
public:
    explicit Parser(const std::string& input):s(input) {}
    Json Parse() {
        Require(s.size()<=8u*1024u*1024u, "JSON byte budget exceeded");
        if (s.compare(0,3,"\xef\xbb\xbf")==0) p=3;
        Json v=Value(0); Space(); Require(p==s.size(), "Trailing JSON data"); return v;
    }
};

std::string Lower(std::string s) { for (char& c:s) if(c>='A'&&c<='Z')c+=32; return s; }
bool Leaf(const std::string& s) {
    if(s.empty() || s.size()>256 || s=="." || s=="..")return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'||c=='-'))return false;
    return true;
}
std::string Directory(const std::string& s) { const auto p=s.find_last_of("/\\"); return p==std::string::npos?"":s.substr(0,p+1); }
std::wstring Wide(const std::string& s, UINT page=CP_ACP) {
    if(s.empty())return {};
    const int n=MultiByteToWideChar(page,0,s.data(),int(s.size()),nullptr,0);
    Require(n>0,"Invalid path encoding");std::wstring result(size_t(n),L'\0');
    MultiByteToWideChar(page,0,s.data(),int(s.size()),&result[0],n);return result;
}
bool Same(const char* live,const std::string& utf8) {
    if(!live || utf8.find('\0')!=std::string::npos)return false;
    bool ascii=true;
    for(unsigned char c:utf8)if(c>=128)ascii=false;
    if(ascii)return _stricmp(live,utf8.c_str())==0;
    // JSON is UTF-8; CString in the original game uses the Windows code page.
    return CompareStringOrdinal(Wide(live).c_str(),-1,Wide(utf8,CP_UTF8).c_str(),-1,TRUE)==CSTR_EQUAL;
}
std::vector<unsigned char> Read(std::ifstream& f, size_t size) {
    std::vector<unsigned char> b(size);if(size)f.read(reinterpret_cast<char*>(b.data()),std::streamsize(size));
    Require(bool(f),"Truncated catalog/RES");return b;
}
uint32_t ReadCatalogU32(const std::vector<unsigned char>& b,size_t p) {
    Require(p<=b.size() && 4<=b.size()-p,"Truncated RES integer");
    return uint32_t(b[p])|(uint32_t(b[p+1])<<8)|(uint32_t(b[p+2])<<16)|(uint32_t(b[p+3])<<24);
}
// Only an index is needed. Never load model/texture payload on a path request.
class Archives {
    std::map<std::string,std::set<std::string>> cache;
public:
    bool Has(const std::string& path,const std::string& member) {
        const std::string key=Lower(path);auto it=cache.find(key);
        if(it==cache.end()) {
            std::set<std::string> names;
            try {
                std::ifstream f(Wide(path),std::ios::binary);Require(bool(f),"Missing RES");
                f.seekg(0,std::ios::end);const auto length=f.tellg();Require(length>=16,"Invalid RES size");
                f.seekg(0);const auto h=Read(f,16);
                const uint32_t count=ReadCatalogU32(h,4),offset=ReadCatalogU32(h,8),bytes=ReadCatalogU32(h,12);
                const uint64_t table=uint64_t(count)*22;
                Require(ReadCatalogU32(h,0)==0x019ce23c && count>0 && count<=1000000 && offset>=16
                    && bytes<=32u*1024u*1024u && uint64_t(offset)+table+bytes<=uint64_t(length),"Invalid RES index");
                f.seekg(offset);const auto index=Read(f,size_t(table)+bytes);
                for(uint32_t i=0;i<count;++i) {
                    const size_t p=size_t(i)*22;const uint32_t next=ReadCatalogU32(index,p),size=ReadCatalogU32(index,p+4),data=ReadCatalogU32(index,p+8),name=ReadCatalogU32(index,p+18);
                    const uint32_t n=index[p+16]|(uint32_t(index[p+17])<<8);
                    Require((next==0xffffffff || next<count) && uint64_t(data)+size<=uint64_t(length)
                        && name<=bytes && n<=bytes-name,"Invalid RES entry");
                    const std::string value(reinterpret_cast<const char*>(index.data()+size_t(table)+name),n);
                    names.insert(Lower(value));
                }
            } catch(const std::exception&) { names.clear(); }
            it=cache.emplace(key,std::move(names)).first;
        }
        return it->second.count(Lower(member))!=0;
    }
};

enum Kind { Weapon, Armor, Quick, Quest };
struct Binding {
    Kind kind;
    std::string name, material, type;
    int selector=-1, modelIndex=-1, variant=-1;
};
std::map<std::string,std::vector<Binding>> models;
std::string loadedProfile;
bool loaded=false;
bool catalogAlive=true;
// theWorld's global destructor also calls Destroy(). Its order relative to
// this translation unit is unspecified; never clear already destroyed STL.
struct CatalogLifetime {
    ~CatalogLifetime() { catalogAlive=false; }
} catalogLifetime;

template<class T> const T* Find(CDatabase<T>& db,const std::string& name) {
    const int count=db.GetNumEntries();
    if(name.empty() || count<0 || count>100000)return nullptr;
    for(int i=0;i<count;++i)if(Same(db[i]->szName.GetString(),name))return db[i];
    return nullptr;
}
template<class T> uintptr_t Start(CDatabase<T>& db) {
    return db.GetNumEntries()>0 ? reinterpret_cast<uintptr_t>(db[0]) : 0;
}
bool Matches(const Binding& b) {
    const SIDBItemGeneralShape* row=nullptr;
    switch(b.kind) {
    case Weapon:row=Find(theItemDatabase.weapons,b.name);break;
    case Armor:row=Find(theItemDatabase.armors,b.name);break;
    case Quick:row=Find(theItemDatabase.quickItems,b.name);break;
    case Quest:row=Find(theItemDatabase.questItems,b.name);break;
    }
    if(!row || b.modelIndex<0 || uint32_t(b.modelIndex)!=row->dwTextureTypeIndex)return false;
    if(b.kind==Weapon || b.kind==Armor) {
        if(b.selector<0 || uint32_t(b.selector)!=row->dwTypeID)return false;
        return b.kind==Weapon || (b.variant>=0 && uint32_t(b.variant)==row->dwTextureTypeIndex2 && Same(row->szMaterialType.GetString(),b.material));
    }
    if(!Same(row->szType.GetString(),b.type) || !Same(row->szMaterialType.GetString(),b.material) || b.selector<0)return false;
    return b.kind==Quick ? uint32_t(b.selector)==uint32_t(static_cast<const SIDBQuickItemShape*>(row)->idbType)
                        : uint32_t(b.selector)==static_cast<const SIDBQuestItemShape*>(row)->dwScriptID;
}
bool Pair(const Json& v,int a,int b) { return v.type=='[' && v.items.size()==2 && v.items[0].Int()==a && v.items[1].Int()==b; }

void LoadWeapons(const Json& root,const std::string& directory,Archives& archives) {
    const auto& db=root.At("database");const auto& armor=root.At("armor_database");
    if(root.At("schema_version").Int()!=2 || !root.At("path_base").Is("catalog_directory")
        || !db.At("table").Is("items.weapons") || db.At("identity_field").Int()!=0
        || db.At("weapon_class_field").Int()!=2 || db.At("model_index_field").Int()!=5)return;
    const bool hasArmor=root.Has("armor_database");
    if(hasArmor && (!armor.At("table").Is("items.armors") || armor.At("identity_field").Int()!=0
        || armor.At("armor_type_field").Int()!=2 || armor.At("model_index_field").Int()!=5))return;
    const std::string archive=root.At("model_archive").text;
    if(!Leaf(archive) || archive.size()<4 || Lower(archive.substr(archive.size()-4))!=".res")return;
    for(const auto& row:root.At("models").items) {
        const auto model=row.At("model").text;
        if(!Leaf(model) || row.At("mod").text!=model+".mod" || row.At("bon").text!=model+".bon"
            || !archives.Has(directory+archive,model+".mod") || !archives.Has(directory+archive,model+".bon"))continue;
        for(Kind kind:{Weapon,Armor}) {
            if(kind==Armor && !hasArmor)continue;
            for(const auto& link:row.At(kind==Armor?"db_armors":"db_weapons").items) {
                Binding b;b.kind=kind;b.name=link.At("name").text;
                b.modelIndex=link.At("model_index").Int();b.selector=link.At(kind==Armor?"armor_type":"weapon_class").Int();
                b.variant=link.At("texture_variant").Int();b.material=link.At("material_type").text;
                models[model].push_back(std::move(b));
            }
        }
    }
}
void LoadInventory(const Json& root) {
    const auto& db=root.At("database");const auto& texture=root.At("texture_layout");
    bool quick=false,quest=false;
    for(const auto& table:db.At("tables").items) {
        if(table.At("table").Is("items.quick_items") && table.At("section_id").Int()==4 && table.At("type_field").Int()==21)quick=true;
        if(table.At("table").Is("items.quest_items") && table.At("section_id").Int()==5 && table.At("script_id_field").Int()==21)quest=true;
    }
    if(root.At("schema_version").Int()!=2 || !root.At("path_base").Is("catalog_directory")
        || !db.At("resource").Is("items.idb") || db.At("identity_field").Int()!=0
        || db.At("model_index_field").Int()!=5 || db.At("material_type_field").Int()!=3 || !quick || !quest
        || !root.At("model_kind").Is("existing_inventory_geometry") || !texture.At("mode").Is("inventory_atlas")
        || !Pair(texture.At("logical_layer_size"),64,64) || !Pair(texture.At("logical_atlas_size"),256,256)
        || !Pair(texture.At("logical_layer_origin"),0,192) || texture.At("preserve_source_uv").type!='t')return;
    for(const auto& row:root.At("models").items) {
        const auto model=row.At("model").text,component=row.At("component").text;
        if(!Leaf(model) || component!=model+"item")continue;
        for(Kind kind:{Quick,Quest}) {
            const int index=row.At("model_index").Int();
            if(index<0 || model!=(kind==Quest?"initqu":"initqi")+std::to_string(index))continue;
            for(const auto& link:row.At(kind==Quest?"db_quest_items":"db_quick_items").items) {
                if(link.At("model_index").Int()!=index)continue;
                Binding b;b.kind=kind;b.name=link.At("name").text;b.modelIndex=index;
                b.material=link.At("material_type").text;b.type=link.At("type").text;
                b.selector=link.At(kind==Quest?"script_id":"quick_item_type").Int();
                // Ataman keeps the DB-confirmed item in m_items even when its
                // geometry/texture is unavailable for placement. containsModel
                // tests that same membership, not item.unavailableReason.
                models[component].push_back(std::move(b));
            }
        }
    }
}

void LoadCatalog(const std::string& path,const std::string& directory,bool weapons,Archives& archives) {
    std::ifstream f(Wide(path),std::ios::binary);if(!f)return;
    f.seekg(0,std::ios::end);const auto n=f.tellg();
    Require(n>=0 && uint64_t(n)<=8u*1024u*1024u,"Catalog byte budget exceeded");
    f.seekg(0);const auto bytes=Read(f,size_t(n));const std::string text(bytes.begin(),bytes.end());
    const auto root=Parser(text).Parse();if(root.type!='{')return;
    if(weapons)LoadWeapons(root,directory,archives);else LoadInventory(root);
}
std::vector<std::string> Databases() {
    // Recorded from the exact CMappedResource that loaded items.idb, including
    // overlay priority. A configured archive need not be the actual provider.
    return {theItemDatabase.sourceArchive};
}
std::string Stamp(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA info={};
    if(!GetFileAttributesExW(Wide(path).c_str(),GetFileExInfoStandard,&info))return "missing";
    return std::to_string(info.nFileSizeHigh)+":"+std::to_string(info.nFileSizeLow)+":"
        +std::to_string(info.ftLastWriteTime.dwHighDateTime)+":"+std::to_string(info.ftLastWriteTime.dwLowDateTime);
}
}

void Reset() { if(!catalogAlive)return;models.clear();loadedProfile.clear();loaded=false; }
bool UsesWorldXY(const char* model) {
    if(!catalogAlive || !model || !*model || theItemDatabase.sourceArchive.empty())return false;
    try {
        const auto databases=Databases();std::string profile=ModRuntime::Resources() ? ModRuntime::Resources()->Directory() : "";
        for(const auto& path:databases) {
            const auto directory=Directory(path);
            profile+='\n'+path+":"+Stamp(path)+":"+Stamp(directory+"mapweapons-catalog.json")+":"+Stamp(directory+"mapitems-catalog.json");
        }
        // Reload candidate membership on a DB reallocation as well as on an
        // archive/catalog replacement. Selectors are rechecked even in-place.
        profile+='\n'+std::to_string(Start(theItemDatabase.weapons))+":"+std::to_string(theItemDatabase.weapons.GetNumEntries())
            +":"+std::to_string(Start(theItemDatabase.armors))+":"+std::to_string(theItemDatabase.armors.GetNumEntries())
            +":"+std::to_string(Start(theItemDatabase.quickItems))+":"+std::to_string(theItemDatabase.quickItems.GetNumEntries())
            +":"+std::to_string(Start(theItemDatabase.questItems))+":"+std::to_string(theItemDatabase.questItems.GetNumEntries());
        if(!loaded || profile!=loadedProfile) {
            Reset();loadedProfile=profile;loaded=true;Archives archives;
            // Select the actual items.idb provider in the configured override
            // order. Never borrow a catalog from another mod or an old DB copy.
            for(const auto& path:databases)if(archives.Has(path,"items.idb")) {
                const auto directory=Directory(path);
                for(bool weapons:{true,false}) {
                    try { LoadCatalog(directory+(weapons?"mapweapons-catalog.json":"mapitems-catalog.json"),directory,weapons,archives); }
                    catch(const std::exception&) { /* malformed/missing catalog keeps native membership */ }
                }
                break;
            }
        }
        const auto it=models.find(model);if(it==models.end())return false;
        for(const auto& binding:it->second)if(Matches(binding))return true;
    } catch(const std::exception&) { return false; }
    return false;
}
}
#pragma pack(pop)
