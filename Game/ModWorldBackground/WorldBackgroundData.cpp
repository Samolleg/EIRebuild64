#include "WorldBackgroundData.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace WorldBackground {
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Json {
    char type = 0;
    std::string text;
    double number = 0;
    std::map<std::string, Json> fields;
    std::vector<Json> items;
    const Json& At(const char* name) const {
        auto i = fields.find(name);
        Require(type == '{' && i != fields.end(), "Missing JSON field");
        return i->second;
    }
    const std::vector<Json>& Array() const { Require(type == '[', "Expected JSON array"); return items; }
    std::string String() const { Require(type == 's', "Expected JSON string"); return text; }
    double Number() const { Require(type == 'n', "Expected JSON number"); return number; }
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
        Require(depth<=32 && ++nodes<=100000, "JSON complexity budget exceeded");
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
        Require(s.size()<=4u*1024u*1024u, "JSON byte budget exceeded");
        if (s.compare(0,3,"\xef\xbb\xbf")==0) p=3;
        Json v=Value(0); Space(); Require(p==s.size(), "Trailing JSON data"); return v;
    }
};
std::string Name(const Json& value) {
    std::string s=Key(value.String());
    Require(!s.empty() && s.size()<=256 && s!="." && s!="..", "Invalid identity");
    for (unsigned char c:s) Require(c>=32 && c<127 && c!='/' && c!='\\' && c!=':', "Unsupported identity");
    return s;
}
std::string Id(const Identity& i) { return Key(i.profile)+'\x1f'+Key(i.allod)+'\x1f'+Key(i.zone); }
bool Size(const CatalogEntry& e) {
    return std::isfinite(e.width) && std::isfinite(e.height) && e.width>0 && e.height>0;
}
struct Bytes {
    const uint8_t* data; size_t size;
    void Check(size_t p,size_t n) const { Require(p<=size && n<=size-p,"Truncated resource"); }
    uint16_t U16(size_t p) const { Check(p,2); return uint16_t(data[p]|(uint16_t(data[p+1])<<8)); }
    uint32_t U32(size_t p) const { return U16(p)|(uint32_t(U16(p+2))<<16); }
    float F32(size_t p) const { uint32_t n=U32(p); float f; std::memcpy(&f,&n,4); return f; }
};
std::map<std::string,Bytes> Members(const Blob& data) {
    Require(data.size()<=128u*1024u*1024u,"Archive byte budget exceeded");
    Bytes b={data.data(),data.size()}; Require(b.U32(0)==0x019ce23c,"Invalid RES signature");
    uint32_t count=b.U32(4),table=b.U32(8),namesSize=b.U32(12);
    Require(count>0 && count<=65536 && table>=16,"Invalid RES table");
    size_t tableSize=size_t(count)*22; b.Check(table,tableSize);
    size_t names=size_t(table)+tableSize; b.Check(names,namesSize);
    std::map<std::string,Bytes> result;
    for (uint32_t i=0;i<count;++i) {
        size_t p=table+size_t(i)*22;
        uint32_t len=b.U32(p+4),offset=b.U32(p+8),no=b.U32(p+18); uint16_t nl=b.U16(p+16);
        Require(nl && no<=namesSize && nl<=namesSize-no,"Invalid RES name range");
        b.Check(offset,len); Require(offset>=16 && offset<=table && len<=table-offset,"RES payload overlaps table");
        std::string name(reinterpret_cast<const char*>(b.data+names+no),nl);
        Require(name.find('\0')==std::string::npos,"NUL in RES name");
        Require(result.emplace(Key(name),Bytes{b.data+offset,len}).second,"Duplicate RES member");
    }
    return result;
}
}

