#ifndef GAME_JOURNAL_STANDALONE
#include "StdAfx.h"
#include "Unit.h"
#include "WorldClient.h"
#include "WorldServer.h"
#include "MapObjectManager.h"
#include "PlayerClient.h"
#include "Item.h"
#include "SpellSystem.h"
#include "TextureManager.h"
#include "Renderer.h"
#include "dx11/RendererDX11.h"
#include <dxgi1_4.h>
#include "CMappedResource.h"
#include "../external/owned-renderer/Source/owned_resource_store.h"
#include "../external/owned-renderer/Source/res_mapping.h"
#endif
#include "GameJournal.h"
#include "CrashEvidence.h"
#include "GameMemorySnapshot.h"
#include <new>
#include <cstring>
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <map>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace GameJournal {
namespace {
thread_local const char* source = "engine";
thread_local std::string pendingCallback, callback;
thread_local int actor = -1;
#ifdef GAME_JOURNAL_STANDALONE
std::atomic<bool> testCombatClock{false};
std::atomic<DWORD> testCombatTime{0};
#endif
DWORD CombatNow() noexcept {
#ifdef GAME_JOURNAL_STANDALONE
    if(testCombatClock)return testCombatTime.load();
#endif
    return GetTickCount();
}
volatile LONG gameTick = 0;
struct Lock {
    SRWLOCK* lock;
    explicit Lock(SRWLOCK& value, bool tryOnly = false) : lock(&value) {
        if(tryOnly){if(!TryAcquireSRWLockExclusive(lock))lock=nullptr;}
        else AcquireSRWLockExclusive(lock);
    }
    ~Lock() { if(lock)ReleaseSRWLockExclusive(lock); }
};
struct Entry { std::string line; bool important; };
struct Repeat { DWORD time; unsigned count; std::string detail; };
struct Counter { DWORD time; unsigned count; std::string category, event, key; };
struct CombatPair {
    DWORD started, last; int attacker, target; std::string attackerPrototype, targetPrototype;
    float damage=0, healing=0; unsigned hits=0, misses=0, blocked=0, critical=0, unknown=0;
};
struct State {
    SRWLOCK queueLock = SRWLOCK_INIT, writeLock = SRWLOCK_INIT;
    std::deque<Entry> queue;
    std::map<std::string, Repeat> repeats;
    std::map<std::string, Repeat> notifications;
    std::map<std::string, Counter> counters;
    std::map<std::pair<int,int>, CombatPair> combat;
    bool fighting=false;
    DWORD lastCombat=0;
    size_t queued = 0;
    unsigned dropped = 0;
    HANDLE wake = nullptr, thread = nullptr, ready = nullptr;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::wstring directory, executable;
    std::atomic<bool> stopping{false};
    DWORD limit = 100 * 1024 * 1024;
    unsigned long long bytes = 0;
    bool capped = false;
    DWORD gameThread=0, memoryTick=0, textureTick=0, resourceTick=0;
    SRWLOCK memoryLock=SRWLOCK_INIT;
    TextureMemory textures;
    GpuMemory gpu;
    ResourceMemory resources;
    char zone[384]="unknown";
    bool memoryEnded=false;
};
std::atomic<State*> current{nullptr};
std::string Utf8(const char* text) {
    if (!text) return "";
    int n = MultiByteToWideChar(1251, 0, text, -1, nullptr, 0);
    if (n <= 0) return "";
    std::vector<wchar_t> wide(n);
    MultiByteToWideChar(1251, 0, text, -1, wide.data(), n);
    n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::vector<char> data(n);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), -1, data.data(), n, nullptr, nullptr);
    return data.data();
}
std::string WideUtf8(const wchar_t* text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::vector<char> data(n);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, data.data(), n, nullptr, nullptr);
    return data.data();
}
std::string Escape(const std::string& value, bool quoted) {
    std::string out = quoted ? "\"" : "";
    for (unsigned char c : value) {
        if (c == '\r') out += "\\r";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (quoted && (c == '\\' || c == '"')) { out += '\\'; out += c; }
        else if (c < 32) { char hex[5]; sprintf_s(hex, "\\x%02x", c); out += hex; }
        else out += c;
    }
    if (quoted) out += '"';
    return out;
}
Entry Line(const char* category, const char* event, const std::string& detail) {
    SYSTEMTIME time; GetLocalTime(&time);
    char head[140];
    sprintf_s(head, "%02u:%02u:%02u.%03u g=%lu %s %s ", time.wHour, time.wMinute,
              time.wSecond, time.wMilliseconds, static_cast<DWORD>(InterlockedCompareExchange(&gameTick, 0, 0)), category, event);
    return {std::string(head) + Escape(detail, false) + "\r\n",
            strcmp(category, "ERROR") == 0 || strcmp(category, "SESSION") == 0};
}
void Enqueue(State& s, Entry entry, bool urgent) {
    {
        Lock guard(s.queueLock);
        if (s.stopping) return;
        if (s.queued + entry.line.size() > 16 * 1024 * 1024) { ++s.dropped; return; }
        s.queued += entry.line.size(); s.queue.push_back(std::move(entry));
    }
    if (urgent) SetEvent(s.wake);
}
void Write(State& s, const std::string& value) {
    if (s.file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    if (!WriteFile(s.file, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) || written != value.size()) {
        CloseHandle(s.file); s.file = INVALID_HANDLE_VALUE; return;
    }
    s.bytes += written;
}
void CombatDrain(State& s, std::deque<Entry>& pending, DWORD now, bool all);
void Drain(State& s, bool tryQueue = false, bool critical = false) {
    std::deque<Entry> pending;
    {
        Lock guard(s.queueLock,tryQueue);
        if(!guard.lock)return;
        pending.swap(s.queue); s.queued = 0;
        DWORD now = GetTickCount();
        for (auto it = s.notifications.begin(); it != s.notifications.end();) {
            auto& repeat = it->second;
            if (repeat.count && (critical || s.stopping || now - repeat.time >= 2000)) {
                auto entry = Line("ERROR", "notification_repeat", repeat.detail + " count=" + std::to_string(repeat.count));
                entry.important = false;
                pending.push_back(std::move(entry));
                repeat.count = 0;
                repeat.time = now;
            }
            if (s.stopping || now - repeat.time >= 60000) it = s.notifications.erase(it);
            else ++it;
        }
        CombatDrain(s, pending, CombatNow(), true);
        for (auto it = s.repeats.begin(); it != s.repeats.end();) {
            const bool expired = now - it->second.time >= 2000;
            if(it->second.count && (expired || critical || s.stopping)){
                pending.push_back(Line("GVAR","repeat",Utf8(it->second.detail.c_str())+" count="+std::to_string(it->second.count)));
                it->second.count=0;
            }
            if (s.stopping || expired) {
                it = s.repeats.erase(it);
            } else ++it;
        }
        for (auto it = s.counters.begin(); it != s.counters.end();) {
            const auto& c = it->second;
            pending.push_back(Line(c.category.c_str(),c.event.c_str(),"key="+Escape(Utf8(c.key.c_str()),true)+" count="+std::to_string(c.count)));
            it = s.counters.erase(it);
        }
        if (s.dropped) {
            pending.push_back(Line("ERROR", "queue_overflow", "dropped=" + std::to_string(s.dropped)));
            s.dropped = 0;
        }
    }
    for (const auto& entry : pending) {
        if (!s.capped && s.bytes + entry.line.size() > s.limit) {
            s.capped = true;
            Write(s, Line("SESSION", "size_limit", "normal_events_suppressed=true limit=" + std::to_string(s.limit)).line);
        }
        if (!s.capped || entry.important) Write(s, entry.line);
    }
    if (s.file != INVALID_HANDLE_VALUE) FlushFileBuffers(s.file);
}
bool Owned(const std::wstring& name) {
    // Strictly our timestamp/pid names. Never delete a directory or arbitrary wildcard match.
    if (name.size() < 29 || name.compare(0, 8, L"journal-") || name.substr(name.size()-4) != L".log") return false;
    if (name[16] != L'-' || name[23] != L'-') return false;
    for (size_t i=8; i<name.size()-4; ++i)
        if (i != 16 && i != 23 && (name[i] < L'0' || name[i] > L'9')) return false;
    return true;
}
void Open(State& s) {
    if (!CreateDirectoryW(s.directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return;
    SYSTEMTIME t; GetLocalTime(&t); wchar_t name[100];
    swprintf_s(name, L"journal-%04u%02u%02u-%02u%02u%02u-%lu.log", t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId());
    std::wstring path = s.directory + L"\\" + name;
    s.file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (s.file == INVALID_HANDLE_VALUE) return;
    struct Old { std::wstring name; ULONGLONG time; };
    std::vector<Old> files;
    WIN32_FIND_DATAW data; HANDLE find = FindFirstFileW((s.directory + L"\\journal-*.log").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && Owned(data.cFileName))
                files.push_back({data.cFileName,(static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime});
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    std::sort(files.begin(),files.end(),[](const Old& a,const Old& b){return a.time==b.time ? a.name<b.name : a.time<b.time;});
    size_t remaining = files.size();
    for (const auto& old : files) {
        if (remaining <= 10) break;
        if (old.name != name && DeleteFileW((s.directory+L"\\"+old.name).c_str())) --remaining;
    }
    Write(s,Line("SESSION","start","build=\"EIRebuild R10 " __DATE__ " " __TIME__ "\" exe="+Escape(WideUtf8(s.executable.c_str()),true)+" pid="+std::to_string(GetCurrentProcessId())).line);
}
void Identity(State& s) {
    HANDLE file = CreateFileW(s.executable.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    unsigned char digest[32]={}; bool okay=false;
    if (file != INVALID_HANDLE_VALUE && BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 && BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0) {
        unsigned char buffer[64*1024]; DWORD read=0; bool valid=true;
        for (;;) {
            if(!ReadFile(file,buffer,sizeof(buffer),&read,nullptr)){valid=false;break;}
            if(!read)break;
            if(BCryptHashData(hash,buffer,read,0)<0){valid=false;break;}
        }
        okay=valid&&BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    }
    if(hash)BCryptDestroyHash(hash); if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
    std::string hex;
    if(okay)for(unsigned char c:digest){char part[3];sprintf_s(part,"%02X",c);hex+=part;}
    Write(s,Line("SESSION","identity","sha256="+(okay?hex:"unavailable")).line);
}
DWORD WINAPI Worker(void* value) noexcept {
    State& s=*static_cast<State*>(value);
    try { Open(s); } catch(...) {}
    SetEvent(s.ready);
    try { Lock guard(s.writeLock); Identity(s); } catch(...) {}
    for (;;) {
        WaitForSingleObject(s.wake,2000);
        // Decide before draining: Stop may enqueue final records during this drain.
        const bool stopAfterDrain=s.stopping.load();
        try { Lock guard(s.writeLock); Drain(s); } catch(...) {}
        if(stopAfterDrain)break;
    }
    if(s.file!=INVALID_HANDLE_VALUE){CloseHandle(s.file);s.file=INVALID_HANDLE_VALUE;}
    return 0;
}
void StartAt(const wchar_t* directory, DWORD limit) {
    if(current.load())return;
    State* s=new State;
    s->gameThread=GetCurrentThreadId();s->memoryTick=GetTickCount();
    try {
        wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);
        if(!n||n==32768){delete s;return;} s->executable.assign(exe,n);
        s->directory=directory?directory:s->executable.substr(0,s->executable.find_last_of(L"\\/"))+L"\\Logs";
        s->limit=limit;
        s->wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);s->ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!s->wake||!s->ready)throw 1;
        current.store(s);
        s->thread=CreateThread(nullptr,0,Worker,s,0,nullptr);
        if(!s->thread)throw 1;
    } catch(...) {current.store(nullptr);if(s->wake)CloseHandle(s->wake);if(s->ready)CloseHandle(s->ready);delete s;}
}
void Synchronize(State& s) noexcept {
    // Bounded wait avoids deadlocking the exception handler if the writer faulted holding a lock.
    if(WaitForSingleObject(s.ready,2000)!=WAIT_OBJECT_0)return;
    DWORD start=GetTickCount();
    while(!TryAcquireSRWLockExclusive(&s.writeLock)) {
        if(GetTickCount()-start>=2000)return;
        Sleep(1);
    }
    try {Drain(s,true,true);}catch(...){}
    ReleaseSRWLockExclusive(&s.writeLock);
}
}
namespace {
void Append(State& s, Entry entry) {
    if(s.queued+entry.line.size()>16*1024*1024){++s.dropped;return;}
    s.queued+=entry.line.size();s.queue.push_back(std::move(entry));
}
Entry PairLine(const CombatPair& p, DWORD now) {
    char values[320];
    sprintf_s(values," attacker=%d target=%d damage=%.9g hits=%u misses=%u blocked=%u critical=%u critical_unknown=%u healing=%.9g interval_ms=%lu flush_age_ms=%lu",
        p.attacker,p.target,p.damage,p.hits,p.misses,p.blocked,p.critical,p.unknown,p.healing,p.last-p.started,now-p.started);
    return Line("COMBAT","aggregate",std::string(values)+" attacker_prototype="+Escape(Utf8(p.attackerPrototype.c_str()),true)+" target_prototype="+Escape(Utf8(p.targetPrototype.c_str()),true));
}
void CombatDrain(State& s, std::deque<Entry>& pending, DWORD now, bool all) {
    for(auto it=s.combat.begin();it!=s.combat.end();) {
        if(all || now-it->second.started>=2000){pending.push_back(PairLine(it->second,now));it=s.combat.erase(it);}
        else ++it;
    }
    if(s.fighting && now-s.lastCombat>=10000) {
        s.fighting=false;
        pending.push_back(Line("COMBAT","end","reason=idle_timeout idle_ms="+std::to_string(now-s.lastCombat)+" heuristic=true"));
    }
}
void Record(int a,int t,const char* ap,const char* tp,bool squad,float damage,float healing,int outcome,int critical) {
    State* s=current.load();if(!s||s->stopping)return;
    Lock guard(s->queueLock);DWORD now=CombatNow();
    // Expire before the next event, including an inactivity gap between hits.
    std::deque<Entry> expired;CombatDrain(*s,expired,now,false);
    for(auto& entry:expired)Append(*s,std::move(entry));
    if(squad) {
        if(!s->fighting){s->fighting=true;Append(*s,Line("COMBAT","begin","reason=squad_event heuristic=true idle_threshold_ms=10000"));}
        s->lastCombat=now;
    }
    auto key=std::make_pair(a,t);auto it=s->combat.find(key);
    if(it==s->combat.end()) {
        if(s->combat.size()>=512){++s->dropped;return;}
        it=s->combat.emplace(key,CombatPair{now,now,a,t,ap?ap:"unknown",tp?tp:"unknown"}).first;
    }
    auto& p=it->second;p.last=now;p.damage+=damage>0?damage:0;p.healing+=healing>0?healing:0;
    if(outcome==1){++p.hits;if(critical>0)++p.critical;else if(critical<0)++p.unknown;}
    else if(outcome==0)++p.misses;else if(outcome==2)++p.blocked;
}
}
void Damage(int a,int t,const char* ap,const char* tp,bool squad,float amount,int outcome,int critical) noexcept {
    try{Record(a,t,ap,tp,squad,amount,0,outcome,critical);}catch(...){}
}
void Healing(int a,int t,const char* tp,float amount) noexcept {
    try{Record(a,t,"context",tp,false,0,amount,-1,-1);}catch(...){}
}
void CombatBoundary(const char* reason,int dead) noexcept {
    State* s=current.load();if(!s||s->stopping)return;
    try {
        Lock guard(s->queueLock);DWORD now=CombatNow();
        for(auto it=s->combat.begin();it!=s->combat.end();) {
            if(dead<0 || it->first.first==dead || it->first.second==dead){Append(*s,PairLine(it->second,now));it=s->combat.erase(it);}
            else ++it;
        }
        if(dead<0 && s->fighting){s->fighting=false;Append(*s,Line("COMBAT","end","reason="+Quote(reason)+" heuristic=true"));}
    }catch(...){}
}
const char* CurrentSource() noexcept{return source;}
void SetCallback(const char* name) noexcept{try{pendingCallback=name?name:"";}catch(...){pendingCallback.clear();}}
Callback::Callback() noexcept {previous.swap(callback);callback.swap(pendingCallback);}
Callback::~Callback() noexcept {callback.swap(previous);}
Actor::Actor(int id) noexcept:previous(actor){actor=id;}
Actor::~Actor() noexcept{actor=previous;}
const char* CampMode(int value) noexcept {
    static const char* names[]={"ISM_WEAPONS","ISM_SPELLS","ISM_SPELLTRADE","ISM_SPELLCONSTR","ISM_ITEMTRADE","ISM_ITEMCONSTR","ISM_ITEMREPAIR","ISM_EXIT","ISM_TOTAL","ISM_CHOOSEPERS","ISM_SKIN","ISM_STATS"};
    return value>=0&&value<12?names[value]:"ISM_UNKNOWN";
}
const char* ItemPart(int value) noexcept {
    static const char* names[]={"ISP_CAMPSLOTS","ISP_WAGON","ISP_SPELLASSORT","ISP_ITEMASSORT","ISP_SPELLTRADE","ISP_ITEMTRADE","ISP_SPELLCONSTR","ISP_ITEMCONSTR","ISP_ITEMREPAIR","ISP_UNKNOWN"};
    return value>=0&&value<10?names[value]:"ISP_UNKNOWN";
}
#ifndef GAME_JOURNAL_STANDALONE
namespace {
const char* Prototype(CUnit* u){return u&&u->GetPrototype()?(LPCSTR)u->GetPrototype()->szName:"unknown";}
bool Squad(CUnit* u){auto* player=theWorld.GetPlayer();return u&&u->GetOwner()&&player&&player->GetPlayer()&&u->GetPlayerNum()==player->GetPlayer()->GetPlayerNumber();}
struct TradeRow { int id,count,price;unsigned type;std::string direction,spell; };
thread_local const void* tradeScreen=nullptr;
thread_local std::vector<TradeRow> tradeRows;
thread_local bool tradeOverflow=false;
}
void ObserveDamage(CUnit* a,CUnit* t,float amount,int outcome,int critical) noexcept {
    try{if(t)Damage(a?a->GetID():actor,t->GetID(),Prototype(a),Prototype(t),Squad(a)||Squad(t),amount,outcome,critical);}catch(...){}
}
void ObserveHeal(CUnit* t,float amount) noexcept {
    try{if(t&&amount>0)Healing(actor,t->GetID(),Prototype(t),amount);}catch(...){}
}
SpellObservation CaptureSpell(CMapObject* a, CMapObject* t) noexcept {
    SpellObservation captured;
    if (a) {
        captured.actor = a->GetID();
        if (a->WhoAmI() == HPC_UNIT) {
            auto* unit = static_cast<CUnit*>(a);
            strncpy_s(captured.prototype, Prototype(unit), _TRUNCATE);
            captured.squad = Squad(unit);
        }
    }
    if (t) captured.target = t->GetID();
    return captured;
}
void ObserveSpell(const SpellObservation& captured,const char* code,int phase,int result,float x,float y,float z,bool combat) noexcept {
    try {
        char key[512];_snprintf_s(key,sizeof(key),_TRUNCATE,"actor=%d prototype=%s spell=%s target=%d x=%g y=%g z=%g phase=%d result_phase=%d",captured.actor,captured.prototype,code?code:"unknown",captured.target,x,y,z,phase,result);
        Count("COMBAT","spell",key);
        if(combat&&captured.squad){State* s=current.load();if(s&&!s->stopping){Lock guard(s->queueLock);DWORD now=CombatNow();if(!s->fighting){s->fighting=true;Append(*s,Line("COMBAT","begin","reason=squad_spell heuristic=true idle_threshold_ms=10000"));}s->lastCombat=now;}}
    }catch(...){}
}
void ObserveObject(CMapObject* object,const char* action) noexcept {
    try{if(object)Event("WORLD","interaction","action=%s id=%d type=%u name=%s",Quote(action).c_str(),object->GetID(),unsigned(object->GetSubtypeID()),Quote(object->WhoAmI()==HPC_UNIT?Prototype(static_cast<CUnit*>(object)):object->GetGameClassName()).c_str());}catch(...){}
}
void ObserveSelection(const std::list<int>& ids) noexcept {
    try {
        std::string value;
        for(int id:ids){auto* object=theWorld.GetMapObjectManager()?static_cast<CMapObject*>(theWorld.GetMapObjectManager()->Find(id)):nullptr;value+=std::to_string(id)+":"+(object&&object->WhoAmI()==HPC_UNIT?Prototype(static_cast<CUnit*>(object)):"unknown")+";";}
        static thread_local std::string last;
        if(last==value)return;last=value;
        Event("INPUT","selection","count=%u members=%s",unsigned(ids.size()),Quote(value.c_str()).c_str());
    }catch(...){}
}
void TradeReset(const void* screen) noexcept {tradeScreen=screen;tradeRows.clear();tradeOverflow=false;}
void TradeQuote(const void* screen,CItem* item,const char* direction,int price) noexcept {
    try {
        if(screen!=tradeScreen||!item)return;
        if(tradeRows.size()>=4096){tradeOverflow=true;return;}
        CSpell* spell=item->GetSpell(0);
        tradeRows.push_back({item->GetID(),item->GetCount(),price,unsigned(item->GetSubtypeID()),direction,spell&&spell->GetPrototype()?spell->GetPrototype()->GetCodeName():""});
    }catch(...){tradeOverflow=true;}
}
void TradeCommit(const void* screen,int shop,int total) noexcept {
    try {
        if(tradeScreen!=screen){Event("TRADE","items_unavailable","shop=%d total=%d",shop,total);return;}
        for(const auto& row:tradeRows)Event("TRADE","item","result=accepted shop=%d direction=%s id=%d prototype_type=%u spell=%s quantity=%d unit_price=%d",shop,row.direction.c_str(),row.id,row.type,Quote(row.spell.c_str()).c_str(),row.count,row.price);
        if(tradeOverflow)Event("TRADE","items_truncated","shop=%d limit=4096",shop);
        tradeRows.clear();
    }catch(...){}
}
#endif


namespace {

bool MemoryContains(const char* text,const char* needle) noexcept {
    for(;*text;++text){const char* a=text;const char* b=needle;
        while(*a && *b && ((*a>='A' && *a<='Z')?*a-'A'+'a':*a)==*b){++a;++b;}
        if(!*b)return true;
    }return false;
}

void ReadGameMemory(State& s, bool failure) noexcept {
    if(GetCurrentThreadId()!=s.gameThread)return;
    TextureMemory textures;GpuMemory gpu;char zone[384]="unknown";
    ResourceMemory resources;
#ifndef GAME_JOURNAL_STANDALONE
    try {
        if(auto* manager=theWorld.GetTextureManager())textures=manager->ReadMemoryCounters();
        const char* name=theWorldServer.GetJournalZoneName();
        if(name && *name){
            wchar_t wide[128]={};char utf8[384]={};
            if(MultiByteToWideChar(1251,0,name,-1,wide,128) &&
               WideCharToMultiByte(CP_UTF8,0,wide,-1,utf8,384,nullptr,nullptr)){
                strcpy_s(zone,utf8);
                for(char* p=zone;*p;++p)if(static_cast<unsigned char>(*p)<32 || *p=='"' || *p=='\\')*p='_';
            }
        }
        auto* renderer=GetRenderer();auto* dx=renderer?renderer->GetD3D():nullptr;
        resources.hostPayload=CMappedResource::LivePayloadBytes();
        resources.ownedImmutable=owned::owned_resource_live_bytes();
        const auto mappings=owned::res_mapping_snapshot();
        resources.mappedArchives=mappings.physical_view_bytes;resources.mappedSaved=mappings.active_saved_view_bytes;
        // An error handler must not synchronize/re-enter the render queue.
        if(!failure && dx) {
            const auto owned=dx->ReadOwnedMemoryCounters();
            resources.rendererAvailable=owned.available;resources.geometry=owned.geometry;
            resources.textures=owned.textures;resources.animation=owned.animation;resources.world=owned.world;
            resources.surfaceCpu=owned.surfaceCpu;resources.surfaceGpu=owned.surfaceGpu;resources.neighbours=owned.neighbours;
        }
        auto device=dx?dx->GetDevice():Microsoft::WRL::ComPtr<ID3D11Device>();
        if(device){
            Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
            if(SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter.As(&adapter3))){
                DXGI_QUERY_VIDEO_MEMORY_INFO info={};
                gpu.local=SUCCEEDED(adapter3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info));
                if(gpu.local){gpu.localUsage=info.CurrentUsage;gpu.localBudget=info.Budget;}
                gpu.nonlocal=SUCCEEDED(adapter3->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&info));
                if(gpu.nonlocal){gpu.nonlocalUsage=info.CurrentUsage;gpu.nonlocalBudget=info.Budget;}
            }
        }
    }catch(...){} // Observation must not escape into the game's error policy.