std::string Key(const std::string& value) {
    size_t first=value.find_first_not_of(" \t\r\n"),last=value.find_last_not_of(" \t\r\n");
    if (first==std::string::npos) return {};
    std::string s=value.substr(first,last-first+1);
    for (char& c:s) if(c>='A' && c<='Z') c+= 'a'-'A';
    return s;
}
std::vector<Placement> ParseLayout(const std::string& json) {
    Json root=Parser(json).Parse();
    const double schema=root.At("schemaVersion").Number();
    Require((schema==1 || schema==2) && root.At("layoutAlgorithmVersion").Number()==1 &&
        root.At("coordinateSystem").String()=="mpr-local-xy","Unsupported layout version/coordinates");
    std::vector<Placement> result; std::set<std::string> ids;
    for (const Json& p:root.At("profiles").Array()) for (const Json& a:p.At("allods").Array())
        for (const Json& z:a.At("zones").Array()) {
            Placement v;
            v.identity={Name(p.At("profileId")),Name(a.At("allodId")),Name(z.At("zoneId")),Name(z.At("mprIdentity"))};
            v.component=Name(z.At("componentAnchorZoneId"));
            v.origin.x=z.At("origin").At("x").Number(); v.origin.y=z.At("origin").At("y").Number();
            if(z.fields.count("mprPath"))v.mprPath=z.At("mprPath").String();
            if(z.fields.count("mobPath"))v.mobPath=z.At("mobPath").String();
            if(z.fields.count("sectorWidth") && z.fields.count("sectorHeight")){
                const double w=z.At("sectorWidth").Number(),h=z.At("sectorHeight").Number();
                if(w>=1&&w<=128&&h>=1&&h<=128&&w==std::floor(w)&&h==std::floor(h)){v.width=w*32;v.height=h*32;}
            }
            Require(ids.insert(Id(v.identity)).second,"Duplicate layout zone"); result.push_back(v);
        }
    return result;
}
Identity ResolveActiveLayout(const std::vector<Placement>& layout,const Identity& observed){
    Require(!Key(observed.profile).empty()&&!Key(observed.mpr).empty(),"Missing active MPR identity");
    const Placement* only=nullptr;const Placement* hinted=nullptr;size_t matches=0,hints=0;
    for(const auto& p:layout)if(Key(p.identity.profile)==Key(observed.profile)&&Key(p.identity.mpr)==Key(observed.mpr)){
        only=&p;++matches;
        if((!observed.allod.empty()||!observed.zone.empty())&&
           (observed.allod.empty()||Key(p.identity.allod)==Key(observed.allod))&&
           (observed.zone.empty()||Key(p.identity.zone)==Key(observed.zone))){hinted=&p;++hints;}
    }
    if(matches==1)return only->identity;
    if(matches>1&&hints==1)return hinted->identity;
    throw std::runtime_error(matches?"Ambiguous active MPR in saved layout":"Active MPR absent from saved layout");
}
std::vector<CatalogEntry> LayoutEntries(const std::vector<Placement>& layout){
    std::vector<CatalogEntry> result;
    for(const auto& p:layout){
        // Incomplete entries cannot become neighbors; no map.txt/name fallback.
        if(p.mprPath.empty()||p.width<=0||p.height<=0)continue;
        CatalogEntry e;e.identity=p.identity;e.width=p.width;e.height=p.height;e.mob=p.mobPath;result.push_back(e);
    }
    return result;
}
std::vector<CatalogEntry> ParseCatalog(const std::string& text,const std::string& profile) {
    Require(text.size()<=4u*1024u*1024u && !Key(profile).empty(),"Invalid catalog input");
    std::vector<CatalogEntry> result; CatalogEntry current; std::string line,pending;
    std::istringstream lines(text); int route=-1;
    auto finish=[&](){if(!current.identity.zone.empty()) result.push_back(current); current=CatalogEntry();route=-1;};
    while(std::getline(lines,line)) {
        line=Key(line); if(line.empty() || line.compare(0,2,"##")==0) continue;
        std::istringstream values(line); values.imbue(std::locale::classic()); std::string word; values>>word;
        if(word=="#zone") { finish(); current.identity.profile=Key(profile); values>>current.identity.zone>>current.identity.allod; pending.clear(); continue; }
        if(word=="#exit") {
            Travel t; if(!current.identity.zone.empty() && (values>>t.exit)) { current.travel.push_back(t); route=int(current.travel.size())-1;pending="exit"; }
            else {route=-1;pending.clear();} continue;
        }
        if(word=="#res" || word=="#maps" || word=="#deploy") {pending=word;continue;}
        if(word[0]=='#') {pending.clear();continue;}
        values.clear(); values.str(line);
        if(pending=="#res") values>>current.identity.mpr>>current.mob;
        else if(pending=="#maps") {
            int w=0,h=0; if(values>>w>>h && w>0 && h>0 && w<=128 && h<=128) {current.width=w*32.0;current.height=h*32.0;}
        } else if(pending=="exit" && route>=0) values>>current.travel[route].destination>>current.travel[route].destinationExit;
        else if(pending=="#deploy" && route>=0) {
            double x1,y1,x2,y2;
            if(values>>x1>>y1>>x2>>y2 && std::isfinite(x1) && std::isfinite(y1) && std::isfinite(x2) && std::isfinite(y2)) {
                Travel& t=current.travel[route];t.minimum.x=std::min(x1,x2);t.minimum.y=std::min(y1,y2);
                t.maximum.x=std::max(x1,x2);t.maximum.y=std::max(y1,y2);t.deploy=t.maximum.x>t.minimum.x && t.maximum.y>t.minimum.y;
            }
        }
        pending.clear(); Require(result.size()<=8192 && current.travel.size()<=1024,"Catalog complexity budget exceeded");
    }
    finish(); return result;
}
std::vector<Neighbor> Select(const std::vector<Placement>& layout,
    const std::vector<CatalogEntry>& catalog,const Identity& active) {
    std::map<std::string,const CatalogEntry*> entries;
    for (const auto& e:catalog) Require(entries.emplace(Id(e.identity),&e).second,"Ambiguous catalog zone");
    auto ac=entries.find(Id(active)); Require(ac!=entries.end() && Size(*ac->second),"Missing active catalog entry");
    Require(Key(ac->second->identity.mpr)==Key(active.mpr),"Active MPR/catalog mismatch");
    const Placement* anchor=nullptr;
    for (const auto& p:layout) if (Id(p.identity)==Id(active)) { Require(!anchor,"Ambiguous active origin"); anchor=&p; }
    Require(anchor && Key(anchor->identity.mpr)==Key(active.mpr),"Missing matching active origin");
    std::vector<Neighbor> result;
    for (const auto& p:layout) {
        if (Key(p.identity.profile)!=Key(active.profile) || Key(p.identity.allod)!=Key(active.allod) ||
            Key(p.component)!=Key(anchor->component) || Id(p.identity)==Id(active) || Key(p.identity.mpr)==Key(active.mpr)) continue;
        auto it=entries.find(Id(p.identity));
        if (it==entries.end() || !Size(*it->second) || Key(it->second->identity.mpr)!=Key(p.identity.mpr)) continue;
        Neighbor n; n.entry=*it->second; n.offset.x=p.origin.x-anchor->origin.x; n.offset.y=p.origin.y-anchor->origin.y;
        const double right=n.offset.x+n.entry.width, top=n.offset.y+n.entry.height;
        if (!std::isfinite(right) || !std::isfinite(top) || !std::isfinite(n.offset.x) || !std::isfinite(n.offset.y)) continue;
        // Closed bounds, including corner contacts. No epsilon silently closes a saved gap.
        if(n.offset.x<=ac->second->width && right>=0 && n.offset.y<=ac->second->height && top>=0) result.push_back(n);
    }
    std::sort(result.begin(),result.end(),[](const Neighbor& a,const Neighbor& b){return Id(a.entry.identity)<Id(b.entry.identity);});
    return result;
}
bool ReadMember(const Blob& archive,const std::string& name,Blob& result) {
    result.clear(); auto members=Members(archive); auto it=members.find(Key(name));
    if(it==members.end()) return false;
    result.assign(it->second.data,it->second.data+it->second.size); return true;
}
Terrain DecodeMpr(const Blob& data) {
    auto members=Members(data); const Bytes* header=nullptr; Terrain t;
    for (const auto& m:members) if(m.first.size()>3 && m.first.substr(m.first.size()-3)==".mp") {
        Require(!header,"Multiple MP headers"); header=&m.second; t.name=m.first.substr(0,m.first.size()-3);
    }
    Require(header && header->U32(0)==0xce4af672,"Invalid MP header"); const Bytes& h=*header;
    t.maxZ=h.F32(4); t.sectorsX=h.U32(8); t.sectorsY=h.U32(12); t.textures=h.U32(16);
    t.textureSize=h.U32(20); uint32_t tiles=h.U32(24); t.tileSize=h.U32(28);
    uint32_t materials=h.U16(32),animated=h.U32(34);
    Require(std::isfinite(t.maxZ) && t.maxZ>=0 && t.sectorsX && t.sectorsY && t.sectorsX<=128 && t.sectorsY<=128 &&
        uint64_t(t.sectorsX)*t.sectorsY<=1024 && t.textures && t.textures<=256 && tiles && tiles<=16384 &&
        materials<=256 && animated<=tiles && t.tileSize && t.textureSize<=4096 &&
        t.textureSize/t.tileSize==8 && t.textureSize%t.tileSize==0,"Unsupported MPR size/count budget");
    Require(h.size==38+size_t(materials)*44+size_t(tiles)*4+size_t(animated)*4,"Invalid MP length");
    for(uint32_t i=0;i<materials;++i) {
        Material m; m.type=h.U32(38+i*44);
        for(size_t j=0;j<10;++j) { m.values[j]=h.F32(42+i*44+j*4); Require(std::isfinite(m.values[j]),"Nonfinite material"); }
        t.materials.push_back(m);
    }
    size_t p=38+size_t(materials)*44;
    for(uint32_t i=0;i<tiles;++i) t.terrainIds.push_back(h.U32(p+i*4));
    p+=size_t(tiles)*4;
    for(uint32_t i=0;i<animated;++i) { Animation a={h.U16(p+i*4),h.U16(p+i*4+2)}; t.animations.push_back(a); }
    for(uint32_t y=0;y<t.sectorsY;++y) for(uint32_t x=0;x<t.sectorsX;++x) {
        auto digits=[](uint32_t n){std::string s=std::to_string(n); return std::string(3-s.size(),'0')+s;};
        auto it=members.find(t.name+digits(x)+digits(y)+".sec"); Require(it!=members.end(),"Missing MPR sector");
        const Bytes& b=it->second; b.Check(0,5); bool overlay=b.data[4]==3;
        Require(b.U32(0)==0xcf4bf774 && (b.data[4]==0 || overlay) && b.size==size_t(overlay?18965:9229),"Invalid sector layout");
        Sector s; s.x=x; s.y=y; s.overlayTiles.fill(0); s.overlayMaterials.fill(65535); s.overlayAlpha.fill(255);
        for(unsigned layer=0;layer<(overlay?2u:1u);++layer) {
            auto& vertices=layer?s.overlay:s.land; vertices.reserve(1089);
            for(unsigned row=0;row<33;++row) for(unsigned col=0;col<33;++col) {
                size_t v=5+size_t(layer)*1089*8+(row*33+col)*8; uint32_t normal=b.U32(v+4);
                auto signedByte=[](uint8_t q){return q<128?int(q):int(q)-256;};
                // Verified original game kernel uses FLOAT reciprocals 1/126, 1/65534.
                // Editor preview's 1/254 XY and 1/65535 Z differ from native terrain.
                Vertex vertex={float(x*32+col)+signedByte(b.data[v])*0.5f*(1.0f/126.0f),float(y*32+row)+signedByte(b.data[v+1])*0.5f*(1.0f/126.0f),
                    float(b.U16(v+2))*t.maxZ*(1.0f/65534.0f),(float((normal>>11)&2047)-1000)*0.001f,
                    (float(normal&2047)-1000)*0.001f,float(normal>>22)*0.001f};
                vertices.push_back(vertex);
            }
        }
        size_t tileOffset=5+1089*8*(overlay?2:1);
        for(size_t j=0;j<256;++j) {
            s.landTiles[j]=b.U16(tileOffset+j*2);
            if(overlay) { s.overlayTiles[j]=b.U16(tileOffset+512+j*2); s.overlayMaterials[j]=b.U16(tileOffset+1024+j*2); }
            for(unsigned layer=0;layer<(overlay?2u:1u);++layer) {
                if(layer && s.overlayMaterials[j]==65535) continue;
                uint16_t tile=layer?s.overlayTiles[j]:s.landTiles[j];
                Require(uint32_t(tile&16383)<tiles && uint32_t((tile>>6)&255)<t.textures,"Invalid tile reference");
                if(layer) Require(s.overlayMaterials[j]<materials,"Invalid material reference");
            }
        }
        if(overlay)for(int row=0;row<33;++row)for(int col=0;col<33;++col){
            int first=-1,geometry=-1;
            // Original SObjFindInfo visits x first, then y. Alpha uses first;
            // geometry uses the smallest wave multiplier, retaining the first tie.
            for(int tx=(col-1)/2-(col==0?1:0);tx<=col/2;++tx)for(int ty=(row-1)/2-(row==0?1:0);ty<=row/2;++ty){
                if(tx<0||tx>=16||ty<0||ty>=16)continue;
                int id=s.overlayMaterials[ty*16+tx];if(id==65535)continue;
                if(first<0)first=id;
                if(geometry<0||t.materials[id].values[5]<t.materials[geometry].values[5])geometry=id;
            }
            const size_t i=size_t(row)*33+col;auto& vertex=s.overlay[i];
            vertex.x=float(x*32+col);vertex.y=float(y*32+row);
            if(geometry<0)vertex.z=0;
            else if(t.materials[geometry].type==4){vertex.x=s.land[i].x;vertex.y=s.land[i].y;}
            if(first>=0){const float opacity=t.materials[first].values[3];
                Require(opacity>=0&&opacity<=1,"Invalid overlay opacity");
                s.overlayAlpha[i]=uint8_t(double(opacity)*255.0);
            }
        }
        t.sectors.push_back(std::move(s));
    }
    return t;
}
size_t Terrain::Bytes() const {
    size_t n=sizeof(Terrain)+name.capacity()+materials.capacity()*sizeof(Material)+terrainIds.capacity()*4+animations.capacity()*sizeof(Animation)+sectors.capacity()*sizeof(Sector);
    for(const auto& s:sectors) n+=(s.land.capacity()+s.overlay.capacity())*sizeof(Vertex);
    return n;
}
void BackgroundData::Clear() { Snapshot empty; std::swap(snapshot_,empty); }
void BackgroundData::Prepare(bool enabled,const std::string& json,const std::vector<CatalogEntry>& catalog,
    const Identity& active,const ReadMpr& read,size_t budgetBytes,const ReadTexture& textures) {
    // Clear first: an invalid new map must never retain the previous map's background.
    Clear(); if(!enabled) return;
    try {
        const auto layout=ParseLayout(json);
        auto candidates=Select(layout,catalog,active);
        for(const auto& n:candidates) try {
            Prepared p; p.placement=n; p.terrain=DecodeMpr(read(n.entry.identity));
            Require(p.terrain.sectorsX*32.0==n.entry.width && p.terrain.sectorsY*32.0==n.entry.height,"MPR/catalog size mismatch");
            size_t bytes=p.terrain.Bytes(); Require(snapshot_.bytes<=budgetBytes && bytes<=budgetBytes-snapshot_.bytes,"Background byte budget exceeded");
            if(textures) for(uint32_t atlas=0;atlas<p.terrain.textures;++atlas) {
                std::string digits=std::to_string(atlas);
                TexturePayload texture;texture.name=p.terrain.name+std::string(3-digits.size(),'0')+digits+".mmp";
                texture.mmp=textures(n.entry.identity.profile,texture.name);
                Bytes b={texture.mmp.data(),texture.mmp.size()};
                b.Check(0,76);
                uint32_t width=b.U32(4),height=b.U32(8),levels=b.U32(12),format=b.U32(16);
                // Resource overrides can contain higher-resolution atlases than MP.
                Require(b.U32(0)==0x00504d4d && width>=8 && width<=4096 && width==height && width%8==0,"Invalid terrain MMP header/size");
                if(format>=0x31545844 && format<=0x35545844 && (format&0x00ffffff)==0x00545844) {
                    Require(levels && levels<=13,"Invalid MMP mip count");size_t required=76;
                    for(uint32_t level=0;level<levels;++level) {
                        required+=size_t((width+3)/4)*((height+3)/4)*(format==0x31545844?8:16);
                        width=std::max(1u,width/2);height=std::max(1u,height/2);
                    }
                    b.Check(0,required);
                } else if(format==0x33544e50) {Require(levels>0,"Empty PNT3 payload");b.Check(76,levels);}
                else if(format==0x5650 || format==0x5551 || format==0x4444 || format==0x8888) {
                    uint32_t bits=b.U32(20);Require(bits==16 || bits==32,"Invalid MMP pixel size");
                    for(size_t shift:{size_t(28),size_t(40),size_t(52),size_t(64)})Require(b.U32(shift)<32,"Invalid MMP channel shift");
                    b.Check(76,size_t(width)*height*(bits/8));
                } else throw std::runtime_error("Unsupported MMP format");
                const size_t extra=sizeof(TexturePayload)+texture.name.capacity()+texture.mmp.capacity();
                Require(extra<=budgetBytes-snapshot_.bytes-bytes,"Texture byte budget exceeded");
                bytes+=extra;p.textures.push_back(std::move(texture));
            }
            snapshot_.neighbors.push_back(std::move(p)); snapshot_.bytes+=bytes;
        } catch(const std::exception& e) { snapshot_.diagnostics.push_back(n.entry.identity.zone+": "+e.what()); }
    } catch(const std::exception& e) { snapshot_.diagnostics.push_back(e.what()); }
}
}