#endif
    AcquireSRWLockExclusive(&s.memoryLock);
    if(!failure){s.resources=resources;s.resourceTick=GetTickCount();}
    s.textures=textures;s.gpu=gpu;strcpy_s(s.zone,zone);s.textureTick=GetTickCount();
    ReleaseSRWLockExclusive(&s.memoryLock);
}
}
void MemorySnapshot(const char* reason,const char* allocationSource) noexcept {
    State* s=current.load();if(!s || s->stopping || !reason)return;
    const bool failure=!strcmp(reason,"alloc_failure");
    // Called from existing handlers after stack unwinding; only the owner reads live providers.
    // A background allocation failure uses the last complete owner snapshot.
    ReadGameMemory(*s,failure);
    TextureMemory textures;GpuMemory gpu;char zone[384]="unknown";DWORD observed=0;
    ResourceMemory resources;
    DWORD resourceObserved=0;
    if(TryAcquireSRWLockShared(&s->memoryLock)){
        textures=s->textures;gpu=s->gpu;strcpy_s(zone,s->zone);observed=s->textureTick;
        resources=s->resources;
        resourceObserved=s->resourceTick;
        ReleaseSRWLockShared(&s->memoryLock);
    }
    const auto memory=CollectProcessMemory();constexpr double mb=1024.0*1024.0;
    SYSTEMTIME time={};GetLocalTime(&time);char line[5120]={};
    int length=_snprintf_s(line,sizeof(line),_TRUNCATE,
        "%02u:%02u:%02u.%03u g=%lu %sMEMORY snapshot reason=%s zone=\"%s\" source=%s "
        "process_valid=%d private_mb=%.3f working_set_mb=%.3f peak_working_set_mb=%.3f pagefile_mb=%.3f "
        "va_total_mb=%.3f va_free_mb=%.3f va_largest_free_mb=%.3f va_complete=%d va_regions=%u va_scan_ms=%.3f "
        "heap_count=%u textures_available=%d textures_count=%llu textures_loaded=%llu textures_bytes=%llu "
        "lru_count=%llu lru_bytes=%llu lru_allowed_bytes=%llu cache_count=%llu cache_bytes=%llu "
        "texture_snapshot=%s texture_age_ms=%lu",
        time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
        static_cast<DWORD>(InterlockedCompareExchange(&gameTick,0,0)),failure?"ERROR ":"",reason,zone,
        allocationSource && *allocationSource?allocationSource:"none",memory.processValid,
        memory.privateBytes/mb,memory.workingSet/mb,memory.peakWorkingSet/mb,memory.pagefile/mb,
        memory.vaTotal/mb,memory.vaFree/mb,memory.vaLargest/mb,memory.vaComplete,memory.regions,memory.vaMilliseconds,
        memory.heapCount,textures.available,textures.count,textures.loaded,textures.bytes,textures.lruCount,
        textures.lruBytes,textures.lruAllowed,textures.cacheCount,textures.cacheBytes,
        GetCurrentThreadId()==s->gameThread?"current":"cached",observed?GetTickCount()-observed:0);
    if(length<0)return;
    length+=sprintf_s(line+length,sizeof(line)-length,
        " va_private_commit_mb=%.3f va_mapped_commit_mb=%.3f va_image_commit_mb=%.3f va_reserved_mb=%.3f"
        " heaps_complete=%d heaps_summarized=%u heap_allocated_mb=%.3f heap_committed_mb=%.3f heap_reserved_mb=%.3f"
        " crt_heap_valid=%d crt_heap_allocated_mb=%.3f crt_heap_committed_mb=%.3f"
        " private_nonheap_commit_mb=%.3f host_resource_payload_mb=%.3f host_mapped_files_mb=%.3f host_mapping_saved_mb=%.3f owned_immutable_mb=%.3f"
        " renderer_counters_valid=%d renderer_geometry_mb=%.3f renderer_texture_cache_mb=%.3f renderer_animation_mb=%.3f"
        " renderer_world_mb=%.3f renderer_surface_cpu_mb=%.3f renderer_surface_gpu_mb=%.3f renderer_neighbours=%llu resource_snapshot=%s resource_age_ms=%lu",
        memory.privateCommit/mb,memory.mappedCommit/mb,memory.imageCommit/mb,memory.reserved/mb,
        memory.heapsComplete,memory.heapsSummarized,memory.heapAllocated/mb,memory.heapCommitted/mb,memory.heapReserved/mb,
        memory.crtValid,memory.crtAllocated/mb,memory.crtCommitted/mb,
        memory.heapsComplete && memory.privateCommit>=memory.heapCommitted?(memory.privateCommit-memory.heapCommitted)/mb:-1.0,
        resources.hostPayload/mb,resources.mappedArchives/mb,resources.mappedSaved/mb,resources.ownedImmutable/mb,
        resources.rendererAvailable,resources.geometry/mb,resources.textures/mb,resources.animation/mb,resources.world/mb,
        resources.surfaceCpu/mb,resources.surfaceGpu/mb,resources.neighbours,
        !failure && GetCurrentThreadId()==s->gameThread?"current":"cached",resourceObserved?GetTickCount()-resourceObserved:0);
    if(gpu.local)length+=sprintf_s(line+length,sizeof(line)-length," gpu_local_usage_mb=%.3f gpu_local_budget_mb=%.3f",gpu.localUsage/mb,gpu.localBudget/mb);
    if(gpu.nonlocal)length+=sprintf_s(line+length,sizeof(line)-length," gpu_nonlocal_usage_mb=%.3f gpu_nonlocal_budget_mb=%.3f",gpu.nonlocalUsage/mb,gpu.nonlocalBudget/mb);
    if(length<=0 || size_t(length)+2>=sizeof(line))return;
    line[length++]='\r';line[length++]='\n';line[length]=0;
    if(!failure){try{Enqueue(*s,{std::string(line,length),true},true);}catch(...){}return;}
    // Allocation-free emergency record, under the same file lock as the worker.
    // Do not Drain here: its queue/combat formatting can allocate during OOM.
    if(WaitForSingleObject(s->ready,2000)!=WAIT_OBJECT_0)return;
    const DWORD begin=GetTickCount();
    while(!TryAcquireSRWLockExclusive(&s->writeLock)){
        if(GetTickCount()-begin>=2000)return;Sleep(1);
    }
    if(s->file!=INVALID_HANDLE_VALUE && s->bytes+length<=s->limit+1024ULL*1024ULL){
        DWORD written=0;
        if(WriteFile(s->file,line,length,&written,nullptr) && written==DWORD(length)){
            s->bytes+=written;FlushFileBuffers(s->file);
        }
    }
    ReleaseSRWLockExclusive(&s->writeLock);
}
void MemoryFrame(unsigned long now) noexcept {
    State* s=current.load();if(!s || s->stopping || GetCurrentThreadId()!=s->gameThread)return;
    if(MemoryTimerDue(now,s->memoryTick))MemorySnapshot("periodic");
}
void AllocationFailure(const char* source) noexcept {MemorySnapshot("alloc_failure",source);}
void AllocationFailureInCatch(const char* source) noexcept {
    try {throw;}catch(const std::bad_alloc&){AllocationFailure(source);}catch(...){}
}
void MemoryErrorText(const char* text) noexcept {
    if(text && (MemoryContains(text,"out of memory") || MemoryContains(text,"out-of-memory") ||
                MemoryContains(text,"not enough memory") || MemoryContains(text,"bad_alloc")))AllocationFailure("ReportError_text");
}

void Start() noexcept {try{StartAt(nullptr,100*1024*1024);MemorySnapshot("session_start");}catch(...) {}}
void Stop() noexcept {
    State* s=current.load();if(!s)return;
    if(s->memoryEnded)return;s->memoryEnded=true;
    MemorySnapshot("session_end");
    CombatBoundary("session_end");
    Event("SESSION","end");s->stopping=true;SetEvent(s->wake);
    WaitForSingleObject(s->thread,3000);
    // State intentionally has process lifetime: late shutdown observers remain safe.
}
void Tick(unsigned long tick) noexcept {InterlockedExchange(&gameTick,static_cast<LONG>(tick));}
std::string Quote(const char* value) noexcept {try{return Escape(value?value:"",true);}catch(...){return {};}}
void Event(const char* category,const char* event,const char* format,...) noexcept {
    State* s=current.load();if(!s||s->stopping)return;
    va_list args;va_start(args,format);
    try {
        va_list copy;va_copy(copy,args);int n=_vscprintf(format,copy);va_end(copy);
        if(n>=0){std::vector<char> text(static_cast<size_t>(n)+1);vsnprintf_s(text.data(),text.size(),_TRUNCATE,format,args);
            Enqueue(*s,Line(category,event,Utf8(text.data())),strcmp(category,"ERROR")==0||strcmp(category,"ZONE")==0||strcmp(category,"SAVE")==0||strcmp(category,"LOAD")==0);}
    }catch(...){} va_end(args);
    if(strcmp(category,"ERROR")==0)Synchronize(*s);
}
void Flush() noexcept {State* s=current.load();if(s)SetEvent(s->wake);}
void Notification(const char* type, const char* text, const char* extra) noexcept {
    if (strcmp(type, "Warning") != 0) {
        Event("ERROR", "notification", "type=%s text=%s extra=%s", Quote(type).c_str(), Quote(text).c_str(), Quote(extra).c_str());
        return;
    }
    State* s = current.load(); if (!s || s->stopping) return;
    try {
        // Match the full raw text before quoting, conversion, timestamping or disk work.
        std::string key(text ? text : ""); key += '\0'; key += extra ? extra : "";
        Lock guard(s->queueLock);
        if (s->stopping) return;
        auto it = s->notifications.find(key);
        if (it != s->notifications.end()) { ++it->second.count; return; }
        if (s->notifications.size() >= 256) {
            const DWORD now = GetTickCount();
            auto oldest = std::min_element(s->notifications.begin(), s->notifications.end(),
                [now](const auto& a, const auto& b) { return DWORD(now - a.second.time) > DWORD(now - b.second.time); });
            if (oldest->second.count) {
                auto entry = Line("ERROR", "notification_repeat", oldest->second.detail + " count=" + std::to_string(oldest->second.count));
                entry.important = false; Append(*s, std::move(entry));
            }
            s->notifications.erase(oldest);
        }
        std::string detail = Utf8(("type=" + Quote(type) + " text=" + Quote(text) + " extra=" + Quote(extra)).c_str());
        auto entry = Line("ERROR", "notification", detail); entry.important = false;
        Append(*s, std::move(entry));
        s->notifications.emplace(std::move(key), Repeat{GetTickCount(), 0, std::move(detail)});
    } catch (...) {}
}
void FlushCritical() noexcept{State* s=current.load();if(s)Synchronize(*s);}
void Crash(const wchar_t* dump,unsigned long code,const CrashEvidence::Details* details) noexcept {
    State* s=current.load();if(!s)return;
    if(WaitForSingleObject(s->ready,2000)!=WAIT_OBJECT_0)return;
    DWORD start=GetTickCount();
    while(!TryAcquireSRWLockExclusive(&s->writeLock)) {
        if(GetTickCount()-start>=2000)return;
        Sleep(1);
    }
    try {
        Drain(*s,true,true);
        std::string description="code="+std::to_string(code)+" dump="+Escape(WideUtf8(dump?dump:L""),true);
        if(details) {
            description+=" kind="+std::string(details->kind)+" type="+Escape(Utf8(details->types),true)+
                " what="+Escape(Utf8(details->message),true)+" at="+Escape(Utf8(details->at),true);
        }
        Write(*s,Line("ERROR","crash",description).line);
        if(details)for(unsigned i=0;i<details->frameCount&&i<12;++i) {
            const auto& frame=details->frames[i]; char numbers[96];
            _snprintf_s(numbers,_TRUNCATE,"n=%u rva=0x%08lX",i,frame.rva);
            Write(*s,Line("ERROR","crash_frame",std::string(numbers)+" module="+Escape(Utf8(frame.module),true)+
                " func="+Escape(Utf8(frame.function),true)+" line="+std::to_string(frame.line)+
                " file="+Escape(Utf8(frame.file),true)).line);
        }
        if(s->file!=INVALID_HANDLE_VALUE)FlushFileBuffers(s->file);
    }catch(...){}
    ReleaseSRWLockExclusive(&s->writeLock);
}
void Variable(const void* set,const char* name,bool present,float oldValue,float newValue,bool deleting) noexcept {
    if(deleting ? !present : (present && oldValue==newValue))return;
    State* s=current.load();if(!s||s->stopping)return;
    try {
        if(name && strncmp(name,"jmv.group.",10)==0 && strcmp(source,"engine")==0) {
            // The final component contains serialized group words, not a stable identity.
            std::string key(name);key.resize(key.find_last_of('.'));
            Count("GVAR",deleting?"group_delete":"group_set",key.c_str());
            return;
        }
        char numbers[160];sprintf_s(numbers," set=%p present=%d old=%.9g new=%.9g deleting=%d",set,present,oldValue,newValue,deleting);
        std::string detail="name="+Quote(name)+numbers+" source="+Quote(source)+" callback="+Quote(callback.c_str());
        std::string key=std::to_string(reinterpret_cast<size_t>(set))+":"+(name?name:"");
        Lock guard(s->queueLock);DWORD now=GetTickCount();
        if(s->stopping)return;
        auto it=s->repeats.find(key);
        if(it!=s->repeats.end() && now-it->second.time<2000) {
            // Keep the last set/delete, value and source even when every update differs.
            it->second.detail=std::move(detail);++it->second.count;return;
        }
        auto flush=[&](const Repeat& repeat){if(repeat.count)Append(*s,Line("GVAR","repeat",Utf8(repeat.detail.c_str())+" count="+std::to_string(repeat.count)));};
        if(it!=s->repeats.end())flush(it->second);
        else if(s->repeats.size()>=256) {
            // Bound memory without discarding a pending final value on eviction.
            auto oldest=std::min_element(s->repeats.begin(),s->repeats.end(),
                [now](const auto& a,const auto& b){return DWORD(now-a.second.time)>DWORD(now-b.second.time);});
            flush(oldest->second);s->repeats.erase(oldest);
        }
        Append(*s,Line("GVAR",deleting?"delete":"set",Utf8(detail.c_str())));
        s->repeats[key]={now,0,std::move(detail)};
    }catch(...){}
}
void Count(const char* category,const char* event,const char* key) noexcept {
    State* s=current.load();if(!s||s->stopping)return;
    try {
        Lock guard(s->queueLock);
        std::string id=std::string(category)+":"+event+":"+(key?key:"");
        auto it=s->counters.find(id);
        if(it!=s->counters.end()){++it->second.count;return;}
        if(s->counters.size()>=256){++s->dropped;return;}
        s->counters.emplace(id,Counter{GetTickCount(),1,category,event,key?key:""});
    }catch(...){}
}
Source::Source(const char* name) noexcept:previous(source){source=name;}
Source::~Source() noexcept{source=previous;}
Operation::Operation(const char* c,const char* n,const char* detail) noexcept:category(c),name(n),started(GetTickCount()),success(false){Event(c,"begin","operation=%s detail=%s",Quote(n).c_str(),Quote(detail).c_str());}
Operation::~Operation() noexcept{Event(category,"end","operation=%s result=%s duration_ms=%lu",Quote(name).c_str(),success?"success":"failure_or_exception",GetTickCount()-started);
    if(success && (!strcmp(category,"ZONE") || (!strcmp(category,"LOAD") && !strcmp(name,"world"))))MemorySnapshot("zone_loaded");
    FlushCritical();}
#ifdef GAME_JOURNAL_STANDALONE
void TestStart(const wchar_t* directory,unsigned long limit) noexcept{try{StartAt(directory,limit);}catch(...){}}
void TestCombatClock(unsigned long value) noexcept {testCombatTime=value;testCombatClock=true;}
void TestDrain() noexcept{State* s=current.load();if(s)Synchronize(*s);}
#endif
}
